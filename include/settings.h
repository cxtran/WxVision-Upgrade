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

enum DataSourceType : uint8_t {
    DATA_SOURCE_OWM = 0,
    DATA_SOURCE_WEATHERFLOW = 1,
    DATA_SOURCE_NONE = 2,
    DATA_SOURCE_OPEN_METEO = 3
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
extern bool sceneClockEnabled;
extern int returnToDefaultSec; // 0 disables auto-return to the default screen
extern int splashDurationSec; // minimum splash display time
extern bool themeRefreshPending;
// --- Forecast UI (WeatherFlow) ---
extern int forecastLinesPerDay;   // 2 or 3
extern int forecastPauseMs;       // pause at header per block
extern int forecastIconSize;      // 0=off, 16=16x16 icons

// --- Weather ---
extern String owmCity;
extern String owmApiKey;
extern int owmCountryIndex;
extern String owmCountryCustom;
extern String wfToken;
extern String wfStationId;

// --- MQTT ---
extern bool mqttEnabled;
extern bool mqttPublishTemp;
extern bool mqttPublishHumidity;
extern bool mqttPublishCO2;
extern bool mqttPublishPressure;
extern bool mqttPublishLight;
extern String mqttHost;
extern uint16_t mqttPort;
extern String mqttUser;
extern String mqttPass;
extern String mqttDeviceId;

// --- Cloud ---
extern bool cloudEnabled;
extern String cloudApiBaseUrl;
extern String cloudRelayUrl;
extern uint32_t cloudHeartbeatIntervalMs;
extern uint32_t cloudReconnectInitialMs;
extern uint32_t cloudReconnectMaxMs;

// --- Calibration ---
extern float tempOffset;   // degrees C
extern int humOffset;    // %
extern int lightGain;    // %
extern int envAlertCo2Threshold;
extern float envAlertTempThresholdC;
extern int envAlertHumidityLowThreshold;
extern int envAlertHumidityHighThreshold;
extern bool envAlertCo2Enabled;
extern bool envAlertTempEnabled;
extern bool envAlertHumidityEnabled;
extern int buzzerVolume;   // 0-100
extern int buzzerToneSet;  // 0 = Bright, 1 = Soft, 2 = Click, 3 = Chime, 4 = Pulse, 5 = Warm, 6 = Melody (ADSR)
extern int alarmSoundMode; // 0 = Tone, 1 = Fur Elise, 2 = Swan Lake, 3 = Turkish March, 4 = Moonlight Sonata

void loadSettings();
void saveDeviceSettings();
void saveDisplaySettings();
void saveCalibrationSettings();
void saveAllSettings();
void saveWeatherSettings();
void saveAlarmSettings();
void saveNoaaSettings();
void saveMqttSettings();
void saveCloudSettings();

// --- UI helpers ---
void toggleDayFormat(int dir);
void toggleDataSource(int dir);
void setDataSource(int source);
bool isDataSourceOwm();
bool isDataSourceWeatherFlow();
bool isDataSourceOpenMeteo();
bool isDataSourceForecastModel();
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
