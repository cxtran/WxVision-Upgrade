#include "windmeter.h"
#include <math.h>
#include "display.h"

// Directions (8 compass points) angles in degrees: N, NE, E, SE, S, SW, W, NW
const float DIR_ANGLES[8] = {0, 45, 90, 135, 180, 225, 270, 315};

// Lengths of lines from center to tip in pixels for 24x24 icon
const int LINE_LENGTHS[8] = {
    10,  // N
    11,  // NE (one pixel longer)
    10,  // E
    11,  // SE (one pixel longer)
    10,  // S
    11,  // SW (one pixel longer)
    10,  // W
    11   // NW (one pixel longer)
};

// Animation timing constants (ms)
const unsigned long ANIM_DURATION_MIN = 150;   // fastest (high wind)
const unsigned long ANIM_DURATION_MAX = 1200;  // slowest (low wind)
const float WIND_SPEED_MAX = 15.0f;             // max wind speed (m/s)

int WindMeter::nearestDirection(float deg) {
    deg = fmod(deg, 360.0f);
    if (deg < 0) deg += 360.0f;

    int nearest = 0;
    float minDiff = 360.0f;
    for (int i = 0; i < 8; ++i) {
        float diff = fabs(deg - DIR_ANGLES[i]);
        if (diff > 180) diff = 360 - diff;
        if (diff < minDiff) {
            minDiff = diff;
            nearest = i;
        }
    }
    return nearest;
}

void WindMeter::drawDirectionLine(int cx, int cy, int dirIndex, uint16_t color) {
    float angleRad = DIR_ANGLES[dirIndex] * PI / 180.0f;
    int length = LINE_LENGTHS[dirIndex];

    int xEnd = cx + (int)(length * sin(angleRad));
    int yEnd = cy - (int)(length * cos(angleRad)); // y axis inverted on screen

    dma_display->drawLine(cx, cy, xEnd, yEnd, color);
}

void WindMeter::drawMovingPixel(int cx, int cy, int dirIndex, float windSpeed, uint16_t color) {
    if (windSpeed < 0) windSpeed = 0;
    if (windSpeed > WIND_SPEED_MAX) windSpeed = WIND_SPEED_MAX;

    // Map wind speed to animation duration (higher speed → shorter duration)
    unsigned long duration = ANIM_DURATION_MAX -
        (unsigned long)((windSpeed / WIND_SPEED_MAX) * (ANIM_DURATION_MAX - ANIM_DURATION_MIN));

    unsigned long elapsed = (millis() - animStartMillis) % duration;
    float progress = 1.0f - ((float)elapsed / duration); // from tip (1) to center (0)

    float angleRad = DIR_ANGLES[dirIndex] * PI / 180.0f;
    int length = LINE_LENGTHS[dirIndex];

    int xPos = cx + (int)(length * sin(angleRad) * progress);
    int yPos = cy - (int)(length * cos(angleRad) * progress);

    dma_display->drawPixel(xPos, yPos, color);
}

void WindMeter::drawWindDirection(int cx, int cy, float windDirDeg, float windSpeed) {
    // Define active color (bright yellow)
    const uint8_t activeR = 255;
    const uint8_t activeG = 255;
    const uint8_t activeB = 0;

    const uint16_t highlightColor = dma_display->color565(activeR, activeG, activeB);

    // Create dim color by scaling down RGB of active color (e.g., 20% brightness)
    uint8_t dimR = activeR * 0.2;
    uint8_t dimG = activeG * 0.2;
    uint8_t dimB = activeB * 0.2;

    const uint16_t dimColor = dma_display->color565(dimR, dimG, dimB);

    // Bright red for moving pixel
    const uint16_t pixelColor = dma_display->color565(255, 100, 100);

    dma_display->fillScreen(0);

    int nearestDir = nearestDirection(windDirDeg);

    // Draw all 8 direction lines with either highlight or dim color
    for (int i = 0; i < 8; ++i) {
        uint16_t color = (i == nearestDir) ? highlightColor : dimColor;
        drawDirectionLine(cx, cy, i, color);
    }

    // Draw moving pixel on active line
    drawMovingPixel(cx, cy, nearestDir, windSpeed, pixelColor);
}

