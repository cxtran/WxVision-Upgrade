#pragma once
#include <Arduino.h>
#include "InfoScreen.h"
#include "display.h"
#include "windmeter.h"

// ==============================
// ===== CONSTANTS & LIMITS =====
// ==============================
#define MAX_FORECAST_DAYS 7
#define MAX_FORECAST_HOURS 48    // you can lower to 24 if you prefer

// ==============================
// ===== DATA STRUCTURES ========
// ==============================

// ----------- Current Conditions Struct -----------
struct CurrentConditions {
    double temp = NAN;
    double feelsLike = NAN;
    double dewPoint = NAN;
    int    humidity = -1;
    double pressure = NAN;
    double windAvg = NAN;
    double windGust = NAN;
    double windDir = NAN;
    int    uv = -1;
    int    precipProb = -1;
    String cond;
    String icon;
    String windCardinal;
    uint32_t time = 0;
};

// ----------- Tempest UDP Data Structure -----------
struct TempestData {
    uint32_t epoch;
    double windLull;
    double windAvg;
    double windGust;
    double windDir;
    double windSampleInt;
    double pressure;
    double temperature;
    double humidity;
    double illuminance;
    double uv;
    double solar;
    double rain;
    int    precipType;
    int    strikeCount;
    double strikeDist;
    double battery;
    int    reportInt;
    String lastObsTime;
    unsigned long lastUpdate;
    double obsWindAvg = NAN;
    double obsWindDir = NAN;
    uint32_t obsEpoch = 0;
    unsigned long obsLastUpdate = 0;
    double rapidWindAvg = NAN;
    double rapidWindDir = NAN;
    uint32_t rapidEpoch = 0;
    unsigned long rapidLastUpdate = 0;
};

// ----------- WeatherFlow 7-Day Forecast Data -----------
struct ForecastDay {
    double highTemp = NAN;
    double lowTemp = NAN;
    int rainChance = -1;
    String conditions;
    String icon;
    uint32_t sunrise = 0;
    uint32_t sunset = 0;
    int dayNum = 0;
    int monthNum = 0;
};

// ----------- WeatherFlow Hourly Forecast Data -----------
struct ForecastHour {
    double temp = NAN;
    int rainChance = -1;
    String conditions;
    String icon;
    uint32_t time = 0;
};

struct ForecastData {
    ForecastDay  days[MAX_FORECAST_DAYS];
    int          numDays = 0;
    ForecastHour hours[MAX_FORECAST_HOURS];
    int          numHours = 0;
    bool         hourlyKeyPresent = false;  // for debugging/logging
    uint32_t     lastUpdate = 0;
};

// ==============================
// ===== GLOBAL INSTANCES =======
// ==============================
extern TempestData tempest;
extern ForecastData forecast;
extern InfoScreen rapidWindScreen;
extern InfoScreen currentCondScreen;
extern InfoScreen hourlyScreen;
extern CurrentConditions currentCond;

extern bool newTempestData;
extern bool newRapidWindData;

// ==============================
// ===== FUNCTION PROTOTYPES ====
// ==============================

// ---- Tempest UDP ----
void updateTempestFromUDP(const char* jsonStr);
void fetchTempestData();
String getTempestField(const char* field);

// ---- Forecast (split into 3 parts) ----
void updateCurrentConditionsFromJson(const String& jsonStr);
void updateDailyForecastFromJson(const String& jsonStr);
void updateHourlyForecastFromJson(const String& jsonStr);

// Back-compat wrapper (calls the 3 funcs above)
void updateForecastFromJson(const String& jsonStr);

// Pull and parse from API
void fetchForecastData();

// (kept for compatibility even if unused)
String getForecastField(const char* field);

// ---- Display Screens ----
void showUdpScreen();
void showForecastScreen();
void showRapidWindScreen();
void showHourlyForecastScreen();
void showCurrentConditionsScreen();
void showWindDirectionScreen();

// ---- Time formatting ----
String formatEpochTime(uint32_t epoch);

// ---- Helpers ----
void debugPrintJsonKeys(JSONVar obj);
String extractJsonArray(const String& json, const String& key);
String extractJsonObject(const String& src, const char* key);
void updateWindInfoScroll(bool resetPosition = false);
