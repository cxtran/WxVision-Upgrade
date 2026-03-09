
#include <WiFi.h>
#include "wifisettings.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "display.h"
#include "settings.h"
#include "utils.h"
#include "menu.h"
#include "default_values.h"
#include "notifications.h"
#include <Preferences.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <Update.h>

#include <WiFiUdp.h>
// =======================
// WiFi SCAN/SELECT logic
// =======================

extern bool wifiSelecting;
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
static wl_status_t wifiConnectLastStatus = WL_IDLE_STATUS;
static unsigned long wifiConnectLastStatusChangeMs = 0;
static bool wifiEventHookInstalled = false;
static uint8_t wifiLastDisconnectReason = 0;
static unsigned long wifiLastDisconnectReasonAtMs = 0;
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = wxv::defaults::kDefaults.wifiConnectTimeoutMs;
static uint8_t preferredBssid[6] = {0};
static bool preferredBssidValid = false;
static int32_t preferredChannel = 0;
static bool wifiManualConnectRequested = false;

static WifiRunState s_wifiRunState = WifiRunState::WIFI_IDLE;
static WifiStatusCode s_wifiStatusCode = WifiStatusCode::OFFLINE;
static WifiStatusReason s_wifiStatusReason = WifiStatusReason::NONE;
static unsigned long s_wifiNextRetryAtMs = 0;
static unsigned long s_wifiRetryBackoffMs = 5000UL;
static uint8_t s_wifiAuthFailureCount = 0;
static bool s_wifiGotIpEvent = false;
static bool s_wifiDisconnectedEvent = false;
static uint8_t s_wifiDisconnectedReason = 0;
static bool s_wifiBootConnectRequested = false;
static uint8_t s_wifiPinnedConnectFailCount = 0;
static uint8_t s_wifiLastKnownBssid[6] = {0};
static bool s_wifiLastKnownBssidValid = false;
static int32_t s_wifiLastKnownChannel = 0;
static int32_t s_wifiLastGoodRssi = -127;

static const unsigned long WIFI_RETRY_MIN_MS = wxv::defaults::kDefaults.wifiRetryInitialMs;
static const unsigned long WIFI_RETRY_MAX_MS = wxv::defaults::kDefaults.wifiRetryMaxMs;
static const unsigned long WIFI_SSID_RESCAN_MS = wxv::defaults::kDefaults.wifiSsidNotFoundScanMs;
static constexpr uint8_t WIFI_AUTH_FAIL_LIMIT = 3;
static constexpr const char *kWifiPrefsNs = "visionwx";
static constexpr const char *kWifiPinBssidKey = "wifi_bssid";
static constexpr const char *kWifiPinChKey = "wifi_ch";
static constexpr const char *kWifiPinRssiKey = "wifi_rssi";
static constexpr const char *kWifiLastReasonKey = "wifi_reason";

static void wifiSetStatus(WifiStatusCode code, WifiStatusReason reason)
{
    s_wifiStatusCode = code;
    s_wifiStatusReason = reason;
}

static bool wifiReasonIsAuthFail(uint8_t reason)
{
    switch (static_cast<wifi_err_reason_t>(reason))
    {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_802_1X_AUTH_FAILED:
        return true;
    default:
        return false;
    }
}

static bool wifiReasonIsSsidNotFound(uint8_t reason)
{
    return static_cast<wifi_err_reason_t>(reason) == WIFI_REASON_NO_AP_FOUND;
}

static bool wifiReasonIsBeaconTimeout(uint8_t reason)
{
    return static_cast<wifi_err_reason_t>(reason) == WIFI_REASON_BEACON_TIMEOUT;
}

static void wifiScheduleRetry(unsigned long delayMs)
{
    s_wifiRunState = WifiRunState::WIFI_WAIT_RETRY;
    s_wifiNextRetryAtMs = millis() + delayMs;
}

static void wifiScheduleBackoffRetry()
{
    if (s_wifiRetryBackoffMs < WIFI_RETRY_MIN_MS)
        s_wifiRetryBackoffMs = WIFI_RETRY_MIN_MS;
    wifiScheduleRetry(s_wifiRetryBackoffMs);
    if (s_wifiRetryBackoffMs < WIFI_RETRY_MAX_MS)
    {
        s_wifiRetryBackoffMs *= 2UL;
        if (s_wifiRetryBackoffMs > WIFI_RETRY_MAX_MS)
            s_wifiRetryBackoffMs = WIFI_RETRY_MAX_MS;
    }
}

static void wifiLoadPinnedMetadata()
{
    Preferences prefs;
    if (!prefs.begin(kWifiPrefsNs, true))
        return;

    String bssidHex = prefs.getString(kWifiPinBssidKey, "");
    s_wifiLastKnownChannel = prefs.getInt(kWifiPinChKey, 0);
    s_wifiLastGoodRssi = prefs.getInt(kWifiPinRssiKey, -127);
    s_wifiStatusReason = static_cast<WifiStatusReason>(prefs.getUChar(kWifiLastReasonKey, static_cast<uint8_t>(WifiStatusReason::NONE)));
    prefs.end();

    bssidHex.trim();
    s_wifiLastKnownBssidValid = false;
    if (bssidHex.length() == 12)
    {
        bool ok = true;
        for (int i = 0; i < 6; ++i)
        {
            String part = bssidHex.substring(i * 2, i * 2 + 2);
            char *endPtr = nullptr;
            long v = strtol(part.c_str(), &endPtr, 16);
            if (endPtr == nullptr || *endPtr != '\0' || v < 0 || v > 255)
            {
                ok = false;
                break;
            }
            s_wifiLastKnownBssid[i] = static_cast<uint8_t>(v);
        }
        s_wifiLastKnownBssidValid = ok;
    }
}

static void wifiSavePinnedMetadata(const uint8_t *bssid, int32_t channel, int32_t rssi)
{
    if (!bssid)
        return;
    char hex[13];
    snprintf(hex, sizeof(hex), "%02X%02X%02X%02X%02X%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);

    Preferences prefs;
    if (!prefs.begin(kWifiPrefsNs, false))
        return;
    prefs.putString(kWifiPinBssidKey, String(hex));
    prefs.putInt(kWifiPinChKey, channel);
    prefs.putInt(kWifiPinRssiKey, rssi);
    prefs.putUChar(kWifiLastReasonKey, static_cast<uint8_t>(s_wifiStatusReason));
    prefs.end();

    memcpy(s_wifiLastKnownBssid, bssid, 6);
    s_wifiLastKnownBssidValid = true;
    s_wifiLastKnownChannel = channel;
    s_wifiLastGoodRssi = rssi;
}

static void wifiSaveLastReason()
{
    Preferences prefs;
    if (!prefs.begin(kWifiPrefsNs, false))
        return;
    prefs.putUChar(kWifiLastReasonKey, static_cast<uint8_t>(s_wifiStatusReason));
    prefs.end();
}

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

static void ensureWiFiEventHook()
{
    if (wifiEventHookInstalled)
        return;

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
        {
            wifiLastDisconnectReason = info.wifi_sta_disconnected.reason;
            wifiLastDisconnectReasonAtMs = millis();
            s_wifiDisconnectedReason = info.wifi_sta_disconnected.reason;
            s_wifiDisconnectedEvent = true;
        }
        else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP)
        {
            s_wifiGotIpEvent = true;
        }
    });

    wifiEventHookInstalled = true;
}

static String wifiFailureReasonFromCode(uint8_t reason)
{
    switch (static_cast<wifi_err_reason_t>(reason))
    {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_802_1X_AUTH_FAILED:
        return "BAD PASS";
    case WIFI_REASON_NO_AP_FOUND:
        return "NO NET";
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_CONNECTION_FAIL:
    case WIFI_REASON_BEACON_TIMEOUT:
        return "AP DOWN";
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_ASSOC_EXPIRE:
    case WIFI_REASON_TIMEOUT:
        return "TIMEOUT";
    case WIFI_REASON_ASSOC_TOOMANY:
        return "AP busy";
    case WIFI_REASON_NOT_AUTHORIZED_THIS_LOCATION:
        return "DENIED";
    default:
        break;
    }

    if (reason != 0)
    {
        return String("ERR ") + String(reason);
    }
    return "WIFI FAIL";
}

static String wifiFailureReasonForStatus(wl_status_t status, bool timedOut)
{
    const unsigned long age = millis() - wifiLastDisconnectReasonAtMs;
    if (wifiLastDisconnectReason != 0 && age <= 30000UL)
    {
        return wifiFailureReasonFromCode(wifiLastDisconnectReason);
    }

    if (status == WL_NO_SSID_AVAIL)
        return "NO NET";
    if (status == WL_CONNECT_FAILED)
        return "BAD PASS";
    if (timedOut)
        return "TIMEOUT";
    return "WIFI FAIL";
}

static void beginWifiConnectAttempt(bool showDisplayStatus, bool backgroundAttempt, bool preserveApMode)
{
    ensureWiFiEventHook();
    wifiLastDisconnectReason = 0;
    wifiLastDisconnectReasonAtMs = 0;
    s_wifiGotIpEvent = false;
    s_wifiDisconnectedEvent = false;
    s_wifiDisconnectedReason = 0;

    uint8_t bestBssid[6] = {0};
    int32_t bestChannel = 0;
    int32_t bestRssi = -127;
    bool hasCandidate = scanBestSsidCandidate(wifiSSID, bestBssid, &bestChannel, &bestRssi);
    const bool recentBeaconTimeout =
        wifiReasonIsBeaconTimeout(wifiLastDisconnectReason) &&
        (millis() - wifiLastDisconnectReasonAtMs) <= 60000UL;

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
        if (recentBeaconTimeout)
        {
            Serial.printf("[WiFi] Scan missed SSID \"%s\" after recent BEACON_TIMEOUT; treating as transient link loss.\n",
                          wifiSSID.c_str());
            wifiSetStatus(WifiStatusCode::OFFLINE, WifiStatusReason::ROUTER_DOWN);
            s_wifiRunState = WifiRunState::WIFI_DISCONNECTED;
            wifiSaveLastReason();
            wifiScheduleBackoffRetry();
        }
        else
        {
            Serial.printf("[WiFi] SSID \"%s\" not found in scan.\n", wifiSSID.c_str());
            wifiSetStatus(WifiStatusCode::OFFLINE, WifiStatusReason::SSID_NOT_FOUND);
            s_wifiRunState = WifiRunState::WIFI_DISCONNECTED;
            wifiSaveLastReason();
            if (showDisplayStatus && dma_display != nullptr && !isSplashActive())
            {
                wxv::notify::showNotification(wxv::notify::NotifyId::WifiFail, myRED, myWHITE, "NO NET");
            }
            wifiScheduleRetry(WIFI_SSID_RESCAN_MS);
        }
        return;
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
    WiFi.disconnect(false, false);

    bool tryPinned = s_wifiLastKnownBssidValid &&
                     s_wifiLastKnownChannel > 0 &&
                     s_wifiPinnedConnectFailCount < 2 &&
                     !wifiManualConnectRequested;

    if (tryPinned)
    {
        Serial.printf("[WiFi] Trying pinned BSSID %02X:%02X:%02X:%02X:%02X:%02X ch %ld\n",
                      s_wifiLastKnownBssid[0], s_wifiLastKnownBssid[1], s_wifiLastKnownBssid[2],
                      s_wifiLastKnownBssid[3], s_wifiLastKnownBssid[4], s_wifiLastKnownBssid[5],
                      static_cast<long>(s_wifiLastKnownChannel));
        WiFi.begin(wifiSSID.c_str(), wifiPass.c_str(), s_wifiLastKnownChannel, s_wifiLastKnownBssid, true);
    }
    else
    {
        WiFi.begin(wifiSSID.c_str(), wifiPass.c_str(), preferredChannel, preferredBssid, true);
    }

    wifiConnectInProgress = true;
    wifiConnectShowDisplay = showDisplayStatus;
    wifiConnectBackground = backgroundAttempt;
    wifiConnectFailedEvent = false;
    wifiConnectStartMs = millis();
    wifiConnectStatusDotMs = 0;
    wifiConnectLastStatus = WiFi.status();
    wifiConnectLastStatusChangeMs = wifiConnectStartMs;
    s_wifiRunState = WifiRunState::WIFI_CONNECTING;
    wifiSetStatus(WifiStatusCode::CONNECTING, WifiStatusReason::NONE);
    wifiSaveLastReason();
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

    if (wifiManualConnectRequested)
    {
        // User selected a network manually: clear old BSSID pin and reset retry/auth state.
        wifiClearPinnedMetadata();
        s_wifiAuthFailureCount = 0;
        s_wifiRetryBackoffMs = WIFI_RETRY_MIN_MS;
        s_wifiNextRetryAtMs = 0;
    }

    const bool allowDisplay = (dma_display != nullptr) && !isSplashActive();

    if (!dma_display)
    {
        Serial.println("dma_display not initialized. Skipping display output.");
    }
    else if (allowDisplay)
    {
        wxv::notify::showNotification(wxv::notify::NotifyId::WifiConnecting, myBLUE);
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
    if (status != wifiConnectLastStatus)
    {
        wifiConnectLastStatus = status;
        wifiConnectLastStatusChangeMs = now;
    }

    if (status == WL_CONNECTED)
    {
        wifiConnectInProgress = false;
        wifiConnectBackground = false;
        s_wifiRunState = WifiRunState::WIFI_CONNECTED;
        wifiSetStatus(WifiStatusCode::CONNECTED, WifiStatusReason::NONE);
        s_wifiRetryBackoffMs = WIFI_RETRY_MIN_MS;
        s_wifiAuthFailureCount = 0;
        s_wifiPinnedConnectFailCount = 0;

        Serial.println("[WiFi] Connected!");
        Serial.print("[WiFi] IP Address: ");
        Serial.println(WiFi.localIP());
        stopAccessPoint();

        const uint8_t *connectedBssid = WiFi.BSSID();
        if (connectedBssid != nullptr)
        {
            wifiSavePinnedMetadata(connectedBssid, WiFi.channel(), WiFi.RSSI());
        }

        if (wifiConnectShowDisplay && dma_display != nullptr && !isSplashActive())
        {
            wxv::notify::showNotification(wxv::notify::NotifyId::WifiConnected, myBLUE, myWHITE, "IP READY");
        }

        menuActive = false;
        wifiSelecting = false;
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
        reset_Time_and_Date_Display = true;
        wifiManualConnectRequested = false;
        return;
    }

    // Only treat hard terminal states as immediate failure.
    // WL_DISCONNECTED / WL_CONNECTION_LOST can be transient while association/auth is in progress.
    bool statusFailed = (status == WL_CONNECT_FAILED ||
                         status == WL_NO_SSID_AVAIL);

    // If link keeps flapping in disconnected/lost long enough, fail this attempt gracefully.
    if (!statusFailed &&
        (status == WL_DISCONNECTED || status == WL_CONNECTION_LOST) &&
        (now - wifiConnectLastStatusChangeMs) >= 6000UL)
    {
        statusFailed = true;
    }
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
    String failReason = wifiFailureReasonForStatus(status, timedOut);
    WifiStatusReason reasonCode = WifiStatusReason::ROUTER_DOWN;
    if (status == WL_NO_SSID_AVAIL)
    {
        reasonCode = WifiStatusReason::SSID_NOT_FOUND;
    }
    else if (timedOut)
    {
        reasonCode = WifiStatusReason::CONNECT_TIMEOUT;
    }
    if (wifiReasonIsAuthFail(wifiLastDisconnectReason))
    {
        reasonCode = WifiStatusReason::AUTH_FAILED;
    }

    if (reasonCode == WifiStatusReason::AUTH_FAILED || status == WL_CONNECT_FAILED)
    {
        if (s_wifiAuthFailureCount < 255)
            s_wifiAuthFailureCount++;
    }
    else
    {
        s_wifiAuthFailureCount = 0;
    }

    if (s_wifiLastKnownBssidValid && !wifiManualConnectRequested && reasonCode != WifiStatusReason::SSID_NOT_FOUND)
    {
        if (s_wifiPinnedConnectFailCount < 255)
            s_wifiPinnedConnectFailCount++;
    }

    if (s_wifiAuthFailureCount >= WIFI_AUTH_FAIL_LIMIT)
    {
        s_wifiRunState = WifiRunState::WIFI_DISCONNECTED;
        wifiSetStatus(WifiStatusCode::AUTH_FAILED, WifiStatusReason::AUTH_FAILED);
        s_wifiNextRetryAtMs = 0;
    }
    else if (reasonCode == WifiStatusReason::SSID_NOT_FOUND)
    {
        s_wifiRunState = WifiRunState::WIFI_DISCONNECTED;
        wifiSetStatus(WifiStatusCode::OFFLINE, WifiStatusReason::SSID_NOT_FOUND);
        wifiScheduleRetry(WIFI_SSID_RESCAN_MS);
    }
    else
    {
        s_wifiRunState = WifiRunState::WIFI_DISCONNECTED;
        wifiSetStatus(WifiStatusCode::OFFLINE, reasonCode);
        wifiScheduleBackoffRetry();
    }
    wifiSaveLastReason();

    Serial.println();
    Serial.printf("[WiFi] Connection FAILED: %s\n", failReason.c_str());

    if (wifiConnectShowDisplay && dma_display != nullptr && !isSplashActive())
    {
        wxv::notify::showNotification(wxv::notify::NotifyId::WifiFail, myRED, myWHITE, failReason);
    }

    // Keep credentials unchanged and avoid forcing WiFi selection prompts.
    wifiSelecting = false;
    menuActive = false;
    currentMenuLevel = MENU_MAIN;
    currentMenuIndex = 0;
    menuScroll = 0;
    reset_Time_and_Date_Display = true;
    wifiManualConnectRequested = false;
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
    if (s_wifiStatusCode == WifiStatusCode::AUTH_FAILED)
        return false;

    wifi_mode_t desiredMode = apActive ? WIFI_AP_STA : WIFI_STA;
    if (WiFi.getMode() != desiredMode)
    {
        WiFi.mode(desiredMode);
    }

    beginWifiConnectAttempt(false, true, apActive);
    return true;
}

bool wifiHadRecentBeaconTimeout(unsigned long windowMs)
{
    if (wifiLastDisconnectReason != static_cast<uint8_t>(WIFI_REASON_BEACON_TIMEOUT))
        return false;

    const unsigned long age = millis() - wifiLastDisconnectReasonAtMs;
    return age <= windowMs;
}
// --- END NEW CODE ---

// --- BEGIN WIFI INDUSTRIAL REDESIGN ---
void wifiClearPinnedMetadata()
{
    Preferences prefs;
    if (prefs.begin(kWifiPrefsNs, false))
    {
        prefs.remove(kWifiPinBssidKey);
        prefs.remove(kWifiPinChKey);
        prefs.remove(kWifiPinRssiKey);
        prefs.end();
    }
    s_wifiLastKnownBssidValid = false;
    s_wifiLastKnownChannel = 0;
    s_wifiLastGoodRssi = -127;
    s_wifiPinnedConnectFailCount = 0;
}

void wifiInitStateMachine()
{
    ensureWiFiEventHook();
    wifiLoadPinnedMetadata();
    s_wifiRetryBackoffMs = WIFI_RETRY_MIN_MS;
    s_wifiAuthFailureCount = 0;
    s_wifiNextRetryAtMs = 0;
    s_wifiBootConnectRequested = false;

    if (wifiHasCredentials())
    {
        s_wifiRunState = WifiRunState::WIFI_IDLE;
        wifiSetStatus(WifiStatusCode::OFFLINE, WifiStatusReason::ROUTER_DOWN);
    }
    else
    {
        s_wifiRunState = WifiRunState::WIFI_PROVISIONING;
        wifiSetStatus(WifiStatusCode::OFFLINE, WifiStatusReason::NO_CREDENTIALS);
    }
}

void wifiMarkManualConnect()
{
    wifiManualConnectRequested = true;
}

bool wifiHasCredentials()
{
    return !wifiSSID.isEmpty() && !wifiPass.isEmpty();
}

bool wifiIsConnecting()
{
    return wifiConnectInProgress || s_wifiRunState == WifiRunState::WIFI_CONNECTING;
}

WifiRunState wifiGetRunState()
{
    return s_wifiRunState;
}

WifiStatusCode wifiGetStatusCode()
{
    return s_wifiStatusCode;
}

WifiStatusReason wifiGetStatusReason()
{
    return s_wifiStatusReason;
}

const char *wifiStatusCodeText()
{
    switch (s_wifiStatusCode)
    {
    case WifiStatusCode::CONNECTED: return "CONNECTED";
    case WifiStatusCode::CONNECTING: return "CONNECTING";
    case WifiStatusCode::OFFLINE: return "OFFLINE";
    case WifiStatusCode::AUTH_FAILED: return "AUTH_FAILED";
    case WifiStatusCode::ERROR: return "ERROR";
    default: return "OFFLINE";
    }
}

const char *wifiStatusReasonText()
{
    switch (s_wifiStatusReason)
    {
    case WifiStatusReason::NONE: return "NONE";
    case WifiStatusReason::NO_CREDENTIALS: return "NO_CREDENTIALS";
    case WifiStatusReason::SSID_NOT_FOUND: return "SSID_NOT_FOUND";
    case WifiStatusReason::ROUTER_DOWN: return "ROUTER_DOWN";
    case WifiStatusReason::AUTH_FAILED: return "AUTH_FAILED";
    case WifiStatusReason::CONNECT_TIMEOUT: return "CONNECT_TIMEOUT";
    case WifiStatusReason::MANUAL_DISCONNECT: return "MANUAL_DISCONNECT";
    case WifiStatusReason::ERROR: return "ERROR";
    default: return "ERROR";
    }
}

void wifiStartBootConnect(bool apActive)
{
    s_wifiBootConnectRequested = true;

    if (!wifiHasCredentials())
    {
        s_wifiRunState = WifiRunState::WIFI_PROVISIONING;
        wifiSetStatus(WifiStatusCode::OFFLINE, WifiStatusReason::NO_CREDENTIALS);
        wifiSaveLastReason();
        return;
    }

    wifiManualConnectRequested = false;
    if (!isWiFiConnectionInProgress())
    {
        startBackgroundWifiReconnect(apActive);
    }
}

void wifiLoop(bool apActive)
{
    serviceWiFiConnection();

    if (WiFi.status() == WL_CONNECTED)
    {
        s_wifiRunState = WifiRunState::WIFI_CONNECTED;
        wifiSetStatus(WifiStatusCode::CONNECTED, WifiStatusReason::NONE);
        s_wifiRetryBackoffMs = WIFI_RETRY_MIN_MS;
        s_wifiNextRetryAtMs = 0;
        s_wifiAuthFailureCount = 0;
    }

    if (s_wifiGotIpEvent)
    {
        s_wifiGotIpEvent = false;
        s_wifiRunState = WifiRunState::WIFI_CONNECTED;
        wifiSetStatus(WifiStatusCode::CONNECTED, WifiStatusReason::NONE);
        s_wifiRetryBackoffMs = WIFI_RETRY_MIN_MS;
        s_wifiNextRetryAtMs = 0;
        s_wifiAuthFailureCount = 0;
    }

    if (s_wifiDisconnectedEvent)
    {
        s_wifiDisconnectedEvent = false;
        if (WiFi.status() != WL_CONNECTED && !wifiConnectInProgress)
        {
            if (wifiReasonIsAuthFail(s_wifiDisconnectedReason))
            {
                if (s_wifiAuthFailureCount < 255)
                    s_wifiAuthFailureCount++;
                if (s_wifiAuthFailureCount >= WIFI_AUTH_FAIL_LIMIT)
                {
                    s_wifiRunState = WifiRunState::WIFI_DISCONNECTED;
                    wifiSetStatus(WifiStatusCode::AUTH_FAILED, WifiStatusReason::AUTH_FAILED);
                    s_wifiNextRetryAtMs = 0;
                    wifiSaveLastReason();
                }
                else
                {
                    s_wifiRunState = WifiRunState::WIFI_DISCONNECTED;
                    wifiSetStatus(WifiStatusCode::OFFLINE, WifiStatusReason::AUTH_FAILED);
                    wifiScheduleBackoffRetry();
                    wifiSaveLastReason();
                }
            }
            else if (wifiReasonIsSsidNotFound(s_wifiDisconnectedReason))
            {
                s_wifiRunState = WifiRunState::WIFI_DISCONNECTED;
                wifiSetStatus(WifiStatusCode::OFFLINE, WifiStatusReason::SSID_NOT_FOUND);
                wifiScheduleRetry(WIFI_SSID_RESCAN_MS);
                wifiSaveLastReason();
            }
            else
            {
                s_wifiRunState = WifiRunState::WIFI_DISCONNECTED;
                wifiSetStatus(WifiStatusCode::OFFLINE, WifiStatusReason::ROUTER_DOWN);
                wifiScheduleBackoffRetry();
                wifiSaveLastReason();
            }
        }
    }

    if (!wifiHasCredentials())
    {
        s_wifiRunState = WifiRunState::WIFI_PROVISIONING;
        wifiSetStatus(WifiStatusCode::OFFLINE, WifiStatusReason::NO_CREDENTIALS);
        return;
    }

    if (s_wifiStatusCode == WifiStatusCode::AUTH_FAILED)
    {
        return;
    }

    if (wifiConnectInProgress || WiFi.status() == WL_CONNECTED || wifiSelecting)
    {
        return;
    }

    unsigned long now = millis();
    if (s_wifiNextRetryAtMs == 0 || now >= s_wifiNextRetryAtMs)
    {
        startBackgroundWifiReconnect(apActive);
    }
}
// --- END WIFI INDUSTRIAL REDESIGN ---


