#pragma once
#include <Arduino.h>
#include "display.h"
#include "InfoModal.h"
#include "InfoScreen.h"
// ---- Data structure to store latest Tempest values ----
struct TempestData {
    float temperature = NAN;     // °C
    float humidity = NAN;        // %
    float pressure = NAN;        // hPa
    float windSpeed = NAN;       // m/s
    float windDir = NAN;         // deg
    float uv = NAN;              // UV index
    float solar = NAN;           // W/m²
    float rain = NAN;            // mm
    float battery = NAN;         // V
    String lastObsTime = "";     // ISO8601 or raw
    unsigned long lastUpdate = 0; // millis()
};

// ---- Data structure for forecast ----
struct ForecastData {
    float highTemp = NAN;    // High temperature (°C)
    float lowTemp = NAN;     // Low temperature (°C)
    int rainChance = -1;     // %
    float wind = NAN;        // m/s
    float humidity = NAN;    // %
    String summary = "";     // Forecast summary
    String day = "";         // "Mon", "Tue", etc
    unsigned long lastUpdate = 0;
};

// ---- Global objects ----
extern TempestData tempest;
extern ForecastData forecast;

// ---- UDP/HTTP update and display utilities ----
void updateTempestFromUDP(const char* jsonStr);
String getTempestField(const char* field);

void updateForecastFromJson(const String& jsonStr);
void fetchTempestData();    // Call in loop() to poll UDP and update tempest
void fetchForecastData();   // Call to fetch and update forecast via HTTP
void showForecastScreen();
void showUdpScreen();