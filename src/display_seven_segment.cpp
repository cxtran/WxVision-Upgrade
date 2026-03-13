#include "display_seven_segment.h"

#include <Arduino.h>
#include <cstdio>
#include <pgmspace.h>

#include "display.h"

namespace wxv {
namespace seg7 {

namespace {
constexpr uint8_t kDigitBitmapRows = 5;
constexpr uint8_t kDigitBitmapCols = 3;

// Source pattern provided by user: 1..9, 0, :, slot 21 (A), slot 22 (P)
const uint8_t kSevenSegmentDigits[13][kDigitBitmapRows] PROGMEM = {
    {0x40, 0x40, 0x40, 0x40, 0x40}, // 1
    {0xe0, 0x20, 0xe0, 0x80, 0xe0}, // 2
    {0xe0, 0x20, 0x60, 0x20, 0xe0}, // 3
    {0x80, 0x80, 0xa0, 0xe0, 0x20}, // 4
    {0xe0, 0x80, 0xe0, 0x20, 0xe0}, // 5
    {0xe0, 0x80, 0xe0, 0xa0, 0xe0}, // 6
    {0xe0, 0x20, 0x20, 0x20, 0x20}, // 7
    {0xe0, 0xa0, 0xe0, 0xa0, 0xe0}, // 8
    {0xe0, 0xa0, 0xe0, 0x20, 0xe0}, // 9
    {0xe0, 0xa0, 0xa0, 0xa0, 0xe0}, // 0
    {0x00, 0x40, 0x00, 0x40, 0x00}, // :
    {0x40, 0xa0, 0xe0, 0xa0, 0xa0}, // A
    {0xe0, 0xa0, 0xe0, 0x80, 0x80}  // P
};

int bitmapIndexForChar(char digit)
{
    switch (digit)
    {
    case '1': return 0;
    case '2': return 1;
    case '3': return 2;
    case '4': return 3;
    case '5': return 4;
    case '6': return 5;
    case '7': return 6;
    case '8': return 7;
    case '9': return 8;
    case '0': return 9;
    case ':': return 10;
    case 'A': return 11;
    case 'P': return 12;
    default: return -1;
    }
}

void formatTimeChars(char *buf, size_t bufSize, int hour24, int minute, bool use24h)
{
    int displayHour = hour24 % 24;
    char suffix = '\0';
    if (!use24h)
    {
        suffix = (displayHour >= 12) ? 'P' : 'A';
        if (displayHour == 0)
            displayHour = 12;
        else if (displayHour > 12)
            displayHour -= 12;
    }

    if (minute < 0)
        minute = 0;
    if (minute > 59)
        minute = 59;

    if (use24h)
        snprintf(buf, bufSize, "%02d:%02d", displayHour, minute);
    else
        snprintf(buf, bufSize, "%02d:%02d%c", displayHour, minute, suffix);
}

} // namespace

Metrics metricsForThickness(int thickness)
{
    if (thickness < 1)
        thickness = 1;

    Metrics metrics;
    metrics.digitWidth = kDigitBitmapCols * thickness;
    metrics.digitHeight = kDigitBitmapRows * thickness;
    metrics.colonWidth = thickness;
    metrics.spacing = 1;
    return metrics;
}

void drawSegment(int x, int y, bool horizontal, int length, uint16_t color, int thickness)
{
    if (length <= 0 || thickness <= 0)
        return;

    if (horizontal)
    {
        dma_display->fillRect(x, y, length, thickness, color);
    }
    else
    {
        dma_display->fillRect(x, y, thickness, length, color);
    }
}

void drawDigit(int x, int y, char digit, uint16_t color, int thickness, const Metrics &metrics)
{
    int bitmapIndex = bitmapIndexForChar(digit);
    if (bitmapIndex < 0 || thickness <= 0)
        return;

    for (int row = 0; row < kDigitBitmapRows; ++row)
    {
        uint8_t pattern = pgm_read_byte(&kSevenSegmentDigits[bitmapIndex][row]);
        for (int col = 0; col < kDigitBitmapCols; ++col)
        {
            uint8_t mask = static_cast<uint8_t>(0x80 >> col);
            if ((pattern & mask) == 0)
                continue;

            int px = x + col * thickness;
            int py = y + row * thickness;
            dma_display->fillRect(px, py, thickness, thickness, color);
        }
    }
}

void drawColon(int x, int y, uint16_t color, int thickness, const Metrics &metrics)
{
    drawDigit(x, y, ':', color, thickness, metrics);
}

int measureTime(int hour24, int minute, bool use24h, int thickness, const Metrics *customMetrics)
{
    Metrics metrics = customMetrics ? *customMetrics : metricsForThickness(thickness);

    char buf[8];
    formatTimeChars(buf, sizeof(buf), hour24, minute, use24h);

    int width = 0;
    bool afterColon = false;
    for (int i = 0; buf[i] != '\0'; ++i)
    {
        if (buf[i] == ':')
        {
            width -= 1;
            width += metrics.colonWidth + metrics.spacing;
            afterColon = true;
        }
        else
        {
            if (afterColon)
            {
                width += 1;
                afterColon = false;
            }
            width += metrics.digitWidth + metrics.spacing;
        }
    }

    return width;
}

int drawTime(int x, int y, int hour24, int minute, bool use24h, uint16_t color, int thickness,
             const Metrics *customMetrics, bool colonVisible)
{
    Metrics metrics = customMetrics ? *customMetrics : metricsForThickness(thickness);

    char buf[8];
    formatTimeChars(buf, sizeof(buf), hour24, minute, use24h);

    int cursorX = x;
    bool afterColon = false;
    for (int i = 0; buf[i] != '\0'; ++i)
    {
        if (buf[i] == ':')
        {
            cursorX -= 1;
            if (colonVisible)
                drawColon(cursorX, y, color, thickness, metrics);
            cursorX += metrics.colonWidth + metrics.spacing;
            afterColon = true;
        }
        else
        {
            if (afterColon)
            {
                cursorX += 1;
                afterColon = false;
            }
            drawDigit(cursorX, y, buf[i], color, thickness, metrics);
            cursorX += metrics.digitWidth + metrics.spacing;
        }
    }

    return cursorX - x;
}

} // namespace seg7
} // namespace wxv
