#include <Arduino.h>
#include <vector>
#include <ctype.h>
#include "display.h"
#include "forecast_summary.h"
#include "noaa.h"
#include "datetimesettings.h"
#include "settings.h"
#include "ui_theme.h"
#include "psram_utils.h"

#if WXV_ENABLE_NOAA_ALERTS

namespace
{
    using NoaaStringVector = std::vector<String, wxv::memory::PsramAllocator<String>>;

    struct AlertPage
    {
        String title;
        NoaaStringVector wrappedLines;
        uint16_t titleColor = 0;
        uint16_t lineColor = 0;
        uint16_t firstLineColor = 0;
        bool loop = true;
    };

    using AlertPageVector = std::vector<AlertPage, wxv::memory::PsramAllocator<AlertPage>>;

    static constexpr unsigned long PAGE_DWELL_MS = 2400UL;
    static constexpr unsigned long PAGE_CONTINUE_DWELL_MS = 1200UL;
    static constexpr unsigned long ALERT_CHAR_REVEAL_MS = 80UL;
    static constexpr unsigned long ALERT_CURSOR_BLINK_MS = 320UL;
    static constexpr int ALERT_TITLE_H = ui_theme::Layout::kTitleBarH;
    static constexpr int ALERT_BODY_TOP_Y = ui_theme::Layout::kBodyY - 1;
    static constexpr int ALERT_LINE_H = ui_theme::Layout::kBodyLineH;
    static constexpr int ALERT_VISIBLE_LINES = ui_theme::Layout::kBodyVisibleLines;
    static constexpr int ALERT_BODY_LEFT_X = 1;
    static constexpr int ALERT_BODY_WIDTH_PX = PANEL_RES_X - 2;

    static uint32_t s_alertSig = 0;
    static size_t s_alertCountCached = 0;
    static uint8_t s_alertPageIndex = 0;
    static size_t s_alertWrappedLineIndex = 0;
    static size_t s_alertPageStartLine = 0;
    static unsigned long s_alertLastPageAdvanceMs = 0;
    static unsigned long s_alertPageRevealStartMs = 0;
    static bool s_alertCompleted = false;
    static AlertPageVector s_alertPages;
    static unsigned long s_forecastRevealStartMs = 0;

    static uint32_t hashAppend(uint32_t h, const String &s)
    {
        const uint32_t kPrime = 16777619u;
        for (int i = 0; i < s.length(); ++i)
        {
            h ^= static_cast<uint8_t>(s.charAt(i));
            h *= kPrime;
        }
        return h;
    }

    static AlertPage makeLoadingPage(const String &title, const String &line1, const String &line2, uint16_t titleColor, uint16_t lineColor)
    {
        AlertPage p;
        p.title = title;
        p.titleColor = titleColor;
        p.lineColor = lineColor;
        p.wrappedLines.push_back(line1);
        p.wrappedLines.push_back(line2);
        return p;
    }

    static uint16_t alertLineColorAt(const AlertPage &page, size_t lineIndex, uint16_t defaultColor)
    {
        if (lineIndex == 0 && page.firstLineColor != 0)
            return page.firstLineColor;
        if (page.lineColor != 0)
            return page.lineColor;
        return defaultColor;
    }

    static uint32_t noaaUiSignature()
    {
        uint32_t h = 2166136261u;
        const size_t count = noaaAlertCount();
        h ^= static_cast<uint32_t>(count & 0xFFu);
        h *= 16777619u;
        h = hashAppend(h, noaaLastCheckHHMM());
        h ^= noaaFetchInProgress() ? 0xA5A5u : 0x5A5Au;
        h *= 16777619u;
        for (size_t i = 0; i < count; ++i)
        {
            NwsAlert a;
            if (!noaaGetAlert(i, a))
                continue;
            h = hashAppend(h, a.event);
            h = hashAppend(h, a.severity);
            h = hashAppend(h, a.certainty);
            h = hashAppend(h, a.urgency);
            h = hashAppend(h, a.response);
            h = hashAppend(h, a.areaDesc);
            h = hashAppend(h, a.onset);
            h = hashAppend(h, a.ends);
            h = hashAppend(h, a.expires);
            h = hashAppend(h, a.headline);
            h = hashAppend(h, a.description);
            h = hashAppend(h, a.instruction);
        }
        return h;
    }

    static String collapseWhitespace(const String &in)
    {
        String out;
        out.reserve(in.length());
        bool prevSpace = true;
        for (int i = 0; i < in.length(); ++i)
        {
            char c = in.charAt(i);
            bool isSpace = (c == ' ' || c == '\t' || c == '\r' || c == '\n');
            if (isSpace)
            {
                if (!prevSpace)
                {
                    out += ' ';
                    prevSpace = true;
                }
            }
            else
            {
                out += c;
                prevSpace = false;
            }
        }
        out.trim();
        return out;
    }

    static String compactPunctuation(const String &in)
    {
        String out;
        out.reserve(in.length());
        char prev = 0;
        for (int i = 0; i < in.length(); ++i)
        {
            char c = in.charAt(i);
            bool punct = (c == '!' || c == '?' || c == '.' || c == ',' || c == ';' || c == ':');
            if (punct && c == prev)
                continue;
            out += c;
            prev = c;
        }
        out.trim();
        return out;
    }

    static String capitalizeSentenceStarts(String in)
    {
        bool capitalizeNext = true;
        for (int i = 0; i < in.length(); ++i)
        {
            char c = in.charAt(i);
            if (isalpha(static_cast<unsigned char>(c)))
            {
                if (capitalizeNext)
                    in.setCharAt(i, static_cast<char>(toupper(static_cast<unsigned char>(c))));
                capitalizeNext = false;
            }
            else if (c == '.' || c == '!' || c == '?')
            {
                capitalizeNext = true;
            }
        }
        return in;
    }

    static void replaceAll(String &s, const char *needle, const char *value)
    {
        s.replace(needle, value);
    }

    static String normalizeAlertText(const String &raw)
    {
        String t = raw;
        replaceAll(t, "The National Weather Service", "NWS");
        replaceAll(t, "National Weather Service", "NWS");
        replaceAll(t, " has issued a ", " issued ");
        replaceAll(t, " has issued ", " issued ");
        replaceAll(t, " in effect", "");
        t = collapseWhitespace(t);
        t = compactPunctuation(t);
        t = capitalizeSentenceStarts(t);
        return t;
    }

    static String titleCaseWord(String word)
    {
        word.toLowerCase();
        if (word.length() > 0)
            word.setCharAt(0, static_cast<char>(toupper(static_cast<unsigned char>(word.charAt(0)))));
        return word;
    }

    static String fullSeverity(const String &severityRaw)
    {
        String s = normalizeAlertText(severityRaw);
        s.trim();
        return s.length() ? titleCaseWord(s) : "Unknown";
    }

    static String fullUrgency(const String &urgencyRaw)
    {
        String s = normalizeAlertText(urgencyRaw);
        s.trim();
        return s.length() ? titleCaseWord(s) : "Unknown";
    }

    static String fullResponse(const String &responseRaw)
    {
        String s = normalizeAlertText(responseRaw);
        s.trim();
        return s.length() ? titleCaseWord(s) : "Unknown";
    }

    static uint16_t noaaSeverityColorUi(const String &severityRaw)
    {
        String s = severityRaw;
        s.toLowerCase();
        if (s == "extreme")
            return ui_theme::noaaSeverityExtreme();
        if (s == "severe")
            return ui_theme::noaaSeveritySevere();
        if (s == "moderate")
            return ui_theme::noaaSeverityModerate();
        if (s == "minor")
            return ui_theme::noaaSeverityMinor();
        return ui_theme::noaaSeverityUnknown();
    }

    static bool parseIsoDateTime(const String &iso, DateTime &utcOut)
    {
        if (iso.length() < 16)
            return false;
        auto toInt2 = [&](int pos) -> int {
            if (pos + 1 >= iso.length())
                return -1;
            char a = iso.charAt(pos);
            char b = iso.charAt(pos + 1);
            if (!isdigit((unsigned char)a) || !isdigit((unsigned char)b))
                return -1;
            return (a - '0') * 10 + (b - '0');
        };
        auto toInt4 = [&](int pos) -> int {
            if (pos + 3 >= iso.length())
                return -1;
            for (int i = 0; i < 4; ++i)
                if (!isdigit((unsigned char)iso.charAt(pos + i)))
                    return -1;
            return (iso.charAt(pos) - '0') * 1000 +
                   (iso.charAt(pos + 1) - '0') * 100 +
                   (iso.charAt(pos + 2) - '0') * 10 +
                   (iso.charAt(pos + 3) - '0');
        };

        int year = toInt4(0);
        int month = toInt2(5);
        int day = toInt2(8);
        int hour = toInt2(11);
        int minute = toInt2(14);
        int second = 0;
        if (iso.length() >= 19 && isdigit((unsigned char)iso.charAt(17)) && isdigit((unsigned char)iso.charAt(18)))
            second = toInt2(17);

        if (year < 2000 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59)
            return false;

        int srcOffsetMinutes = 0;
        int zPos = iso.indexOf('Z');
        if (zPos < 0)
            zPos = iso.indexOf('z');
        if (zPos < 0)
        {
            int signPos = -1;
            for (int i = iso.length() - 1; i >= 10; --i)
            {
                char c = iso.charAt(i);
                if (c == '+' || c == '-')
                {
                    signPos = i;
                    break;
                }
            }
            if (signPos > 0 && signPos + 5 < iso.length())
            {
                int oh = toInt2(signPos + 1);
                int om = toInt2(signPos + 4);
                if (oh >= 0 && om >= 0)
                {
                    int sign = (iso.charAt(signPos) == '-') ? -1 : 1;
                    srcOffsetMinutes = sign * (oh * 60 + om);
                }
            }
        }

        DateTime src(year, month, day, hour, minute, second);
        int64_t utcEpoch = static_cast<int64_t>(src.unixtime()) - static_cast<int64_t>(srcOffsetMinutes) * 60;
        if (utcEpoch < 0)
            return false;
        utcOut = DateTime(static_cast<uint32_t>(utcEpoch));
        return true;
    }

    static String formatLocalDateTime(const String &valueRaw)
    {
        String value = normalizeAlertText(valueRaw);
        DateTime utc;
        if (parseIsoDateTime(value, utc))
        {
            int offsetMinutes = timezoneIsCustom() ? tzStandardOffset : timezoneOffsetForUtc(utc);
            int64_t localEpoch = static_cast<int64_t>(utc.unixtime()) + static_cast<int64_t>(offsetMinutes) * 60;
            if (localEpoch < 0)
                localEpoch = 0;
            DateTime local(static_cast<uint32_t>(localEpoch));
            char buf[12];
            snprintf(buf, sizeof(buf), "%02d/%02d %02d:%02d", local.month(), local.day(), local.hour(), local.minute());
            return String(buf);
        }

        if (value.length() >= 16 &&
            isdigit((unsigned char)value.charAt(5)) &&
            isdigit((unsigned char)value.charAt(6)) &&
            isdigit((unsigned char)value.charAt(8)) &&
            isdigit((unsigned char)value.charAt(9)) &&
            isdigit((unsigned char)value.charAt(11)) &&
            isdigit((unsigned char)value.charAt(12)) &&
            isdigit((unsigned char)value.charAt(14)) &&
            isdigit((unsigned char)value.charAt(15)))
        {
            return value.substring(5, 10) + " " + value.substring(11, 16);
        }
        return "--/-- --:--";
    }

    static NoaaStringVector splitSentences(const String &textRaw)
    {
        String text = normalizeAlertText(textRaw);
        NoaaStringVector out;
        String cur;
        cur.reserve(64);
        for (int i = 0; i < text.length(); ++i)
        {
            char c = text.charAt(i);
            cur += c;
            if (c == '.' || c == '!' || c == '?' || c == ';' || c == '\n')
            {
                cur.trim();
                if (cur.length())
                    out.push_back(cur);
                cur = "";
            }
        }
        cur.trim();
        if (cur.length())
            out.push_back(cur);
        return out;
    }

    static String firstSentence(const String &description)
    {
        NoaaStringVector s = splitSentences(description);
        if (!s.empty())
        {
            String one = s[0];
            if (one.length() > 60)
                one = one.substring(0, 60);
            one.trim();
            return one;
        }
        String one = normalizeAlertText(description);
        if (one.length() > 60)
            one = one.substring(0, 60);
        return one;
    }

    static String extractHazardImpact(const String &description)
    {
        NoaaStringVector s = splitSentences(description);
        String haz;
        String imp;
        for (size_t i = 0; i < s.size(); ++i)
        {
            String up = s[i];
            up.toUpperCase();
            if (haz.length() == 0 && up.indexOf("HAZARD") >= 0)
                haz = s[i];
            if (imp.length() == 0 && up.indexOf("IMPACT") >= 0)
                imp = s[i];
            if (haz.length() && imp.length())
                break;
        }

        if (haz.length() || imp.length())
        {
            String out = "";
            if (haz.length())
                out += haz;
            if (imp.length())
            {
                if (out.length())
                    out += " ";
                out += imp;
            }
            return out;
        }

        String out = "";
        int taken = 0;
        for (size_t i = 0; i < s.size() && taken < 2; ++i)
        {
            String line = s[i];
            line.trim();
            if (line.length() < 8)
                continue;
            if (out.length())
                out += " ";
            out += line;
            taken++;
        }
        if (out.length() == 0)
            out = "SEE DETAILS";
        return out;
    }

    static String trimLeadingSectionPunctuation(const String &raw)
    {
        String out = raw;
        while (out.length() > 0)
        {
            char c = out.charAt(0);
            if (c == ' ' || c == '.' || c == ':' || c == '-' || c == '*' || c == '\t')
            {
                out.remove(0, 1);
                continue;
            }
            break;
        }
        out.trim();
        return out;
    }

    static int findNextBulletSection(const String &upperText, int searchFrom)
    {
        int nextPos = upperText.indexOf("* ", searchFrom);
        while (nextPos >= 0)
        {
            if (nextPos == 0 || upperText.charAt(nextPos - 1) == ' ')
                return nextPos;
            nextPos = upperText.indexOf("* ", nextPos + 2);
        }
        return -1;
    }

    static String extractBulletSection(const String &textRaw, const char *header)
    {
        String text = normalizeAlertText(textRaw);
        if (text.length() == 0)
            return "";

        String upper = text;
        upper.toUpperCase();
        String target = String("* ") + String(header);
        target.toUpperCase();

        int start = upper.indexOf(target);
        if (start < 0)
            return "";

        int valueStart = start + target.length();
        while (valueStart < text.length())
        {
            char c = text.charAt(valueStart);
            if (c == ' ' || c == '.' || c == ':' || c == '-')
            {
                ++valueStart;
                continue;
            }
            break;
        }

        int end = findNextBulletSection(upper, valueStart);
        if (end < 0)
            end = text.length();

        String section = text.substring(valueStart, end);
        section = trimLeadingSectionPunctuation(section);
        return section;
    }

    static int findNextSectionBoundary(const String &upperText, int searchFrom)
    {
        static const char *kSectionHeaders[] = {
            "HAZARD",
            "IMPACTS",
            "IMPACT",
            "ADDITIONAL DETAILS",
            "PRECAUTIONARY/PREPAREDNESS ACTIONS",
            "PRECAUTIONARY ACTION/PREPAREDNESS ACTIONS",
            "PRECAUTIONARY ACTIONS",
            "PREPAREDNESS ACTIONS",
            "INSTRUCTIONS"};

        int nextPos = -1;
        for (const char *header : kSectionHeaders)
        {
            int pos = upperText.indexOf(header, searchFrom);
            if (pos >= 0 && (nextPos < 0 || pos < nextPos))
                nextPos = pos;
        }
        return nextPos;
    }

    static String extractSectionByHeader(const String &textRaw, const char *header)
    {
        String text = normalizeAlertText(textRaw);
        if (text.length() == 0)
            return "";

        String upper = text;
        upper.toUpperCase();
        String target(header);
        target.toUpperCase();

        int start = upper.indexOf(target);
        if (start < 0)
            return "";

        int valueStart = start + target.length();
        int end = findNextSectionBoundary(upper, valueStart);
        if (end < 0)
            end = text.length();

        String section = text.substring(valueStart, end);
        section = trimLeadingSectionPunctuation(section);
        return section;
    }

    static void appendSectionText(String &dest, const String &label, const String &value)
    {
        String cleaned = normalizeAlertText(value);
        if (cleaned.length() == 0)
            return;
        if (dest.length() > 0)
            dest += " ";
        if (label.length() > 0)
            dest += label + ": ";
        dest += cleaned;
    }

    static String extractAction(const String &instructionRaw, const String &descriptionRaw)
    {
        String instruction = normalizeAlertText(instructionRaw);
        if (instruction.length() > 0)
            return instruction;

        String desc = normalizeAlertText(descriptionRaw);
        String up = desc;
        up.toUpperCase();
        struct KeyAction
        {
            const char *key;
            const char *label;
        };
        static const KeyAction actions[] = {
            {"TAKE COVER", "TAKE COVER"},
            {"MOVE INDOORS", "MOVE INDOORS"},
            {"AVOID TRAVEL", "AVOID TRAVEL"},
            {"SEEK SHELTER", "SEEK SHELTER"},
            {"STAY INDOORS", "STAY INDOORS"},
            {"TURN AROUND", "TURN AROUND"}};
        for (const auto &a : actions)
        {
            if (up.indexOf(a.key) >= 0)
                return String(a.label);
        }
        return "SEE DETAILS";
    }

    static int noaaTextWidthPx(const String &text)
    {
        if (!dma_display || text.length() == 0)
            return static_cast<int>(text.length()) * 6;

        int16_t x1, y1;
        uint16_t w, h;
        dma_display->setFont(&Font5x7Uts);
        dma_display->getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);
        return static_cast<int>(w);
    }

    static int preferredWrapSplit(const String &word, int maxWidthPx)
    {
        const int wordLen = static_cast<int>(word.length());
        for (int i = wordLen; i >= 2; --i)
        {
            const char c = word.charAt(i - 1);
            if ((c == '-' || c == '/' || c == ',' || c == ';') &&
                noaaTextWidthPx(word.substring(0, i)) <= maxWidthPx)
                return i;
        }

        for (int i = wordLen; i >= 2; --i)
        {
            if (noaaTextWidthPx(word.substring(0, i)) <= maxWidthPx)
                return i;
        }

        return 1;
    }

    static NoaaStringVector wrapTextToPixelWidth(const String &textRaw, int maxWidthPx)
    {
        NoaaStringVector lines;
        String text = collapseWhitespace(textRaw);
        if (text.length() == 0)
        {
            lines.push_back("--");
            return lines;
        }

        String line;
        int pos = 0;
        while (pos < text.length())
        {
            while (pos < text.length() && text.charAt(pos) == ' ')
                ++pos;
            if (pos >= text.length())
                break;

            int start = pos;
            while (pos < text.length() && text.charAt(pos) != ' ')
                ++pos;
            String word = text.substring(start, pos);

            while (noaaTextWidthPx(word) > maxWidthPx)
            {
                const int splitAt = preferredWrapSplit(word, maxWidthPx);
                String chunk = word.substring(0, splitAt);
                if (line.length() > 0)
                {
                    lines.push_back(line);
                    line = "";
                }
                lines.push_back(chunk);
                word = word.substring(splitAt);
                word.trim();
            }

            if (line.length() == 0)
            {
                line = word;
            }
            else if (noaaTextWidthPx(line + " " + word) <= maxWidthPx)
            {
                line += " ";
                line += word;
            }
            else
            {
                lines.push_back(line);
                line = word;
            }
        }

        if (line.length() > 0)
            lines.push_back(line);
        if (lines.empty())
            lines.push_back("--");
        return lines;
    }

    struct AlertTextPage
    {
        size_t startLine = 0;
        size_t lineCount = 0;
    };

    static size_t pageCountForWrappedLines(const NoaaStringVector &wrappedLines)
    {
        if (ALERT_VISIBLE_LINES <= 0)
            return 1u;
        if (wrappedLines.empty())
            return 1u;
        return (wrappedLines.size() + static_cast<size_t>(ALERT_VISIBLE_LINES) - 1u) /
               static_cast<size_t>(ALERT_VISIBLE_LINES);
    }

    static size_t lastPageStartLine(const AlertPage &page)
    {
        const size_t pageCount = pageCountForWrappedLines(page.wrappedLines);
        if (pageCount == 0)
            return 0;
        return (pageCount - 1u) * static_cast<size_t>(ALERT_VISIBLE_LINES);
    }

    static AlertPage makePage(const String &title, const String &body, uint16_t titleColor = 0, uint16_t lineColor = 0)
    {
        AlertPage p;
        p.title = title;
        p.titleColor = titleColor;
        p.wrappedLines = wrapTextToPixelWidth(normalizeAlertText(body), ALERT_BODY_WIDTH_PX);
        if (p.wrappedLines.empty())
            p.wrappedLines.push_back("--");
        p.lineColor = lineColor;
        return p;
    }

    static String eventPhrase(const String &eventRaw)
    {
        String event = normalizeAlertText(eventRaw);
        event.trim();
        if (event.length() == 0)
            return "Weather alert";
        event.toLowerCase();
        return event;
    }

    static String sentenceCase(const String &raw)
    {
        String out = normalizeAlertText(raw);
        out.trim();
        if (out.length() == 0)
            return out;
        out.toLowerCase();
        out.setCharAt(0, static_cast<char>(toupper(static_cast<unsigned char>(out.charAt(0)))));
        return out;
    }

    static String urgencyPhrase(const String &urgencyRaw)
    {
        String urgency = fullUrgency(urgencyRaw);
        urgency.toLowerCase();
        if (urgency == "immediate")
            return "is in effect now";
        if (urgency == "expected")
            return "is expected";
        if (urgency == "future")
            return "is possible later";
        if (urgency == "past")
            return "was reported earlier";
        return "is in effect";
    }

    static String conciseTimeRange(const NwsAlert &a, const String &whenSection)
    {
        String when = normalizeAlertText(whenSection);
        if (when.length() > 0)
        {
            String lower = when;
            lower.toLowerCase();
            if (lower.startsWith("from "))
                when = when.substring(5);
            else if (lower.startsWith("until "))
                when = when.substring(6);
            else if (lower.startsWith("starting "))
                when = when.substring(9);
            else if (lower.startsWith("begins "))
                when = when.substring(7);
            else if (lower.startsWith("expires "))
                when = when.substring(8);
            if (when.length() > 56)
                when = when.substring(0, 56);
            return when;
        }
        if (a.onset.length() > 0 && a.ends.length() > 0)
            return formatLocalDateTime(a.onset) + " to " + formatLocalDateTime(a.ends);
        if (a.onset.length() > 0)
            return "starting " + formatLocalDateTime(a.onset);
        if (a.ends.length() > 0)
            return "until " + formatLocalDateTime(a.ends);
        if (a.expires.length() > 0)
            return "until " + formatLocalDateTime(a.expires);
        return "";
    }

    static String conciseImpactText(const String &impactsRaw, const String &descriptionRaw)
    {
        String text = impactsRaw.length() > 0 ? impactsRaw : extractHazardImpact(descriptionRaw);
        String up = normalizeAlertText(text);
        up.toUpperCase();
        if (up.indexOf("TORNADO") >= 0)
            return "A tornado may be on the ground.";
        if (up.indexOf("HEAT ILLNES") >= 0)
            return "Hot weather can cause heat illness.";
        if (up.indexOf("LARGE HAIL") >= 0)
            return "Large hail may cause injury or damage.";
        if (up.indexOf("DAMAGING WIND") >= 0 || up.indexOf("DESTRUCTIVE WIND") >= 0)
            return "Damaging winds are possible.";
        if (up.indexOf("FLASH FLOOD") >= 0 || up.indexOf("FLOODING") >= 0)
            return "Flooding is possible.";
        if (up.indexOf("GUSTY WIND") >= 0)
            return "Strong winds may cause damage.";
        if (up.indexOf("HEAVY SNOW") >= 0 || up.indexOf("BLOWING SNOW") >= 0)
            return "Snow may make travel difficult.";
        if (up.indexOf("ICE ACCUMULATION") >= 0 || up.indexOf("FREEZING RAIN") >= 0)
            return "Ice may create dangerous travel.";
        if (up.indexOf("REDUCED VISIBILITY") >= 0)
            return "Visibility may be reduced.";
        if (up.indexOf("DENSE FOG") >= 0)
            return "Dense fog may make driving dangerous.";
        if (up.indexOf("DANGEROUS SURF") >= 0)
            return "Surf conditions may be dangerous.";
        if (up.indexOf("CRITICAL FIRE WEATHER") >= 0 || up.indexOf("RAPID FIRE GROWTH") >= 0)
            return "Fire can spread quickly.";
        return "";
    }

    static String friendlyActionText(const NwsAlert &a)
    {
        String action = extractAction(a.instruction, a.description);
        if (action == "SEE DETAILS" || action.length() == 0)
        {
            String instruction = normalizeAlertText(a.instruction);
            String up = instruction;
            up.toUpperCase();
            if (up.indexOf("TAKE COVER NOW") >= 0)
                return "Take cover now.";
            if (up.indexOf("MOVE TO A BASEMENT") >= 0 || up.indexOf("INTERIOR ROOM") >= 0)
                return "Move to an interior room on the lowest floor.";
            if (up.indexOf("AVOID WINDOWS") >= 0)
                return "Stay away from windows.";
            if (up.indexOf("MOVE TO HIGHER GROUND") >= 0)
                return "Move to higher ground now.";
            if (up.indexOf("TURN AROUND") >= 0 && up.indexOf("DON'T DROWN") >= 0)
                return "Turn around, don't drive through flood water.";
            if (up.indexOf("DO NOT TRAVEL") >= 0)
                return "Avoid travel.";
            if (up.indexOf("POSTPONE TRAVEL") >= 0)
                return "Delay travel if possible.";
            if (up.indexOf("USE EXTREME CAUTION") >= 0)
                return "Use extreme caution.";
            if (up.indexOf("SECURE LOOSE OBJECTS") >= 0)
                return "Secure loose outdoor items.";
            if (up.indexOf("DO NOT BURN") >= 0)
                return "Avoid outdoor burning.";
            if (up.indexOf("REPORT SMOKE") >= 0 || up.indexOf("REPORT ANY FIRE") >= 0)
                return "Report smoke or fire immediately.";
            if (up.indexOf("DRINK PLENTY OF FLUIDS") >= 0 || up.indexOf("STAY HYDRATED") >= 0)
                return "Drink water and stay cool.";
            if (up.indexOf("AIR-CONDITIONED ROOM") >= 0 || up.indexOf("AIR CONDITIONED ROOM") >= 0)
                return "Stay in a cool indoor place.";
            if (up.indexOf("STAY OUT OF THE SUN") >= 0)
                return "Stay out of direct sun.";
            if (up.indexOf("CHECK UP ON RELATIVES") >= 0 || up.indexOf("CHECK UP ON NEIGHBORS") >= 0)
                return "Check on family and neighbors.";
            if (up.indexOf("REDUCE OUTDOOR RECREATION") >= 0 || up.indexOf("LIMIT OUTDOOR") >= 0)
                return "Limit time outdoors.";
            if (up.indexOf("TAKE FREQUENT REST BREAKS") >= 0)
                return "Take frequent breaks.";
            if (up.indexOf("CALL 9 1 1") >= 0 || up.indexOf("HEAT STROKE IS AN EMERGENCY") >= 0)
                return "Call 911 for heat stroke.";

            String response = fullResponse(a.response);
            response.toLowerCase();
            if (response == "execute")
                return "Take precautions.";
            if (response == "avoid")
                return "Avoid risky activity.";
            if (response == "monitor")
                return "Monitor conditions closely.";
            if (response == "prepare")
                return "Prepare now.";
            if (response == "shelter")
                return "Seek shelter.";
            if (response == "evacuate")
                return "Follow evacuation guidance.";

            String eventUp = normalizeAlertText(a.event);
            eventUp.toUpperCase();
            if (eventUp.indexOf("TORNADO WARNING") >= 0)
                return "Take cover now.";
            if (eventUp.indexOf("FLASH FLOOD WARNING") >= 0)
                return "Move to higher ground.";
            if (eventUp.indexOf("SEVERE THUNDERSTORM WARNING") >= 0)
                return "Move indoors and stay away from windows.";
            if (eventUp.indexOf("WINTER STORM WARNING") >= 0 || eventUp.indexOf("BLIZZARD WARNING") >= 0)
                return "Avoid travel if possible.";
            if (eventUp.indexOf("ICE STORM WARNING") >= 0)
                return "Avoid travel and watch for ice.";
            if (eventUp.indexOf("RED FLAG WARNING") >= 0)
                return "Avoid outdoor burning.";
            if (eventUp.indexOf("DENSE FOG ADVISORY") >= 0)
                return "Slow down and use low-beam headlights.";
            if (eventUp.indexOf("HIGH WIND WARNING") >= 0 || eventUp.indexOf("WIND ADVISORY") >= 0)
                return "Secure loose objects and use caution.";
            return "";
        }

        action.toLowerCase();
        if (action == "take cover")
            return "Take cover.";
        if (action == "move indoors")
            return "Move indoors.";
        if (action == "avoid travel")
            return "Avoid travel.";
        if (action == "seek shelter")
            return "Seek shelter.";
        if (action == "stay indoors")
            return "Stay indoors.";
        if (action == "turn around")
            return "Turn around, don't drive through it.";

        if (!action.endsWith("."))
            action += ".";
        if (action.length() > 0)
            action.setCharAt(0, static_cast<char>(toupper(static_cast<unsigned char>(action.charAt(0)))));
        return action;
    }

    static String buildReadableSummaryParagraph(const NwsAlert &a, const String &whenSection, const String &impactsSection)
    {
        String summary;
        String severity = fullSeverity(a.severity);
        severity.toLowerCase();
        if (severity != "unknown")
            summary += "A " + severity + " ";
        else
            summary += "A ";
        summary += eventPhrase(a.event) + " " + urgencyPhrase(a.urgency);

        String when = conciseTimeRange(a, whenSection);
        if (when.length() > 0)
        {
            String lowerWhen = when;
            lowerWhen.toLowerCase();
            if (lowerWhen.indexOf(" to ") >= 0)
                summary += " from " + when;
            else
                summary += " until " + when;
        }

        summary += ".";
        String impactText = conciseImpactText(impactsSection, a.description);
        if (impactText.length() > 0)
            summary += " " + sentenceCase(impactText);
        String actionText = friendlyActionText(a);
        if (actionText.length() > 0)
            summary += " " + actionText;
        return summary;
    }

    static void buildAlertPages(const NwsAlert &a, size_t alertIndex, size_t totalAlerts, AlertPageVector &outPages)
    {
        outPages.clear();
        String whatSection = extractBulletSection(a.description, "WHAT");
        String whereSection = extractBulletSection(a.description, "WHERE");
        String whenSection = extractBulletSection(a.description, "WHEN");
        String impactsSection = extractBulletSection(a.description, "IMPACTS");
        String additionalBulletSection = extractBulletSection(a.description, "ADDITIONAL DETAILS");
        String preparednessBulletSection = extractBulletSection(a.description, "PRECAUTIONARY/PREPAREDNESS ACTIONS");
        if (preparednessBulletSection.length() == 0)
            preparednessBulletSection = extractBulletSection(a.description, "PRECAUTIONARY ACTION/PREPAREDNESS ACTIONS");
        if (preparednessBulletSection.length() == 0)
            preparednessBulletSection = extractBulletSection(a.description, "PRECAUTIONARY ACTIONS");
        if (preparednessBulletSection.length() == 0)
            preparednessBulletSection = extractBulletSection(a.description, "PREPAREDNESS ACTIONS");

        String what = whatSection.length() > 0 ? whatSection
                                               : ((a.headline.length() > 0) ? a.headline : firstSentence(a.description));
        if (a.senderName.length() > 0)
            what = "From " + a.senderName + ". " + what;
        String hazard = extractSectionByHeader(a.description, "HAZARD");
        String impacts = impactsSection.length() > 0 ? impactsSection : extractSectionByHeader(a.description, "IMPACTS");
        if (impacts.length() == 0)
            impacts = extractSectionByHeader(a.description, "IMPACT");
        String additionalDetails = additionalBulletSection.length() > 0 ? additionalBulletSection : extractSectionByHeader(a.description, "ADDITIONAL DETAILS");
        String preparedness = preparednessBulletSection.length() > 0 ? preparednessBulletSection : extractSectionByHeader(a.description, "PRECAUTIONARY/PREPAREDNESS ACTIONS");
        if (preparedness.length() == 0)
            preparedness = extractSectionByHeader(a.description, "PRECAUTIONARY ACTION/PREPAREDNESS ACTIONS");
        if (preparedness.length() == 0)
            preparedness = extractSectionByHeader(a.description, "PRECAUTIONARY ACTIONS");
        if (preparedness.length() == 0)
            preparedness = extractSectionByHeader(a.description, "PREPAREDNESS ACTIONS");

        String detailsFallback;
        appendSectionText(detailsFallback, hazard.length() > 0 ? "Hazard" : "", hazard);
        appendSectionText(detailsFallback, impacts.length() > 0 ? "Impacts" : "", impacts);
        if (detailsFallback.length() == 0)
            detailsFallback = extractHazardImpact(a.description);
        if (detailsFallback == "SEE DETAILS")
            detailsFallback = normalizeAlertText(a.description);
        if (a.note.length() > 0)
        {
            if (detailsFallback.length() > 0)
                detailsFallback += " ";
            detailsFallback += "Note: " + normalizeAlertText(a.note);
        }
        if (detailsFallback.length() == 0)
            detailsFallback = "No details provided.";

        String action = extractAction(a.instruction, a.description);
        if (preparedness.length() > 0)
        {
            if (action == "SEE DETAILS")
                action = "";
            appendSectionText(action, action.length() > 0 ? "Preparedness" : "", preparedness);
        }
        if (action == "SEE DETAILS" && a.note.length() > 0)
            action = normalizeAlertText(a.note);

        const uint16_t sevColor = noaaSeverityColorUi(a.severity);
        const String pageSuffix = String(alertIndex + 1) + "/" + String(totalAlerts);

        AlertPage summaryPage = makePage("SUMMARY " + pageSuffix, buildReadableSummaryParagraph(a, whenSection, impactsSection), sevColor, ui_theme::noaaLineSecondary());
        outPages.push_back(summaryPage);

        if (action.length() > 0 && action != "SEE DETAILS")
        {
            AlertPage actionPage = makePage("DO THIS " + pageSuffix, action, ui_theme::noaaTitleDoThis(), ui_theme::noaaTitleDoThis());
            outPages.push_back(actionPage);
        }

        if (what.length() > 0)
        {
            AlertPage whatPage = makePage("WHAT " + pageSuffix, what, ui_theme::noaaTitleWhat(), ui_theme::noaaLineWhat());
            outPages.push_back(whatPage);
        }

        String area = whereSection.length() > 0 ? whereSection : a.areaDesc;
        if (area.length() > 0)
        {
            AlertPage areaPage = makePage("WHERE " + pageSuffix, area, ui_theme::noaaTitleArea(), ui_theme::noaaTitleArea());
            outPages.push_back(areaPage);
        }

        String whenBody;
        if (whenSection.length() > 0)
            whenBody = whenSection;
        if (a.onset.length() > 0)
            appendSectionText(whenBody, whenBody.length() > 0 ? "Starts" : "Starts", formatLocalDateTime(a.onset));
        if (a.ends.length() > 0)
            appendSectionText(whenBody, "Ends", formatLocalDateTime(a.ends));
        else if (a.expires.length() > 0)
            appendSectionText(whenBody, "Expires", formatLocalDateTime(a.expires));
        if (whenBody.length() > 0)
        {
            AlertPage whenPage = makePage("WHEN " + pageSuffix, whenBody, ui_theme::noaaLineSecondary(), ui_theme::noaaLineSecondary());
            outPages.push_back(whenPage);
        }

        String impactsBody = impacts.length() > 0 ? impacts : detailsFallback;
        if (impactsBody.length() > 0)
        {
            AlertPage impactsPage = makePage("IMPACTS " + pageSuffix, impactsBody, sevColor, sevColor);
            outPages.push_back(impactsPage);
        }

        if (additionalDetails.length() > 0)
        {
            AlertPage morePage = makePage("ADDITIONAL " + pageSuffix, additionalDetails, ui_theme::noaaTitleWhat(), ui_theme::noaaLineWhat());
            outPages.push_back(morePage);
        }
    }

    static void rebuildAlertPagesForCurrentAlert()
    {
        s_alertPages.clear();
        if (noaaFetchInProgress())
        {
            AlertPage p0 = makeLoadingPage("NOAA ALERT", "GET ALERT", "INFO", ui_theme::noaaTitleInfo(), ui_theme::noaaLineInfo());
            p0.firstLineColor = ui_theme::noaaLinePrimary();
            s_alertPages.push_back(p0);

            AlertPage p1 = makeLoadingPage("STATUS", "CHECKING", "NOAA FEED", ui_theme::noaaTitleWhat(), ui_theme::noaaLineInfo());
            s_alertPages.push_back(p1);
            return;
        }

        size_t count = noaaAlertCount();
        if (count == 0)
        {
            s_alertPages.push_back(makePage("NOAA ALERT",
                                            "No active alerts. Last check " + noaaLastCheckHHMM() + ". Monitoring NOAA feed.",
                                            ui_theme::noaaTitleInfo(),
                                            ui_theme::noaaLineInfo()));
            return;
        }

        for (size_t alertIndex = 0; alertIndex < count; ++alertIndex)
        {
            NwsAlert a;
            if (!noaaGetAlert(alertIndex, a))
                continue;

            AlertPageVector pages;
            buildAlertPages(a, alertIndex, count, pages);
            for (const AlertPage &page : pages)
                s_alertPages.push_back(page);
        }
    }

    static void resetAlertPager(unsigned long nowMs)
    {
        rebuildAlertPagesForCurrentAlert();
        s_alertPageIndex = 0;
        s_alertWrappedLineIndex = 0;
        s_alertPageStartLine = 0;
        s_alertLastPageAdvanceMs = nowMs;
        s_alertPageRevealStartMs = nowMs;
        s_alertCompleted = false;
    }

    static void resetAlertPageState(unsigned long nowMs)
    {
        s_alertWrappedLineIndex = 0;
        s_alertPageStartLine = 0;
        s_alertLastPageAdvanceMs = nowMs;
        s_alertPageRevealStartMs = nowMs;
        s_alertCompleted = false;
    }

    static void setCurrentAlertPageStart(size_t startLine, unsigned long nowMs)
    {
        s_alertPageStartLine = startLine;
        s_alertWrappedLineIndex = startLine;
        s_alertLastPageAdvanceMs = nowMs;
        s_alertPageRevealStartMs = nowMs;
        s_alertCompleted = false;
    }

    static AlertTextPage currentAlertTextPage(const AlertPage &page, size_t pageStartLine)
    {
        AlertTextPage currentPage;
        currentPage.startLine = 0;
        currentPage.lineCount = 0;
        if (page.wrappedLines.empty() || ALERT_VISIBLE_LINES <= 0)
            return currentPage;

        const size_t maxStartLine = lastPageStartLine(page);
        currentPage.startLine = min(pageStartLine, maxStartLine);
        const size_t remaining = page.wrappedLines.size() - currentPage.startLine;
        currentPage.lineCount = min(remaining, static_cast<size_t>(ALERT_VISIBLE_LINES));
        return currentPage;
    }

    static unsigned long alertPageRevealDurationMs(const AlertPage &page, size_t pageStartLine)
    {
        const AlertTextPage currentPage = currentAlertTextPage(page, pageStartLine);
        unsigned long durationMs = 0;
        for (size_t lineOffset = 0; lineOffset < currentPage.lineCount; ++lineOffset)
        {
            const size_t lineIndex = currentPage.startLine + lineOffset;
            if (lineIndex >= page.wrappedLines.size())
                break;

            const String &line = page.wrappedLines[lineIndex];
            for (int i = 0; i < line.length(); ++i)
                durationMs += (line.charAt(i) == '.') ? (ALERT_CHAR_REVEAL_MS + 320UL) : ALERT_CHAR_REVEAL_MS;
        }
        return durationMs;
    }

    static unsigned long alertCharRevealDelayMs(char c)
    {
        if (c == '.')
            return ALERT_CHAR_REVEAL_MS + 320UL;
        if (c == '!' || c == '?')
            return ALERT_CHAR_REVEAL_MS + 260UL;
        if (c == ',' || c == ';' || c == ':')
            return ALERT_CHAR_REVEAL_MS + 140UL;
        return ALERT_CHAR_REVEAL_MS;
    }

    static size_t revealedCharsForElapsedMs(const String &line, unsigned long elapsedMs, unsigned long &consumedMs)
    {
        consumedMs = 0;
        size_t visibleChars = 0;
        for (int i = 0; i < line.length(); ++i)
        {
            const unsigned long charDelayMs = alertCharRevealDelayMs(line.charAt(i));
            if ((consumedMs + charDelayMs) > elapsedMs)
                break;
            consumedMs += charDelayMs;
            ++visibleChars;
        }
        return visibleChars;
    }

    static bool alertPageEndsWithPeriod(const AlertPage &page, size_t pageStartLine)
    {
        const AlertTextPage currentPage = currentAlertTextPage(page, pageStartLine);
        if (currentPage.lineCount == 0)
            return false;

        for (size_t reverseOffset = currentPage.lineCount; reverseOffset > 0; --reverseOffset)
        {
            const size_t lineIndex = currentPage.startLine + (reverseOffset - 1u);
            if (lineIndex >= page.wrappedLines.size())
                continue;

            String line = page.wrappedLines[lineIndex];
            line.trim();
            if (line.length() == 0)
                continue;

            return line.charAt(line.length() - 1) == '.';
        }

        return false;
    }

    static unsigned long forecastCharRevealDelayMs(char c)
    {
        if (c == '.')
            return 56UL;
        if (c == '!' || c == '?')
            return 50UL;
        if (c == ',' || c == ';' || c == ':')
            return 40UL;
        return 28UL;
    }

    static size_t forecastRevealedCharsForElapsedMs(const String &line, unsigned long elapsedMs, unsigned long &consumedMs)
    {
        consumedMs = 0;
        size_t visibleChars = 0;
        for (int i = 0; i < line.length(); ++i)
        {
            const unsigned long charDelayMs = forecastCharRevealDelayMs(line.charAt(i));
            if ((consumedMs + charDelayMs) > elapsedMs)
                break;
            consumedMs += charDelayMs;
            ++visibleChars;
        }
        return visibleChars;
    }

    static void drawForecastSummaryBody(const ForecastSummaryMessage &msg, unsigned long nowMs)
    {
        const uint16_t line1Color = (theme == 1) ? ui_theme::monoHeaderFg() : ui_theme::rgb(232, 248, 255);
        const uint16_t line2Color = (theme == 1) ? ui_theme::monoBodyText() : ui_theme::rgb(150, 236, 255);
        const bool showCursor = ((nowMs / ALERT_CURSOR_BLINK_MS) % 2UL) == 0UL;
        String lines[2];
        String first = String(msg.line1);
        first.trim();
        String second = String(msg.line2);
        second.trim();

        if (first.length() > 0 && second.length() > 0 &&
            noaaTextWidthPx(first) <= ALERT_BODY_WIDTH_PX &&
            noaaTextWidthPx(second) <= ALERT_BODY_WIDTH_PX)
        {
            lines[0] = first;
            lines[1] = second;
        }
        else
        {
            String combined = first;
            if (second.length() > 0)
            {
                if (combined.length() > 0)
                    combined += " ";
                combined += second;
            }
            NoaaStringVector wrapped = wrapTextToPixelWidth(normalizeAlertText(combined), ALERT_BODY_WIDTH_PX);
            if (!wrapped.empty())
                lines[0] = wrapped[0];
            if (wrapped.size() > 1)
                lines[1] = wrapped[1];
        }

        unsigned long revealElapsedMs = nowMs - s_forecastRevealStartMs;
        bool cursorDrawn = false;

        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);

        for (int i = 0; i < 2; ++i)
        {
            if (lines[i].length() == 0)
                continue;

            String visibleLine = lines[i];
            if (msg.useTypewriter)
            {
                unsigned long consumedMs = 0;
                const size_t visibleChars = forecastRevealedCharsForElapsedMs(lines[i], revealElapsedMs, consumedMs);
                if (visibleChars < static_cast<size_t>(lines[i].length()))
                {
                    visibleLine = lines[i].substring(0, visibleChars);
                    revealElapsedMs = 0;
                }
                else
                {
                    revealElapsedMs = (revealElapsedMs > consumedMs) ? (revealElapsedMs - consumedMs) : 0;
                }

                if (!cursorDrawn && showCursor && visibleChars < static_cast<size_t>(lines[i].length()))
                {
                    const int cursorX = ALERT_BODY_LEFT_X + noaaTextWidthPx(visibleLine);
                    const int cursorY = ALERT_BODY_TOP_Y + 3 + i * ALERT_LINE_H;
                    dma_display->drawFastVLine(cursorX, cursorY, 7, (i == 0) ? line1Color : line2Color);
                    cursorDrawn = true;
                }
            }

            dma_display->setTextColor((i == 0) ? line1Color : line2Color);
            dma_display->setCursor(ALERT_BODY_LEFT_X, ALERT_BODY_TOP_Y + 3 + i * ALERT_LINE_H);
            dma_display->print(visibleLine);
        }
    }

    static void advanceAlertPage(unsigned long nowMs)
    {
        if (s_alertPages.empty())
            return;

        const AlertPage &page = s_alertPages[s_alertPageIndex];
        const size_t nextStart = s_alertPageStartLine + static_cast<size_t>(ALERT_VISIBLE_LINES);
        if (nextStart < page.wrappedLines.size())
        {
            setCurrentAlertPageStart(nextStart, nowMs);
            return;
        }

        s_alertCompleted = true;
        if ((s_alertPageIndex + 1u) < s_alertPages.size())
        {
            s_alertPageIndex = static_cast<uint8_t>(s_alertPageIndex + 1u);
            setCurrentAlertPageStart(0, nowMs);
            return;
        }

        if (page.loop)
        {
            s_alertPageIndex = 0;
            setCurrentAlertPageStart(0, nowMs);
        }
    }

    static void stepAlertPageManual(int direction, unsigned long nowMs)
    {
        if (s_alertPages.empty())
            return;

        if (direction >= 0)
        {
            advanceAlertPage(nowMs);
            return;
        }

        if (s_alertPageStartLine >= static_cast<size_t>(ALERT_VISIBLE_LINES))
        {
            setCurrentAlertPageStart(s_alertPageStartLine - static_cast<size_t>(ALERT_VISIBLE_LINES), nowMs);
            return;
        }

        if (s_alertPageIndex == 0)
            s_alertPageIndex = static_cast<uint8_t>(s_alertPages.size() - 1u);
        else
            s_alertPageIndex = static_cast<uint8_t>(s_alertPageIndex - 1u);

        setCurrentAlertPageStart(lastPageStartLine(s_alertPages[s_alertPageIndex]), nowMs);
    }

    static void drawNoaaAlertPage(const AlertPage &page, size_t pageStartLine, uint16_t defaultColor, unsigned long nowMs)
    {
        const AlertTextPage currentPage = currentAlertTextPage(page, pageStartLine);
        unsigned long revealElapsedMs = nowMs - s_alertPageRevealStartMs;
        const bool showCursor = ((nowMs / ALERT_CURSOR_BLINK_MS) % 2UL) == 0UL;
        bool cursorDrawn = false;

        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);

        for (size_t lineOffset = 0; lineOffset < currentPage.lineCount; ++lineOffset)
        {
            const size_t lineIndex = currentPage.startLine + lineOffset;
            if (lineIndex >= page.wrappedLines.size())
                break;

            const uint16_t lineColor = alertLineColorAt(page, lineIndex, defaultColor);

            const String &fullLine = page.wrappedLines[lineIndex];
            String visibleLine;
            unsigned long consumedLineMs = 0;
            const size_t visibleChars = revealedCharsForElapsedMs(fullLine, revealElapsedMs, consumedLineMs);
            if (visibleChars >= fullLine.length())
            {
                visibleLine = fullLine;
                revealElapsedMs = (revealElapsedMs > consumedLineMs) ? (revealElapsedMs - consumedLineMs) : 0;
            }
            else
            {
                visibleLine = fullLine.substring(0, visibleChars);
                revealElapsedMs = 0;
            }

            const int y = ALERT_BODY_TOP_Y + 1 + static_cast<int>(lineOffset * ALERT_LINE_H);
            dma_display->setTextColor(lineColor);
            dma_display->setCursor(ALERT_BODY_LEFT_X, y);
            dma_display->print(visibleLine);

            if (!cursorDrawn && showCursor && visibleChars < fullLine.length())
            {
                const String cursorPrefix = fullLine.substring(0, visibleChars);
                const int cursorX = ALERT_BODY_LEFT_X + noaaTextWidthPx(cursorPrefix);
                dma_display->drawFastVLine(cursorX, y, 7, lineColor);
                cursorDrawn = true;
                break;
            }
        }

        if (!cursorDrawn && showCursor && currentPage.lineCount > 0)
        {
            const size_t lastLineOffset = currentPage.lineCount - 1u;
            const size_t lastLineIndex = currentPage.startLine + lastLineOffset;
            if (lastLineIndex < page.wrappedLines.size())
            {
                const String &lastLine = page.wrappedLines[lastLineIndex];
                const uint16_t lineColor = alertLineColorAt(page, lastLineIndex, defaultColor);

                const int y = ALERT_BODY_TOP_Y + 1 + static_cast<int>(lastLineOffset * ALERT_LINE_H);
                const int cursorX = ALERT_BODY_LEFT_X + noaaTextWidthPx(lastLine);
                dma_display->drawFastVLine(cursorX, y, 7, lineColor);
            }
        }
    }

    static void renderNoaaPage(unsigned long nowMs = millis())
    {
        if (!dma_display || s_alertPages.empty())
            return;

        if (s_alertPageIndex >= s_alertPages.size())
            s_alertPageIndex = 0;
        const AlertPage &page = s_alertPages[s_alertPageIndex];

        uint16_t headerBg = ui_theme::noaaHeaderBg(theme);
        uint16_t headerFg = page.titleColor ? page.titleColor : ui_theme::noaaHeaderFgFallback(theme);
        uint16_t bodyFg = (theme == 1) ? ui_theme::monoBodyText() : ui_theme::noaaLinePrimary();

        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);

        dma_display->fillRect(0, ALERT_BODY_TOP_Y, PANEL_RES_X, PANEL_RES_Y - ALERT_BODY_TOP_Y, myBLACK);
        drawNoaaAlertPage(page, s_alertWrappedLineIndex, bodyFg, nowMs);

        // Draw title last so the top scrolling line slides behind it instead of disappearing early.
        dma_display->fillRect(0, ui_theme::Layout::kTitleBarY, PANEL_RES_X, ALERT_TITLE_H, headerBg);
        dma_display->setTextColor(headerFg);
        dma_display->setCursor(ui_theme::Layout::kTitleTextX, ui_theme::Layout::kTitleBarY);
        dma_display->print(page.title);

    }
} // namespace

void drawNoaaAlertsScreen()
{
    unsigned long nowMs = millis();
    uint32_t sig = noaaUiSignature();
    size_t alertCount = noaaAlertCount();

    if (sig != s_alertSig || alertCount != s_alertCountCached || s_alertPages.empty())
    {
        s_alertSig = sig;
        s_alertCountCached = alertCount;
        resetAlertPager(nowMs);
    }

    renderNoaaPage(nowMs);
}

void resetNoaaAlertsScreenPager()
{
    s_alertPageIndex = 0;
    s_alertWrappedLineIndex = 0;
    s_alertPageStartLine = 0;
    s_alertLastPageAdvanceMs = millis();
    s_alertPageRevealStartMs = s_alertLastPageAdvanceMs;
    s_alertCompleted = false;
}

void tickNoaaAlertsScreen()
{
    if (!dma_display)
        return;

    unsigned long nowMs = millis();
    if (s_alertPages.empty())
    {
        drawNoaaAlertsScreen();
        return;
    }

    if (s_alertPageIndex >= s_alertPages.size())
        s_alertPageIndex = 0;
    const AlertPage &page = s_alertPages[s_alertPageIndex];
    const unsigned long revealDurationMs = alertPageRevealDurationMs(page, s_alertWrappedLineIndex);
    const unsigned long pageDwellMs = alertPageEndsWithPeriod(page, s_alertWrappedLineIndex)
                                          ? PAGE_DWELL_MS
                                          : PAGE_CONTINUE_DWELL_MS;
    const unsigned long elapsedSinceRevealStart = nowMs - s_alertPageRevealStartMs;

    renderNoaaPage(nowMs);

    if (elapsedSinceRevealStart >= (revealDurationMs + pageDwellMs))
    {
        advanceAlertPage(nowMs);
        renderNoaaPage(nowMs);
    }
}

void stepNoaaAlertsScreen(int direction)
{
    if (!dma_display)
        return;
    stepAlertPageManual((direction >= 0) ? 1 : -1, millis());
    renderNoaaPage();
}

bool stepNoaaAlertSelection(int direction)
{
    if (!dma_display)
        return false;

    if (direction > 0)
        return false;

    (void)direction;

    return false;
}

void drawForecastSummaryScreen()
{
    if (!dma_display)
        return;

    const ForecastSummaryMessage &msg = currentForecastSummaryMessage();
    if (!msg.available)
        return;

    const unsigned long nowMs = millis();
    if (!forecastSummaryScreenActive())
    {
        s_forecastRevealStartMs = nowMs;
        beginForecastSummaryDisplay();
    }
    else if (s_forecastRevealStartMs == 0)
        s_forecastRevealStartMs = nowMs;

    const uint16_t headerBg = (theme == 1) ? ui_theme::monoHeaderBg() : ui_theme::rgb(0, 46, 66);
    const uint16_t headerFg = (theme == 1) ? ui_theme::monoHeaderFg() : ui_theme::rgb(118, 226, 250);

    dma_display->fillRect(0, ALERT_BODY_TOP_Y, PANEL_RES_X, PANEL_RES_Y - ALERT_BODY_TOP_Y, myBLACK);
    drawForecastSummaryBody(msg, nowMs);
    dma_display->fillRect(0, ui_theme::Layout::kTitleBarY, PANEL_RES_X, ALERT_TITLE_H, headerBg);
    dma_display->setTextColor(headerFg);
    dma_display->setCursor(ui_theme::Layout::kTitleTextX, ui_theme::Layout::kTitleBarY);
    dma_display->print(msg.title);
}

void tickForecastSummaryScreen()
{
    drawForecastSummaryScreen();
}

#else

void drawNoaaAlertsScreen() {}

void drawForecastSummaryScreen() {}

void tickForecastSummaryScreen() {}

void resetNoaaAlertsScreenPager() {}

void tickNoaaAlertsScreen() {}

void stepNoaaAlertsScreen(int direction)
{
    (void)direction;
}

bool stepNoaaAlertSelection(int direction)
{
    (void)direction;
    return false;
}

#endif
