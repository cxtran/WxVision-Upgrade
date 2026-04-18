#pragma once

#include <Arduino.h>
#include <RTClib.h>
#include "settings.h"
#include "datetimesettings.h"
#include "units.h"

struct AppState
{
    int &dayFormat;
    int &dataSource;
    int &autoRotate;
    int &manualScreen;
    int &autoRotateInterval;

    UnitPrefs &units;
    int &theme;
    int &brightness;
    int &scrollSpeed;
    int &verticalScrollSpeed;
    int &scrollLevel;
    int &verticalScrollLevel;
    int &returnToDefaultSec;
    int &splashDurationSec;
    bool &autoBrightness;
    String &customMsg;

    String &wifiSSID;
    String &wifiPass;
    String &owmCity;
    String &owmApiKey;
    String &wfToken;
    String &wfStationId;
    int &owmCountryIndex;
    String &owmCountryCustom;

    float &tempOffset;
    int &humOffset;
    int &lightGain;
    int &buzzerVolume;
    int &buzzerToneSet;
    int &alarmSoundMode;

    String &deviceHostname;
    bool (&alarmEnabled)[3];
    int (&alarmHour)[3];
    int (&alarmMinute)[3];
    AlarmRepeatMode (&alarmRepeatMode)[3];
    int (&alarmWeeklyDay)[3];

    bool &noaaAlertsEnabled;
    float &noaaLatitude;
    float &noaaLongitude;
    bool &debugMemoryLogs;
    int &forecastLinesPerDay;
    int &forecastPauseMs;
    int &forecastIconSize;

    RTC_DS3231 &rtc;
    int &tzOffset;
    int &dateFmt;
    int &fmt24;
    char (&ntpServerHost)[64];

    const int (&scrollDelays)[10];
    bool &reset_Time_and_Date_Display;

    String &str_Weather_Conditions;
    String &str_Temp;
    String &str_Humd;
    char (&chr_t_hour)[3];
    char (&chr_t_minute)[3];
    char (&chr_t_second)[3];

    void (*saveAllSettings)();
    void (*loadSettings)();
    void (*saveDateTimeSettings)();
    bool (*syncTimeFromNTP)();
};

AppState &appState();
