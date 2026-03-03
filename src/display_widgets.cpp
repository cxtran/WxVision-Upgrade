#include <Arduino.h>
#include "display.h"
#include "display_widgets.h"
namespace
{
    int wifiSignalLevelFromRssi(int rssi)
    {
        if (rssi >= -55)
            return 3; // excellent
        if (rssi >= -67)
            return 2; // good
        if (rssi >= -75)
            return 1; // fair
        return 0;     // weak/very weak
    }
}
void drawSunIcon(int x, int y, uint16_t color)
{
    dma_display->drawLine(x + 3, y, x + 3, y + 6, color);
    dma_display->drawLine(x, y + 3, x + 6, y + 3, color);
    dma_display->drawLine(x + 1, y + 1, x + 5, y + 5, color);
    dma_display->drawLine(x + 5, y + 1, x + 1, y + 5, color);
}
void drawHouseIcon(int x, int y, uint16_t color)
{
    dma_display->drawPixel(x + 4, 0, color);
    dma_display->drawLine(x + 2, y + 2, x + 6, y + 2, color);
    dma_display->drawLine(x + 1, y + 3, x + 7, y + 3, color);
    dma_display->drawLine(x + 3, y + 1, x + 5, y + 1, color);
    dma_display->drawRect(x + 2, y + 4, 5, 3, color);
    dma_display->drawLine(x + 4, y + 5, x + 4, y + 6, color);
}
void drawHumidityIcon(int x, int y, uint16_t color)
{
    dma_display->drawPixel(x + 3, y, color);
    dma_display->drawLine(x + 2, y + 1, x + 4, y + 1, color);
    dma_display->drawLine(x + 1, y + 2, x + 5, y + 2, color);
    dma_display->drawLine(x + 1, y + 3, x + 5, y + 3, color);
    dma_display->drawLine(x + 1, y + 4, x + 5, y + 4, color);
    dma_display->drawLine(x + 2, y + 5, x + 4, y + 5, color);
}
void drawWiFiIcon(int x, int y, uint16_t dimColor, uint16_t activeColor, int rssi)
{
    int level = wifiSignalLevelFromRssi(rssi);
    dma_display->drawPixel(x + 3, y + 4, dimColor);
    dma_display->drawLine(x + 3, y + 4, x + 3, y + 6, dimColor);
    dma_display->drawLine(x + 2, y + 3, x + 4, y + 3, dimColor);
    dma_display->drawLine(x + 1, y + 2, x + 5, y + 2, dimColor);
    dma_display->drawLine(x + 0, y + 1, x + 6, y + 1, dimColor);
    dma_display->drawPixel(x + 3, y + 4, activeColor);
    dma_display->drawLine(x + 3, y + 4, x + 3, y + 6, activeColor);
    if (level >= 1)
        dma_display->drawLine(x + 2, y + 3, x + 4, y + 3, activeColor);
    if (level >= 2)
        dma_display->drawLine(x + 1, y + 2, x + 5, y + 2, activeColor);
    if (level >= 3)
        dma_display->drawLine(x + 0, y + 1, x + 6, y + 1, activeColor);
}
void drawAlarmIcon(int x, int y, uint16_t color)
{
    dma_display->drawLine(x + 2, y + 0, x + 3, y + 0, color);
    dma_display->drawLine(x + 1, y + 1, x + 4, y + 1, color);
    dma_display->drawLine(x + 1, y + 2, x + 4, y + 2, color);
    dma_display->drawLine(x + 1, y + 3, x + 4, y + 3, color);
    dma_display->drawLine(x + 0, y + 4, x + 5, y + 4, color);
    dma_display->drawLine(x + 2, y + 5, x + 3, y + 5, color);
}
