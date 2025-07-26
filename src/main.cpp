#include <Arduino_JSON.h>
#include "time.h"
#include "RTClib.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include "display.h"
#include "pins.h"
#include "button.h"
#include "utils.h"
#include "icons.h"
#include "sensors.h"

#include <IRremoteESP8266.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Update.h>
#include "settings.h"
#include "web.h"
#include "buzzer.h"
#include "menu.h"
#include "keyboard.h"


// Make these global
ScreenMode currentScreen = ScreenMode::SCREEN_OWM;
const int SCREEN_COUNT = 5; // Update if you add/remove screens

extern InfoModal sysInfoModal;
extern InfoModal wifiInfoModal;
extern InfoModal dateModal;

extern int wifiSelectIndex;

extern unsigned long lastMenuActivity;
extern int menuScroll;
bool menuActive = false;
Preferences preferences;
int menuIndex = 0;
bool inSetupMenu = false;
WiFiUDP udp;
const int localPort = 50222;

// === Display Colors ===
uint16_t dayColor = dma_display->color565(255, 170, 51);
uint16_t nightColor = dma_display->color565(255, 255, 255);
String colorMode = "color";

// === Display Timers ===
unsigned long prevMillis_ShowTimeDate = 0;
const long interval_ShowTimeDate = 1000;
unsigned long lastBrightnessRead = 0;
unsigned long lastDHTRead = 0;
unsigned long lastIRCheck = 0;
unsigned long lastButtonCheck = 0;
unsigned long lastScreenSwitch = 0;
const unsigned long brightnessInterval = 5000;
const unsigned long dhtInterval = 10000;
const unsigned long irInterval = 50;
const unsigned long buttonInterval = 100;
const unsigned long screenInterval = 10000;


// --- Functions ---
void fetchTempestData() {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
        char packet[1024];
        int len = udp.read(packet, sizeof(packet) - 1);
        if (len > 0) {
            packet[len] = 0;
            JSONVar doc = JSON.parse(packet);
            if (JSON.typeof(doc) == "undefined") {
                Serial.println("Failed to parse Tempest UDP");
                return;
            }
            String type = (const char *)doc["type"];
            if (type == "rapid_wind") {
                Serial.printf("Wind: %.2f m/s from %.1f°\n",
                              (double)doc["ob"][1], (double)doc["ob"][2]);
            }
            else if (type == "obs_st") {
                JSONVar obs = doc["obs"][0];
                Serial.printf("Air Temp: %.2f°C | Hum: %.1f%%\n",
                              (double)obs[7], (double)obs[8]);
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);
    loadSettings();
    delay(500);
    setupDisplay();
    Serial.println("Display setup done.");

    Serial.println("Setup IR Sensor");
    setupIRSensor();

    Serial.println("\nESP32 Weather Display");

    // ---- WiFi credential check ----
    if (wifiSSID.isEmpty() || wifiPass.isEmpty()) {
        Serial.println("[WiFi] No credentials, showing WiFi menu...");
        onWiFiConnectFailed(); // Show WiFi select menu immediately
        return;                // Pause setup until WiFi is configured
    }

    Serial.println("Connecting WiFi...");
    connectToWiFi();

    // If WiFi menu is open, pause rest of setup (user needs to pick network)
    if (wifiSelecting)
        return;

    // Extra connection check (in case connectToWiFi fails to connect)
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Connection failed, showing WiFi menu...");
        onWiFiConnectFailed();
        return;
    }

    Serial.println("WiFi done.");

    // OTA setup
    ArduinoOTA.setHostname("ESP32-Weather");
    ArduinoOTA.begin();

    setupWebServer();
    Serial.println("Displaying Time...");
    syncTimeFromNTP1();
    Serial.println("Done.");

    // UDP setup for Tempest integration
    udp.begin(localPort);
    Serial.printf("Listening for Tempest on UDP port %d\n", localPort);

    // OpenWeatherMap API setup
    fetchWeatherFromOWM();
    delay(500);
    getTimeFromRTC();

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

    static unsigned long lastBlink = 0;
    const unsigned long blinkInterval = 500; // ms

    // --- Physical Reset Button (5 sec long press) ---
    bool buttonDown = (digitalRead(BTN_SEL) == LOW);
    if (buttonDown && !buttonWasDown) {
        // Button just pressed
        buttonDownMillis = millis();
        resetLongPressHandled = false;
        buttonWasDown = true;
    }
    if (buttonDown && !resetLongPressHandled) {
        if (millis() - buttonDownMillis > resetHoldTime) {
            // Long press detected, trigger reset
            resetLongPressHandled = true;
            triggerPhysicalReset();
        }
    }
    if (!buttonDown && buttonWasDown) {
        // Button release
        buttonWasDown = false;
        resetLongPressHandled = false;
    }

    // --- Always check IR sensor ---
    if (now - lastIRCheck >= irInterval) {
        lastIRCheck = now;
        readIRSensor();
    }

    // --- System Info Modal ---
    if (sysInfoModal.isActive()) {
        sysInfoModal.tick();
        delay(40);
        return;
    }

    if (wifiInfoModal.isActive()) {
        wifiInfoModal.tick();
        delay(40);
        return;
    }

    if (dateModal.isActive()) {
        dateModal.tick();
        delay(40);
        return;
    }

    if (inKeyboardMode && now - lastBlink >= blinkInterval) {
        lastBlink = now;
        keyboardBlinkTick();
    }

    if (now - lastButtonCheck >= buttonInterval) {
        lastButtonCheck = now;
        getButton();
    }

    if (wifiSelecting) {
        updateMenu();
        delay(60);
        return;
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
        } else {
            lastAutoReturnCheck = lastMenuActivity;
        }
        delay(100);
        return;
    }

    // --- Main background tasks (run only if not in menu or wifi selection) ---

    ArduinoOTA.handle();
    scrollWeatherDetails();
    fetchTempestData();

    if (now - lastBrightnessRead >= brightnessInterval) {
        lastBrightnessRead = now;
        float lux = readBrightnessSensor();
        if (autoBrightness) {
            setDisplayBrightnessFromLux(lux);
        } else {
            int hardwareBrightness = map(brightness, 1, 100, 3, 255);
            dma_display->setBrightness8(hardwareBrightness);
            Serial.printf("Manual Brightness: %d%% -> %d\n", brightness, hardwareBrightness);
        }
    }

    if (reset_Time_and_Date_Display) {
        reset_Time_and_Date_Display = false;
        displayClock();
        displayDate();
        displayWeatherData();
    }

    // ---- Draw the selected screen ----
    switch (currentScreen) {
    case SCREEN_OWM:
        if (now - prevMillis_ShowTimeDate >= interval_ShowTimeDate) {   
            reset_Time_and_Date_Display = true;
            prevMillis_ShowTimeDate = now;
            drawOWMScreen();
        }
        // Draw rolling text line at top
        
        break;
    case SCREEN_CLOCK:
        drawClockScreen();
        break;
    case SCREEN_WEATHER:
        drawWeatherScreen();
        break;
    case SCREEN_UDP:
        drawUdpDataScreen();
        break;
    case SCREEN_SETTINGS:
        drawSettingsScreen();
        break;
    }
    delay(80);
}
