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
#include <WiFi.h>
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
const ScreenMode InfoScreenModes[] = {SCREEN_OWM, SCREEN_UDP_DATA, SCREEN_UDP_FORECAST, 
                                      SCREEN_RAPID_WIND, SCREEN_WIND_DIR, SCREEN_AIR_QUALITY, 
                                      SCREEN_TEMP_HUM_BARO, SCREEN_CURRENT, SCREEN_HOURLY, SCREEN_CLOCK};
const int NUM_INFOSCREENS = sizeof(InfoScreenModes) / sizeof(ScreenMode);

// --- Global system state ---
ScreenMode currentScreen = SCREEN_CLOCK;

// --- Modal objects ---
extern InfoModal sysInfoModal, wifiInfoModal, dateModal, mainMenuModal, deviceModal, displayModal, weatherModal, calibrationModal, systemModal;

InfoScreen udpScreen("Live Weather", SCREEN_UDP_DATA);
InfoScreen forecastScreen("7-Day Fcst", SCREEN_UDP_FORECAST);
InfoScreen rapidWindScreen("Rapid Wind", SCREEN_RAPID_WIND);
InfoScreen airQualityScreen("Air Quality", SCREEN_AIR_QUALITY);
InfoScreen tempHumBaroScreen("Climate Data", SCREEN_TEMP_HUM_BARO);
InfoScreen currentCondScreen("Current", SCREEN_CURRENT);
InfoScreen hourlyScreen("24-HR Fcst", SCREEN_HOURLY);

// Screen Wind Info
WindMeter windMeter;
ScrollLine scrollLine(64, 40); // 64 px wide, scroll every 50ms
ScrollLine windInfo(64, 40);

// Screen Clock


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
extern SensirionI2cScd4x scd4x;
extern Adafruit_AHTX0 aht20;
extern Adafruit_BMP280 bmp280;

Preferences preferences;
int menuIndex = 0;
bool inSetupMenu = false;
WiFiUDP udp;
const int localPort = 50222;

static bool startupCompleted = false;
bool initialSetupAwaitingWifi = false;
static bool udpListening = false;

static void completeStartupAfterWiFi(bool force = false);
void handleInitialSetupDecision(bool wantsWiFi);

// === Display Timers ===

unsigned long prevMillis_ShowTimeDate = 0;
unsigned long lastBrightnessRead = 0;
unsigned long lastButtonCheck = 0;

const long interval_ShowTimeDate = 1000;
const unsigned long brightnessInterval = 5000;
const unsigned long buttonInterval = 100;

// === Sensors ===
unsigned long lastReadSCD40 = 0;
const unsigned long SCD40ReadInterval = 5000;
unsigned long lastReadAHT20_BMP280 = 0;
const unsigned long AHT20_BMP280ReadInterval = 5000;


// CLock Screen
unsigned long lastClockUpdate = 0;

// unsigned long lastReadDHT = 0;
// const unsigned long DHTreadInterval = 2000;

// --- Weather/scroll tick ---
void scrollWeatherTick()
{
    static unsigned long lastTick = 0;
    if (millis() - lastTick >= 40)
    {
        lastTick = millis();
        scrollWeatherDetails();
    }
}

// --- Hide all InfoScreens (convenience) ---
void hideAllInfoScreens()
{
    udpScreen.hide();
    forecastScreen.hide();
    rapidWindScreen.hide();
    tempHumBaroScreen.hide();
    currentCondScreen.hide();
    hourlyScreen.hide();
    airQualityScreen.hide(); // ??? add this
    // Add more InfoScreens here as needed
}

static unsigned long lastAutoRotateMillis = 0;

static bool isRotationBlocked()
{
    return inKeyboardMode ||
           menuActive ||
           wifiSelecting ||
           sysInfoModal.isActive() ||
           wifiInfoModal.isActive() ||
           dateModal.isActive() ||
           mainMenuModal.isActive() ||
           deviceModal.isActive() ||
           displayModal.isActive() ||
           weatherModal.isActive() ||
           calibrationModal.isActive() ||
           systemModal.isActive();
}

static void renderScreenContents(ScreenMode mode)
{
    switch (mode)
    {
    case SCREEN_OWM:
        drawOWMScreen();
        break;
    case SCREEN_UDP_DATA:
        udpScreen.tick();
        break;
    case SCREEN_UDP_FORECAST:
        forecastScreen.tick();
        break;
    case SCREEN_RAPID_WIND:
        rapidWindScreen.tick();
        break;
    case SCREEN_WIND_DIR:
        showWindDirectionScreen();
        break;
    case SCREEN_AIR_QUALITY:
        airQualityScreen.tick();
        break;
    case SCREEN_TEMP_HUM_BARO:
        tempHumBaroScreen.tick();
        break;
    case SCREEN_CURRENT:
        currentCondScreen.tick();
        break;
    case SCREEN_HOURLY:
        hourlyScreen.tick();
        break;
    case SCREEN_CLOCK:
        drawClockScreen();
        break;
    default:
        break;
    }
}

static void playScreenRevealEffect(ScreenMode mode)
{
    uint8_t original = currentPanelBrightness;
    if (original == 0)
    {
        original = map(brightness, 1, 100, 3, 255);
        if (original == 0)
            original = 30;
    }

    setPanelBrightness(original);
    renderScreenContents(mode);
}

static void noteScreenRotation(unsigned long now)
{
    lastAutoRotateMillis = now;
}

// --- Screen rotation handler ---
void rotateScreen(int direction)
{
    int idx = -1;
    for (int i = 0; i < NUM_INFOSCREENS; ++i)
    {
        if (InfoScreenModes[i] == currentScreen)
        {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return;

    int nextIdx = (idx + direction + NUM_INFOSCREENS) % NUM_INFOSCREENS;
    ScreenMode next = InfoScreenModes[nextIdx];

    hideAllInfoScreens();
    currentScreen = next;

    switch (currentScreen)
    {
    case SCREEN_UDP_DATA:
        showUdpScreen();
        break;
    case SCREEN_UDP_FORECAST:
        showForecastScreen();
        break;
    case SCREEN_RAPID_WIND:
        showRapidWindScreen();
        break;
    case SCREEN_WIND_DIR:
        showWindDirectionScreen();
        break;
    case SCREEN_AIR_QUALITY:
        showAirQualityScreen();
        break;
    case SCREEN_TEMP_HUM_BARO:
        showTempHumBaroScreen();
        break;
    case SCREEN_CURRENT:
        showCurrentConditionsScreen();
        break;
    case SCREEN_HOURLY:
        showHourlyForecastScreen();
        break;
    case SCREEN_CLOCK:
        drawClockScreen();
        break;  
    case SCREEN_OWM: /* draw in loop */
        break;
    }

    playScreenRevealEffect(currentScreen);
    noteScreenRotation(millis());
}

static void handleAutoRotate(unsigned long now)
{
    if (autoRotate == 0)
    {
        noteScreenRotation(now);
        return;
    }
    if (isRotationBlocked())
    {
        noteScreenRotation(now);
        return;
    }
    unsigned long intervalMs = (autoRotateInterval > 0)
        ? static_cast<unsigned long>(autoRotateInterval) * 1000UL
        : 15000UL;
    if (now - lastAutoRotateMillis >= intervalMs)
    {
        rotateScreen(+1);
    }
}

// --- Button reset logic ---
void handleResetButton()
{
    static bool buttonWasDown = false;
    static unsigned long buttonDownMillis = 0;
    static bool resetLongPressHandled = false;
    const unsigned long resetHoldTime = 3000;
    bool buttonDown = (digitalRead(BTN_SEL) == LOW);

    if (buttonDown && !buttonWasDown)
    {
        buttonDownMillis = millis();
        resetLongPressHandled = false;
        buttonWasDown = true;
    }
    if (buttonDown && !resetLongPressHandled)
    {
        if (millis() - buttonDownMillis > resetHoldTime)
        {
            resetLongPressHandled = true;
            triggerPhysicalReset();
        }
    }
    if (!buttonDown && buttonWasDown)
    {
        buttonWasDown = false;
        resetLongPressHandled = false;
    }
}

static void completeStartupAfterWiFi(bool force)
{
    if (startupCompleted && !force)
        return;

    bool wifiOk = WiFi.status() == WL_CONNECTED;
    if (!force && !wifiOk)
        return;

    startupCompleted = true;
    initialSetupAwaitingWifi = false;

    if (wifiOk)
    {
        WiFi.setSleep(false);
        Serial.println("WiFi done.");
        ArduinoOTA.setHostname("ESP32-Weather");
        ArduinoOTA.begin();
        setupWebServer();
        Serial.println("Displaying Time...");
        if (!syncTimeFromNTP())
        {
            Serial.println("[Setup] Initial NTP sync failed.");
        }
    }
    else
    {
        Serial.println("[Setup] Starting without WiFi connection.");
        if (udpListening)
        {
            udp.stop();
            udpListening = false;
        }
    }

    Serial.println("Done.");

    if (wifiOk)
    {
        if (udp.begin(localPort))
        {
            udpListening = true;
            Serial.printf("Listening for Tempest on UDP port %d\n", localPort);
        }
        else
        {
            udpListening = false;
            Serial.printf("Failed to bind UDP port %d\n", localPort);
        }

        fetchWeatherFromOWM();
        delay(500);
    }

    getTimeFromRTC();

    if (wifiOk)
    {
        fetchTempestData();
        fetchForecastData();
    }

    reset_Time_and_Date_Display = true;

    displayWeatherData();
    displayClock();
    displayDate();
    requestScrollRebuild();
    serviceScrollRebuild();

    delay(2000);
    setupButtons();

    currentMenuLevel = MENU_MAIN;
    currentMenuIndex = 0;
    menuActive = false;
    menuScroll = 0;
    wifiSelecting = false;

    scrollLine.setTitleText("Wind Info");
    scrollLine.setTitleColors(INFOSCREEN_HEADERFG, INFOSCREEN_HEADERBG);
    scrollLine.setTitleMode(true);

    windInfo.setTitleMode(false);
    windInfo.setLineScrollDirection(0, 1);
    String lines[] = {"This is the line for all wind related data."};
    windInfo.setLines(lines, 1);
    windInfo.setBounceEnabled(false);
    noteScreenRotation(millis());

    lastReadSCD40        = millis() - 0;
    lastReadAHT20_BMP280 = millis() - 1500;
    lastBrightnessRead   = millis() - 3000;

    if (!setupComplete)
    {
        markSetupComplete(true);
    }
}

void handleInitialSetupDecision(bool wantsWiFi)
{
    if (wantsWiFi)
    {
        wifiSelecting = true;
        currentMenuLevel = MENU_WIFI_SELECT;
        menuActive = true;
        menuScroll = 0;
        wifiSelectIndex = 0;
        scanWiFiNetworks();
        drawMenu();
        initialSetupAwaitingWifi = true;
        return;
    }

    wifiSelecting = false;
    menuActive = false;
    markSetupComplete(true);
    initialSetupAwaitingWifi = false;
    completeStartupAfterWiFi(true);
}

void setup()
{
    Serial.begin(115200);
    delay(100);

    Serial.printf("Flash size: %u bytes\n", ESP.getFlashChipSize());
    loadSettings();
    delay(500);
    setupDisplay();
    Serial.println("Display setup done.");

     // Setup Sensors
    setupSensors();

    Serial.println("Setup IR Sensor");
    setupIRSensor();

    // Initialise RTC once sensors/I2C are ready
    rtcReady = rtc.begin();
    if (!rtcReady)
    {
        Serial.println("?????? RTC not detected; time will default to 00:00");
    }
    else
    {
        Serial.println("RTC initialised successfully.");
    }

    Serial.println("\nESP32 Weather Display");

    // Ensure TCP/IP stack is initialised even if we stay offline
    WiFi.mode(WIFI_STA);

    if (initialSetupRequired)
    {
        Serial.println("[Setup] Initial configuration required.");
        showInitialSetupPrompt();
        return;
    }

    if (wifiSSID.isEmpty() || wifiPass.isEmpty())
    {
        Serial.println("[WiFi] No credentials, starting offline mode.");
        initialSetupAwaitingWifi = false;
        completeStartupAfterWiFi(true);
        return;
    }

    Serial.println("Connecting WiFi...");
    connectToWiFi();

    if (wifiSelecting)
        return;

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[WiFi] Connection failed, showing WiFi menu...");
        onWiFiConnectFailed();
        return;
    }

    completeStartupAfterWiFi(false);

    // Seed timers to stagger 5s jobs (spread ~1.5s apart)
    lastReadSCD40           = millis() - 0;      // first at ~5.0s
    lastReadAHT20_BMP280    = millis() - 1500;   // first at ~3.5s + 5s = 6.5s
    lastBrightnessRead      = millis() - 3000;   // first at ~2.0s + 5s = 7.0s

}

void loop()
{
    unsigned long now = millis();
    static unsigned long lastForecast = 0;
    static unsigned long lastBlink = 0;
    const unsigned long blinkInterval = 500;

    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    if ((initialSetupAwaitingWifi || !startupCompleted) && wifiConnected)
    {
        completeStartupAfterWiFi(initialSetupAwaitingWifi);
    }
    else if (!wifiConnected && udpListening)
    {
        udp.stop();
        udpListening = false;
    }

    static ScreenMode lastScreen = SCREEN_CLOCK; // or whatever your default is
    static bool needsClear = false;
    if (currentScreen != lastScreen)
    {
        needsClear = true;
        lastScreen = currentScreen;
    }

    // === [1] --- Always-on background tasks --- ===
    // --- Fetch new forecast data every 15 minutes ---
    if (wifiConnected && (now - lastForecast > 15 * 60 * 1000))
    {
        fetchForecastData();
        lastForecast = now;
    }

    // --- Main background tasks ---
    if (wifiConnected)
    {
        ArduinoOTA.handle();
    }

    if (udpListening)
    {
        fetchTempestData();
    }

    // --- Brightness control ---
    static unsigned long lastBrightnessRead = 0;
    if (now - lastBrightnessRead >= 5000)
    {
        lastBrightnessRead = now;
        float lux = readBrightnessSensor();
        if (autoBrightness)
        {
            setDisplayBrightnessFromLux(lux);
        }
        else
        {
            int hwBrightness = map(brightness, 1, 100, 3, 255);
            setPanelBrightness(hwBrightness);
            Serial.printf("Manual Brightness: %d%% -> %d\n", brightness, hwBrightness);
        }
    }

    /*
    if( now - lastReadDHT > DHTreadInterval){
        lastReadDHT = now;
        readDHTSensor();
    }
    */

    // --- Read SCD40 ---
    if (now - lastReadSCD40 > SCD40ReadInterval)
    {
        lastReadSCD40 = now;
        newAirQualityData = true;
        readSCD40();
    }

    // --- Read AHT20 and BMP280 Sensor ---
    if (now - lastReadAHT20_BMP280 > AHT20_BMP280ReadInterval)
    {
        lastReadAHT20_BMP280 = now;
        newAHT20_BMP280Data = true;
        readBMP280();
        readAHT20();
    }

    // === [2] --- UI/modal/menu/infoscreen handling --- ===

    // Only check physical reset if NO modal, NO keyboard, and NOT in WiFi select
    if (
        !inKeyboardMode &&
        !setupPromptModal.isActive() &&
        !sysInfoModal.isActive() &&
        !wifiInfoModal.isActive() &&
        !dateModal.isActive() &&
        !mainMenuModal.isActive() &&
        !deviceModal.isActive() &&
        !displayModal.isActive() &&
        !weatherModal.isActive() &&
        !calibrationModal.isActive() &&
        !systemModal.isActive() &&
        !(wifiSelecting && currentMenuLevel == MENU_WIFI_SELECT))
    {
        handleResetButton();
    }

    handleAutoRotate(now);

    // --- 1. Keyboard always has focus if active ---
    if (inKeyboardMode)
    {
        uint32_t code = getIRCodeNonBlocking();
        if (code)
            handleKeyboardIR(code);

        if (now - lastBlink >= blinkInterval)
        {
            lastBlink = now;
            keyboardBlinkTick();
        }
        tickKeyboard();
        delay(40);
        return;
    }

    // --- 2. If any modal is active, route IR input ONLY to modal ---
    if (setupPromptModal.isActive())
    {
        setupPromptModal.tick();
        setupPromptModal.handleIR(getIRCodeNonBlocking());
        delay(40);
        return;
    }

    if (sysInfoModal.isActive())
    {
        sysInfoModal.tick();
        sysInfoModal.handleIR(getIRCodeNonBlocking());
        delay(40);
        return;
    }
    if (wifiInfoModal.isActive())
    {
        wifiInfoModal.tick();
        wifiInfoModal.handleIR(getIRCodeNonBlocking());
        delay(40);
        return;
    }
    if (dateModal.isActive())
    {
        dateModal.tick();
        dateModal.handleIR(getIRCodeNonBlocking());
        delay(40);
        return;
    }
    if (mainMenuModal.isActive())
    {
        mainMenuModal.tick();
        mainMenuModal.handleIR(getIRCodeNonBlocking());
        delay(40);
        return;
    }
    if (deviceModal.isActive())
    {
        deviceModal.tick();
        deviceModal.handleIR(getIRCodeNonBlocking());
        delay(40);
        return;
    }
    if (displayModal.isActive())
    {
        displayModal.tick();
        displayModal.handleIR(getIRCodeNonBlocking());
        delay(40);
        return;
    }
    if (weatherModal.isActive())
    {
        weatherModal.tick();
        weatherModal.handleIR(getIRCodeNonBlocking());
        delay(40);
        return;
    }
    if (calibrationModal.isActive())
    {
        calibrationModal.tick();
        calibrationModal.handleIR(getIRCodeNonBlocking());
        delay(40);
        return;
    }
    if (systemModal.isActive())
    {
        systemModal.tick();
        systemModal.handleIR(getIRCodeNonBlocking());
        delay(40);
        return;
    }

    // --- 3. Handle WiFi SSID selection menu ---
    if (wifiSelecting && currentMenuLevel == MENU_WIFI_SELECT)
    {
        uint32_t code = getIRCodeNonBlocking();
        if (code)
            handleIR(code); // Route IR to menu.cpp WiFi handler
        drawWiFiMenu();     // Draw scanned WiFi list
        delay(80);          // Slight delay for smoother response
        return;             // Skip rest of loop while selecting WiFi
    }

    // --- 4. Pending modal delayed calls ---
    if (pendingModalFn && millis() >= pendingModalTime)
    {
        void (*fn)() = pendingModalFn;
        pendingModalFn = nullptr;
        pendingModalTime = 0;
        fn();
        return;
    }

    // --- 5. InfoScreens (live screens, auto-refresh if new data) ---
    if (udpScreen.isActive())
    {
        if (newTempestData)
        {
            showUdpScreen();
            newTempestData = false;
        }
        uint32_t code = getIRCodeNonBlocking();
        if (code == IR_CANCEL)
        {
            hideAllInfoScreens();
            showMainMenuModal();
            playBuzzerTone(3000, 100);
            return;
        }
        udpScreen.tick();
        udpScreen.handleIR(code);
        delay(40);
        return;
    }

    if (forecastScreen.isActive())
    {
        uint32_t code = getIRCodeNonBlocking();
        if (code == IR_CANCEL)
        {
            hideAllInfoScreens();
            showMainMenuModal();
            playBuzzerTone(3000, 100);
            return;
        }
        forecastScreen.tick();
        forecastScreen.handleIR(code);
        delay(40);
        return;
    }

    if (currentCondScreen.isActive())
    {
        uint32_t code = getIRCodeNonBlocking();
        if (code == IR_CANCEL)
        {
            hideAllInfoScreens();
            showMainMenuModal();
            playBuzzerTone(3000, 100);
            return;
        }
        currentCondScreen.tick();
        currentCondScreen.handleIR(code);
        delay(40);
        return;
    }

    if (hourlyScreen.isActive())
    {
        uint32_t code = getIRCodeNonBlocking();
        if (code == IR_CANCEL)
        {
            hideAllInfoScreens();
            showMainMenuModal();
            playBuzzerTone(3000, 100);
            return;
        }
        hourlyScreen.tick();
        hourlyScreen.handleIR(code);
        delay(40);
        return;
    }

    if (rapidWindScreen.isActive())
    {
        if (newRapidWindData)
        {
            showRapidWindScreen();
            newRapidWindData = false;
        }
        uint32_t code = getIRCodeNonBlocking();
        if (code == IR_CANCEL)
        {
            hideAllInfoScreens();
            showMainMenuModal();
            playBuzzerTone(3000, 100);
            return;
        }
        rapidWindScreen.tick();
        rapidWindScreen.handleIR(code);
        delay(40);
        return;
    }

    if (airQualityScreen.isActive())
    {
        if (newAirQualityData)
        {
            showAirQualityScreen();
            newAirQualityData = false;
        }
        uint32_t code = getIRCodeNonBlocking();
        if (code == IR_CANCEL)
        {
            hideAllInfoScreens();
            showMainMenuModal();
            playBuzzerTone(3000, 100);
            return;
        }
        airQualityScreen.tick();
        airQualityScreen.handleIR(code);
        delay(40);
        return;
    }

    if (tempHumBaroScreen.isActive())
    {
        if (newAHT20_BMP280Data)
        {
            showTempHumBaroScreen();
            newAHT20_BMP280Data = false;
        }
        uint32_t code = getIRCodeNonBlocking();
        if (code == IR_CANCEL)
        {
            hideAllInfoScreens();
            showMainMenuModal();
            playBuzzerTone(3000, 100);
            return;
        }
        tempHumBaroScreen.tick();
        tempHumBaroScreen.handleIR(code);
        delay(40);
        return;
    }

    // --- 6. No modal/menu/keyboard/InfoScreen active: handle IR for menu or screen rotation ---
    uint32_t code = getIRCodeNonBlocking();
    if (code == IR_LEFT)
    {
        rotateScreen(-1);
        return;
    }
    if (code == IR_RIGHT)
    {
        rotateScreen(+1);
        return;
    }
    if (code == IR_CANCEL)
    {
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
        forecastScreen.isActive() ||
        airQualityScreen.isActive() ||
        tempHumBaroScreen.isActive();

if (currentScreen == SCREEN_OWM && !anyModalOrInfoScreenActive)
{
    // 1) Detect unit changes (C/F, mph/kph/kts/mps, inHg/hPa, inch/mm, 12h/24h)
    notifyUnitsMaybeChanged();

    // 2) Rebuild marquee once if needed (after unit/data changes)
    serviceScrollRebuild();

    // 3) Animate at fixed cadence independent of the 1s clock redraw
    scrollWeatherTick();

    if (reset_Time_and_Date_Display)
    {
        reset_Time_and_Date_Display = false;
        displayClock();
        displayDate();
        displayWeatherData();
    }

    static unsigned long prevMillis_ShowTimeDate = 0;
    if (now - prevMillis_ShowTimeDate >= 1000)
    {
        reset_Time_and_Date_Display = true;
        prevMillis_ShowTimeDate = now;
        if (needsClear)
        {
            dma_display->fillScreen(0);
            needsClear = false;
        }
        drawOWMScreen();   // <- no createScrollingText() call inside this anymore
    }
}


    // --- 8. InfoScreen auto-activation (if not already active) ---
    switch (currentScreen)
    {
    case SCREEN_UDP_FORECAST:
        if (!forecastScreen.isActive())
            showForecastScreen();
        break;
    case SCREEN_UDP_DATA:
        if (!udpScreen.isActive())
            showUdpScreen();
        break;
    case SCREEN_RAPID_WIND:
        if (!rapidWindScreen.isActive())
            showRapidWindScreen();
        break;
    case SCREEN_CURRENT:
        if (!currentCondScreen.isActive())
            showCurrentConditionsScreen();
        break;

    case SCREEN_HOURLY:
        if (!hourlyScreen.isActive())
            showHourlyForecastScreen();
        break;

    case SCREEN_AIR_QUALITY:
        if (!airQualityScreen.isActive())
            showAirQualityScreen();
        break;
    case SCREEN_TEMP_HUM_BARO:
        if (!tempHumBaroScreen.isActive())
            showTempHumBaroScreen();
        break;

    default:
        break;
    }

    if( currentScreen == SCREEN_CLOCK){
        // Handle clock screen updates
        static unsigned long lastClockUpdate = 0;
        if (now - lastClockUpdate >= 1000) // Update every second
        {
            lastClockUpdate = now;
            drawClockScreen(); // Redraw clock screen
        }
    }




    if (currentScreen == SCREEN_WIND_DIR)
    {
        static unsigned long lastDataUpdate = 0;
        static unsigned long lastFrameUpdate = 0;
        const unsigned long dataUpdateInterval = 3000; // 3 seconds
        const unsigned long frameUpdateInterval = 40;  // 20 FPS
        unsigned long now = millis();

        // Update data every 3 seconds or when new rapid wind data arrives
        if (newRapidWindData || (now - lastDataUpdate) > dataUpdateInterval)
        {
            // Just clear the flag, don't redraw full screen here
            newRapidWindData = false;
            lastDataUpdate = now;
        }
        // Animate and redraw frame every ~50 ms
        if (now - lastFrameUpdate > frameUpdateInterval)
        {
            showWindDirectionScreen(); // Draw frame with animation
            lastFrameUpdate = now;
        }
        // IR input handling for this screen
        uint32_t code = getIRCodeNonBlocking();
        if (code)
        {
            if (code == IR_LEFT)
            {
                rotateScreen(-1);
                return;
            }
            if (code == IR_RIGHT)
            {
                rotateScreen(+1);
                return;
            }
            if (code == IR_CANCEL)
            {
                showMainMenuModal();
                return;
            }
        }

        delay(10);
        return;
    }

    delay(0);
}


