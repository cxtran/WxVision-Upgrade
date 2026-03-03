#include <Arduino.h>
#include <cmath>
#include <cstring>

#include "menu.h"
#include "display.h"
#include "settings.h"
#include "ui_theme.h"

void showDisplaySettingsModal()
{
    // Avoid stacking duplicate Display entries when rebuilding the modal after Theme Mode changes
    bool rebuildingDisplay = (currentMenuLevel == MENU_DISPLAY && preserveDisplayModeTemp);
    if (currentMenuLevel != MENU_NONE && !rebuildingDisplay)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_DISPLAY;
    menuActive = true;

    // Persist across rebuilds so we can re-open with the just-selected mode
    if (preserveDisplayModeTemp)
    {
        autoThemeModeTemp = constrain(cachedDisplayModeTemp, 0, 2);
        preserveDisplayModeTemp = false;
    }
    else
    {
        autoThemeModeTemp = autoThemeAmbient ? 2 : (autoThemeSchedule ? 1 : 0);
    }
    cachedDisplayModeTemp = autoThemeModeTemp;
    preserveDisplayModeTemp = false;
    static int dayThemeStartTemp;
    static int nightThemeStartTemp;
    static int lightThresholdTemp;
    dayThemeStartTemp = dayThemeStartMinutes;
    nightThemeStartTemp = nightThemeStartMinutes;
    lightThresholdTemp = autoThemeLightThreshold;
    static int autoBrightnessInt;
    autoBrightnessInt = autoBrightness ? 1 : 0;
    static int brightnessTemp = brightness;
    static int scrollLevelTemp = 3;
    static int vScrollLevelTemp = 3;
    for (int i = 0; i < 10; ++i)
    {
        if (scrollSpeed >= scrollDelays[i])
        {
            scrollLevelTemp = i;
            break;
        }
    }
    for (int i = 0; i < 10; ++i)
    {
        if (verticalScrollSpeed >= scrollDelays[i])
        {
            vScrollLevelTemp = i;
            break;
        }
    }

    static int autoRotateTemp;
    autoRotateTemp = autoRotate ? 1 : 0;

    static const int rotateIntervalValues[] = {5, 10, 15, 20, 30, 45, 60, 90, 120};
    static const char *rotateIntervalOpt[] = {"5 s", "10 s", "15 s", "20 s", "30 s", "45 s", "60 s", "90 s", "120 s"};
    const int rotateIntervalCount = sizeof(rotateIntervalValues) / sizeof(rotateIntervalValues[0]);

    static int rotateIntervalIndex = 0;
    rotateIntervalIndex = 0;
    int bestIntervalDiff = (rotateIntervalValues[0] > autoRotateInterval)
                               ? rotateIntervalValues[0] - autoRotateInterval
                               : autoRotateInterval - rotateIntervalValues[0];
    for (int i = 1; i < rotateIntervalCount; ++i)
    {
        int diff = (rotateIntervalValues[i] > autoRotateInterval)
                       ? rotateIntervalValues[i] - autoRotateInterval
                       : autoRotateInterval - rotateIntervalValues[i];
        if (diff < bestIntervalDiff)
        {
            bestIntervalDiff = diff;
            rotateIntervalIndex = i;
        }
    }

    float currentLux = readBrightnessSensor();
    static const char *themeOpts[] = {"Day", "Night"};
    static const char *themeModeOpts[] = {"Manual", "Scheduled", "Light Sensor"};
    static const char *autoOpts[] = {"Off", "On"};
    static const char *speedOpts[] = {"1 - Slow", "2", "3", "4", "5", "6", "7", "8", "9", "10 - Fast"};
    static const char *autoRotateOpt[] = {"Off", "On"};

    // Build dynamic lines based on theme mode selection
    const int MAX_LINES = 12;
    String labels[MAX_LINES];
    InfoFieldType types[MAX_LINES];
    int *numberRefs[MAX_LINES];
    int numberCount = 0;
    int *chooserRefs[MAX_LINES];
    const char *const *chooserOpts[MAX_LINES];
    int chooserCounts[MAX_LINES];
    int chooserCount = 0;
    int lineCount = 0;

    auto addChooserLine = [&](const String &label, int *ref, const char *const *opts, int count) {
        labels[lineCount] = label;
        types[lineCount] = InfoChooser;
        chooserRefs[chooserCount] = ref;
        chooserOpts[chooserCount] = opts;
        chooserCounts[chooserCount] = count;
        ++chooserCount;
        ++lineCount;
    };
    auto addNumberLine = [&](const String &label, int *ref) {
        labels[lineCount] = label;
        types[lineCount] = InfoNumber;
        numberRefs[numberCount] = ref;
        ++numberCount;
        ++lineCount;
    };

    // Place Theme Mode above Theme so users choose behavior before palette
    addChooserLine("Theme Mode", &autoThemeModeTemp, themeModeOpts, 3);
    addChooserLine("Theme", &theme, themeOpts, 2);

    if (autoThemeModeTemp == 1)
    {
        addNumberLine("Day Theme Start", &dayThemeStartTemp);
        addNumberLine("Night Theme Start", &nightThemeStartTemp);
    }
    else if (autoThemeModeTemp == 2)
    {
        String lightLabel = "Light Threshold (Lux)";
        if (!isnan(currentLux))
        {
            lightLabel += " [" + String(currentLux, 1) + " lx]";
        }
        addNumberLine(lightLabel, &lightThresholdTemp);
    }

    addChooserLine("Auto Brightness", &autoBrightnessInt, autoOpts, 2);
    addNumberLine("Brightness", &brightnessTemp);
    addChooserLine("Scroll Speed", &scrollLevelTemp, speedOpts, 10);
    addChooserLine("Vert Scroll", &vScrollLevelTemp, speedOpts, 10);
    addChooserLine("Auto Rotate", &autoRotateTemp, autoRotateOpt, 2);
    addChooserLine("Rotate Interval", &rotateIntervalIndex, rotateIntervalOpt, rotateIntervalCount);

    static char customMsgBuf[64];
    strncpy(customMsgBuf, customMsg.c_str(), sizeof(customMsgBuf));
    customMsgBuf[sizeof(customMsgBuf) - 1] = 0;
    labels[lineCount] = "Custom Msg";
    types[lineCount] = InfoText;
    char *textRefs[] = {customMsgBuf};
    int textSizes[] = {sizeof(customMsgBuf)};
    ++lineCount;

    displayModal.setLines(labels, types, lineCount);
    displayModal.setValueRefs(numberRefs, numberCount, chooserRefs, chooserCount, chooserOpts, chooserCounts, textRefs, 1, textSizes);
    displayModal.setShowNumberArrows(true); // show left/right arrows for numeric fields (e.g., Light Threshold)

    displayModal.setCallback([](bool /*accepted*/, int) {
        brightness = constrain(brightnessTemp, 1, 100);
        autoBrightness = (autoBrightnessInt > 0);
        scrollLevel = constrain(scrollLevelTemp, 0, 9);
        scrollSpeed = scrollDelays[scrollLevel];
        verticalScrollLevel = constrain(vScrollLevelTemp, 0, 9);
        verticalScrollSpeed = scrollDelays[verticalScrollLevel];
        setAutoRotateEnabled(autoRotateTemp > 0, true);
        setAutoRotateInterval(rotateIntervalValues[rotateIntervalIndex], true);
        customMsg = String(customMsgBuf);
        bool prevAutoTheme = autoThemeSchedule;
        bool prevAmbient = autoThemeAmbient;
        int prevLightThr = autoThemeLightThreshold;
        int prevDayStart = dayThemeStartMinutes;
        int prevNightStart = nightThemeStartMinutes;
        autoThemeSchedule = (autoThemeModeTemp == 1);
        autoThemeAmbient = (autoThemeModeTemp == 2);
        dayThemeStartMinutes = normalizeThemeScheduleMinutes(dayThemeStartTemp);
        nightThemeStartMinutes = normalizeThemeScheduleMinutes(nightThemeStartTemp);
        autoThemeLightThreshold = constrain(lightThresholdTemp, 1, 5000);
        bool scheduleChanged = (prevAutoTheme != autoThemeSchedule) ||
                               (prevDayStart != dayThemeStartMinutes) ||
                               (prevNightStart != nightThemeStartMinutes);
        bool ambientChanged = (prevAmbient != autoThemeAmbient) || (prevLightThr != autoThemeLightThreshold);
        saveDisplaySettings();
        if ((scheduleChanged && autoThemeSchedule) || (ambientChanged && autoThemeAmbient))
        {
            forceAutoThemeSchedule();
        }
        Serial.printf("[Saved] brightness=%d, scrollLevel=%d -> scrollSpeed=%d vScrollLevel=%d -> vScrollSpeed=%d autoBrightness=%d autoRotate=%d interval=%ds dayStart=%d nightStart=%d autoThemeMode=%d luxThr=%d\n",
                      brightness, scrollLevel + 1, scrollSpeed, verticalScrollLevel + 1, verticalScrollSpeed, autoBrightness, autoRotate, autoRotateInterval,
                      dayThemeStartMinutes, nightThemeStartMinutes, autoThemeModeTemp, autoThemeLightThreshold);
        displayModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    });
    displayModal.show();
}

void showCalibrationModal()
{
    if (currentMenuLevel != MENU_NONE)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_CALIBRATION;
    menuActive = true;

    bool displayInF = (units.temp == TempUnit::F);
    String tempLabel = displayInF ? "Temp Offset (F)" : "Temp Offset (C)";
    float displayOffset = static_cast<float>(dispTempOffset(tempOffset));
    static int tempOffsetDisplayTenths = 0;
    tempOffsetDisplayTenths = static_cast<int>(lroundf(displayOffset * 10.0f));

    // Bind directly to globals
    String labels[] = {tempLabel, "Hum Offset (%)", "Light Gain (%)"};
    InfoFieldType types[] = {InfoNumber, InfoNumber, InfoNumber};
    int *numberRefs[] = {&tempOffsetDisplayTenths, &humOffset, &lightGain};

    calibrationModal.setLines(labels, types, 3);
    calibrationModal.setValueRefs(numberRefs, 3, nullptr, 0, nullptr, nullptr);
    calibrationModal.setShowNumberArrows(true);

    // No final "OK" save. We autosave on each change in handleIR().
    calibrationModal.setCallback([](bool, int) {
        calibrationModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    });

    calibrationModal.show();
}
