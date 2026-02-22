
#include <WiFi.h>
#include "wifisettings.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "display.h"
#include "settings.h"
#include "utils.h"
#include "menu.h"
#include <Preferences.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <Update.h>

#include <WiFiUdp.h>
// =======================
// WiFi SCAN/SELECT logic
// =======================

extern bool wifiSelecting;
extern int wifiScanCount;
int wifiScanIndex = 0;
String wifiScanSSIDs[20];
String wifiScanEncr[20];
String wifiScanRSSI[20];
extern int menuScroll;
extern bool menuActive;
extern int wifiSelectIndex;

extern String wifiSSID;
extern String wifiPass;
extern String owmCity;
extern String owmApiKey;
extern String wfToken;
extern String wfStationId;
extern int humOffset;

static bool apModeActive = false;
static IPAddress apIp(0, 0, 0, 0);
static String apSsid;

// --- BEGIN NEW CODE ---
static bool wifiConnectInProgress = false;
static bool wifiConnectShowDisplay = false;
static bool wifiConnectFailedEvent = false;
static bool wifiConnectBackground = false;
static unsigned long wifiConnectStartMs = 0;
static unsigned long wifiConnectStatusDotMs = 0;
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL;
static uint8_t preferredBssid[6] = {0};
static bool preferredBssidValid = false;
static int32_t preferredChannel = 0;

static bool scanBestSsidCandidate(const String &targetSsid, uint8_t *outBssid, int32_t *outChannel, int32_t *outRssi)
{
    if (targetSsid.isEmpty())
        return false;

    wifi_mode_t previousMode = WiFi.getMode();
    wifi_mode_t scanMode = previousMode;
    if (scanMode == WIFI_OFF)
    {
        scanMode = WIFI_STA;
    }
    else if (scanMode == WIFI_AP)
    {
        scanMode = WIFI_AP_STA;
    }

    if (scanMode != previousMode)
    {
        WiFi.mode(scanMode);
        delay(100);
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        WiFi.disconnect(false, false);
    }

    WiFi.scanDelete();
    int found = WiFi.scanNetworks(false, true);
    int bestIndex = -1;
    int32_t bestRssi = -127;

    for (int i = 0; i < found; ++i)
    {
        if (WiFi.SSID(i) != targetSsid)
            continue;

        int32_t rssi = WiFi.RSSI(i);
        if (bestIndex < 0 || rssi > bestRssi)
        {
            bestIndex = i;
            bestRssi = rssi;
        }
    }

    if (bestIndex >= 0)
    {
        const uint8_t *bssid = WiFi.BSSID(bestIndex);
        if (bssid != nullptr && outBssid != nullptr)
        {
            memcpy(outBssid, bssid, 6);
        }
        if (outChannel != nullptr)
        {
            *outChannel = WiFi.channel(bestIndex);
        }
        if (outRssi != nullptr)
        {
            *outRssi = bestRssi;
        }
    }

    WiFi.scanDelete();

    if (scanMode != previousMode)
    {
        WiFi.mode(previousMode);
    }

    return (bestIndex >= 0);
}

static void beginWifiConnectAttempt(bool showDisplayStatus, bool backgroundAttempt, bool preserveApMode)
{
    uint8_t bestBssid[6] = {0};
    int32_t bestChannel = 0;
    int32_t bestRssi = -127;
    bool hasCandidate = scanBestSsidCandidate(wifiSSID, bestBssid, &bestChannel, &bestRssi);

    if (hasCandidate)
    {
        memcpy(preferredBssid, bestBssid, 6);
        preferredBssidValid = true;
        preferredChannel = bestChannel;
        Serial.printf("[WiFi] Best AP for \"%s\": BSSID %02X:%02X:%02X:%02X:%02X:%02X ch %ld RSSI %ld\n",
                      wifiSSID.c_str(),
                      preferredBssid[0], preferredBssid[1], preferredBssid[2],
                      preferredBssid[3], preferredBssid[4], preferredBssid[5],
                      static_cast<long>(preferredChannel), static_cast<long>(bestRssi));
    }
    else
    {
        Serial.printf("[WiFi] SSID \"%s\" not found in scan.\n", wifiSSID.c_str());
    }

    if (!preserveApMode)
    {
        stopAccessPoint();
        WiFi.mode(WIFI_STA);
    }
    else
    {
        WiFi.mode(WIFI_AP_STA);
    }
    delay(50);
    WiFi.disconnect(false, false);
    delay(20);

    if (hasCandidate)
    {
        WiFi.begin(wifiSSID.c_str(), wifiPass.c_str(), preferredChannel, preferredBssid, true);
    }
    else
    {
        WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
    }

    wifiConnectInProgress = true;
    wifiConnectShowDisplay = showDisplayStatus;
    wifiConnectBackground = backgroundAttempt;
    wifiConnectFailedEvent = false;
    wifiConnectStartMs = millis();
    wifiConnectStatusDotMs = 0;
}
// --- END NEW CODE ---

bool startAccessPoint(const char *ssidOverride, const char *passOverride)
{
    String ssid = (ssidOverride && ssidOverride[0] != '\0') ? ssidOverride : WIFI_AP_NAME;
    ssid.trim();
    if (ssid.isEmpty())
    {
        ssid = "WxVision";
    }

    String pass = (passOverride && passOverride[0] != '\0') ? passOverride : WIFI_AP_PASS;
    pass.trim();

    if (apModeActive)
    {
        return true;
    }

    Serial.printf("[WiFi][AP] Starting SoftAP \"%s\"\n", ssid.c_str());

    WiFi.softAPdisconnect(true);
    WiFi.enableAP(false);

    wifi_mode_t mode = WiFi.getMode();
    if (mode != WIFI_AP && mode != WIFI_AP_STA)
    {
        WiFi.mode(WIFI_AP_STA);
    }
    else
    {
        WiFi.enableAP(true);
    }

    IPAddress local(WIFI_AP_IP);
    IPAddress gateway(WIFI_AP_GATEWAY);
    IPAddress subnet(WIFI_AP_SUBNET);

    if (!WiFi.softAPConfig(local, gateway, subnet))
    {
        Serial.println("[WiFi][AP] Failed to configure SoftAP network settings.");
        return false;
    }

    // Open AP: do not require a password
    const char *passPtr = nullptr;

    bool started = WiFi.softAP(ssid.c_str(), passPtr, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CLIENTS);
    if (!started)
    {
        Serial.println("[WiFi][AP] Failed to start SoftAP.");
        return false;
    }

    apModeActive = true;
    apSsid = ssid;
    apIp = WiFi.softAPIP();

    Serial.printf("[WiFi][AP] SoftAP ready at %s (clients connect to SSID \"%s\").\n",
                  apIp.toString().c_str(), ssid.c_str());
    return true;
}

void stopAccessPoint()
{
    if (!apModeActive)
        return;

    Serial.println("[WiFi][AP] Stopping SoftAP.");
    WiFi.softAPdisconnect(true);
    WiFi.enableAP(false);
    apModeActive = false;
    apSsid = "";
    apIp = IPAddress(0, 0, 0, 0);
}

bool isAccessPointActive()
{
    return apModeActive;
}

IPAddress getAccessPointIP()
{
    return apIp;
}

String getAccessPointSSID()
{
    return apSsid;
}

void connectToWiFi()
{
    Serial.printf("SSID: %s\n", wifiSSID.c_str());

    // --- Block empty or fake SSIDs ---
    if (wifiSSID.isEmpty() || wifiSSID == "(No networks)")
    {
        wifiSelecting = true;
        currentMenuLevel = MENU_WIFI_SELECT;
        menuActive = true;
        menuScroll = 0;
        wifiSelectIndex = 0;
        drawMenu();  // Will show drawWiFiMenu via drawMenu()
        return;
    }

    const bool allowDisplay = (dma_display != nullptr) && !isSplashActive();

    if (!dma_display)
    {
        Serial.println("dma_display not initialized. Skipping display output.");
    }
    else if (allowDisplay)
    {
        dma_display->clearScreen();
        dma_display->setCursor(0, 0);
        dma_display->setTextColor(myBLUE);
        dma_display->print("Connecting");
        dma_display->setCursor(0, 8);
        dma_display->setTextColor(myBLUE);
        dma_display->print("to WiFi...");
    }

    stopAccessPoint();

    Serial.printf("[WiFi] Connecting to SSID: %s\n", wifiSSID.c_str());

    // --- BEGIN NEW CODE ---
    beginWifiConnectAttempt(allowDisplay, false, false);
    // --- END NEW CODE ---
}

// --- BEGIN NEW CODE ---
void serviceWiFiConnection()
{
    if (!wifiConnectInProgress)
        return;

    wl_status_t status = WiFi.status();
    unsigned long now = millis();

    if (status == WL_CONNECTED)
    {
        wifiConnectInProgress = false;
        wifiConnectBackground = false;

        Serial.println("[WiFi] Connected!");
        Serial.print("[WiFi] IP Address: ");
        Serial.println(WiFi.localIP());
        stopAccessPoint();

        if (wifiConnectShowDisplay && dma_display != nullptr && !isSplashActive())
        {
            String ssidStr = WiFi.SSID();
            String ipStr = WiFi.localIP().toString();

            dma_display->clearScreen();
            dma_display->setCursor(0, 0);
            dma_display->setTextColor(myBLUE);
            dma_display->print("WiFi:");
            dma_display->setCursor(0, 8);
            dma_display->setTextColor(myWHITE);
            dma_display->printf(" %s", ssidStr.c_str());
            dma_display->setCursor(0, 16);
            dma_display->setTextColor(myBLUE);
            dma_display->print("IP: ");
            dma_display->setCursor(0, 24);
            dma_display->setTextColor(myWHITE);
            dma_display->printf(" %s", ipStr.c_str());
        }

        menuActive = false;
        wifiSelecting = false;
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
        reset_Time_and_Date_Display = true;
        return;
    }

    bool statusFailed = (status == WL_CONNECT_FAILED ||
                         status == WL_NO_SSID_AVAIL ||
                         status == WL_CONNECTION_LOST ||
                         status == WL_DISCONNECTED);
    bool timedOut = (now - wifiConnectStartMs) >= WIFI_CONNECT_TIMEOUT_MS;

    if (!timedOut && !statusFailed)
    {
        if (!wifiConnectBackground && (now - wifiConnectStatusDotMs) >= 500)
        {
            wifiConnectStatusDotMs = now;
            Serial.print(".");
        }
        return;
    }

    wifiConnectInProgress = false;
    wifiConnectBackground = false;
    wifiConnectFailedEvent = true;
    Serial.println();
    Serial.println("[WiFi] Connection FAILED!");

    if (wifiConnectShowDisplay && dma_display != nullptr && !isSplashActive())
    {
        dma_display->clearScreen();
        dma_display->setCursor(0, 0);
        dma_display->setTextColor(myRED);
        dma_display->print("WiFi Failed!");
    }

    // Keep credentials unchanged and avoid forcing WiFi selection prompts.
    wifiSelecting = false;
    menuActive = false;
    currentMenuLevel = MENU_MAIN;
    currentMenuIndex = 0;
    menuScroll = 0;
    reset_Time_and_Date_Display = true;
}

bool isWiFiConnectionInProgress()
{
    return wifiConnectInProgress;
}

bool consumeWiFiConnectionFailure()
{
    bool failed = wifiConnectFailedEvent;
    wifiConnectFailedEvent = false;
    return failed;
}

bool startBackgroundWifiReconnect(bool apActive)
{
    if (wifiConnectInProgress)
        return false;
    if (wifiSSID.isEmpty() || wifiPass.isEmpty())
        return false;

    wifi_mode_t desiredMode = apActive ? WIFI_AP_STA : WIFI_STA;
    if (WiFi.getMode() != desiredMode)
    {
        WiFi.mode(desiredMode);
    }

    beginWifiConnectAttempt(false, true, apActive);
    return true;
}
// --- END NEW CODE ---


