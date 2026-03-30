#include "app_state.h"

#include "display.h"
#include "display_runtime.h"

extern String owmCountryCode;
extern String deviceHostname;

AppState &appState()
{
    static AppState state{
        dayFormat,
        dataSource,
        autoRotate,
        manualScreen,
        autoRotateInterval,

        units,
        theme,
        brightness,
        scrollSpeed,
        verticalScrollSpeed,
        scrollLevel,
        verticalScrollLevel,
        returnToDefaultSec,
        splashDurationSec,
        autoBrightness,
        customMsg,

        wifiSSID,
        wifiPass,
        owmCity,
        owmApiKey,
        wfToken,
        wfStationId,
        owmCountryIndex,
        owmCountryCustom,
        owmCountryCode,

        tempOffset,
        humOffset,
        lightGain,
        buzzerVolume,
        buzzerToneSet,
        alarmSoundMode,

        deviceHostname,
        alarmEnabled,
        alarmHour,
        alarmMinute,
        alarmRepeatMode,
        alarmWeeklyDay,

        noaaAlertsEnabled,
        noaaLatitude,
        noaaLongitude,
        forecastLinesPerDay,
        forecastPauseMs,
        forecastIconSize,

        rtc,
        tzOffset,
        dateFmt,
        fmt24,
        ntpServerHost,

        scrollDelays,
        reset_Time_and_Date_Display,

        str_Weather_Conditions,
        str_Temp,
        str_Humd,
        chr_t_hour,
        chr_t_minute,
        chr_t_second,

        saveAllSettings,
        loadSettings,
        saveDateTimeSettings,
        syncTimeFromNTP};

    return state;
}
