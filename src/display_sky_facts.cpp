#include "display_sky_facts.h"

#include <ctype.h>
#include <math.h>
#include <string.h>
#include <vector>

#include "astronomy.h"
#include "display.h"
#include "psram_utils.h"
#include "render_scheduler.h"
#include "settings.h"
#include "ui_theme.h"

#if WXV_ENABLE_ASTRONOMY || WXV_ENABLE_SKY_BRIEF
namespace
{
using SkyStringVector = std::vector<String, wxv::memory::PsramAllocator<String>>;

constexpr int kSummaryRepeatGapPx = 4;
constexpr unsigned long kSummaryStartDelayMs = 2000UL;
constexpr unsigned long kSkyBriefParagraphPageDwellMs = 2400UL;
constexpr unsigned long kSkyBriefParagraphContinueDwellMs = 1200UL;
constexpr unsigned long kSkyBriefParagraphCharRevealMs = 80UL;
constexpr unsigned long kSkyBriefParagraphCursorBlinkMs = 320UL;
constexpr int kSkyBriefTitleH = ui_theme::Layout::kTitleBarH;
constexpr int kSkyBriefBodyTopY = ui_theme::Layout::kBodyY - 1;
constexpr int kSkyBriefLineH = ui_theme::Layout::kBodyLineH;
constexpr int kSkyBriefVisibleLines = ui_theme::Layout::kBodyVisibleLines;
constexpr int kSkyBriefBodyLeftX = 1;
constexpr int kSkyBriefBodyWidthPx = PANEL_RES_X - 2;

int s_skyFactsLastTheme = -1;
int s_skyFactsLastDateKey = -1;
int s_skyFactsLastMinute = -1;
int s_summaryMarqueeOffset = 0;
int s_summaryMarqueeWidth = 0;
unsigned long s_summaryMarqueeLastStepMs = 0;
unsigned long s_summaryMarqueeStartAfterMs = 0;
unsigned long s_summaryStepMs = 0;
char s_summaryLastText[wxv::astronomy::kSkyFactMarqueeLen] = "";
char s_skyBriefLastMarquee[wxv::astronomy::kSkyFactMarqueeLen] = "";

struct SkyBriefTextPage
{
    size_t startLine = 0;
    size_t lineCount = 0;
};

struct SkyBriefSection
{
    String title;
    String text;
};

struct SkyBriefSubpage
{
    String title;
    SkyStringVector lines;
};

using SkyBriefSubpageVector = std::vector<SkyBriefSubpage, wxv::memory::PsramAllocator<SkyBriefSubpage>>;

SkyStringVector s_skyBriefWrappedLines;
String s_skyBriefParagraphText;
size_t s_skyBriefParagraphStartLine = 0;
unsigned long s_skyBriefParagraphRevealStartMs = 0;
unsigned long s_skyBriefParagraphLastAdvanceMs = 0;
bool s_skyBriefParagraphCompleted = false;
SkyBriefSubpageVector s_skyBriefSubpages;
size_t s_skyBriefSubpageIndex = 0;
size_t s_skyBriefVisibleChars = 0;
unsigned long s_skyBriefNextCharMs = 0;

SkyStringVector wrapSkyBriefText(const String &textRaw, int maxWidthPx);
std::vector<SkyBriefTextPage> paginateSkyBriefLines(const SkyStringVector &wrappedLines, size_t linesPerPage);

SkyStringVector splitSummaryPhrases(const char *raw)
{
    SkyStringVector phrases;
    if (!raw || raw[0] == '\0')
        return phrases;

    String text(raw);
    text.replace("Â", "");
    text.replace("¦", "|");
    int startPos = 0;
    while (startPos < text.length())
    {
        int sep = text.indexOf(" ¦ ", startPos);
        String part = (sep >= 0) ? text.substring(startPos, sep) : text.substring(startPos);
        part.trim();
        if (part.length() > 0 && !part.equalsIgnoreCase("NOW"))
            phrases.push_back(part);
        if (sep < 0)
            break;
        startPos = sep + 3;
    }
    return phrases;
}

SkyStringVector splitSummaryPhrasesClean(const char *raw)
{
    SkyStringVector phrases = splitSummaryPhrases(raw);
    if (!phrases.empty())
        return phrases;

    SkyStringVector fallback;
    if (!raw || raw[0] == '\0')
        return fallback;

    String text(raw);
    text.replace("¦", "|");
    text.replace(" | ", "|");
    int startPos = 0;
    while (startPos < text.length())
    {
        int sep = text.indexOf('|', startPos);
        String part = (sep >= 0) ? text.substring(startPos, sep) : text.substring(startPos);
        part.trim();
        if (part.length() > 0 && !part.equalsIgnoreCase("NOW"))
            fallback.push_back(part);
        if (sep < 0)
            break;
        startPos = sep + 1;
    }
    return fallback;
}

const char *summaryMarqueeText(const wxv::astronomy::SkyFactPage &page)
{
    return page.marquee ? page.marquee : "";
}

String normalizeSummaryPhrase(String phrase)
{
    const char *kEllipsisToken = "{ELLIPSIS}";
    phrase.trim();
    phrase.replace("Â", "");
    phrase.replace("Â¦", " ");
    phrase.replace("¦", " ");
    phrase.replace("|", " ");
    phrase.replace("NOW ", "");
    phrase.replace("Sun Times", "Sun times are");
    phrase.replace("Moonrise ", "Moonrise is ");
    phrase.replace("Moonset ", "Moonset is ");
    phrase.replace("Daylight ", "Daylight lasts ");
    phrase.replace("Day ", "Today is day ");
    phrase.replace(" day left", " day remains in the year");
    phrase.replace(" days left", " days remain in the year");
    phrase.replace(" day remain in the year ", " day remains in the year. ");
    phrase.replace(" days remain in the year ", " days remain in the year. ");
    phrase.replace("above horizon", "above the horizon");
    phrase.replace("below horizon", "below the horizon");
    phrase.replace("Sun set ", "Sunset was ");
    phrase.replace("Moon Position", "Moon position is");
    phrase.replace("Sun Position", "Sun position is");
    phrase.replace("First Quarter", "First Quarter Moon");
    phrase.replace("Last Quarter", "Last Quarter Moon");
    phrase.replace("Waxing Crescent", "Waxing Crescent Moon");
    phrase.replace("Waxing Gibbous", "Waxing Gibbous Moon");
    phrase.replace("Waning Gibbous", "Waning Gibbous Moon");
    phrase.replace("Waning Crescent", "Waning Crescent Moon");
    phrase.replace("  ", " ");
    phrase.replace(" ,", ",");
    phrase.replace(" .", ".");
    phrase.replace(" ;", ";");
    phrase.replace(" :", ":");
    phrase.replace("...", kEllipsisToken);
    phrase.replace("..", ".");
    phrase.replace(",,", ",");
    phrase.replace(kEllipsisToken, "...");
    if (phrase.endsWith("day remains in the year") || phrase.endsWith("days remain in the year"))
        phrase += ".";
    phrase.trim();
    return phrase;
}

String finalizeSummaryParagraph(String paragraph)
{
    const char *kEllipsisToken = "{ELLIPSIS}";
    paragraph.replace("  ", " ");
    paragraph.replace(" .", ".");
    paragraph.replace(" ,", ",");
    paragraph.replace(" ;", ";");
    paragraph.replace(" :", ":");
    paragraph.replace("...", kEllipsisToken);
    paragraph.replace("..", ".");
    paragraph.replace(".,", ".");
    paragraph.replace(",.", ".");
    paragraph.replace(kEllipsisToken, "...");
    paragraph.trim();
    if (paragraph.length() > 0)
    {
        char tail = paragraph.charAt(paragraph.length() - 1);
        if (tail != '.' && tail != '!' && tail != '?')
            paragraph += ".";
    }
    return paragraph;
}

String expandSkyBriefTimeUnits(String text)
{
    text.replace(" d ", " days ");
    text.replace(" h ", " hour ");
    text.replace(" m ", " minute ");
    text.replace("D ", "Days ");

    for (int i = 0; i < text.length(); ++i)
    {
        if (!isDigit(text.charAt(i)))
            continue;

        int unitIndex = i + 1;
        while (unitIndex < text.length() && isDigit(text.charAt(unitIndex)))
            ++unitIndex;
        if (unitIndex >= text.length())
            continue;

        const char unit = text.charAt(unitIndex);
        const bool boundaryAfter = (unitIndex + 1 >= text.length()) || !isAlphaNumeric(text.charAt(unitIndex + 1));
        if (!boundaryAfter)
            continue;

        if (unit == 'd')
        {
            text.remove(unitIndex, 1);
            int valueStart = i;
            while (valueStart > 0 && isDigit(text.charAt(valueStart - 1)))
                --valueStart;
            const int value = text.substring(valueStart, unitIndex).toInt();
            const String unitWord = (value == 1) ? " day" : " days";
            text = text.substring(0, unitIndex) + unitWord + text.substring(unitIndex);
            i = unitIndex + unitWord.length() - 1;
        }
        else if (unit == 'h' || unit == 'H')
        {
            text.remove(unitIndex, 1);
            int valueStart = i;
            while (valueStart > 0 && isDigit(text.charAt(valueStart - 1)))
                --valueStart;
            const int value = text.substring(valueStart, unitIndex).toInt();
            const String unitWord = (value == 1) ? " hour" : " hours";
            text = text.substring(0, unitIndex) + unitWord + text.substring(unitIndex);
            i = unitIndex + unitWord.length() - 1;
        }
        else if (unit == 'm' || unit == 'M')
        {
            text.remove(unitIndex, 1);
            int valueStart = i;
            while (valueStart > 0 && isDigit(text.charAt(valueStart - 1)))
                --valueStart;
            const int value = text.substring(valueStart, unitIndex).toInt();
            const String unitWord = (value == 1) ? " minute" : " minutes";
            text = text.substring(0, unitIndex) + unitWord + text.substring(unitIndex);
            i = unitIndex + unitWord.length() - 1;
        }
    }

    text.replace("  ", " ");
    text.trim();
    return text;
}

SkyStringVector splitSummaryPhrasesRobust(const char *raw)
{
    SkyStringVector phrases;
    if (!raw || raw[0] == '\0')
        return phrases;

    String text(raw);
    text.replace("Ã‚", "");
    text.replace("Â", "");
    text.replace("¦", "|");
    text.replace("Â¦", "|");
    text.replace(" | ", "|");
    text.replace("| ", "|");
    text.replace(" |", "|");
    text.trim();

    int startPos = 0;
    while (startPos < text.length())
    {
        int sep = text.indexOf('|', startPos);
        String part = (sep >= 0) ? text.substring(startPos, sep) : text.substring(startPos);
        part.trim();
        if (part.startsWith("NOW "))
            part.remove(0, 4);
        else if (part.equalsIgnoreCase("NOW"))
            part = "";
        part.trim();
        if (part.length() > 0)
            phrases.push_back(part);
        if (sep < 0)
            break;
        startPos = sep + 1;
    }

    if (phrases.empty())
    {
        String single(raw);
        single.replace("Ã‚", "");
        single.replace("Â", "");
        single.replace("¦", " ");
        single.replace("|", " ");
        if (single.startsWith("NOW "))
            single.remove(0, 4);
        single.trim();
        if (single.length() > 0)
            phrases.push_back(single);
    }

    return phrases;
}

void buildSummaryLayout(const wxv::astronomy::SkyFactPage &page, String &line1, String &line2, String &ticker)
{
    const char *marquee = summaryMarqueeText(page);
    if (page.line1[0] != '\0')
        line1 = page.line1;
    if (page.line2[0] != '\0')
        line2 = page.line2;
    if (marquee[0] != '\0')
        ticker = marquee;

    if (line1.length() > 0 || line2.length() > 0)
        return;

    SkyStringVector phrases = splitSummaryPhrasesRobust(marquee);
    if (!phrases.empty())
        line1 = normalizeSummaryPhrase(phrases[0]);
    if (phrases.size() > 1)
        line2 = normalizeSummaryPhrase(phrases[1]);

    ticker = "";
    for (size_t i = 2; i < phrases.size(); ++i)
    {
        if (ticker.length() > 0)
            ticker += " ¦ ";
        ticker += normalizeSummaryPhrase(phrases[i]);
    }
}

String buildSummaryParagraph(const wxv::astronomy::SkyFactPage &page)
{
    SkyStringVector phrases = splitSummaryPhrasesRobust(summaryMarqueeText(page));
    String paragraph;
    for (size_t i = 0; i < phrases.size(); ++i)
    {
        String phrase = expandSkyBriefTimeUnits(normalizeSummaryPhrase(phrases[i]));
        if (phrase.length() == 0)
            continue;

        bool sentenceEnded = false;
        if (phrase.length() > 0)
        {
            const char tail = phrase.charAt(phrase.length() - 1);
            sentenceEnded = (tail == '.' || tail == '!' || tail == '?');
        }

        if (paragraph.length() > 0)
        {
            if (phrase.startsWith("Today is day ") && !paragraph.endsWith(".") && !paragraph.endsWith("!") && !paragraph.endsWith("?"))
                paragraph += ".";
            paragraph += " ";
        }
        paragraph += phrase;
        if (!sentenceEnded)
            paragraph += ".";
    }

    if (paragraph.length() == 0)
        paragraph = "Sky facts unavailable.";
    return finalizeSummaryParagraph(paragraph);
}

int skyBriefSectionIndexForPhrase(const String &phrase)
{
    if (phrase.indexOf("DST") >= 0 || phrase.indexOf("peak") >= 0 ||
        phrase.indexOf("Standard time") >= 0 || phrase.indexOf("weekend") >= 0)
        return 3; // Events
    if (phrase.startsWith("Spring") || phrase.startsWith("Summer") || phrase.startsWith("Autumn") ||
        phrase.startsWith("Winter"))
        return 0; // Season
    if (phrase.startsWith("Daylight") || phrase.startsWith("Sun") || phrase.startsWith("Earth-Sun"))
        return 1; // Sun
    if (phrase.indexOf("Moon") >= 0)
        return 2; // Moon
    if (phrase.indexOf("Today is day") >= 0 || phrase.startsWith("Week ") || phrase.startsWith("Quarter ") ||
        phrase.startsWith("Weekend") || phrase.startsWith("It is the weekend") ||
        phrase.indexOf("remain in ") >= 0)
        return 4; // Calendar
    return 4;     // Calendar
}

void appendSkyBriefSentence(String &text, const String &phraseRaw)
{
    String phrase = phraseRaw;
    if (phrase.length() == 0)
        return;

    bool sentenceEnded = false;
    const char tail = phrase.charAt(phrase.length() - 1);
    sentenceEnded = (tail == '.' || tail == '!' || tail == '?');

    if (text.length() > 0)
    {
        if (phrase.startsWith("Today is day ") && !text.endsWith(".") && !text.endsWith("!") && !text.endsWith("?"))
            text += ".";
        text += " ";
    }
    text += phrase;
    if (!sentenceEnded)
        text += ".";
}

SkyBriefSubpageVector buildSkyBriefSubpages(const wxv::astronomy::SkyFactPage &page)
{
    SkyBriefSubpageVector subpages;
    std::vector<SkyBriefSection> sections = {
        {"SEASON", ""},
        {"SUN", ""},
        {"MOON", ""},
        {"EVENTS", ""},
        {"CALENDAR", ""}};

    SkyStringVector phrases = splitSummaryPhrasesRobust(summaryMarqueeText(page));
    for (size_t i = 0; i < phrases.size(); ++i)
    {
        const String normalized = expandSkyBriefTimeUnits(normalizeSummaryPhrase(phrases[i]));
        if (normalized.length() == 0)
            continue;
        const int sectionIndex = skyBriefSectionIndexForPhrase(normalized);
        appendSkyBriefSentence(sections[sectionIndex].text, normalized);
    }

    if (sections[3].text.length() == 0)
        sections[3].text = "No special sky events right now.";

    for (size_t i = 0; i < sections.size(); ++i)
    {
        String sectionText = finalizeSummaryParagraph(sections[i].text);
        if (sectionText.length() == 0)
            continue;

        const SkyStringVector wrapped = wrapSkyBriefText(sectionText, kSkyBriefBodyWidthPx);
        const std::vector<SkyBriefTextPage> pages = paginateSkyBriefLines(wrapped, kSkyBriefVisibleLines);
        for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex)
        {
            SkyBriefSubpage subpage;
            subpage.title = (pages.size() > 1)
                                ? (sections[i].title + " " + String(pageIndex + 1) + "/" + String(pages.size()))
                                : sections[i].title;
            for (size_t lineOffset = 0; lineOffset < pages[pageIndex].lineCount; ++lineOffset)
            {
                const size_t lineIndex = pages[pageIndex].startLine + lineOffset;
                if (lineIndex < wrapped.size())
                    subpage.lines.push_back(wrapped[lineIndex]);
            }
            if (!subpage.lines.empty())
                subpages.push_back(subpage);
        }
    }

    if (subpages.empty())
    {
        SkyBriefSubpage fallback;
        fallback.title = "SKY BRIEF";
        fallback.lines.push_back("Sky facts unavailable.");
        subpages.push_back(fallback);
    }

    return subpages;
}

uint16_t titleBg()
{
    return ui_theme::isNightTheme() ? ui_theme::monoHeaderBg() : ui_theme::infoScreenHeaderBg();
}

uint16_t titleFg()
{
    return ui_theme::isNightTheme() ? ui_theme::monoHeaderFg() : ui_theme::infoScreenHeaderFg();
}

uint16_t bodyColor()
{
    return ui_theme::isNightTheme() ? ui_theme::monoBodyText() : ui_theme::infoValueDay();
}

uint16_t pageAccentColor(wxv::astronomy::SkyFactType type)
{
    switch (type)
    {
    case wxv::astronomy::SkyFactType::Season:
        return ui_theme::applyGraphicColor(dma_display->color565(110, 220, 120));
    case wxv::astronomy::SkyFactType::EquinoxSolstice:
        return ui_theme::applyGraphicColor(dma_display->color565(110, 220, 255));
    case wxv::astronomy::SkyFactType::Daylight:
        return ui_theme::applyGraphicColor(dma_display->color565(255, 225, 110));
    case wxv::astronomy::SkyFactType::SunCountdown:
        return ui_theme::applyGraphicColor(dma_display->color565(255, 190, 90));
    case wxv::astronomy::SkyFactType::Moon:
        return ui_theme::applyGraphicColor(dma_display->color565(214, 226, 250));
    case wxv::astronomy::SkyFactType::Summary:
    case wxv::astronomy::SkyFactType::YearProgress:
    default:
        return ui_theme::applyGraphicColor(dma_display->color565(170, 220, 235));
    }
}

uint16_t trendColor(int8_t trend)
{
    if (trend > 0)
        return ui_theme::applyGraphicColor(dma_display->color565(90, 220, 110));
    if (trend < 0)
        return ui_theme::applyGraphicColor(dma_display->color565(255, 120, 90));
    return bodyColor();
}

void drawHeader(const char *title)
{
    dma_display->fillScreen(0);
    dma_display->fillRect(0, 0, PANEL_RES_X, 8, titleBg());
    dma_display->drawFastHLine(0, 7, PANEL_RES_X, ui_theme::isNightTheme() ? ui_theme::monoUnderline() : ui_theme::infoUnderlineDay());
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    dma_display->setTextColor(titleFg());

    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    int x = (PANEL_RES_X - static_cast<int>(w)) / 2;
    if (x < 0)
        x = 0;
    dma_display->setCursor(x, 0);
    dma_display->print(title);
}

void drawCenteredLine(int y, const char *text, uint16_t color)
{
    if (!text || text[0] == '\0')
        return;

    dma_display->setTextColor(color);
    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    int x = (PANEL_RES_X - static_cast<int>(w)) / 2;
    if (x < 0)
        x = 0;
    dma_display->setCursor(x, y);
    dma_display->print(text);
}

int skyBriefTextWidthPx(const String &text)
{
    if (!dma_display || text.length() == 0)
        return static_cast<int>(text.length()) * 6;

    int16_t x1, y1;
    uint16_t w, h;
    dma_display->setFont(&Font5x7Uts);
    dma_display->getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);
    return static_cast<int>(w);
}

int preferredSkyBriefWrapSplit(const String &word, int maxWidthPx)
{
    const int wordLen = static_cast<int>(word.length());
    for (int i = wordLen; i >= 2; --i)
    {
        const char c = word.charAt(i - 1);
        if ((c == '-' || c == '/' || c == ',' || c == ';') &&
            skyBriefTextWidthPx(word.substring(0, i)) <= maxWidthPx)
            return i;
    }

    for (int i = wordLen; i >= 2; --i)
    {
        if (skyBriefTextWidthPx(word.substring(0, i)) <= maxWidthPx)
            return i;
    }
    return 1;
}

SkyStringVector wrapSkyBriefText(const String &textRaw, int maxWidthPx)
{
    SkyStringVector lines;
    String text = textRaw;
    text.trim();
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

        int end = text.indexOf(' ', pos);
        if (end < 0)
            end = text.length();
        String word = text.substring(pos, end);
        pos = end + 1;

        if (line.length() == 0)
        {
            if (skyBriefTextWidthPx(word) <= maxWidthPx)
            {
                line = word;
                continue;
            }

            String remainder = word;
            while (remainder.length() > 0)
            {
                int split = preferredSkyBriefWrapSplit(remainder, maxWidthPx);
                String part = remainder.substring(0, split);
                remainder.remove(0, split);
                remainder.trim();
                lines.push_back(part);
            }
            continue;
        }

        const String candidate = line + " " + word;
        if (skyBriefTextWidthPx(candidate) <= maxWidthPx)
        {
            line = candidate;
            continue;
        }

        lines.push_back(line);
        line = "";
        if (skyBriefTextWidthPx(word) <= maxWidthPx)
        {
            line = word;
        }
        else
        {
            String remainder = word;
            while (remainder.length() > 0)
            {
                int split = preferredSkyBriefWrapSplit(remainder, maxWidthPx);
                String part = remainder.substring(0, split);
                remainder.remove(0, split);
                remainder.trim();
                lines.push_back(part);
            }
        }
    }

    if (line.length() > 0)
        lines.push_back(line);

    if (lines.empty())
        lines.push_back("--");
    return lines;
}

std::vector<SkyBriefTextPage> paginateSkyBriefLines(const SkyStringVector &wrappedLines, size_t linesPerPage)
{
    std::vector<SkyBriefTextPage> pages;
    if (wrappedLines.empty() || linesPerPage == 0)
        return pages;

    for (size_t start = 0; start < wrappedLines.size(); start += linesPerPage)
    {
        SkyBriefTextPage page;
        page.startLine = start;
        page.lineCount = std::min(linesPerPage, wrappedLines.size() - start);
        pages.push_back(page);
    }
    return pages;
}

SkyBriefTextPage currentSkyBriefTextPage(size_t pageStartLine)
{
    const std::vector<SkyBriefTextPage> pages = paginateSkyBriefLines(s_skyBriefWrappedLines, kSkyBriefVisibleLines);
    SkyBriefTextPage currentPage;
    if (pages.empty())
        return currentPage;
    currentPage = pages[0];
    for (const SkyBriefTextPage &candidate : pages)
    {
        if (candidate.startLine == pageStartLine)
        {
            currentPage = candidate;
            break;
        }
    }
    return currentPage;
}

size_t lastSkyBriefPageStartLine()
{
    const std::vector<SkyBriefTextPage> pages = paginateSkyBriefLines(s_skyBriefWrappedLines, kSkyBriefVisibleLines);
    if (pages.empty())
        return 0;
    return pages.back().startLine;
}

unsigned long skyBriefCharRevealDelayMs(char c)
{
    if (c == '.')
        return kSkyBriefParagraphCharRevealMs + 320UL;
    if (c == '!' || c == '?')
        return kSkyBriefParagraphCharRevealMs + 260UL;
    if (c == ',' || c == ';' || c == ':')
        return kSkyBriefParagraphCharRevealMs + 140UL;
    return kSkyBriefParagraphCharRevealMs;
}

size_t revealedSkyBriefCharsForElapsedMs(const String &line, unsigned long elapsedMs, unsigned long &consumedMs)
{
    consumedMs = 0;
    size_t visibleChars = 0;
    for (int i = 0; i < line.length(); ++i)
    {
        const unsigned long charDelayMs = skyBriefCharRevealDelayMs(line.charAt(i));
        if ((consumedMs + charDelayMs) > elapsedMs)
            break;
        consumedMs += charDelayMs;
        ++visibleChars;
    }
    return visibleChars;
}

unsigned long skyBriefRevealDurationMs(size_t pageStartLine)
{
    (void)pageStartLine;
    unsigned long durationMs = 0;
    if (s_skyBriefSubpages.empty() || s_skyBriefSubpageIndex >= s_skyBriefSubpages.size())
        return durationMs;

    const SkyBriefSubpage &currentSubpage = s_skyBriefSubpages[s_skyBriefSubpageIndex];
    for (size_t lineOffset = 0; lineOffset < currentSubpage.lines.size(); ++lineOffset)
    {
        const String &line = currentSubpage.lines[lineOffset];
        for (int i = 0; i < line.length(); ++i)
            durationMs += skyBriefCharRevealDelayMs(line.charAt(i));
    }
    return durationMs;
}

bool skyBriefPageEndsWithPeriod(size_t pageStartLine)
{
    (void)pageStartLine;
    if (s_skyBriefSubpages.empty() || s_skyBriefSubpageIndex >= s_skyBriefSubpages.size())
        return false;

    const SkyBriefSubpage &currentSubpage = s_skyBriefSubpages[s_skyBriefSubpageIndex];
    if (currentSubpage.lines.empty())
        return false;

    for (size_t reverseOffset = currentSubpage.lines.size(); reverseOffset > 0; --reverseOffset)
    {
        String line = currentSubpage.lines[reverseOffset - 1u];
        line.trim();
        if (line.length() == 0)
            continue;
        return line.charAt(line.length() - 1) == '.';
    }
    return false;
}

size_t skyBriefSubpageTotalChars()
{
    if (s_skyBriefSubpages.empty() || s_skyBriefSubpageIndex >= s_skyBriefSubpages.size())
        return 0;

    size_t total = 0;
    const SkyBriefSubpage &currentSubpage = s_skyBriefSubpages[s_skyBriefSubpageIndex];
    for (size_t i = 0; i < currentSubpage.lines.size(); ++i)
        total += currentSubpage.lines[i].length();
    return total;
}

char skyBriefCharAtVisibleIndex(size_t visibleIndex)
{
    if (s_skyBriefSubpages.empty() || s_skyBriefSubpageIndex >= s_skyBriefSubpages.size())
        return '\0';

    const SkyBriefSubpage &currentSubpage = s_skyBriefSubpages[s_skyBriefSubpageIndex];
    size_t offset = visibleIndex;
    for (size_t i = 0; i < currentSubpage.lines.size(); ++i)
    {
        const String &line = currentSubpage.lines[i];
        if (offset < static_cast<size_t>(line.length()))
            return line.charAt(offset);
        offset -= static_cast<size_t>(line.length());
    }
    return '\0';
}

bool skyBriefRevealComplete()
{
    return s_skyBriefVisibleChars >= skyBriefSubpageTotalChars();
}

void advanceSkyBriefReveal(unsigned long nowMs)
{
    if (skyBriefRevealComplete())
        return;
    if (s_skyBriefNextCharMs == 0)
        s_skyBriefNextCharMs = s_skyBriefParagraphRevealStartMs + kSkyBriefParagraphCharRevealMs;

    while (!skyBriefRevealComplete() && nowMs >= s_skyBriefNextCharMs)
    {
        const char currentChar = skyBriefCharAtVisibleIndex(s_skyBriefVisibleChars);
        ++s_skyBriefVisibleChars;
        s_skyBriefNextCharMs += kSkyBriefParagraphCharRevealMs + skyBriefCharRevealDelayMs(currentChar);
    }
}

void resetSkyBriefParagraphPager(unsigned long nowMs)
{
    s_skyBriefParagraphStartLine = 0;
    s_skyBriefParagraphRevealStartMs = nowMs;
    s_skyBriefParagraphLastAdvanceMs = nowMs;
    s_skyBriefParagraphCompleted = false;
    s_skyBriefVisibleChars = 0;
    s_skyBriefNextCharMs = nowMs + kSkyBriefParagraphCharRevealMs;
}

void syncSkyBriefParagraphState(const wxv::astronomy::SkyFactPage &page, bool forceReset)
{
    const char *marquee = summaryMarqueeText(page);
    const bool marqueeChanged = forceReset || strncmp(s_skyBriefLastMarquee, marquee, sizeof(s_skyBriefLastMarquee)) != 0;
    if (!marqueeChanged)
        return;

    String currentTitle;
    if (!forceReset && !s_skyBriefSubpages.empty() && s_skyBriefSubpageIndex < s_skyBriefSubpages.size())
        currentTitle = s_skyBriefSubpages[s_skyBriefSubpageIndex].title;

    const String nextParagraph = buildSummaryParagraph(page);
    const SkyBriefSubpageVector nextSubpages = buildSkyBriefSubpages(page);
    snprintf(s_skyBriefLastMarquee, sizeof(s_skyBriefLastMarquee), "%s", marquee);
    s_skyBriefSubpages = nextSubpages;
    s_skyBriefParagraphText = nextParagraph;
    s_skyBriefWrappedLines = wrapSkyBriefText(s_skyBriefParagraphText, kSkyBriefBodyWidthPx);
    s_skyBriefSubpageIndex = 0;
    if (!forceReset && currentTitle.length() > 0)
    {
        for (size_t i = 0; i < s_skyBriefSubpages.size(); ++i)
        {
            if (s_skyBriefSubpages[i].title == currentTitle)
            {
                s_skyBriefSubpageIndex = i;
                break;
            }
        }
    }
    resetSkyBriefParagraphPager(millis());
}

void setSkyBriefParagraphStart(size_t startLine, unsigned long nowMs)
{
    s_skyBriefParagraphStartLine = startLine;
    s_skyBriefParagraphRevealStartMs = nowMs;
    s_skyBriefParagraphLastAdvanceMs = nowMs;
    s_skyBriefParagraphCompleted = false;
    s_skyBriefVisibleChars = 0;
    s_skyBriefNextCharMs = nowMs + kSkyBriefParagraphCharRevealMs;
}

void drawSkyBriefParagraphPage(unsigned long nowMs)
{
    const wxv::astronomy::SkyFactPage &page = wxv::astronomy::skySummaryPage();
    syncSkyBriefParagraphState(page, false);
    const String headerTitle = (s_skyBriefSubpages.empty() || s_skyBriefSubpageIndex >= s_skyBriefSubpages.size())
                                   ? String("SKY BRIEF")
                                   : s_skyBriefSubpages[s_skyBriefSubpageIndex].title;
    const uint16_t normal = bodyColor();
    const uint16_t headerBg = titleBg();
    const uint16_t headerFg = titleFg();
    SkyBriefSubpage fallbackSubpage;
    fallbackSubpage.title = "SKY BRIEF";
    fallbackSubpage.lines.push_back("Sky facts unavailable.");
    const SkyBriefSubpage &currentSubpage =
        (s_skyBriefSubpages.empty() || s_skyBriefSubpageIndex >= s_skyBriefSubpages.size())
            ? fallbackSubpage
            : s_skyBriefSubpages[s_skyBriefSubpageIndex];
    unsigned long revealElapsedMs = nowMs - s_skyBriefParagraphRevealStartMs;
    const bool showCursor = ((nowMs / kSkyBriefParagraphCursorBlinkMs) % 2UL) == 0UL;
    bool cursorDrawn = false;

    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    dma_display->fillRect(0, kSkyBriefBodyTopY, PANEL_RES_X, PANEL_RES_Y - kSkyBriefBodyTopY, myBLACK);

    for (size_t lineOffset = 0; lineOffset < currentSubpage.lines.size(); ++lineOffset)
    {
        const String &fullLine = currentSubpage.lines[lineOffset];
        String visibleLine;
        unsigned long consumedLineMs = 0;
        const size_t visibleChars = revealedSkyBriefCharsForElapsedMs(fullLine, revealElapsedMs, consumedLineMs);
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

        const int y = kSkyBriefBodyTopY + 1 + static_cast<int>(lineOffset * kSkyBriefLineH);
        dma_display->setTextColor(normal);
        dma_display->setCursor(kSkyBriefBodyLeftX, y);
        dma_display->print(visibleLine);

        if (!cursorDrawn && showCursor && visibleChars < fullLine.length())
        {
            const String cursorPrefix = fullLine.substring(0, visibleChars);
            const int cursorX = kSkyBriefBodyLeftX + skyBriefTextWidthPx(cursorPrefix);
            dma_display->drawFastVLine(cursorX, y, 7, normal);
            cursorDrawn = true;
            break;
        }
    }

    if (!cursorDrawn && showCursor && !currentSubpage.lines.empty())
    {
        const size_t lastLineOffset = currentSubpage.lines.size() - 1u;
        const String &lastLine = currentSubpage.lines[lastLineOffset];
        const int y = kSkyBriefBodyTopY + 1 + static_cast<int>(lastLineOffset * kSkyBriefLineH);
        const int cursorX = kSkyBriefBodyLeftX + skyBriefTextWidthPx(lastLine);
        dma_display->drawFastVLine(cursorX, y, 7, normal);
    }

    dma_display->fillRect(0, 0, PANEL_RES_X, 8, headerBg);
    dma_display->drawFastHLine(0, 7, PANEL_RES_X, ui_theme::isNightTheme() ? ui_theme::monoUnderline() : ui_theme::infoUnderlineDay());
    dma_display->setTextColor(headerFg);
    dma_display->setCursor(ui_theme::Layout::kTitleTextX, ui_theme::Layout::kTitleBarY);
    dma_display->print(headerTitle);
}

void advanceSkyBriefParagraphPage(unsigned long nowMs)
{
    if (s_skyBriefSubpages.empty())
        return;
    if (s_skyBriefSubpageIndex + 1 < s_skyBriefSubpages.size())
    {
        s_skyBriefSubpageIndex++;
        setSkyBriefParagraphStart(0, nowMs);
        return;
    }

    s_skyBriefParagraphCompleted = true;
    s_skyBriefSubpageIndex = 0;
    setSkyBriefParagraphStart(0, nowMs);
}

static String skyBriefSectionKey(const String &title)
{
    const int spacePos = title.indexOf(' ');
    if (spacePos < 0)
        return title;
    const String suffix = title.substring(spacePos + 1);
    if (suffix.length() > 0 && isDigit(suffix.charAt(0)))
        return title.substring(0, spacePos);
    return title;
}

void stepSkyBriefParagraphManual(int direction, unsigned long nowMs)
{
    if (s_skyBriefSubpages.empty())
        return;

    if (direction >= 0)
    {
        advanceSkyBriefParagraphPage(nowMs);
        return;
    }

    const String currentSection = skyBriefSectionKey(s_skyBriefSubpages[s_skyBriefSubpageIndex].title);
    size_t firstIndexInSection = s_skyBriefSubpageIndex;
    size_t lastIndexInSection = s_skyBriefSubpageIndex;
    while (firstIndexInSection > 0 &&
           skyBriefSectionKey(s_skyBriefSubpages[firstIndexInSection - 1u].title) == currentSection)
        --firstIndexInSection;
    while ((lastIndexInSection + 1u) < s_skyBriefSubpages.size() &&
           skyBriefSectionKey(s_skyBriefSubpages[lastIndexInSection + 1u].title) == currentSection)
        ++lastIndexInSection;

    if (s_skyBriefSubpageIndex > firstIndexInSection)
        --s_skyBriefSubpageIndex;
    else
        s_skyBriefSubpageIndex = lastIndexInSection;
    setSkyBriefParagraphStart(0, nowMs);
}

void syncSummaryMarqueeState(const wxv::astronomy::SkyFactPage &page, bool forceReset)
{
    String line1;
    String line2;
    String ticker;
    buildSummaryLayout(page, line1, line2, ticker);
    const char *marqueeText = ticker.c_str();
    const bool textChanged = strncmp(s_summaryLastText, marqueeText, sizeof(s_summaryLastText)) != 0;
    if (forceReset || textChanged)
    {
        const bool hadExistingText = s_summaryLastText[0] != '\0';
        const int previousCycle = PANEL_RES_X + s_summaryMarqueeWidth + kSummaryRepeatGapPx;
        snprintf(s_summaryLastText, sizeof(s_summaryLastText), "%s", marqueeText);
        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);
        int16_t x1, y1;
        uint16_t w, h;
        dma_display->getTextBounds(marqueeText, 0, 0, &x1, &y1, &w, &h);
        s_summaryMarqueeWidth = static_cast<int>(w);
        const int nextCycle = PANEL_RES_X + s_summaryMarqueeWidth + kSummaryRepeatGapPx;
        if (forceReset || previousCycle <= 0 || nextCycle <= 0)
            s_summaryMarqueeOffset = 0;
        else
            s_summaryMarqueeOffset %= nextCycle;
        const unsigned long nowMs = millis();
        if (forceReset || !hadExistingText)
        {
            s_summaryMarqueeLastStepMs = nowMs;
            s_summaryMarqueeStartAfterMs = nowMs + kSummaryStartDelayMs;
        }
        else
        {
            // Preserve current motion when only the content changes.
            if (s_summaryMarqueeLastStepMs == 0)
                s_summaryMarqueeLastStepMs = nowMs;
            if (s_summaryMarqueeStartAfterMs < nowMs)
                s_summaryMarqueeStartAfterMs = nowMs;
        }
    }
}

void drawSummaryPage(const wxv::astronomy::SkyFactPage &page)
{
    drawHeader(page.title[0] ? page.title : "SKY BRIEF");
    dma_display->fillRect(0, 8, PANEL_RES_X, 24, myBLACK);

    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    const uint16_t accent = pageAccentColor(page.type);
    const uint16_t normal = bodyColor();

    String line1;
    String line2;
    String ticker;
    buildSummaryLayout(page, line1, line2, ticker);
    ticker.replace("Â", "");
    ticker.replace("¦", " ");
    ticker.replace("|", " ");

    if (line1.length() > 0 && line2.length() > 0)
    {
        drawCenteredLine(8, line1.c_str(), accent);
        drawCenteredLine(16, line2.c_str(), normal);
    }
    else if (line1.length() > 0)
    {
        drawCenteredLine(16, line1.c_str(), accent);
    }

    dma_display->setTextColor(normal);
    if (ticker.length() > 0)
    {
        const int textX = PANEL_RES_X - s_summaryMarqueeOffset;
        dma_display->setCursor(textX, 24);
        dma_display->print(ticker);
        dma_display->setCursor(textX + s_summaryMarqueeWidth + kSummaryRepeatGapPx, 24);
        dma_display->print(ticker);
    }
}

void resetSummaryMarquee()
{
    s_summaryMarqueeOffset = 0;
    s_summaryMarqueeWidth = 0;
    s_summaryMarqueeLastStepMs = 0;
    s_summaryMarqueeStartAfterMs = 0;
    s_summaryLastText[0] = '\0';
}

unsigned long defaultSummaryStepMs()
{
    return static_cast<unsigned long>(constrain(scrollSpeed, 40, 120));
}

unsigned long effectiveSummaryStepMs()
{
    return (s_summaryStepMs > 0) ? s_summaryStepMs : defaultSummaryStepMs();
}

int summaryCycleWidth()
{
    return PANEL_RES_X + s_summaryMarqueeWidth + kSummaryRepeatGapPx;
}

bool speedUpSummaryStep()
{
    const int current = static_cast<int>(effectiveSummaryStepMs());
    const int next = constrain((current * 7) / 8, static_cast<int>(kRenderSkySummaryMs), 180);
    if (next == current)
        return false;
    s_summaryStepMs = static_cast<unsigned long>(next);
    return true;
}

bool slowDownSummaryStep()
{
    const int current = static_cast<int>(effectiveSummaryStepMs());
    const int next = constrain(((current * 9) + 7) / 8, static_cast<int>(kRenderSkySummaryMs), 180);
    if (next == current)
        return false;
    s_summaryStepMs = static_cast<unsigned long>(next);
    return true;
}

void drawSkyFactPageImpl(const wxv::astronomy::SkyFactPage &page)
{
    if (page.type == wxv::astronomy::SkyFactType::Summary)
    {
        syncSummaryMarqueeState(page, false);
        drawSummaryPage(page);
        return;
    }

    drawHeader(page.title[0] ? page.title : "SKY");
    if (!page.valid)
    {
        drawCenteredLine(16, "No sky facts", bodyColor());
        return;
    }

    const uint16_t accent = pageAccentColor(page.type);
    const uint16_t normal = bodyColor();

    auto drawArrow = [&](int x, int y, int8_t trend, uint16_t color)
    {
        if (trend > 0)
        {
            dma_display->drawLine(x + 3, y + 1, x + 1, y + 3, color);
            dma_display->drawLine(x + 3, y + 1, x + 5, y + 3, color);
            dma_display->drawLine(x + 3, y + 1, x + 3, y + 7, color);
        }
        else if (trend < 0)
        {
            dma_display->drawLine(x + 3, y + 7, x + 1, y + 5, color);
            dma_display->drawLine(x + 3, y + 7, x + 5, y + 5, color);
            dma_display->drawLine(x + 3, y + 1, x + 3, y + 7, color);
        }
    };

    auto drawSeasonMeter = [&](int x, int y, uint8_t fill, uint8_t total)
    {
        if (total == 0)
            return;
        const int barW = 58;
        const int barH = 4;
        const uint16_t remainingColor = ui_theme::isNightTheme()
                                            ? ui_theme::applyGraphicColor(dma_display->color565(48, 56, 68))
                                            : ui_theme::applyGraphicColor(dma_display->color565(52, 72, 92));
        const uint16_t borderColor = ui_theme::applyGraphicColor(dma_display->color565(95, 120, 145));
        const uint16_t elapsedColor = accent;
        const int innerW = barW - 2;
        const int fillW = constrain((innerW * static_cast<int>(fill) + static_cast<int>(total) / 2) / static_cast<int>(total), 0, innerW);

        dma_display->drawRect(x, y, barW, barH, borderColor);
        if (innerW <= 0 || barH <= 2)
            return;

        dma_display->fillRect(x + 1, y + 1, innerW, barH - 2, remainingColor);
        if (fillW > 0)
            dma_display->fillRect(x + 1, y + 1, fillW, barH - 2, elapsedColor);
    };

    auto drawMoonIcon = [&](int cx, int cy)
    {
        const wxv::astronomy::AstronomyData &astro = wxv::astronomy::astronomyData();
        const uint16_t dark = ui_theme::applyGraphicColor(dma_display->color565(30, 36, 48));
        const uint16_t lit = accent;
        const uint16_t rim = ui_theme::applyGraphicColor(dma_display->color565(245, 235, 180));
        const int r = 4;
        const float wrapped = astro.moonPhaseFraction - floorf(astro.moonPhaseFraction);
        const float phaseAngle = wrapped * 2.0f * 3.14159265f;
        const float k = cosf(phaseAngle);
        const bool waxing = wrapped <= 0.5f;

        for (int dy = -r; dy <= r; ++dy)
        {
            for (int dx = -r; dx <= r; ++dx)
            {
                const float xn = static_cast<float>(dx) / static_cast<float>(r);
                const float yn = static_cast<float>(dy) / static_cast<float>(r);
                if (xn * xn + yn * yn > 1.0f)
                    continue;
                const float limb = sqrtf(fmaxf(0.0f, 1.0f - yn * yn));
                const bool on = waxing ? (xn >= k * limb) : (xn <= -k * limb);
                dma_display->drawPixel(cx + dx, cy + dy, on ? lit : dark);
            }
        }
        dma_display->drawCircle(cx, cy, r, rim);
    };

    switch (page.type)
    {
    case wxv::astronomy::SkyFactType::Season:
        drawCenteredLine(10, page.line1, accent);
        drawSeasonMeter(3, 18, page.meterFill, page.meterTotal);
        drawCenteredLine(23, page.line2, normal);
        break;
    case wxv::astronomy::SkyFactType::Daylight:
    {
        const char *trendText = page.line2;
        if (trendText[0] == '+' || trendText[0] == '-')
            ++trendText;
        drawCenteredLine(12, page.line1, accent);
        drawCenteredLine(21, trendText, trendColor(page.trend));
        if (trendText[0] != '\0')
            drawArrow(8, 20, page.trend, trendColor(page.trend));
        break;
    }
    case wxv::astronomy::SkyFactType::SunCountdown:
        drawCenteredLine(12, page.line1, accent);
        drawCenteredLine(21, page.line2, normal);
        drawArrow(8, 11, page.trend, accent);
        break;
    case wxv::astronomy::SkyFactType::Moon:
        drawMoonIcon(12, 18);
        dma_display->setTextColor(accent);
        dma_display->setCursor(21, 12);
        dma_display->print(page.line1);
        dma_display->setTextColor(normal);
        dma_display->setCursor(16, 22);
        dma_display->print(page.line2);
        if (page.line3[0] != '\0')
        {
            dma_display->setCursor(12, 28);
            dma_display->print(page.line3);
        }
        break;
    default:
        switch (page.lineCount)
        {
        case 1:
            drawCenteredLine(16, page.line1, accent);
            break;
        case 2:
            drawCenteredLine(12, page.line1, accent);
            drawCenteredLine(21, page.line2, normal);
            break;
        default:
            drawCenteredLine(10, page.line1, accent);
            drawCenteredLine(17, page.line2, normal);
            drawCenteredLine(24, page.line3, normal);
            break;
        }
        break;
    }
}
} // namespace

void drawSkyBriefScreen()
{
    wxv::astronomy::updateSkyFacts();
    const wxv::astronomy::AstronomyData &astro = wxv::astronomy::astronomyData();
    s_skyFactsLastTheme = theme;
    s_skyFactsLastDateKey = astro.localDateKey;
    s_skyFactsLastMinute = astro.localMinutes;

    const wxv::astronomy::SkyFactPage &page = wxv::astronomy::skySummaryPage();
    syncSkyBriefParagraphState(page, false);
    drawSkyBriefParagraphPage(millis());
}

void tickSkyBriefScreen()
{
    wxv::astronomy::updateSkyFacts();
    const wxv::astronomy::AstronomyData &astro = wxv::astronomy::astronomyData();
    const unsigned long nowMs = millis();
    const wxv::astronomy::SkyFactPage &page = wxv::astronomy::skySummaryPage();

    if (theme != s_skyFactsLastTheme ||
        astro.localDateKey != s_skyFactsLastDateKey ||
        astro.localMinutes != s_skyFactsLastMinute)
    {
        drawSkyBriefScreen();
        return;
    }

    syncSkyBriefParagraphState(page, false);
    const unsigned long revealDurationMs = skyBriefRevealDurationMs(s_skyBriefParagraphStartLine);
    const unsigned long pageDwellMs = skyBriefPageEndsWithPeriod(s_skyBriefParagraphStartLine)
                                          ? kSkyBriefParagraphPageDwellMs
                                          : kSkyBriefParagraphContinueDwellMs;
    const unsigned long elapsedSinceRevealStart = nowMs - s_skyBriefParagraphRevealStartMs;

    drawSkyBriefParagraphPage(nowMs);
    if (elapsedSinceRevealStart >= (revealDurationMs + pageDwellMs))
    {
        advanceSkyBriefParagraphPage(nowMs);
        drawSkyBriefParagraphPage(nowMs);
    }
}

void resetSkyBriefScreenState()
{
    s_skyBriefLastMarquee[0] = '\0';
    s_skyBriefWrappedLines.clear();
    s_skyBriefSubpages.clear();
    s_skyBriefSubpageIndex = 0;
    s_skyBriefParagraphText = "";
    s_skyBriefParagraphStartLine = 0;
    s_skyBriefParagraphRevealStartMs = 0;
    s_skyBriefParagraphLastAdvanceMs = 0;
    s_skyBriefParagraphCompleted = false;
    s_skyBriefVisibleChars = 0;
    s_skyBriefNextCharMs = 0;
    resetSummaryMarquee();
}

void handleSkyBriefDownPress()
{
    const wxv::astronomy::SkyFactPage &page = wxv::astronomy::skySummaryPage();
    syncSkyBriefParagraphState(page, false);
    stepSkyBriefParagraphManual(-1, millis());
    drawSkyBriefScreen();
}

void handleSkyBriefUpPress()
{
    const wxv::astronomy::SkyFactPage &page = wxv::astronomy::skySummaryPage();
    syncSkyBriefParagraphState(page, false);
    stepSkyBriefParagraphManual(1, millis());
    drawSkyBriefScreen();
}

void drawSkyFactSubpage(const wxv::astronomy::SkyFactPage &page)
{
    drawSkyFactPageImpl(page);
}
#else
void drawSkyBriefScreen() {}
void tickSkyBriefScreen() {}
void drawSkyFactSubpage(const wxv::astronomy::SkyFactPage &) {}
void handleSkyBriefDownPress() {}
void handleSkyBriefUpPress() {}
void resetSkyBriefScreenState() {}
#endif
