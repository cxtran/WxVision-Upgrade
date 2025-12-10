#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <RTClib.h>
#include "datetimesettings.h"
#include "units.h"   // <-- include here so others can use UnitPrefs & formatters

extern RTC_DS3231 rtc;

extern const int scrollDelays[10];

enum AlarmRepeatMode : uint8_t {
    ALARM_REPEAT_NONE = 0,
    ALARM_REPEAT_DAILY = 1,
    ALARM_REPEAT_WEEKLY = 2,
    ALARM_REPEAT_WEEKDAY = 3,
    ALARM_REPEAT_WEEKEND = 4
};

// --- Date/Time/Timezone Settings ---
extern int dstAuto; // 0 = off, 1 = auto
extern int timeZoneOffsetMinutes; // Time Zone offset in minutes, e.g. 420 for UTC+7
extern int dateFormat;            // 0 = YYYY-MM-DD, 1 = MM/DD/YYYY, 2 = DD/MM/YYYY
extern int timeFormat24h;         // 1 = 24-hour, 0 = 12-hour

enum DataSourceType : uint8_t {
    DATA_SOURCE_OWM = 0,
    DATA_SOURCE_WEATHERFLOW = 1,
    DATA_SOURCE_NONE = 2
};

// --- Device ---
extern int dayFormat;        // 0 = MM/DD/YYYY, 1 = DD/MM/YYYY
extern int dataSource;       // see DataSourceType
extern int autoRotate;       // 1=on, 0=off
extern int autoRotateInterval;  // seconds between auto rotations
extern int manualScreen;     // 0=Main,1=Weather,2=Forecast,3=Calib
extern String wifiSSID;
extern String wifiPass;
extern bool alarmEnabled[3];
extern int alarmHour[3];
extern int alarmMinute[3];
extern AlarmRepeatMode alarmRepeatMode[3];
extern int alarmWeeklyDay[3];
extern bool alarmOneShotPending[3];
extern bool noaaAlertsEnabled;
extern float noaaLatitude;
extern float noaaLongitude;

extern bool setupComplete;       // true once onboarding finished
extern bool initialSetupRequired; // true when device needs first-time setup

// --- Display ---
extern int theme;            // 0 = Day, 1 = Night
extern bool autoThemeSchedule;   // legacy scheduled mode flag
extern bool autoThemeAmbient;    // true when light-sensor driven theme is enabled
extern int autoThemeLightThreshold; // lux threshold for ambient switching
extern int dayThemeStartMinutes;
extern int nightThemeStartMinutes;
static constexpr int LIGHT_GAIN_MIN = 1;
static constexpr int LIGHT_GAIN_MAX = 300;
int normalizeThemeScheduleMinutes(int value);
extern int brightness;       // 1 - 100
extern int scrollSpeed;      // derived from level
extern int verticalScrollSpeed; // independent speed for vertical marquees
extern String customMsg;
extern int scrollLevel;      // <-- fixed name (was scrollingLevel)
extern int verticalScrollLevel;
extern bool autoBrightness;
extern int splashDurationSec; // minimum splash display time
extern bool themeRefreshPending;

// --- Weather ---
extern String owmCity;
extern String owmApiKey;
extern int owmCountryIndex;
extern String owmCountryCustom;
extern String wfToken;
extern String wfStationId;

// --- Calibration ---
extern float tempOffset;   // degrees C
extern int humOffset;    // %
extern int lightGain;    // %
extern int buzzerVolume;   // 0-100
extern int buzzerToneSet;  // 0 = Bright, 1 = Soft, 2 = Click, 3 = Chime, 4 = Pulse
extern int alarmSoundMode; // 0 = Tone, 1 = Fur Elise, 2 = Turkish March

void loadSettings();
void saveDeviceSettings();
void saveDisplaySettings();
void saveCalibrationSettings();
void saveAllSettings();
void saveWeatherSettings();
void saveAlarmSettings();
void saveNoaaSettings();

// --- UI helpers ---
void toggleDayFormat(int dir);
void toggleDataSource(int dir);
void setDataSource(int source);
bool isDataSourceOwm();
bool isDataSourceWeatherFlow();
bool isDataSourceNone();
void toggleAutoRotate(int dir);
void setAutoRotateEnabled(bool enabled, bool persist = true);
void setAutoRotateInterval(int seconds, bool persist = true);

void markSetupComplete(bool complete=true);

void toggleTheme(int dir);
void adjustBrightness(int dir);
void adjustScrollSpeed(int dir);
void adjustVerticalScrollSpeed(int dir);

void adjustTempOffset(int dir);
void adjustHumOffset(int dir);
void adjustLightGain(int dir);

void tickAutoThemeSchedule();
void tickAutoThemeAmbient(float lux, bool persist = true, bool force = false);
void forceAutoThemeSchedule();

