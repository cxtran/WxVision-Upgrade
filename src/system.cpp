#include <Arduino.h>
#include <Preferences.h>
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
    Serial.println("[SYSTEM] Quick Restore to Defaults...");
    wxv::notify::showNotification(wxv::notify::NotifyId::Restoring, dma_display->color565(255, 0, 0));
    // Reset config keys but keep logs/history
    prefs.begin("visionwx", false);
    prefs.clear(); // clears all keys in namespace
    prefs.end();
    delay(1000);
    ESP.restart();
}

void factoryReset() {
    Serial.println("[SYSTEM] Factory Reset EVERYTHING...");
    wxv::notify::showNotification(wxv::notify::NotifyId::FactoryReset, dma_display->color565(255, 0, 0));
    delay(500);
    prefs.begin("visionwx", false);
    prefs.clear();
    prefs.end();
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

