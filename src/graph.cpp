// Temperature history screen rendering
#include "graph.h"
#include <Arduino.h>
#include <math.h>
#include <float.h>
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

static ScrollLine s_tempScroll(PANEL_RES_X, 60);
static ScrollLine s_co2Scroll(PANEL_RES_X, 60);
static ScrollLine s_humScroll(PANEL_RES_X, 60);
static ScrollLine s_baroScroll(PANEL_RES_X, 60);
static std::vector<String> s_predictLines;
static RollingUpScreen s_predictRoll(PANEL_RES_X, PANEL_RES_Y - 9, 8);
static bool s_predictPaused = false;

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

Point mapSampleToPoint(size_t idx, size_t total, float value, float minVal, float maxVal,
                       int graphLeft, int graphWidth, int graphTop, int graphHeight)
{
    if (graphWidth < 1 || graphHeight < 1)
        return {graphLeft, graphTop};
    float range = maxVal - minVal;
    if (range < 0.1f)
        range = 0.1f;

    int x;
    if (total <= 1)
    {
        x = graphLeft + graphWidth / 2;
    }
    else
    {
        float pos = static_cast<float>(idx) / static_cast<float>(total - 1);
        x = graphLeft + static_cast<int>((graphWidth - 1) * pos + 0.5f);
    }

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
    std::vector<float> tempsC;
    tempsC.reserve(log.size());
    for (const auto &s : log)
    {
        if (!isnan(s.temp))
        {
            tempsC.push_back(s.temp + tempOffset);
        }
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

    float minVal = tempsC[0];
    float maxVal = tempsC[0];
    for (size_t i = 1; i < tempsC.size(); ++i)
    {
        float v = tempsC[i];
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
    String curStr = formatTempShort(tempsC.back(), decimals);

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

    Point prev{-1, -1};
    for (int xStep = 0; xStep < graphWidth; ++xStep)
    {
        size_t idx;
        if (tempsC.size() <= 1)
        {
            idx = 0;
        }
        else
        {
            float pos = static_cast<float>(xStep) / static_cast<float>(graphWidth - 1);
            idx = static_cast<size_t>(pos * (tempsC.size() - 1) + 0.5f);
            if (idx >= tempsC.size())
                idx = tempsC.size() - 1;
        }
        Point pt = mapSampleToPoint(idx, tempsC.size(), tempsC[idx], minVal, maxVal,
                                    graphLeft, graphWidth, graphTop, graphHeight);
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
    std::vector<float> hums;
    hums.reserve(log.size());
    for (const auto &s : log)
    {
        if (!isnan(s.hum))
        {
            hums.push_back(s.hum);
        }
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

    float minVal = hums[0];
    float maxVal = hums[0];
    for (size_t i = 1; i < hums.size(); ++i)
    {
        float v = hums[i];
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
    String curStr = formatHumShort(hums.back(), decimals);

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

    Point prev{-1, -1};
    for (int xStep = 0; xStep < graphWidth; ++xStep)
    {
        size_t idx;
        if (hums.size() <= 1)
        {
            idx = 0;
        }
        else
        {
            float pos = static_cast<float>(xStep) / static_cast<float>(graphWidth - 1);
            idx = static_cast<size_t>(pos * (hums.size() - 1) + 0.5f);
            if (idx >= hums.size())
                idx = hums.size() - 1;
        }
        Point pt = mapSampleToPoint(idx, hums.size(), hums[idx], minVal, maxVal,
                                    graphLeft, graphWidth, graphTop, graphHeight);
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
    std::vector<float> pressVals;
    pressVals.reserve(log.size());
    for (const auto &s : log)
    {
        if (!isnan(s.press) && s.press > 0.0f)
        {
            pressVals.push_back(s.press);
        }
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

    float minVal = pressVals[0];
    float maxVal = pressVals[0];
    for (size_t i = 1; i < pressVals.size(); ++i)
    {
        float v = pressVals[i];
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
    String curStr = formatPressShort(pressVals.back());

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

    Point prev{-1, -1};
    for (int xStep = 0; xStep < graphWidth; ++xStep)
    {
        size_t idx;
        if (pressVals.size() <= 1)
        {
            idx = 0;
        }
        else
        {
            float pos = static_cast<float>(xStep) / static_cast<float>(graphWidth - 1);
            idx = static_cast<size_t>(pos * (pressVals.size() - 1) + 0.5f);
            if (idx >= pressVals.size())
                idx = pressVals.size() - 1;
        }
        Point pt = mapSampleToPoint(idx, pressVals.size(), pressVals[idx], minVal, maxVal,
                                    graphLeft, graphWidth, graphTop, graphHeight);
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
    std::vector<float> co2Vals;
    co2Vals.reserve(log.size());
    for (const auto &s : log)
    {
        if (!isnan(s.co2) && s.co2 > 0.0f)
        {
            co2Vals.push_back(s.co2);
        }
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

    float minVal = co2Vals[0];
    float maxVal = co2Vals[0];
    for (size_t i = 1; i < co2Vals.size(); ++i)
    {
        float v = co2Vals[i];
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
    String curStr = formatCo2Short(co2Vals.back());

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

    Point prev{-1, -1};
    for (int xStep = 0; xStep < graphWidth; ++xStep)
    {
        size_t idx;
        if (co2Vals.size() <= 1)
        {
            idx = 0;
        }
        else
        {
            float pos = static_cast<float>(xStep) / static_cast<float>(graphWidth - 1);
            idx = static_cast<size_t>(pos * (co2Vals.size() - 1) + 0.5f);
            if (idx >= co2Vals.size())
                idx = co2Vals.size() - 1;
        }
        Point pt = mapSampleToPoint(idx, co2Vals.size(), co2Vals[idx], minVal, maxVal,
                                    graphLeft, graphWidth, graphTop, graphHeight);
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

    int textWidth = min(PANEL_RES_X - 2, 64); // constrain to 64px like a 64x24 matrix
    s_predictLines.clear();
    std::vector<uint16_t> lineColors;
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
            }
            else
            {
                s_predictLines.push_back(w);
                lineColors.push_back(colorValues);
            }
        }
    };
    pushWrapped(tempLine, labelColor, valueColor);
    pushWrapped(humLine, labelColor, valueColor);
    pushWrapped(pressLine, labelColor, valueColor);
    pushWrapped(outlookLine, labelColor, valueColor);
    pushWrapped(tendLine, labelColor, valueColor);
    pushWrapped(confLine, labelColor, valueColor);

    s_predictRoll.setLines(s_predictLines, false); // keep position continuous
    s_predictRoll.setLineColors(lineColors);
    s_predictRoll.setGapHoldMs(500); // 0.5 second gap between cycles
    s_predictRoll.setPaused(false);
    s_predictPaused = false;
    int yStart = headerHeight + 1;
    int entryY = yStart + matrixHeight;
    int exitY = 8; // exit just above the header line
    s_predictRoll.setEntryExit(entryY, exitY);
    // Enter from bottom of body (y=32) and exit just above header (y=9)
    unsigned int speed = (verticalScrollSpeed > 0) ? static_cast<unsigned int>(verticalScrollSpeed) : 60u;
    s_predictRoll.setScrollSpeed(speed);

    dma_display->setTextColor(bodyColor);
    int bodyHeight = min(matrixHeight, PANEL_RES_Y - yStart);
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
    // Ensure paused state applies
    s_predictRoll.setPaused(s_predictPaused);

    const bool mono = (theme == 1);
    const uint16_t headerBg = mono ? dma_display->color565(20, 20, 40) : INFOMODAL_HEADERBG;
    const uint16_t headerFg = mono ? dma_display->color565(60, 60, 120) : INFOMODAL_GREEN;
    uint16_t bodyColor = mono ? dma_display->color565(90, 90, 150) : INFOMODAL_UNSEL;
    constexpr int headerHeight = 8;
    constexpr int matrixHeight = 24;
    const int yStart = headerHeight + 1; // body begins under the title
    const int bodyHeight = min(matrixHeight, PANEL_RES_Y - yStart); // emulate 64x24 dot-matrix window

    // Clear only the body area (keep header intact)
    dma_display->fillRect(0, yStart, PANEL_RES_X, bodyHeight, 0);
    // Clear the separator row so no pixels linger under the title line
    dma_display->fillRect(0, yStart - 1, PANEL_RES_X, 1, 0);
    s_predictRoll.setScrollSpeed((verticalScrollSpeed > 0) ? static_cast<unsigned int>(verticalScrollSpeed) : 60u);
    s_predictRoll.update();
    s_predictRoll.draw(*dma_display, 0, yStart, bodyHeight, bodyColor);

    // Repaint the header so scrolling text can never discolor it
    dma_display->fillRect(0, 0, PANEL_RES_X, headerHeight, headerBg);
    dma_display->setTextColor(headerFg, headerBg); // opaque title background
    dma_display->setCursor(1, 0);
    dma_display->print("Next 24h");
}

void setPredictionScrollPaused(bool paused)
{
    s_predictPaused = paused;
}
