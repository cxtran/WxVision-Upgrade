#include <Arduino_JSON.h>
#include "time.h"
#include "RTClib.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include "display.h"
#include "pins.h"
#include "button.h"
#include "utils.h"
#include "icons.h"
#include "sensors.h"
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Update.h>
#include "settings.h"
#include "web.h"
#include "buzzer.h"
#include "menu.h"
#include "keyboard.h"
#include "tempest.h"
#include "InfoScreen.h"

// --- Screen rotation: add or remove as needed ---
const ScreenMode InfoScreenModes[] = {
    SCREEN_OWM, SCREEN_UDP_DATA, SCREEN_UDP_FORECAST
};
const int NUM_INFOSCREENS = sizeof(InfoScreenModes) / sizeof(ScreenMode);

// --- Global system state ---
ScreenMode currentScreen = SCREEN_OWM;

// --- Modal objects ---
extern InfoModal sysInfoModal, wifiInfoModal, dateModal, mainMenuModal, deviceModal, displayModal, weatherModal, calibrationModal, systemModal;

InfoScreen udpScreen("Live Weather", SCREEN_UDP_DATA);
InfoScreen forecastScreen("Forecast", SCREEN_UDP_FORECAST);

extern void (*pendingModalFn)();
extern unsigned long pendingModalTime;

extern int wifiSelectIndex;
extern unsigned long lastMenuActivity;
extern int menuScroll;
extern bool menuActive;
extern int currentMenuIndex;
extern MenuLevel currentMenuLevel;

// IR globals for InfoScreen IR handling
extern IRrecv irrecv;
extern decode_results results;

Preferences preferences;
int menuIndex = 0;
bool inSetupMenu = false;
WiFiUDP udp;
const int localPort = 50222;

// === Display Timers ===
unsigned long prevMillis_ShowTimeDate = 0;
const long interval_ShowTimeDate = 1000;
unsigned long lastBrightnessRead = 0;
unsigned long lastButtonCheck = 0;
const unsigned long brightnessInterval = 5000;
const unsigned long buttonInterval = 100;

// --- Weather/scroll tick ---
void scrollWeatherTick() {
    static unsigned long lastTick = 0;
    if (millis() - lastTick >= 40) {
        lastTick = millis();
        scrollWeatherDetails();
    }
}

// --- Hide all InfoScreens (convenience) ---
void hideAllInfoScreens() {
    udpScreen.hide();
    forecastScreen.hide();
    // Add more InfoScreens here as needed
}

// --- Screen rotation handler ---
void rotateScreen(int direction) {
    int idx = -1;
    for (int i = 0; i < NUM_INFOSCREENS; ++i) {
        if (InfoScreenModes[i] == currentScreen) { idx = i; break; }
    }
    if (idx < 0) return;
    int nextIdx = (idx + direction + NUM_INFOSCREENS) % NUM_INFOSCREENS;
    ScreenMode next = InfoScreenModes[nextIdx];

    hideAllInfoScreens(); // Hide previous
    currentScreen = next;
    // Show next if needed (not OWM)
    if (currentScreen == SCREEN_UDP_DATA) showUdpScreen();
    else if (currentScreen == SCREEN_UDP_FORECAST) showForecastScreen();
    // OWM will be drawn in main loop
}

// --- Button reset logic ---
void handleResetButton() {
    static bool buttonWasDown = false;
    static unsigned long buttonDownMillis = 0;
    static bool resetLongPressHandled = false;
    const unsigned long resetHoldTime = 3000;
    bool buttonDown = (digitalRead(BTN_SEL) == LOW);

    if (buttonDown && !buttonWasDown) {
        buttonDownMillis = millis();
        resetLongPressHandled = false;
        buttonWasDown = true;
    }
    if (buttonDown && !resetLongPressHandled) {
        if (millis() - buttonDownMillis > resetHoldTime) {
            resetLongPressHandled = true;
            triggerPhysicalReset();
        }
    }
    if (!buttonDown && buttonWasDown) {
        buttonWasDown = false;
        resetLongPressHandled = false;
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);

    Serial.printf("Flash size: %u bytes\n", ESP.getFlashChipSize());
    loadSettings();
    delay(500);
    setupDisplay();
    Serial.println("Display setup done.");

    Serial.println("Setup IR Sensor");
    setupIRSensor();

    Serial.println("\nESP32 Weather Display");

    if (wifiSSID.isEmpty() || wifiPass.isEmpty()) {
        Serial.println("[WiFi] No credentials, showing WiFi menu...");
        onWiFiConnectFailed();
        return;
    }

    Serial.println("Connecting WiFi...");
    connectToWiFi();

    if (wifiSelecting)
        return;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Connection failed, showing WiFi menu...");
        onWiFiConnectFailed();
        return;
    }

    Serial.println("WiFi done.");
    ArduinoOTA.setHostname("ESP32-Weather");
    ArduinoOTA.begin();

    setupWebServer();
    Serial.println("Displaying Time...");
    syncTimeFromNTP1();
    Serial.println("Done.");

    udp.begin(localPort);
    Serial.printf("Listening for Tempest on UDP port %d\n", localPort);

    fetchWeatherFromOWM();
    delay(500);
    getTimeFromRTC();
    fetchTempestData();
    fetchForecastData();

    reset_Time_and_Date_Display = true;

    displayWeatherData();
    displayClock();
    displayDate();

    delay(2000);
    setupButtons();

    currentMenuLevel = MENU_MAIN;
    currentMenuIndex = 0;
    menuActive = false;
    menuScroll = 0;
}


void loop() {
    unsigned long now = millis();
    static unsigned long lastForecast = 0;
    static unsigned long lastBlink = 0;
    const unsigned long blinkInterval = 500;
 
    static ScreenMode lastScreen = SCREEN_OWM;  // or whatever your default is
    static bool needsClear = false;
    if (currentScreen != lastScreen) {
        needsClear = true;
        lastScreen = currentScreen;
    }

    // --- Fetch new forecast data every 15 minutes ---
    if (now - lastForecast > 15 * 60 * 1000) {
        fetchForecastData();
        lastForecast = now;
    }

    // Only check physical reset if NO modal, NO keyboard, and NOT in WiFi select
    if (
        !inKeyboardMode &&
        !sysInfoModal.isActive() &&
        !wifiInfoModal.isActive() &&
        !dateModal.isActive() &&
        !mainMenuModal.isActive() &&
        !deviceModal.isActive() &&
        !displayModal.isActive() &&
        !weatherModal.isActive() &&
        !calibrationModal.isActive() &&
        !systemModal.isActive() &&
        !(wifiSelecting && currentMenuLevel == MENU_WIFI_SELECT)
    ) {
        handleResetButton();
    }

    // --- 1. Keyboard always has focus if active ---
    if (inKeyboardMode) {
        uint32_t code = getIRCodeNonBlocking();
        if (code) handleKeyboardIR(code);

        if (now - lastBlink >= blinkInterval) {
            lastBlink = now;
            keyboardBlinkTick();
        }
        tickKeyboard();
        delay(40);
        return;
    }

    // --- 2. If any modal is active, route IR input ONLY to modal ---
    if (sysInfoModal.isActive())        { sysInfoModal.tick(); sysInfoModal.handleIR(getIRCodeNonBlocking()); delay(40); return; }
    if (wifiInfoModal.isActive())       { wifiInfoModal.tick(); wifiInfoModal.handleIR(getIRCodeNonBlocking()); delay(40); return; }
    if (dateModal.isActive())           { dateModal.tick(); dateModal.handleIR(getIRCodeNonBlocking()); delay(40); return; }
    if (mainMenuModal.isActive())       { mainMenuModal.tick(); mainMenuModal.handleIR(getIRCodeNonBlocking()); delay(40); return; }
    if (deviceModal.isActive())         { deviceModal.tick(); deviceModal.handleIR(getIRCodeNonBlocking()); delay(40); return; }
    if (displayModal.isActive())        { displayModal.tick(); displayModal.handleIR(getIRCodeNonBlocking()); delay(40); return; }
    if (weatherModal.isActive())        { weatherModal.tick(); weatherModal.handleIR(getIRCodeNonBlocking()); delay(40); return; }
    if (calibrationModal.isActive())    { calibrationModal.tick(); calibrationModal.handleIR(getIRCodeNonBlocking()); delay(40); return; }
    if (systemModal.isActive())         { systemModal.tick(); systemModal.handleIR(getIRCodeNonBlocking()); delay(40); return; }

    // --- 3. Handle WiFi SSID selection menu ---
    if (wifiSelecting && currentMenuLevel == MENU_WIFI_SELECT) {
        uint32_t code = getIRCodeNonBlocking();
        if (code) handleIR(code);    // Route IR to menu.cpp WiFi handler
        drawWiFiMenu();              // Draw scanned WiFi list
        delay(80);                  // Slight delay for smoother response
        return;                     // Skip rest of loop while selecting WiFi
    }

    // --- 4. Pending modal delayed calls ---
    if (pendingModalFn && millis() >= pendingModalTime) {
        void (*fn)() = pendingModalFn;
        pendingModalFn = nullptr;
        pendingModalTime = 0;
        fn();
        return;
    }

    // --- 5. InfoScreens (live screens) ---
    if (udpScreen.isActive()) {
        udpScreen.tick();
        udpScreen.handleIR(getIRCodeNonBlocking());
        delay(40);
        return;
    }
    if (forecastScreen.isActive()) {
        forecastScreen.tick();
        forecastScreen.handleIR(getIRCodeNonBlocking());
        delay(40);
        return;
    }

    // --- 6. No modal/menu/keyboard/InfoScreen active: handle IR for menu or screen rotation ---
    uint32_t code = getIRCodeNonBlocking();
    if (code == IR_LEFT)  { rotateScreen(-1); return; }
    if (code == IR_RIGHT) { rotateScreen(+1); return; }
    if (code == IR_CANCEL) {
        showMainMenuModal();
        playBuzzerTone(3000, 100);
        delay(100);
        return;
    }

    // --- 7. Weather scroll tick ---
    scrollWeatherTick();

    // --- 8. Main background tasks ---
    ArduinoOTA.handle();
    fetchTempestData();

    // --- 9. Brightness control ---
    static unsigned long lastBrightnessRead = 0;
    if (now - lastBrightnessRead >= 5000) {
        lastBrightnessRead = now;
        float lux = readBrightnessSensor();
        if (autoBrightness) {
            setDisplayBrightnessFromLux(lux);
        } else {
            int hwBrightness = map(brightness, 1, 100, 3, 255);
            dma_display->setBrightness8(hwBrightness);
            Serial.printf("Manual Brightness: %d%% -> %d\n", brightness, hwBrightness);
        }
    }

    // --- 10. Update main live screen ---
    if (reset_Time_and_Date_Display) {
        reset_Time_and_Date_Display = false;
        displayClock();
        displayDate();
        displayWeatherData();
    }

    // --- 11. Screen rendering ---
    switch (currentScreen) {
        case SCREEN_OWM:
            static unsigned long prevMillis_ShowTimeDate = 0;
            if (now - prevMillis_ShowTimeDate >= 1000) {
                reset_Time_and_Date_Display = true;
                prevMillis_ShowTimeDate = now;
                if (needsClear) {
                    dma_display->fillScreen(0);
                    needsClear = false;
                }
                drawOWMScreen();
            }
            break;
        case SCREEN_UDP_FORECAST:
            if (!forecastScreen.isActive()) showForecastScreen();
            forecastScreen.tick();
            forecastScreen.handleIR(getIRCodeNonBlocking());
            break;
        case SCREEN_UDP_DATA:
            if (!udpScreen.isActive()) showUdpScreen();
            udpScreen.tick();
            udpScreen.handleIR(getIRCodeNonBlocking());
            break;
    }
    delay(0);
}
