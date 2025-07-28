#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <RTClib.h>
#include "datetimesettings.h"
extern RTC_DS3231 rtc;

extern const int scrollDelays[10];
// --- Date/Time/Timezone Settings ---
extern int dstAuto; // 0 = off, 1 = auto
extern int timeZoneOffsetMinutes; // Time Zone offset in minutes, e.g. 420 for UTC+7
extern int dateFormat;            // 0 = YYYY-MM-DD, 1 = MM/DD/YYYY, 2 = DD/MM/YYYY
extern int timeFormat24h;         // 1 = 24-hour, 0 = 12-hour


// --- Device ---
extern int units;            // 0 = F+mph, 1 = C+m/s
extern int dayFormat;        // 0 = MM/DD/YYYY, 1 = DD/MM/YYYY
extern int forecastSrc;      // 0 = OpenWeather, 1 = WeatherFlow
extern int autoRotate;       // 1=on, 0=off
extern int manualScreen;     // 0=Main,1=Weather,2=Forecast,3=Calib
extern String wifiSSID;
extern String wifiPass;

// --- Display ---
extern int theme;            // 0 = Color, 1 = Monochrome
extern int brightness;       // 1 - 100
extern int scrollSpeed;      // 1-5
extern String customMsg;
extern int scrollingLevel;
extern bool autoBrightness;

// --- Weather ---
extern String owmCity;
extern String owmApiKey;
extern int owmCountryIndex;
extern String owmCountryCustom;
extern String wfToken;
extern String wfStationId;

// --- Calibration ---
extern int tempOffset;   // degrees
extern int humOffset;    // %
extern int lightGain;    // %





void loadSettings();
void saveDeviceSettings();
void saveDisplaySettings();
void saveCalibrationSettings();
void saveAllSettings();
void saveWeatherSettings();


void toggleUnits(int dir);
void toggleDayFormat(int dir);
void toggleForecastSrc(int dir);
void toggleAutoRotate(int dir);

void toggleTheme(int dir);
void adjustBrightness(int dir);
void adjustScrollSpeed(int dir);

void adjustTempOffset(int dir);
void adjustHumOffset(int dir);
void adjustLightGain(int dir);

