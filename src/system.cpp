#include <Arduino.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include "settings.h"
#include "display.h"
#include "notifications.h"

extern Preferences prefs;
extern int brightness;

void startOTA() {
    Serial.println("[SYSTEM] Starting OTA...");
    wxv::notify::showNotification(wxv::notify::NotifyId::OtaUpdate, dma_display->color565(255, 0, 0));
    // Actually call ElegantOTA here
    // ElegantOTA.begin(&server); if using in web.cpp
}

void quickRestore() {
    Serial.println("[SYSTEM] Reset Settings (keep Wi-Fi + logs)...");
    wxv::notify::showNotification(wxv::notify::NotifyId::Restoring, dma_display->color565(255, 0, 0));

    // Preserve Wi-Fi credentials, reset everything else in preferences.
    String savedSsid = "";
    String savedPass = "";
    prefs.begin("visionwx", false);
    savedSsid = prefs.getString("wifiSSID", "");
    savedPass = prefs.getString("wifiPass", "");
    prefs.clear(); // clears all keys in namespace
    if (!savedSsid.isEmpty())
    {
        prefs.putString("wifiSSID", savedSsid);
        prefs.putString("wifiPass", savedPass);
        // Keep setup flow bypass when credentials are still present.
        prefs.putBool("setupReady", true);
    }
    prefs.end();

    delay(1000);
    ESP.restart();
}

void factoryReset() {
    Serial.println("[SYSTEM] Factory Reset (erase Wi-Fi + logs)...");
    wxv::notify::showNotification(wxv::notify::NotifyId::FactoryReset, dma_display->color565(255, 0, 0));
    delay(500);

    // Clear all stored preferences, including Wi-Fi credentials.
    prefs.begin("visionwx", false);
    prefs.clear();
    prefs.end();

    // Remove persisted sensor log data from SPIFFS.
    if (SPIFFS.begin(true))
    {
        if (SPIFFS.exists("/sensor_log.bin"))
        {
            SPIFFS.remove("/sensor_log.bin");
        }
    }

    delay(1000);
    ESP.restart();
}

// --- RAM-only screen off state ---
static bool screenOff = false;
static uint8_t lastBrightness = 32; // Default or load from settings

void setScreenOff(bool off) {
    if (off && !screenOff) {
        uint8_t fallback = map(brightness, 1, 100, 3, 255);
        lastBrightness = currentPanelBrightness > 0 ? currentPanelBrightness : fallback;
        setPanelBrightness(0);
        screenOff = true;
    } else if (!off && screenOff) {
        screenOff = false;
        uint8_t restore = lastBrightness > 0 ? lastBrightness : map(brightness, 1, 100, 3, 255);
        setPanelBrightness(restore);
    }
}
bool isScreenOff() {
    return screenOff;
}

void toggleScreenPower() {
    setScreenOff(!isScreenOff());
}

