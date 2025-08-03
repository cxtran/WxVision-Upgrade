#pragma once
#include <Arduino.h>

class WindMeter {
public:
    WindMeter() = default;

    // Call repeatedly to draw and animate the wind direction indicator inside a 16x16 box centered at (cx, cy)
    void drawWindDirection(int cx, int cy, float windDirDeg, float windSpeed);
private:
    // Map windDirDeg to nearest of 8 directions (0°, 45°, ..., 315°)
    int nearestDirection(float deg);

    // Draw the static direction line (length 7 pixels)
    void drawDirectionLine(int cx, int cy, int dirIndex, uint16_t color);

    // Animate the moving pixel from tip to center along the line
    void drawMovingPixel(int cx, int cy, int dirIndex, float windSpeed, uint16_t color);

    unsigned long animStartMillis = 0; // For animation timing
};

