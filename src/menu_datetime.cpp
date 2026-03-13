#include <Arduino.h>
#include <RTClib.h>
#include <cstring>
#include <cstdlib>

#include "menu.h"
#include "display.h"
#include "settings.h"
#include "datetimesettings.h"
#include "notifications.h"
#include "units.h"

extern int dtYear, dtMonth, dtDay, dtHour, dtMinute, dtSecond;
extern int dtTimezoneIndex;
extern int dtManualOffset;
extern int dtFmt24;
extern int dtDateFmt;
extern int dtNtpPreset;
extern int dtAutoDst;
extern int unitTempSel, unitPressSel, unitClockSel, unitWindSel, unitPrecipSel;

void showDateTimeModal()
{
    if (currentMenuLevel != MENU_NONE)
        pushMenu(currentMenuLevel);
    currentMenuLevel = MENU_SYSDATETIME;
    menuActive = true;

    DateTime now;
    if (rtcReady)
        now = utcToLocal(rtc.now());
    else if (!getLocalDateTime(now))
        now = DateTime(2000, 1, 1, 0, 0, 0);

    dtYear = now.year();
    dtMonth = now.month();
    dtDay = now.day();
    dtHour = now.hour();
    dtMinute = now.minute();
    dtSecond = now.second();

    size_t tzCount = timezoneCount();
    if (tzCount > 31)
        tzCount = 31;

    int currentIndex = timezoneCurrentIndex();
    dtManualOffset = tzStandardOffset;
    dtAutoDst = tzAutoDst ? 1 : 0;
    if (currentIndex >= 0 && currentIndex < static_cast<int>(tzCount))
    {
        dtTimezoneIndex = currentIndex;
        dtManualOffset = timezoneInfoAt(currentIndex).offsetMinutes;
    }
    else
    {
        dtTimezoneIndex = static_cast<int>(tzCount);
        dtAutoDst = 0;
    }
    dtManualOffset = constrain(dtManualOffset, -720, 840);

    dtFmt24 = (fmt24 < 0 || fmt24 > 1) ? 1 : fmt24;
    dtDateFmt = (dateFmt < 0 || dateFmt > 2) ? 0 : dateFmt;

    static char ntpServerBuf[64];
    dtNtpPreset = ntpServerPreset;
    if (dtNtpPreset < 0 || dtNtpPreset > NTP_PRESET_CUSTOM)
        dtNtpPreset = NTP_PRESET_CUSTOM;

    if (dtNtpPreset == NTP_PRESET_CUSTOM)
        strncpy(ntpServerBuf, ntpServerHost, sizeof(ntpServerBuf) - 1);
    else
        strncpy(ntpServerBuf, ntpPresetHost(dtNtpPreset), sizeof(ntpServerBuf) - 1);
    ntpServerBuf[sizeof(ntpServerBuf) - 1] = '\0';

    static const char *const ntpPresetOptions[] = {
        ntpPresetHost(0), ntpPresetHost(1), ntpPresetHost(2), "Custom"};

    static const char *timezoneOptions[32];
    size_t tzOptCount = tzCount;
    for (size_t i = 0; i < tzOptCount; ++i)
    {
        timezoneOptions[i] = timezoneLabelAt(i);
    }
    timezoneOptions[tzOptCount] = "Custom Offset";
    int timezoneChooserCount = static_cast<int>(tzOptCount) + 1;

    static const char *fmt24Opts[] = {"12h", "24h"};
    static const char *dateFmtOpts[] = {"YYYY-MM-DD", "MM/DD/YYYY", "DD/MM/YYYY"};
    static const char *autoDstOpts[] = {"Off", "On"};

    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", dtHour, dtMinute, dtSecond);

    int activeOffset = timezoneOffsetForLocal(now);
    char offsetBuf[16];
    char offsetSign = (activeOffset >= 0) ? '+' : '-';
    int absOffset = abs(activeOffset);
    int offsetHours = absOffset / 60;
    int offsetMinutes = absOffset % 60;
    snprintf(offsetBuf, sizeof(offsetBuf), "UTC%c%02d:%02d", offsetSign, offsetHours, offsetMinutes);

    int currentIdx = timezoneCurrentIndex();
    bool dstActive = tzAutoDst && (activeOffset != tzStandardOffset);

    String tzShort = "Custom";
    if (currentIdx >= 0 && !timezoneIsCustom())
    {
        tzShort = timezoneInfoAt(static_cast<size_t>(currentIdx)).city;
    }

    String localSummary = "Local ";
    localSummary += timeBuf;
    localSummary += " (";
    localSummary += offsetBuf;
    if (tzAutoDst)
    {
        localSummary += dstActive ? " DST" : " Std";
    }
    else if (currentIdx >= 0 && timezoneSupportsDst(static_cast<size_t>(currentIdx)))
    {
        localSummary += " DST-off";
    }
    if (tzShort.length() > 0)
    {
        localSummary += ", ";
        localSummary += tzShort;
    }
    localSummary += ")";

    if (localSummary.length() > 44)
    {
        localSummary.remove(44);
        localSummary += "...";
    }

    String rtcNote = "RTC stores UTC; display applies TZ & DST";

    String lines[16];
    InfoFieldType types[16];
    int lineIdx = 0;

    lines[lineIdx] = localSummary;
    types[lineIdx++] = InfoLabel;

    lines[lineIdx] = "Timezone";
    types[lineIdx++] = InfoChooser;
    lines[lineIdx] = "Manual Offset (min)";
    types[lineIdx++] = InfoNumber;
    lines[lineIdx] = "Auto DST";
    types[lineIdx++] = InfoChooser;
    lines[lineIdx] = "Year";
    types[lineIdx++] = InfoNumber;
    lines[lineIdx] = "Month";
    types[lineIdx++] = InfoNumber;
    lines[lineIdx] = "Day";
    types[lineIdx++] = InfoNumber;
    lines[lineIdx] = "Hour";
    types[lineIdx++] = InfoNumber;
    lines[lineIdx] = "Minute";
    types[lineIdx++] = InfoNumber;
    lines[lineIdx] = "Second";
    types[lineIdx++] = InfoNumber;
    lines[lineIdx] = "Time Format";
    types[lineIdx++] = InfoChooser;
    lines[lineIdx] = "Date Format";
    types[lineIdx++] = InfoChooser;
    lines[lineIdx] = "NTP Preset";
    types[lineIdx++] = InfoChooser;
    lines[lineIdx] = "NTP Server";
    types[lineIdx++] = InfoText;
    lines[lineIdx] = "Sync NTP";
    types[lineIdx++] = InfoButton;
    lines[lineIdx] = rtcNote;
    types[lineIdx++] = InfoLabel;

    int *intRefs[] = {&dtManualOffset, &dtYear, &dtMonth, &dtDay, &dtHour,
                      &dtMinute, &dtSecond};
    int *chooserRefs[] = {&dtTimezoneIndex, &dtAutoDst, &dtFmt24, &dtDateFmt, &dtNtpPreset};
    const char *const *chooserOptPtrs[] = {timezoneOptions, autoDstOpts, fmt24Opts, dateFmtOpts, ntpPresetOptions};
    int chooserOptCounts[] = {timezoneChooserCount, 2, 2, 3, NTP_PRESET_CUSTOM + 1};
    char *textRefs[] = {ntpServerBuf};
    int textSizes[] = {64};

    dateModal.setLines(lines, types, lineIdx);
    dateModal.setValueRefs(intRefs, 7, chooserRefs, 5,
                           chooserOptPtrs, chooserOptCounts,
                           textRefs, 1, textSizes);
    dateModal.clearNumberFieldConfigs();
    NumberFieldConfig yearConfig;
    yearConfig.step = 1;
    yearConfig.minValue = 2020;
    yearConfig.maxValue = 2099;
    yearConfig.hasBounds = true;
    dateModal.setNumberFieldConfig(4, yearConfig);

    NumberFieldConfig monthConfig;
    monthConfig.step = 1;
    monthConfig.minValue = 1;
    monthConfig.maxValue = 12;
    monthConfig.hasBounds = true;
    monthConfig.wrap = true;
    monthConfig.accelerateOnHold = true;
    dateModal.setNumberFieldConfig(5, monthConfig);

    NumberFieldConfig dayConfig;
    dayConfig.step = 1;
    dayConfig.minValue = 1;
    dayConfig.maxValue = 31;
    dayConfig.hasBounds = true;
    dayConfig.wrap = true;
    dayConfig.accelerateOnHold = true;
    dayConfig.useDateDayRange = true;
    dateModal.setNumberFieldConfig(6, dayConfig);

    NumberFieldConfig hourConfig;
    hourConfig.step = 1;
    hourConfig.minValue = 0;
    hourConfig.maxValue = 23;
    hourConfig.hasBounds = true;
    hourConfig.wrap = true;
    hourConfig.accelerateOnHold = true;
    dateModal.setNumberFieldConfig(7, hourConfig);

    NumberFieldConfig minuteConfig;
    minuteConfig.step = 1;
    minuteConfig.minValue = 0;
    minuteConfig.maxValue = 59;
    minuteConfig.hasBounds = true;
    minuteConfig.wrap = true;
    minuteConfig.accelerateOnHold = true;
    dateModal.setNumberFieldConfig(8, minuteConfig);
    dateModal.setNumberFieldConfig(9, minuteConfig);
    dateModal.setShowNumberArrows(true);

    dateModal.setCallback([](bool accepted, int /*btnIdx*/) {
        int sel = dateModal.getSelIndex();
        constexpr int kSyncButtonIndex = 14;
        auto applyCurrentTimezoneSelectionForSync = []() {
            int tzCountInt = static_cast<int>(timezoneCount());
            if (tzCountInt > 31)
                tzCountInt = 31;

            dtManualOffset = constrain(dtManualOffset, -720, 840);
            dtTimezoneIndex = constrain(dtTimezoneIndex, 0, tzCountInt);
            bool useCustomTz = (dtTimezoneIndex == tzCountInt);

            if (useCustomTz)
            {
                setCustomTimezoneOffset(dtManualOffset);
                dtAutoDst = 0;
            }
            else
            {
                selectTimezoneByIndex(dtTimezoneIndex);
                dtManualOffset = tzStandardOffset;
                dtAutoDst = tzAutoDst ? 1 : 0;
            }
        };

        if (sel == kSyncButtonIndex)
        {
            applyCurrentTimezoneSelectionForSync();
            dateModal.hide();
            wxv::notify::showNotification(wxv::notify::NotifyId::NtpSync, myWHITE);
            String chosenHost;
            if (dtNtpPreset == NTP_PRESET_CUSTOM)
            {
                chosenHost = String(ntpServerBuf);
            }
            else
            {
                const char *presetHost = ntpPresetHost(dtNtpPreset);
                if (presetHost)
                    chosenHost = String(presetHost);
            }
            setNtpServerFromHostString(chosenHost);
            dtNtpPreset = ntpServerPreset;
            strncpy(ntpServerBuf, ntpServerHost, sizeof(ntpServerBuf) - 1);
            ntpServerBuf[sizeof(ntpServerBuf) - 1] = '\0';

            bool ok = syncTimeFromNTP();
            wxv::notify::showNotification(ok ? wxv::notify::NotifyId::NtpOk : wxv::notify::NotifyId::NtpFail,
                                          ok ? myGREEN : myRED);
            if (ok)
            {
                getTimeFromRTC();
                DateTime localNow;
                if (!getLocalDateTime(localNow))
                {
                    localNow = DateTime(2000, 1, 1, 0, 0, 0);
                }
                dtYear = localNow.year();
                dtMonth = localNow.month();
                dtDay = localNow.day();
                dtHour = localNow.hour();
                dtMinute = localNow.minute();
                dtSecond = localNow.second();
                reset_Time_and_Date_Display = true;
            }
            saveDateTimeSettings();
            pendingModalFn = showDateTimeModal;
            pendingModalTime = millis() + 1500;
            return;
        }

        clampDateTimeFields(dtYear, dtMonth, dtDay, dtHour, dtMinute, dtSecond);
        dtManualOffset = constrain(dtManualOffset, -720, 840);

        int tzCountInt = static_cast<int>(timezoneCount());
        if (tzCountInt > 31)
            tzCountInt = 31;
        dtTimezoneIndex = constrain(dtTimezoneIndex, 0, tzCountInt);
        bool useCustomTz = (dtTimezoneIndex == tzCountInt);

        if (useCustomTz)
        {
            setCustomTimezoneOffset(dtManualOffset);
            dtAutoDst = 0;
        }
        else
        {
            selectTimezoneByIndex(dtTimezoneIndex);
            dtManualOffset = tzStandardOffset;
            dtAutoDst = tzAutoDst ? 1 : 0;
        }

        DateTime manualLocal(dtYear, dtMonth, dtDay,
                             dtHour, dtMinute, dtSecond);
        int effectiveOffset = timezoneOffsetForLocal(manualLocal);
        DateTime manualUtc = localToUtc(manualLocal, effectiveOffset);

        if (!rtcReady)
            rtcReady = rtc.begin();
        if (rtcReady)
            rtc.adjust(manualUtc);
        else
            Serial.println("[RTC] Module not available; system clock updated only");

        setSystemTimeFromDateTime(manualUtc);
        updateTimezoneOffsetWithUtc(manualUtc);
        getTimeFromRTC();

        bool formatChanged = (fmt24 != dtFmt24);
        fmt24 = dtFmt24;
        units.clock24h = (fmt24 == 1);
        dateFmt = dtDateFmt;
        String hostSelection;
        if (dtNtpPreset == NTP_PRESET_CUSTOM)
        {
            hostSelection = String(ntpServerBuf);
        }
        else
        {
            const char *presetHost = ntpPresetHost(dtNtpPreset);
            if (presetHost)
                hostSelection = String(presetHost);
        }
        setNtpServerFromHostString(hostSelection);
        dtNtpPreset = ntpServerPreset;
        strncpy(ntpServerBuf, ntpServerHost, sizeof(ntpServerBuf) - 1);
        ntpServerBuf[sizeof(ntpServerBuf) - 1] = '\0';

        saveDateTimeSettings();
        saveAllSettings();

        reset_Time_and_Date_Display = true;
        if (formatChanged)
        {
            dma_display->clearScreen();
            drawClockScreen();
            displayDate();
            displayWeatherData();
        }

        dateModal.hide();
    });

    dateModal.show();
}

void showUnitSettingsModal()
{
    if (currentMenuLevel != MENU_NONE)
        pushMenu(currentMenuLevel);
    currentMenuLevel = MENU_SYSUNITS;
    menuActive = true;

    unitTempSel = (units.temp == TempUnit::F) ? 1 : 0;
    unitPressSel = (units.press == PressUnit::INHG) ? 1 : 0;
    unitClockSel = units.clock24h ? 1 : 0;
    switch (units.wind)
    {
    case WindUnit::MPH:
        unitWindSel = 1;
        break;
    case WindUnit::KTS:
        unitWindSel = 2;
        break;
    case WindUnit::KPH:
        unitWindSel = 3;
        break;
    default:
        unitWindSel = 0;
        break;
    }
    unitPrecipSel = (units.precip == PrecipUnit::INCH) ? 1 : 0;

    static const char *tempOpts[] = {"Celsius", "Fahrenheit"};
    static const char *pressOpts[] = {"hPa", "inHg"};
    static const char *clockOpts[] = {"12h", "24h"};
    static const char *windOpts[] = {"m/s", "mph", "knots", "km/h"};
    static const char *precipOpts[] = {"mm", "inches"};

    String labels[] = {"Temperature", "Pressure", "Clock Format", "Wind Speed", "Precipitation"};
    InfoFieldType types[] = {InfoChooser, InfoChooser, InfoChooser, InfoChooser, InfoChooser};
    int *chooserRefs[] = {&unitTempSel, &unitPressSel, &unitClockSel, &unitWindSel, &unitPrecipSel};
    const char *const *chooserOpts[] = {tempOpts, pressOpts, clockOpts, windOpts, precipOpts};
    int chooserCounts[] = {2, 2, 2, 4, 2};

    unitSettingsModal.setLines(labels, types, 5);
    unitSettingsModal.setValueRefs(nullptr, 0, chooserRefs, 5, chooserOpts, chooserCounts);

    unitSettingsModal.setCallback([](bool /*accepted*/, int) {
        unitTempSel = constrain(unitTempSel, 0, 1);
        unitPressSel = constrain(unitPressSel, 0, 1);
        unitClockSel = constrain(unitClockSel, 0, 1);
        unitWindSel = constrain(unitWindSel, 0, 3);
        unitPrecipSel = constrain(unitPrecipSel, 0, 1);

        uint16_t prevSig = unitSignature();
        int prevFmt24 = fmt24;

        units.temp = (unitTempSel == 1) ? TempUnit::F : TempUnit::C;
        units.press = (unitPressSel == 1) ? PressUnit::INHG : PressUnit::HPA;
        fmt24 = (unitClockSel == 1) ? 1 : 0;
        units.clock24h = (fmt24 == 1);
        switch (unitWindSel)
        {
        case 1:
            units.wind = WindUnit::MPH;
            break;
        case 2:
            units.wind = WindUnit::KTS;
            break;
        case 3:
            units.wind = WindUnit::KPH;
            break;
        default:
            units.wind = WindUnit::MPS;
            break;
        }
        units.precip = (unitPrecipSel == 1) ? PrecipUnit::INCH : PrecipUnit::MM;

        uint16_t newSig = unitSignature();

        applyUnitPreferences();
        saveUnits();
        saveDateTimeSettings();

        bool clockChanged = (prevFmt24 != fmt24);
        bool signatureChanged = (newSig != prevSig);

        if (signatureChanged)
        {
            displayWeatherData();
            requestScrollRebuild();
            serviceScrollRebuild();
            if (isDataSourceOwm())
            {
                fetchWeatherFromOWM();
            }
        }
        if (clockChanged)
        {
            reset_Time_and_Date_Display = true;
            displayClock();
            displayDate();
        }

        unitSettingsModal.hide();
        currentMenuLevel = MENU_SYSTEM;
    });

    unitSettingsModal.show();
}
