#pragma once
#include <ESPAsyncWebServer.h>

void setupWebServer();      // Call this from setup()
void webTick();             // Call from loop() for deferred web actions

// Flag set during OTA upload to pause normal rendering
extern bool otaInProgress;

// Trend logging (declared in datalogger.cpp)
void sensorLogToJson(class JsonDocument &doc);

