#pragma once
#include <Arduino.h>
#include "InfoScreen.h"
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
};

// ----------- WeatherFlow Better Forecast Data -----------
struct ForecastData {
    String day;
    double highTemp;
    double lowTemp;
    int    rainChance;
    double wind;
    double humidity;
    String summary;
    unsigned long lastUpdate;
};

// ----------- Global Instances -----------
extern TempestData tempest;
extern ForecastData forecast;
extern InfoScreen rapidWindScreen;

extern bool newTempestData;
extern bool newRapidWindData;

// ----------- API Functions -----------

// Parse Tempest UDP JSON packet
void updateTempestFromUDP(const char* jsonStr);
// Poll and process any UDP data
void fetchTempestData();
// Get field as printable string (for InfoScreen or logging)
String getTempestField(const char* field);

// Parse WeatherFlow forecast JSON
void updateForecastFromJson(const String& jsonStr);
// Poll and process forecast from WeatherFlow API
void fetchForecastData();
// Get forecast field as printable string
String getForecastField(const char* field);

// Fill and show InfoScreen with Tempest data
void showUdpScreen();
// Fill and show InfoScreen with forecast data
void showForecastScreen();

void showRapidWindScreen();

String formatEpochTime(uint32_t epoch);
