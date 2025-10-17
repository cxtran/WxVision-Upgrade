#include <Arduino.h>
#include "utils.h"
#include "math.h"
#include "display.h"
#include "env_quality.h"
#define SCREEN_WIDTH 64   // Change to your actual width
int lastScrollMenuIndex = -1; // Track last selected menu index
#define CHAR_WIDTH 5


int customRoundString(const char *str) {
    double x = atof(str); // or use strtod
    double fractional = x - floor(x);
    if (fractional < 0.5)
        return (int)floor(x);
    else
        return (int)ceil(x);
}

/*


int getTextWidth(const char* text) {
    // For 5x7 font: 6 pixels per character (5 glyph + 1 space)
   // return strlen(text) * CHAR_WIDTH;
  int16_t x1, y1;
  uint16_t w, h;
  dma_display->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return w;

}


*/
bool needsScroll(const char* text) {
    return getTextWidth(text) > SCREEN_WIDTH;
}

unsigned long lastScrollTime = 0;
int scrollOffset = 0;

void drawScrollingText(const char* text, int y, uint16_t color, int selectedIndex)
{
    unsigned long now = millis();
    int textW = getTextWidth(text);

    // Reset scrollOffset if the selected line changed
    if (selectedIndex != lastScrollMenuIndex) {
        scrollOffset = 0;
        lastScrollMenuIndex = selectedIndex;
    }

    // Only update offset every 40ms for smooth scroll
    if (now - lastScrollTime > 40) {
        lastScrollTime = now;
        scrollOffset++;
        if (scrollOffset > (textW + 8)) scrollOffset = -SCREEN_WIDTH;
    }

    dma_display->setTextColor(color);
    dma_display->setCursor(-scrollOffset, y);
    dma_display->print(text);
}

void drawBackArrow(int x, int y, uint16_t color){
    dma_display->setCursor(x + 1, y );
    dma_display->drawLine(x + 1, y + 3, x + 3, y + 1, color);
    dma_display->drawLine(x + 1, y + 3, x + 3, y + 5, color);
    dma_display->drawLine(x + 1, y + 3, x + 5, y + 3, color);
}

static uint16_t brightenColorValue(uint16_t color, uint8_t boost)
{
    uint8_t r = ((color >> 11) & 0x1F) * 255 / 31;
    uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
    uint8_t b = (color & 0x1F) * 255 / 31;

    auto clamp = [](uint8_t value, uint8_t increase) -> uint8_t {
        uint16_t temp = static_cast<uint16_t>(value) + increase;
        if (temp > 255)
            temp = 255;
        return static_cast<uint8_t>(temp);
    };

    r = clamp(r, boost);
    g = clamp(g, boost);
    b = clamp(b, boost);
    return dma_display->color565(r, g, b);
}

static void fill7x7(int x, int y, uint16_t color)
{
    for (int dy = 0; dy < 7; ++dy)
    {
        dma_display->drawFastHLine(x, y + dy, 7, color);
    }
}

static void drawFaceBase(int x, int y, uint16_t faceColor)
{
    dma_display->fillCircle(x + 3, y + 3, 3, faceColor);
}

static void drawFaceEyes(int x, int y, uint16_t featureColor)
{
    dma_display->drawPixel(x + 2, y + 2, featureColor);
    dma_display->drawPixel(x + 4, y + 2, featureColor);
}

static void drawHappyFaceIcon(int x, int y, uint16_t faceColor, uint16_t featureColor)
{
    drawFaceBase(x, y, faceColor);
    drawFaceEyes(x, y, featureColor);
    dma_display->drawPixel(x + 1, y + 4, featureColor);
    dma_display->drawPixel(x + 2, y + 5, featureColor);
    dma_display->drawPixel(x + 3, y + 5, featureColor);
    dma_display->drawPixel(x + 4, y + 5, featureColor);
    dma_display->drawPixel(x + 5, y + 4, featureColor);
}

static void drawNeutralFaceIcon(int x, int y, uint16_t faceColor, uint16_t featureColor)
{
    drawFaceBase(x, y, faceColor);
    drawFaceEyes(x, y, featureColor);
    dma_display->drawFastHLine(x + 2, y + 4, 3, featureColor);
}

static void drawUnhappyFaceIcon(int x, int y, uint16_t faceColor, uint16_t featureColor)
{
    drawFaceBase(x, y, faceColor);
    drawFaceEyes(x, y, featureColor);
    dma_display->drawPixel(x + 1, y + 5, featureColor);
    dma_display->drawPixel(x + 2, y + 4, featureColor);
    dma_display->drawPixel(x + 3, y + 4, featureColor);
    dma_display->drawPixel(x + 4, y + 4, featureColor);
    dma_display->drawPixel(x + 5, y + 5, featureColor);
}

static void drawCriticalIcon(int x, int y, uint16_t color)
{
    for (int i = 1; i < 6; ++i)
    {
        dma_display->drawPixel(x + i, y + i, color);
        dma_display->drawPixel(x + 6 - i, y + i, color);
    }
}

static void drawUnknownIcon(int x, int y, uint16_t color)
{
    dma_display->drawRect(x, y, 7, 7, color);
    dma_display->drawPixel(x + 3, y + 3, color);
}

void drawEnvBandIcon(int x, int y, EnvBand band, uint16_t bandColor, uint16_t backgroundColor, bool emphasize, bool swapFaceForeground)
{
    if (!dma_display)
        return;

    uint16_t iconColor = emphasize ? brightenColorValue(bandColor, 40) : bandColor;
    uint16_t baseColor = backgroundColor;

    if (swapFaceForeground)
    {
        uint16_t temp = iconColor;
        iconColor = baseColor;
        baseColor = temp;
    }

    fill7x7(x, y, baseColor);

    uint16_t faceColor = iconColor;
    uint16_t featureColor = baseColor;
    if (swapFaceForeground)
    {
        uint16_t temp = faceColor;
        faceColor = featureColor;
        featureColor = temp;
    }

    switch (band)
    {
    case EnvBand::Good:
        drawHappyFaceIcon(x, y, faceColor, featureColor);
        break;
    case EnvBand::Moderate:
        drawNeutralFaceIcon(x, y, faceColor, featureColor);
        break;
    case EnvBand::Poor:
        drawUnhappyFaceIcon(x, y, faceColor, featureColor);
        break;
    case EnvBand::Critical:
        drawCriticalIcon(x, y, iconColor);
        break;
    default:
        drawUnknownIcon(x, y, iconColor);
        break;
    }
}
