#pragma once
#include <Arduino.h>

struct AppApiResponse
{
    int statusCode = 200;
    const char *contentType = "application/json";
    String body;
};

void setupWebServer();      // Call this from setup()
void webTick();             // Call from loop() for deferred web actions
void broadcastAppSettingsUpdate(const char *section);
bool webServerIsRunning();
bool handleAppApiRequest(const String &method, const String &path, const String &requestBody, AppApiResponse &response);
String buildAppHelloMessage();
bool isLocalAppClientConnected();

// Flag set during OTA upload to pause normal rendering
extern bool otaInProgress;

// Trend logging (declared in datalogger.cpp)

