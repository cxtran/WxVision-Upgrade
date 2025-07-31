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
extern InfoModal sysInfoModal;
extern InfoModal wifiInfoModal;
extern InfoModal dateModal;
extern InfoModal mainMenuModal;
extern InfoModal deviceModal;
extern InfoModal displayModal;
extern InfoModal weatherModal;
extern InfoModal calibrationModal;
extern InfoModal systemModal;

InfoScreen udpScreen("LIVE WEATHER", SCREEN_UDP_DATA);
InfoScreen forecastScreen("FORECAST", SCREEN_UDP_FORECAST);

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

void rotateScreen(int direction) {
    // Find current screen index
    int idx = -1;
    for (int i = 0; i < NUM_INFOSCREENS; ++i) {
        if (InfoScreenModes[i] == currentScreen) { idx = i; break; }
    }
    if (idx < 0) return; // Not found
    int nextIdx = (idx + direction + NUM_INFOSCREENS) % NUM_INFOSCREENS;
    ScreenMode next = InfoScreenModes[nextIdx];

    // Hide all InfoScreens so only one is ever open
    udpScreen.hide();
    forecastScreen.hide();

    currentScreen = next;

    // Show appropriate InfoScreen if needed
    if (currentScreen == SCREEN_UDP_DATA) {
        showUdpScreen();
    } else if (currentScreen == SCREEN_UDP_FORECAST) {
        showForecastScreen();
    }
    // OWM does NOT need a show, just handled in the main switch
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
    static unsigned long lastForecast = 0;
    if (millis() - lastForecast > 15 * 60 * 1000) {
        fetchForecastData();
        lastForecast = millis();
    }

    unsigned long now = millis();
    static unsigned long lastBlink = 0;
    const unsigned long blinkInterval = 500;

    // Handle reset button (physical long-press)
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

    // IR input for classic UI, not InfoScreen!
    if (now - lastButtonCheck >= buttonInterval) {
        lastButtonCheck = now;
        if (!udpScreen.isActive() && !forecastScreen.isActive()) {
            readIRSensor();
        }
    }

    // --- Global screen rotation with LEFT/RIGHT ---
    uint32_t code = getIRCodeNonBlocking();
    if (code == IR_LEFT)  { rotateScreen(-1); return; }
    if (code == IR_RIGHT) { rotateScreen(+1); return; }

    // Cursor blink timer for keyboard
    if (inKeyboardMode && now - lastBlink >= blinkInterval) {
        lastBlink = now;
        keyboardBlinkTick();
    }

    // Keyboard entry active
    if (inKeyboardMode) {
        tickKeyboard();
        delay(40);
        return;
    }

    // Modals (unchanged)
    if (sysInfoModal.isActive())        { sysInfoModal.tick();    delay(40);      return; }
    if (wifiInfoModal.isActive())       { wifiInfoModal.tick();   delay(40);      return; }
    if (dateModal.isActive())           { dateModal.tick();       delay(40);      return; }
    if (mainMenuModal.isActive())       { mainMenuModal.tick();   delay(40);      return; }
    if (deviceModal.isActive())         { deviceModal.tick();     delay(40);      return; }
    if (displayModal.isActive())        { displayModal.tick();    delay(40);      return; }
    if (weatherModal.isActive())        { weatherModal.tick();    delay(40);      return; }
    if (calibrationModal.isActive())    { calibrationModal.tick(); delay(40);     return; }
    if (systemModal.isActive())         { systemModal.tick();      delay(40);     return; }
    if (wifiSelecting)                  {  updateMenu();           delay(60);     return; }

    if (pendingModalFn && millis() >= pendingModalTime) {
        void (*fn)() = pendingModalFn;
        pendingModalFn = nullptr;
        pendingModalTime = 0;
        fn();
    }

    if (menuActive) {
        updateMenu();
        static unsigned long lastAutoReturnCheck = 0;
        if (millis() - lastMenuActivity > 30000 && lastMenuActivity == lastAutoReturnCheck) {
            if (currentMenuLevel != MENU_MAIN || currentMenuIndex != 0 || menuScroll != 0) {
                currentMenuLevel = MENU_MAIN;
                currentMenuIndex = 0;
                menuScroll = 0;
                drawMenu();
            }
        }
        else {
            lastAutoReturnCheck = lastMenuActivity;
        }
        delay(100);
        return;
    }

    // Weather scroll tick
    if (!menuActive) {
        scrollWeatherTick();
    }

    // -------- InfoScreen MODAL (blocks everything) ---------
    if (udpScreen.isActive()) {
        udpScreen.tick();
        udpScreen.handleIR(getIRCodeNonBlocking()); // Will handle [OK] to go back to OWM
        delay(40);
        return;
    }
    if (forecastScreen.isActive()) {
        forecastScreen.tick();
        forecastScreen.handleIR(getIRCodeNonBlocking());
        delay(40);
        return;
    }

    // ----------- Main background tasks ----------
    ArduinoOTA.handle();
    fetchTempestData();

    // Brightness
    if (now - lastBrightnessRead >= brightnessInterval) {
        lastBrightnessRead = now;
        float lux = readBrightnessSensor();
        if (autoBrightness) {
            setDisplayBrightnessFromLux(lux);
        }
        else {
            int hwBrightness = map(brightness, 1, 100, 3, 255);
            dma_display->setBrightness8(hwBrightness);
            Serial.printf("Manual Brightness: %d%% -> %d\n", brightness, hwBrightness);
        }
    }

    if (reset_Time_and_Date_Display) {
        reset_Time_and_Date_Display = false;
        displayClock();
        displayDate();
        displayWeatherData();
    }

    // Display screen content
    switch (currentScreen) {
    case SCREEN_OWM:
        if (now - prevMillis_ShowTimeDate >= interval_ShowTimeDate) {
            reset_Time_and_Date_Display = true;
            prevMillis_ShowTimeDate = now;
            drawOWMScreen();
        }
        break;
    case SCREEN_UDP_FORECAST:
        showForecastScreen(); // updates, then forecastScreen.show()
        break;
    case SCREEN_UDP_DATA:
        showUdpScreen();      // updates, then udpScreen.show()
        break;
    }
    delay(0);
}
