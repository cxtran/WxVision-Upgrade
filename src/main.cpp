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
#include "windmeter.h"
#include "ScrollLine.h"

// --- Screen rotation: add or remove as needed ---
const ScreenMode InfoScreenModes[] = { SCREEN_OWM, SCREEN_UDP_DATA, SCREEN_UDP_FORECAST,SCREEN_RAPID_WIND,SCREEN_WIND_DIR };
const int NUM_INFOSCREENS = sizeof(InfoScreenModes) / sizeof(ScreenMode);

// --- Global system state ---
ScreenMode currentScreen = SCREEN_OWM;

// --- Modal objects ---
extern InfoModal sysInfoModal, wifiInfoModal, dateModal, mainMenuModal, deviceModal, displayModal, weatherModal, calibrationModal, systemModal;

InfoScreen udpScreen("Live Weather", SCREEN_UDP_DATA);
InfoScreen forecastScreen("Forecast", SCREEN_UDP_FORECAST);
InfoScreen rapidWindScreen("Rapid Wind", SCREEN_RAPID_WIND);
WindMeter windMeter;
ScrollLine scrollLine(64, 40); // 64 px wide, scroll every 50ms
ScrollLine windInfo(64,40); 



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
    rapidWindScreen.hide(); 

    // Add more InfoScreens here as needed
}

// --- Screen rotation handler ---
void rotateScreen(int direction) {
    int idx = -1;
    for (int i = 0; i < NUM_INFOSCREENS; ++i) {
        if (InfoScreenModes[i] == currentScreen) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return;

    int nextIdx = (idx + direction + NUM_INFOSCREENS) % NUM_INFOSCREENS;
    ScreenMode next = InfoScreenModes[nextIdx];

    hideAllInfoScreens();
    currentScreen = next;

    switch (currentScreen) {
        case SCREEN_UDP_DATA: showUdpScreen(); break;
        case SCREEN_UDP_FORECAST: showForecastScreen(); break;
        case SCREEN_RAPID_WIND: showRapidWindScreen(); break;
        case SCREEN_WIND_DIR: showWindDirectionScreen(); break;
        case SCREEN_OWM: /* draw in loop */ break;
    }
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

    // Setup your display and ScrollLine...

    // Set lines with text
 //   String lines[] = {"Hello world!", "Second line", "Third line"};
  //  scrollLine.setLines(lines, 3);

    // Set per-line colors: white on black, green on blue, red on yellow
 //   uint16_t textColors[3] = {0xFFFF, 0x07E0, 0xF800};
 //   uint16_t bgColors[3] = {0x0000, 0x001F, 0xFFE0};
 //   scrollLine.setLineColors(textColors, bgColors, 3);

    // For title mode
    scrollLine.setTitleText("Wind Info");
    scrollLine.setTitleColors(INFOSCREEN_HEADERFG, INFOSCREEN_HEADERBG); // white on black
    scrollLine.setTitleMode(true);
    

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

    // === [1] --- Always-on background tasks --- ===
    // --- Fetch new forecast data every 15 minutes ---
    if (now - lastForecast > 15 * 60 * 1000) {
        fetchForecastData();
        lastForecast = now;
    }

    // --- Main background tasks ---
    ArduinoOTA.handle();
    fetchTempestData();

    // --- Brightness control ---
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

    // === [2] --- UI/modal/menu/infoscreen handling --- ===

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

    // --- 5. InfoScreens (live screens, auto-refresh if new data) ---
    if (udpScreen.isActive()) {
        if (newTempestData) {
            showUdpScreen();    // Rebuilds udpScreen with latest data
            newTempestData = false;
        }
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
    
    if (rapidWindScreen.isActive()) {
        if (newRapidWindData) {
            showRapidWindScreen(); // Only update when actual rapid_wind packet arrives!
            newRapidWindData = false;
        }
        rapidWindScreen.tick();
        rapidWindScreen.handleIR(getIRCodeNonBlocking());
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

    // --- 7. Update and render OWM screen ONLY if visible (no InfoScreen/modal active) ---
    bool anyModalOrInfoScreenActive =
        sysInfoModal.isActive() ||
        wifiInfoModal.isActive() ||
        dateModal.isActive() ||
        mainMenuModal.isActive() ||
        deviceModal.isActive() ||
        displayModal.isActive() ||
        weatherModal.isActive() ||
        calibrationModal.isActive() ||
        systemModal.isActive() ||
        inKeyboardMode ||
        udpScreen.isActive() ||
        forecastScreen.isActive();

    if (currentScreen == SCREEN_OWM && !anyModalOrInfoScreenActive) {
        scrollWeatherTick();
        if (reset_Time_and_Date_Display) {
            reset_Time_and_Date_Display = false;
            displayClock();
            displayDate();
            displayWeatherData();
        }

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
    }

    // --- 8. InfoScreen auto-activation (if not already active) ---
    switch (currentScreen) {
        case SCREEN_UDP_FORECAST:
            if (!forecastScreen.isActive()) showForecastScreen();
            break;
        case SCREEN_UDP_DATA:
            if (!udpScreen.isActive()) showUdpScreen();
            break;
        case SCREEN_RAPID_WIND:
            if (!rapidWindScreen.isActive()) showRapidWindScreen();
            break;

        default:
            break;
    }

    if (currentScreen == SCREEN_WIND_DIR) {
        static unsigned long lastDataUpdate = 0;
        static unsigned long lastFrameUpdate = 0;
        const unsigned long dataUpdateInterval = 3000;  // 3 seconds
        const unsigned long frameUpdateInterval = 40;   // 20 FPS



        unsigned long now = millis();





        // Update data every 3 seconds or when new rapid wind data arrives
        if (newRapidWindData || (now - lastDataUpdate) > dataUpdateInterval) {
            // Just clear the flag, don't redraw full screen here
            newRapidWindData = false;
            lastDataUpdate = now;

        }

        // Animate and redraw frame every ~50 ms
        if (now - lastFrameUpdate > frameUpdateInterval) {


            showWindDirectionScreen();  // Draw frame with animation
            lastFrameUpdate = now;
        }

        // IR input handling for this screen
        uint32_t code = getIRCodeNonBlocking();
        if (code) {
            if (code == IR_LEFT)  { rotateScreen(-1); return; }
            if (code == IR_RIGHT) { rotateScreen(+1); return; }
            if (code == IR_CANCEL) {
                showMainMenuModal();
                return;
            }
        }

        delay(10);
        return;
    }




    delay(0);
}
