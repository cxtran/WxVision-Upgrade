#include <Arduino_JSON.h>
#ifdef typeof
#undef typeof
#endif
#include "time.h"
#include "RTClib.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <math.h>
#include "display.h"
#include "env_quality.h"
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
#include <ESPmDNS.h>
#include "settings.h"
#include "datetimesettings.h"
#include "config.h"
#include "wifisettings.h"
#include "web.h"
#include "buzzer.h"
#include "menu.h"
#include "alarm.h"
#include "noaa.h"
#include "keyboard.h"
#include "tempest.h"
#include "InfoScreen.h"
#include "windmeter.h"
#include "ScrollLine.h"
#include "system.h"
#include "datalogger.h"
#include "graph.h"
#include "graph.h"

// --- Screen rotation: add or remove as needed ---
const ScreenMode InfoScreenModes[] = {
    SCREEN_CLOCK,
    SCREEN_LUNAR_VI,
    SCREEN_OWM,
    SCREEN_UDP_DATA,
    SCREEN_UDP_FORECAST,
    SCREEN_WIND_DIR,
    SCREEN_ENV_INDEX,
    SCREEN_TEMP_HISTORY,
    SCREEN_HUM_HISTORY,
    SCREEN_CO2_HISTORY,
    SCREEN_BARO_HISTORY,
    SCREEN_PREDICT,
    SCREEN_NOAA_ALERT,
    SCREEN_CONDITION_SCENE,
    SCREEN_CURRENT,
    SCREEN_HOURLY};
const int NUM_INFOSCREENS = sizeof(InfoScreenModes) / sizeof(ScreenMode);

// --- Global system state ---
ScreenMode currentScreen = SCREEN_CLOCK;

// --- Modal objects ---
extern InfoModal wifiSettingsModal, sysInfoModal, wifiInfoModal, dateModal, mainMenuModal, deviceModal, displayModal, weatherModal, tempestModal, calibrationModal, systemModal, scenePreviewModal, unitSettingsModal, alarmModal;

InfoScreen udpScreen("Live Weather", SCREEN_UDP_DATA);
InfoScreen forecastScreen("Next 10 Days", SCREEN_UDP_FORECAST);
InfoScreen envQualityScreen("Air Quality", SCREEN_ENV_INDEX);
InfoScreen currentCondScreen("Current", SCREEN_CURRENT);
InfoScreen hourlyScreen("Next 24 HRS", SCREEN_HOURLY);
InfoScreen noaaAlertScreen("NOAA Alert", SCREEN_NOAA_ALERT);

// Screen Wind Info
WindMeter windMeter;
ScrollLine scrollLine(64, 40); // 64 px wide, scroll every 50ms
ScrollLine windInfo(64, 40);

// Screen Clock
extern void (*pendingModalFn)();
extern unsigned long pendingModalTime;
extern bool isWeatherScenePreviewActive();
extern void handleWeatherScenePreviewIR(uint32_t code);
extern int alarmSlotSelection;
extern int alarmSlotShown;

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

String deviceHostname;
static bool otaInitialized = false;
static bool mdnsRunning = false;
static bool lastWifiState = false;
static bool lastApState = false;

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
    envQualityScreen.hide();
    currentCondScreen.hide();
    hourlyScreen.hide();
    noaaAlertScreen.hide();
    // Add more InfoScreens here as needed
}

static void playScreenRevealEffect(ScreenMode mode);
static void noteScreenRotation(unsigned long now);
static void ensureCurrentScreenAllowed();
static void applyDataSourcePolicies(bool wifiConnected);

static unsigned long lastAutoRotateMillis = 0;

static String buildDefaultHostname()
{
    String base = MDNS_BASE_HOSTNAME;
    base.trim();
    if (base.isEmpty())
    {
        base = "visionwx";
    }
    base.toLowerCase();

    base.replace(" ", "-");

    // Append last 4 hex characters of the MAC address as a suffix
    String mac = WiFi.macAddress(); // e.g. "AA:BB:CC:DD:EE:FF"
    mac.replace(":", "");
    if (mac.length() >= 4)
    {
        String suffix = mac.substring(mac.length() - 4);
        suffix.toLowerCase();
        base += "-";
        base += suffix;
    }
    return base;
}

static void ensureHostname()
{
    if (deviceHostname.length() == 0)
    {
        deviceHostname = buildDefaultHostname();
    }
}

static void stopMdnsService()
{
    if (!mdnsRunning)
        return;

    MDNS.end();
    mdnsRunning = false;
    Serial.println("[mDNS] responder stopped.");
}

static void startMdnsService(bool wifiConnected)
{
    ensureHostname();

    if (mdnsRunning)
    {
        MDNS.end();
        mdnsRunning = false;
    }

    if (!MDNS.begin(deviceHostname.c_str()))
    {
        Serial.println("[mDNS] Failed to start responder.");
        return;
    }

    MDNS.addService("http", "tcp", 80);
    IPAddress ip = wifiConnected ? WiFi.localIP() : getAccessPointIP();
    Serial.printf("[mDNS] http://%s.local (%s)\n", deviceHostname.c_str(), ip.toString().c_str());
    mdnsRunning = true;
}

static void refreshNetworkServices(bool wifiConnected)
{
    ensureHostname();

    if (!otaInitialized)
    {
        ArduinoOTA.setHostname(deviceHostname.c_str());
        ArduinoOTA.begin();
        otaInitialized = true;
        Serial.printf("[OTA] Ready (%s.local)\n", deviceHostname.c_str());
    }

    setupWebServer();
    startMdnsService(wifiConnected);

    IPAddress ip = wifiConnected ? WiFi.localIP() : getAccessPointIP();
    Serial.printf("[Web] Control panel: http://%s.local  (direct IP http://%s)\n",
                  deviceHostname.c_str(), ip.toString().c_str());
}

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
           wifiSettingsModal.isActive() ||
           displayModal.isActive() ||
           weatherModal.isActive() ||
           tempestModal.isActive() ||
           calibrationModal.isActive() ||
           systemModal.isActive() ||
           scenePreviewModal.isActive() ||
           isWeatherScenePreviewActive();
}

static void renderScreenContents(ScreenMode mode)
{
    if (!screenIsAllowed(mode))
        return;

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
    case SCREEN_WIND_DIR:
        showWindDirectionScreen();
        break;
    case SCREEN_ENV_INDEX:
        envQualityScreen.tick();
        break;
    case SCREEN_TEMP_HISTORY:
        drawTemperatureHistoryScreen();
        break;
    case SCREEN_HUM_HISTORY:
        drawHumidityHistoryScreen();
        break;
    case SCREEN_CO2_HISTORY:
        drawCo2HistoryScreen();
        break;
    case SCREEN_BARO_HISTORY:
        drawBaroHistoryScreen();
        break;
    case SCREEN_PREDICT:
        drawPredictionScreen();
        break;
    case SCREEN_CONDITION_SCENE:
        drawConditionSceneScreen();
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
    case SCREEN_LUNAR_VI:
        drawLunarScreenVi();
        break;
    default:
        break;
    }
}

static void playScreenRevealEffect(ScreenMode mode)
{
    if (isScreenOff())
        return;
    if (!screenIsAllowed(mode))
        return;

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

static void ensureCurrentScreenAllowed()
{
    ScreenMode allowed = enforceAllowedScreen(currentScreen);
    if (allowed != currentScreen)
    {
        hideAllInfoScreens();
        currentScreen = allowed;
        playScreenRevealEffect(currentScreen);
        noteScreenRotation(millis());
    }
}

static void applyDataSourcePolicies(bool wifiConnected)
{
    static int lastSource = -1;

    if (isDataSourceWeatherFlow())
    {
        if (!udpListening && wifiConnected)
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
        }
    }
    else if (udpListening)
    {
        udp.stop();
        udpListening = false;
    }

    if (lastSource != dataSource)
    {
        if (wifiConnected)
        {
            if (isDataSourceWeatherFlow())
            {
                fetchForecastData();
            }
            else if (isDataSourceOwm())
            {
                fetchWeatherFromOWM();
            }
        }
        ensureCurrentScreenAllowed();
        lastSource = dataSource;
    }
}

// --- Screen rotation handler ---
void rotateScreen(int direction)
{
    currentScreen = enforceAllowedScreen(currentScreen);

    ScreenMode next = nextAllowedScreen(currentScreen, direction);
    if (!screenIsAllowed(next))
        next = enforceAllowedScreen(next);

    if (next == currentScreen)
    {
        playScreenRevealEffect(currentScreen);
        noteScreenRotation(millis());
        return;
    }

    hideAllInfoScreens();
    currentScreen = next;

    switch (currentScreen)
    {
    case SCREEN_CLOCK:
        drawClockScreen();
        break;
    case SCREEN_UDP_DATA:
        showUdpScreen();
        break;
    case SCREEN_UDP_FORECAST:
        showForecastScreen();
        break;
    case SCREEN_WIND_DIR:
        showWindDirectionScreen();
        break;
    case SCREEN_ENV_INDEX:
        showEnvironmentalQualityScreen();
        break;
    case SCREEN_TEMP_HISTORY:
        drawTemperatureHistoryScreen();
        break;
    case SCREEN_HUM_HISTORY:
        drawHumidityHistoryScreen();
        break;
    case SCREEN_CO2_HISTORY:
        drawCo2HistoryScreen();
        break;
    case SCREEN_BARO_HISTORY:
        drawBaroHistoryScreen();
        break;
    case SCREEN_PREDICT:
        drawPredictionScreen();
        break;
    case SCREEN_CONDITION_SCENE:
        drawConditionSceneScreen();
        break;
    case SCREEN_CURRENT:
        showCurrentConditionsScreen();
        break;
    case SCREEN_HOURLY:
        showHourlyForecastScreen();
        break;
    case SCREEN_OWM:
        break;
    default:
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
        refreshNetworkServices(true);
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
        if (isDataSourceWeatherFlow())
        {
            if (!udpListening)
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
            }
        }
        else if (udpListening)
        {
            udp.stop();
            udpListening = false;
        }

        if (isDataSourceOwm())
        {
            fetchWeatherFromOWM();
            delay(500);
        }
    }

    getTimeFromRTC();

    if (wifiOk && isDataSourceWeatherFlow())
    {
        fetchTempestData();
        fetchForecastData();
    }

    reset_Time_and_Date_Display = true;

    // All data ready; now fade out the splash before drawing the main UI.
    splashEnd();

    displayWeatherData();
    displayClock();
    displayDate();
    requestScrollRebuild();
    serviceScrollRebuild();

    currentScreen = SCREEN_CLOCK;
    playScreenRevealEffect(currentScreen);

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
    windInfo.setBounceEnabled(false);
    updateWindInfoScroll(true);
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
    startAccessPoint();
    completeStartupAfterWiFi(true);
}

void setup()
{
    Serial.begin(115200);
    delay(100);

    Serial.printf("Flash size: %u bytes\n", ESP.getFlashChipSize());
    deviceHostname = buildDefaultHostname();
    Serial.printf("Hostname: %s.local\n", deviceHostname.c_str());
    loadSettings();
    initAlarmModule();
    delay(500);
    setupDisplay();
    initNoaaAlerts();
    int clampedSplashSec = constrain(splashDurationSec, 1, 10);
    uint16_t splashMs = static_cast<uint16_t>(clampedSplashSec * 1000);
    if (splashMs < 3000)
        splashMs = 3000;
    splashBegin(splashMs);
    splashUpdate("Display On", 1, 6);
    Serial.println("Display setup done.");

     // Setup Sensors
    splashUpdate("Sensors", 2, 6);
    setupSensors();

    Serial.println("Setup IR Sensor");
    splashUpdate("IR Remote", 3, 6);
    setupIRSensor();

    splashUpdate("Buzzer", 4, 6);
    setupBuzzer();

    splashUpdate("Data Log", 5, 6);
    initSensorLog();

    // Initialise RTC once sensors/I2C are ready
    rtcReady = rtc.begin();
    if (!rtcReady)
    {
        Serial.println("RTC not detected; time will default to 00:00");
        splashUpdate("RTC Check", 4, 6);
    }
    else
    {
        Serial.println("RTC initialised successfully.");
        splashUpdate("RTC Check", 4, 6);
    }

    Serial.println("\nESP32 Weather Display");

    // InfoScreen highlight preferences
    udpScreen.setHighlightEnabled(true);
    forecastScreen.setHighlightEnabled(true);
    envQualityScreen.setHighlightEnabled(false);
    currentCondScreen.setHighlightEnabled(true);
    hourlyScreen.setHighlightEnabled(true);
    noaaAlertScreen.setHighlightEnabled(true);

    // Ensure TCP/IP stack is initialised even if we stay offline
    WiFi.mode(WIFI_STA);
    splashUpdate("WiFi Prep", 5, 6);

    if (initialSetupRequired)
    {
        Serial.println("[Setup] Initial configuration required.");
        splashUpdate("Startup", 6, 6);
        splashEnd();
        showInitialSetupPrompt();
        return;
    }

    if (wifiSSID.isEmpty() || wifiPass.isEmpty())
    {
        Serial.println("[WiFi] No credentials, starting offline mode.");
        initialSetupAwaitingWifi = false;
        startAccessPoint();
        splashUpdate("Offline", 6, 6);
        completeStartupAfterWiFi(true);
        return;
    }

    Serial.println("Connecting WiFi...");
    splashUpdate("WiFi Link", 6, 6);
    connectToWiFi();

    if (wifiSelecting)
    {
        splashEnd();
        return;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[WiFi] Connection failed, showing WiFi menu...");
        splashEnd();
        onWiFiConnectFailed();
        return;
    }

    splashUpdate("Startup", 6, 6);
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

    // Pause normal rendering during OTA uploads; keep lightweight processing only
    if (otaInProgress)
    {
        delay(50);
        return;
    }

    DateTime alarmNow;
    bool haveAlarmTime = false;
    if (rtcReady)
    {
        DateTime utcNow = rtc.now();
        int offsetMinutes = timezoneIsCustom() ? tzStandardOffset : timezoneOffsetForUtc(utcNow);
        alarmNow = utcToLocal(utcNow, offsetMinutes);
        updateTimezoneOffsetWithUtc(utcNow);
        haveAlarmTime = true;
    }
    else if (getLocalDateTime(alarmNow))
    {
        haveAlarmTime = true;
    }
    if (haveAlarmTime)
    {
        tickAlarmState(alarmNow);
    }

    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    bool apActive = isAccessPointActive();

    if (wifiConnected && apActive)
    {
        stopAccessPoint();
        apActive = isAccessPointActive();
    }

    if ((initialSetupAwaitingWifi || !startupCompleted) && wifiConnected)
    {
        completeStartupAfterWiFi(initialSetupAwaitingWifi);
    }
    else if (!wifiConnected && udpListening)
    {
        udp.stop();
        udpListening = false;
    }

    static unsigned long wifiDownSince = 0;
    if (!wifiConnected)
    {
        if (wifiDownSince == 0)
            wifiDownSince = now;

        if (!apActive && !wifiSelecting && (now - wifiDownSince) >= WIFI_RETRY_TIMEOUT)
        {
            if (startAccessPoint())
            {
                apActive = true;
            }
        }
    }
    else
    {
        wifiDownSince = 0;
    }

    if (wifiConnected != lastWifiState || apActive != lastApState)
    {
        if (wifiConnected || apActive)
        {
            refreshNetworkServices(wifiConnected);
        }
        else
        {
            stopMdnsService();
        }
        lastWifiState = wifiConnected;
        lastApState = apActive;
    }

    applyDataSourcePolicies(wifiConnected);

    static ScreenMode lastScreen = SCREEN_CLOCK; // or whatever your default is
    static bool needsClear = false;
    if (currentScreen != lastScreen)
    {
        needsClear = true;
        lastScreen = currentScreen;
    }

    // === [1] --- Always-on background tasks --- ===
    // --- Fetch new forecast data every 15 minutes ---
    if (wifiConnected && isDataSourceWeatherFlow() && (now - lastForecast > 15 * 60 * 1000))
    {
        fetchForecastData();
        lastForecast = now;
    }

    // --- Main background tasks ---
    if (wifiConnected || apActive)
    {
        ArduinoOTA.handle();
    }

    if (udpListening)
    {
        fetchTempestData();
    }

    // --- Physical button handling (active low) ---
    if (now - lastButtonCheck >= buttonInterval)
    {
        lastButtonCheck = now;
        getButton(); // maps UP/DOWN/LEFT/RIGHT/SELECT to menu actions
    }

    // --- Sensor trend logging (every 5 minutes) ---
    static unsigned long lastLogMs = 0;
    if (!otaInProgress && now - lastLogMs >= 300000UL) // 5 minutes
    {
        lastLogMs = now;
        float temp = NAN, hum = NAN, press = NAN, co2 = NAN;
        if (!isnan(SCD40_temp)) temp = SCD40_temp;
        else if (!isnan(aht20_temp)) temp = aht20_temp;
        if (!isnan(SCD40_hum)) hum = SCD40_hum;
        else if (!isnan(aht20_hum)) hum = aht20_hum;
        if (!isnan(bmp280_pressure)) press = bmp280_pressure;
        if (SCD40_co2 > 0) co2 = static_cast<float>(SCD40_co2);
        float lux = readBrightnessSensor();
        DateTime logNow;
        uint32_t ts = millis() / 1000;
        if (getLocalDateTime(logNow)) {
            ts = logNow.unixtime();
        }
        SensorSample s{ts, temp, hum, press, lux, co2};
        appendSensorSample(s);
    }

    // --- Brightness control ---
    static unsigned long lastBrightnessRead = 0;
    if (now - lastBrightnessRead >= 5000)
    {
        lastBrightnessRead = now;
        float lux = readBrightnessSensor();
        // Always derive a calibrated reading so theme switching has up-to-date lux
        if (autoBrightness)
        {
            setDisplayBrightnessFromLux(lux);
        }
        else
        {
            int hwBrightness = map(brightness, 1, 100, 3, 255);
            if (!isScreenOff())
            {
                setPanelBrightness(hwBrightness);
            }
            Serial.printf("Manual Brightness: %d%% -> %d\n", brightness, hwBrightness);
        }
        tickAutoThemeAmbient(lux);
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
        !wifiSettingsModal.isActive() &&
        !displayModal.isActive() &&
        !alarmModal.isActive() &&
        !weatherModal.isActive() &&
        !tempestModal.isActive() &&
        !calibrationModal.isActive() &&
        !systemModal.isActive() &&
        !scenePreviewModal.isActive() &&
        !isWeatherScenePreviewActive() &&
        !(wifiSelecting && currentMenuLevel == MENU_WIFI_SELECT))
    {
        handleResetButton();
    }

    handleAutoRotate(now);
    tickAutoThemeSchedule();
    tickNoaaAlerts(now);

    if (themeRefreshPending)
    {
        if (!isScreenOff())
        {
            playScreenRevealEffect(currentScreen);
        }
        themeRefreshPending = false;
    }

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
    if (wifiSettingsModal.isActive())
    {
        wifiSettingsModal.tick();
        wifiSettingsModal.handleIR(getIRCodeNonBlocking());
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
    if (unitSettingsModal.isActive())
    {
        unitSettingsModal.tick();
        unitSettingsModal.handleIR(getIRCodeNonBlocking());
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
    if (tempestModal.isActive())
    {
        tempestModal.tick();
        tempestModal.handleIR(getIRCodeNonBlocking());
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
    if (alarmModal.isActive())
    {
        alarmModal.tick();
        alarmModal.handleIR(getIRCodeNonBlocking());
        if (alarmSlotSelection != alarmSlotShown)
        {
            alarmModal.hide();
            showAlarmSettingsModal();
        }
        delay(40);
        return;
    }
    if (noaaModal.isActive())
    {
        noaaModal.tick();
        noaaModal.handleIR(getIRCodeNonBlocking());
        delay(40);
        return;
    }
    if (scenePreviewModal.isActive())
    {
        scenePreviewModal.tick();
        scenePreviewModal.handleIR(getIRCodeNonBlocking());
        delay(40);
        return;
    }

    if (isWeatherScenePreviewActive())
    {
        uint32_t code = getIRCodeDebounced();
        if (code)
            handleWeatherScenePreviewIR(code);
        delay(40);
        return;
    }

    // --- 3. Handle WiFi SSID selection menu ---
    if (wifiSelecting && currentMenuLevel == MENU_WIFI_SELECT)
    {
        uint32_t code = getIRCodeNonBlocking();
        if (code)
            handleIR(code); // Route IR to menu.cpp WiFi handler

        // If WiFi selection exited (e.g. keyboard opened), skip drawing list again.
        if (!wifiSelecting || currentMenuLevel != MENU_WIFI_SELECT || inKeyboardMode)
        {
            delay(10);
            return;
        }

        drawWiFiMenu(); // Draw scanned WiFi list
        delay(80);      // Slight delay for smoother response
        return;         // Skip rest of loop while selecting WiFi
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
        uint32_t code = getIRCodeDebounced();
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
        uint32_t code = getIRCodeDebounced();
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
        uint32_t code = getIRCodeDebounced();
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
        uint32_t code = getIRCodeDebounced();
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
    // --- 6. No modal/menu/keyboard/InfoScreen active: handle IR for menu or screen rotation ---
    uint32_t code = getIRCodeDebounced();
    // Pause/resume Next 24h scroll with Down/Up when on prediction screen
    if (code == IR_DOWN && currentScreen == SCREEN_PREDICT)
    {
        handlePredictionDownPress();
        return;
    }
    if (code == IR_UP && currentScreen == SCREEN_PREDICT)
    {
        handlePredictionUpPress();
        return;
    }

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
        wifiSettingsModal.isActive() ||
        displayModal.isActive() ||
        weatherModal.isActive() ||
        tempestModal.isActive() ||
        calibrationModal.isActive() ||
        alarmModal.isActive() ||
        noaaModal.isActive() ||
        systemModal.isActive() ||
        inKeyboardMode ||
        udpScreen.isActive() ||
        forecastScreen.isActive() ||
        envQualityScreen.isActive() ||
        currentCondScreen.isActive() ||
        hourlyScreen.isActive() ||
        noaaAlertScreen.isActive();


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
    case SCREEN_ENV_INDEX:
        if (!envQualityScreen.isActive())
            showEnvironmentalQualityScreen();
        break;
    case SCREEN_NOAA_ALERT:
        if (!noaaAlertScreen.isActive())
            showNoaaAlertScreen();
        break;
    case SCREEN_CURRENT:
        if (!currentCondScreen.isActive())
            showCurrentConditionsScreen();
        break;

    case SCREEN_HOURLY:
        if (!hourlyScreen.isActive())
            showHourlyForecastScreen();
        break;

    default:
        break;
    }

// Screens not use InfoScreen class   
    if( currentScreen == SCREEN_CLOCK){
        // Handle clock screen updates
        static unsigned long lastClockUpdate = 0;
        unsigned long interval = isAlarmCurrentlyActive() ? 400 : 1000;
        if (now - lastClockUpdate >= interval) // Update display
        {
            lastClockUpdate = now;
            drawClockScreen(); // Redraw clock screen
        }
    }

    if (currentScreen == SCREEN_LUNAR_VI)
    {
        static unsigned long lastLunarRedraw = 0;
        if (needsClear)
        {
            dma_display->fillScreen(0);
            needsClear = false;
            lastLunarRedraw = 0;
        }
        if (now - lastLunarRedraw >= 60000)
        {
            lastLunarRedraw = now;
            drawLunarScreenVi();
        }
        tickLunarMarquee();
    }

    if (currentScreen == SCREEN_CONDITION_SCENE)
    {
        static unsigned long lastConditionSceneUpdate = 0;
        const unsigned long conditionSceneInterval = 5000;

        bool shouldRedraw = needsClear || (now - lastConditionSceneUpdate) >= conditionSceneInterval;
        if (shouldRedraw)
        {
            if (needsClear)
            {
                dma_display->fillScreen(0);
                needsClear = false;
            }
            drawConditionSceneScreen();
            lastConditionSceneUpdate = now;
        }
        tickConditionSceneMarquee();
    }

    if (currentScreen == SCREEN_TEMP_HISTORY)
    {
        static unsigned long lastTempHistoryRedraw = 0;
        const unsigned long redrawInterval = 15000;
        if (!anyModalOrInfoScreenActive &&
            (needsClear || (now - lastTempHistoryRedraw) >= redrawInterval))
        {
            drawTemperatureHistoryScreen();
            needsClear = false;
            lastTempHistoryRedraw = now;
        }
        if (!needsClear && !anyModalOrInfoScreenActive)
        {
            tickTemperatureHistoryMarquee();
        }
    }

    if (currentScreen == SCREEN_HUM_HISTORY)
    {
        static unsigned long lastHumHistoryRedraw = 0;
        const unsigned long redrawInterval = 15000;
        if (!anyModalOrInfoScreenActive &&
            (needsClear || (now - lastHumHistoryRedraw) >= redrawInterval))
        {
            drawHumidityHistoryScreen();
            needsClear = false;
            lastHumHistoryRedraw = now;
        }
        if (!needsClear && !anyModalOrInfoScreenActive)
        {
            tickHumidityHistoryMarquee();
        }
    }

    if (currentScreen == SCREEN_CO2_HISTORY)
    {
        static unsigned long lastCo2HistoryRedraw = 0;
        const unsigned long redrawInterval = 15000;
        if (!anyModalOrInfoScreenActive &&
            (needsClear || (now - lastCo2HistoryRedraw) >= redrawInterval))
        {
            drawCo2HistoryScreen();
            needsClear = false;
            lastCo2HistoryRedraw = now;
        }
        if (!needsClear && !anyModalOrInfoScreenActive)
        {
            tickCo2HistoryMarquee();
        }
    }

    if (currentScreen == SCREEN_PREDICT)
    {
        static unsigned long lastPredictRedraw = 0;
        const unsigned long redrawInterval = 15000;
        if (!anyModalOrInfoScreenActive &&
            (needsClear || (now - lastPredictRedraw) >= redrawInterval))
        {
            drawPredictionScreen();
            needsClear = false;
            lastPredictRedraw = now;
        }
        if (!needsClear && !anyModalOrInfoScreenActive)
        {
            tickPredictionScreen();
        }
        delay(40); // align tick cadence with other scrolling screens
    }

    if (currentScreen == SCREEN_BARO_HISTORY)
    {
        static unsigned long lastBaroHistoryRedraw = 0;
        const unsigned long redrawInterval = 15000;
        if (!anyModalOrInfoScreenActive &&
            (needsClear || (now - lastBaroHistoryRedraw) >= redrawInterval))
        {
            drawBaroHistoryScreen();
            needsClear = false;
            lastBaroHistoryRedraw = now;
        }
        if (!needsClear && !anyModalOrInfoScreenActive)
        {
            tickBaroHistoryMarquee();
        }
    }

    if (envQualityScreen.isActive())
    {
        if (newAirQualityData || newAHT20_BMP280Data)
        {
            showEnvironmentalQualityScreen();
            newAirQualityData = false;
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
        envQualityScreen.tick();
        envQualityScreen.handleIR(code);
        delay(40);
        return;
    }

    if (noaaAlertScreen.isActive())
    {
        uint32_t code = getIRCodeNonBlocking();
        if (code == IR_CANCEL)
        {
            hideAllInfoScreens();
            showMainMenuModal();
            playBuzzerTone(3000, 100);
            return;
        }
        // Apply input before ticking so pause/step actions take effect immediately
        if (code)
            noaaAlertScreen.handleIR(code);
        noaaAlertScreen.tick();
        delay(40);
        return;
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
        uint32_t code = getIRCodeDebounced();
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

    if (currentScreen == SCREEN_OWM)
    {
        if (!isDataSourceOwm())
        {
            ScreenMode fallback = enforceAllowedScreen(currentScreen);
            if (fallback != currentScreen)
            {
                hideAllInfoScreens();
                currentScreen = fallback;
                needsClear = true;
            }
        }
        else if (!anyModalOrInfoScreenActive)
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
                drawOWMScreen(); // <- no createScrollingText() call inside this anymore
            }
        }
    }

    delay(0);
}
