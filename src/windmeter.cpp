#include "windmeter.h"
#include <math.h>
#include "display.h"

extern int theme;

// 3x3 sprites for moving pixel and center dot
const uint8_t sprite_NS[3][3]    = {{1,1,1},{0,1,0},{0,1,0}};
const uint8_t sprite_NE_SW[3][3] = {{1,1,1},{0,1,1},{1,0,1}};
const uint8_t sprite_EW[3][3]    = {{0,0,1},{1,1,0},{0,0,1}};
const uint8_t sprite_SE_NW[3][3] = {{1,1,1},{1,1,0},{1,0,1}};
const uint8_t sprite_SN[3][3]    = {{0,1,0},{0,1,0},{1,1,1}};
const uint8_t sprite_SW_NE[3][3] = {{1,1,1},{0,1,1},{1,0,1}};
const uint8_t sprite_WE[3][3]    = {{1,0,0},{1,1,1},{1,0,0}};
const uint8_t sprite_NW_SE[3][3] = {{1,0,1},{0,1,1},{1,1,1}};
const uint8_t sprite_FULL[3][3]  = {{1,1,1},{1,1,1},{1,1,1}};

const uint8_t (*sprites[8])[3] = {
    sprite_NS, sprite_NE_SW, sprite_EW, sprite_SE_NW,
    sprite_SN, sprite_SW_NE, sprite_WE, sprite_NW_SE
};

// Directions (8 compass points)
const float DIR_ANGLES[8] = {0, 45, 90, 135, 180, 225, 270, 315};
// Tweak lengths to fit your display (16x16, 24x24, etc)
const int LINE_LENGTHS[8] = {7, 8, 7, 8, 7, 8, 7, 8};

const unsigned long ANIM_DURATION_MIN = 150;
const unsigned long ANIM_DURATION_MAX = 1200;
const float WIND_SPEED_MAX = 15.0f;

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

// Draw 3x3 sprite
void WindMeter::drawSprite3x3(int x, int y, const uint8_t pattern[3][3], uint16_t color) {
    for (int dy = 0; dy < 3; ++dy)
        for (int dx = 0; dx < 3; ++dx)
            if (pattern[dy][dx]) dma_display->drawPixel(x + dx, y + dy, color);
}

// Always draws tip-to-tip
void WindMeter::drawDirectionLine(int cx, int cy, int dirIndex, uint16_t color) {
    float angleRad = DIR_ANGLES[dirIndex] * PI / 180.0f;
    int lenTip = LINE_LENGTHS[dirIndex];
    int xTip = cx + int(lenTip * sin(angleRad));
    int yTip = cy - int(lenTip * cos(angleRad));
    float oppAngleRad = fmod(angleRad + PI, 2 * PI);
    int lenOpp = LINE_LENGTHS[(dirIndex + 4) % 8];
    int xOpp = cx + int(lenOpp * sin(oppAngleRad));
    int yOpp = cy - int(lenOpp * cos(oppAngleRad));
    dma_display->drawLine(xOpp, yOpp, xTip, yTip, color);
}

// Animate from one tip THROUGH center to opposite tip
void WindMeter::drawMovingPixel(int cx, int cy, int dirIndex, float windSpeed, uint16_t color) {
    if (windSpeed < 0) windSpeed = 0;
    if (windSpeed > WIND_SPEED_MAX) windSpeed = WIND_SPEED_MAX;

    unsigned long duration = ANIM_DURATION_MAX - (unsigned long)((windSpeed / WIND_SPEED_MAX) * (ANIM_DURATION_MAX - ANIM_DURATION_MIN));
    unsigned long elapsed = (millis() - animStartMillis) % duration;
    float progress = (float)elapsed / duration;  // 0 to 1

    float angleRad = DIR_ANGLES[dirIndex] * PI / 180.0f;
    int lenTip = LINE_LENGTHS[dirIndex];

    int xTip = cx + int(lenTip * sin(angleRad));
    int yTip = cy - int(lenTip * cos(angleRad));

    float oppAngleRad = fmod(angleRad + PI, 2 * PI);
    int lenOpp = LINE_LENGTHS[(dirIndex + 4) % 8];
    int xOpp = cx + int(lenOpp * sin(oppAngleRad));
    int yOpp = cy - int(lenOpp * cos(oppAngleRad));

    int xPos = xTip + int((xOpp - xTip) * progress);
    int yPos = yTip + int((yOpp - yTip) * progress);

    drawSprite3x3(xPos - 1, yPos - 1, sprites[dirIndex], color);
}

void WindMeter::drawWindDirection(int cx, int cy, float windDirDeg, float windSpeed) {
    const uint8_t activeR = 255, activeG = 255, activeB = 0;
    const uint16_t highlightColor = (theme == 1) ? dma_display->color565(90,90,150)
                                : dma_display->color565(activeR, activeG, activeB);
    uint8_t dimR = activeR * 0.2, dimG = activeG * 0.2, dimB = activeB * 0.2;
    const uint16_t dimColor = (theme == 1) ? dma_display->color565(40,40,90)
                             : dma_display->color565(dimR, dimG, dimB);
    const uint16_t pixelColor = (theme == 1) ? dma_display->color565(140,140,200)
                              : dma_display->color565(255, 100, 100);
    const uint16_t centerColor = (theme == 1) ? dma_display->color565(60,60,120)
                               : dma_display->color565(40, 180, 180);

    dma_display->fillScreen(0);

    int nearestDir = nearestDirection(windDirDeg);

    // Draw ALL dim lines FIRST
    for (int i = 0; i < 8; ++i)
        if (i != nearestDir)
            drawDirectionLine(cx, cy, i, dimColor);

    // Draw the active line LAST so it always appears boldest
    drawDirectionLine(cx, cy, nearestDir, highlightColor);

    // Bold 3x3 center dot
    drawSprite3x3(cx - 1, cy - 1, sprite_FULL, centerColor);

    // Animated moving 3x3 pixel
    drawMovingPixel(cx, cy, nearestDir, windSpeed, pixelColor);
}
