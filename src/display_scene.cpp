#include <Arduino.h>
#include <ctype.h>
#include "display.h"
#include "display_scene.h"
#include "datetimesettings.h"
#include "settings.h"

uint16_t scaleColor565(uint16_t color, float intensity)
{
    if (intensity <= 0.0f)
        return 0;
    if (intensity >= 1.0f)
        return color;

    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;

    int newR = static_cast<int>(r * intensity + 0.5f);
    int newG = static_cast<int>(g * intensity + 0.5f);
    int newB = static_cast<int>(b * intensity + 0.5f);

    if (newR > 31)
        newR = 31;
    if (newG > 63)
        newG = 63;
    if (newB > 31)
        newB = 31;
    if (newR < 0)
        newR = 0;
    if (newG < 0)
        newG = 0;
    if (newB < 0)
        newB = 0;

    return static_cast<uint16_t>((newR << 11) | (newG << 5) | newB);
}

static uint16_t lerpColor565(uint16_t a, uint16_t b, float t)
{
    if (t <= 0.0f)
        return a;
    if (t >= 1.0f)
        return b;

    int ar = (a >> 11) & 0x1F;
    int ag = (a >> 5) & 0x3F;
    int ab = a & 0x1F;

    int br = (b >> 11) & 0x1F;
    int bg = (b >> 5) & 0x3F;
    int bb = b & 0x1F;

    int nr = static_cast<int>(ar + (br - ar) * t + 0.5f);
    int ng = static_cast<int>(ag + (bg - ag) * t + 0.5f);
    int nb = static_cast<int>(ab + (bb - ab) * t + 0.5f);

    nr = constrain(nr, 0, 31);
    ng = constrain(ng, 0, 63);
    nb = constrain(nb, 0, 31);

    return static_cast<uint16_t>((nr << 11) | (ng << 5) | nb);
}

static void drawVerticalGradient(uint16_t topColor, uint16_t bottomColor)
{
    for (int y = 0; y < PANEL_RES_Y; ++y)
    {
        float t = (PANEL_RES_Y <= 1) ? 0.0f : static_cast<float>(y) / (PANEL_RES_Y - 1);
        uint16_t color = lerpColor565(topColor, bottomColor, t);
        dma_display->drawFastHLine(0, y, PANEL_RES_X, color);
    }
}

static void drawGroundBand(uint16_t color, int height = 8)
{
    if (height <= 0)
        return;
    int y = PANEL_RES_Y - height;
    if (y < 0)
        y = 0;
    dma_display->fillRect(0, y, PANEL_RES_X, height, color);
}

static void drawSkyGradient(const uint16_t *colors, int count)
{
    if (count <= 0)
    {
        dma_display->fillScreen(0);
        return;
    }
    if (count == 1)
    {
        dma_display->fillScreen(colors[0]);
        return;
    }

    const int totalRows = PANEL_RES_Y;
    for (int y = 0; y < totalRows; ++y)
    {
        float globalT = (totalRows <= 1) ? 0.0f : static_cast<float>(y) / (totalRows - 1);
        float scaled = globalT * (count - 1);
        int idx = static_cast<int>(scaled);
        if (idx >= count - 1)
            idx = count - 2;
        float localT = scaled - idx;
        uint16_t rowColor = lerpColor565(colors[idx], colors[idx + 1], localT);
        dma_display->drawFastHLine(0, y, PANEL_RES_X, rowColor);
    }
}

static void drawGroundGradient(const uint16_t *colors, int count, int height)
{
    if (height <= 0 || colors == nullptr || count <= 0)
        return;
    if (height > PANEL_RES_Y)
        height = PANEL_RES_Y;

    int startY = PANEL_RES_Y - height;
    for (int y = 0; y < height; ++y)
    {
        float globalT = (height <= 1) ? 0.0f : static_cast<float>(y) / (height - 1);
        if (count == 1)
        {
            dma_display->drawFastHLine(0, startY + y, PANEL_RES_X, colors[0]);
            continue;
        }
        float scaled = globalT * (count - 1);
        int idx = static_cast<int>(scaled);
        if (idx >= count - 1)
            idx = count - 2;
        float localT = scaled - idx;
        uint16_t rowColor = lerpColor565(colors[idx], colors[idx + 1], localT);
        dma_display->drawFastHLine(0, startY + y, PANEL_RES_X, rowColor);
    }
}

static void drawSun(int centerX, int centerY, int radius, uint16_t baseColor)
{
    if (radius < 3)
        radius = 3;
    centerX = constrain(centerX, radius, PANEL_RES_X - 1 - radius);
    centerY = constrain(centerY, radius, PANEL_RES_Y - 1 - radius);

    dma_display->fillCircle(centerX, centerY, radius, baseColor);
    dma_display->drawCircle(centerX, centerY, radius + 1, scaleColor565(baseColor, 0.7f));
}

static void drawCloud(int x, int y, uint16_t color, int radius)
{
    if (radius < 2)
        radius = 2;

    dma_display->fillCircle(x, y, radius, color);
    dma_display->fillCircle(x - radius, y + 2, radius - 1, color);
    dma_display->fillCircle(x + radius, y + 2, radius - 1, color);
    dma_display->fillCircle(x - radius / 2, y + radius, radius - 2, color);
    dma_display->fillCircle(x + radius / 2, y + radius, radius - 2, color);

    int bodyWidth = radius * 2 + 6;
    dma_display->fillRect(x - bodyWidth / 2, y + radius - 1, bodyWidth, radius + 2, color);
}

static void drawSnowflake(int x, int y, uint16_t color)
{
    if (x < 0 || x >= PANEL_RES_X || y < 0 || y >= PANEL_RES_Y)
        return;

    dma_display->drawPixel(x, y, color);
    if (x > 0)
        dma_display->drawPixel(x - 1, y, color);
    if (x < PANEL_RES_X - 1)
        dma_display->drawPixel(x + 1, y, color);
    if (y > 0)
        dma_display->drawPixel(x, y - 1, color);
    if (y < PANEL_RES_Y - 1)
        dma_display->drawPixel(x, y + 1, color);
}

static void drawStar(int x, int y, uint16_t color)
{
    if (x < 1 || x >= PANEL_RES_X - 1 || y < 1 || y >= PANEL_RES_Y - 1)
        return;

    dma_display->drawPixel(x, y, color);
    dma_display->drawPixel(x - 1, y, color);
    dma_display->drawPixel(x + 1, y, color);
    dma_display->drawPixel(x, y - 1, color);
    dma_display->drawPixel(x, y + 1, color);
}

static void drawCompactCloud(int cx, int cy, uint16_t color)
{
    uint16_t shade = scaleColor565(color, 0.85f);
    uint16_t outline = scaleColor565(color, 0.62f);
    uint16_t highlight = scaleColor565(color, 1.08f);
    dma_display->fillCircle(cx, cy, 4, color);
    dma_display->fillCircle(cx - 4, cy + 1, 3, shade);
    dma_display->fillCircle(cx + 4, cy + 1, 3, shade);
    dma_display->fillCircle(cx - 2, cy + 3, 2, color);
    dma_display->fillCircle(cx + 2, cy + 3, 2, color);

    // Outline + highlight for better definition on 64x32 panels.
    dma_display->drawCircle(cx, cy, 4, outline);
    dma_display->drawCircle(cx - 4, cy + 1, 3, outline);
    dma_display->drawCircle(cx + 4, cy + 1, 3, outline);
    dma_display->drawCircle(cx - 2, cy + 3, 2, outline);
    dma_display->drawCircle(cx + 2, cy + 3, 2, outline);
    dma_display->drawFastHLine(cx - 8, cy + 5, 17, outline);
    dma_display->drawPixel(cx - 2, cy - 2, highlight);
    dma_display->drawPixel(cx + 1, cy - 1, highlight);
}

// Condition Scene (64x24) helpers ------------------------------------------------
static constexpr int SCENE_W = PANEL_RES_X;
static constexpr int SCENE_H = 24; // top area used for the scene; bottom is reserved for marquee/status

static void clearConditionSceneArea()
{
    dma_display->fillRect(0, 0, SCENE_W, SCENE_H, myBLACK);
    if (PANEL_RES_Y > SCENE_H)
        dma_display->fillRect(0, SCENE_H, SCENE_W, PANEL_RES_Y - SCENE_H, myBLACK);
}

static void fillDayBackground()
{
    // Pixel-art style (banded) sky like the reference image.
    uint16_t top = dma_display->color565(10, 120, 255);
    uint16_t mid = dma_display->color565(40, 165, 255);
    uint16_t bot = dma_display->color565(85, 205, 255);
    for (int y = 0; y < SCENE_H; ++y)
    {
        uint16_t c = (y < 8) ? top : (y < 16 ? mid : bot);
        dma_display->drawFastHLine(0, y, SCENE_W, c);
    }
}

static void fillNightBackground(int starShiftX = 0)
{
    // Deep blue + stars like the reference image.
    uint16_t top = dma_display->color565(2, 6, 28);
    uint16_t mid = dma_display->color565(4, 10, 40);
    uint16_t bot = dma_display->color565(7, 16, 58);
    for (int y = 0; y < SCENE_H; ++y)
    {
        uint16_t c = (y < 8) ? top : (y < 16 ? mid : bot);
        dma_display->drawFastHLine(0, y, SCENE_W, c);
    }

    uint16_t star = dma_display->color565(230, 240, 255);
    // Fixed starfield (no flicker)
    const uint8_t stars[][2] = {
        {6, 3},  {13, 6}, {18, 2}, {28, 5}, {36, 3}, {44, 7},
        {52, 4}, {58, 6}, {24, 11}, {40, 12}, {12, 14}, {54, 13}
    };
    for (auto &p : stars)
    {
        int sx = static_cast<int>(p[0]) + starShiftX;
        if (sx < 0 || sx >= SCENE_W)
            continue;
        dma_display->drawPixel(sx, p[1], star);
        // occasional plus star
        if ((p[0] % 3) == 0)
        {
            if (sx > 0) dma_display->drawPixel(sx - 1, p[1], star);
            if (sx < SCENE_W - 1) dma_display->drawPixel(sx + 1, p[1], star);
            if (p[1] > 0) dma_display->drawPixel(sx, p[1] - 1, star);
            if (p[1] < SCENE_H - 1) dma_display->drawPixel(sx, p[1] + 1, star);
        }
    }
}

static void drawPixelSun(int cx, int cy)
{
    // Compact pixel sun with orange edge + yellow center
    uint16_t edge = dma_display->color565(255, 170, 0);
    uint16_t fill = dma_display->color565(255, 235, 70);
    uint16_t hi = dma_display->color565(255, 255, 150);
    const int r = 6;
    for (int dy = -r; dy <= r; ++dy)
    {
        int ady = abs(dy);
        int span = (ady == 6) ? 1 : (ady == 5 ? 3 : (ady == 4 ? 4 : (ady == 3 ? 5 : (ady == 2 ? 6 : (ady == 1 ? 6 : 6)))));
        int y = cy + dy;
        if (y < 0 || y >= SCENE_H) continue;
        for (int dx = -span; dx <= span; ++dx)
        {
            int x = cx + dx;
            if (x < 0 || x >= SCENE_W) continue;
            bool border = (ady == r) || (abs(dx) == span);
            dma_display->drawPixel(x, y, border ? edge : fill);
        }
    }
    // highlight
    dma_display->drawPixel(cx - 2, cy - 2, hi);
    dma_display->drawPixel(cx - 1, cy - 3, hi);
    // rays (8-bit style)
    dma_display->drawPixel(cx, cy - 9, edge);
    dma_display->drawPixel(cx, cy + 9, edge);
    dma_display->drawPixel(cx - 9, cy, edge);
    dma_display->drawPixel(cx + 9, cy, edge);
    dma_display->drawPixel(cx - 7, cy - 7, edge);
    dma_display->drawPixel(cx + 7, cy - 7, edge);
    dma_display->drawPixel(cx - 7, cy + 7, edge);
    dma_display->drawPixel(cx + 7, cy + 7, edge);
}

static void drawPixelMoon(int cx, int cy)
{
    // Crescent moon (filled disc minus offset disc), tuned for pixel art.
    uint16_t moon = dma_display->color565(255, 235, 95);
    uint16_t rim = dma_display->color565(255, 210, 40);
    uint16_t skyCut = dma_display->color565(4, 10, 40);
    const int r = 7;
    for (int dy = -r; dy <= r; ++dy)
    {
        int y = cy + dy;
        if (y < 0 || y >= SCENE_H) continue;
        int span = (int)sqrt((double)(r * r - dy * dy));
        for (int dx = -span; dx <= span; ++dx)
        {
            int x = cx + dx;
            if (x < 0 || x >= SCENE_W) continue;
            dma_display->drawPixel(x, y, moon);
        }
    }
    // cut-out to form crescent
    int cutR = 6;
    for (int dy = -cutR; dy <= cutR; ++dy)
    {
        int y = cy + dy;
        if (y < 0 || y >= SCENE_H) continue;
        int span = (int)sqrt((double)(cutR * cutR - dy * dy));
        for (int dx = -span; dx <= span; ++dx)
        {
            int x = cx + dx + 3;
            if (x < 0 || x >= SCENE_W) continue;
            dma_display->drawPixel(x, y, skyCut);
        }
    }
    // rim accent
    dma_display->drawPixel(cx - 5, cy - 4, rim);
    dma_display->drawPixel(cx - 6, cy - 1, rim);
    dma_display->drawPixel(cx - 5, cy + 2, rim);
}

static void drawPixelCloud(int x, int y, bool night)
{
    // Alternate style: smoother silhouette cloud using scanline spans.
    uint16_t outline = night ? dma_display->color565(92, 112, 150) : dma_display->color565(110, 132, 168);
    uint16_t fill = night ? dma_display->color565(176, 192, 220) : dma_display->color565(236, 243, 250);
    uint16_t shade = night ? dma_display->color565(128, 146, 184) : dma_display->color565(192, 208, 226);
    uint16_t hi = night ? dma_display->color565(214, 226, 246) : dma_display->color565(254, 255, 255);

    // left edge offset + run length for each row (about 24x11 cloud)
    const uint8_t spans[][2] = {
        {8, 8}, {6, 12}, {4, 16}, {3, 18}, {2, 20}, {1, 22},
        {1, 22}, {2, 20}, {3, 18}, {5, 14}, {7, 10}
    };

    for (int row = 0; row < 11; ++row)
    {
        int yy = y + row + 4; // move cloud up by 3 px
        if (yy < 0 || yy >= SCENE_H)
            continue;

        int start = x + spans[row][0] + 2;
        int width = spans[row][1];
        int end = start + width - 1;
        if (end < 0 || start >= SCENE_W)
            continue;

        for (int xx = start; xx <= end; ++xx)
        {
            if (xx < 0 || xx >= SCENE_W)
                continue;
            bool edge = (row == 0) || (row == 10) || (xx == start) || (xx == end);
            dma_display->drawPixel(xx, yy, edge ? outline : fill);
        }

        // underside shading inside cloud body
        if (row >= 6 && width > 6)
        {
            for (int xx = start + 2; xx <= end - 2; ++xx)
                dma_display->drawPixel(xx, yy, shade);
        }
    }

    // small top highlights to lift the shape
    dma_display->drawPixel(x + 14, y + 5, hi);
    dma_display->drawPixel(x + 18, y + 4, hi);
    dma_display->drawPixel(x + 21, y + 5, hi);
}

static void drawPixelRain(int x, int y, int w, bool night)
{
    uint16_t drop = night ? dma_display->color565(80, 170, 255) : dma_display->color565(0, 200, 255);
    uint16_t drop2 = night ? dma_display->color565(40, 120, 220) : dma_display->color565(0, 140, 220);
    int phase = (millis() / 130) % 6;
    for (int col = x; col < x + w; col += 4)
    {
        for (int i = 0; i < 6; ++i)
        {
            int yy = y + ((i * 3 + (col / 4) + phase) % 12);
            if (yy >= SCENE_H - 1) continue;
            dma_display->drawPixel(col, yy, (i % 2 == 0) ? drop : drop2);
            dma_display->drawPixel(col - 1, yy + 1, (i % 2 == 0) ? drop : drop2);
        }
    }
}

static void drawPixelSnow(int x, int y, int w, bool night)
{
    uint16_t snow = night ? dma_display->color565(200, 220, 255) : dma_display->color565(240, 255, 255);
    int phase = (millis() / 250) % 8;
    for (int col = x; col < x + w; col += 7)
    {
        for (int i = 0; i < 4; ++i)
        {
            int yy = y + ((i * 4 + (col / 7) + phase) % 12);
            if (yy >= SCENE_H - 1) continue;
            dma_display->drawPixel(col, yy, snow);
            if (col > 0) dma_display->drawPixel(col - 1, yy, snow);
            if (col < SCENE_W - 1) dma_display->drawPixel(col + 1, yy, snow);
            if (yy > 0) dma_display->drawPixel(col, yy - 1, snow);
            if (yy < SCENE_H - 1) dma_display->drawPixel(col, yy + 1, snow);
        }
    }
}

static void drawPixelBolt(int x, int y)
{
    uint16_t bolt = dma_display->color565(255, 210, 40);
    uint16_t hi = dma_display->color565(255, 255, 180);
    // thick zig-zag bolt
    dma_display->drawLine(x, y, x - 4, y + 7, bolt);
    dma_display->drawLine(x + 1, y, x - 3, y + 7, bolt);
    dma_display->drawLine(x - 4, y + 7, x + 2, y + 7, bolt);
    dma_display->drawLine(x + 2, y + 7, x - 5, y + 16, bolt);
    dma_display->drawLine(x - 5, y + 16, x + 3, y + 12, bolt);
    dma_display->drawLine(x + 3, y + 12, x - 1, y + 20, bolt);
    // highlight pixels
    dma_display->drawPixel(x - 1, y + 2, hi);
    dma_display->drawPixel(x - 2, y + 10, hi);
}

static void drawSceneSkyGradient(const uint16_t *colors, int count)
{
    if (count <= 0)
        return;
    for (int y = 0; y < SCENE_H; ++y)
    {
        int idx = (count == 1) ? 0 : (y * (count - 1)) / (SCENE_H - 1);
        dma_display->drawFastHLine(0, y, SCENE_W, colors[idx]);
    }
}

static uint16_t lerp565(uint16_t a, uint16_t b, int t256)
{
    uint8_t ar = ((a >> 11) & 0x1F) * 255 / 31;
    uint8_t ag = ((a >> 5) & 0x3F) * 255 / 63;
    uint8_t ab = (a & 0x1F) * 255 / 31;
    uint8_t br = ((b >> 11) & 0x1F) * 255 / 31;
    uint8_t bg = ((b >> 5) & 0x3F) * 255 / 63;
    uint8_t bb = (b & 0x1F) * 255 / 31;

    uint8_t r = (uint8_t)((ar * (256 - t256) + br * t256) / 256);
    uint8_t g = (uint8_t)((ag * (256 - t256) + bg * t256) / 256);
    uint8_t bl = (uint8_t)((ab * (256 - t256) + bb * t256) / 256);
    return dma_display->color565(r, g, bl);
}

static void drawSceneSkyLerp(uint16_t top, uint16_t bottom)
{
    for (int y = 0; y < SCENE_H; ++y)
    {
        int t256 = (SCENE_H <= 1) ? 0 : (y * 256) / (SCENE_H - 1);
        dma_display->drawFastHLine(0, y, SCENE_W, lerp565(top, bottom, t256));
    }
}

static void drawSceneGround(uint16_t groundTop, uint16_t groundBottom)
{
    const int h = 3;
    for (int i = 0; i < h; ++i)
    {
        int y = SCENE_H - h + i;
        int t256 = (h <= 1) ? 0 : (i * 256) / (h - 1);
        dma_display->drawFastHLine(0, y, SCENE_W, lerp565(groundTop, groundBottom, t256));
    }
    dma_display->drawFastHLine(0, SCENE_H - h - 1, SCENE_W, scaleColor565(groundTop, 1.1f));
}

static void drawCrescentMoon(int cx, int cy, int r, uint16_t moonColor, uint16_t skyColor)
{
    dma_display->fillCircle(cx, cy, r, moonColor);
    dma_display->fillCircle(cx + (r / 3), cy - (r / 4), r - 2, skyColor);
    dma_display->drawCircle(cx, cy, r, scaleColor565(moonColor, 0.75f));
}

static void drawSunWithGlow(int cx, int cy, int r, uint16_t sunColor)
{
    uint16_t glow = scaleColor565(sunColor, 0.45f);
    dma_display->fillCircle(cx, cy, r + 2, glow);
    dma_display->fillCircle(cx, cy, r, sunColor);
    dma_display->fillCircle(cx, cy, r - 3, dma_display->color565(255, 255, 160));
    dma_display->drawCircle(cx, cy, r, scaleColor565(sunColor, 0.75f));

    uint16_t ray = scaleColor565(sunColor, 0.80f);
    dma_display->drawLine(cx, cy - (r + 3), cx, cy - (r + 1), ray);
    dma_display->drawLine(cx, cy + (r + 1), cx, cy + (r + 3), ray);
    dma_display->drawLine(cx - (r + 3), cy, cx - (r + 1), cy, ray);
    dma_display->drawLine(cx + (r + 1), cy, cx + (r + 3), cy, ray);
    dma_display->drawLine(cx - (r + 2), cy - (r + 2), cx - r, cy - r, ray);
    dma_display->drawLine(cx + r, cy - (r + 2), cx + (r + 2), cy - r, ray);
    dma_display->drawLine(cx - (r + 2), cy + (r + 2), cx - r, cy + r, ray);
    dma_display->drawLine(cx + r, cy + (r + 2), cx + (r + 2), cy + r, ray);
}

static void drawCloudIconLarge(int x, int y, uint16_t cLight, uint16_t cMid, uint16_t cDark, uint16_t outline)
{
    // Large readable cloud (~40x16)
    dma_display->fillCircle(x + 10, y + 8, 6, cLight);
    dma_display->fillCircle(x + 20, y + 6, 7, cMid);
    dma_display->fillCircle(x + 30, y + 8, 6, cDark);
    dma_display->fillRoundRect(x + 6, y + 10, 32, 7, 3, cMid);
    dma_display->fillRect(x + 6, y + 12, 32, 5, cMid);

    // Shading band
    dma_display->drawFastHLine(x + 7, y + 12, 30, scaleColor565(cMid, 0.85f));
    dma_display->drawFastHLine(x + 8, y + 13, 28, scaleColor565(cMid, 0.80f));

    // Outline
    dma_display->drawCircle(x + 10, y + 8, 6, outline);
    dma_display->drawCircle(x + 20, y + 6, 7, outline);
    dma_display->drawCircle(x + 30, y + 8, 6, outline);
    dma_display->drawRoundRect(x + 6, y + 10, 32, 7, 3, outline);
    dma_display->drawFastHLine(x + 6, y + 16, 32, outline);

    // Highlights
    dma_display->drawPixel(x + 17, y + 3, scaleColor565(cLight, 1.08f));
    dma_display->drawPixel(x + 22, y + 4, scaleColor565(cLight, 1.08f));
}

static void drawRainStreaks(int x0, int y0, int w, int yMax)
{
    int phase = (millis() / 120) % 6;
    uint16_t dropA = dma_display->color565(170, 245, 255);
    uint16_t dropB = dma_display->color565(80, 170, 230);
    for (int x = x0; x < x0 + w; x += 4)
    {
        for (int i = 0; i < 10; ++i)
        {
            int y = y0 + ((i * 3 + (x / 4) + phase) % 16);
            if (y >= yMax)
                continue;
            uint16_t c = (i % 3 == 0) ? dropB : dropA;
            dma_display->drawPixel(x, y, c);
            if (x > 0 && y + 1 < yMax)
                dma_display->drawPixel(x - 1, y + 1, c);
        }
    }
}

static void drawSnowDrift(int x0, int y0, int w, int yMax, uint16_t snowColor)
{
    int phase = (millis() / 220) % 8;
    for (int x = x0; x < x0 + w; x += 7)
    {
        for (int i = 0; i < 5; ++i)
        {
            int y = y0 + ((i * 4 + (x / 7) + phase) % 16);
            if (y >= yMax)
                continue;
            drawSnowflake(x, y, snowColor);
        }
    }
}

static void drawLightningBolt(int tipX, int tipY, uint16_t bolt, uint16_t glow)
{
    dma_display->drawLine(tipX, tipY, tipX - 4, tipY + 8, bolt);
    dma_display->drawLine(tipX - 4, tipY + 8, tipX + 1, tipY + 8, bolt);
    dma_display->drawLine(tipX + 1, tipY + 8, tipX - 6, tipY + 18, bolt);
    dma_display->drawLine(tipX - 6, tipY + 18, tipX + 3, tipY + 14, bolt);
    dma_display->drawLine(tipX + 3, tipY + 14, tipX - 1, tipY + 22, bolt);
    dma_display->drawLine(tipX, tipY, tipX - 4, tipY + 8, glow);
}

static void drawWeatherSceneSunny()
{
    clearConditionSceneArea();
    fillDayBackground();
    // Clear/Sunny: keep sky visually clear (no cloud glyphs).
    drawPixelSun(50, 11);
}

static void drawWeatherScenePartlyCloudy()
{
    clearConditionSceneArea();
    fillDayBackground();
    drawPixelSun(48, 10);
    drawPixelCloud(22, 9, false);
}

static void drawWeatherScenePartlyCloudyNight()
{
    clearConditionSceneArea();
    fillNightBackground();
    drawPixelMoon(49, 9);
    drawPixelCloud(22, 9, true);
}

static void drawWeatherSceneCloudy()
{
    clearConditionSceneArea();
    fillDayBackground();
    // Keep cloud mass away from top-left temp overlay.
    drawPixelCloud(20, 7, false);
    drawPixelCloud(31, 10, false);
}

static void drawWeatherSceneCloudyNight()
{
    clearConditionSceneArea();
    fillNightBackground();
    drawPixelMoon(50, 9);
    drawPixelCloud(21, 8, true);
}

static void drawWeatherSceneOvercast()
{
    clearConditionSceneArea();
    // Flat muted sky for overcast.
    const uint16_t overcastSky[] = {
        dma_display->color565(118, 138, 156),
        dma_display->color565(130, 148, 165),
        dma_display->color565(142, 158, 174)
    };
    drawSceneSkyGradient(overcastSky, 3);
    drawPixelCloud(19, 7, false);
    drawPixelCloud(30, 10, false);
}

static void drawWeatherSceneOvercastNight()
{
    clearConditionSceneArea();
    const uint16_t overcastNight[] = {
        dma_display->color565(12, 18, 34),
        dma_display->color565(16, 24, 42),
        dma_display->color565(20, 30, 50)
    };
    drawSceneSkyGradient(overcastNight, 3);
    drawPixelCloud(19, 7, true);
    drawPixelCloud(30, 10, true);
}

static void drawWeatherSceneFog()
{
    clearConditionSceneArea();
    fillDayBackground();
    drawPixelCloud(24, 8, false);
    const uint16_t fog = dma_display->color565(205, 218, 230);
    dma_display->drawFastHLine(16, 15, 44, fog);
    dma_display->drawFastHLine(14, 18, 46, fog);
    dma_display->drawFastHLine(18, 21, 40, fog);
}

static void drawWeatherSceneFogNight()
{
    clearConditionSceneArea();
    fillNightBackground();
    drawPixelMoon(50, 8);
    drawPixelCloud(24, 8, true);
    const uint16_t fog = dma_display->color565(145, 165, 195);
    dma_display->drawFastHLine(16, 15, 44, fog);
    dma_display->drawFastHLine(14, 18, 46, fog);
    dma_display->drawFastHLine(18, 21, 40, fog);
}

static void drawWeatherSceneWindy()
{
    clearConditionSceneArea();
    fillDayBackground();
    drawPixelCloud(24, 8, false);
    const uint16_t gust = dma_display->color565(235, 245, 255);
    dma_display->drawLine(18, 16, 36, 16, gust);
    dma_display->drawLine(14, 19, 42, 19, gust);
    dma_display->drawLine(20, 22, 46, 22, gust);
}

static void drawWeatherSceneWindyNight()
{
    clearConditionSceneArea();
    fillNightBackground();
    drawPixelMoon(50, 8);
    drawPixelCloud(24, 8, true);
    const uint16_t gust = dma_display->color565(170, 196, 230);
    dma_display->drawLine(18, 16, 36, 16, gust);
    dma_display->drawLine(14, 19, 42, 19, gust);
    dma_display->drawLine(20, 22, 46, 22, gust);
}

static void drawWeatherSceneRain()
{
    clearConditionSceneArea();
    fillDayBackground();
    drawPixelCloud(22, 6, false);
    drawPixelRain(20, 14, 40, false);
    drawPixelRain(4, 14, 12, false);   // left side band
    drawPixelRain(52, 15, 10, false);  // right side band
}

static void drawWeatherSceneRainNight()
{
    clearConditionSceneArea();
    fillNightBackground();
    drawPixelMoon(50, 9);
    drawPixelCloud(22, 7, true);
    drawPixelRain(20, 14, 40, true);
    drawPixelRain(4, 14, 12, true);    // left side band
    drawPixelRain(52, 15, 10, true);   // right side band
}

static void drawWeatherSceneThunderstorm()
{
    clearConditionSceneArea();
    fillDayBackground();
    drawPixelCloud(22, 6, false);
    drawPixelBolt(42, 5);
    drawPixelRain(20, 14, 40, false);
    drawPixelRain(4, 14, 12, false);   // left side band
    drawPixelRain(52, 15, 10, false);  // right side band
}

static void drawWeatherSceneThunderstormNight()
{
    clearConditionSceneArea();
    fillNightBackground();
    drawPixelMoon(50, 9);
    drawPixelCloud(22, 7, true);
    drawPixelBolt(42, 5);
    drawPixelRain(20, 14, 40, true);
    drawPixelRain(4, 14, 12, true);    // left side band
    drawPixelRain(52, 15, 10, true);   // right side band
}

static void drawWeatherSceneSnow()
{
    clearConditionSceneArea();
    fillDayBackground();
    drawPixelCloud(22, 6, false);
    drawPixelSnow(20, 14, 40, false);
}

static void drawWeatherSceneSnowNight()
{
    clearConditionSceneArea();
    fillNightBackground();
    drawPixelMoon(50, 9);
    drawPixelCloud(22, 7, true);
    drawPixelSnow(20, 14, 40, true);
}

static void drawWeatherSceneClearNight()
{
    clearConditionSceneArea();
    fillNightBackground();
    // Clear Night: moon + stars only (like reference)
    drawPixelMoon(54, 9);
}

struct WeatherSceneRenderer
{
    WeatherSceneKind kind;
    void (*drawFn)();
};

static void drawWeatherSceneDefault()
{
    drawWeatherSceneSunny();
}

static const WeatherSceneRenderer WEATHER_SCENE_RENDERERS[] = {
    {WeatherSceneKind::Sunny, drawWeatherSceneSunny},
    {WeatherSceneKind::SunnyNight, drawWeatherSceneClearNight},
    {WeatherSceneKind::PartlyCloudy, drawWeatherScenePartlyCloudy},
    {WeatherSceneKind::PartlyCloudyNight, drawWeatherScenePartlyCloudyNight},
    {WeatherSceneKind::Cloudy, drawWeatherSceneCloudy},
    {WeatherSceneKind::CloudyNight, drawWeatherSceneCloudyNight},
    {WeatherSceneKind::Overcast, drawWeatherSceneOvercast},
    {WeatherSceneKind::OvercastNight, drawWeatherSceneOvercastNight},
    {WeatherSceneKind::Fog, drawWeatherSceneFog},
    {WeatherSceneKind::FogNight, drawWeatherSceneFogNight},
    {WeatherSceneKind::Windy, drawWeatherSceneWindy},
    {WeatherSceneKind::WindyNight, drawWeatherSceneWindyNight},
    {WeatherSceneKind::Rain, drawWeatherSceneRain},
    {WeatherSceneKind::RainNight, drawWeatherSceneRainNight},
    {WeatherSceneKind::Thunderstorm, drawWeatherSceneThunderstorm},
    {WeatherSceneKind::ThunderstormNight, drawWeatherSceneThunderstormNight},
    {WeatherSceneKind::Snow, drawWeatherSceneSnow},
    {WeatherSceneKind::SnowNight, drawWeatherSceneSnowNight},
    {WeatherSceneKind::ClearNight, drawWeatherSceneClearNight}
};

struct WeatherSceneAlias
{
    const char *key;
    WeatherSceneKind kind;
};

static const WeatherSceneAlias WEATHER_SCENE_ALIASES[] = {
    {"sunny", WeatherSceneKind::Sunny},
    {"clear", WeatherSceneKind::Sunny},
    {"clear day", WeatherSceneKind::Sunny},
    {"clear sky", WeatherSceneKind::Sunny},
    {"clean", WeatherSceneKind::Sunny},
    {"clean day", WeatherSceneKind::Sunny},
    {"fair", WeatherSceneKind::Sunny},
    {"cloudy", WeatherSceneKind::Cloudy},
    {"mostly cloudy", WeatherSceneKind::Cloudy},
    {"partly cloudy", WeatherSceneKind::PartlyCloudy},
    {"partly sunny", WeatherSceneKind::PartlyCloudy},
    {"mostly clear", WeatherSceneKind::PartlyCloudy},
    {"overcast", WeatherSceneKind::Overcast},
    {"overcast clouds", WeatherSceneKind::Overcast},
    {"few clouds", WeatherSceneKind::PartlyCloudy},
    {"scattered clouds", WeatherSceneKind::PartlyCloudy},
    {"broken clouds", WeatherSceneKind::PartlyCloudy},
    {"mist", WeatherSceneKind::Fog},
    {"fog", WeatherSceneKind::Fog},
    {"haze", WeatherSceneKind::Fog},
    {"smoke", WeatherSceneKind::Fog},
    {"dust", WeatherSceneKind::Fog},
    {"sand", WeatherSceneKind::Fog},
    {"ash", WeatherSceneKind::Fog},
    {"windy", WeatherSceneKind::Windy},
    {"breezy", WeatherSceneKind::Windy},
    {"gusty", WeatherSceneKind::Windy},
    {"rain", WeatherSceneKind::Rain},
    {"rainy", WeatherSceneKind::Rain},
    {"showers", WeatherSceneKind::Rain},
    {"shower rain", WeatherSceneKind::Rain},
    {"light rain", WeatherSceneKind::Rain},
    {"moderate rain", WeatherSceneKind::Rain},
    {"heavy rain", WeatherSceneKind::Rain},
    {"heavy intensity rain", WeatherSceneKind::Rain},
    {"drizzle", WeatherSceneKind::Rain},
    {"squalls", WeatherSceneKind::Rain},
    {"thunderstorm", WeatherSceneKind::Thunderstorm},
    {"thunderstorms", WeatherSceneKind::Thunderstorm},
    {"storm", WeatherSceneKind::Thunderstorm},
    {"tstorm", WeatherSceneKind::Thunderstorm},
    {"tornado", WeatherSceneKind::Thunderstorm},
    {"snow", WeatherSceneKind::Snow},
    {"snowy", WeatherSceneKind::Snow},
    {"light snow", WeatherSceneKind::Snow},
    {"heavy snow", WeatherSceneKind::Snow},
    {"snow shower", WeatherSceneKind::Snow},
    {"flurries", WeatherSceneKind::Snow},
    {"sleet", WeatherSceneKind::Snow},
    {"clear night", WeatherSceneKind::ClearNight},
    {"clean night", WeatherSceneKind::ClearNight},
    {"night", WeatherSceneKind::ClearNight},
    {"mostly clear night", WeatherSceneKind::ClearNight},
    {nullptr, WeatherSceneKind::Unknown}
};

static String normalizeConditionKey(const String &condition)
{
    String key = condition;
    key.trim();
    key.toLowerCase();
    key.replace('-', ' ');
    key.replace('_', ' ');
    while (key.indexOf("  ") >= 0)
    {
        key.replace("  ", " ");
    }
    return key;
}

WeatherSceneKind resolveWeatherSceneKind(const String &condition)
{
    auto isNightNow = []() {
        DateTime utcNow = rtc.now();
        int offsetMinutes = timezoneIsCustom() ? tzStandardOffset : timezoneOffsetForUtc(utcNow);
        DateTime local = utcToLocal(utcNow, offsetMinutes);
        int hr = local.hour();
        return (hr < 6 || hr >= 18);
    };

    bool night = isNightNow();
    String normalized = normalizeConditionKey(condition);
    if (normalized.length() == 0)
        return night ? WeatherSceneKind::SunnyNight : WeatherSceneKind::Sunny;

    for (int i = 0; WEATHER_SCENE_ALIASES[i].key != nullptr; ++i)
    {
        if (normalized.equals(WEATHER_SCENE_ALIASES[i].key))
        {
            WeatherSceneKind base = WEATHER_SCENE_ALIASES[i].kind;
            if (base == WeatherSceneKind::Sunny && night)
                return WeatherSceneKind::SunnyNight;
            if (base == WeatherSceneKind::PartlyCloudy && night)
                return WeatherSceneKind::PartlyCloudyNight;
            if (base == WeatherSceneKind::Cloudy && night)
                return WeatherSceneKind::CloudyNight;
            if (base == WeatherSceneKind::Overcast && night)
                return WeatherSceneKind::OvercastNight;
            if (base == WeatherSceneKind::Fog && night)
                return WeatherSceneKind::FogNight;
            if (base == WeatherSceneKind::Windy && night)
                return WeatherSceneKind::WindyNight;
            if (base == WeatherSceneKind::Rain && night)
                return WeatherSceneKind::RainNight;
            if (base == WeatherSceneKind::Thunderstorm && night)
                return WeatherSceneKind::ThunderstormNight;
            if (base == WeatherSceneKind::Snow && night)
                return WeatherSceneKind::SnowNight;
            return base;
        }
    }

    if (normalized.indexOf("thunder") >= 0 || normalized.indexOf("storm") >= 0)
        return night ? WeatherSceneKind::ThunderstormNight : WeatherSceneKind::Thunderstorm;
    if (normalized.indexOf("wind") >= 0 || normalized.indexOf("breez") >= 0 || normalized.indexOf("gust") >= 0)
        return night ? WeatherSceneKind::WindyNight : WeatherSceneKind::Windy;
    if (normalized.indexOf("rain") >= 0 || normalized.indexOf("shower") >= 0 || normalized.indexOf("drizzle") >= 0)
        return night ? WeatherSceneKind::RainNight : WeatherSceneKind::Rain;
    if (normalized.indexOf("snow") >= 0 || normalized.indexOf("sleet") >= 0 || normalized.indexOf("flurry") >= 0)
        return night ? WeatherSceneKind::SnowNight : WeatherSceneKind::Snow;
    if (normalized.indexOf("night") >= 0)
        return WeatherSceneKind::ClearNight;
    if (normalized.indexOf("overcast") >= 0)
        return night ? WeatherSceneKind::OvercastNight : WeatherSceneKind::Overcast;
    if (normalized.indexOf("fog") >= 0 || normalized.indexOf("mist") >= 0 || normalized.indexOf("haze") >= 0 ||
        normalized.indexOf("smoke") >= 0 || normalized.indexOf("dust") >= 0 || normalized.indexOf("sand") >= 0 || normalized.indexOf("ash") >= 0)
        return night ? WeatherSceneKind::FogNight : WeatherSceneKind::Fog;
    if (normalized.indexOf("partly") >= 0 || normalized.indexOf("few cloud") >= 0 || normalized.indexOf("scattered cloud") >= 0 || normalized.indexOf("broken cloud") >= 0)
        return night ? WeatherSceneKind::PartlyCloudyNight : WeatherSceneKind::PartlyCloudy;
    if (normalized.indexOf("cloud") >= 0 || normalized.indexOf("overcast") >= 0 || normalized.indexOf("mist") >= 0)
        return night ? WeatherSceneKind::CloudyNight : WeatherSceneKind::Cloudy;
    return night ? WeatherSceneKind::SunnyNight : WeatherSceneKind::Sunny;
}

uint16_t weatherSceneAccentColor(WeatherSceneKind kind)
{
    switch (kind)
    {
    case WeatherSceneKind::Sunny:
        return dma_display->color565(235, 185, 60);
    case WeatherSceneKind::SunnyNight:
        return dma_display->color565(180, 190, 240);
    case WeatherSceneKind::PartlyCloudy:
        return dma_display->color565(210, 205, 150);
    case WeatherSceneKind::PartlyCloudyNight:
        return dma_display->color565(170, 185, 220);
    case WeatherSceneKind::Cloudy:
        return dma_display->color565(190, 200, 220);
    case WeatherSceneKind::CloudyNight:
        return dma_display->color565(150, 170, 210);
    case WeatherSceneKind::Overcast:
        return dma_display->color565(176, 188, 202);
    case WeatherSceneKind::OvercastNight:
        return dma_display->color565(126, 144, 172);
    case WeatherSceneKind::Fog:
        return dma_display->color565(205, 210, 220);
    case WeatherSceneKind::FogNight:
        return dma_display->color565(145, 165, 190);
    case WeatherSceneKind::Windy:
        return dma_display->color565(180, 210, 230);
    case WeatherSceneKind::WindyNight:
        return dma_display->color565(135, 175, 215);
    case WeatherSceneKind::Rain:
        return dma_display->color565(120, 170, 210);
    case WeatherSceneKind::RainNight:
        return dma_display->color565(100, 150, 200);
    case WeatherSceneKind::Thunderstorm:
        return dma_display->color565(220, 180, 50);
    case WeatherSceneKind::ThunderstormNight:
        return dma_display->color565(200, 160, 70);
    case WeatherSceneKind::Snow:
        return dma_display->color565(210, 220, 235);
    case WeatherSceneKind::SnowNight:
        return dma_display->color565(180, 190, 220);
    case WeatherSceneKind::ClearNight:
        return dma_display->color565(160, 180, 220);
    default:
        return dma_display->color565(200, 200, 210);
    }
}

bool weatherSceneIsNight(WeatherSceneKind kind)
{
    return kind == WeatherSceneKind::SunnyNight ||
           kind == WeatherSceneKind::PartlyCloudyNight ||
           kind == WeatherSceneKind::CloudyNight ||
           kind == WeatherSceneKind::OvercastNight ||
           kind == WeatherSceneKind::FogNight ||
           kind == WeatherSceneKind::WindyNight ||
           kind == WeatherSceneKind::RainNight ||
           kind == WeatherSceneKind::ThunderstormNight ||
           kind == WeatherSceneKind::SnowNight ||
           kind == WeatherSceneKind::ClearNight;
}

uint16_t weatherSceneTempBgColor(WeatherSceneKind kind)
{
    // Approximate top-right sky color for each scene (where temp is rendered).
    switch (kind)
    {
    case WeatherSceneKind::Sunny:
        return dma_display->color565(45, 170, 255);
    case WeatherSceneKind::PartlyCloudy:
        return dma_display->color565(95, 175, 235);
    case WeatherSceneKind::Cloudy:
        return dma_display->color565(120, 165, 210);
    case WeatherSceneKind::Overcast:
        return dma_display->color565(128, 145, 165);
    case WeatherSceneKind::Fog:
        return dma_display->color565(160, 185, 205);
    case WeatherSceneKind::Windy:
        return dma_display->color565(85, 165, 225);
    case WeatherSceneKind::Rain:
        return dma_display->color565(55, 110, 175);
    case WeatherSceneKind::Thunderstorm:
        return dma_display->color565(38, 74, 128);
    case WeatherSceneKind::Snow:
        return dma_display->color565(175, 210, 240);
    case WeatherSceneKind::SunnyNight:
    case WeatherSceneKind::PartlyCloudyNight:
    case WeatherSceneKind::CloudyNight:
    case WeatherSceneKind::OvercastNight:
    case WeatherSceneKind::FogNight:
    case WeatherSceneKind::WindyNight:
    case WeatherSceneKind::RainNight:
    case WeatherSceneKind::ThunderstormNight:
    case WeatherSceneKind::SnowNight:
    case WeatherSceneKind::ClearNight:
        return dma_display->color565(8, 16, 52);
    default:
        return dma_display->color565(36, 78, 134);
    }
}

static int colorLuma565(uint16_t c)
{
    int r = ((c >> 11) & 0x1F) * 255 / 31;
    int g = ((c >> 5) & 0x3F) * 255 / 63;
    int b = (c & 0x1F) * 255 / 31;
    return (r * 30 + g * 59 + b * 11) / 100;
}

uint16_t weatherSceneAdaptiveTempTextColor(WeatherSceneKind kind, uint16_t accent, bool secondary)
{
    if (theme == 1)
    {
        return secondary ? dma_display->color565(135, 155, 205)
                         : dma_display->color565(200, 215, 245);
    }

    if (weatherSceneIsNight(kind))
    {
        uint16_t base = secondary ? dma_display->color565(145, 205, 255)
                                  : dma_display->color565(235, 246, 255);
        return lerpColor565(base, accent, secondary ? 0.12f : 0.08f);
    }

    uint16_t base = secondary ? dma_display->color565(255, 188, 112)
                              : dma_display->color565(255, 236, 172);
    return lerpColor565(base, accent, secondary ? 0.10f : 0.06f);
}

static void drawSceneReadabilityOverlay(WeatherSceneKind kind)
{
    // Keep overlays lightweight so the scene top is never obscured.
    // Only draw the scene/status divider line.
    const bool night = weatherSceneIsNight(kind);
    const uint16_t accent = weatherSceneAccentColor(kind);
    const uint16_t divider = scaleColor565(accent, night ? 0.55f : 0.72f);

    // Thin divider between scene and status/marquee zone.
    dma_display->drawFastHLine(0, SCENE_H - 1, SCENE_W, divider);
}

static void applyNightThemeSceneDimmer()
{
    if (theme != 1)
        return;

    // Static dither dimming so the same weather scene is preserved in night theme.
    for (int y = 0; y < SCENE_H; ++y)
    {
        int start = y % 3;
        for (int x = start; x < SCENE_W; x += 3)
            dma_display->drawPixel(x, y, myBLACK);
    }
}

String formatConditionLabel(const String &condition)
{
    String label = condition;
    label.trim();
    if (label.length() == 0)
        return String("No Data");

    label.replace('_', ' ');
    label.replace('-', ' ');
    label.toLowerCase();

    bool capitalizeNext = true;
    for (int i = 0; i < label.length(); ++i)
    {
        char c = label.charAt(i);
        unsigned char uc = static_cast<unsigned char>(c);
        if (isalpha(uc))
        {
            if (capitalizeNext)
                label.setCharAt(i, static_cast<char>(toupper(uc)));
            capitalizeNext = false;
        }
        else if (isdigit(uc))
        {
            capitalizeNext = false;
        }
        else
        {
            capitalizeNext = true;
        }
    }
    return label;
}

void drawWeatherConditionScene(WeatherSceneKind kind)
{
    for (const auto &renderer : WEATHER_SCENE_RENDERERS)
    {
        if (renderer.kind == kind)
        {
            renderer.drawFn();
            applyNightThemeSceneDimmer();
            drawSceneReadabilityOverlay(kind);
            return;
        }
    }
    drawWeatherSceneDefault();
    applyNightThemeSceneDimmer();
    drawSceneReadabilityOverlay(kind);
}

void drawWeatherConditionScene(const String &condition)
{
    WeatherSceneKind kind = resolveWeatherSceneKind(condition);
    drawWeatherConditionScene(kind);
}


