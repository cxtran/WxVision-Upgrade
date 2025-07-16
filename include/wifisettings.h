#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>  
#include <SPIFFS.h>
#include <ArduinoJson.h>    
#include "settings.h"
#include "utils.h"
#include "display.h"
#include "buzzer.h"
#include "wifi.h"
#include "menu.h"
#include <SPIFFS.h>

void scanAndSelectWiFi();
void selectWiFiNetwork(int delta);  
void confirmWiFiSelection();
void cancelWiFiSelection();
void connectToWiFi();
void scanWiFiNetworks();

