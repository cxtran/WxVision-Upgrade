#pragma once
#include <ESPAsyncWebServer.h>

void setupWebServer();      // Call this from setup()
void webTick();             // Call from loop() for deferred web actions
void broadcastAppSettingsUpdate(const char *section);

// Flag set during OTA upload to pause normal rendering
extern bool otaInProgress;

// Trend logging (declared in datalogger.cpp)

