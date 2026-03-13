#include "display_astronomy.h"

#include <math.h>

#include "astronomy.h"
#include "display.h"
#include "display_sky_facts.h"
#include "display_seven_segment.h"
#include "settings.h"
#include "ui_theme.h"
#include "units.h"

namespace
{
constexpr unsigned long kAstronomyPageAutoMs = 4200UL;
constexpr uint8_t kAstronomyBasePageCount = 5;

uint8_t s_astronomyPageIndex = 0;
unsigned long s_astronomyLastSwitchMs = 0;
int s_astronomyLastTheme = -1;
int s_astronomyLastDateKey = -1;
bool s_astronomyRotationPaused = false;
int s_astronomyLastMinute = -1;

uint8_t astronomyPageCount()
{
    const size_t skyFacts = wxv::astronomy::skyFactCount();
    const size_t total = static_cast<size_t>(kAstronomyBasePageCount) + skyFacts;
    return static_cast<uint8_t>(total > 255 ? 255 : total);
}

uint16_t titleBg()
{
    return ui_theme::isNightTheme() ? ui_theme::monoHeaderBg() : ui_theme::infoScreenHeaderBg();
}

uint16_t titleFg()
{
    return ui_theme::isNightTheme() ? ui_theme::monoHeaderFg() : ui_theme::infoScreenHeaderFg();
}

uint16_t bodyLabel()
{
    return ui_theme::isNightTheme() ? ui_theme::monoHeaderFg() : ui_theme::infoLabelDay();
}

uint16_t bodyValue()
{
    return ui_theme::isNightTheme() ? ui_theme::monoBodyText() : ui_theme::infoValueDay();
}

uint16_t bodyAccent()
{
    return ui_theme::applyGraphicColor(dma_display->color565(255, 208, 96));
}

uint16_t moonAccent()
{
    return ui_theme::applyGraphicColor(dma_display->color565(214, 226, 250));
}

uint16_t horizonColor()
{
    return ui_theme::applyGraphicColor(dma_display->color565(96, 136, 180));
}

uint16_t arcColor()
{
    return ui_theme::applyGraphicColor(dma_display->color565(88, 154, 218));
}

uint16_t passedArcColor()
{
    return ui_theme::applyGraphicColor(dma_display->color565(255, 190, 96));
}

uint16_t passedSkyFillColor()
{
    return ui_theme::applyGraphicColor(dma_display->color565(72, 112, 168));
}

uint16_t timeColor()
{
    return ui_theme::applyGraphicColor(dma_display->color565(236, 245, 255));
}

void drawHeader(const char *title)
{
    dma_display->fillScreen(0);
    dma_display->fillRect(0, 0, PANEL_RES_X, 8, titleBg());
    dma_display->drawFastHLine(0, 7, PANEL_RES_X, ui_theme::isNightTheme() ? ui_theme::monoUnderline() : ui_theme::infoUnderlineDay());
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    dma_display->setTextColor(titleFg());
    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    int x = (PANEL_RES_X - static_cast<int>(w)) / 2;
    if (x < 0)
        x = 0;
    dma_display->setCursor(x, 0);
    dma_display->print(title);
}

String formatAngle(float degrees)
{
    if (!isfinite(degrees))
        return String("--");
    int rounded = static_cast<int>(roundf(degrees));
    if (rounded < 0)
        rounded += 360;
    if (rounded >= 360)
        rounded -= 360;
    return String(rounded) + "\xB0";
}

String formatElevation(float degrees)
{
    if (!isfinite(degrees))
        return String("--");
    int rounded = static_cast<int>(roundf(degrees));
    char buf[12];
    snprintf(buf, sizeof(buf), "%+d\xB0", rounded);
    return String(buf);
}

String formatClockMinutes(int minutes)
{
    if (minutes < 0)
        return String("--");

    int hour = (minutes / 60) % 24;
    int minute = minutes % 60;
    char buf[12];
    if (units.clock24h)
    {
        snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    }
    else
    {
        bool isPm = hour >= 12;
        int displayHour = hour % 12;
        if (displayHour == 0)
            displayHour = 12;
        snprintf(buf, sizeof(buf), "%d:%02d%c", displayHour, minute, isPm ? 'P' : 'A');
    }
    return String(buf);
}

String compassLabel(float degrees)
{
    if (!isfinite(degrees))
        return String("--");
    static const char *dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int idx = static_cast<int>(floorf(fmodf(degrees + 22.5f + 360.0f, 360.0f) / 45.0f)) & 7;
    return String(dirs[idx]);
}

void drawLabeledValueRow(int y, const char *label, const String &value, uint16_t labelColor, uint16_t valueColor)
{
    dma_display->setTextColor(labelColor);
    dma_display->setCursor(2, y);
    dma_display->print(label);
    dma_display->setTextColor(valueColor);
    dma_display->setCursor(26, y);
    dma_display->print(value);
}

void splitPhaseLabel(const char *label, String &line1, String &line2)
{
    String text = label ? String(label) : String("N/A");
    text.trim();
    int split = text.indexOf(' ');
    if (split < 0)
    {
        line1 = text;
        line2 = "";
        return;
    }
    line1 = text.substring(0, split);
    line2 = text.substring(split + 1);
    line2.trim();
}

const char *shortMoonPhaseLabelLocal(wxv::astronomy::MoonPhase phase)
{
    switch (phase)
    {
    case wxv::astronomy::MoonPhase::NewMoon:
        return "New";
    case wxv::astronomy::MoonPhase::WaxingCrescent:
        return "Wax Cres";
    case wxv::astronomy::MoonPhase::FirstQuarter:
        return "1st Qtr";
    case wxv::astronomy::MoonPhase::WaxingGibbous:
        return "Wax Gib";
    case wxv::astronomy::MoonPhase::FullMoon:
        return "Full";
    case wxv::astronomy::MoonPhase::WaningGibbous:
        return "Wan Gib";
    case wxv::astronomy::MoonPhase::LastQuarter:
        return "Last Qtr";
    default:
        return "Wan Cres";
    }
}

String moonCountdownText(float phaseFraction)
{
    constexpr float kMoonSynodicDays = 29.530588853f;
    const float phase = phaseFraction - floorf(phaseFraction);
    const int daysToFull = static_cast<int>(roundf((((phase <= 0.5f) ? (0.5f - phase) : (1.5f - phase)) * kMoonSynodicDays)));
    const int daysToNew = static_cast<int>(roundf((((phase <= 0.01f) ? 0.0f : (1.0f - phase)) * kMoonSynodicDays)));

    char buf[16];
    if (daysToFull <= daysToNew)
        snprintf(buf, sizeof(buf), "Full %dd", daysToFull);
    else
        snprintf(buf, sizeof(buf), "New %dd", daysToNew);
    return String(buf);
}

void drawMoonPhaseIcon(int cx, int cy, int radius, float phaseFraction)
{
    const uint16_t darkColor = ui_theme::applyGraphicColor(dma_display->color565(18, 26, 42));
    const uint16_t earthshineColor = ui_theme::applyGraphicColor(dma_display->color565(44, 62, 96));
    const uint16_t litColor = ui_theme::applyGraphicColor(dma_display->color565(232, 234, 228));
    const uint16_t litSoftColor = ui_theme::applyGraphicColor(dma_display->color565(186, 192, 198));
    const uint16_t craterColor = ui_theme::applyGraphicColor(dma_display->color565(150, 152, 150));
    const uint16_t craterSoftColor = ui_theme::applyGraphicColor(dma_display->color565(110, 118, 132));
    const uint16_t rimColor = ui_theme::applyGraphicColor(dma_display->color565(248, 240, 214));

    const float wrapped = phaseFraction - floorf(phaseFraction);
    const float phaseAngle = wrapped * 2.0f * static_cast<float>(M_PI);
    const float k = cosf(phaseAngle);
    const bool waxing = wrapped <= 0.5f;

    for (int dy = -radius; dy <= radius; ++dy)
    {
        for (int dx = -radius; dx <= radius; ++dx)
        {
            const float xn = static_cast<float>(dx) / static_cast<float>(radius);
            const float yn = static_cast<float>(dy) / static_cast<float>(radius);
            const float rr = xn * xn + yn * yn;
            if (rr > 1.0f)
                continue;

            const float limb = sqrtf(fmaxf(0.0f, 1.0f - yn * yn));
            const float earthViewX = xn;
            const float terminator = waxing ? (earthViewX - k * limb) : (-earthViewX - k * limb);
            const bool lit = terminator >= 0.0f;
            uint16_t color = earthshineColor;
            if (lit)
            {
                const float highlight = fminf(1.0f, fmaxf(0.0f, (0.90f - rr) * 1.25f + 0.14f));
                color = highlight > 0.56f ? litColor : litSoftColor;
            }
            else
            {
                const float edgeGlow = fminf(1.0f, fmaxf(0.0f, (terminator + 0.18f) / 0.18f));
                color = edgeGlow > 0.55f ? earthshineColor : darkColor;
                if (rr < 0.78f && wrapped > 0.04f && wrapped < 0.96f)
                    color = earthshineColor;
            }

            dma_display->drawPixel(cx + dx, cy + dy, color);
        }
    }

    dma_display->drawCircle(cx, cy, radius, rimColor);

    // A few crater marks give the larger icon more structure without clutter.
    auto isLitPoint = [&](int px, int py) -> bool
    {
        const float xn = static_cast<float>(px - cx) / static_cast<float>(radius);
        const float yn = static_cast<float>(py - cy) / static_cast<float>(radius);
        if (xn * xn + yn * yn > 1.0f)
            return false;
        const float limb = sqrtf(fmaxf(0.0f, 1.0f - yn * yn));
        const float earthViewX = xn;
        const float terminator = waxing ? (earthViewX - k * limb) : (-earthViewX - k * limb);
        return terminator >= 0.0f;
    };
    auto isSoftLitPoint = [&](int px, int py) -> bool
    {
        const float xn = static_cast<float>(px - cx) / static_cast<float>(radius);
        const float yn = static_cast<float>(py - cy) / static_cast<float>(radius);
        const float rr = xn * xn + yn * yn;
        if (rr > 1.0f)
            return false;
        const float brightness = fminf(1.0f, fmaxf(0.0f, (0.90f - rr) * 1.25f + 0.14f));
        return brightness <= 0.56f;
    };

    const int crater1X = cx - radius / 3;
    const int crater1Y = cy - radius / 4;
    const int crater2X = cx;
    const int crater2Y = cy + radius / 5;
    const int crater3X = cx + radius / 4;
    const int crater3Y = cy - 1;
    if (isLitPoint(crater1X, crater1Y) && isSoftLitPoint(crater1X, crater1Y))
        dma_display->drawPixel(crater1X, crater1Y, craterColor);
    if (isLitPoint(crater1X + 1, crater1Y) && isSoftLitPoint(crater1X + 1, crater1Y))
        dma_display->drawPixel(crater1X + 1, crater1Y, craterSoftColor);
    if (isLitPoint(crater1X, crater1Y + 1) && isSoftLitPoint(crater1X, crater1Y + 1))
        dma_display->drawPixel(crater1X, crater1Y + 1, craterSoftColor);
    if (isLitPoint(crater2X, crater2Y) && isSoftLitPoint(crater2X, crater2Y))
        dma_display->drawPixel(crater2X, crater2Y, craterColor);
    if (isLitPoint(crater2X + 1, crater2Y) && isSoftLitPoint(crater2X + 1, crater2Y))
        dma_display->drawPixel(crater2X + 1, crater2Y, craterSoftColor);
    if (isLitPoint(crater3X, crater3Y) && isSoftLitPoint(crater3X, crater3Y))
        dma_display->drawPixel(crater3X, crater3Y, craterSoftColor);

    const uint16_t spec1 = ui_theme::applyGraphicColor(dma_display->color565(255, 250, 230));
    const uint16_t spec2 = ui_theme::applyGraphicColor(dma_display->color565(205, 210, 216));
    if (isLitPoint(cx - radius / 2, cy - radius / 3))
        dma_display->drawPixel(cx - radius / 2, cy - radius / 3, spec1);
    if (isLitPoint(cx - radius / 4, cy + radius / 5))
        dma_display->drawPixel(cx - radius / 4, cy + radius / 5, spec2);
}

float clamp01(float value)
{
    if (value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

int arcYForT(int horizonY, int peakY, float t)
{
    const float arc = sinf(clamp01(t) * static_cast<float>(M_PI));
    return horizonY - static_cast<int>(roundf(arc * static_cast<float>(horizonY - peakY)));
}

float arcProgressFromAzimuth(float azimuthDeg)
{
    if (!isfinite(azimuthDeg))
        return 0.5f;
    return clamp01((azimuthDeg - 90.0f) / 180.0f);
}

float sunArcProgress(const wxv::astronomy::AstronomyData &data)
{
    if (data.hasSunTimes && data.sunriseMinutes >= 0 && data.sunsetMinutes > data.sunriseMinutes && data.localMinutes >= 0)
        return clamp01(static_cast<float>(data.localMinutes - data.sunriseMinutes) /
                       static_cast<float>(data.sunsetMinutes - data.sunriseMinutes));
    return arcProgressFromAzimuth(data.sunAzimuthDeg);
}

float moonArcProgress(const wxv::astronomy::AstronomyData &data)
{
    if (data.hasMoonTimes && data.moonriseMinutes >= 0 && data.moonsetMinutes >= 0 && data.localMinutes >= 0)
    {
        if (data.moonriseMinutes <= data.moonsetMinutes)
        {
            if (data.localMinutes >= data.moonriseMinutes && data.localMinutes <= data.moonsetMinutes)
            {
                return clamp01(static_cast<float>(data.localMinutes - data.moonriseMinutes) /
                               static_cast<float>(data.moonsetMinutes - data.moonriseMinutes));
            }
        }
        else
        {
            const int duration = (24 * 60 - data.moonriseMinutes) + data.moonsetMinutes;
            if (duration > 0 && (data.localMinutes >= data.moonriseMinutes || data.localMinutes <= data.moonsetMinutes))
            {
                const int elapsed = (data.localMinutes >= data.moonriseMinutes)
                                        ? (data.localMinutes - data.moonriseMinutes)
                                        : ((24 * 60 - data.moonriseMinutes) + data.localMinutes);
                return clamp01(static_cast<float>(elapsed) / static_cast<float>(duration));
            }
        }
    }
    return arcProgressFromAzimuth(data.moonAzimuthDeg);
}

float sunPassedArcProgress(const wxv::astronomy::AstronomyData &data)
{
    if (data.hasSunTimes && data.sunriseMinutes >= 0 && data.sunsetMinutes > data.sunriseMinutes && data.localMinutes >= 0)
    {
        if (data.localMinutes <= data.sunriseMinutes)
            return 0.0f;
        if (data.localMinutes >= data.sunsetMinutes)
            return 1.0f;
    }
    if (data.hasSunAltitude)
        return data.sunAltitudeDeg > 0.0f ? sunArcProgress(data) : 0.0f;
    return 0.0f;
}

bool sunIsVisible(const wxv::astronomy::AstronomyData &data)
{
    if (data.hasSunAltitude)
        return data.sunAltitudeDeg > 0.0f;
    if (data.hasSunTimes && data.localMinutes >= 0)
        return data.localMinutes >= data.sunriseMinutes && data.localMinutes <= data.sunsetMinutes;
    return false;
}

bool moonIsVisible(const wxv::astronomy::AstronomyData &data)
{
    return data.hasMoonAltitude && data.moonAltitudeDeg > 0.0f;
}

void drawTinySun(int cx, int cy)
{
    const uint16_t fill = bodyAccent();
    dma_display->fillRect(cx - 1, cy - 1, 3, 3, fill);
}

void drawTinyMoon(int cx, int cy)
{
    const uint16_t moon = moonAccent();
    const uint16_t cut = ui_theme::applyGraphicColor(dma_display->color565(4, 10, 40));
    dma_display->drawPixel(cx - 1, cy - 1, moon);
    dma_display->drawPixel(cx, cy - 1, moon);
    dma_display->drawPixel(cx - 1, cy, moon);
    dma_display->drawPixel(cx, cy, moon);
    dma_display->drawPixel(cx - 1, cy + 1, moon);
    dma_display->drawPixel(cx, cy + 1, moon);
    dma_display->drawPixel(cx + 1, cy - 1, cut);
    dma_display->drawPixel(cx + 1, cy, cut);
    dma_display->drawPixel(cx + 1, cy + 1, cut);
}

void drawPlaceholderTime(int x, int y, uint16_t color)
{
    wxv::seg7::Metrics metrics;
    metrics.digitWidth = 3;
    metrics.digitHeight = 5;
    metrics.colonWidth = 1;
    metrics.spacing = 1;

    for (int offset : {0, 4, 9, 13})
        dma_display->drawFastHLine(x + offset, y + 2, metrics.digitWidth, color);
    wxv::seg7::drawColon(x + 8, y, color, 1, metrics);
}

void drawSceneStyleTime(int x, int y, int minutes, uint16_t color)
{
    if (minutes < 0)
    {
        drawPlaceholderTime(x, y, color);
        return;
    }

    wxv::seg7::Metrics metrics;
    metrics.digitWidth = 3;
    metrics.digitHeight = 5;
    metrics.colonWidth = 1;
    metrics.spacing = 1;
    wxv::seg7::drawTime(x, y, minutes / 60, minutes % 60, units.clock24h, color, 1, &metrics, true);
}

int sceneStyleTimeWidth(int minutes)
{
    wxv::seg7::Metrics metrics;
    metrics.digitWidth = 3;
    metrics.digitHeight = 5;
    metrics.colonWidth = 1;
    metrics.spacing = 1;
    if (minutes < 0)
        return 16;
    return wxv::seg7::measureTime(minutes / 60, minutes % 60, units.clock24h, 1, &metrics);
}

int sceneStyleElevationWidth(float degrees)
{
    if (!isfinite(degrees))
        return 8;

    const int value = static_cast<int>(roundf(fabsf(degrees)));
    const int digits = (value >= 100) ? 3 : 2;
    return 3 + digits * 4 + 2;
}

void drawSceneStyleElevation(int x, int y, float degrees, uint16_t color, bool rightAlign)
{
    if (!isfinite(degrees))
        return;

    const int width = sceneStyleElevationWidth(degrees);
    int drawX = rightAlign ? (x - width) : x;

    const int rounded = static_cast<int>(roundf(degrees));
    const int absValue = abs(rounded);
    const bool negative = rounded < 0;

    wxv::seg7::Metrics metrics;
    metrics.digitWidth = 3;
    metrics.digitHeight = 5;
    metrics.colonWidth = 1;
    metrics.spacing = 1;

    const int signY = y + 2;
    if (negative)
        dma_display->drawFastHLine(drawX, signY, 3, color);
    else
    {
        dma_display->drawFastHLine(drawX, signY, 3, color);
        dma_display->drawFastVLine(drawX + 1, y + 1, 3, color);
    }

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", absValue);
    int cursorX = drawX + 4;
    for (int i = 0; buf[i] != '\0'; ++i)
    {
        wxv::seg7::drawDigit(cursorX, y, buf[i], color, 1, metrics);
        cursorX += metrics.digitWidth + metrics.spacing;
    }

    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    dma_display->setTextColor(color);
    dma_display->setCursor(cursorX - 1, y - 1);
    dma_display->print("\xB0");
}

void drawAstronomyGraphicPage(const wxv::astronomy::AstronomyData &data)
{
    drawHeader("Sky");

    constexpr int horizonY = 24;
    constexpr int peakY = 10;
    constexpr int leftX = 4;
    constexpr int rightX = PANEL_RES_X - 5;
    constexpr int timeY = 26;
    constexpr int elevationY = 9;

    drawSceneStyleElevation(1, elevationY, data.sunAltitudeDeg, bodyAccent(), false);
    drawSceneStyleElevation(PANEL_RES_X - 1, elevationY, data.moonAltitudeDeg, moonAccent(), true);

    const float passedT = sunPassedArcProgress(data);
    const int passedX = leftX + static_cast<int>(roundf(passedT * static_cast<float>(rightX - leftX)));
    if (passedT > 0.0f)
    {
        for (int x = leftX; x <= passedX; ++x)
        {
            const float t = static_cast<float>(x - leftX) / static_cast<float>(rightX - leftX);
            const int y = arcYForT(horizonY, peakY, t);
            if (horizonY - y > 1)
                dma_display->drawFastVLine(x, y + 1, horizonY - y - 1, passedSkyFillColor());
        }
    }

    dma_display->drawFastHLine(0, horizonY, PANEL_RES_X, horizonColor());
    int lastX = leftX;
    int lastY = arcYForT(horizonY, peakY, 0.0f);
    for (int x = leftX + 1; x <= rightX; ++x)
    {
        const float t = static_cast<float>(x - leftX) / static_cast<float>(rightX - leftX);
        const int y = arcYForT(horizonY, peakY, t);
        const float segmentMid = (t + static_cast<float>(lastX - leftX) / static_cast<float>(rightX - leftX)) * 0.5f;
        dma_display->drawLine(lastX, lastY, x, y, segmentMid <= passedT ? passedArcColor() : arcColor());
        lastX = x;
        lastY = y;
    }

    if (sunIsVisible(data))
    {
        const float t = sunArcProgress(data);
        const int x = leftX + static_cast<int>(roundf(t * static_cast<float>(rightX - leftX)));
        const int y = arcYForT(horizonY, peakY, t);
        drawTinySun(x, y);
    }

    if (moonIsVisible(data))
    {
        const float t = moonArcProgress(data);
        const int x = leftX + static_cast<int>(roundf(t * static_cast<float>(rightX - leftX)));
        const int y = arcYForT(horizonY, peakY, t);
        drawTinyMoon(x, y);
    }

    drawSceneStyleTime(2, timeY, data.sunriseMinutes, timeColor());
    drawSceneStyleTime(PANEL_RES_X - sceneStyleTimeWidth(data.sunsetMinutes) - 2, timeY, data.sunsetMinutes, timeColor());
}

void drawAzimuthPage(const wxv::astronomy::AstronomyData &data)
{
    drawHeader("Azimuth");
    if (!data.hasLocation)
    {
        dma_display->setTextColor(bodyLabel());
        dma_display->setCursor(5, 12);
        dma_display->print("No location");
        dma_display->setCursor(8, 21);
        dma_display->print("set");
        dma_display->setTextColor(bodyValue());
        dma_display->setCursor(26, 21);
        dma_display->print("lat/lon");
        return;
    }

    const String sunAngle = formatAngle(data.sunAzimuthDeg);
    const String sunDir = compassLabel(data.sunAzimuthDeg);
    const String moonAngle = formatAngle(data.moonAzimuthDeg);
    const String moonDir = compassLabel(data.moonAzimuthDeg);
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);

    int16_t x1, y1;
    uint16_t sunAngleW, sunAngleH;
    uint16_t moonAngleW, moonAngleH;
    dma_display->getTextBounds(sunAngle.c_str(), 0, 0, &x1, &y1, &sunAngleW, &sunAngleH);
    dma_display->getTextBounds(moonAngle.c_str(), 0, 0, &x1, &y1, &moonAngleW, &moonAngleH);
    constexpr int dirX = PANEL_RES_X - 13;
    const int sunAngleX = dirX - 4 - static_cast<int>(sunAngleW);
    const int moonAngleX = dirX - 4 - static_cast<int>(moonAngleW);

    dma_display->setTextColor(bodyLabel());
    dma_display->setCursor(2, 11);
    dma_display->print("Sun");
    dma_display->setTextColor(bodyAccent());
    dma_display->setCursor(sunAngleX, 11);
    dma_display->print(sunAngle);
    dma_display->setCursor(dirX, 11);
    dma_display->print(sunDir);

    dma_display->setTextColor(bodyLabel());
    dma_display->setCursor(2, 21);
    dma_display->print("Moon");
    dma_display->setTextColor(moonAccent());
    dma_display->setCursor(moonAngleX, 21);
    dma_display->print(moonAngle);
    dma_display->setCursor(dirX, 21);
    dma_display->print(moonDir);
}

void drawSunTimesPage(const wxv::astronomy::AstronomyData &data)
{
    drawHeader("Sun Times");
    if (!data.hasSunTimes)
    {
        dma_display->setTextColor(bodyLabel());
        dma_display->setCursor(4, 16);
        dma_display->print("No sun data");
        return;
    }

    drawLabeledValueRow(11, "Rise", formatClockMinutes(data.sunriseMinutes), bodyLabel(), bodyAccent());
    drawLabeledValueRow(21, "Set", formatClockMinutes(data.sunsetMinutes), bodyLabel(), bodyAccent());
}

void drawElevationPage(const wxv::astronomy::AstronomyData &data)
{
    drawHeader("Elevation");
    if (!data.hasLocation)
    {
        dma_display->setTextColor(bodyLabel());
        dma_display->setCursor(5, 12);
        dma_display->print("No location");
        dma_display->setCursor(8, 21);
        dma_display->print("set");
        dma_display->setTextColor(bodyValue());
        dma_display->setCursor(26, 21);
        dma_display->print("lat/lon");
        return;
    }

    drawLabeledValueRow(11, "Sun", formatElevation(data.sunAltitudeDeg), bodyLabel(), bodyAccent());
    drawLabeledValueRow(21, "Moon", formatElevation(data.moonAltitudeDeg), bodyLabel(), moonAccent());
}

void drawMoonPage(const wxv::astronomy::AstronomyData &data)
{
    drawHeader("Moon");
    drawMoonPhaseIcon(10, 19, 9, data.moonPhaseFraction);

    dma_display->setTextColor(bodyValue());
    dma_display->setCursor(22, 8);
    dma_display->print(shortMoonPhaseLabelLocal(data.moonPhase));

    dma_display->setCursor(22, 16);
    dma_display->print(moonCountdownText(data.moonPhaseFraction));

    dma_display->setTextColor(bodyLabel());
    dma_display->setCursor(22, 24);
    dma_display->print(String("Lit ") + String(data.moonIlluminationPct) + "%");
}
} // namespace

void drawAstronomyScreen()
{
    wxv::astronomy::updateSkyFacts();
    const wxv::astronomy::AstronomyData &data = wxv::astronomy::astronomyData();
    s_astronomyLastTheme = theme;
    s_astronomyLastDateKey = data.localDateKey;
    s_astronomyLastMinute = data.localMinutes;
    const uint8_t pageCount = astronomyPageCount();
    if (pageCount == 0)
        return;
    if (s_astronomyPageIndex >= pageCount)
        s_astronomyPageIndex = 0;

    if (s_astronomyPageIndex >= kAstronomyBasePageCount)
    {
        const size_t skyFactIndex = static_cast<size_t>(s_astronomyPageIndex - kAstronomyBasePageCount);
        drawSkyFactSubpage(wxv::astronomy::skyFactPage(skyFactIndex));
        return;
    }

    switch (s_astronomyPageIndex)
    {
    case 0:
        drawAstronomyGraphicPage(data);
        break;
    case 1:
        drawAzimuthPage(data);
        break;
    case 2:
        drawSunTimesPage(data);
        break;
    case 3:
        drawElevationPage(data);
        break;
    default:
        drawMoonPage(data);
        break;
    }
}

void tickAstronomyScreen()
{
    wxv::astronomy::updateSkyFacts();
    const wxv::astronomy::AstronomyData &data = wxv::astronomy::astronomyData();
    const unsigned long nowMs = millis();

    if (theme != s_astronomyLastTheme ||
        data.localDateKey != s_astronomyLastDateKey ||
        data.localMinutes != s_astronomyLastMinute)
    {
        drawAstronomyScreen();
        return;
    }

    if (s_astronomyLastSwitchMs == 0)
        s_astronomyLastSwitchMs = nowMs;

    if (s_astronomyRotationPaused)
        return;

    const uint8_t pageCount = astronomyPageCount();
    if (pageCount == 0)
        return;

    if (nowMs - s_astronomyLastSwitchMs >= kAstronomyPageAutoMs)
    {
        s_astronomyPageIndex = static_cast<uint8_t>((s_astronomyPageIndex + 1u) % pageCount);
        s_astronomyLastSwitchMs = nowMs;
        drawAstronomyScreen();
    }
}

void handleAstronomyDownPress()
{
    const uint8_t pageCount = astronomyPageCount();
    if (pageCount == 0)
        return;
    s_astronomyPageIndex = static_cast<uint8_t>((s_astronomyPageIndex + 1u) % pageCount);
    s_astronomyLastSwitchMs = millis();
    drawAstronomyScreen();
}

void handleAstronomyUpPress()
{
    const uint8_t pageCount = astronomyPageCount();
    if (pageCount == 0)
        return;
    int next = static_cast<int>(s_astronomyPageIndex) - 1;
    if (next < 0)
        next = pageCount - 1;
    s_astronomyPageIndex = static_cast<uint8_t>(next);
    s_astronomyLastSwitchMs = millis();
    drawAstronomyScreen();
}

void handleAstronomySelectPress()
{
    s_astronomyRotationPaused = !s_astronomyRotationPaused;
    s_astronomyLastSwitchMs = millis();
    drawAstronomyScreen();
}

void resetAstronomyScreenState()
{
    s_astronomyPageIndex = 0;
    s_astronomyLastSwitchMs = millis();
    s_astronomyLastTheme = -1;
    s_astronomyLastDateKey = -1;
    s_astronomyLastMinute = -1;
    s_astronomyRotationPaused = false;
}
