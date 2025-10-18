#include <math.h>

#include "env_quality.h"
#include "display.h"
#include "InfoScreen.h"
#include "settings.h"
#include "sensors.h"
#include "units.h"
#include "utils.h"

extern InfoScreen envQualityScreen;
extern ScreenMode currentScreen;
extern int theme;
extern int scrollSpeed;

static EnvBand s_lineBands[3];
static int s_lineBandCount = 0;
static EnvBand s_overallBand = EnvBand::Unknown;
static String s_detailsDisplayText;
static String s_detailsPendingText;
static int s_detailsDisplayWidth = 0;
static int s_detailsScrollOffset = 0;
static unsigned long s_detailsLastScroll = 0;
static bool s_iconBitmapValid = false;
static EnvBand s_iconBitmapBand = EnvBand::Unknown;
static uint16_t s_iconBitmapBackground = 0;
static uint16_t s_iconBitmap[16 * 16];

static int scoreForBand(EnvBand band)
{
    switch (band)
    {
    case EnvBand::Good:
        return 3;
    case EnvBand::Moderate:
        return 2;
    case EnvBand::Poor:
        return 1;
    case EnvBand::Critical:
        return 0;
    default:
        return -1;
    }
}

static EnvBand bandFromIndex(float idx)
{
    if (idx < 0.0f)
        return EnvBand::Unknown;
    if (idx >= 75.0f)
        return EnvBand::Good;
    if (idx >= 50.0f)
        return EnvBand::Moderate;
    if (idx >= 25.0f)
        return EnvBand::Poor;
    return EnvBand::Critical;
}

static EnvBand bandFromCo2(float co2)
{
    if (isnan(co2) || co2 <= 0.0f)
        return EnvBand::Unknown;
    if (co2 <= 800.0f)
        return EnvBand::Good;
    if (co2 <= 1200.0f)
        return EnvBand::Moderate;
    if (co2 <= 2000.0f)
        return EnvBand::Poor;
    return EnvBand::Critical;
}

static EnvBand bandFromTemp(float tempC)
{
    if (isnan(tempC))
        return EnvBand::Unknown;
    if (tempC >= 20.0f && tempC <= 24.0f)
        return EnvBand::Good;
    if ((tempC >= 18.0f && tempC < 20.0f) || (tempC > 24.0f && tempC <= 26.0f))
        return EnvBand::Moderate;
    if ((tempC >= 16.0f && tempC < 18.0f) || (tempC > 26.0f && tempC <= 28.0f))
        return EnvBand::Poor;
    return EnvBand::Critical;
}

static EnvBand bandFromHumidity(float humidity)
{
    if (isnan(humidity))
        return EnvBand::Unknown;
    if (humidity >= 35.0f && humidity <= 55.0f)
        return EnvBand::Good;
    if ((humidity >= 30.0f && humidity < 35.0f) || (humidity > 55.0f && humidity <= 60.0f))
        return EnvBand::Moderate;
    if ((humidity >= 25.0f && humidity < 30.0f) || (humidity > 60.0f && humidity <= 70.0f))
        return EnvBand::Poor;
    return EnvBand::Critical;
}

static EnvBand bandFromPressure(float pressure)
{
    if (isnan(pressure) || pressure < 200.0f)
        return EnvBand::Unknown;
    if (pressure >= 995.0f && pressure <= 1025.0f)
        return EnvBand::Good;
    if ((pressure >= 985.0f && pressure < 995.0f) || (pressure > 1025.0f && pressure <= 1035.0f))
        return EnvBand::Moderate;
    if ((pressure >= 970.0f && pressure < 985.0f) || (pressure > 1035.0f && pressure <= 1045.0f))
        return EnvBand::Poor;
    return EnvBand::Critical;
}

static uint16_t colorForBand(EnvBand band)
{
    const bool monoTheme = (theme == 1);
    if (monoTheme)
    {
        switch (band)
        {
        case EnvBand::Good:
            return dma_display->color565(120, 120, 220);
        case EnvBand::Moderate:
            return dma_display->color565(90, 90, 180);
        case EnvBand::Poor:
            return dma_display->color565(70, 70, 150);
        case EnvBand::Critical:
            return dma_display->color565(50, 50, 110);
        default:
            return dma_display->color565(80, 80, 140);
        }
    }

    switch (band)
    {
    case EnvBand::Good:
        return dma_display->color565(54, 196, 93);
    case EnvBand::Moderate:
        return dma_display->color565(241, 196, 15);
    case EnvBand::Poor:
        return dma_display->color565(230, 126, 34);
    case EnvBand::Critical:
        return dma_display->color565(231, 76, 60);
    default:
        return dma_display->color565(120, 120, 120);
    }
}

static const char *shortLabelForBand(EnvBand band)
{
    switch (band)
    {
    case EnvBand::Good:
        return "Good";
    case EnvBand::Moderate:
        return "Moderate";
    case EnvBand::Poor:
        return "Poor";
    case EnvBand::Critical:
        return "Critical";
    default:
        return "Unknown";
    }
}

static int roundToInt(double value)
{
    return (value >= 0.0) ? static_cast<int>(value + 0.5) : static_cast<int>(value - 0.5);
}

static uint16_t makeColor(uint8_t r, uint8_t g, uint8_t b)
{
    if (dma_display)
        return dma_display->color565(r, g, b);
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}

static uint16_t adjustColor(uint16_t color, int dr, int dg, int db)
{
    auto clampComponent = [](int value) -> uint8_t {
        if (value < 0)
            return 0;
        if (value > 255)
            return 255;
        return static_cast<uint8_t>(value);
    };

    uint8_t r = ((color >> 11) & 0x1F) * 255 / 31;
    uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
    uint8_t b = (color & 0x1F) * 255 / 31;

    int ri = static_cast<int>(r) + dr;
    int gi = static_cast<int>(g) + dg;
    int bi = static_cast<int>(b) + db;

    return makeColor(clampComponent(ri), clampComponent(gi), clampComponent(bi));
}

static inline void setIconPixel(int x, int y, uint16_t color)
{
    if (x < 0 || x >= 16 || y < 0 || y >= 16)
        return;
    s_iconBitmap[y * 16 + x] = color;
}

static void fillIconBackground(uint16_t color)
{
    for (int i = 0; i < 16 * 16; ++i)
    {
        s_iconBitmap[i] = color;
    }
}

static void renderStatusEmojiBitmap(EnvBand band)
{
    const int size = 16;
    const double center = (size - 1) * 0.5; // 7.5
    const double radius = 7.35;
    const double radiusSq = radius * radius;
    const double outlineInner = radius - 1.1;
    const double outlineInnerSq = outlineInner * outlineInner;
    const double highlightOuter = radius - 2.5;
    const double highlightInner = radius - 4.5;
    const double highlightOuterSq = highlightOuter * highlightOuter;
    const double highlightInnerSq = highlightInner * highlightInner;

    s_iconBitmapBackground = (theme == 1) ? makeColor(5, 5, 15)
                                          : makeColor(0, 0, 0);
    fillIconBackground(s_iconBitmapBackground);

    if (!dma_display)
    {
        s_iconBitmapValid = true;
        s_iconBitmapBand = band;
        return;
    }

    bool isUnknown = (band == EnvBand::Unknown);
    uint16_t faceColor = isUnknown ? makeColor(160, 160, 160) : colorForBand(band);
    uint16_t featureColor = makeColor(0, 0, 0);

    // Fill face circle
    for (int y = 0; y < size; ++y)
    {
        double dy = (y + 0.5) - center;
        for (int x = 0; x < size; ++x)
        {
            double dx = (x + 0.5) - center;
            double distSq = dx * dx + dy * dy;
            if (distSq <= radiusSq)
            {
                setIconPixel(x, y, faceColor);
            }
        }
    }

    // Subtle highlight
    uint16_t highlightColor = adjustColor(faceColor, 35, 35, 35);
    for (int y = 0; y < size; ++y)
    {
        double dy = (y + 0.5) - center;
        for (int x = 0; x < size; ++x)
        {
            double dx = (x + 0.5) - center;
            double distSq = dx * dx + dy * dy;
            if (distSq <= highlightOuterSq && distSq >= highlightInnerSq && (dx + dy) < -2.5)
            {
                setIconPixel(x, y, highlightColor);
            }
        }
    }

    // Outline
    for (int y = 0; y < size; ++y)
    {
        double dy = (y + 0.5) - center;
        for (int x = 0; x < size; ++x)
        {
            double dx = (x + 0.5) - center;
            double distSq = dx * dx + dy * dy;
            if (distSq <= radiusSq && distSq >= outlineInnerSq)
            {
                setIconPixel(x, y, featureColor);
            }
        }
    }

    auto drawHappyEye = [&](double offsetX) {
        int eyeX = static_cast<int>(round(center + offsetX));
        int eyeY = static_cast<int>(round(center - 2.5));
        setIconPixel(eyeX - 1, eyeY, featureColor);
        setIconPixel(eyeX, eyeY - 1, featureColor);
        setIconPixel(eyeX + 1, eyeY, featureColor);
    };

    auto drawOpenEye = [&](double offsetX) {
        int eyeX = static_cast<int>(round(center + offsetX));
        int eyeY = static_cast<int>(round(center - 2.5));
        for (int dy = 0; dy < 2; ++dy)
        {
            for (int dx = 0; dx < 2; ++dx)
            {
                setIconPixel(eyeX + dx - 1, eyeY + dy, featureColor);
            }
        }
    };

    auto drawDroopyEye = [&](double offsetX) {
        int eyeX = static_cast<int>(round(center + offsetX));
        int eyeY = static_cast<int>(round(center - 2.0));
        setIconPixel(eyeX - 1, eyeY - 1, featureColor);
        setIconPixel(eyeX, eyeY, featureColor);
        setIconPixel(eyeX + 1, eyeY + 1, featureColor);
    };

    auto drawCrossEye = [&](double offsetX) {
        int eyeX = static_cast<int>(round(center + offsetX));
        int eyeY = static_cast<int>(round(center - 2.5));
        setIconPixel(eyeX - 1, eyeY - 1, featureColor);
        setIconPixel(eyeX + 1, eyeY - 1, featureColor);
        setIconPixel(eyeX, eyeY, featureColor);
        setIconPixel(eyeX - 1, eyeY + 1, featureColor);
        setIconPixel(eyeX + 1, eyeY + 1, featureColor);
    };

    auto drawDotEye = [&](double offsetX) {
        int eyeX = static_cast<int>(round(center + offsetX));
        int eyeY = static_cast<int>(round(center - 2.5));
        setIconPixel(eyeX, eyeY, featureColor);
    };

    switch (band)
    {
    case EnvBand::Good:
    {
        drawHappyEye(-3.0);
        drawHappyEye(3.0);

        uint16_t blush = adjustColor(faceColor, 30, -20, -20);
        setIconPixel(static_cast<int>(round(center - 4.0)), static_cast<int>(round(center + 1.5)), blush);
        setIconPixel(static_cast<int>(round(center + 4.0)), static_cast<int>(round(center + 1.5)), blush);

        for (int dx = -4; dx <= 4; ++dx)
        {
            double mouthY = center + 2.6 - (dx * dx) / 12.0;
            int px = static_cast<int>(round(center + dx));
            int py = static_cast<int>(round(mouthY));
            setIconPixel(px, py, featureColor);
            if (abs(dx) <= 1)
            {
                setIconPixel(px, py + 1, featureColor);
            }
        }
        break;
    }
    case EnvBand::Moderate:
    {
        drawOpenEye(-3.0);
        drawOpenEye(3.0);

        for (int dx = -4; dx <= 4; ++dx)
        {
            int px = static_cast<int>(round(center + dx));
            int py = static_cast<int>(round(center + 2.7));
            setIconPixel(px, py, featureColor);
        }
        break;
    }
    case EnvBand::Poor:
    {
        drawDroopyEye(-3.0);
        drawDroopyEye(3.0);

        for (int dx = -4; dx <= 4; ++dx)
        {
            double mouthY = center + 3.0 + (dx * dx) / 10.0;
            int px = static_cast<int>(round(center + dx));
            int py = static_cast<int>(round(mouthY));
            setIconPixel(px, py, featureColor);
            if (abs(dx) <= 1)
            {
                setIconPixel(px, py - 1, featureColor);
            }
        }

        uint16_t dropColor = makeColor(90, 180, 255);
        setIconPixel(static_cast<int>(round(center + 5.0)), static_cast<int>(round(center - 1.0)), dropColor);
        setIconPixel(static_cast<int>(round(center + 4.0)), static_cast<int>(round(center)), dropColor);
        setIconPixel(static_cast<int>(round(center + 5.0)), static_cast<int>(round(center)), dropColor);
        break;
    }
    case EnvBand::Critical:
    {
        drawCrossEye(-3.0);
        drawCrossEye(3.0);

        for (int i = -3; i <= 3; ++i)
        {
            int pxL = static_cast<int>(round(center - 1.5 - i));
            int pxR = static_cast<int>(round(center + 1.5 + i));
            int py = static_cast<int>(round(center - 3 + i));
            setIconPixel(pxL, py, featureColor);
            setIconPixel(pxR, py, featureColor);
        }
        for (int y = static_cast<int>(round(center - 1)); y <= static_cast<int>(round(center + 4)); ++y)
        {
            setIconPixel(static_cast<int>(round(center - 0.8)), y, featureColor);
            setIconPixel(static_cast<int>(round(center + 0.8)), y, featureColor);
        }
        setIconPixel(static_cast<int>(round(center)), static_cast<int>(round(center + 5)), featureColor);

        for (int dx = -2; dx <= 2; ++dx)
        {
            for (int dy = 0; dy <= 3; ++dy)
            {
                setIconPixel(static_cast<int>(round(center + dx)), static_cast<int>(round(center + 2 + dy)), featureColor);
            }
        }
        uint16_t mouthFill = makeColor(220, 70, 70);
        for (int dx = -1; dx <= 1; ++dx)
        {
            for (int dy = 1; dy <= 2; ++dy)
            {
                setIconPixel(static_cast<int>(round(center + dx)), static_cast<int>(round(center + 2 + dy)), mouthFill);
            }
        }
        uint16_t spark = makeColor(255, 220, 90);
        setIconPixel(static_cast<int>(round(center)), static_cast<int>(round(center - 5.5)), spark);
        setIconPixel(static_cast<int>(round(center)), static_cast<int>(round(center - 4.5)), spark);
        break;
    }
    case EnvBand::Unknown:
    {
        drawDotEye(-2.0);
        drawDotEye(2.0);

        for (int dx = -2; dx <= 2; ++dx)
        {
            double topY = center - 3.5 + (dx * dx) / 6.0;
            int px = static_cast<int>(round(center + dx));
            int py = static_cast<int>(round(topY));
            setIconPixel(px, py, featureColor);
        }
        for (int y = 0; y <= 2; ++y)
        {
            setIconPixel(static_cast<int>(round(center + 2)), static_cast<int>(round(center - 1 + y)), featureColor);
        }
        setIconPixel(static_cast<int>(round(center + 2)), static_cast<int>(round(center + 3)), featureColor);
        setIconPixel(static_cast<int>(round(center + 2)), static_cast<int>(round(center + 5)), featureColor);
        setIconPixel(static_cast<int>(round(center + 2)), static_cast<int>(round(center + 6)), featureColor);
        break;
    }
    default:
        drawOpenEye(-3.0);
        drawOpenEye(3.0);
        break;
    }

    s_iconBitmapValid = true;
    s_iconBitmapBand = band;
}

static void ensureIconBitmap(EnvBand band)
{
    uint16_t background = (theme == 1) ? makeColor(5, 5, 15)
                                       : makeColor(0, 0, 0);
    if (!s_iconBitmapValid || band != s_iconBitmapBand || background != s_iconBitmapBackground)
    {
        renderStatusEmojiBitmap(band);
        s_iconBitmapBackground = background;
    }
}

static uint16_t boostColor(uint16_t color, uint8_t boost)
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

static void appendDataSegment(String &target, const String &label, const String &value)
{
    if (value.length() == 0)
        return;
    if (target.length() > 0)
        target += " | ";
    target += label;
    target += " ";
    target += value;
}

static void appendAdvice(String &target, const String &text)
{
    if (text.length() == 0)
        return;
    if (target.length() > 0)
        target += "; ";
    target += text;
}

static String formatValueWithBand(const String &value, EnvBand band)
{
    if (value.length() == 0)
        return value;
    if (band == EnvBand::Unknown)
        return value;
    String result = value;
    if (result[result.length() - 1] != ' ')
        result += " ";
    result += shortLabelForBand(band);
    return result;
}

static String buildDetailsValue(int eqIndexInt,
                                EnvBand overallBand,
                                float co2,
                                EnvBand co2Band,
                                float tempC,
                                EnvBand tempBand,
                                float humidity,
                                EnvBand humBand,
                                float pressure,
                                EnvBand pressBand)
{
    String dataSegment;

    String eqValue = (eqIndexInt >= 0) ? String(eqIndexInt) : String("--");
    appendDataSegment(dataSegment, "EQI", formatValueWithBand(eqValue, overallBand));

    String co2String;
    if (isnan(co2) || co2 <= 0.0f)
    {
        co2String = "--";
    }
    else
    {
        int co2Rounded = roundToInt(co2);
        if (co2Rounded < 0)
            co2Rounded = 0;
        co2String = String(co2Rounded);
    }
    appendDataSegment(dataSegment, "CO2", formatValueWithBand(co2String, co2Band));

    String tempString;
    if (isnan(tempC))
    {
        tempString = "--";
    }
    else
    {
        double dispTempValue = dispTemp(tempC);
        int tempRounded = roundToInt(dispTempValue);
        tempString = String(tempRounded);
        tempString += (units.temp == TempUnit::F) ? "F" : "C";
    }
    appendDataSegment(dataSegment, "Temp", formatValueWithBand(tempString, tempBand));

    String humString;
    if (isnan(humidity))
    {
        humString = "--";
    }
    else
    {
        int humRounded = roundToInt(humidity);
        if (humRounded < 0)
            humRounded = 0;
        if (humRounded > 100)
            humRounded = 100;
        humString = String(humRounded);
        humString += "%";
    }
    appendDataSegment(dataSegment, "Humidity", formatValueWithBand(humString, humBand));

    String pressString;
    if (isnan(pressure))
    {
        pressString = "--";
    }
    else
    {
        double dispPressValue = dispPress(pressure);
        if (units.press == PressUnit::INHG)
        {
            pressString = String(dispPressValue, 2);
            pressString += "inHg";
        }
        else
        {
            pressString = String(roundToInt(dispPressValue));
            pressString += "hPa";
        }
    }
    appendDataSegment(dataSegment, "Baro", formatValueWithBand(pressString, pressBand));

    String adviceSegment;

    if (!isnan(co2) && co2Band != EnvBand::Unknown)
    {
        if (co2Band == EnvBand::Moderate)
        {
            appendAdvice(adviceSegment, "Turn on fan to lower CO2");
        }
        else if (co2Band == EnvBand::Poor || co2Band == EnvBand::Critical)
        {
            appendAdvice(adviceSegment, "Turn on fan or open windows");
        }
    }

    if (!isnan(humidity))
    {
        if (humidity < 40.0f)
        {
            appendAdvice(adviceSegment, "Activate humidifier");
        }
        else if (humidity > 65.0f)
        {
            if (!isnan(tempC) && tempC > 27.0f)
            {
                appendAdvice(adviceSegment, "Use dehumidifier or AC");
            }
            else
            {
                appendAdvice(adviceSegment, "Reduce indoor humidity");
            }
        }
    }

    if (!isnan(tempC) && tempBand != EnvBand::Unknown)
    {
        if (tempBand == EnvBand::Poor)
        {
            appendAdvice(adviceSegment, "Adjust thermostat toward comfort range");
        }
        else if (tempBand == EnvBand::Critical)
        {
            appendAdvice(adviceSegment, "Check heating or cooling immediately");
        }
    }

    if (!isnan(pressure) && pressBand != EnvBand::Unknown)
    {
        if (pressBand == EnvBand::Poor)
        {
            appendAdvice(adviceSegment, "Barometer drifting; expect weather changes");
        }
        else if (pressBand == EnvBand::Critical)
        {
            appendAdvice(adviceSegment, "Unusual pressure; monitor conditions closely");
        }
    }

    if (adviceSegment.length() == 0)
    {
        switch (overallBand)
        {
        case EnvBand::Good:
            appendAdvice(adviceSegment, "Air quality looks good");
            break;
        case EnvBand::Moderate:
            appendAdvice(adviceSegment, "Monitor readings; add light ventilation if needed");
            break;
        case EnvBand::Poor:
            appendAdvice(adviceSegment, "Multiple issues detected; ventilate and adjust HVAC");
            break;
        case EnvBand::Critical:
            appendAdvice(adviceSegment, "Take immediate action to improve air quality");
            break;
        default:
            appendAdvice(adviceSegment, "Awaiting stable sensor data");
            break;
        }
    }

    String result = dataSegment;
    if (adviceSegment.length() > 0)
    {
        if (result.length() > 0)
        {
            result += " | Tips: ";
        }
        else
        {
            result = "Tips: ";
        }
        result += adviceSegment;
    }

    if (result.length() == 0)
    {
        result = "Awaiting sensor data";
    }

    return result;
}

static void updateDetailsDisplay(const String &value)
{
    String display = " ";
    display += value;
    display += "   ";

    auto setDisplay = [&](const String &text, bool resetOffset) {
        s_detailsDisplayText = text;
        s_detailsDisplayWidth = getTextWidth(s_detailsDisplayText.c_str());
        if (resetOffset)
        {
            s_detailsScrollOffset = 0;
            s_detailsLastScroll = millis();
        }
    };

    if (s_detailsDisplayText.length() == 0)
    {
        setDisplay(display, true);
        s_detailsPendingText = "";
        return;
    }

    if (display == s_detailsDisplayText || display == s_detailsPendingText)
        return;

    if (s_detailsDisplayWidth <= InfoScreen::SCREEN_WIDTH)
    {
        setDisplay(display, true);
        s_detailsPendingText = "";
    }
    else
    {
        s_detailsPendingText = display;
    }
}

static void drawEnvQualityOverlay(int lineIndex, int y, bool selected)
{
    if (lineIndex < 0 || lineIndex >= s_lineBandCount)
        return;

    const int iconSize = 16;
    const int iconX = InfoScreen::SCREEN_WIDTH - iconSize;
    const uint16_t background = (theme == 1) ? dma_display->color565(5, 5, 15)
                                             : dma_display->color565(0, 0, 0);

    if (lineIndex == 0)
    {
        dma_display->fillRect(iconX, y, iconSize, InfoScreen::CHARH, background);
        if (s_lineBandCount < 2)
        {
            ensureIconBitmap(s_overallBand);
            dma_display->drawRGBBitmap(iconX, y, s_iconBitmap, iconSize, iconSize);
        }
    }
    else if (lineIndex == 1)
    {
        ensureIconBitmap(s_overallBand);
        dma_display->drawRGBBitmap(iconX, y - InfoScreen::CHARH, s_iconBitmap, iconSize, iconSize);
    }
    else if (lineIndex == 2)
    {
        if (!dma_display || s_detailsDisplayText.length() == 0)
            return;

        dma_display->fillRect(0, y, InfoScreen::SCREEN_WIDTH, InfoScreen::CHARH, 0);

        uint16_t valueColor = colorForBand(s_overallBand);
        if (selected)
        {
            valueColor = boostColor(valueColor, 30);
        }

        const int areaWidth = InfoScreen::SCREEN_WIDTH;
        if (s_detailsDisplayWidth <= areaWidth)
        {
            if (s_detailsPendingText.length() > 0)
            {
                s_detailsDisplayText = s_detailsPendingText;
                s_detailsPendingText = "";
                s_detailsDisplayWidth = getTextWidth(s_detailsDisplayText.c_str());
                s_detailsScrollOffset = 0;
                s_detailsLastScroll = millis();
            }
            dma_display->setTextColor(valueColor);
            dma_display->setCursor(0, y);
            dma_display->print(s_detailsDisplayText);
            return;
        }

        const int gap = 12;
        unsigned long now = millis();
        if (now - s_detailsLastScroll > (unsigned)scrollSpeed)
        {
            s_detailsScrollOffset++;
            s_detailsLastScroll = now;
            if (s_detailsScrollOffset >= s_detailsDisplayWidth + gap)
            {
                s_detailsScrollOffset = 0;
                if (s_detailsPendingText.length() > 0)
                {
                    s_detailsDisplayText = s_detailsPendingText;
                    s_detailsPendingText = "";
                    s_detailsDisplayWidth = getTextWidth(s_detailsDisplayText.c_str());
                    s_detailsLastScroll = now;
                }
            }
        }

        int cursorX = -s_detailsScrollOffset;

        dma_display->setTextColor(valueColor);
        dma_display->setCursor(cursorX, y);
        dma_display->print(s_detailsDisplayText);
        dma_display->setCursor(cursorX + s_detailsDisplayWidth + gap, y);
        dma_display->print(s_detailsDisplayText);
    }
}

void showEnvironmentalQualityScreen()
{
    EnvBand previousOverall = s_overallBand;

    float co2Raw = (SCD40_co2 > 0) ? static_cast<float>(SCD40_co2) : NAN;
    float tempC = !isnan(SCD40_temp) ? SCD40_temp : aht20_temp;
    float humidity = !isnan(SCD40_hum) ? SCD40_hum : aht20_hum;
    float pressure = (!isnan(bmp280_pressure) && bmp280_pressure > 200.0f) ? bmp280_pressure : NAN;

    EnvBand co2Band = bandFromCo2(co2Raw);
    EnvBand tempBand = bandFromTemp(tempC);
    EnvBand humBand = bandFromHumidity(humidity);
    EnvBand pressBand = bandFromPressure(pressure);

    EnvBand bands[4] = {co2Band, tempBand, humBand, pressBand};
    int totalScore = 0;
    int validCount = 0;
    for (int i = 0; i < 4; ++i)
    {
        int s = scoreForBand(bands[i]);
        if (s >= 0)
        {
            totalScore += s;
            ++validCount;
        }
    }

    float eqIndex = (validCount > 0)
                        ? (static_cast<float>(totalScore) / (validCount * 3.0f)) * 100.0f
                        : -1.0f;
    int eqIndexInt = (eqIndex >= 0.0f) ? static_cast<int>(eqIndex + 0.5f) : -1;
    EnvBand overallBand = (validCount > 0) ? bandFromIndex(eqIndex) : EnvBand::Unknown;

    String lines[3];
    uint16_t colors[3];
    int lineCount = 0;

    String eqLine = "EQI: ";
    if (eqIndexInt >= 0)
    {
        eqLine += String(eqIndexInt);
   //      eqLine += " ";
   //     eqLine += shortLabelForBand(overallBand);
    }
    else
    {
        eqLine += "--";
    }
    lines[lineCount] = eqLine;
    colors[lineCount] = colorForBand(overallBand);
    s_lineBands[lineCount] = overallBand;
    ++lineCount;

    String co2Line = "CO2: ";
    if (co2Band == EnvBand::Unknown)
    {
        co2Line += "--";
    }
    else
    {
        co2Line += String(roundToInt(co2Raw));
    }
    lines[lineCount] = co2Line;
    colors[lineCount] = colorForBand(co2Band);
    s_lineBands[lineCount] = co2Band;
    ++lineCount;

    String detailsValue = buildDetailsValue(eqIndexInt,
                                            overallBand,
                                            co2Raw,
                                            co2Band,
                                            tempC,
                                            tempBand,
                                            humidity,
                                            humBand,
                                            pressure,
                                            pressBand);
    updateDetailsDisplay(detailsValue);
    String detailsLine = " ";
    lines[lineCount] = detailsLine;
    colors[lineCount] = colorForBand(overallBand);
    s_lineBands[lineCount] = overallBand;
    ++lineCount;

    s_lineBandCount = lineCount;
    bool iconStatusChanged = (overallBand != previousOverall);
    s_overallBand = overallBand;
    for (int i = lineCount; i < 3; ++i)
    {
        s_lineBands[i] = EnvBand::Unknown;
    }

    envQualityScreen.setHighlightEnabled(true);
    envQualityScreen.setLineOverlay(drawEnvQualityOverlay);

    bool wasActive = envQualityScreen.isActive();
    envQualityScreen.setLines(lines, lineCount, !wasActive, colors);
    if (!wasActive)
    {
        envQualityScreen.show([]() { currentScreen = homeScreenForDataSource(); });
    }
    if (!wasActive || iconStatusChanged)
    {
        s_iconBitmapValid = false;
    }
    int preferredLine = (lineCount >= 3) ? 2 : (lineCount - 1);
    if (preferredLine >= 0)
    {
        envQualityScreen.setSelectedLine(preferredLine);
    }
}
