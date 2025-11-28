// Temperature history screen rendering
#include "graph.h"
#include <math.h>
#include <float.h>
#include "datalogger.h"
#include "display.h"
#include "InfoModal.h"
#include "units.h"
#include "settings.h"

extern int theme;

namespace
{
struct Point
{
    int x;
    int y;
};

String formatTempShort(float tempC, uint8_t decimals)
{
    if (isnan(tempC))
        return String("--");
    double disp = dispTemp(tempC);
    String out = String(disp, static_cast<unsigned int>(decimals));
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
    const uint16_t axisColor = mono ? dma_display->color565(70, 70, 130) : dma_display->color565(70, 130, 180);
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

    float minVal = tempsC[0];
    float maxVal = tempsC[0];
    size_t minIdx = 0, maxIdx = 0;
    for (size_t i = 1; i < tempsC.size(); ++i)
    {
        float v = tempsC[i];
        if (v < minVal)
        {
            minVal = v;
            minIdx = i;
        }
        if (v > maxVal)
        {
            maxVal = v;
            maxIdx = i;
        }
    }

    uint8_t decimals = (fabsf(maxVal - minVal) < 10.0f) ? 1 : 0;
    String minStr = formatTempShort(minVal, decimals);
    String maxStr = formatTempShort(maxVal, decimals);

    dma_display->setTextColor(statsColor);
    dma_display->setCursor(0, statsY);
    dma_display->print(minStr);

    int hiWidth = getTextWidth(maxStr.c_str());
    int hiX = 64 - hiWidth;
    if (hiX < 0)
        hiX = 0;
    dma_display->setCursor(hiX, statsY);
    dma_display->print(maxStr);

    const int graphLeft = 1;
    const int graphWidth = 62;
    const int graphTop = headerHeight + 1;
    const int graphHeight = statsY - graphTop - 2; // leave a gap before stats line
    if (graphHeight < 2)
        return;

    dma_display->drawRect(graphLeft - 1, graphTop - 1, graphWidth + 2, graphHeight + 2, axisColor);

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

    Point minPt = mapSampleToPoint(minIdx, tempsC.size(), minVal, minVal, maxVal,
                                   graphLeft, graphWidth, graphTop, graphHeight);
    Point maxPt = mapSampleToPoint(maxIdx, tempsC.size(), maxVal, minVal, maxVal,
                                   graphLeft, graphWidth, graphTop, graphHeight);

    dma_display->fillRect(minPt.x - 1, minPt.y - 1, 3, 3, minColor);
    dma_display->fillRect(maxPt.x - 1, maxPt.y - 1, 3, 3, maxColor);

    labelGraphPoint(minPt, graphTop, minColor, "L");
    labelGraphPoint(maxPt, graphTop, maxColor, "H");
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
    const uint16_t axisColor = mono ? dma_display->color565(70, 70, 130) : dma_display->color565(70, 130, 180);
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

    float minVal = co2Vals[0];
    float maxVal = co2Vals[0];
    size_t minIdx = 0, maxIdx = 0;
    for (size_t i = 1; i < co2Vals.size(); ++i)
    {
        float v = co2Vals[i];
        if (v < minVal)
        {
            minVal = v;
            minIdx = i;
        }
        if (v > maxVal)
        {
            maxVal = v;
            maxIdx = i;
        }
    }

    String minStr = formatCo2Short(minVal);
    String maxStr = formatCo2Short(maxVal);

    dma_display->setTextColor(statsColor);
    dma_display->setCursor(0, statsY);
    dma_display->print(minStr);

    int hiWidth = getTextWidth(maxStr.c_str());
    int hiX = 64 - hiWidth;
    if (hiX < 0)
        hiX = 0;
    dma_display->setCursor(hiX, statsY);
    dma_display->print(maxStr);

    const int graphLeft = 1;
    const int graphWidth = 62;
    const int graphTop = headerHeight + 1;
    const int graphHeight = statsY - graphTop - 2;
    if (graphHeight < 2)
        return;

    dma_display->drawRect(graphLeft - 1, graphTop - 1, graphWidth + 2, graphHeight + 2, axisColor);

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

    Point minPt = mapSampleToPoint(minIdx, co2Vals.size(), minVal, minVal, maxVal,
                                   graphLeft, graphWidth, graphTop, graphHeight);
    Point maxPt = mapSampleToPoint(maxIdx, co2Vals.size(), maxVal, minVal, maxVal,
                                   graphLeft, graphWidth, graphTop, graphHeight);

    dma_display->fillRect(minPt.x - 1, minPt.y - 1, 3, 3, minColor);
    dma_display->fillRect(maxPt.x - 1, maxPt.y - 1, 3, 3, maxColor);

    labelGraphPoint(minPt, graphTop, minColor, "L");
    labelGraphPoint(maxPt, graphTop, maxColor, "H");
}
