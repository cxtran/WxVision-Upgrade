#include <Arduino.h>
#include "utils.h"
#include "math.h"
#include "display.h"
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
