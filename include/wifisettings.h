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

// --- BEGIN WIFI INDUSTRIAL REDESIGN ---
enum class WifiRunState : uint8_t
{
    WIFI_OFF = 0,
    WIFI_IDLE,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_DISCONNECTED,
    WIFI_PROVISIONING,
    WIFI_WAIT_RETRY
};

enum class WifiStatusCode : uint8_t
{
    CONNECTED = 0,
    CONNECTING,
    OFFLINE,
    AUTH_FAILED,
    ERROR
};

enum class WifiStatusReason : uint8_t
{
    NONE = 0,
    NO_CREDENTIALS,
    SSID_NOT_FOUND,
    ROUTER_DOWN,
    AUTH_FAILED,
    CONNECT_TIMEOUT,
    MANUAL_DISCONNECT,
    ERROR
};

void wifiInitStateMachine();
void wifiLoop(bool apActive);
void wifiStartBootConnect(bool apActive);
void wifiMarkManualConnect();
bool wifiHasCredentials();
bool wifiIsConnecting();
WifiRunState wifiGetRunState();
WifiStatusCode wifiGetStatusCode();
WifiStatusReason wifiGetStatusReason();
const char *wifiStatusCodeText();
const char *wifiStatusReasonText();

void wifiClearPinnedMetadata();

void serviceWiFiConnection();
bool isWiFiConnectionInProgress();
bool consumeWiFiConnectionFailure();
bool startBackgroundWifiReconnect(bool apActive);
bool wifiHadRecentBeaconTimeout(unsigned long windowMs);
// --- END WIFI INDUSTRIAL REDESIGN ---

