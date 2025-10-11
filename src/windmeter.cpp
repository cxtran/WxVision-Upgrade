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

// Animate from the TAIL (opposite side) THROUGH center to the TIP (wind direction)
// Move pixel from the UPWIND TIP toward the CENTER (no overshoot)
// Animate from the TAIL (opposite side) THROUGH center to the TIP (wind direction)
// Animate from the TAIL (opposite side) THROUGH center to the TIP (wind direction)
void WindMeter::drawMovingPixel(int cx, int cy, int dirIndex, float windSpeed, uint16_t color) {
    if (windSpeed < 0) windSpeed = 0;
    if (windSpeed > WIND_SPEED_MAX) windSpeed = WIND_SPEED_MAX;

    unsigned long duration = ANIM_DURATION_MAX -
        (unsigned long)((windSpeed / WIND_SPEED_MAX) * (ANIM_DURATION_MAX - ANIM_DURATION_MIN));
    if (duration < ANIM_DURATION_MIN) duration = ANIM_DURATION_MIN;
    if (animStartMillis == 0) animStartMillis = millis();

    unsigned long elapsed = (millis() - animStartMillis) % duration;
    float progress = (float)elapsed / duration;

    float angleRad = DIR_ANGLES[dirIndex] * PI / 180.0f;
    float dx = sin(angleRad);
    float dy = -cos(angleRad);

    int lenTip  = LINE_LENGTHS[dirIndex];
    int lenTail = LINE_LENGTHS[(dirIndex + 4) % 8];

    // endpoints along the highlighted spoke
    float tipX  = cx + lenTip  * dx;
    float tipY  = cy + lenTip  * dy;
    float tailX = cx - lenTail * dx;
    float tailY = cy - lenTail * dy;

    // ✅ move from TAIL → TIP (reversed from your version)
    float startX = tailX, startY = tailY;
    float endX   = tipX,  endY   = tipY;

    for (int i = 0; i < 3; ++i) {
        float p = progress - i * 0.08f;
        if (p < 0.0f) continue;
        if (p > 1.0f) p = 1.0f;

        float x = startX + (endX - startX) * p;
        float y = startY + (endY - startY) * p;
        dma_display->drawPixel((int)(x + 0.5f), (int)(y + 0.5f), color);
    }
}


void WindMeter::drawArrowHead(int cx, int cy, int dirIndex, uint16_t color) {
    constexpr float SQRT_HALF = 0.70710678f; // 1/sqrt(2) keeps sides at 45 degrees

    float angleRad = DIR_ANGLES[dirIndex] * PI / 180.0f;
    float dx = sin(angleRad);
    float dy = -cos(angleRad);

    float headLength = 3.0f * SQRT_HALF;       // 3-pixel edge length for all headings
    float lateral = headLength;                // matches depth to maintain 45-degree sides

    float tipX = cx + LINE_LENGTHS[dirIndex] * dx;
    float tipY = cy + LINE_LENGTHS[dirIndex] * dy;

    float baseX = tipX - headLength * dx;
    float baseY = tipY - headLength * dy;

    float px = -dy;
    float py = dx;

    int x1 = int(baseX + lateral * px + 0.5f);
    int y1 = int(baseY + lateral * py + 0.5f);
    int x2 = int(baseX - lateral * px + 0.5f);
    int y2 = int(baseY - lateral * py + 0.5f);
    int xtip = int(tipX + 0.5f);
    int ytip = int(tipY + 0.5f);

    dma_display->drawLine(x1, y1, xtip, ytip, color);
    dma_display->drawLine(x2, y2, xtip, ytip, color);
    dma_display->drawLine(x1, y1, x2, y2, color);
}

void WindMeter::drawWindDirection(int cx, int cy, float windDirDeg, float windSpeed) {
    // Convert FROM (meteorological) to TO (where it’s going)
    float drawDirDeg = fmodf(windDirDeg + 180.0f, 360.0f);

    const uint8_t activeR = 255, activeG = 255, activeB = 0;
    const uint16_t highlightColor = (theme == 1)
        ? dma_display->color565(90, 90, 150)
        : dma_display->color565(activeR, activeG, activeB);

    uint8_t dimR = static_cast<uint8_t>(activeR * 0.2f);
    uint8_t dimG = static_cast<uint8_t>(activeG * 0.2f);
    uint8_t dimB = static_cast<uint8_t>(activeB * 0.2f);
    const uint16_t dimColor = (theme == 1)
        ? dma_display->color565(40, 40, 90)
        : dma_display->color565(dimR, dimG, dimB);

    const uint16_t pixelColor = (theme == 1)
        ? dma_display->color565(140, 140, 200)
        : dma_display->color565(255, 100, 100);
    const uint16_t centerColor = (theme == 1)
        ? dma_display->color565(60, 60, 120)
        : dma_display->color565(40, 180, 180);

    dma_display->fillScreen(0);

    int nearestDir = nearestDirection(drawDirDeg);   // <-- use drawDirDeg now
    static int lastDir = -1;
    if (nearestDir != lastDir) {
        animStartMillis = millis();
        lastDir = nearestDir;
    }

    // dim all other spokes
    for (int i = 0; i < 8; ++i) {
        if (i != nearestDir) {
            drawDirectionLine(cx, cy, i, dimColor);
        }
    }

    // highlight chosen spoke + arrow head pointing TO direction
    drawDirectionLine(cx, cy, nearestDir, highlightColor);
    const uint16_t arrowColor = (theme == 1)
        ? dma_display->color565(128, 128, 220)
        : dma_display->color565(255, 220, 100);
    drawArrowHead(cx, cy, nearestDir, arrowColor);

    // center and moving pixel
    drawSprite3x3(cx - 1, cy - 1, sprite_FULL, centerColor);
    drawMovingPixel(cx, cy, nearestDir, windSpeed, pixelColor);
}







