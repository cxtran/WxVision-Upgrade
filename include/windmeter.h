#pragma once
#include <Arduino.h>

class WindMeter {
public:
    int nearestDirection(float deg);
    void drawDirectionLine(int cx, int cy, int dirIndex, uint16_t color);
    void drawMovingPixel(int cx, int cy, int dirIndex, float windSpeed, uint16_t color);
    void drawArrowHead(int cx, int cy, int dirIndex, uint16_t color);
    void drawWindDirection(int cx, int cy, float windDirDeg, float windSpeed);
    unsigned long animStartMillis = 0;

private:
    void drawSprite3x3(int x, int y, const uint8_t pattern[3][3], uint16_t color);
};
