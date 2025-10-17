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

static EnvBand s_lineBands[5];
static int s_lineBandCount = 0;

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
        return "GOOD";
    case EnvBand::Moderate:
        return "FAIR";
    case EnvBand::Poor:
        return "POOR";
    case EnvBand::Critical:
        return "ALRT";
    default:
        return "N/A";
    }
}

static int roundToInt(double value)
{
    return (value >= 0.0) ? static_cast<int>(value + 0.5) : static_cast<int>(value - 0.5);
}

static void drawEnvQualityOverlay(int lineIndex, int y, bool selected)
{
    if (lineIndex < 0 || lineIndex >= s_lineBandCount)
        return;

    EnvBand band = s_lineBands[lineIndex];
    if (band == EnvBand::Unknown)
        return;

    const int iconX = InfoScreen::SCREEN_WIDTH - 8;
    uint16_t background = 0;
    if (theme == 1)
    {
        background = dma_display->color565(5, 5, 15);
    }

    drawEnvBandIcon(iconX, y, band, colorForBand(band), background, selected);
}

void showEnvironmentalQualityScreen()
{
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

    String lines[5];
    uint16_t colors[5];
    int lineCount = 0;

    String eqLine = "EQI: ";
    eqLine += (eqIndexInt >= 0) ? String(eqIndexInt) : String("--");
    eqLine += " ";
 //   eqLine += shortLabelForBand(overallBand);
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
        co2Line += String(SCD40_co2);
     //   co2Line += "ppm";
    }
    lines[lineCount] = co2Line;
    colors[lineCount] = colorForBand(co2Band);
    s_lineBands[lineCount] = co2Band;
    ++lineCount;

    double dispTempValue = NAN;
    if (tempBand != EnvBand::Unknown)
        dispTempValue = dispTemp(tempC);
    String tempLine = "Temp: ";
    if (tempBand == EnvBand::Unknown)
    {
        tempLine += "--";
    }
    else
    {
        tempLine += String(roundToInt(dispTempValue));
        tempLine += (units.temp == TempUnit::F) ? "°F" : "°C";
    }
    lines[lineCount] = tempLine;
    colors[lineCount] = colorForBand(tempBand);
    s_lineBands[lineCount] = tempBand;
    ++lineCount;

    int humRounded = (humBand == EnvBand::Unknown) ? 0 : roundToInt(humidity);
    if (humRounded < 0)
        humRounded = 0;
    if (humRounded > 100)
        humRounded = 100;
    String humLine = "Hum: ";
    if (humBand == EnvBand::Unknown)
    {
        humLine += "--";
    }
    else
    {
        humLine += String(humRounded);
        humLine += "%";
    }
    lines[lineCount] = humLine;
    colors[lineCount] = colorForBand(humBand);
    s_lineBands[lineCount] = humBand;
    ++lineCount;

    double dispPressValue = NAN;
    if (pressBand != EnvBand::Unknown)
        dispPressValue = dispPress(pressure);
    String pressLine = "Press: ";
    if (pressBand == EnvBand::Unknown)
    {
        pressLine += "--";
    }
    else if (units.press == PressUnit::INHG)
    {
        pressLine += String(dispPressValue, 2);
        pressLine += "inHg";
    }
    else
    {
        pressLine += String(roundToInt(dispPressValue));
        pressLine += "hPa";
    }
    lines[lineCount] = pressLine;
    colors[lineCount] = colorForBand(pressBand);
    s_lineBands[lineCount] = pressBand;
    ++lineCount;

    s_lineBandCount = lineCount;
    for (int i = lineCount; i < 5; ++i)
    {
        s_lineBands[i] = EnvBand::Unknown;
    }

    envQualityScreen.setHighlightEnabled(false);
    envQualityScreen.setLineOverlay(drawEnvQualityOverlay);

    bool wasActive = envQualityScreen.isActive();
    envQualityScreen.setLines(lines, lineCount, !wasActive, colors);
    if (!wasActive)
    {
        envQualityScreen.show([]() { currentScreen = homeScreenForDataSource(); });
    }
}
