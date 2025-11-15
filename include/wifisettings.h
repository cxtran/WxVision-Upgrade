#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>  
#include <SPIFFS.h>
#include <ArduinoJson.h>    
#include "settings.h"
#include "utils.h"
#include "display.h"
#include "buzzer.h"
#include "menu.h"
#include "config.h"

void scanAndSelectWiFi();
void selectWiFiNetwork(int delta);  
void confirmWiFiSelection();
void cancelWiFiSelection();
void connectToWiFi();
int scanWiFiNetworks();

bool startAccessPoint(const char *ssidOverride = nullptr, const char *passOverride = nullptr);
void stopAccessPoint();
bool isAccessPointActive();
IPAddress getAccessPointIP();
String getAccessPointSSID();

