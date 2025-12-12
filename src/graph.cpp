// Temperature history screen rendering
#include "graph.h"
#include <Arduino.h>
#include <math.h>
#include <float.h>
#include <time.h>
#include <functional>
#include "datalogger.h"
#include "display.h"
#include "InfoModal.h"
#include "units.h"
#include "settings.h"
#include "ScrollLine.h"
#include "RollingUpScreen.h"

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

static ScrollLine s_tempScroll(PANEL_RES_X, 60);
static ScrollLine s_co2Scroll(PANEL_RES_X, 60);
static ScrollLine s_humScroll(PANEL_RES_X, 60);
static ScrollLine s_baroScroll(PANEL_RES_X, 60);
static std::vector<String> s_predictLines;
static RollingUpScreen s_predictRoll(PANEL_RES_X, PANEL_RES_Y - 9, 8);
static std::vector<int> s_predictOffsets;

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

static std::vector<String> wrapLines(const String &text, int maxWidthPx)
{
    std::vector<String> lines;
    String current = "";
    int currentW = 0;
    int spaceW = getTextWidth(" ");
    int maxChars = maxWidthPx / 6;
    if (maxChars < 4) maxChars = 4;

    int idx = 0;
    while (idx < text.length())
    {
        int nextSpace = text.indexOf(' ', idx);
        if (nextSpace < 0) nextSpace = text.length();
        String word = text.substring(idx, nextSpace);
        int wordW = getTextWidth(word.c_str());
        if (current.length() == 0)
        {
            if (word.length() > maxChars)
            {
                lines.push_back(word);
                idx = nextSpace + 1;
                continue;
            }
            current = word;
            currentW = wordW;
        }
        else if (currentW + spaceW + wordW <= maxWidthPx)
        {
            current += " " + word;
            currentW += spaceW + wordW;
        }
        else
        {
            lines.push_back(current);
            current = word;
            currentW = wordW;
        }
        idx = nextSpace + 1;
    }
    if (current.length() > 0)
        lines.push_back(current);
    return lines;
}

Point mapSampleToPoint(uint32_t ts, uint32_t dayStart, float value, float minVal, float maxVal,
                       int graphLeft, int graphWidth, int graphTop, int graphHeight)
{
    if (graphWidth < 1 || graphHeight < 1)
        return {graphLeft, graphTop};
    float range = maxVal - minVal;
    if (range < 0.1f)
        range = 0.1f;

    // Position along 0..24h day (seconds 0..86400)
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
} // namespace

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
        dayStart = midnightFor(tempsC.back().ts);
        std::vector<TimedValue> filtered;
        filtered.reserve(tempsC.size());
        for (const auto &tv : tempsC)
        {
            if (tv.ts >= dayStart && tv.ts <= dayStart + 86400)
                filtered.push_back(tv);
        }
        tempsC.swap(filtered);
    }

    dma_display->fillScreen(0);

    const bool mono = (theme == 1);
    const uint16_t headerBg = mono ? dma_display->color565(20, 20, 40) : INFOMODAL_HEADERBG;
    const uint16_t headerFg = mono ? dma_display->color565(60, 60, 120) : INFOMODAL_GREEN;
    const uint16_t underlineColor = mono ? dma_display->color565(30, 30, 70) : INFOMODAL_ULINE;
    const uint16_t statsColor = mono ? dma_display->color565(90, 90, 150) : INFOMODAL_UNSEL;
    // Pick an axis color distinct from both tick colors for clarity
    const uint16_t axisColor = mono ? dma_display->color565(90, 90, 140) : dma_display->color565(200, 200, 220);
    const uint16_t lineColor = mono ? dma_display->color565(170, 170, 230) : dma_display->color565(255, 170, 90);
    const uint16_t minColor = mono ? dma_display->color565(120, 160, 220) : dma_display->color565(90, 200, 255);
    const uint16_t maxColor = mono ? dma_display->color565(220, 140, 140) : dma_display->color565(255, 90, 90);

    const int headerHeight = 8;
    dma_display->fillRect(0, 0, PANEL_RES_X, headerHeight, headerBg);
    dma_display->setTextColor(headerFg);
    const char *title = "Temp 24h";
    int16_t tx1, ty1;
    uint16_t tw, th;
    dma_display->getTextBounds(title, 0, 0, &tx1, &ty1, &tw, &th);
    int titleX = 1; // left align to leave room elsewhere for current temp
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
        uint16_t textColors[] = {statsColor};
        uint16_t bgColors[] = {0};
        s_tempScroll.setLineColors(textColors, bgColors, 1);
        prevTempStats = statsLine;
    }
    s_tempScroll.update();
    s_tempScroll.draw(0, statsY, statsColor);

    const int graphLeft = 1;
    const int graphWidth = 62;
    const int graphTop = headerHeight + 1;
    const int graphHeight = statsY - graphTop - 2; // leave a gap before stats line
    if (graphHeight < 2)
        return;

    // Draw only bottom border; remove top/left/right outline
    int axisY = graphTop + graphHeight;
    dma_display->drawFastHLine(graphLeft - 1, axisY, graphWidth + 2, axisColor);
    // Use red for all tick dots
    uint16_t morningTickColor = dma_display->color565(255, 60, 60);
    uint16_t afternoonTickColor = morningTickColor;
    drawXAxisTicks(graphLeft, graphWidth, axisY, morningTickColor, afternoonTickColor);

    // Line chart for clearer trend
    // Smooth line chart with per-pixel segments
    static bool blinkOn = true;
    static unsigned long lastBlinkToggle = 0;

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

    // Vertical guide from axis to top (through latest point)
    if (!tempsC.empty())
    {
        int lineTop = (graphTop > 0) ? (graphTop - 1) : 0;
        Point last = mapSampleToPoint(tempsC.back().ts, dayStart, tempsC.back().value, minVal, maxVal,
                                      graphLeft, graphWidth, graphTop, graphHeight);
        dma_display->drawLine(last.x, axisY, last.x, lineTop, dma_display->color565(20, 40, 90));
    }

    // Blink current-time marker every 0.5s
    unsigned long now = millis();
    if (now - lastBlinkToggle >= 500)
    {
        blinkOn = !blinkOn;
        lastBlinkToggle = now;
    }
    if (!tempsC.empty())
    {
        Point last = mapSampleToPoint(tempsC.back().ts, dayStart, tempsC.back().value, minVal, maxVal,
                                      graphLeft, graphWidth, graphTop, graphHeight);
        dma_display->drawPixel(last.x, last.y, blinkOn ? maxColor : lineColor);
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
        dayStart = midnightFor(hums.back().ts);
        std::vector<TimedValue> filtered;
        filtered.reserve(hums.size());
        for (const auto &tv : hums)
        {
            if (tv.ts >= dayStart && tv.ts <= dayStart + 86400)
                filtered.push_back(tv);
        }
        hums.swap(filtered);
    }

    dma_display->fillScreen(0);

    const bool mono = (theme == 1);
    const uint16_t headerBg = mono ? dma_display->color565(20, 20, 40) : INFOMODAL_HEADERBG;
    const uint16_t headerFg = mono ? dma_display->color565(60, 60, 120) : INFOMODAL_GREEN;
    const uint16_t underlineColor = mono ? dma_display->color565(30, 30, 70) : INFOMODAL_ULINE;
    const uint16_t statsColor = mono ? dma_display->color565(90, 90, 150) : INFOMODAL_UNSEL;
    const uint16_t axisColor = mono ? dma_display->color565(90, 90, 140) : dma_display->color565(200, 200, 220);
    const uint16_t lineColor = mono ? dma_display->color565(170, 170, 230) : dma_display->color565(255, 170, 90);
    const uint16_t minColor = mono ? dma_display->color565(120, 160, 220) : dma_display->color565(90, 200, 255);
    const uint16_t maxColor = mono ? dma_display->color565(220, 140, 140) : dma_display->color565(255, 90, 90);

    const int headerHeight = 8;
    dma_display->fillRect(0, 0, PANEL_RES_X, headerHeight, headerBg);
    dma_display->setTextColor(headerFg);
    const char *title = "Humidity 24h";
    int16_t tx1, ty1;
    uint16_t tw, th;
    dma_display->getTextBounds(title, 0, 0, &tx1, &ty1, &tw, &th);
    int titleX = 1;
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
        uint16_t textColors[] = {statsColor};
        uint16_t bgColors[] = {0};
        s_humScroll.setLineColors(textColors, bgColors, 1);
        prevHumStats = statsLine;
    }
    s_humScroll.update();
    s_humScroll.draw(0, statsY, statsColor);

    const int graphLeft = 1;
    const int graphWidth = 62;
    const int graphTop = headerHeight + 1;
    const int graphHeight = statsY - graphTop - 2;
    if (graphHeight < 2)
        return;

    int axisY = graphTop + graphHeight;
    dma_display->drawFastHLine(graphLeft - 1, axisY, graphWidth + 2, axisColor);
    uint16_t morningTickColor = dma_display->color565(255, 60, 60);
    uint16_t afternoonTickColor = morningTickColor;
    drawXAxisTicks(graphLeft, graphWidth, axisY, morningTickColor, afternoonTickColor);

    // Dim vertical guide at current time through the full chart height
    if (!hums.empty() && dayStart != 0)
    {
        uint32_t delta = (hums.back().ts > dayStart) ? (hums.back().ts - dayStart) : 0;
        if (delta > 86400) delta = 86400;
        float frac = static_cast<float>(delta) / 86400.0f;
        int curX = graphLeft + static_cast<int>((graphWidth - 1) * frac + 0.5f);
        curX = constrain(curX, graphLeft, graphLeft + graphWidth - 1);
        int lineTop = (graphTop > 0) ? (graphTop - 1) : 0;
        Point last = mapSampleToPoint(hums.back().ts, dayStart, hums.back().value, minVal, maxVal,
                                      graphLeft, graphWidth, graphTop, graphHeight);
        dma_display->drawLine(curX, axisY, curX, lineTop, dma_display->color565(20, 40, 90));
    }

    static bool blinkOn = true;
    static unsigned long lastBlinkToggle = 0;

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

    unsigned long now = millis();
    if (now - lastBlinkToggle >= 500)
    {
        blinkOn = !blinkOn;
        lastBlinkToggle = now;
    }
    if (!hums.empty())
    {
        Point last = mapSampleToPoint(hums.back().ts, dayStart, hums.back().value, minVal, maxVal,
                                      graphLeft, graphWidth, graphTop, graphHeight);
        dma_display->drawPixel(last.x, last.y, blinkOn ? maxColor : lineColor);
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
        dayStart = midnightFor(pressVals.back().ts);
        std::vector<TimedValue> filtered;
        filtered.reserve(pressVals.size());
        for (const auto &tv : pressVals)
        {
            if (tv.ts >= dayStart && tv.ts <= dayStart + 86400)
                filtered.push_back(tv);
        }
        pressVals.swap(filtered);
    }

    dma_display->fillScreen(0);

    const bool mono = (theme == 1);
    const uint16_t headerBg = mono ? dma_display->color565(20, 20, 40) : INFOMODAL_HEADERBG;
    const uint16_t headerFg = mono ? dma_display->color565(60, 60, 120) : INFOMODAL_GREEN;
    const uint16_t underlineColor = mono ? dma_display->color565(30, 30, 70) : INFOMODAL_ULINE;
    const uint16_t statsColor = mono ? dma_display->color565(90, 90, 150) : INFOMODAL_UNSEL;
    const uint16_t axisColor = mono ? dma_display->color565(90, 90, 140) : dma_display->color565(200, 200, 220);
    const uint16_t lineColor = mono ? dma_display->color565(170, 170, 230) : dma_display->color565(255, 170, 90);
    const uint16_t minColor = mono ? dma_display->color565(120, 160, 220) : dma_display->color565(90, 200, 255);
    const uint16_t maxColor = mono ? dma_display->color565(220, 140, 140) : dma_display->color565(255, 90, 90);

    const int headerHeight = 8;
    dma_display->fillRect(0, 0, PANEL_RES_X, headerHeight, headerBg);
    dma_display->setTextColor(headerFg);
    const char *title = "Baro 24h";
    int16_t tx1, ty1;
    uint16_t tw, th;
    dma_display->getTextBounds(title, 0, 0, &tx1, &ty1, &tw, &th);
    int titleX = 1;
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
        uint16_t textColors[] = {statsColor};
        uint16_t bgColors[] = {0};
        s_baroScroll.setLineColors(textColors, bgColors, 1);
        prevBaroStats = statsLine;
    }
    s_baroScroll.update();
    s_baroScroll.draw(0, statsY, statsColor);

    const int graphLeft = 1;
    const int graphWidth = 62;
    const int graphTop = headerHeight + 1;
    const int graphHeight = statsY - graphTop - 2;
    if (graphHeight < 2)
        return;

    int axisY = graphTop + graphHeight;
    dma_display->drawFastHLine(graphLeft - 1, axisY, graphWidth + 2, axisColor);
    uint16_t morningTickColor = dma_display->color565(255, 60, 60);
    uint16_t afternoonTickColor = morningTickColor;
    drawXAxisTicks(graphLeft, graphWidth, axisY, morningTickColor, afternoonTickColor);

    // Dim vertical guide at current time through the full chart height
    if (!pressVals.empty() && dayStart != 0)
    {
        uint32_t delta = (pressVals.back().ts > dayStart) ? (pressVals.back().ts - dayStart) : 0;
        if (delta > 86400) delta = 86400;
        float frac = static_cast<float>(delta) / 86400.0f;
        int curX = graphLeft + static_cast<int>((graphWidth - 1) * frac + 0.5f);
        curX = constrain(curX, graphLeft, graphLeft + graphWidth - 1);
        int lineTop = (graphTop > 0) ? (graphTop - 1) : 0;
        Point last = mapSampleToPoint(pressVals.back().ts, dayStart, pressVals.back().value, minVal, maxVal,
                                      graphLeft, graphWidth, graphTop, graphHeight);
        dma_display->drawLine(curX, axisY, curX, lineTop, dma_display->color565(20, 40, 90));
    }

    static bool blinkOn = true;
    static unsigned long lastBlinkToggle = 0;

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

    // Vertical guide from axis to top (through latest point)
    if (!pressVals.empty())
    {
        int lineTop = (graphTop > 0) ? (graphTop - 1) : 0;
        Point last = mapSampleToPoint(pressVals.back().ts, dayStart, pressVals.back().value, minVal, maxVal,
                                      graphLeft, graphWidth, graphTop, graphHeight);
        dma_display->drawLine(last.x, axisY, last.x, lineTop, dma_display->color565(20, 40, 90));
    }

    unsigned long now = millis();
    if (now - lastBlinkToggle >= 500)
    {
        blinkOn = !blinkOn;
        lastBlinkToggle = now;
    }
    if (!pressVals.empty())
    {
        Point last = mapSampleToPoint(pressVals.back().ts, dayStart, pressVals.back().value, minVal, maxVal,
                                      graphLeft, graphWidth, graphTop, graphHeight);
        dma_display->drawPixel(last.x, last.y, blinkOn ? maxColor : lineColor);
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
        dayStart = midnightFor(co2Vals.back().ts);
        std::vector<TimedValue> filtered;
        filtered.reserve(co2Vals.size());
        for (const auto &tv : co2Vals)
        {
            if (tv.ts >= dayStart && tv.ts <= dayStart + 86400)
                filtered.push_back(tv);
        }
        co2Vals.swap(filtered);
    }

    dma_display->fillScreen(0);

    const bool mono = (theme == 1);
    const uint16_t headerBg = mono ? dma_display->color565(20, 20, 40) : INFOMODAL_HEADERBG;
    const uint16_t headerFg = mono ? dma_display->color565(60, 60, 120) : INFOMODAL_GREEN;
    const uint16_t underlineColor = mono ? dma_display->color565(30, 30, 70) : INFOMODAL_ULINE;
    const uint16_t statsColor = mono ? dma_display->color565(90, 90, 150) : INFOMODAL_UNSEL;
    // Pick an axis color distinct from both tick colors for clarity
    const uint16_t axisColor = mono ? dma_display->color565(90, 90, 140) : dma_display->color565(200, 200, 220);
    const uint16_t lineColor = mono ? dma_display->color565(170, 170, 230) : dma_display->color565(255, 170, 90);
    const uint16_t minColor = mono ? dma_display->color565(120, 160, 220) : dma_display->color565(90, 200, 255);
    const uint16_t maxColor = mono ? dma_display->color565(220, 140, 140) : dma_display->color565(255, 90, 90);

    const int headerHeight = 8;
    dma_display->fillRect(0, 0, PANEL_RES_X, headerHeight, headerBg);
    dma_display->setTextColor(headerFg);
    const char *title = "CO2 24h";
    int16_t tx1, ty1;
    uint16_t tw, th;
    dma_display->getTextBounds(title, 0, 0, &tx1, &ty1, &tw, &th);
    int titleX = 1; // left align, leave graph area for current value
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
        uint16_t textColors[] = {statsColor};
        uint16_t bgColors[] = {0};
        s_co2Scroll.setLineColors(textColors, bgColors, 1);
        prevCo2Stats = statsLine;
    }
    s_co2Scroll.update();
    s_co2Scroll.draw(0, statsY, statsColor);

    const int graphLeft = 1;
    const int graphWidth = 62;
    const int graphTop = headerHeight + 1;
    const int graphHeight = statsY - graphTop - 2;
    if (graphHeight < 2)
        return;

    // Draw only bottom border; remove top/left/right outline
    int axisY = graphTop + graphHeight;
    dma_display->drawFastHLine(graphLeft - 1, axisY, graphWidth + 2, axisColor);
    // Use red for all tick dots
    uint16_t morningTickColor = dma_display->color565(255, 60, 60);
    uint16_t afternoonTickColor = morningTickColor;
    drawXAxisTicks(graphLeft, graphWidth, axisY, morningTickColor, afternoonTickColor);

    static bool blinkOn = true;
    static unsigned long lastBlinkToggle = 0;

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

    // Vertical guide from axis to slightly above top (through latest point)
    if (!co2Vals.empty())
    {
        int lineTop = (graphTop > 0) ? (graphTop - 1) : 0;
        Point last = mapSampleToPoint(co2Vals.back().ts, dayStart, co2Vals.back().value, minVal, maxVal,
                                      graphLeft, graphWidth, graphTop, graphHeight);
        dma_display->drawLine(last.x, axisY, last.x, lineTop, dma_display->color565(20, 40, 90));
    }

    unsigned long now = millis();
    if (now - lastBlinkToggle >= 500)
    {
        blinkOn = !blinkOn;
        lastBlinkToggle = now;
    }
    if (!co2Vals.empty())
    {
        Point last = mapSampleToPoint(co2Vals.back().ts, dayStart, co2Vals.back().value, minVal, maxVal,
                                      graphLeft, graphWidth, graphTop, graphHeight);
        dma_display->drawPixel(last.x, last.y, blinkOn ? maxColor : lineColor);
    }

    // No current marker/label on the chart per request
}

void drawPredictionScreen()
{
    if (!dma_display)
        return;

    dma_display->setTextWrap(false);
    dma_display->setTextSize(1);
    dma_display->setFont(&Font5x7Uts);

    const auto &log = getSensorLog();
    if (log.size() < 2)
    {
        dma_display->fillScreen(0);
        dma_display->setTextColor(INFOMODAL_UNSEL);
        dma_display->setCursor(2, 10);
        dma_display->print("Not enough data");
        return;
    }

    size_t firstIdx = 0;
    size_t lastIdx = log.size() - 1;
    float tFirst = log[firstIdx].temp + tempOffset;
    float tLast = log[lastIdx].temp + tempOffset;
    float hFirst = log[firstIdx].hum;
    float hLast = log[lastIdx].hum;
    float pFirst = log[firstIdx].press;
    float pLast = log[lastIdx].press;

    double tempDeltaC = (double)tLast - (double)tFirst;
    double humDelta = (double)hLast - (double)hFirst;
    double pressDelta = (double)pLast - (double)pFirst;
    // Shorter-term pressure tendency (last ~6 samples) for near-term signal
    size_t shortIdx = (log.size() > 6) ? (log.size() - 6) : 0;
    double pressDeltaShort = (double)pLast - (double)log[shortIdx].press;

    String tempTrend = formatTempDelta(tempDeltaC);
    String humTrend = formatHumDelta(humDelta);
    String pressTrend = formatPressDelta(pressDelta);
    String pressTendency = "Steady";
    if (pressDeltaShort <= -3.0)
        pressTendency = "Rapid drop";
    else if (pressDeltaShort <= -1.5)
        pressTendency = "Falling";
    else if (pressDeltaShort >= 3.0)
        pressTendency = "Rapid rise";
    else if (pressDeltaShort >= 1.5)
        pressTendency = "Rising";

    String outlook = "Steady/Clouds";
    if (pressDelta < -3.0 && humDelta > 5.0)
        outlook = "Rain/Storm risk";
    else if (pressDelta < -1.5 && humDelta > 2.5)
        outlook = "Rain likely";
    else if (pressDelta > 2.0 && humDelta < -3.0)
        outlook = "Clearing/Fair";

    String tempDir = (tempDeltaC > 1.0) ? "Warming" : (tempDeltaC < -1.0) ? "Cooling" : "Stable";
    // Simple confidence estimate based on signal strength
    String confidence = "Medium";
    double absPress = fabs(pressDeltaShort);
    double absHum = fabs(humDelta);
    if (absPress > 3.5 || (absPress > 2.5 && absHum > 4.0))
        confidence = "High";
    else if (absPress < 1.0 && absHum < 2.0)
        confidence = "Low";

    // Gather last-24h ranges for readability
    uint32_t windowStart = log.back().ts > 86400 ? (log.back().ts - 86400) : 0;
    float tMin = NAN, tMax = NAN, hMin = NAN, hMax = NAN, pMin = NAN, pMax = NAN, co2Min = NAN, co2Max = NAN;
    auto updateMinMax = [](float v, float &mn, float &mx) {
        if (isnan(v)) return;
        if (isnan(mn) || v < mn) mn = v;
        if (isnan(mx) || v > mx) mx = v;
    };
    for (const auto &s : log)
    {
        if (s.ts < windowStart) continue;
        updateMinMax(s.temp + tempOffset, tMin, tMax);
        updateMinMax(s.hum, hMin, hMax);
        updateMinMax(s.press, pMin, pMax);
        updateMinMax(s.co2, co2Min, co2Max);
    }

    dma_display->fillScreen(0);

    const bool mono = (theme == 1);
    const uint16_t headerBg = mono ? dma_display->color565(20, 20, 40) : INFOMODAL_HEADERBG;
    const uint16_t headerFg = mono ? dma_display->color565(60, 60, 120) : INFOMODAL_GREEN;
    const uint16_t bodyColor = mono ? dma_display->color565(90, 90, 150) : INFOMODAL_UNSEL;
    // Match the theme palette: labels use the header color, values use the body color
    const uint16_t labelColor = mono ? dma_display->color565(220, 220, 120) : dma_display->color565(255, 240, 140);
    const uint16_t valueColor = mono ? dma_display->color565(120, 160, 255) : dma_display->color565(120, 200, 255);
    constexpr int matrixHeight = 24; // emulate a 64x24 dot-matrix window for scrolling

    const int headerHeight = 8;
    dma_display->fillRect(0, 0, PANEL_RES_X, headerHeight, headerBg);
    dma_display->setTextColor(headerFg, headerBg); // opaque title background
    const char *title = "Next 24h";
    dma_display->setCursor(1, 0);
    dma_display->print(title);

    // Build labeled lines with distinct colors and a bit more context
    // Current values are taken from the last sample to give a concise snapshot
    float currentTemp = tLast;
    float currentHum = hLast;
    float currentPress = pLast;
    String tempLine = "Temperature: " + tempDir + " (" + tempTrend + "), now " + formatTempShort(currentTemp, 1);
    String humLine = "Humidity: " + formatHumDelta(humDelta) + ", now " + formatHumShort(currentHum, 0);
    String pressLine = "Pressure: " + pressTrend + ", now " + formatPressShort(currentPress);
    String outlookLine = "Outlook: " + outlook;
    String tendLine = "Pressure Tend: " + pressTendency;
    String confLine = "Confidence: " + confidence;
    // Ranges for last 24h where available
    auto makeRange = [&](const String &label, float mn, float mx, const std::function<String(float)> &fmtFn) -> String {
        if (isnan(mn) || isnan(mx)) return "";
        return label + ": " + fmtFn(mn) + " - " + fmtFn(mx);
    };
    String tempRange = makeRange("Temp 24h", tMin, tMax, std::function<String(float)>([&](float v){ return formatTempShort(v, 1); }));
    String humRange = makeRange("Hum 24h", hMin, hMax, std::function<String(float)>([&](float v){ return formatHumShort(v, 0); }));
    String pressRange = makeRange("Press 24h", pMin, pMax, std::function<String(float)>([&](float v){ return formatPressShort(v); }));
    String co2Range;
    if (!isnan(co2Min) && !isnan(co2Max) && co2Min > 0 && co2Max > 0)
    {
        co2Range = "CO2 24h: " + String(static_cast<int>(co2Min + 0.5f)) + "-" +
                   String(static_cast<int>(co2Max + 0.5f)) + " ppm";
    }

    int textWidth = min(PANEL_RES_X - 2, 64); // constrain to 64px like a 64x24 matrix
    s_predictLines.clear();
    std::vector<uint16_t> lineColors;
    s_predictOffsets.clear();
    auto pushWrapped = [&](const String &line, uint16_t colorLabels, uint16_t colorValues) {
        // Split label/value at first colon for coloring
        int colon = line.indexOf(':');
        String labelPart = (colon >= 0) ? line.substring(0, colon + 1) : "";
        String valuePart = (colon >= 0) ? line.substring(colon + 1) : line;
        String combined = labelPart + valuePart;
        auto wrapped = wrapLines(combined, textWidth);
        for (const auto &w : wrapped)
        {
            // Apply label color if the wrapped line still starts inside the label span
            if (!labelPart.isEmpty() && w.startsWith(labelPart))
            {
                s_predictLines.push_back(w);
                lineColors.push_back(colorLabels);
                s_predictOffsets.push_back(0);
            }
            else
            {
                s_predictLines.push_back(w);
                lineColors.push_back(colorValues);
                s_predictOffsets.push_back(1); // indent value lines 1px under their labels
            }
        }
    };
    pushWrapped(tempLine, labelColor, valueColor);
    pushWrapped(humLine, labelColor, valueColor);
    pushWrapped(pressLine, labelColor, valueColor);
    pushWrapped(outlookLine, labelColor, valueColor);
    pushWrapped(tendLine, labelColor, valueColor);
    pushWrapped(confLine, labelColor, valueColor);
    if (tempRange.length()) pushWrapped(tempRange, labelColor, valueColor);
    if (humRange.length()) pushWrapped(humRange, labelColor, valueColor);
    if (pressRange.length()) pushWrapped(pressRange, labelColor, valueColor);
    if (co2Range.length()) pushWrapped(co2Range, labelColor, valueColor);

    s_predictRoll.setLines(s_predictLines, false); // keep position continuous
    s_predictRoll.setLineColors(lineColors);
    s_predictRoll.setLineOffsets(s_predictOffsets);
    // Use 500 ms gap hold to match other scroll-up screens
    s_predictRoll.setGapHoldMs(500);
    s_predictRoll.onUpPress(); // ensure auto-scroll enabled
    const int yStart = headerHeight + 1;
    const int bodyHeight = min(matrixHeight, PANEL_RES_Y - yStart); // visible body height
    const int entryY = yStart + bodyHeight; // enter from bottom of body
    const int exitY = 7; // exit one pixel higher above the header line
    s_predictRoll.setEntryExit(entryY, exitY);
    unsigned int speed = (verticalScrollSpeed > 0) ? static_cast<unsigned int>(verticalScrollSpeed) : 60u;
    s_predictRoll.setScrollSpeed(speed);

    dma_display->setTextColor(bodyColor);
    int y = yStart;
    for (const auto &line : s_predictLines)
    {
        dma_display->setCursor(0, y);
        dma_display->print(line);
        y += 8;
    }
}

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

void tickPredictionScreen()
{
    if (!dma_display) return;
    if (s_predictLines.empty())
        return;

    const bool mono = (theme == 1);
    const uint16_t headerBg = mono ? dma_display->color565(20, 20, 40) : INFOMODAL_HEADERBG;
    const uint16_t headerFg = mono ? dma_display->color565(60, 60, 120) : INFOMODAL_GREEN;
    uint16_t bodyColor = mono ? dma_display->color565(90, 90, 150) : INFOMODAL_UNSEL;
    constexpr int headerHeight = 8;
    constexpr int matrixHeight = 24;
    const int yStart = headerHeight; // body begins immediately under the title
    const int bodyHeight = min(matrixHeight, PANEL_RES_Y - yStart); // emulate 64x24 dot-matrix window

    // Clear only the body area (keep header intact)
    dma_display->fillRect(0, yStart, PANEL_RES_X, bodyHeight, 0);
    // No separator row; text starts directly under header
    s_predictRoll.setScrollSpeed((verticalScrollSpeed > 0) ? static_cast<unsigned int>(verticalScrollSpeed) : 60u);
    // Ensure exit stays one pixel above previous (y=7); entry from bottom of body
    s_predictRoll.setEntryExit(yStart + bodyHeight, 7);
    s_predictRoll.update();
    s_predictRoll.draw(*dma_display, 0, yStart, bodyHeight, bodyColor);

    // Repaint the header so scrolling text can never discolor it
    dma_display->fillRect(0, 0, PANEL_RES_X, headerHeight, headerBg);
    dma_display->setTextColor(headerFg, headerBg); // opaque title background
    dma_display->setCursor(1, 0);
    dma_display->print("Next 24h");
}

void handlePredictionDownPress()
{
    s_predictRoll.onDownPress();
}

void handlePredictionUpPress()
{
    s_predictRoll.onUpPress();
}
