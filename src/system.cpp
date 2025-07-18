#include <Arduino.h>
#include <Preferences.h>
#include "settings.h"
#include "display.h"

extern Preferences prefs;
extern int brightness;

void startOTA() {
    Serial.println("[SYSTEM] Starting OTA...");
    dma_display->clearScreen();
    dma_display->setCursor(0, 0);
    dma_display->setTextColor(dma_display->color565(255,0,0));
    dma_display->print("OTA Update...");
    // Actually call ElegantOTA here
    // ElegantOTA.begin(&server); if using in web.cpp
}

void resetPowerUsage() {
    Serial.println("[SYSTEM] Resetting Power Usage...");
    dma_display->clearScreen();
    dma_display->setCursor(0, 0);
    dma_display->setTextColor(dma_display->color565(255,0,0));
    dma_display->print("Reset Power...");
    // Reset your power usage stats
    prefs.begin("visionwx", false);
    prefs.putFloat("kWhTotal", 0);
    prefs.putFloat("powerCost", 0);
    prefs.end();
}

void quickRestore() {
    Serial.println("[SYSTEM] Quick Restore to Defaults...");
    dma_display->clearScreen();
    dma_display->setCursor(0, 0);
    dma_display->setTextColor(dma_display->color565(255,0,0));
    dma_display->print("Restoring...");
    // Reset config keys but keep logs/history
    prefs.begin("visionwx", false);
    prefs.clear(); // clears all keys in namespace
    prefs.end();
    delay(1000);
    ESP.restart();
}

void factoryReset() {
    Serial.println("[SYSTEM] Factory Reset EVERYTHING...");
    dma_display->clearScreen();
    dma_display->setCursor(0, 0);
    dma_display->setTextColor(dma_display->color565(255,0,0));
    dma_display->print("FACTORY RESET...");
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
        lastBrightness = brightness;
        dma_display->setBrightness8(0);
        screenOff = true;
    } else if (!off && screenOff) {
        dma_display->setBrightness8(lastBrightness);
        screenOff = false;
    }
}
bool isScreenOff() {
    return screenOff;
}

