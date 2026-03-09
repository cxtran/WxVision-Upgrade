#include <Arduino.h>
#include <vector>
#include <ctype.h>
#include "display.h"
#include "noaa.h"
#include "datetimesettings.h"
#include "settings.h"
#include "ui_theme.h"

namespace
{
    struct AlertPage
    {
        String title;
        std::vector<String> lines;
        std::vector<uint16_t> lineColors;
        bool scrollable = false;
        uint16_t titleColor = 0;
        bool staged = false;
        std::vector<String> stages;
    };

    static constexpr unsigned long PAGE_DWELL_MS = 2200UL;
    static constexpr unsigned long SCROLL_STEP_MS = 55UL;
    static constexpr unsigned long SCROLL_PAUSE_END_MS = 500UL;
    static constexpr unsigned long ALERT_STAGE_DWELL_MS = 2000UL;
    static constexpr unsigned long PAGE_SCROLL_START_DELAY_MS = 2000UL;
    static constexpr unsigned long ALERT_ROTATE_EXTRA_MS = 0UL;
    static constexpr int ALERT_TITLE_H = ui_theme::Layout::kTitleBarH;
    static constexpr int ALERT_BODY_Y = ui_theme::Layout::kBodyY;
    static constexpr int ALERT_BODY_TOP_Y = ui_theme::Layout::kBodyY - 1;
    static constexpr int ALERT_LINE_H = ui_theme::Layout::kBodyLineH;
    static constexpr int ALERT_VISIBLE_LINES = ui_theme::Layout::kBodyVisibleLines;
    static constexpr int ALERT_VISIBLE_H = ui_theme::Layout::kBodyVisibleH + 1;
    static constexpr int ALERT_WRAP_CHARS = ui_theme::Layout::kWrapCharsTiny;
    static constexpr int ALERT_STAGE_TARGET_Y = ALERT_BODY_TOP_Y;
    static constexpr int ALERT_STAGE_GAP_PX = ALERT_LINE_H;

    static uint32_t s_alertSig = 0;
    static size_t s_alertCountCached = 0;
    static size_t s_alertIndex = 0;
    static uint8_t s_alertPageIndex = 0;
    static int s_alertScrollOffsetPx = 0;
    static bool s_alertEndPause = false;
    static unsigned long s_alertPageStartMs = 0;
    static unsigned long s_alertLastScrollMs = 0;
    static unsigned long s_alertPauseStartMs = 0;
    static std::vector<AlertPage> s_alertPages;
    static size_t s_alertStageIndex = 0;
    static bool s_alertStageAnimating = false;
    static int s_alertStageScrollOffsetPx = 0;

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

    static String loadingDots(unsigned long nowMs)
    {
        const unsigned long phase = (nowMs / 350UL) % 4UL;
        if (phase == 1UL)
            return ".";
        if (phase == 2UL)
            return "..";
        if (phase == 3UL)
            return "...";
        return "";
    }

    static AlertPage makeLoadingPage(const String &title, const String &line1, const String &line2, uint16_t titleColor, uint16_t lineColor)
    {
        AlertPage p;
        p.title = title;
        p.titleColor = titleColor;
        p.lines.push_back(line1);
        p.lines.push_back(line2);
        p.lineColors.push_back(lineColor);
        p.lineColors.push_back(lineColor);
        p.scrollable = false;
        return p;
    }

    static uint32_t noaaUiSignature()
    {
        uint32_t h = 2166136261u;
        size_t count = noaaAlertCount();
        h ^= static_cast<uint32_t>(count & 0xFFu);
        h *= 16777619u;
        h = hashAppend(h, noaaLastCheckHHMM());
        NwsAlert a;
        for (size_t i = 0; i < count; ++i)
        {
            if (!noaaGetAlert(i, a))
                continue;
            h = hashAppend(h, a.event);
            h = hashAppend(h, a.severity);
            h = hashAppend(h, a.urgency);
            h = hashAppend(h, a.areaDesc);
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
        return t;
    }

    static String shortEventName(const String &eventRaw)
    {
        String e = normalizeAlertText(eventRaw);
        if (e.equalsIgnoreCase("Severe Thunderstorm Warning"))
            return "SVR TSTORM";
        if (e.equalsIgnoreCase("Tornado Warning"))
            return "TORNADO";
        if (e.equalsIgnoreCase("Flash Flood Warning"))
            return "FLASH FLD";
        if (e.equalsIgnoreCase("Flood Warning"))
            return "FLOOD WARN";
        if (e.equalsIgnoreCase("Winter Storm Warning"))
            return "WTR STORM";

        if (e.length() > 16)
            e = e.substring(0, 16);
        e.trim();
        return e.length() ? e : "NOAA ALERT";
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

    static String compactSeverity(const String &severityRaw)
    {
        String s = severityRaw;
        s.toLowerCase();
        if (s == "extreme")
            return "EXT";
        if (s == "severe")
            return "SEV";
        if (s == "moderate")
            return "MOD";
        if (s == "minor")
            return "MIN";
        return "UNK";
    }

    static String compactUrgency(const String &urgencyRaw)
    {
        String s = urgencyRaw;
        s.toLowerCase();
        if (s == "immediate")
            return "IMM";
        if (s == "expected")
            return "EXP";
        if (s == "future")
            return "FUT";
        if (s == "past")
            return "PST";
        return "UNK";
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

    static String formatExpiresLocalDateTime(const String &expiresRaw)
    {
        String expires = normalizeAlertText(expiresRaw);
        DateTime utc;
        if (parseIsoDateTime(expires, utc))
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

        if (expires.length() >= 16 &&
            isdigit((unsigned char)expires.charAt(5)) &&
            isdigit((unsigned char)expires.charAt(6)) &&
            isdigit((unsigned char)expires.charAt(8)) &&
            isdigit((unsigned char)expires.charAt(9)) &&
            isdigit((unsigned char)expires.charAt(11)) &&
            isdigit((unsigned char)expires.charAt(12)) &&
            isdigit((unsigned char)expires.charAt(14)) &&
            isdigit((unsigned char)expires.charAt(15)))
        {
            return expires.substring(5, 10) + " " + expires.substring(11, 16);
        }
        return "--/-- --:--";
    }

    static std::vector<String> splitSentences(const String &textRaw)
    {
        String text = normalizeAlertText(textRaw);
        std::vector<String> out;
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
        std::vector<String> s = splitSentences(description);
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
        std::vector<String> s = splitSentences(description);
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

    static std::vector<String> wrapTextToLines(const String &textRaw, int maxCharsPerLine)
    {
        std::vector<String> lines;
        String text = normalizeAlertText(textRaw);
        if (text.length() == 0)
        {
            lines.push_back("--");
            return lines;
        }

        String line;
        line.reserve(maxCharsPerLine + 2);
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

            while (word.length() > maxCharsPerLine)
            {
                String chunk = word.substring(0, maxCharsPerLine);
                if (line.length() > 0)
                {
                    lines.push_back(line);
                    line = "";
                }
                lines.push_back(chunk);
                word = word.substring(maxCharsPerLine);
            }

            if (line.length() == 0)
            {
                line = word;
            }
            else if ((line.length() + 1 + word.length()) <= maxCharsPerLine)
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

    static AlertPage makePage(const String &title, const String &body)
    {
        AlertPage p;
        p.title = title;
        p.lines = wrapTextToLines(body, ALERT_WRAP_CHARS);
        p.lineColors.assign(p.lines.size(), 0);
        p.scrollable = static_cast<int>(p.lines.size()) > ALERT_VISIBLE_LINES;
        return p;
    }

    static void buildAlertPages(const NwsAlert &a, AlertPage outPages[5])
    {
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

        String details;
        appendSectionText(details, hazard.length() > 0 ? "Hazard" : "", hazard);
        appendSectionText(details, impacts.length() > 0 ? "Impacts" : "", impacts);
        appendSectionText(details, additionalDetails.length() > 0 ? "Additional Details" : "", additionalDetails);
        if (details.length() == 0)
            details = extractHazardImpact(a.description);
        if (details == "SEE DETAILS")
            details = normalizeAlertText(a.description);
        if (a.note.length() > 0)
        {
            if (details.length() > 0)
                details += " ";
            details += "Note: " + normalizeAlertText(a.note);
        }
        if (details.length() == 0)
            details = "No details provided.";
        String action = extractAction(a.instruction, a.description);
        if (preparedness.length() > 0)
        {
            if (action == "SEE DETAILS")
                action = "";
            appendSectionText(action, action.length() > 0 ? "Preparedness" : "", preparedness);
        }
        if (action == "SEE DETAILS" && a.note.length() > 0)
            action = normalizeAlertText(a.note);
        uint16_t sevColor = noaaSeverityColorUi(a.severity);

        AlertPage p0;
        p0.title = "ALERT";
        p0.staged = true;
        p0.stages.push_back("Event: " + normalizeAlertText(a.event));
        p0.stages.push_back("Severity: " + fullSeverity(a.severity));
        p0.stages.push_back("Urgency: " + fullUrgency(a.urgency));
        p0.stages.push_back("Expires: " + formatExpiresLocalDateTime(a.expires));
        p0.lines = wrapTextToLines(p0.stages[0], ALERT_WRAP_CHARS);
        p0.lineColors.push_back(ui_theme::noaaLinePrimary());
        p0.lineColors.push_back(sevColor);
        p0.lineColors.push_back(ui_theme::noaaLineSecondary());
        p0.scrollable = false;
        p0.titleColor = sevColor;

        outPages[0] = p0;
        String area = whereSection.length() > 0 ? whereSection : (a.areaDesc.length() ? a.areaDesc : "N/A");
        if (whenSection.length() > 0)
        {
            if (area.length() > 0)
                area += " ";
            area += "When: " + whenSection;
        }
        outPages[1] = makePage("AREA", area);
        outPages[2] = makePage("WHAT", what);
        outPages[3] = makePage("DETAILS", details);
        outPages[4] = makePage("DO THIS", action);

        outPages[1].titleColor = ui_theme::noaaTitleArea();
        outPages[2].titleColor = ui_theme::noaaTitleWhat();
        outPages[3].titleColor = sevColor;
        outPages[4].titleColor = ui_theme::noaaTitleDoThis();

        for (size_t i = 0; i < outPages[1].lineColors.size(); ++i)
            outPages[1].lineColors[i] = ui_theme::noaaTitleArea();
        for (size_t i = 0; i < outPages[2].lineColors.size(); ++i)
            outPages[2].lineColors[i] = ui_theme::noaaLineWhat();
        for (size_t i = 0; i < outPages[3].lineColors.size(); ++i)
            outPages[3].lineColors[i] = sevColor;
        for (size_t i = 0; i < outPages[4].lineColors.size(); ++i)
            outPages[4].lineColors[i] = ui_theme::noaaTitleDoThis();
    }

    static void rebuildAlertPagesForCurrentAlert()
    {
        s_alertPages.clear();
        if (noaaFetchInProgress())
        {
            AlertPage p0 = makeLoadingPage("NOAA ALERT", "GET ALERT", "INFO", ui_theme::noaaTitleInfo(), ui_theme::noaaLineInfo());
            p0.lineColors[0] = ui_theme::noaaLinePrimary();
            s_alertPages.push_back(p0);

            AlertPage p1 = makeLoadingPage("STATUS", "CHECKING", "NOAA FEED", ui_theme::noaaTitleWhat(), ui_theme::noaaLineInfo());
            s_alertPages.push_back(p1);
            return;
        }

        size_t count = noaaAlertCount();
        if (count == 0)
        {
            AlertPage p0 = makePage("NOAA ALERT", "NONE ACTIVE LAST CHECK " + noaaLastCheckHHMM());
            p0.titleColor = ui_theme::noaaTitleInfo();
            for (size_t i = 0; i < p0.lineColors.size(); ++i)
                p0.lineColors[i] = (i == 0)
                                       ? ui_theme::noaaTitleDoThis()
                                       : ui_theme::noaaTitleInfo();
            s_alertPages.push_back(p0);

            AlertPage p1 = makePage("NOAA ALERT", "MONITORING... WXVISION OK");
            p1.titleColor = ui_theme::noaaTitleInfo();
            for (size_t i = 0; i < p1.lineColors.size(); ++i)
                p1.lineColors[i] = ui_theme::noaaLineInfo();
            s_alertPages.push_back(p1);
            return;
        }

        if (s_alertIndex >= count)
            s_alertIndex = 0;
        NwsAlert a;
        if (!noaaGetAlert(s_alertIndex, a))
            return;

        AlertPage pages[5];
        buildAlertPages(a, pages);
        for (int i = 0; i < 5; ++i)
            s_alertPages.push_back(pages[i]);
    }

    static void resetAlertPager(unsigned long nowMs)
    {
        s_alertPageIndex = 0;
        s_alertScrollOffsetPx = 0;
        s_alertEndPause = false;
        s_alertLastScrollMs = nowMs;
        s_alertPauseStartMs = 0;
        s_alertPageStartMs = nowMs;
        s_alertStageIndex = 0;
        s_alertStageAnimating = false;
        s_alertStageScrollOffsetPx = 0;
        rebuildAlertPagesForCurrentAlert();
    }

    static void resetAlertPageState(unsigned long nowMs)
    {
        s_alertScrollOffsetPx = 0;
        s_alertEndPause = false;
        s_alertLastScrollMs = nowMs;
        s_alertPauseStartMs = 0;
        s_alertPageStartMs = nowMs;
        s_alertStageIndex = 0;
        s_alertStageAnimating = false;
        s_alertStageScrollOffsetPx = 0;
    }

    static void advanceAlertPage(unsigned long nowMs)
    {
        resetAlertPageState(nowMs);

        size_t count = noaaAlertCount();
        if (count == 0)
        {
            s_alertPageIndex = static_cast<uint8_t>((s_alertPageIndex + 1) % 2);
            rebuildAlertPagesForCurrentAlert();
            return;
        }

        s_alertPageIndex++;
        if (s_alertPageIndex >= 5)
        {
            s_alertPageIndex = 0;
            if (count > 0)
            {
                s_alertIndex = (s_alertIndex + 1) % count;
                rebuildAlertPagesForCurrentAlert();
            }
        }
    }

    static void stepAlertPageManual(int direction, unsigned long nowMs)
    {
        size_t count = noaaAlertCount();
        const size_t pageCount = (count == 0) ? 2u : 5u;
        if (pageCount == 0)
            return;

        int next = static_cast<int>(s_alertPageIndex) + ((direction >= 0) ? 1 : -1);
        while (next < 0)
            next += static_cast<int>(pageCount);
        next %= static_cast<int>(pageCount);
        s_alertPageIndex = static_cast<uint8_t>(next);

        resetAlertPageState(nowMs);

        if (count == 0)
        {
            rebuildAlertPagesForCurrentAlert();
            return;
        }

        if (s_alertIndex >= count)
            s_alertIndex = 0;
        rebuildAlertPagesForCurrentAlert();
    }

    static void drawWrappedLinesAt(const std::vector<String> &lines, const std::vector<uint16_t> &colors, int baseY, uint16_t defaultColor)
    {
        for (size_t i = 0; i < lines.size(); ++i)
        {
            int y = baseY + static_cast<int>(i) * ALERT_LINE_H;
            if (y < (ALERT_BODY_TOP_Y - 7) || y > PANEL_RES_Y - 1)
                continue;
            uint16_t lineColor = defaultColor;
            if (i < colors.size() && colors[i] != 0)
                lineColor = colors[i];
            dma_display->setTextColor(lineColor);
            dma_display->setCursor(1, y);
            dma_display->print(lines[i]);
        }
    }

    static uint16_t stageColorForIndex(size_t stageIndex, uint16_t bodyFg, uint16_t headerFg)
    {
        if (stageIndex == 0)
            return ui_theme::noaaLinePrimary();
        if (stageIndex == 1)
            return headerFg;
        if (stageIndex == 3)
            return ui_theme::noaaLineSecondary();
        return bodyFg;
    }

    static uint16_t stageLabelColorForIndex(size_t stageIndex, uint16_t headerFg)
    {
        if (stageIndex <= 3)
            return ui_theme::noaaLineSecondary();
        return headerFg;
    }

    static void splitStageText(const String &stageText, String &labelOut, String &valueOut)
    {
        int sep = stageText.indexOf(':');
        if (sep < 0)
        {
            labelOut = "";
            valueOut = stageText;
            return;
        }
        labelOut = stageText.substring(0, sep + 1);
        valueOut = stageText.substring(sep + 1);
        valueOut.trim();
    }

    static std::vector<String> buildStageLines(const String &stageText)
    {
        String label;
        String value;
        splitStageText(stageText, label, value);

        std::vector<String> lines;
        lines.push_back(label.length() ? label : stageText);

        std::vector<String> valueLines = wrapTextToLines(value, ALERT_WRAP_CHARS);
        if (valueLines.empty())
            valueLines.push_back("--");
        for (const String &line : valueLines)
            lines.push_back(line);

        return lines;
    }

    static int stageBlockHeightPx(const String &stageText)
    {
        std::vector<String> lines = buildStageLines(stageText);
        return static_cast<int>(lines.size()) * ALERT_LINE_H;
    }

    static int stageTransitionDistancePx(const String &stageText)
    {
        return stageBlockHeightPx(stageText) + ALERT_STAGE_GAP_PX;
    }

    static void drawStageAt(const String &stageText, size_t stageIndex, int baseY, int scrollOffsetPx, uint16_t bodyFg, uint16_t headerFg)
    {
        std::vector<String> stageLines = buildStageLines(stageText);
        const uint16_t labelColor = stageLabelColorForIndex(stageIndex, headerFg);
        const uint16_t valueColor = stageColorForIndex(stageIndex, bodyFg, headerFg);
        const int minVisibleY = ui_theme::Layout::kTitleBarY;

        for (size_t i = 0; i < stageLines.size(); ++i)
        {
            int y = baseY + static_cast<int>(i) * ALERT_LINE_H - scrollOffsetPx;
            if (y < minVisibleY || y > PANEL_RES_Y - 1)
                continue;
            dma_display->setTextColor((i == 0) ? labelColor : valueColor);
            dma_display->setCursor(1, y);
            dma_display->print(stageLines[i]);
        }
    }

    static void renderNoaaPage()
    {
        if (!dma_display || s_alertPages.empty())
            return;

        const unsigned long nowMs = millis();
        if (s_alertPageIndex >= s_alertPages.size())
            s_alertPageIndex = 0;
        const AlertPage &page = s_alertPages[s_alertPageIndex];

        uint16_t headerBg = ui_theme::noaaHeaderBg(theme);
        uint16_t headerFg = page.titleColor ? page.titleColor : ui_theme::noaaHeaderFgFallback(theme);
        uint16_t bodyFg = (theme == 1) ? ui_theme::monoBodyText() : ui_theme::noaaTitleArea();

        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);

        dma_display->fillRect(0, ALERT_BODY_TOP_Y, PANEL_RES_X, PANEL_RES_Y - ALERT_BODY_TOP_Y, myBLACK);
        dma_display->setTextColor(bodyFg);

        if (page.staged && !page.stages.empty())
        {
            size_t stageIndex = s_alertStageIndex;
            if (stageIndex >= page.stages.size())
                stageIndex = page.stages.size() - 1;

            int currentBaseY = ALERT_STAGE_TARGET_Y;
            if (s_alertStageAnimating && (stageIndex + 1) < page.stages.size())
            {
                int nextBaseY = currentBaseY + stageTransitionDistancePx(page.stages[stageIndex]);
                drawStageAt(page.stages[stageIndex], stageIndex, currentBaseY, s_alertStageScrollOffsetPx, bodyFg, headerFg);
                drawStageAt(page.stages[stageIndex + 1], stageIndex + 1, nextBaseY, s_alertStageScrollOffsetPx, bodyFg, headerFg);
            }
            else
            {
                drawStageAt(page.stages[stageIndex], stageIndex, currentBaseY, 0, bodyFg, headerFg);
            }
        }
        else
        {
            for (size_t i = 0; i < page.lines.size(); ++i)
            {
                int y = ALERT_BODY_TOP_Y + static_cast<int>(i) * ALERT_LINE_H - s_alertScrollOffsetPx;
                if (y < (ALERT_BODY_TOP_Y - 7) || y > PANEL_RES_Y - 1)
                    continue;
                uint16_t lineColor = bodyFg;
                if (i < page.lineColors.size() && page.lineColors[i] != 0)
                    lineColor = page.lineColors[i];
                String lineText = page.lines[i];
                if (noaaFetchInProgress())
                {
                    const String dots = loadingDots(nowMs);
                    if (page.title == "NOAA ALERT" && i == 1)
                        lineText = "INFO" + dots;
                    else if (page.title == "STATUS" && i == 1)
                        lineText = "NOAA FEED" + dots;
                }
                dma_display->setTextColor(lineColor);
                dma_display->setCursor(1, y);
                dma_display->print(lineText);
            }
        }

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
        if (alertCount > 0 && s_alertIndex >= alertCount)
            s_alertIndex = 0;
        resetAlertPager(nowMs);
    }

    renderNoaaPage();
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

    if (page.staged)
    {
        bool changed = false;
        if (!s_alertStageAnimating)
        {
            if ((nowMs - s_alertPageStartMs) >= ALERT_STAGE_DWELL_MS)
            {
                if ((s_alertStageIndex + 1) < page.stages.size())
                {
                    s_alertStageAnimating = true;
                    s_alertStageScrollOffsetPx = 0;
                    s_alertLastScrollMs = nowMs;
                }
                else
                {
                    advanceAlertPage(nowMs + ALERT_ROTATE_EXTRA_MS);
                }
                changed = true;
            }
        }
        else if ((nowMs - s_alertLastScrollMs) >= SCROLL_STEP_MS)
        {
            s_alertLastScrollMs = nowMs;
            s_alertStageScrollOffsetPx++;
            changed = true;
            int blockHeightPx = stageTransitionDistancePx(page.stages[s_alertStageIndex]);
            if (s_alertStageScrollOffsetPx >= blockHeightPx)
            {
                s_alertStageAnimating = false;
                s_alertStageScrollOffsetPx = 0;
                if ((s_alertStageIndex + 1) < page.stages.size())
                {
                    ++s_alertStageIndex;
                    s_alertPageStartMs = nowMs;
                }
                else
                {
                    advanceAlertPage(nowMs + ALERT_ROTATE_EXTRA_MS);
                }
            }
        }

        if (changed)
            renderNoaaPage();
        return;
    }

    int contentHeight = static_cast<int>(page.lines.size()) * ALERT_LINE_H;
    int maxOffset = contentHeight - ALERT_VISIBLE_H;
    if (maxOffset < 0)
        maxOffset = 0;

    bool changed = false;
    if (maxOffset <= 0 || !page.scrollable)
    {
        if ((nowMs - s_alertPageStartMs) >= PAGE_DWELL_MS)
        {
            advanceAlertPage(nowMs + ALERT_ROTATE_EXTRA_MS);
            changed = true;
        }
    }
    else
    {
        if (s_alertScrollOffsetPx < maxOffset)
        {
            if (s_alertScrollOffsetPx == 0 && (nowMs - s_alertPageStartMs) < PAGE_SCROLL_START_DELAY_MS)
            {
                // Hold the initial visible content long enough to read before scrolling begins.
            }
            else if ((nowMs - s_alertLastScrollMs) >= SCROLL_STEP_MS)
            {
                s_alertLastScrollMs = nowMs;
                s_alertScrollOffsetPx++;
                changed = true;
            }
        }
        else
        {
            if (!s_alertEndPause)
            {
                s_alertEndPause = true;
                s_alertPauseStartMs = nowMs;
            }
            else if ((nowMs - s_alertPauseStartMs) >= SCROLL_PAUSE_END_MS)
            {
                advanceAlertPage(nowMs + ALERT_ROTATE_EXTRA_MS);
                changed = true;
            }
        }
    }

    if (changed)
        renderNoaaPage();
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

    size_t count = noaaAlertCount();
    if (count <= 1)
        return false;

    int next = static_cast<int>(s_alertIndex) + ((direction >= 0) ? 1 : -1);
    if (next < 0 || next >= static_cast<int>(count))
        return false;

    s_alertIndex = static_cast<size_t>(next);

    resetAlertPager(millis());
    renderNoaaPage();
    return true;
}
