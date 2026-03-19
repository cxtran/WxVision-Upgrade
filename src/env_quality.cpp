#include <math.h>
#include <algorithm>

#include "env_quality.h"
#include "display.h"
#include "InfoScreen.h"
#include "settings.h"
#include "sensors.h"
#include "screen_manager.h"
#include "units.h"
#include "ui_theme.h"
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
static float s_eqIndexValue = -1.0f;
static float s_iconBitmapEqValue = -1.0f;
static float s_co2Value = NAN;
static EnvBand s_co2Band = EnvBand::Unknown;
static bool s_iconBitmapCo2Valid = false;
static float s_iconBitmapCo2Value = 0.0f;
static EnvBand s_iconBitmapCo2Band = EnvBand::Unknown;

static EnvBand s_detailsBandOverall = EnvBand::Unknown;
static EnvBand s_detailsBandEQI = EnvBand::Unknown;
static EnvBand s_detailsBandCO2 = EnvBand::Unknown;
static EnvBand s_detailsBandTemp = EnvBand::Unknown;
static EnvBand s_detailsBandHumidity = EnvBand::Unknown;
static EnvBand s_detailsBandBaro = EnvBand::Unknown;
static String s_lineTexts[3];
static bool s_prevCo2HighAlert = false;
static bool s_prevTempHighAlert = false;
static bool s_prevHumidityAlert = false;
static bool s_prevSensorFailureAlert = false;
static unsigned long s_lastCo2AlertMs = 0;
static unsigned long s_lastTempAlertMs = 0;
static unsigned long s_lastHumidityAlertMs = 0;
static unsigned long s_lastSensorFailureAlertMs = 0;

static constexpr unsigned long kEnvAlertCooldownMs = 5UL * 60UL * 1000UL;
static constexpr uint16_t kEnvAlertDisplayMs = 4000;

static void updateDetailsBands(EnvBand overall, EnvBand co2, EnvBand temp, EnvBand humidity, EnvBand pressure)
{
    s_detailsBandOverall = overall;
    s_detailsBandEQI = overall;
    s_detailsBandCO2 = co2;
    s_detailsBandTemp = temp;
    s_detailsBandHumidity = humidity;
    s_detailsBandBaro = pressure;
}

static EnvBand bandForDetailsLabel(const String &label)
{
    if (label.equalsIgnoreCase("EQI"))
        return s_detailsBandEQI;
    if (label.equalsIgnoreCase("CO2"))
        return s_detailsBandCO2;
    if (label.equalsIgnoreCase("TEMP"))
        return s_detailsBandTemp;
    if (label.equalsIgnoreCase("HUMIDITY"))
        return s_detailsBandHumidity;
    if (label.equalsIgnoreCase("BARO"))
        return s_detailsBandBaro;
    if (label.equalsIgnoreCase("TIPS"))
        return s_detailsBandOverall;
    return EnvBand::Unknown;
}

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

static bool shouldRetriggerEnvAlert(bool active, bool &previousActive, unsigned long &lastAlertMs, unsigned long nowMs)
{
    const bool risingEdge = active && !previousActive;
    const bool cooldownElapsed = active && (lastAlertMs == 0 || (nowMs - lastAlertMs) >= kEnvAlertCooldownMs);
    previousActive = active;
    if (!active)
        return false;
    if (risingEdge || cooldownElapsed)
    {
        lastAlertMs = nowMs;
        return true;
    }
    return false;
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
    s_iconBitmapBackground = makeColor(0, 0, 0);
    fillIconBackground(s_iconBitmapBackground);

    bool co2Valid = !isnan(s_co2Value) && s_co2Value > 0.0f;

    if (!dma_display)
    {
        s_iconBitmapValid = true;
        s_iconBitmapBand = band;
        s_iconBitmapEqValue = s_eqIndexValue;
        s_iconBitmapCo2Valid = co2Valid;
        s_iconBitmapCo2Value = co2Valid ? s_co2Value : 0.0f;
        s_iconBitmapCo2Band = s_co2Band;
        return;
    }

    const int barLeft = 1;
    const int barRight = size - 2;
    const int barWidth = barRight - barLeft + 1;
    const int barHeight = 3;
    const int topMargin = 2;

    int eqBarTop = topMargin;
    int co2BarTop = size - topMargin - barHeight;
    if (co2BarTop <= eqBarTop + barHeight)
        co2BarTop = eqBarTop + barHeight + 2;
    if (co2BarTop > size - barHeight - 1)
        co2BarTop = size - barHeight - 1;
    if (co2BarTop < 0)
        co2BarTop = 0;

    auto normalizedToPixels = [&](float norm, bool usePowerCurve, float curveExponent) -> int {
        if (norm <= 0.0f)
            return 0;
        if (norm > 1.0f)
            norm = 1.0f;
        float displayNorm;
        if (usePowerCurve)
        {
            if (curveExponent <= 0.0f)
                curveExponent = 1.0f;
            displayNorm = powf(norm, curveExponent);
        }
        else
        {
            displayNorm = log10f(1.0f + 9.0f * norm) / log10f(10.0f);
        }
        int pix = static_cast<int>(roundf(displayNorm * barWidth));
        if (pix < 0)
            pix = 0;
        if (pix > barWidth)
            pix = barWidth;
        return pix;
    };

    struct Segment
    {
        float start;
        float end;
        EnvBand band;
    };
    const Segment eqSegments[] = {
        {0.0f, 0.25f, EnvBand::Critical},
        {0.25f, 0.50f, EnvBand::Poor},
        {0.50f, 0.75f, EnvBand::Moderate},
        {0.75f, 1.0f, EnvBand::Good},
    };

    const Segment co2Segments[] = {
        {0.0f, 0.25f, EnvBand::Good},
        {0.25f, 0.50f, EnvBand::Moderate},
        {0.50f, 0.75f, EnvBand::Poor},
        {0.75f, 1.0f, EnvBand::Critical},
    };

    auto drawValueBar = [&](float norm,
                            EnvBand valueBand,
                            int barTop,
                            bool arrowUp,
                            const Segment *segments,
                            int segmentCount,
                            bool usePowerCurve,
                            float curveExponent) {
        if (norm < 0.0f)
            return;

        int barBottom = barTop + barHeight - 1;
        if (barBottom >= size)
            barBottom = size - 1;

        uint16_t barBackground;
        if (theme == 1)
        {
            barBackground = makeColor(45, 45, 95);
        }
        else
        {
            barBackground = makeColor(48, 48, 48);
        }
        for (int x = 0; x < barWidth; ++x)
        {
            int drawX = barLeft + x;
            for (int y = barTop; y <= barBottom; ++y)
                setIconPixel(drawX, y, barBackground);
        }

        int filledPixels = normalizedToPixels(norm, usePowerCurve, curveExponent);
        if (norm >= 0.0f && filledPixels == 0 && valueBand != EnvBand::Unknown)
            filledPixels = 1;
        if (filledPixels > barWidth)
            filledPixels = barWidth;

        for (int i = 0; i < segmentCount; ++i)
        {
            if (filledPixels <= 0)
                break;

            const Segment &seg = segments[i];
            int segStart = normalizedToPixels(seg.start, usePowerCurve, curveExponent);
            int segEnd = normalizedToPixels(seg.end, usePowerCurve, curveExponent);
            int drawEnd = std::min(filledPixels, segEnd);
            if (drawEnd <= segStart)
                continue;

            uint16_t segColor = colorForBand(seg.band);
            for (int x = segStart; x < drawEnd; ++x)
            {
                int drawX = barLeft + x;
                for (int y = barTop; y <= barBottom; ++y)
                    setIconPixel(drawX, y, segColor);
            }
        }

        if (filledPixels > 0)
        {
            int markerY = barTop + barHeight / 2;
            EnvBand arrowBand = (valueBand == EnvBand::Unknown) ? band : valueBand;
            uint16_t indicatorColor = adjustColor(colorForBand(arrowBand), 40, 40, 40);
            int xIndicator = barLeft + (filledPixels - 1);

            auto drawVerticalArrow = [&](const int offsets[4]) {
                for (int i = 0; i < 4; ++i)
                {
                    int yPos = markerY + offsets[i];
                    setIconPixel(xIndicator, yPos, indicatorColor);
                }
            };

            if (arrowUp)
            {
                const int offsets[4] = {-1, 0, 1, 2};
                drawVerticalArrow(offsets);
            }
            else
            {
                const int offsets[4] = {1, 0, -1, -2};
                drawVerticalArrow(offsets);
            }
        }
    };

    float eqNorm = (s_eqIndexValue >= 0.0f) ? (std::min(s_eqIndexValue, 100.0f) / 100.0f) : -1.0f;
    const int eqSegmentCount = static_cast<int>(sizeof(eqSegments) / sizeof(eqSegments[0]));
    const float eqCurveExponent = 1.35f;
    drawValueBar(eqNorm, band, eqBarTop, true, eqSegments, eqSegmentCount, true, eqCurveExponent);

    float co2Norm = -1.0f;
    if (co2Valid)
    {
        float norm = (s_co2Value - 400.0f) / 1600.0f;
        if (norm < 0.0f)
            norm = 0.0f;
        if (norm > 1.0f)
            norm = 1.0f;
        co2Norm = norm;
    }
    const int co2SegmentCount = static_cast<int>(sizeof(co2Segments) / sizeof(co2Segments[0]));
    drawValueBar(co2Norm, s_co2Band, co2BarTop, false, co2Segments, co2SegmentCount, false, 1.0f);

    ui_theme::applyGraphicThemeToBuffer(s_iconBitmap, 16 * 16);

    s_iconBitmapValid = true;
    s_iconBitmapBand = band;
    s_iconBitmapEqValue = s_eqIndexValue;
    s_iconBitmapCo2Valid = co2Valid;
    s_iconBitmapCo2Value = co2Valid ? s_co2Value : 0.0f;
    s_iconBitmapCo2Band = s_co2Band;
}
static void ensureIconBitmap(EnvBand band)
{
    uint16_t background = makeColor(0, 0, 0);
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
        target += " ¦ ";
    target += label;
    if (!label.endsWith(":"))
        target += ":";
    target += " ";
    target += value;
}

static void appendAdvice(String &target, const String &text)
{
    if (text.length() == 0)
        return;
    if (target.length() > 0)
        target += "; ";
    target += text + ".";
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

static String formatValueWithLabel(const String &value, const char *label)
{
    if (value.length() == 0)
        return value;
    if (!label || label[0] == '\0')
        return value;
    String result = value;
    if (result[result.length() - 1] != ' ')
        result += " ";
    result += label;
    return result;
}

static const char *tempDescriptor(float tempC)
{
    if (isnan(tempC))
        return nullptr;
    if (tempC < 16.0f)
        return "Very cold";
    if (tempC < 18.0f)
        return "Cold";
    if (tempC < 20.0f)
        return "Cool";
    if (tempC <= 24.0f)
        return "Comfortable";
    if (tempC <= 26.0f)
        return "Warm";
    if (tempC <= 28.0f)
        return "Hot";
    return "Very hot";
}

static const char *humidityDescriptor(float humidity)
{
    if (isnan(humidity))
        return nullptr;
    if (humidity < 25.0f)
        return "Very dry";
    if (humidity < 30.0f)
        return "Dry";
    if (humidity < 35.0f)
        return "Slightly dry";
    if (humidity <= 55.0f)
        return "Comfortable";
    if (humidity <= 60.0f)
        return "Humid";
    if (humidity <= 70.0f)
        return "Very humid";
    return "Extremely humid";
}

static const char *co2Descriptor(float co2)
{
    if (isnan(co2) || co2 <= 0.0f)
        return nullptr;
    if (co2 <= 600.0f)
        return "Fresh air";
    if (co2 <= 800.0f)
        return "Comfortable";
    if (co2 <= 1000.0f)
        return "Slightly elevated";
    if (co2 <= 1200.0f)
        return "Ventilate soon";
    if (co2 <= 1600.0f)
        return "Stale air";
    if (co2 <= 2000.0f)
        return "Poor ventilation";
    return "Unsafe level";
}

static const char *baroDescriptor(float pressure)
{
    if (isnan(pressure) || pressure < 200.0f)
        return nullptr;
    if (pressure < 985.0f)
        return "Low pressure";
    if (pressure < 995.0f)
        return "Slightly low";
    if (pressure <= 1025.0f)
        return "Normal";
    if (pressure <= 1035.0f)
        return "Slightly high";
    if (pressure <= 1045.0f)
        return "High pressure";
    return "Extreme pressure";
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
    const char *co2Label = co2Descriptor(co2);
    String co2Formatted = co2Label ? formatValueWithLabel(co2String, co2Label)
                                   : formatValueWithBand(co2String, co2Band);
    appendDataSegment(dataSegment, "CO2", co2Formatted);

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
        tempString += (units.temp == TempUnit::F) ? "°F" : "°C";
    }
    const char *tempLabel = tempDescriptor(tempC);
    String tempFormatted = tempLabel ? formatValueWithLabel(tempString, tempLabel)
                                     : formatValueWithBand(tempString, tempBand);
    appendDataSegment(dataSegment, "Temp", tempFormatted);

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
    const char *humidityLabel = humidityDescriptor(humidity);
    String humidityFormatted = humidityLabel ? formatValueWithLabel(humString, humidityLabel)
                                             : formatValueWithBand(humString, humBand);
    appendDataSegment(dataSegment, "Humidity", humidityFormatted);

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
    const char *baroLabel = baroDescriptor(pressure);
    String baroFormatted = baroLabel ? formatValueWithLabel(pressString, baroLabel)
                                     : formatValueWithBand(pressString, pressBand);
    appendDataSegment(dataSegment, "Baro", baroFormatted);

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
            result += " ¦ Tips: ";
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
    String display = value;
 //   display += value;
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

static void drawDetailsFormatted(int startX,
                                 int y,
                                 const String &text,
                                 uint16_t prefixColor,
                                 uint16_t labelColor,
                                 uint16_t valueColor,
                                 bool selected)
{
    int len = text.length();
    if (len == 0 || !dma_display)
        return;

    const int screenWidth = InfoScreen::SCREEN_WIDTH;
    int idx = 0;
    int x = startX;

    auto drawToken = [&](const String &token, uint16_t color) {
        if (token.length() == 0)
            return;
        int tokenWidth = getTextWidth(token.c_str());
        int tokenEnd = x + tokenWidth;
        if (tokenEnd > 0 && x < screenWidth)
        {
            dma_display->setTextColor(color);
            dma_display->setCursor(x, y);
            dma_display->print(token);
        }
        x += tokenWidth;
    };

    EnvBand currentBand = EnvBand::Unknown;
    bool inValueSegment = false;
    const uint8_t labelBoost = selected ? 40 : 24;
    const uint8_t valueBoost = selected ? 30 : 0;

    while (idx < len)
    {
        char c = text[idx];
        if (c == ' ')
        {
            uint16_t spaceColor;
            if (inValueSegment && currentBand != EnvBand::Unknown)
            {
                spaceColor = colorForBand(currentBand);
                if (valueBoost)
                    spaceColor = boostColor(spaceColor, valueBoost);
            }
            else
            {
                spaceColor = prefixColor;
            }
            drawToken(" ", spaceColor);
            idx++;
            continue;
        }
        if (c == '|')
        {
            drawToken("|", prefixColor);
            currentBand = EnvBand::Unknown;
            inValueSegment = false;
            idx++;
            continue;
        }

        int wordStart = idx;
        while (idx < len && text[idx] != ' ' && text[idx] != '|')
            idx++;
        String word = text.substring(wordStart, idx);
        if (word.length() == 0)
            continue;

        int colonIndex = word.indexOf(':');
        if (colonIndex >= 0)
        {
            String label = word.substring(0, colonIndex);
            label.trim();
            EnvBand labelBand = bandForDetailsLabel(label);
            currentBand = labelBand;
            inValueSegment = true;

            uint16_t labelTokenColor = labelColor;
            drawToken(word, labelTokenColor);
            continue;
        }

        if (!inValueSegment || currentBand == EnvBand::Unknown)
        {
            drawToken(word, prefixColor);
            continue;
        }

        uint16_t bandColor = colorForBand(currentBand);
        if (valueBoost)
            bandColor = boostColor(bandColor, valueBoost);
        drawToken(word, bandColor);
    }
}

static void drawEnvQualityOverlay(int lineIndex, int y, bool selected)
{
    if (lineIndex < 0 || lineIndex >= s_lineBandCount)
        return;

    auto drawWhiteLabelPrefix = [&](int idx) {
        if (!dma_display)
            return;
        if (idx < 0 || idx >= 3)
            return;
        const String &lineText = s_lineTexts[idx];
        int length = lineText.length();
        if (length == 0)
            return;

        int splitIndex = -1;
        for (int i = 0; i < length; ++i)
        {
            char c = lineText[i];
            if (c == ':' || c == ' ')
            {
                splitIndex = i + 1;
                if (c == ' ')
                {
                    while (splitIndex < length && lineText[splitIndex] == ' ')
                        ++splitIndex;
                }
                else
                {
                    while (splitIndex < length && lineText[splitIndex] == ' ')
                        ++splitIndex;
                }
                break;
            }
        }
        if (splitIndex <= 0)
            return;

        String labelPart = lineText.substring(0, splitIndex);
        dma_display->setTextColor(dma_display->color565(255, 255, 255));
        dma_display->setCursor(0, y);
        dma_display->print(labelPart);
    };

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
        drawWhiteLabelPrefix(lineIndex);
    }
    else if (lineIndex == 1)
    {
        ensureIconBitmap(s_overallBand);
        dma_display->drawRGBBitmap(iconX, y - InfoScreen::CHARH, s_iconBitmap, iconSize, iconSize);
        drawWhiteLabelPrefix(lineIndex);
    }
    else if (lineIndex == 2)
    {
        if (!dma_display || s_detailsDisplayText.length() == 0)
            return;

        dma_display->fillRect(0, y, InfoScreen::SCREEN_WIDTH, InfoScreen::CHARH, 0);

        const bool monoTheme = (theme == 1);
        uint16_t valueColor = colorForBand(s_overallBand);
        if (selected)
        {
            valueColor = boostColor(valueColor, 30);
        }
        uint16_t labelColor = dma_display->color565(255, 255, 255);
        uint16_t prefixColor = monoTheme
                                   ? dma_display->color565(150, 150, 210)
                                   : dma_display->color565(235, 235, 250);

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
            drawDetailsFormatted(0, y, s_detailsDisplayText, prefixColor, labelColor, valueColor, selected);
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
        drawDetailsFormatted(cursorX, y, s_detailsDisplayText, prefixColor, labelColor, valueColor, selected);
        drawDetailsFormatted(cursorX + s_detailsDisplayWidth + gap, y, s_detailsDisplayText, prefixColor, labelColor, valueColor, selected);
    }
}

void showEnvironmentalQualityScreen()
{
    EnvBand previousOverall = s_overallBand;

    float co2Raw = (SCD40_co2 > 0) ? static_cast<float>(SCD40_co2) : NAN;
    float tempC = NAN;
    if (!isnan(SCD40_temp))
        tempC = SCD40_temp + tempOffset;
    else if (!isnan(aht20_temp))
        tempC = aht20_temp + tempOffset;
    float humidity = !isnan(SCD40_hum) ? SCD40_hum : aht20_hum;
    if (!isnan(humidity))
    {
        humidity += static_cast<float>(humOffset);
        if (humidity < 0.0f)
            humidity = 0.0f;
        if (humidity > 100.0f)
            humidity = 100.0f;
    }
    float pressure = (!isnan(bmp280_pressure) && bmp280_pressure > 200.0f) ? bmp280_pressure : NAN;

    EnvBand co2Band = bandFromCo2(co2Raw);
    EnvBand tempBand = bandFromTemp(tempC);
    EnvBand humBand = bandFromHumidity(humidity);
    EnvBand pressBand = bandFromPressure(pressure);

    s_co2Value = co2Raw;
    s_co2Band = co2Band;

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
    s_eqIndexValue = eqIndex;

    String lines[3];
    uint16_t colors[3];
    int lineCount = 0;

    String eqLine = "EQI ";
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
    s_lineTexts[lineCount] = eqLine;
    ++lineCount;

    String co2Line = "CO2 ";
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
    s_lineTexts[lineCount] = co2Line;
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
    updateDetailsBands(overallBand, co2Band, tempBand, humBand, pressBand);
    updateDetailsDisplay(detailsValue);
    String detailsLine = " ";
    lines[lineCount] = detailsLine;
    colors[lineCount] = colorForBand(overallBand);
    s_lineBands[lineCount] = overallBand;
    s_lineTexts[lineCount] = detailsLine;
    ++lineCount;

    s_lineBandCount = lineCount;
    bool eqValueChanged = false;
    if ((s_iconBitmapEqValue < 0.0f) != (s_eqIndexValue < 0.0f))
    {
        eqValueChanged = true;
    }
    else if (s_eqIndexValue >= 0.0f && s_iconBitmapEqValue >= 0.0f)
    {
        eqValueChanged = fabsf(s_eqIndexValue - s_iconBitmapEqValue) > 0.5f;
    }

    bool co2Valid = !isnan(co2Raw) && co2Raw > 0.0f;
    bool co2ValueChanged = false;
    if (co2Valid != s_iconBitmapCo2Valid)
    {
        co2ValueChanged = true;
    }
    else if (co2Valid)
    {
        co2ValueChanged = fabsf(co2Raw - s_iconBitmapCo2Value) > 5.0f;
    }
    bool co2BandChanged = (co2Band != s_iconBitmapCo2Band);

    bool iconStatusChanged = (overallBand != previousOverall) || eqValueChanged || co2ValueChanged || co2BandChanged;
    s_overallBand = overallBand;
    for (int i = lineCount; i < 3; ++i)
    {
        s_lineBands[i] = EnvBand::Unknown;
        s_lineTexts[i] = "";
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

void serviceEnvironmentalAlerts()
{
    const unsigned long nowMs = millis();

    float co2Raw = (SCD40_co2 > 0) ? static_cast<float>(SCD40_co2) : NAN;
    float tempC = NAN;
    if (!isnan(SCD40_temp))
        tempC = SCD40_temp + tempOffset;
    else if (!isnan(aht20_temp))
        tempC = aht20_temp + tempOffset;

    float humidity = !isnan(SCD40_hum) ? SCD40_hum : aht20_hum;
    if (!isnan(humidity))
    {
        humidity += static_cast<float>(humOffset);
        if (humidity < 0.0f)
            humidity = 0.0f;
        if (humidity > 100.0f)
            humidity = 100.0f;
    }

    const EnvBand co2Band = bandFromCo2(co2Raw);
    const EnvBand tempBand = bandFromTemp(tempC);
    const EnvBand humBand = bandFromHumidity(humidity);

    // Drive the alert directly from the measured ppm threshold so it does not
    // depend on a secondary band classification near the cutoff.
    const bool co2High = envAlertCo2Enabled &&
                         !isnan(co2Raw) && co2Raw >= static_cast<float>(envAlertCo2Threshold);
    const bool tempHighRaw = envAlertTempEnabled &&
                             !isnan(tempC) && tempC > envAlertTempThresholdC &&
                             (tempBand == EnvBand::Poor || tempBand == EnvBand::Critical);
    const bool humidityWarnRaw = envAlertHumidityEnabled &&
                               !isnan(humidity) &&
                               ((humidity < static_cast<float>(envAlertHumidityLowThreshold)) ||
                                (humidity > static_cast<float>(envAlertHumidityHighThreshold))) &&
                               (humBand == EnvBand::Poor || humBand == EnvBand::Critical);
    const bool sensorFailure = !scd40Ready || !aht20Ready || !bmp280Ready;
    const bool tempHigh = !co2High && tempHighRaw;
    const bool humidityWarn = !co2High && !tempHigh && humidityWarnRaw;

    const bool queueSensorFailure = shouldRetriggerEnvAlert(sensorFailure, s_prevSensorFailureAlert, s_lastSensorFailureAlertMs, nowMs);
    const bool queueCo2 = shouldRetriggerEnvAlert(co2High, s_prevCo2HighAlert, s_lastCo2AlertMs, nowMs);
    const bool queueTemp = shouldRetriggerEnvAlert(tempHigh, s_prevTempHighAlert, s_lastTempAlertMs, nowMs);
    const bool queueHumidity = shouldRetriggerEnvAlert(humidityWarn, s_prevHumidityAlert, s_lastHumidityAlertMs, nowMs);

    // Queue at most one environmental alert per pass so the highest-priority
    // condition is readable instead of being immediately followed by others.
    if (queueSensorFailure)
    {
        queueTemporaryAlertHeading("Sensor Failure", kEnvAlertDisplayMs, 0x53454E01UL);
    }
    else if (queueCo2)
    {
        queueTemporaryAlertHeading("CO2 Too High", kEnvAlertDisplayMs, 0x434F3201UL);
    }
    else if (queueTemp)
    {
        queueTemporaryAlertHeading("Temperature Too High", kEnvAlertDisplayMs, 0x54454D01UL);
    }
    else if (queueHumidity)
    {
        queueTemporaryAlertHeading("Humidity Warning", kEnvAlertDisplayMs, 0x48554D01UL);
    }
}




