#include <math.h>
#include <algorithm>

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
static float s_eqIndexValue = -1.0f;
static float s_iconBitmapEqValue = -1.0f;

static EnvBand s_detailsBandOverall = EnvBand::Unknown;
static EnvBand s_detailsBandEQI = EnvBand::Unknown;
static EnvBand s_detailsBandCO2 = EnvBand::Unknown;
static EnvBand s_detailsBandTemp = EnvBand::Unknown;
static EnvBand s_detailsBandHumidity = EnvBand::Unknown;
static EnvBand s_detailsBandBaro = EnvBand::Unknown;

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

    if (!dma_display)
    {
        s_iconBitmapValid = true;
        s_iconBitmapBand = band;
        s_iconBitmapEqValue = s_eqIndexValue;
        return;
    }

    float eqValue = s_eqIndexValue;
    if (eqValue < 0.0f)
        eqValue = 0.0f;
    if (eqValue > 100.0f)
        eqValue = 100.0f;
    const float eqNorm = eqValue / 100.0f;

    const int barX = 5;
    const int barWidth = 6;
    const int markerX = barX + barWidth;
    const int barTop = 1;
    const int barBottom = size - 2;
    const int barHeight = barBottom - barTop + 1;

    auto normalizedToPixels = [&](float norm) -> int {
        if (norm <= 0.0f)
            return 0;
        if (norm > 1.0f)
            norm = 1.0f;
        float displayNorm = log10f(1.0f + 9.0f * norm) / log10f(10.0f);
        int pix = static_cast<int>(roundf(displayNorm * barHeight));
        if (pix < 0)
            pix = 0;
        if (pix > barHeight)
            pix = barHeight;
        return pix;
    };

    int filledPixels = normalizedToPixels(eqNorm);

    struct Segment
    {
        float start;
        float end;
        EnvBand band;
    };
    const Segment segments[] = {
        {0.0f, 0.25f, EnvBand::Critical},
        {0.25f, 0.50f, EnvBand::Poor},
        {0.50f, 0.75f, EnvBand::Moderate},
        {0.75f, 1.0f, EnvBand::Good},
    };

    for (const Segment &seg : segments)
    {
        if (filledPixels <= 0)
            break;

        int segStart = normalizedToPixels(seg.start);
        int segEnd = normalizedToPixels(seg.end);
        int drawEnd = std::min(filledPixels, segEnd);
        if (drawEnd <= segStart)
            continue;

        uint16_t segColor = colorForBand(seg.band);
        for (int pix = segStart; pix < drawEnd; ++pix)
        {
            int y = barBottom - pix;
            for (int x = barX; x < barX + barWidth; ++x)
                setIconPixel(x, y, segColor);
        }
    }

    if (filledPixels > 0)
    {
        uint16_t indicatorColor = adjustColor(colorForBand(band), 40, 40, 40);
        int yIndicator = barBottom - (filledPixels - 1);
        setIconPixel(markerX, yIndicator, indicatorColor);
    }

    s_iconBitmapValid = true;
    s_iconBitmapBand = band;
    s_iconBitmapEqValue = s_eqIndexValue;
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
        tempString += (units.temp == TempUnit::F) ? "°F" : "°C";
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
    String display = "Details - ";
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

            uint16_t labelTokenColor;
            if (labelBand != EnvBand::Unknown)
            {
                labelTokenColor = colorForBand(labelBand);
                labelTokenColor = boostColor(labelTokenColor, labelBoost);
            }
            else
            {
                labelTokenColor = labelColor;
            }
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

        const bool monoTheme = (theme == 1);
        uint16_t valueColor = colorForBand(s_overallBand);
        if (selected)
        {
            valueColor = boostColor(valueColor, 30);
        }
        const uint8_t labelBoost = selected ? 35 : 20;
        uint16_t labelColor = boostColor(valueColor, labelBoost);

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
    s_eqIndexValue = eqIndex;

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
    updateDetailsBands(overallBand, co2Band, tempBand, humBand, pressBand);
    updateDetailsDisplay(detailsValue);
    String detailsLine = " ";
    lines[lineCount] = detailsLine;
    colors[lineCount] = colorForBand(overallBand);
    s_lineBands[lineCount] = overallBand;
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

    bool iconStatusChanged = (overallBand != previousOverall) || eqValueChanged;
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




