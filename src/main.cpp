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
const int SCREEN_COUNT = 5;

extern InfoModal sysInfoModal;
extern InfoModal wifiInfoModal;
extern InfoModal dateModal;
extern InfoModal mainMenuModal;
extern InfoModal deviceModal;
extern InfoModal displayModal;
extern InfoModal weatherModal;
extern InfoModal calibrationModal;
extern InfoModal systemModal;

extern void (*pendingModalFn)();
extern unsigned long pendingModalTime;

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

    // Handle reset button (physical long-press)
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

    // IR input
    if (now - lastIRCheck >= irInterval) {
        lastIRCheck = now;
        readIRSensor();
    }

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

    // Modals
    if (sysInfoModal.isActive()) { sysInfoModal.tick(); delay(40); return; }
    if (wifiInfoModal.isActive()) { wifiInfoModal.tick(); delay(40); return; }
    if (dateModal.isActive())     { dateModal.tick(); delay(40); return; }
    if (mainMenuModal.isActive()) { mainMenuModal.tick(); delay(40); return; }
    if (deviceModal.isActive())   { deviceModal.tick(); delay(40); return; }
    if (displayModal.isActive())  { displayModal.tick(); delay(40); return; }
    if (weatherModal.isActive())  { weatherModal.tick(); delay(40); return; }
    if (calibrationModal.isActive()) { calibrationModal.tick(); delay(40); return; }
    if (systemModal.isActive()) { systemModal.tick(); delay(40); return; }

    if (now - lastButtonCheck >= buttonInterval) {
        lastButtonCheck = now;
        getButton();
    }

    if (wifiSelecting) {
        updateMenu();
        delay(60);
        return;
    }


if (pendingModalFn && millis() >= pendingModalTime) {
    void (*fn)() = pendingModalFn; // copy before clearing
    pendingModalFn = nullptr;
    pendingModalTime = 0;
    fn(); // call modal function
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

    // Background tasks
    ArduinoOTA.handle();
    scrollWeatherDetails();
    fetchTempestData();

    // Brightness
    if (now - lastBrightnessRead >= brightnessInterval) {
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
        case SCREEN_CLOCK: drawClockScreen(); break;
        case SCREEN_WEATHER: drawWeatherScreen(); break;
        case SCREEN_UDP: drawUdpDataScreen(); break;
        case SCREEN_SETTINGS: drawSettingsScreen(); break;
    }

    delay(80);
}
