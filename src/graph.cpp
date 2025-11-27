// Temperature history screen rendering
#include "graph.h"
#include <math.h>
#include <float.h>
#include "datalogger.h"
#include "display.h"
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
    const uint16_t titleColor = mono ? dma_display->color565(120, 120, 200)
                                     : dma_display->color565(180, 230, 255);
    const uint16_t axisColor = mono ? dma_display->color565(70, 70, 130)
                                    : dma_display->color565(70, 130, 180);
    const uint16_t lineColor = mono ? dma_display->color565(170, 170, 230)
                                    : dma_display->color565(255, 170, 90);
    const uint16_t minColor = mono ? dma_display->color565(120, 160, 220)
                                   : dma_display->color565(90, 200, 255);
    const uint16_t maxColor = mono ? dma_display->color565(220, 140, 140)
                                   : dma_display->color565(255, 90, 90);

    dma_display->setTextColor(titleColor);
    dma_display->setCursor(0, 0);
    dma_display->print("Temp 24h");

    const int statsY = 8;

    if (tempsC.empty())
    {
        dma_display->setTextColor(axisColor);
        dma_display->setCursor(0, statsY);
        dma_display->print("No temp data");
        dma_display->setCursor(0, statsY + 8);
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

    dma_display->setTextColor(axisColor);
    dma_display->setCursor(0, statsY);
    dma_display->print("Lo");
    dma_display->setCursor(getTextWidth("Lo ") , statsY);
    dma_display->print(minStr);

    String hiLabel = "Hi " + maxStr;
    int hiWidth = getTextWidth(hiLabel.c_str());
    int hiX = 64 - hiWidth;
    if (hiX < 0)
        hiX = 0;
    dma_display->setCursor(hiX, statsY);
    dma_display->print(hiLabel);

    const int graphLeft = 1;
    const int graphWidth = 62;
    const int graphTop = statsY + 8;
    const int graphHeight = 32 - graphTop - 1;
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
}
