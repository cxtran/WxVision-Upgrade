// Temperature history screen rendering
#include "graph.h"
#include <Arduino.h>
#include <math.h>
#include <float.h>
#include <time.h>
#include "datalogger.h"
#include "display.h"
#include "InfoModal.h"
#include "units.h"
#include "settings.h"
#include "ScrollLine.h"
#include "fonts/verdanab8pt7b.h"
#include "ui_theme.h"
#include "ml_predictor.h"

extern int theme;

namespace
{
struct Point
{
    int x;
    int y;
};

struct TimedValue
{
    uint32_t ts;
    float value;
};

// Forward declaration
Point mapSampleToPoint(uint32_t ts, uint32_t dayStart, float value, float minVal, float maxVal,
                       int graphLeft, int graphWidth, int graphTop, int graphHeight);

static std::vector<Point> buildUniquePoints(const std::vector<TimedValue> &vals,
                                            uint32_t dayStart,
                                            float minVal, float maxVal,
                                            int graphLeft, int graphWidth, int graphTop, int graphHeight)
{
    std::vector<Point> points;
    if (graphWidth <= 0) return points;

    std::vector<float> sum(graphWidth, 0.0f);
    std::vector<int> count(graphWidth, 0);

    for (const auto &tv : vals)
    {
        uint32_t delta = (tv.ts > dayStart) ? (tv.ts - dayStart) : 0;
        if (delta > 86400) delta = 86400;
        float frac = static_cast<float>(delta) / 86400.0f;
        int xIdx = static_cast<int>((graphWidth - 1) * frac + 0.5f);
        if (xIdx < 0) xIdx = 0;
        if (xIdx >= graphWidth) xIdx = graphWidth - 1;
        sum[xIdx] += tv.value;
        count[xIdx] += 1;
    }

    float range = maxVal - minVal;
    if (range < 0.1f) range = 0.1f;

    for (int i = 0; i < graphWidth; ++i)
    {
        if (count[i] <= 0) continue;
        float avg = sum[i] / static_cast<float>(count[i]);
        float norm = (avg - minVal) / range;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        int x = graphLeft + i;
        int y = graphTop + (graphHeight - 1) - static_cast<int>(norm * (graphHeight - 1) + 0.5f);
        points.push_back({x, y});
    }
    return points;
}

// Get day start in epoch seconds using UTC arithmetic to avoid TZ/DST drift.
static uint32_t midnightFor(uint32_t ts)
{
    return ts - (ts % 86400UL);
}

// Rolling 24-hour window start anchored at the most recent sample time.
static uint32_t windowStartForLast24h(uint32_t lastTs)
{
    return (lastTs > 86400UL) ? (lastTs - 86400UL) : 0UL;
}

static ScrollLine s_tempScroll(PANEL_RES_X, 60);
static ScrollLine s_co2Scroll(PANEL_RES_X, 60);
static ScrollLine s_humScroll(PANEL_RES_X, 60);
static ScrollLine s_baroScroll(PANEL_RES_X, 60);
static constexpr unsigned long kPredictPageAutoMs = 4200UL;
static constexpr unsigned long kPredictManualHoldMs = 5000UL;
static const ScreenMode kHistory24Pages[] = {
    SCREEN_TEMP_HISTORY,
    SCREEN_HUM_HISTORY,
    SCREEN_CO2_HISTORY,
    SCREEN_BARO_HISTORY};
static constexpr uint8_t kHistory24PageCount = sizeof(kHistory24Pages) / sizeof(kHistory24Pages[0]);
static uint8_t s_history24PageIndex = 0;
static unsigned long s_history24LastSwitchMs = 0;
static unsigned long s_history24ManualHoldUntilMs = 0;
static bool s_history24RotationPaused = false;
static bool s_history24WaitForMarqueeCycle = false;
static int8_t s_history24ArmedPageIndex = -1;

static ScrollLine *scrollFor24HourPage(ScreenMode mode)
{
    switch (mode)
    {
    case SCREEN_TEMP_HISTORY:
        return &s_tempScroll;
    case SCREEN_HUM_HISTORY:
        return &s_humScroll;
    case SCREEN_CO2_HISTORY:
        return &s_co2Scroll;
    case SCREEN_BARO_HISTORY:
        return &s_baroScroll;
    default:
        return nullptr;
    }
}

#if WXV_ENABLE_NEXT24H_PREDICTION

enum class PredictPageId : uint8_t
{
    Outlook = 0,
    Temp,
    Humid,
    Press,
    Air,
    Summary,
    Count
};

struct PredictPage
{
    PredictPageId id;
    const char *title;
    char big[24];
    char meaning[96];
    int8_t trendDir = 0;     // -1 down, 0 right, +1 up
    uint8_t trendArrows = 0; // 0 none, 1 normal, 2 fast
};

struct PredictSnapshot
{
    bool ready = false;
    bool hasCo2 = false;
    int signalPercent = 0;
    char outlook[16] = "STEADY";
    char reason[64] = "Pressure steady, humidity steady, temperature steady";

    char tempBig[24] = "--";
    char tempRange[24] = "24H --/--";
    char tempHint[12] = "STABLE";

    char humBig[24] = "--";
    char humRange[24] = "24H --/--";
    char humHint[12] = "STABLE";

    char pressBig[24] = "--";
    char pressRange[24] = "24H --/--";
    char pressHint[12] = "STABLE";

    char airBig[24] = "--";
    char airRange[24] = "24H --/--";
    char airHint[12] = "OK";

    char summaryBig[24] = "STEADY";
    char summaryLine[96] = "Temperature steady, humidity steady, pressure steady";
};

static PredictSnapshot s_predictSnapshot;
static PredictPage s_predictPages[static_cast<size_t>(PredictPageId::Count)];
static uint8_t s_predictPageCount = 0;
static uint8_t s_predictPageIndex = 0;
static unsigned long s_predictLastSwitchMs = 0;
static unsigned long s_predictManualHoldUntilMs = 0;
static bool s_predictRotationPaused = false;
static bool s_predictTransitionActive = false;
static unsigned long s_predictTransitionStartMs = 0;
static constexpr unsigned long kPredictTransitionMs = 180UL;
static constexpr int kPredictMeaningGapPx = 12;
static bool s_predictMeaningCycleDone = false;
static bool s_predictMeaningNeedsScroll = false;
static int s_predictMeaningOffsetPx = 0;
static int s_predictMeaningWidthPx = 0;
static unsigned long s_predictMeaningLastStepMs = 0;
static uint8_t s_predictMeaningPageIndex = 255;
static bool s_predictStaticDirty = true;
static uint8_t s_predictRenderedPageIndex = 255;
static bool s_predictRenderedMono = false;
static int s_predictLastTheme = -1;

static unsigned long predictMeaningStepMs()
{
    // Follow global marquee speed setting for consistent scrolling across screens.
    // Lower delay => faster scroll. Clamp to stable range for panel rendering.
    return static_cast<unsigned long>(constrain(scrollSpeed, 12, 120));
}

#endif

String formatTempShort(float tempC, uint8_t decimals)
{
    if (isnan(tempC))
        return String("--");
    double disp = dispTemp(tempC);
    if (decimals == 0)
    {
        disp = static_cast<double>(lround(disp)); // round to whole number for display
    }
    String out = String(disp, static_cast<unsigned int>(decimals));
    out += "\xB0"; // degree symbol
    out += (units.temp == TempUnit::F) ? "F" : "C";
    return out;
}

String formatCo2Short(float co2)
{
    if (isnan(co2) || co2 <= 0.0f)
        return String("--");
    int rounded = static_cast<int>(co2 + 0.5f);
    return String(rounded);
}

String formatHumShort(float hum, uint8_t decimals)
{
    if (isnan(hum))
        return String("--");
    float clamped = constrain(hum, 0.0f, 100.0f);
    String out = String(clamped, static_cast<unsigned int>(decimals));
    out += "%";
    return out;
}

String formatPressShort(float hpa)
{
    if (isnan(hpa) || hpa <= 0.0f)
        return String("--");
    double disp = dispPress(hpa);
    uint8_t dp = (units.press == PressUnit::INHG) ? 2 : 0;
    char buf[20];
    dtostrf(disp, 0, dp, buf);
    String out(buf);
    out += (units.press == PressUnit::INHG) ? " inHg" : " hPa";
    return out;
}

String formatTempDelta(double delta)
{
    double disp = (units.temp == TempUnit::F) ? (delta * 9.0 / 5.0) : delta;
    char buf[16];
    dtostrf(disp, 0, 1, buf);
    String out(buf);
    out += "\xB0";
    out += (units.temp == TempUnit::F) ? "F" : "C";
    return out;
}

String formatPressDelta(double delta)
{
    double disp = dispPress(delta);
    uint8_t dp = (units.press == PressUnit::INHG) ? 2 : 1;
    char buf[16];
    dtostrf(disp, 0, dp, buf);
    String out(buf);
    out += (units.press == PressUnit::INHG) ? " inHg" : " hPa";
    return out;
}

String formatHumDelta(double delta)
{
    char buf[16];
    dtostrf(delta, 0, 1, buf);
    String out(buf);
    out += "%";
    return out;
}

Point mapSampleToPoint(uint32_t ts, uint32_t dayStart, float value, float minVal, float maxVal,
                       int graphLeft, int graphWidth, int graphTop, int graphHeight)
{
    if (graphWidth < 1 || graphHeight < 1)
        return {graphLeft, graphTop};
    float range = maxVal - minVal;
    if (range < 0.1f)
        range = 0.1f;

    // Position along rolling 24-hour window (seconds 0..86400)
    float frac = 0.0f;
    if (ts > dayStart)
    {
        uint32_t delta = ts - dayStart;
        if (delta > 86400) delta = 86400;
        frac = static_cast<float>(delta) / 86400.0f;
    }
    int x = graphLeft + static_cast<int>((graphWidth - 1) * frac + 0.5f);

    float norm = (value - minVal) / range;
    if (norm < 0.0f)
        norm = 0.0f;
    if (norm > 1.0f)
        norm = 1.0f;
    int y = graphTop + (graphHeight - 1) - static_cast<int>(norm * (graphHeight - 1) + 0.5f);
    return {x, y};
}

void labelGraphPoint(const Point &pt, int graphTop, uint16_t color, const char *label)
{
    int labelX = pt.x + 3;
    if (labelX > PANEL_RES_X - 4)
        labelX = pt.x - 4;
    if (labelX < 0)
        labelX = 0;

    int labelY = pt.y - 7;
    if (labelY < graphTop)
        labelY = pt.y + 5;
    if (labelY > PANEL_RES_Y - 7)
        labelY = PANEL_RES_Y - 7;

    dma_display->setTextColor(color);
    dma_display->setCursor(labelX, labelY);
    dma_display->print(label);
}

void drawXAxisTicks(int graphLeft, int graphWidth, int yBase, uint16_t morningColor, uint16_t afternoonColor)
{
    // 2-hour ticks across 24h -> 13 marks including ends
    for (int i = 0; i <= 12; ++i)
    {
        float pos = static_cast<float>(i) / 12.0f;
        int x = graphLeft + static_cast<int>((graphWidth - 1) * pos + 0.5f);
        uint16_t c = (i <= 6) ? morningColor : afternoonColor; // 0-12h vs 12-24h
        // Dot on the axis line
        dma_display->drawPixel(x, yBase, c);
    }
}

void drawChartScaffold(int graphLeft, int graphTop, int graphWidth, int graphHeight, uint16_t frameColor, uint16_t gridColor)
{
    if (graphWidth < 2 || graphHeight < 2)
        return;

    const bool mono = (theme == 1);
    // Match Lunar title divider dim tone.
    const uint16_t bottomBorderColor = mono ? dma_display->color565(80, 80, 90)
                                            : dma_display->color565(55, 80, 120);

    // Frame without top edge: keep sides + bottom only.
    dma_display->drawFastVLine(graphLeft - 1, graphTop - 1, graphHeight + 2, frameColor);
    dma_display->drawFastVLine(graphLeft + graphWidth, graphTop - 1, graphHeight + 2, frameColor);
    dma_display->drawFastHLine(graphLeft - 1, graphTop + graphHeight, graphWidth + 2, bottomBorderColor);

    // Lightweight quarter-step guides improve readability without visual clutter.
    for (int i = 1; i <= 3; ++i)
    {
        int y = graphTop + (graphHeight * i) / 4;
        dma_display->drawFastHLine(graphLeft, y, graphWidth, gridColor);
    }
    for (int i = 1; i <= 3; ++i)
    {
        int x = graphLeft + (graphWidth * i) / 4;
        dma_display->drawFastVLine(x, graphTop, graphHeight, gridColor);
    }
}

uint16_t currentChartPointColor()
{
    return (theme == 1)
               ? dma_display->color565(220, 190, 255)
               : dma_display->color565(255, 96, 180);
}

void draw24HourSectionPageBar(uint8_t activePage)
{
    (void)activePage;
    // Page indicator intentionally disabled.
}

#if WXV_ENABLE_NEXT24H_PREDICTION

static void resetPredictMeaningScroll(unsigned long nowMs)
{
    s_predictMeaningOffsetPx = 0;
    s_predictMeaningWidthPx = 0;
    s_predictMeaningLastStepMs = nowMs;
    s_predictMeaningPageIndex = 255;
    s_predictMeaningCycleDone = false;
    s_predictMeaningNeedsScroll = false;
}

static void onPredictPageChanged(unsigned long nowMs)
{
    s_predictLastSwitchMs = nowMs;
    s_predictTransitionActive = false; // disable wipe animation to prevent visible flicker on matrix panels
    s_predictTransitionStartMs = nowMs;
    s_predictStaticDirty = true;
    s_predictRenderedPageIndex = 255;
    resetPredictMeaningScroll(nowMs);
}

static void drawPredictionStatusWrapped(const char *message, uint16_t color)
{
    if (!dma_display)
        return;

    constexpr int kScreenW = 64;
    constexpr int kScreenH = 32;
    constexpr int kX = 1;
    constexpr int kLineHeight = 8;
    constexpr int kMaxLines = 3;
    constexpr int kMaxCharsPerLine = 10; // fits 64px with 5x7 font and spacing

    dma_display->fillScreen(0);
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    dma_display->setTextColor(color);

    String text = message ? String(message) : String("");
    text.trim();
    if (text.isEmpty())
        text = "No data";

    String lines[kMaxLines];
    int lineCount = 0;
    int i = 0;
    while (lineCount < kMaxLines && i < text.length())
    {
        while (i < text.length() && text[i] == ' ')
            ++i;
        if (i >= text.length())
            break;

        int lineStart = i;
        int lineEnd = i;
        int chars = 0;
        int lastSpace = -1;
        while (lineEnd < text.length() && chars < kMaxCharsPerLine)
        {
            char c = text[lineEnd];
            if (c == ' ')
                lastSpace = lineEnd;
            ++lineEnd;
            ++chars;
        }

        if (lineEnd < text.length() && lastSpace > lineStart)
        {
            lineEnd = lastSpace;
        }

        String chunk = text.substring(lineStart, lineEnd);
        chunk.trim();
        lines[lineCount++] = chunk;

        i = lineEnd;
    }

    if (lineCount <= 0)
    {
        lines[0] = text;
        lineCount = 1;
    }

    int blockHeight = lineCount * kLineHeight;
    int startY = (kScreenH - blockHeight) / 2;
    if (startY < 0)
        startY = 0;

    for (int line = 0; line < lineCount; ++line)
    {
        int y = startY + line * kLineHeight;
        if (y > (kScreenH - kLineHeight))
            break;
        dma_display->setCursor(kX, y);
        dma_display->print(lines[line]);
    }
}

static void renderPredictPage(unsigned long nowMs)
{
    if (!dma_display || !s_predictSnapshot.ready || s_predictPageCount == 0)
        return;

    const bool mono = (theme == 1);
    const uint16_t headerBg = mono ? ui_theme::monoHeaderBg() : INFOMODAL_HEADERBG;
    const uint16_t headerFg = mono ? ui_theme::monoHeaderFg() : INFOMODAL_GREEN;
    // Make day/night differences explicit on prediction subpages.
    const uint16_t bigColor = mono ? ui_theme::infoValueHighlightMono() : ui_theme::infoLabelDay();
    const uint16_t bodyColor = mono ? ui_theme::monoHeaderFg() : ui_theme::infoValueDay();

    const PredictPage &page = s_predictPages[s_predictPageIndex];
    const bool redrawStatic =
        s_predictStaticDirty ||
        s_predictRenderedPageIndex != s_predictPageIndex ||
        s_predictRenderedMono != mono;
    if (redrawStatic)
    {
        dma_display->fillScreen(0);
        dma_display->fillRect(0, 0, PANEL_RES_X, 8, headerBg);

        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);
        int16_t tx1, ty1;
        uint16_t tw, th;
        dma_display->getTextBounds(page.title, 0, 0, &tx1, &ty1, &tw, &th);
        int titleX = (PANEL_RES_X - static_cast<int>(tw)) / 2;
        if (titleX < 0) titleX = 0;
        dma_display->setTextColor(headerFg, headerBg);
        dma_display->setCursor(titleX, 0);
        dma_display->print(page.title);

        auto drawTrendArrowLikeMenu = [&](int x, int y, uint16_t color, int8_t dir) {
        // Menu back-arrow rotated 90deg (up direction)
        if (dir > 0)
        {
            // Up
            dma_display->drawLine(x + 3, y + 1, x + 1, y + 3, color);
            dma_display->drawLine(x + 3, y + 1, x + 5, y + 3, color);
            dma_display->drawLine(x + 3, y + 1, x + 3, y + 7, color);
        }
        else if (dir < 0)
        {
            // Down
            dma_display->drawLine(x + 3, y + 7, x + 1, y + 5, color);
            dma_display->drawLine(x + 3, y + 7, x + 5, y + 5, color);
            dma_display->drawLine(x + 3, y + 1, x + 3, y + 7, color);
        }
        else
        {
            // Right
            dma_display->drawLine(x + 7, y + 3, x + 5, y + 1, color);
            dma_display->drawLine(x + 7, y + 3, x + 5, y + 5, color);
            dma_display->drawLine(x + 1, y + 3, x + 7, y + 3, color);
        }
        };

        // Big text: same font style used by clock main digits, fitted into 64x16 area.
        // Center big value in the area between title bar (y:0-7) and footer line (y:25).
        const int bigAreaTop = 9;
        const int bigAreaH = 16; // y:9..24
        const int arrowStride = 9;
        const int arrowBlockW = (page.trendArrows > 0) ? static_cast<int>(page.trendArrows) * arrowStride : 0;

        dma_display->setFont(&verdanab8pt7b);
        dma_display->setTextSize(1);

        int bigX = 0;
        int bigY = 0;
        int textDrawW = 0;

        if (page.id == PredictPageId::Air && strncmp(page.big, "CO2 ", 4) == 0)
        {
        const char *valueText = page.big + 4; // after "CO2 "

        int16_t valX1, valY1;
        uint16_t valW, valH;
        dma_display->setFont(&verdanab8pt7b);
        dma_display->setTextSize(1);
        dma_display->getTextBounds(valueText, 0, 0, &valX1, &valY1, &valW, &valH);

        // Small CO2 label at requested position inside content section.
        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);
        dma_display->setTextColor(bigColor);
        dma_display->setCursor(16, bigAreaTop + 0);
        dma_display->print("CO2");

        // Center CO2 value in big-font area.
        textDrawW = static_cast<int>(valW);
        bigX = (PANEL_RES_X - (textDrawW + arrowBlockW)) / 2 - valX1;
        if (bigX < 0) bigX = 0;
        bigY = bigAreaTop + (bigAreaH - static_cast<int>(valH)) / 2 - valY1;

        dma_display->setTextColor(bigColor);
        dma_display->setFont(&verdanab8pt7b);
        dma_display->setTextSize(1);
        dma_display->setCursor(bigX, bigY);
        dma_display->print(valueText);
        }
        else if (page.id == PredictPageId::Temp)
        {
        char valueText[16] = {0};
        char unitChar = (units.temp == TempUnit::F) ? 'F' : 'C';
        size_t widx = 0;
        for (const char *p = page.big; *p && widx < sizeof(valueText) - 1; ++p)
        {
            if ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+')
            {
                valueText[widx++] = *p;
            }
            else if (*p == 'C' || *p == 'F')
            {
                unitChar = *p;
            }
        }
        valueText[widx] = '\0';
        if (widx == 0)
        {
            strncpy(valueText, "--", sizeof(valueText) - 1);
            valueText[sizeof(valueText) - 1] = '\0';
        }

        int16_t valX1, valY1;
        uint16_t valW, valH;
        dma_display->setFont(&verdanab8pt7b);
        dma_display->setTextSize(1);
        dma_display->getTextBounds(valueText, 0, 0, &valX1, &valY1, &valW, &valH);

        char unitText[4] = {'\xB0', unitChar, '\0'};
        int16_t unitX1, unitY1;
        uint16_t unitW, unitH;
        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);
        dma_display->getTextBounds(unitText, 0, 0, &unitX1, &unitY1, &unitW, &unitH);

        const int unitGap = 1;
        textDrawW = static_cast<int>(valW) + unitGap + static_cast<int>(unitW);
        bigX = (PANEL_RES_X - (textDrawW + arrowBlockW)) / 2 - valX1;
        if (bigX < 0)
            bigX = 0;
        bigY = bigAreaTop + (bigAreaH - static_cast<int>(valH)) / 2 - valY1;

        dma_display->setTextColor(bigColor);
        dma_display->setFont(&verdanab8pt7b);
        dma_display->setTextSize(1);
        dma_display->setCursor(bigX, bigY);
        dma_display->print(valueText);

        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);
        dma_display->setCursor(bigX + static_cast<int>(valW) + unitGap, bigAreaTop + 2);
        dma_display->print(unitText);
        }
        else if (page.id == PredictPageId::Humid)
        {
        char valueText[16] = {0};
        size_t widx = 0;
        bool hasPercent = false;
        for (const char *p = page.big; *p && widx < sizeof(valueText) - 1; ++p)
        {
            if ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+')
            {
                valueText[widx++] = *p;
            }
            else if (*p == '%')
            {
                hasPercent = true;
            }
        }
        valueText[widx] = '\0';
        if (widx == 0)
        {
            strncpy(valueText, "--", sizeof(valueText) - 1);
            valueText[sizeof(valueText) - 1] = '\0';
        }

        int16_t valX1, valY1;
        uint16_t valW, valH;
        dma_display->setFont(&verdanab8pt7b);
        dma_display->setTextSize(1);
        dma_display->getTextBounds(valueText, 0, 0, &valX1, &valY1, &valW, &valH);

        const char *unitText = hasPercent ? "%" : "";
        int16_t unitX1, unitY1;
        uint16_t unitW, unitH;
        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);
        dma_display->getTextBounds(unitText, 0, 0, &unitX1, &unitY1, &unitW, &unitH);

        const int unitGap = hasPercent ? 1 : 0;
        textDrawW = static_cast<int>(valW) + unitGap + static_cast<int>(unitW);
        bigX = (PANEL_RES_X - (textDrawW + arrowBlockW)) / 2 - valX1;
        if (bigX < 0)
            bigX = 0;
        bigY = bigAreaTop + (bigAreaH - static_cast<int>(valH)) / 2 - valY1;

        dma_display->setTextColor(bigColor);
        dma_display->setFont(&verdanab8pt7b);
        dma_display->setTextSize(1);
        dma_display->setCursor(bigX, bigY);
        dma_display->print(valueText);

        if (hasPercent)
        {
            dma_display->setFont(&Font5x7Uts);
            dma_display->setTextSize(1);
            dma_display->setCursor(bigX + static_cast<int>(valW) + unitGap + 2, bigAreaTop + 2);
            dma_display->print(unitText);
        }
        }
        else
        {
        // Auto-fit textual headlines: use big font when it fits, fallback to 5x7 for long words.
        dma_display->setFont(&verdanab8pt7b);
        dma_display->setTextSize(1);
        dma_display->getTextBounds(page.big, 0, 0, &tx1, &ty1, &tw, &th);
        int bigFontW = static_cast<int>(tw);
        int bigFontH = static_cast<int>(th);
        const bool fitsBig = (bigFontW + arrowBlockW) <= PANEL_RES_X;

        if (fitsBig)
        {
            textDrawW = bigFontW;
            bigX = (PANEL_RES_X - (textDrawW + arrowBlockW)) / 2 - tx1;
            if (bigX < 0) bigX = 0;
            bigY = bigAreaTop + (bigAreaH - bigFontH) / 2 - ty1;
            dma_display->setTextColor(bigColor);
            dma_display->setCursor(bigX, bigY);
            dma_display->print(page.big);
        }
        else
        {
            dma_display->setFont(&Font5x7Uts);
            dma_display->setTextSize(1);
            dma_display->getTextBounds(page.big, 0, 0, &tx1, &ty1, &tw, &th);
            textDrawW = static_cast<int>(tw);
            bigX = (PANEL_RES_X - (textDrawW + arrowBlockW)) / 2 - tx1;
            if (bigX < 0) bigX = 0;
            bigY = bigAreaTop + (bigAreaH - static_cast<int>(th)) / 2 - ty1;
            dma_display->setTextColor(bigColor);
            dma_display->setCursor(bigX, bigY);
            dma_display->print(page.big);
        }
        }

        if (page.trendArrows > 0)
        {
        int ax = bigX + textDrawW + 2; // 2px gap between value and trend arrow
        if (page.id == PredictPageId::Humid)
            ax += 2;
        if (page.id == PredictPageId::Press)
            ax += 1;
        int ay = bigAreaTop + ((bigAreaH - 8) / 2);
        for (uint8_t i = 0; i < page.trendArrows; ++i)
        {
            drawTrendArrowLikeMenu(ax + static_cast<int>(i) * arrowStride, ay, bigColor, page.trendDir);
        }
        }

        s_predictStaticDirty = false;
        s_predictRenderedPageIndex = s_predictPageIndex;
        s_predictRenderedMono = mono;
    }

    // Bottom line: small scrolling explanation text.
    dma_display->fillRect(0, 25, PANEL_RES_X, PANEL_RES_Y - 25, myBLACK);
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    if (s_predictMeaningPageIndex != s_predictPageIndex)
    {
        s_predictMeaningPageIndex = s_predictPageIndex;
        s_predictMeaningWidthPx = getTextWidth(page.meaning);
        s_predictMeaningLastStepMs = nowMs;
        if (s_predictMeaningWidthPx <= PANEL_RES_X)
        {
            s_predictMeaningNeedsScroll = false;
            s_predictMeaningCycleDone = true;
            s_predictMeaningOffsetPx = 0;
        }
        else
        {
            s_predictMeaningNeedsScroll = true;
            s_predictMeaningCycleDone = false;
            s_predictMeaningOffsetPx = PANEL_RES_X; // Lunar-style: enter from right edge
        }
    }
    if (s_predictMeaningWidthPx <= 0)
        s_predictMeaningWidthPx = getTextWidth(page.meaning);

    if (s_predictMeaningNeedsScroll)
    {
        const unsigned long stepMs = predictMeaningStepMs();
        const unsigned long elapsed = nowMs - s_predictMeaningLastStepMs;
        if (elapsed >= stepMs)
        {
            // Keep motion visually smooth: never jump by multiple pixels in one frame.
            s_predictMeaningOffsetPx -= 1;
            s_predictMeaningLastStepMs = nowMs;
        }
        if (s_predictMeaningOffsetPx <= -(s_predictMeaningWidthPx + kPredictMeaningGapPx))
        {
            s_predictMeaningCycleDone = true;
        }
    }

    dma_display->setTextColor(bodyColor);
    const int meaningY = 25;
    if (!s_predictMeaningNeedsScroll)
    {
        dma_display->setCursor(0, meaningY);
        dma_display->print(page.meaning);
    }
    else
    {
        dma_display->setCursor(s_predictMeaningOffsetPx, meaningY);
        dma_display->print(page.meaning);
    }

    if (s_predictTransitionActive)
    {
        unsigned long elapsed = nowMs - s_predictTransitionStartMs;
        if (elapsed >= kPredictTransitionMs)
        {
            s_predictTransitionActive = false;
        }
        else
        {
            int visibleW = static_cast<int>((static_cast<uint32_t>(elapsed) * PANEL_RES_X) / kPredictTransitionMs);
            if (visibleW < 0) visibleW = 0;
            if (visibleW > PANEL_RES_X) visibleW = PANEL_RES_X;
            dma_display->fillRect(visibleW, 10, PANEL_RES_X - visibleW, PANEL_RES_Y - 10, myBLACK);
        }
    }
}

#endif

} // namespace

#if WXV_ENABLE_NEXT24H_PREDICTION
void resetPredictionRenderState()
{
    s_predictStaticDirty = true;
    s_predictRenderedPageIndex = 255;
    s_predictRenderedMono = false;
    s_predictMeaningPageIndex = 255;
    s_predictLastTheme = -1;
    s_predictRotationPaused = false;
}
#endif

void updateGraphData()
{
    // Placeholder for future streaming/preprocessing.
}

void drawTemperatureHistoryScreen()
{
    if (!dma_display)
        return;

    dma_display->setTextWrap(false);
    dma_display->setTextSize(1);
    dma_display->setFont(&Font5x7Uts);

    const auto &log = getSensorLog();
    uint32_t dayStart = 0;
    std::vector<TimedValue> tempsC;
    tempsC.reserve(log.size());
    for (const auto &s : log)
    {
        if (!isnan(s.temp))
        {
            tempsC.push_back({s.ts, s.temp + tempOffset});
        }
    }
    if (!tempsC.empty())
    {
        dayStart = windowStartForLast24h(tempsC.back().ts);
        std::vector<TimedValue> filtered;
        filtered.reserve(tempsC.size());
        for (const auto &tv : tempsC)
        {
            if (tv.ts >= dayStart && tv.ts <= dayStart + 86400UL)
                filtered.push_back(tv);
        }
        tempsC.swap(filtered);
    }

    dma_display->fillScreen(0);

    const bool mono = (theme == 1);
    const uint16_t headerBg = mono ? ui_theme::monoHeaderBg() : INFOMODAL_HEADERBG;
    const uint16_t headerFg = mono ? ui_theme::monoHeaderFg() : INFOMODAL_GREEN;
    const uint16_t underlineColor = mono ? dma_display->color565(80, 80, 90)
                                         : dma_display->color565(55, 80, 120);
    const uint16_t statsColor = mono ? ui_theme::monoBodyText() : INFOMODAL_UNSEL;
    // Pick an axis color distinct from both tick colors for clarity
    const uint16_t axisColor = mono ? dma_display->color565(90, 90, 140) : dma_display->color565(170, 190, 215);
    const uint16_t frameColor = mono ? dma_display->color565(70, 70, 110) : dma_display->color565(110, 130, 150);
    const uint16_t gridColor = mono ? dma_display->color565(26, 26, 44) : dma_display->color565(24, 34, 46);
    const uint16_t lineColor = mono ? dma_display->color565(170, 170, 230) : dma_display->color565(255, 170, 90);
    const uint16_t minColor = mono ? dma_display->color565(120, 160, 220) : dma_display->color565(90, 200, 255);
    const uint16_t maxColor = mono ? dma_display->color565(220, 140, 140) : dma_display->color565(255, 90, 90);

    const int headerHeight = 8;
    dma_display->fillRect(0, 0, PANEL_RES_X, headerHeight, headerBg);
    dma_display->setTextColor(headerFg);
    const char *title = "Temp 24H";
    int16_t tx1, ty1;
    uint16_t tw, th;
    dma_display->getTextBounds(title, 0, 0, &tx1, &ty1, &tw, &th);
    int titleX = (PANEL_RES_X - static_cast<int>(tw)) / 2;
    if (titleX < 0)
        titleX = 0;
    dma_display->setCursor(titleX, 0);
    dma_display->print(title);

    dma_display->drawFastHLine(0, headerHeight - 1, PANEL_RES_X, underlineColor);

    const int statsY = PANEL_RES_Y - 8; // bottom line for min/max readout

    if (tempsC.empty())
    {
        dma_display->setTextColor(statsColor);
        dma_display->setCursor(0, statsY - 8);
        dma_display->print("No temp data");
        dma_display->setCursor(0, statsY);
        dma_display->print("Yet.");
        return;
    }

    float minVal = tempsC[0].value;
    float maxVal = tempsC[0].value;
    for (size_t i = 1; i < tempsC.size(); ++i)
    {
        float v = tempsC[i].value;
        if (v < minVal)
        {
            minVal = v;
        }
        if (v > maxVal)
        {
            maxVal = v;
        }
    }

    uint8_t decimals = 0; // always show whole numbers
    String minStr = formatTempShort(minVal, decimals);
    String maxStr = formatTempShort(maxVal, decimals);
    String curStr = formatTempShort(tempsC.back().value, decimals);

    String statsLine = "Min: " + minStr + " ¦ Max: " + maxStr + " ¦ Current: " + curStr;
    static String prevTempStats;
    if (statsLine != prevTempStats)
    {
        String lines[] = {statsLine};
        s_tempScroll.setLines(lines, 1, true);
        s_tempScroll.setScrollSpeed(scrollSpeed); // match global marquee speed
        s_tempScroll.setStartPauseMs(2000);
        s_tempScroll.setContinuousWrap(false);
        uint16_t textColors[] = {statsColor};
        uint16_t bgColors[] = {0};
        s_tempScroll.setLineColors(textColors, bgColors, 1);
        prevTempStats = statsLine;
    }
    s_tempScroll.draw(0, statsY, statsColor);

    const int graphLeft = 1;
    const int graphWidth = 62;
    const int graphTop = headerHeight + 1;
    const int graphHeight = statsY - graphTop - 2; // leave a gap before stats line
    if (graphHeight < 2)
        return;

    // Draw only bottom border; remove top/left/right outline
    int axisY = graphTop + graphHeight;
    drawChartScaffold(graphLeft, graphTop, graphWidth, graphHeight, frameColor, gridColor);
    dma_display->drawFastHLine(graphLeft - 1, axisY, graphWidth + 2, axisColor);
    uint16_t morningTickColor = mono ? dma_display->color565(92, 112, 168) : dma_display->color565(95, 140, 185);
    uint16_t afternoonTickColor = mono ? dma_display->color565(108, 94, 148) : dma_display->color565(120, 120, 168);
    drawXAxisTicks(graphLeft, graphWidth, axisY, morningTickColor, afternoonTickColor);

    // Line chart for clearer trend
    // Smooth line chart with per-pixel segments

    auto points = buildUniquePoints(tempsC, dayStart, minVal, maxVal,
                                    graphLeft, graphWidth, graphTop, graphHeight);
    Point prev{-1, -1};
    for (const auto &pt : points)
    {
        if (prev.x >= 0)
        {
            dma_display->drawLine(prev.x, prev.y, pt.x, pt.y, lineColor);
        }
        else
        {
            dma_display->drawPixel(pt.x, pt.y, lineColor);
        }
        prev = pt;
    }

    if (!tempsC.empty())
    {
        Point last = mapSampleToPoint(tempsC.back().ts, dayStart, tempsC.back().value, minVal, maxVal,
                                      graphLeft, graphWidth, graphTop, graphHeight);
        dma_display->drawPixel(last.x, last.y, currentChartPointColor());
    }

    // No current marker/label on the chart per request
}

void drawHumidityHistoryScreen()
{
    if (!dma_display)
        return;

    dma_display->setTextWrap(false);
    dma_display->setTextSize(1);
    dma_display->setFont(&Font5x7Uts);

    const auto &log = getSensorLog();
    uint32_t dayStart = 0;
    std::vector<TimedValue> hums;
    hums.reserve(log.size());
    for (const auto &s : log)
    {
        if (!isnan(s.hum))
        {
            hums.push_back({s.ts, s.hum});
        }
    }
    if (!hums.empty())
    {
        dayStart = windowStartForLast24h(hums.back().ts);
        std::vector<TimedValue> filtered;
        filtered.reserve(hums.size());
        for (const auto &tv : hums)
        {
            if (tv.ts >= dayStart && tv.ts <= dayStart + 86400UL)
                filtered.push_back(tv);
        }
        hums.swap(filtered);
    }

    dma_display->fillScreen(0);

    const bool mono = (theme == 1);
    const uint16_t headerBg = mono ? ui_theme::monoHeaderBg() : INFOMODAL_HEADERBG;
    const uint16_t headerFg = mono ? ui_theme::monoHeaderFg() : INFOMODAL_GREEN;
    const uint16_t underlineColor = mono ? dma_display->color565(80, 80, 90)
                                         : dma_display->color565(55, 80, 120);
    const uint16_t statsColor = mono ? ui_theme::monoBodyText() : INFOMODAL_UNSEL;
    const uint16_t axisColor = mono ? dma_display->color565(90, 90, 140) : dma_display->color565(170, 190, 215);
    const uint16_t frameColor = mono ? dma_display->color565(70, 70, 110) : dma_display->color565(110, 130, 150);
    const uint16_t gridColor = mono ? dma_display->color565(26, 26, 44) : dma_display->color565(24, 34, 46);
    const uint16_t lineColor = mono ? dma_display->color565(170, 170, 230) : dma_display->color565(255, 170, 90);
    const uint16_t minColor = mono ? dma_display->color565(120, 160, 220) : dma_display->color565(90, 200, 255);
    const uint16_t maxColor = mono ? dma_display->color565(220, 140, 140) : dma_display->color565(255, 90, 90);

    const int headerHeight = 8;
    dma_display->fillRect(0, 0, PANEL_RES_X, headerHeight, headerBg);
    dma_display->setTextColor(headerFg);
    const char *title = "Humidity 24H";
    int16_t tx1, ty1;
    uint16_t tw, th;
    dma_display->getTextBounds(title, 0, 0, &tx1, &ty1, &tw, &th);
    int titleX = (PANEL_RES_X - static_cast<int>(tw)) / 2;
    if (titleX < 0)
        titleX = 0;
    dma_display->setCursor(titleX, 0);
    dma_display->print(title);

    dma_display->drawFastHLine(0, headerHeight - 1, PANEL_RES_X, underlineColor);

    const int statsY = PANEL_RES_Y - 8;

    if (hums.empty())
    {
        dma_display->setTextColor(statsColor);
        dma_display->setCursor(0, statsY - 8);
        dma_display->print("No hum data");
        dma_display->setCursor(0, statsY);
        dma_display->print("Yet.");
        return;
    }

    float minVal = hums[0].value;
    float maxVal = hums[0].value;
    for (size_t i = 1; i < hums.size(); ++i)
    {
        float v = hums[i].value;
        if (v < minVal)
        {
            minVal = v;
        }
        if (v > maxVal)
        {
            maxVal = v;
        }
    }

    uint8_t decimals = (fabsf(maxVal - minVal) < 10.0f) ? 1 : 0;
    String minStr = formatHumShort(minVal, decimals);
    String maxStr = formatHumShort(maxVal, decimals);
    String curStr = formatHumShort(hums.back().value, decimals);

    String statsLine = "Min: " + minStr + " ¦ Max: " + maxStr + " ¦ Current: " + curStr;
    static String prevHumStats;
    if (statsLine != prevHumStats)
    {
        String lines[] = {statsLine};
        s_humScroll.setLines(lines, 1, true);
        s_humScroll.setScrollSpeed(scrollSpeed);
        s_humScroll.setStartPauseMs(2000);
        s_humScroll.setContinuousWrap(false);
        uint16_t textColors[] = {statsColor};
        uint16_t bgColors[] = {0};
        s_humScroll.setLineColors(textColors, bgColors, 1);
        prevHumStats = statsLine;
    }
    s_humScroll.draw(0, statsY, statsColor);

    const int graphLeft = 1;
    const int graphWidth = 62;
    const int graphTop = headerHeight + 1;
    const int graphHeight = statsY - graphTop - 2;
    if (graphHeight < 2)
        return;

    int axisY = graphTop + graphHeight;
    drawChartScaffold(graphLeft, graphTop, graphWidth, graphHeight, frameColor, gridColor);
    dma_display->drawFastHLine(graphLeft - 1, axisY, graphWidth + 2, axisColor);
    uint16_t morningTickColor = mono ? dma_display->color565(92, 112, 168) : dma_display->color565(95, 140, 185);
    uint16_t afternoonTickColor = mono ? dma_display->color565(108, 94, 148) : dma_display->color565(120, 120, 168);
    drawXAxisTicks(graphLeft, graphWidth, axisY, morningTickColor, afternoonTickColor);

    auto points = buildUniquePoints(hums, dayStart, minVal, maxVal,
                                    graphLeft, graphWidth, graphTop, graphHeight);
    Point prev{-1, -1};
    for (const auto &pt : points)
    {
        if (prev.x >= 0)
        {
            dma_display->drawLine(prev.x, prev.y, pt.x, pt.y, lineColor);
        }
        else
        {
            dma_display->drawPixel(pt.x, pt.y, lineColor);
        }
        prev = pt;
    }

    if (!hums.empty())
    {
        Point last = mapSampleToPoint(hums.back().ts, dayStart, hums.back().value, minVal, maxVal,
                                      graphLeft, graphWidth, graphTop, graphHeight);
        dma_display->drawPixel(last.x, last.y, currentChartPointColor());
    }

    // No current marker/label on the chart per request
}

void drawBaroHistoryScreen()
{
    if (!dma_display)
        return;

    dma_display->setTextWrap(false);
    dma_display->setTextSize(1);
    dma_display->setFont(&Font5x7Uts);

    const auto &log = getSensorLog();
    uint32_t dayStart = 0;
    std::vector<TimedValue> pressVals;
    pressVals.reserve(log.size());
    for (const auto &s : log)
    {
        if (!isnan(s.press) && s.press > 0.0f)
        {
            pressVals.push_back({s.ts, s.press});
        }
    }
    if (!pressVals.empty())
    {
        dayStart = windowStartForLast24h(pressVals.back().ts);
        std::vector<TimedValue> filtered;
        filtered.reserve(pressVals.size());
        for (const auto &tv : pressVals)
        {
            if (tv.ts >= dayStart && tv.ts <= dayStart + 86400UL)
                filtered.push_back(tv);
        }
        pressVals.swap(filtered);
    }

    dma_display->fillScreen(0);

    const bool mono = (theme == 1);
    const uint16_t headerBg = mono ? ui_theme::monoHeaderBg() : INFOMODAL_HEADERBG;
    const uint16_t headerFg = mono ? ui_theme::monoHeaderFg() : INFOMODAL_GREEN;
    const uint16_t underlineColor = mono ? dma_display->color565(80, 80, 90)
                                         : dma_display->color565(55, 80, 120);
    const uint16_t statsColor = mono ? ui_theme::monoBodyText() : INFOMODAL_UNSEL;
    const uint16_t axisColor = mono ? dma_display->color565(90, 90, 140) : dma_display->color565(170, 190, 215);
    const uint16_t frameColor = mono ? dma_display->color565(70, 70, 110) : dma_display->color565(110, 130, 150);
    const uint16_t gridColor = mono ? dma_display->color565(26, 26, 44) : dma_display->color565(24, 34, 46);
    const uint16_t lineColor = mono ? dma_display->color565(170, 170, 230) : dma_display->color565(255, 170, 90);
    const uint16_t minColor = mono ? dma_display->color565(120, 160, 220) : dma_display->color565(90, 200, 255);
    const uint16_t maxColor = mono ? dma_display->color565(220, 140, 140) : dma_display->color565(255, 90, 90);

    const int headerHeight = 8;
    dma_display->fillRect(0, 0, PANEL_RES_X, headerHeight, headerBg);
    dma_display->setTextColor(headerFg);
    const char *title = "Baro 24H";
    int16_t tx1, ty1;
    uint16_t tw, th;
    dma_display->getTextBounds(title, 0, 0, &tx1, &ty1, &tw, &th);
    int titleX = (PANEL_RES_X - static_cast<int>(tw)) / 2;
    if (titleX < 0)
        titleX = 0;
    dma_display->setCursor(titleX, 0);
    dma_display->print(title);

    dma_display->drawFastHLine(0, headerHeight - 1, PANEL_RES_X, underlineColor);

    const int statsY = PANEL_RES_Y - 8;

    if (pressVals.empty())
    {
        dma_display->setTextColor(statsColor);
        dma_display->setCursor(0, statsY - 8);
        dma_display->print("No baro data");
        dma_display->setCursor(0, statsY);
        dma_display->print("Yet.");
        return;
    }

    float minVal = pressVals[0].value;
    float maxVal = pressVals[0].value;
    for (size_t i = 1; i < pressVals.size(); ++i)
    {
        float v = pressVals[i].value;
        if (v < minVal)
        {
            minVal = v;
        }
        if (v > maxVal)
        {
            maxVal = v;
        }
    }

    String minStr = formatPressShort(minVal);
    String maxStr = formatPressShort(maxVal);
    String curStr = formatPressShort(pressVals.back().value);

    String statsLine = "Min: " + minStr + " ¦ Max: " + maxStr + " ¦ Current: " + curStr;
    static String prevBaroStats;
    if (statsLine != prevBaroStats)
    {
        String lines[] = {statsLine};
        s_baroScroll.setLines(lines, 1, true);
        s_baroScroll.setScrollSpeed(scrollSpeed);
        s_baroScroll.setStartPauseMs(2000);
        s_baroScroll.setContinuousWrap(false);
        uint16_t textColors[] = {statsColor};
        uint16_t bgColors[] = {0};
        s_baroScroll.setLineColors(textColors, bgColors, 1);
        prevBaroStats = statsLine;
    }
    s_baroScroll.draw(0, statsY, statsColor);

    const int graphLeft = 1;
    const int graphWidth = 62;
    const int graphTop = headerHeight + 1;
    const int graphHeight = statsY - graphTop - 2;
    if (graphHeight < 2)
        return;

    int axisY = graphTop + graphHeight;
    drawChartScaffold(graphLeft, graphTop, graphWidth, graphHeight, frameColor, gridColor);
    dma_display->drawFastHLine(graphLeft - 1, axisY, graphWidth + 2, axisColor);
    uint16_t morningTickColor = mono ? dma_display->color565(92, 112, 168) : dma_display->color565(95, 140, 185);
    uint16_t afternoonTickColor = mono ? dma_display->color565(108, 94, 148) : dma_display->color565(120, 120, 168);
    drawXAxisTicks(graphLeft, graphWidth, axisY, morningTickColor, afternoonTickColor);

    auto points = buildUniquePoints(pressVals, dayStart, minVal, maxVal,
                                    graphLeft, graphWidth, graphTop, graphHeight);
    Point prev{-1, -1};
    for (const auto &pt : points)
    {
        if (prev.x >= 0)
        {
            dma_display->drawLine(prev.x, prev.y, pt.x, pt.y, lineColor);
        }
        else
        {
            dma_display->drawPixel(pt.x, pt.y, lineColor);
        }
        prev = pt;
    }

    if (!pressVals.empty())
    {
        Point last = mapSampleToPoint(pressVals.back().ts, dayStart, pressVals.back().value, minVal, maxVal,
                                      graphLeft, graphWidth, graphTop, graphHeight);
        dma_display->drawPixel(last.x, last.y, currentChartPointColor());
    }

    // No current marker/label on the chart per request
}

void drawCo2HistoryScreen()
{
    if (!dma_display)
        return;

    dma_display->setTextWrap(false);
    dma_display->setTextSize(1);
    dma_display->setFont(&Font5x7Uts);

    const auto &log = getSensorLog();
    uint32_t dayStart = 0;
    std::vector<TimedValue> co2Vals;
    co2Vals.reserve(log.size());
    for (const auto &s : log)
    {
        if (!isnan(s.co2) && s.co2 > 0.0f)
        {
            co2Vals.push_back({s.ts, s.co2});
        }
    }
    if (!co2Vals.empty())
    {
        dayStart = windowStartForLast24h(co2Vals.back().ts);
        std::vector<TimedValue> filtered;
        filtered.reserve(co2Vals.size());
        for (const auto &tv : co2Vals)
        {
            if (tv.ts >= dayStart && tv.ts <= dayStart + 86400UL)
                filtered.push_back(tv);
        }
        co2Vals.swap(filtered);
    }

    dma_display->fillScreen(0);

    const bool mono = (theme == 1);
    const uint16_t headerBg = mono ? ui_theme::monoHeaderBg() : INFOMODAL_HEADERBG;
    const uint16_t headerFg = mono ? ui_theme::monoHeaderFg() : INFOMODAL_GREEN;
    const uint16_t underlineColor = mono ? dma_display->color565(80, 80, 90)
                                         : dma_display->color565(55, 80, 120);
    const uint16_t statsColor = mono ? ui_theme::monoBodyText() : INFOMODAL_UNSEL;
    // Pick an axis color distinct from both tick colors for clarity
    const uint16_t axisColor = mono ? dma_display->color565(90, 90, 140) : dma_display->color565(170, 190, 215);
    const uint16_t frameColor = mono ? dma_display->color565(70, 70, 110) : dma_display->color565(110, 130, 150);
    const uint16_t gridColor = mono ? dma_display->color565(26, 26, 44) : dma_display->color565(24, 34, 46);
    const uint16_t lineColor = mono ? dma_display->color565(170, 170, 230) : dma_display->color565(255, 170, 90);
    const uint16_t minColor = mono ? dma_display->color565(120, 160, 220) : dma_display->color565(90, 200, 255);
    const uint16_t maxColor = mono ? dma_display->color565(220, 140, 140) : dma_display->color565(255, 90, 90);

    const int headerHeight = 8;
    dma_display->fillRect(0, 0, PANEL_RES_X, headerHeight, headerBg);
    dma_display->setTextColor(headerFg);
    const char *title = "CO2 24H";
    int16_t tx1, ty1;
    uint16_t tw, th;
    dma_display->getTextBounds(title, 0, 0, &tx1, &ty1, &tw, &th);
    int titleX = (PANEL_RES_X - static_cast<int>(tw)) / 2;
    if (titleX < 0)
        titleX = 0;
    dma_display->setCursor(titleX, 0);
    dma_display->print(title);
    dma_display->drawFastHLine(0, headerHeight - 1, PANEL_RES_X, underlineColor);

    const int statsY = PANEL_RES_Y - 8;

    if (co2Vals.empty())
    {
        dma_display->setTextColor(statsColor);
        dma_display->setCursor(0, statsY - 8);
        dma_display->print("No CO2 data");
        dma_display->setCursor(0, statsY);
        dma_display->print("Yet.");
        return;
    }

    float minVal = co2Vals[0].value;
    float maxVal = co2Vals[0].value;
    for (size_t i = 1; i < co2Vals.size(); ++i)
    {
        float v = co2Vals[i].value;
        if (v < minVal)
        {
            minVal = v;
        }
        if (v > maxVal)
        {
            maxVal = v;
        }
    }

    String minStr = formatCo2Short(minVal);
    String maxStr = formatCo2Short(maxVal);
    String curStr = formatCo2Short(co2Vals.back().value);

    String statsLine = "Min: " + minStr + " ppm ¦ Max: " + maxStr + " ppm ¦ Current: " + curStr + " ppm";
    static String prevCo2Stats;
    if (statsLine != prevCo2Stats)
    {
        String lines[] = {statsLine};
        s_co2Scroll.setLines(lines, 1, true);
        s_co2Scroll.setScrollSpeed(scrollSpeed); // match global marquee speed
        s_co2Scroll.setStartPauseMs(2000);
        s_co2Scroll.setContinuousWrap(false);
        uint16_t textColors[] = {statsColor};
        uint16_t bgColors[] = {0};
        s_co2Scroll.setLineColors(textColors, bgColors, 1);
        prevCo2Stats = statsLine;
    }
    s_co2Scroll.draw(0, statsY, statsColor);

    const int graphLeft = 1;
    const int graphWidth = 62;
    const int graphTop = headerHeight + 1;
    const int graphHeight = statsY - graphTop - 2;
    if (graphHeight < 2)
        return;

    // Draw only bottom border; remove top/left/right outline
    int axisY = graphTop + graphHeight;
    drawChartScaffold(graphLeft, graphTop, graphWidth, graphHeight, frameColor, gridColor);
    dma_display->drawFastHLine(graphLeft - 1, axisY, graphWidth + 2, axisColor);
    uint16_t morningTickColor = mono ? dma_display->color565(92, 112, 168) : dma_display->color565(95, 140, 185);
    uint16_t afternoonTickColor = mono ? dma_display->color565(120, 120, 168) : dma_display->color565(120, 120, 168);
    drawXAxisTicks(graphLeft, graphWidth, axisY, morningTickColor, afternoonTickColor);

    auto points = buildUniquePoints(co2Vals, dayStart, minVal, maxVal,
                                    graphLeft, graphWidth, graphTop, graphHeight);
    Point prev{-1, -1};
    for (const auto &pt : points)
    {
        if (prev.x >= 0)
        {
            dma_display->drawLine(prev.x, prev.y, pt.x, pt.y, lineColor);
        }
        else
        {
            dma_display->drawPixel(pt.x, pt.y, lineColor);
        }
        prev = pt;
    }

    if (!co2Vals.empty())
    {
        Point last = mapSampleToPoint(co2Vals.back().ts, dayStart, co2Vals.back().value, minVal, maxVal,
                                      graphLeft, graphWidth, graphTop, graphHeight);
        dma_display->drawPixel(last.x, last.y, currentChartPointColor());
    }

    // No current marker/label on the chart per request
}

#if WXV_ENABLE_NEXT24H_PREDICTION
void drawPredictionScreen()
{
    if (!dma_display)
        return;

    if (s_predictLastTheme != theme)
    {
        s_predictLastTheme = theme;
        s_predictStaticDirty = true;
        s_predictRenderedPageIndex = 255;
        s_predictRenderedMono = false;
        s_predictMeaningPageIndex = 255;
    }

    PredictPageId prevPageId = PredictPageId::Count;
    char prevMeaning[96] = {0};
    bool hadPrevPage = false;
    if (s_predictPageCount > 0 && s_predictPageIndex < s_predictPageCount)
    {
        const PredictPage &prevPage = s_predictPages[s_predictPageIndex];
        prevPageId = prevPage.id;
        strncpy(prevMeaning, prevPage.meaning, sizeof(prevMeaning) - 1);
        prevMeaning[sizeof(prevMeaning) - 1] = '\0';
        hadPrevPage = true;
    }
    const bool preserveActiveMarqueeText = hadPrevPage && !s_predictMeaningCycleDone;

    auto copyText = [](char *dst, size_t dstLen, const char *src) {
        if (!dst || dstLen == 0)
            return;
        if (!src)
            src = "";
        strncpy(dst, src, dstLen - 1);
        dst[dstLen - 1] = '\0';
    };

    auto tempCompact = [&](float tempC, char *out, size_t outLen) {
        if (!out || outLen == 0)
            return;
        if (isnan(tempC))
        {
            copyText(out, outLen, "--");
            return;
        }
        int v = static_cast<int>(lround(dispTemp(tempC)));
        snprintf(out, outLen, "%d\xB0%c", v, (units.temp == TempUnit::F) ? 'F' : 'C');
    };

    auto pressCompact = [&](float pressHpa, char *out, size_t outLen) {
        if (!out || outLen == 0)
            return;
        if (isnan(pressHpa) || pressHpa <= 0.0f)
        {
            copyText(out, outLen, "--");
            return;
        }
        double disp = dispPress(pressHpa);
        if (units.press == PressUnit::INHG)
            snprintf(out, outLen, "%.2f", disp);
        else
            snprintf(out, outLen, "%d", static_cast<int>(lround(disp)));
    };

    auto trendDirAndArrows = [](double delta, double weak, double strong, int8_t &dir, uint8_t &arrows) {
        if (delta >= strong)
        {
            dir = 1;
            arrows = 2;
        }
        else if (delta >= weak)
        {
            dir = 1;
            arrows = 1;
        }
        else if (delta <= -strong)
        {
            dir = -1;
            arrows = 2;
        }
        else if (delta <= -weak)
        {
            dir = -1;
            arrows = 1;
        }
        else
        {
            dir = 0;
            arrows = 1;
        }
    };

    auto trendWord = [](double delta, double weak, double strong) -> const char * {
        if (delta >= strong) return "rising fast";
        if (delta >= weak) return "rising";
        if (delta <= -strong) return "falling fast";
        if (delta <= -weak) return "falling";
        return "steady";
    };

    auto rangeTemp = [&](float mn, float mx, char *out, size_t outLen) {
        if (isnan(mn) || isnan(mx))
        {
            copyText(out, outLen, "24H --/--");
            return;
        }
        int lo = static_cast<int>(lround(dispTemp(mn)));
        int hi = static_cast<int>(lround(dispTemp(mx)));
        snprintf(out, outLen, "24H %d-%d\xB0%c", lo, hi, (units.temp == TempUnit::F) ? 'F' : 'C');
    };

    auto rangeHum = [](float mn, float mx, char *out, size_t outLen) {
        if (isnan(mn) || isnan(mx))
        {
            strncpy(out, "24H --/--", outLen - 1);
            out[outLen - 1] = '\0';
            return;
        }
        snprintf(out, outLen, "24H %d-%d%%", static_cast<int>(lround(mn)), static_cast<int>(lround(mx)));
    };

    auto rangePress = [&](float mn, float mx, char *out, size_t outLen) {
        if (isnan(mn) || isnan(mx) || mn <= 0.0f || mx <= 0.0f)
        {
            copyText(out, outLen, "24H --/--");
            return;
        }
        if (units.press == PressUnit::INHG)
            snprintf(out, outLen, "24H %.2f-%.2f", dispPress(mn), dispPress(mx));
        else
            snprintf(out, outLen, "24H %d-%d",
                     static_cast<int>(lround(dispPress(mn))),
                     static_cast<int>(lround(dispPress(mx))));
    };

    auto rangeCo2 = [](float mn, float mx, char *out, size_t outLen) {
        if (isnan(mn) || isnan(mx) || mn <= 0.0f || mx <= 0.0f)
        {
            strncpy(out, "24H --/--", outLen - 1);
            out[outLen - 1] = '\0';
            return;
        }
        snprintf(out, outLen, "24H %d-%d", static_cast<int>(lround(mn)), static_cast<int>(lround(mx)));
    };

    auto addPage = [&](PredictPageId id, const char *title, const char *big, const char *meaning,
                       int8_t trendDir = 0, uint8_t trendArrows = 0) {
        if (s_predictPageCount >= static_cast<uint8_t>(PredictPageId::Count))
            return;
        PredictPage &p = s_predictPages[s_predictPageCount++];
        p.id = id;
        p.title = title;
        copyText(p.big, sizeof(p.big), big);
        copyText(p.meaning, sizeof(p.meaning), meaning);
        p.trendDir = trendDir;
        p.trendArrows = trendArrows;
    };

    const auto &log = getSensorLog();
    s_predictSnapshot = PredictSnapshot{};
    s_predictPageCount = 0;

    if (log.size() < 2)
    {
        drawPredictionStatusWrapped("Need more data for prediction", INFOMODAL_UNSEL);
        s_predictSnapshot.ready = false;
        return;
    }

    float tFirst = NAN, tLast = NAN, hFirst = NAN, hLast = NAN, pFirst = NAN, pLast = NAN, co2First = NAN, co2Last = NAN;
    for (size_t i = 0; i < log.size(); ++i)
    {
        if (isnan(tFirst) && !isnan(log[i].temp)) tFirst = log[i].temp + tempOffset;
        if (isnan(hFirst) && !isnan(log[i].hum)) hFirst = log[i].hum;
        if (isnan(pFirst) && !isnan(log[i].press) && log[i].press > 0.0f) pFirst = log[i].press;
        if (!isnan(log[i].temp)) tLast = log[i].temp + tempOffset;
        if (!isnan(log[i].hum)) hLast = log[i].hum;
        if (!isnan(log[i].press) && log[i].press > 0.0f) pLast = log[i].press;
        if (!isnan(log[i].co2) && log[i].co2 > 0.0f)
        {
            if (isnan(co2First)) co2First = log[i].co2;
            co2Last = log[i].co2;
        }
    }

    if (isnan(tFirst) || isnan(tLast) || isnan(hFirst) || isnan(hLast) || isnan(pFirst) || isnan(pLast))
    {
        drawPredictionStatusWrapped("Sensor data low", INFOMODAL_UNSEL);
        s_predictSnapshot.ready = false;
        return;
    }

    const size_t shortIdx = (log.size() > 6) ? (log.size() - 6) : 0;
    float pShort = pFirst;
    for (size_t i = shortIdx; i < log.size(); ++i)
    {
        if (!isnan(log[i].press) && log[i].press > 0.0f)
        {
            pShort = log[i].press;
            break;
        }
    }

    const double tempDeltaC = static_cast<double>(tLast) - static_cast<double>(tFirst);
    const double humDelta = static_cast<double>(hLast) - static_cast<double>(hFirst);
    const double pressDelta = static_cast<double>(pLast) - static_cast<double>(pFirst);
    const double pressDeltaShort = static_cast<double>(pLast) - static_cast<double>(pShort);

    const uint32_t lastTs = log.back().ts;
    const uint32_t firstTs = log.front().ts;
    const uint32_t spanSec = (lastTs > firstTs) ? (lastTs - firstTs) : 0;
    const uint32_t windowStart = (lastTs > 86400UL) ? (lastTs - 86400UL) : 0;

    float tMin = NAN, tMax = NAN, hMin = NAN, hMax = NAN, pMin = NAN, pMax = NAN, co2Min = NAN, co2Max = NAN;
    int tCount = 0, hCount = 0, pCount = 0;
    for (size_t i = 0; i < log.size(); ++i)
    {
        const SensorSample &s = log[i];
        if (s.ts < windowStart)
            continue;
        if (!isnan(s.temp))
        {
            float v = s.temp + tempOffset;
            if (isnan(tMin) || v < tMin) tMin = v;
            if (isnan(tMax) || v > tMax) tMax = v;
            ++tCount;
        }
        if (!isnan(s.hum))
        {
            if (isnan(hMin) || s.hum < hMin) hMin = s.hum;
            if (isnan(hMax) || s.hum > hMax) hMax = s.hum;
            ++hCount;
        }
        if (!isnan(s.press) && s.press > 0.0f)
        {
            if (isnan(pMin) || s.press < pMin) pMin = s.press;
            if (isnan(pMax) || s.press > pMax) pMax = s.press;
            ++pCount;
        }
        if (!isnan(s.co2) && s.co2 > 0.0f)
        {
            if (isnan(co2Min) || s.co2 < co2Min) co2Min = s.co2;
            if (isnan(co2Max) || s.co2 > co2Max) co2Max = s.co2;
        }
    }

    int confScore = 0;
    if (spanSec >= 20UL * 3600UL) confScore += 2;
    else if (spanSec >= 8UL * 3600UL) confScore += 1;

    if (tCount >= 96 && hCount >= 96 && pCount >= 96) confScore += 2;
    else if (tCount >= 24 && hCount >= 24 && pCount >= 24) confScore += 1;

    const bool pressureAligned =
        ((pressDelta >= 0.4 && pressDeltaShort >= 0.4) ||
         (pressDelta <= -0.4 && pressDeltaShort <= -0.4) ||
         (fabs(pressDelta) < 0.4 && fabs(pressDeltaShort) < 0.4));
    if (pressureAligned) confScore += 1;
    else confScore -= 1;

    const bool sensorStable = (fabs(humDelta) < 14.0 && fabs(tempDeltaC) < 6.0);
    if (sensorStable) confScore += 1;

    int clampedScore = confScore;
    if (clampedScore < 0) clampedScore = 0;
    if (clampedScore > 6) clampedScore = 6;
    s_predictSnapshot.signalPercent = static_cast<int>((clampedScore * 100 + 3) / 6);

    const char *outlook = "STEADY";
    if (pressDeltaShort <= -1.8 && humDelta >= 2.0) outlook = "RAIN POSS";
    else if (pressDeltaShort >= 1.6 && humDelta <= 1.0) outlook = "CLEARING";
    else if (fabs(pressDeltaShort) < 0.8 && hLast >= 70.0f) outlook = "CLOUDY";
    else if (humDelta >= 3.0 && hLast >= 65.0f) outlook = "MUGGY";
    else if (pressDeltaShort <= -1.2 && tempDeltaC <= -0.8) outlook = "UNSETTLED";
    else if (pressDeltaShort > 0.8) outlook = "FAIR";
    else if (pressDeltaShort < -0.8) outlook = "CHANGE";

    copyText(s_predictSnapshot.outlook, sizeof(s_predictSnapshot.outlook), outlook);
    snprintf(s_predictSnapshot.reason, sizeof(s_predictSnapshot.reason), "Pressure %s, humidity %s",
             trendWord(pressDeltaShort, 0.8, 2.0),
             trendWord(humDelta, 1.5, 4.0));

    char tempNow[12];
    tempCompact(tLast, tempNow, sizeof(tempNow));
    copyText(s_predictSnapshot.tempBig, sizeof(s_predictSnapshot.tempBig), tempNow);
    int8_t tempTrendDir = 0;
    uint8_t tempTrendArrows = 1;
    trendDirAndArrows(tempDeltaC, 0.6, 2.0, tempTrendDir, tempTrendArrows);
    rangeTemp(tMin, tMax, s_predictSnapshot.tempRange, sizeof(s_predictSnapshot.tempRange));
    if (tempDeltaC >= 0.6) copyText(s_predictSnapshot.tempHint, sizeof(s_predictSnapshot.tempHint), "WARMING");
    else if (tempDeltaC <= -0.6) copyText(s_predictSnapshot.tempHint, sizeof(s_predictSnapshot.tempHint), "COOLING");
    else copyText(s_predictSnapshot.tempHint, sizeof(s_predictSnapshot.tempHint), "STABLE");

    snprintf(s_predictSnapshot.humBig, sizeof(s_predictSnapshot.humBig), "%d%%",
             static_cast<int>(lround(hLast)));
    int8_t humTrendDir = 0;
    uint8_t humTrendArrows = 1;
    trendDirAndArrows(humDelta, 1.5, 4.0, humTrendDir, humTrendArrows);
    rangeHum(hMin, hMax, s_predictSnapshot.humRange, sizeof(s_predictSnapshot.humRange));
    if (hLast >= 70.0f || humDelta >= 4.0) copyText(s_predictSnapshot.humHint, sizeof(s_predictSnapshot.humHint), "MUGGY");
    else if (hLast <= 35.0f || humDelta <= -4.0) copyText(s_predictSnapshot.humHint, sizeof(s_predictSnapshot.humHint), "DRY");
    else copyText(s_predictSnapshot.humHint, sizeof(s_predictSnapshot.humHint), "COMFY");

    char pressNow[16];
    pressCompact(pLast, pressNow, sizeof(pressNow));
    copyText(s_predictSnapshot.pressBig, sizeof(s_predictSnapshot.pressBig), pressNow);
    int8_t pressTrendDir = 0;
    uint8_t pressTrendArrows = 1;
    if (pressDeltaShort >= 2.0)
    {
        pressTrendDir = 1;
        pressTrendArrows = 2;
    }
    else if (pressDeltaShort >= 0.8)
    {
        pressTrendDir = 1;
        pressTrendArrows = 1;
    }
    else if (pressDeltaShort <= -2.0)
    {
        pressTrendDir = -1;
        pressTrendArrows = 2;
    }
    else if (pressDeltaShort <= -0.8)
    {
        pressTrendDir = -1;
        pressTrendArrows = 1;
    }
    else
    {
        pressTrendDir = 0; // stable -> right arrow
        pressTrendArrows = 1;
    }
    rangePress(pMin, pMax, s_predictSnapshot.pressRange, sizeof(s_predictSnapshot.pressRange));
    if (pressDeltaShort <= -2.0) copyText(s_predictSnapshot.pressHint, sizeof(s_predictSnapshot.pressHint), "DROPFAST");
    else if (pressDeltaShort <= -0.8) copyText(s_predictSnapshot.pressHint, sizeof(s_predictSnapshot.pressHint), "FALLING");
    else if (pressDeltaShort >= 2.0) copyText(s_predictSnapshot.pressHint, sizeof(s_predictSnapshot.pressHint), "RISEFAST");
    else if (pressDeltaShort >= 0.8) copyText(s_predictSnapshot.pressHint, sizeof(s_predictSnapshot.pressHint), "RISING");
    else copyText(s_predictSnapshot.pressHint, sizeof(s_predictSnapshot.pressHint), "STABLE");

    s_predictSnapshot.hasCo2 = (!isnan(co2Last) && co2Last > 0.0f && !isnan(co2Min) && !isnan(co2Max));
    if (s_predictSnapshot.hasCo2)
    {
        double co2Delta = (!isnan(co2First) && co2First > 0.0f) ? (static_cast<double>(co2Last) - static_cast<double>(co2First)) : 0.0;
        int8_t co2TrendDir = 0;
        uint8_t co2TrendArrows = 1;
        if (co2Delta >= 150.0)
        {
            co2TrendDir = 1;
            co2TrendArrows = 2;
        }
        else if (co2Delta >= 50.0)
        {
            co2TrendDir = 1;
            co2TrendArrows = 1;
        }
        else if (co2Delta <= -150.0)
        {
            co2TrendDir = -1;
            co2TrendArrows = 2;
        }
        else if (co2Delta <= -50.0)
        {
            co2TrendDir = -1;
            co2TrendArrows = 1;
        }
        else
        {
            co2TrendDir = 0;
            co2TrendArrows = 1;
        }

        snprintf(s_predictSnapshot.airBig, sizeof(s_predictSnapshot.airBig), "%d",
                 static_cast<int>(lround(co2Last)));
        rangeCo2(co2Min, co2Max, s_predictSnapshot.airRange, sizeof(s_predictSnapshot.airRange));
        if (co2Last <= 800.0f) copyText(s_predictSnapshot.airHint, sizeof(s_predictSnapshot.airHint), "AIR GOOD");
        else if (co2Last <= 1200.0f) copyText(s_predictSnapshot.airHint, sizeof(s_predictSnapshot.airHint), "OPEN WINDOW");
        else copyText(s_predictSnapshot.airHint, sizeof(s_predictSnapshot.airHint), "VENT NOW");

        addPage(PredictPageId::Air, "CO2",
                s_predictSnapshot.airBig,
                (String(s_predictSnapshot.airRange) + ". " + s_predictSnapshot.airHint).c_str(),
                co2TrendDir,
                co2TrendArrows);
    }

    copyText(s_predictSnapshot.summaryBig, sizeof(s_predictSnapshot.summaryBig), s_predictSnapshot.outlook);
    snprintf(s_predictSnapshot.summaryLine, sizeof(s_predictSnapshot.summaryLine),
             "Temperature %s, humidity %s, pressure %s",
             trendWord(tempDeltaC, 0.5, 1.5),
             trendWord(humDelta, 1.5, 4.0),
             trendWord(pressDeltaShort, 0.8, 2.0));

    addPage(PredictPageId::Temp, "TEMP",
            s_predictSnapshot.tempBig,
            (String(s_predictSnapshot.tempRange) + ". Trend " + s_predictSnapshot.tempHint).c_str(),
            tempTrendDir,
            tempTrendArrows);
    addPage(PredictPageId::Humid, "HUMID",
            s_predictSnapshot.humBig,
            (String(s_predictSnapshot.humRange) + ". Air feels " + s_predictSnapshot.humHint).c_str(),
            humTrendDir,
            humTrendArrows);
    addPage(PredictPageId::Press, "PRESSURE",
            s_predictSnapshot.pressBig,
            (String("Pressure is ") + s_predictSnapshot.pressHint + ". " + s_predictSnapshot.pressRange).c_str(),
            pressTrendDir,
            pressTrendArrows);
    if (s_predictSnapshot.hasCo2)
    {
        // Added above with trend arrows.
    }
    addPage(PredictPageId::Summary, "NEXT 24H",
            s_predictSnapshot.summaryBig,
            (String(s_predictSnapshot.summaryLine) + ". Signal " + String(s_predictSnapshot.signalPercent) + "%").c_str());

    // Optional ML hint (requires a generated model in ml_model_generated.h).
    const auto mlPred = wxv::ml::predictOutlookFromLog(log);
    if (mlPred.available)
    {
        char mlLine[96];
        snprintf(mlLine, sizeof(mlLine), "ML %s (%u%%)", mlPred.label, static_cast<unsigned>(mlPred.confidencePct));
        addPage(PredictPageId::Outlook, "SMART WX", mlPred.label, mlLine);
    }

    // Keep the currently visible scrolling text stable until it finishes one cycle,
    // so periodic prediction refreshes do not cause mid-scroll jumps.
    if (preserveActiveMarqueeText && prevPageId != PredictPageId::Count)
    {
        for (uint8_t i = 0; i < s_predictPageCount; ++i)
        {
            if (s_predictPages[i].id == prevPageId)
            {
                copyText(s_predictPages[i].meaning, sizeof(s_predictPages[i].meaning), prevMeaning);
                break;
            }
        }
    }

    if (s_predictPageCount == 0)
    {
        dma_display->fillScreen(0);
        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);
        dma_display->setTextColor(INFOMODAL_UNSEL);
        dma_display->setCursor(1, 12);
        dma_display->print("No prediction");
        s_predictSnapshot.ready = false;
        return;
    }

    if (s_predictPageIndex >= s_predictPageCount)
        s_predictPageIndex = 0;

    const unsigned long nowMs = millis();
    if (s_predictLastSwitchMs == 0)
        s_predictLastSwitchMs = nowMs;

    s_predictSnapshot.ready = true;
    renderPredictPage(nowMs);
}

#endif

// Tick functions to animate marquee between full redraws
void tickTemperatureHistoryMarquee()
{
    if (!dma_display) return;
    s_tempScroll.update();
    // Use last configured colors from drawTemperatureHistoryScreen; default to INFOMODAL_UNSEL if unset
    uint16_t defaultColor = INFOMODAL_UNSEL;
    s_tempScroll.draw(0, PANEL_RES_Y - 8, defaultColor);
}

void tickHumidityHistoryMarquee()
{
    if (!dma_display) return;
    s_humScroll.update();
    uint16_t defaultColor = INFOMODAL_UNSEL;
    s_humScroll.draw(0, PANEL_RES_Y - 8, defaultColor);
}

void tickCo2HistoryMarquee()
{
    if (!dma_display) return;
    s_co2Scroll.update();
    uint16_t defaultColor = INFOMODAL_UNSEL;
    s_co2Scroll.draw(0, PANEL_RES_Y - 8, defaultColor);
}

void tickBaroHistoryMarquee()
{
    if (!dma_display) return;
    s_baroScroll.update();
    uint16_t defaultColor = INFOMODAL_UNSEL;
    s_baroScroll.draw(0, PANEL_RES_Y - 8, defaultColor);
}

bool is24HourSectionScreen(ScreenMode mode)
{
    return mode == SCREEN_TEMP_HISTORY ||
           mode == SCREEN_HUM_HISTORY ||
           mode == SCREEN_CO2_HISTORY ||
           mode == SCREEN_BARO_HISTORY;
}

void set24HourSectionPageForScreen(ScreenMode mode)
{
    for (uint8_t i = 0; i < kHistory24PageCount; ++i)
    {
        if (kHistory24Pages[i] == mode)
        {
            s_history24PageIndex = i;
            return;
        }
    }
    s_history24PageIndex = 0;
}

void draw24HourSectionScreen()
{
    if (!dma_display)
        return;

    if (s_history24PageIndex >= kHistory24PageCount)
        s_history24PageIndex = 0;

    const int8_t currentPage = static_cast<int8_t>(s_history24PageIndex);
    const bool pageChanged = (s_history24ArmedPageIndex != currentPage);
    ScrollLine *activeScroll = scrollFor24HourPage(kHistory24Pages[s_history24PageIndex]);

    if (pageChanged && activeScroll)
    {
        // Reset before draw so first frame does not start mid-scroll.
        activeScroll->reset();
        activeScroll->consumeCycleCompleted();
        activeScroll->consumeEnteredFromRight();
    }

    switch (kHistory24Pages[s_history24PageIndex])
    {
    case SCREEN_TEMP_HISTORY:
        drawTemperatureHistoryScreen();
        break;
    case SCREEN_HUM_HISTORY:
        drawHumidityHistoryScreen();
        break;
    case SCREEN_CO2_HISTORY:
        drawCo2HistoryScreen();
        break;
    case SCREEN_BARO_HISTORY:
        drawBaroHistoryScreen();
        break;
    default:
        drawTemperatureHistoryScreen();
        break;
    }

    // Re-evaluate after draw because draw functions can refresh line content via setLines().
    if (pageChanged && activeScroll)
    {
        s_history24WaitForMarqueeCycle = activeScroll->selectedLineNeedsScroll();
    }
    else if (pageChanged)
    {
        s_history24WaitForMarqueeCycle = false;
    }

    s_history24ArmedPageIndex = currentPage;
    s_history24RotationPaused = false;

    if (s_history24LastSwitchMs == 0)
        s_history24LastSwitchMs = millis();
}

void tick24HourSection()
{
    if (!dma_display)
        return;

    const unsigned long nowMs = millis();
    ScrollLine *activeScroll = nullptr;
    switch (kHistory24Pages[s_history24PageIndex])
    {
    case SCREEN_TEMP_HISTORY:
        activeScroll = &s_tempScroll;
        tickTemperatureHistoryMarquee();
        break;
    case SCREEN_HUM_HISTORY:
        activeScroll = &s_humScroll;
        tickHumidityHistoryMarquee();
        break;
    case SCREEN_CO2_HISTORY:
        activeScroll = &s_co2Scroll;
        tickCo2HistoryMarquee();
        break;
    case SCREEN_BARO_HISTORY:
        activeScroll = &s_baroScroll;
        tickBaroHistoryMarquee();
        break;
    default:
        break;
    }

    if (kHistory24PageCount <= 1 || s_history24RotationPaused || nowMs < s_history24ManualHoldUntilMs)
        return;

    bool canAdvance = false;
    if (s_history24WaitForMarqueeCycle)
    {
        // Advance only after a full marquee cycle completes and returns to left edge.
        canAdvance = (activeScroll != nullptr) && activeScroll->consumeCycleCompleted();
    }
    else
    {
        // If marquee is not needed, keep timed pacing.
        canAdvance = (nowMs - s_history24LastSwitchMs) >= kPredictPageAutoMs;
    }

    if (canAdvance)
    {
        s_history24PageIndex = static_cast<uint8_t>((s_history24PageIndex + 1U) % kHistory24PageCount);
        s_history24LastSwitchMs = nowMs;
        draw24HourSectionScreen();
    }
}

void handle24HourSectionDownPress()
{
    if (kHistory24PageCount <= 1)
        return;
    s_history24PageIndex = static_cast<uint8_t>((s_history24PageIndex + 1U) % kHistory24PageCount);
    const unsigned long nowMs = millis();
    s_history24LastSwitchMs = nowMs;
    s_history24ManualHoldUntilMs = nowMs + kPredictManualHoldMs;
    draw24HourSectionScreen();
}

void handle24HourSectionUpPress()
{
    if (kHistory24PageCount <= 1)
        return;
    int next = static_cast<int>(s_history24PageIndex) - 1;
    if (next < 0)
        next = static_cast<int>(kHistory24PageCount) - 1;
    s_history24PageIndex = static_cast<uint8_t>(next);
    const unsigned long nowMs = millis();
    s_history24LastSwitchMs = nowMs;
    s_history24ManualHoldUntilMs = nowMs + kPredictManualHoldMs;
    draw24HourSectionScreen();
}

void handle24HourSectionSelectPress()
{
    s_history24RotationPaused = !s_history24RotationPaused;
    if (!s_history24RotationPaused)
        s_history24LastSwitchMs = millis();
    draw24HourSectionScreen();
}

#if WXV_ENABLE_NEXT24H_PREDICTION

void tickPredictionScreen()
{
    if (!dma_display || !s_predictSnapshot.ready || s_predictPageCount == 0)
        return;

    const unsigned long nowMs = millis();
    bool shouldRender = s_predictStaticDirty;
    if (s_predictLastTheme != theme)
    {
        s_predictLastTheme = theme;
        s_predictStaticDirty = true;
        s_predictRenderedPageIndex = 255;
        s_predictRenderedMono = false;
        s_predictMeaningPageIndex = 255;
        shouldRender = true;
    }
    if (!s_predictRotationPaused &&
        s_predictPageCount > 1 &&
        nowMs >= s_predictManualHoldUntilMs &&
        (nowMs - s_predictLastSwitchMs) >= kPredictPageAutoMs &&
        s_predictMeaningCycleDone)
    {
        int next = static_cast<int>(s_predictPageIndex) + 1;
        if (next >= static_cast<int>(s_predictPageCount))
            next = 0;
        s_predictPageIndex = static_cast<uint8_t>(next);
        onPredictPageChanged(nowMs);
        shouldRender = true;
    }

    if (!shouldRender)
    {
        if (s_predictTransitionActive)
        {
            shouldRender = true;
        }
        else if (s_predictMeaningPageIndex != s_predictPageIndex)
        {
            shouldRender = true;
        }
        else if (s_predictMeaningNeedsScroll &&
                 (nowMs - s_predictMeaningLastStepMs) >= predictMeaningStepMs())
        {
            shouldRender = true;
        }
    }

    if (shouldRender)
    {
        renderPredictPage(nowMs);
    }
}

void handlePredictionDownPress()
{
    if (!s_predictSnapshot.ready || s_predictPageCount <= 1)
        return;
    int next = static_cast<int>(s_predictPageIndex) + 1;
    if (next >= static_cast<int>(s_predictPageCount))
        next = 0;
    s_predictPageIndex = static_cast<uint8_t>(next);
    unsigned long nowMs = millis();
    s_predictManualHoldUntilMs = nowMs + kPredictManualHoldMs;
    onPredictPageChanged(nowMs);
}

void handlePredictionUpPress()
{
    if (!s_predictSnapshot.ready || s_predictPageCount <= 1)
        return;
    int next = static_cast<int>(s_predictPageIndex) - 1;
    if (next < 0)
        next = static_cast<int>(s_predictPageCount) - 1;
    s_predictPageIndex = static_cast<uint8_t>(next);
    unsigned long nowMs = millis();
    s_predictManualHoldUntilMs = nowMs + kPredictManualHoldMs;
    onPredictPageChanged(nowMs);
}

void handlePredictionSelectPress()
{
    s_predictRotationPaused = !s_predictRotationPaused;
    if (!s_predictRotationPaused)
        s_predictLastSwitchMs = millis();
    renderPredictPage(millis());
}

#else

void resetPredictionRenderState() {}

void drawPredictionScreen() {}

void tickPredictionScreen() {}

void handlePredictionDownPress() {}

void handlePredictionUpPress() {}

void handlePredictionSelectPress() {}

#endif
