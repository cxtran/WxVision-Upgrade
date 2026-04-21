#include <Arduino.h>

#include "display.h"
#include "display_scene.h"
#include "display_runtime.h"
#include "settings.h"
#include "units.h"
#include "alarm.h"
#include "worldtime.h"
#include "sensors.h"
#include "display_widgets.h"
#include "display_worldtime.h"
#include "env_quality.h"
#include "noaa.h"
#include "fonts/verdanab8pt7b.h"

static String formatIndoorHumidity()
{
    float humiditySource = SCD40_hum;

    if (!isnan(humiditySource))
    {
        float calibrated = humiditySource + static_cast<float>(humOffset);
        if (calibrated < 0.0f)
            calibrated = 0.0f;
        if (calibrated > 100.0f)
            calibrated = 100.0f;
        int rounded = static_cast<int>(calibrated + 0.5f);
        if (rounded < 0)
            rounded = 0;
        if (rounded > 100)
            rounded = 100;
        return String(rounded);
    }

    if (str_Humd.length() > 0 && str_Humd != "--")
        return str_Humd;

    return String("--");
}

static int envScoreForBand(EnvBand band)
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

static EnvBand envBandFromIndex(float idx)
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

static EnvBand envBandFromCo2(float co2)
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

static EnvBand envBandFromTemp(float tempC)
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

static EnvBand envBandFromHumidity(float humidity)
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

static EnvBand envBandFromPressure(float pressure)
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

static uint16_t envColorForBand(EnvBand band)
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

void drawClockTimeLine(const DateTime &now, bool alarmActive)
{
    int hour = now.hour();
    int minute = now.minute();

    bool isPM = false;
    if (!units.clock24h)
    {
        if (hour == 0)
            hour = 12;
        else if (hour >= 12)
        {
            if (hour > 12)
                hour -= 12;
            isPM = true;
        }
    }

    char timeStr[6];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hour, minute);
    bool showTimeDigits = !alarmActive || isAlarmFlashVisible();

    dma_display->setFont(&verdanab8pt7b);
    dma_display->setTextSize(1);
    uint16_t timeColor = (theme == 1) ? dma_display->color565(60, 60, 120)
                                      : dma_display->color565(255, 255, 80);
    if (alarmActive)
        timeColor = dma_display->color565(255, 80, 80);
    dma_display->setTextColor(timeColor);

    int timeW = getTextWidth(timeStr);
    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
    int timeH = h;

    int ampmW = 0;
    if (!units.clock24h)
        ampmW = getTextWidth(isPM ? "PM" : "AM");
    int totalW = timeW + (ampmW ? ampmW + 2 : 0);
    int boxX = (64 - totalW) / 2;

    if (units.clock24h)
        boxX -= 3;

    if (boxX < 0)
        boxX = 0;

    int boxY = (32 - timeH) / 2;

    if (showTimeDigits)
    {
        dma_display->setCursor(boxX, boxY + timeH - 1);
        dma_display->print(timeStr);

        if (!units.clock24h)
        {
            String ampmStr = isPM ? "PM" : "AM";
            dma_display->setFont(&Font5x7Uts);
            dma_display->setTextSize(1);

            dma_display->getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
            int digitH = h;
            dma_display->getTextBounds(ampmStr.c_str(), 0, 0, &x1, &y1, &w, &h);
            int ampmWidth = w;
            int ampmH = h;
            int ampmX = 64 - ampmWidth - 1;
            int ampmY = boxY + digitH - (digitH - ampmH) - 1;
            ampmY -= 1;

            uint16_t ampmColor, bgColor;
            if (theme == 1)
            {
                ampmColor = dma_display->color565(100, 100, 140);
                bgColor = dma_display->color565(20, 20, 40);
            }
            else
            {
                if (isPM)
                {
                    ampmColor = dma_display->color565(255, 170, 60);
                    bgColor = dma_display->color565(50, 30, 0);
                }
                else
                {
                    ampmColor = dma_display->color565(100, 200, 255);
                    bgColor = dma_display->color565(10, 30, 50);
                }
            }

            dma_display->setTextColor(ampmColor);
            dma_display->fillRect(ampmX - 1, ampmY - ampmH + 6, ampmWidth + 2, ampmH + 2, bgColor);
            dma_display->setCursor(ampmX, ampmY);
            dma_display->print(ampmStr);
        }
    }
}

void drawClockDateLine(const DateTime &now)
{
    const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char *dayStr = days[now.dayOfTheWeek()];
    char dateSuffix[10];
    snprintf(dateSuffix, sizeof(dateSuffix), " %02d/%02d", now.month(), now.day());
    char dateStr[14];
    snprintf(dateStr, sizeof(dateStr), "%s%s", dayStr, dateSuffix);

    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    uint16_t dateColor = (theme == 1) ? dma_display->color565(60, 60, 120)
                                      : dma_display->color565(150, 200, 255);
    uint16_t sundayColor = (theme == 1) ? dma_display->color565(180, 80, 120)
                                        : dma_display->color565(255, 80, 120);
    uint16_t saturdayColor = (theme == 1) ? dma_display->color565(80, 140, 200)
                                          : dma_display->color565(80, 180, 255);

    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
    int dateX = (64 - static_cast<int>(w)) / 2;
    int dateY = 25;
    dma_display->setCursor(dateX, dateY);

    if (now.dayOfTheWeek() == 0)
    {
        dma_display->setTextColor(sundayColor);
        dma_display->print(dayStr);
        dma_display->setTextColor(dateColor);
        dma_display->print(dateSuffix);
    }
    else if (now.dayOfTheWeek() == 6)
    {
        dma_display->setTextColor(saturdayColor);
        dma_display->print(dayStr);
        dma_display->setTextColor(dateColor);
        dma_display->print(dateSuffix);
    }
    else
    {
        dma_display->setTextColor(dateColor);
        dma_display->print(dateStr);
    }
}

void drawClockPulseDot(int second)
{
    uint16_t pulseColor;
    if (noaaHasUnreadAlert())
    {
        pulseColor = (second % 2 == 0)
                         ? dma_display->color565(255, 60, 60)
                         : dma_display->color565(110, 0, 0);
    }
    else
    {
        pulseColor = (second % 2 == 0)
                         ? dma_display->color565(0, 150, 0)
                         : dma_display->color565(0, 60, 0);
    }
    dma_display->fillCircle(62, 30, 1, pulseColor);
}

void drawClockScreen()
{
    dma_display->fillScreen(0);

    DateTime systemNow;
    if (rtcReady)
    {
        DateTime utcNow = rtc.now();
        int offsetMinutes = timezoneIsCustom() ? tzStandardOffset : timezoneOffsetForUtc(utcNow);
        systemNow = utcToLocal(utcNow, offsetMinutes);
        updateTimezoneOffsetWithUtc(utcNow);
    }
    else if (!getLocalDateTime(systemNow))
    {
        systemNow = DateTime(2000, 1, 1, 0, 0, 0);
    }
    tickAlarmState(systemNow);

    DateTime now = systemNow;
    bool worldView = false;
    if (worldTimeIsWorldView())
    {
        DateTime worldNow;
        if (worldTimeGetCurrentDateTime(worldNow))
        {
            now = worldNow;
            worldView = true;
        }
    }

    int second = now.second();
    bool alarmActive = isAlarmCurrentlyActive();
    drawClockTimeLine(now, alarmActive);
    int wifiX = 57;
    int wifiY = 7;
    int alarmX = units.clock24h ? wifiX : 51;
    int alarmY = units.clock24h ? (wifiY + 8) : 8;

    if (WiFi.status() == WL_CONNECTED)
    {
        uint16_t wifiDim = (theme == 1)
                               ? dma_display->color565(35, 35, 60)
                               : dma_display->color565(80, 80, 80);
        uint16_t wifiActive = (theme == 1)
                                  ? dma_display->color565(90, 140, 200)
                                  : dma_display->color565(100, 255, 120);
        drawWiFiIcon(wifiX, wifiY, wifiDim, wifiActive, WiFi.RSSI());
    }
    if (isAnyAlarmEnabled() || alarmActive)
    {
        uint16_t alarmColor = alarmActive
                                  ? dma_display->color565(255, 80, 80)
                                  : ((theme == 1) ? dma_display->color565(120, 120, 180)
                                                  : dma_display->color565(255, 255, 120));
        drawAlarmIcon(alarmX, alarmY, alarmColor);
    }
    drawClockDateLine(now);

    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    uint16_t tempColor = (theme == 1) ? dma_display->color565(60, 60, 120)
                                      : dma_display->color565(200, 200, 255);
    dma_display->setTextColor(tempColor);
    int16_t x1, y1;
    uint16_t w, h;
    String outdoorTempStr = formatOutdoorTemperature();
    bool showOutdoor = !isDataSourceNone() && outdoorTempStr != "--";
    String indoorHumidityStr = formatIndoorHumidity();
    bool showIndoorHumidity = isDataSourceNone() && indoorHumidityStr != "--";
    float indoorTempC = NAN;
    if (!isnan(SCD40_temp))
        indoorTempC = SCD40_temp + tempOffset;
    String localTempStr = fmtTemp(indoorTempC, 0);

    if (worldView)
    {
        worldTimeHeaderSync(true);
    }
    else
    {
        worldTimeHeaderSync(false);
        dma_display->fillRect(0, 0, 64, 7, myBLACK);
        dma_display->fillRect(0, 0, 32, 7, myBLACK);

        if (showOutdoor)
        {
            const int iconWidth = 7;
            const int padding = 1;
            int sunX = 0;
            int sunY = 0;
            uint16_t sunColor = (theme == 1)
                                    ? dma_display->color565(100, 100, 140)
                                    : dma_display->color565(255, 200, 60);
            drawSunIcon(sunX, sunY, sunColor);

            int tempX = sunX + iconWidth + padding;
            dma_display->setCursor(tempX, 0);
            dma_display->print(outdoorTempStr);
        }
        else if (showIndoorHumidity)
        {
            String humidityDisplay = indoorHumidityStr + "%";
            const int iconWidth = 7;
            const int padding = 1;
            int dropX = 0;
            int dropY = 0;
            uint16_t dropColor = (theme == 1)
                                     ? dma_display->color565(100, 100, 160)
                                     : dma_display->color565(100, 200, 255);
            drawHumidityIcon(dropX, dropY, dropColor);

            int humidityX = dropX + iconWidth + padding;
            dma_display->setCursor(humidityX, 0);
            dma_display->print(humidityDisplay);
        }
    }

    if (!worldView)
    {
        dma_display->getTextBounds(localTempStr.c_str(), 0, 0, &x1, &y1, &w, &h);
        int localX = 64 - w;
        int localY = 0;
        int houseX = localX - 9;
        int houseY = 0;
        uint16_t houseColor = (theme == 1)
                                  ? dma_display->color565(100, 100, 140)
                                  : dma_display->color565(100, 180, 255);
        drawHouseIcon(houseX, houseY, houseColor);

        dma_display->setCursor(localX, localY);
        dma_display->print(localTempStr);
    }

    const int dotRadius = 1;
    const int dotDiameter = dotRadius * 2 + 1;
    const int co2DotX = 2;
    const int eqDotX = co2DotX;
    const int eqDotY = 30;
    const int co2DotY = eqDotY - (dotDiameter + 1);
    const int clearTop = eqDotY - dotRadius;
    const int clearHeight = (eqDotY + dotRadius) - clearTop + 1;
    dma_display->fillRect(co2DotX - dotRadius - 1, clearTop, dotDiameter + 2, clearHeight, myBLACK);

    float co2Raw = (SCD40_co2 > 0) ? static_cast<float>(SCD40_co2) : NAN;
    float humiditySource = SCD40_hum;
    if (!isnan(humiditySource))
    {
        humiditySource += static_cast<float>(humOffset);
        if (humiditySource < 0.0f)
            humiditySource = 0.0f;
        else if (humiditySource > 100.0f)
            humiditySource = 100.0f;
    }
    float pressure = (!isnan(bmp280_pressure) && bmp280_pressure > 200.0f) ? bmp280_pressure : NAN;

    EnvBand co2Band = envBandFromCo2(co2Raw);
    EnvBand tempBand = envBandFromTemp(indoorTempC);
    EnvBand humidityBand = envBandFromHumidity(humiditySource);
    EnvBand pressureBand = envBandFromPressure(pressure);

    EnvBand bands[] = {co2Band, tempBand, humidityBand, pressureBand};
    int scoreSum = 0;
    int validCount = 0;
    for (EnvBand band : bands)
    {
        int score = envScoreForBand(band);
        if (score >= 0)
        {
            scoreSum += score;
            ++validCount;
        }
    }

    float eqIndex = (validCount > 0) ? (static_cast<float>(scoreSum) / (validCount * 3.0f)) * 100.0f : -1.0f;
    EnvBand eqBand = (validCount > 0) ? envBandFromIndex(eqIndex) : EnvBand::Unknown;

    auto intensityForBand = [&](EnvBand band) -> float {
        if (band == EnvBand::Critical)
            return (second % 2 == 0) ? 1.0f : 0.35f;
        if (band == EnvBand::Poor)
            return ((second / 2) % 2 == 0) ? 1.0f : 0.6f;
        return 1.0f;
    };

    uint16_t eqPulseColor = scaleColor565(envColorForBand(eqBand), intensityForBand(eqBand));
    uint16_t co2PulseColor = scaleColor565(envColorForBand(co2Band), intensityForBand(co2Band));

    dma_display->fillCircle(eqDotX, eqDotY, dotRadius, eqPulseColor);
    dma_display->fillCircle(co2DotX, co2DotY, dotRadius, co2PulseColor);

    drawClockPulseDot(second);
}
