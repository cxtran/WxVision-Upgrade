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
    };

    static constexpr unsigned long PAGE_DWELL_MS = 2200UL;
    static constexpr unsigned long SCROLL_STEP_MS = 140UL;
    static constexpr unsigned long SCROLL_PAUSE_END_MS = 500UL;
    static constexpr unsigned long ALERT_ROTATE_EXTRA_MS = 0UL;
    static constexpr int ALERT_TITLE_H = ui_theme::Layout::kTitleBarH;
    static constexpr int ALERT_BODY_Y = ui_theme::Layout::kBodyY;
    static constexpr int ALERT_LINE_H = ui_theme::Layout::kBodyLineH;
    static constexpr int ALERT_VISIBLE_LINES = ui_theme::Layout::kBodyVisibleLines;
    static constexpr int ALERT_VISIBLE_H = ui_theme::Layout::kBodyVisibleH;
    static constexpr int ALERT_WRAP_CHARS = ui_theme::Layout::kWrapCharsTiny;

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
            h = hashAppend(h, a.expires);
            h = hashAppend(h, a.headline);
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

    static String formatExpiresLocalHHMM(const String &expiresRaw)
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
            char buf[6];
            snprintf(buf, sizeof(buf), "%02d:%02d", local.hour(), local.minute());
            return String(buf);
        }

        if (expires.length() >= 16 &&
            isdigit((unsigned char)expires.charAt(11)) &&
            isdigit((unsigned char)expires.charAt(12)) &&
            isdigit((unsigned char)expires.charAt(14)) &&
            isdigit((unsigned char)expires.charAt(15)))
        {
            return expires.substring(11, 16);
        }
        return "--:--";
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
        String sevUrg = compactSeverity(a.severity) + " " + compactUrgency(a.urgency);
        String exp = "EXP " + formatExpiresLocalHHMM(a.expires);
        String what = (a.headline.length() > 0) ? a.headline : firstSentence(a.description);
        String details = extractHazardImpact(a.description);
        String action = extractAction(a.instruction, a.description);
        uint16_t sevColor = noaaSeverityColorUi(a.severity);

        AlertPage p0;
        p0.title = "ALERT";
        p0.lines.push_back(shortEventName(a.event));
        p0.lines.push_back(sevUrg);
        p0.lines.push_back(exp);
        p0.lineColors.push_back(ui_theme::noaaLinePrimary());
        p0.lineColors.push_back(sevColor);
        p0.lineColors.push_back(ui_theme::noaaLineSecondary());
        p0.scrollable = false;
        p0.titleColor = sevColor;

        outPages[0] = p0;
        outPages[1] = makePage("AREA", a.areaDesc.length() ? a.areaDesc : "N/A");
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
        rebuildAlertPagesForCurrentAlert();
    }

    static void advanceAlertPage(unsigned long nowMs)
    {
        s_alertScrollOffsetPx = 0;
        s_alertEndPause = false;
        s_alertPauseStartMs = 0;
        s_alertLastScrollMs = nowMs;
        s_alertPageStartMs = nowMs;

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

    static void renderNoaaPage()
    {
        if (!dma_display || s_alertPages.empty())
            return;

        if (s_alertPageIndex >= s_alertPages.size())
            s_alertPageIndex = 0;
        const AlertPage &page = s_alertPages[s_alertPageIndex];

        uint16_t headerBg = ui_theme::noaaHeaderBg(theme);
        uint16_t headerFg = page.titleColor ? page.titleColor : ui_theme::noaaHeaderFgFallback(theme);
        uint16_t bodyFg = (theme == 1) ? ui_theme::monoBodyText() : ui_theme::noaaTitleArea();

        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);

        dma_display->fillRect(0, ui_theme::Layout::kTitleBarY, PANEL_RES_X, ALERT_TITLE_H, headerBg);
        dma_display->setTextColor(headerFg);
        dma_display->setCursor(ui_theme::Layout::kTitleTextX, ui_theme::Layout::kTitleBarY);
        dma_display->print(page.title);

        dma_display->fillRect(0, ALERT_BODY_Y, PANEL_RES_X, PANEL_RES_Y - ALERT_BODY_Y, myBLACK);
        dma_display->setTextColor(bodyFg);

        for (size_t i = 0; i < page.lines.size(); ++i)
        {
            int y = ALERT_BODY_Y + static_cast<int>(i) * ALERT_LINE_H - s_alertScrollOffsetPx;
            if (y < ALERT_BODY_Y - 7 || y > PANEL_RES_Y - 1)
                continue;
            uint16_t lineColor = bodyFg;
            if (i < page.lineColors.size() && page.lineColors[i] != 0)
                lineColor = page.lineColors[i];
            dma_display->setTextColor(lineColor);
            dma_display->setCursor(0, y);
            dma_display->print(page.lines[i]);
        }
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
            if ((nowMs - s_alertLastScrollMs) >= SCROLL_STEP_MS)
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
