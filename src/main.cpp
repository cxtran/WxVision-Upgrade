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
#include "worldtime.h"
#include "WorldClockScreen.h"
#include "ir_learn.h"
#include "input_manager.h"
#include "screen_manager.h"
#include "render_scheduler.h"
#include "weather_provider.h"
#include "display_astronomy.h"
#include "display_sky_facts.h"
#include "mqtt_client.h"

// --- Screen rotation: add or remove as needed ---
const ScreenMode InfoScreenModes[] = {
    SCREEN_CLOCK,
    SCREEN_OWM,
    SCREEN_WORLD_CLOCK,
    SCREEN_ASTRONOMY,
    SCREEN_SKY_BRIEF,
    SCREEN_CONDITION_SCENE,
    SCREEN_UDP_DATA,
    SCREEN_LIGHTNING,
    SCREEN_CURRENT, 
    SCREEN_HOURLY, 
    SCREEN_UDP_FORECAST,
    SCREEN_WIND_DIR,
    SCREEN_ENV_INDEX,
    SCREEN_TEMP_HISTORY,
    SCREEN_PREDICT,
    SCREEN_NOAA_ALERT,
#if WXV_ENABLE_LUNAR_CALENDAR && WXV_ENABLE_LUNAR_LUCK
    SCREEN_LUNAR_LUCK,
#endif
    };
const int NUM_INFOSCREENS = sizeof(InfoScreenModes) / sizeof(ScreenMode);

// --- Global system state ---
ScreenMode currentScreen = SCREEN_CLOCK;

InfoScreen udpScreen("Live Weather", SCREEN_UDP_DATA);
InfoScreen lightningScreen("Lightning", SCREEN_LIGHTNING);
InfoScreen forecastScreen("Next 10 Days", SCREEN_UDP_FORECAST);
InfoScreen envQualityScreen("Air Quality", SCREEN_ENV_INDEX);
InfoScreen currentCondScreen("Current", SCREEN_CURRENT);
InfoScreen hourlyScreen("Next 24 HRS", SCREEN_HOURLY);
InfoScreen noaaAlertScreen("NOAA Alert", SCREEN_NOAA_ALERT);

// Screen Wind Info
WindMeter windMeter;
ScrollLine scrollLine(64, 40); // 64 px wide, scroll every 50ms
ScrollLine windInfo(64, 40);

Preferences preferences;
int menuIndex = 0;
bool inSetupMenu = false;
WiFiUDP udp;
int localPort = 50222;

static bool startupCompleted = false;
bool initialSetupAwaitingWifi = false;
bool udpListening = false;

String deviceHostname;
static bool otaInitialized = false;
static bool mdnsRunning = false;
static bool lastWifiState = false;
static bool lastApState = false;
static void completeStartupAfterWiFi(bool force = false);
void handleInitialSetupDecision(bool wantsWiFi);

// === Display Timers ===
unsigned long lastBrightnessRead = 0;
unsigned long lastButtonCheck = 0;

const unsigned long brightnessInterval = 10000;
const unsigned long buttonInterval = 100;

// === Sensors ===
unsigned long lastReadSCD40 = 0;
const unsigned long SCD40ReadInterval = 10000;
unsigned long lastReadAHT20_BMP280 = 0;
const unsigned long AHT20_BMP280ReadInterval = 10000;

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
    lightningScreen.hide();
    forecastScreen.hide();
    envQualityScreen.hide();
    currentCondScreen.hide();
    hourlyScreen.hide();
    noaaAlertScreen.hide();
    // Add more InfoScreens here as needed
}


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

static void attemptWifiReconnect(bool apActive)
{
    // --- BEGIN NEW CODE ---
    startBackgroundWifiReconnect(apActive);
    // --- END NEW CODE ---
}

static bool isSavedSsidAvailable()
{
    if (wifiSSID.isEmpty())
        return false;

    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    wifi_mode_t previousMode = WiFi.getMode();
    wifi_mode_t scanMode = previousMode;

    if (scanMode == WIFI_OFF)
    {
        scanMode = WIFI_STA;
    }
    else if (scanMode == WIFI_AP)
    {
        scanMode = WIFI_AP_STA;
    }

    if (scanMode != previousMode)
    {
        WiFi.mode(scanMode);
        delay(100);
    }

    if (!wifiConnected)
    {
        WiFi.disconnect(false, false);
    }

    WiFi.scanDelete();
    int found = WiFi.scanNetworks(false, true);
    bool ssidAvailable = false;

    for (int i = 0; i < found; ++i)
    {
        if (WiFi.SSID(i) == wifiSSID)
        {
            ssidAvailable = true;
            break;
        }
    }

    WiFi.scanDelete();

    if (scanMode != previousMode)
    {
        WiFi.mode(previousMode);
    }

    return ssidAvailable;
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
           mqttModal.isActive() ||
           weatherModal.isActive() ||
           tempestModal.isActive() ||
           calibrationModal.isActive() ||
           systemModal.isActive() ||
           locationModal.isActive() ||
           worldTimeModal.isActive() ||
           manageTzModal.isActive() ||
           scenePreviewModal.isActive() ||
           isWeatherScenePreviewActive();
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
        const bool wantsUdpMulticast = wxv::provider::sourceUsesUdpMulticast(dataSource);
        if (wantsUdpMulticast)
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

        wxv::provider::fetchActiveProviderData();
        delay(500);
    }

    getTimeFromRTC();

    if (wifiOk && wxv::provider::sourceIsForecastModel(dataSource))
    {
        if (wxv::provider::sourceUsesUdpMulticast(dataSource))
            fetchTempestData();
        wxv::provider::fetchActiveProviderData();
    }

    reset_Time_and_Date_Display = true;

    // All data ready; now fade out the splash before drawing the main UI.
    splashEnd();
    splashLockout(true);

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
    // --- BEGIN WORLD TIME FEATURE ---
    loadWorldTimeSettings();
    // --- END WORLD TIME FEATURE ---
    initAlarmModule();
    delay(500);
    setupDisplay();
    initNoaaAlerts();
    int clampedSplashSec = constrain(splashDurationSec, 1, 10);
    uint16_t splashMs = static_cast<uint16_t>(clampedSplashSec * 1000);
    if (splashMs < 3000)
        splashMs = 3000;
    splashLockout(false);
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
    lightningScreen.setHighlightEnabled(true);
    forecastScreen.setHighlightEnabled(true);
    envQualityScreen.setHighlightEnabled(false);
    currentCondScreen.setHighlightEnabled(true);
    hourlyScreen.setHighlightEnabled(true);
    noaaAlertScreen.setHighlightEnabled(true);

    // Ensure TCP/IP stack is initialised even if we stay offline
    WiFi.mode(WIFI_STA);
    wifiInitStateMachine();
    mqttInit();
    splashUpdate("WiFi Prep", 5, 6);

    if (initialSetupRequired)
    {
        Serial.println("[Setup] Initial configuration required.");
        splashUpdate("Startup", 6, 6);
        splashEnd();
        splashLockout(true);
        showInitialSetupPrompt();
        return;
    }

    if (!wifiHasCredentials())
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
    wifiStartBootConnect(false);

    if (wifiSelecting)
    {
        splashEnd();
        splashLockout(true);
        return;
    }

    // --- BEGIN NEW CODE ---
    if (wifiIsConnecting())
    {
        initialSetupAwaitingWifi = true;
        return;
    }
    // --- END NEW CODE ---

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[WiFi] Connection failed; starting offline without WiFi prompt.");
        initialSetupAwaitingWifi = false;
        splashUpdate("Offline", 6, 6);
        completeStartupAfterWiFi(true);
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
    static bool clockSensorUpdatePending = false;
    const unsigned long blinkInterval = 500;

    // Pause normal rendering during OTA uploads; keep lightweight processing only
    if (otaInProgress)
    {
        delay(50);
        return;
    }

    webTick();

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
    wifiLoop(apActive);
    wifiConnected = (WiFi.status() == WL_CONNECTED);
    apActive = isAccessPointActive();
    mqttLoop();

    if (wifiConnected && apActive)
    {
        stopAccessPoint();
        apActive = isAccessPointActive();
    }

    if ((initialSetupAwaitingWifi || !startupCompleted) && wifiConnected)
    {
        completeStartupAfterWiFi(initialSetupAwaitingWifi);
    }
    else if ((initialSetupAwaitingWifi || !startupCompleted) &&
             !wifiConnected &&
             !wifiIsConnecting() &&
             !(wifiSelecting && currentMenuLevel == MENU_WIFI_SELECT))
    {
        Serial.printf("[WiFi] Startup offline (%s/%s), continuing normal operation.\n",
                      wifiStatusCodeText(), wifiStatusReasonText());
        initialSetupAwaitingWifi = false;
        completeStartupAfterWiFi(true);
    }
    else if (!wifiConnected && udpListening)
    {
        udp.stop();
        udpListening = false;
    }

    servicePendingTimeSync();

    // Do not auto-enter setup AP mode just because STA dropped temporarily.
    // NOAA HTTPS can trigger a transient disconnect (e.g. BEACON_TIMEOUT),
    // and the WiFi state machine should recover that in the background
    // without kicking the device out of normal client operation.

    if (wifiConnected != lastWifiState || apActive != lastApState)
    {
        const bool wifiReconnected = (wifiConnected && !lastWifiState);
        const bool wifiDisconnected = (!wifiConnected && lastWifiState);
        const bool haveStaIp = wifiConnected && (WiFi.localIP() != IPAddress(static_cast<uint32_t>(0)));

        if (wifiReconnected && haveStaIp)
        {
            refreshNetworkServices(true);
            restartAutomaticTimeSync();
            mqttOnWifiConnected();
        }
        else if (wifiDisconnected)
        {
            stopMdnsService();
            mqttOnWifiDisconnected();
        }
        lastWifiState = wifiConnected;
        lastApState = apActive;
    }

    applyDataSourcePolicies(wifiConnected);

    // Keep startup splash visually exclusive until it has fully ended.
    // This prevents the clock from rendering first and then being briefly
    // overdrawn by a late splash fade.
    if (isSplashActive())
    {
        delay(20);
        return;
    }

    static ScreenMode lastScreen = SCREEN_CLOCK; // or whatever your default is
    static bool needsClear = false;
    if (currentScreen != lastScreen)
    {
        needsClear = true;
        lastScreen = currentScreen;
        if (WXV_ENABLE_LUNAR_CALENDAR && WXV_ENABLE_LUNAR_LUCK && currentScreen == SCREEN_LUNAR_LUCK)
            resetLunarLuckSectionRotation();
        if (currentScreen == SCREEN_WORLD_CLOCK)
            resetWorldClockScreenState();
        if (currentScreen == SCREEN_ASTRONOMY)
            resetAstronomyScreenState();
        if (currentScreen == SCREEN_SKY_BRIEF)
            resetSkyBriefScreenState();
    }

    // === [1] --- Always-on background tasks --- ===
    // --- Fetch new forecast data every 15 minutes ---
    if (wifiConnected && wxv::provider::sourceIsForecastModel(dataSource) && (now - lastForecast > 15 * 60 * 1000))
    {
        wxv::provider::fetchActiveProviderData();
        lastForecast = now;
    }

    // Open-Meteo bootstrap retry: if no parsed data yet, retry sooner than 15 minutes.
    static unsigned long lastOpenMeteoBootstrapTry = 0;
    if (wifiConnected && isDataSourceOpenMeteo())
    {
        bool missingOpenMeteoData = (forecast.numDays <= 0 && forecast.numHours <= 0) || isnan(currentCond.temp);
        if (missingOpenMeteoData && (now - lastOpenMeteoBootstrapTry > 60000UL))
        {
            wxv::provider::fetchActiveProviderData();
            lastOpenMeteoBootstrapTry = now;
        }
    }

    static unsigned long lastOwmBootstrapTry = 0;
    if (wifiConnected && isDataSourceOwm())
    {
        bool missingOwmData = (forecast.numDays <= 0 && forecast.numHours <= 0) || isnan(currentCond.temp);
        if (missingOwmData && (now - lastOwmBootstrapTry > 60000UL))
        {
            wxv::provider::fetchActiveProviderData();
            lastOwmBootstrapTry = now;
        }
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
    // --- BEGIN WORLD TIME WEATHER ---
    // Keep world-time weather fetching active even while the world clock screen is shown,
    // otherwise entries near the end of the list can stay in "Updating..." too long.
    worldTimeWeatherTick(true);
    // --- END WORLD TIME WEATHER ---

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
    if (now - lastBrightnessRead >= brightnessInterval)
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

    // --- Read SCD40 ---
    if (now - lastReadSCD40 > SCD40ReadInterval)
    {
        lastReadSCD40 = now;
        newAirQualityData = true;
        readSCD40();
        clockSensorUpdatePending = true;
    }

    // --- Read AHT20 and BMP280 Sensor ---
    if (now - lastReadAHT20_BMP280 > AHT20_BMP280ReadInterval)
    {
        lastReadAHT20_BMP280 = now;
        newAHT20_BMP280Data = true;
        readBMP280();
        readAHT20();
        clockSensorUpdatePending = true;
    }

    serviceEnvironmentalAlerts();

    // === [2] --- UI/modal/menu/infoscreen handling --- ===

    // Only check physical reset if NO modal, NO keyboard, and NOT in WiFi select
    if (
        startupCompleted &&
        !inKeyboardMode &&
        !setupPromptModal.isActive() &&
        !sysInfoModal.isActive() &&
        !wifiInfoModal.isActive() &&
        !dateModal.isActive() &&
        !mainMenuModal.isActive() &&
        !deviceModal.isActive() &&
        !wifiSettingsModal.isActive() &&
        !displayModal.isActive() &&
        !mqttModal.isActive() &&
        !alarmModal.isActive() &&
        !weatherModal.isActive() &&
        !tempestModal.isActive() &&
        !calibrationModal.isActive() &&
        !systemModal.isActive() &&
        !locationModal.isActive() &&
        !worldTimeModal.isActive() &&
        !manageTzModal.isActive() &&
        !scenePreviewModal.isActive() &&
        !isWeatherScenePreviewActive() &&
        !(wifiSelecting && currentMenuLevel == MENU_WIFI_SELECT))
    {
        handleResetButton();
    }

    if (themeRefreshPending)
    {
        if (isSectionHeadingActive())
        {
            requestSectionHeadingRerender();
        }
        else if (isTemporaryAlertActive())
        {
            requestSectionHeadingRerender();
        }
        else if (!isScreenOff())
        {
            if (currentScreen == SCREEN_PREDICT)
            {
                resetPredictionRenderState();
            }
            playScreenRevealEffect(currentScreen);
        }
        themeRefreshPending = false;
    }

    handleAutoRotate(now);
    const bool temporaryAlertActive = isTemporaryAlertActive();
    const bool sectionHeadingActive = isSectionHeadingActive();
    if (sectionHeadingActive)
    {
        IRCodes::WxKey headingKey = getIRCodeDebounced();
        if (headingKey == IRCodes::WxKey::Cancel || headingKey == IRCodes::WxKey::Menu)
        {
            skipSectionHeading(now);
            showMainMenuModal();
            delay(5);
            return;
        }
        if (sectionHeadingActive && headingKey == IRCodes::WxKey::Ok)
        {
            skipSectionHeading(now);
            delay(5);
            return;
        }
        if (sectionHeadingActive && headingKey == IRCodes::WxKey::Left)
        {
            stepSectionHeading(-1, now);
            delay(5);
            return;
        }
        if (sectionHeadingActive && headingKey == IRCodes::WxKey::Right)
        {
            stepSectionHeading(+1, now);
            delay(5);
            return;
        }
    }
    if (serviceTemporaryAlertHeading(now))
    {
        delay(5);
        return;
    }
    if (serviceSectionHeading(now))
    {
        delay(5);
        return;
    }
    tickAutoThemeSchedule();
    tickNoaaAlerts(now);
    wxv::irlearn::tick();

    if (wxv::irlearn::consumeReturnToSystemMenuRequest())
    {
        showSystemModal();
        delay(5);
        return;
    }

    if (wxv::irlearn::isActive())
    {
        // Keep decoding IR frames while learn mode is active; routing happens in sensors.cpp.
        getIRCodeNonBlocking();
        delay(5);
        return;
    }

    // Keep small-text rendering consistent on non-lunar screens.
    // Large text paths explicitly set their own font and remain unchanged.
    if (dma_display && (!WXV_ENABLE_LUNAR_CALENDAR || !WXV_ENABLE_LUNAR_LUCK || currentScreen != SCREEN_LUNAR_LUCK))
    {
        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);
    }

    // --- 1. Keyboard always has focus if active ---
    if (inKeyboardMode)
    {
        IRCodes::WxKey key = getIRCodeNonBlocking();
        if (key != IRCodes::WxKey::Unknown)
            handleKeyboardIR(IRCodes::legacyCodeForKey(key));

        if (now - lastBlink >= blinkInterval)
        {
            lastBlink = now;
            keyboardBlinkTick();
        }
        tickKeyboard();
        delay(5);
        return;
    }

    // --- 2. If any modal is active, route IR input ONLY to modal ---
    if (setupPromptModal.isActive())
    {
        setupPromptModal.tick();
        setupPromptModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }

    if (sysInfoModal.isActive())
    {
        sysInfoModal.tick();
        sysInfoModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }
    if (wifiInfoModal.isActive())
    {
        wifiInfoModal.tick();
        wifiInfoModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }
    if (dateModal.isActive())
    {
        dateModal.tick();
        dateModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }
    if (mainMenuModal.isActive())
    {
        mainMenuModal.tick();
        mainMenuModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }
    if (deviceModal.isActive())
    {
        deviceModal.tick();
        deviceModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }
    if (wifiSettingsModal.isActive())
    {
        wifiSettingsModal.tick();
        wifiSettingsModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }
    if (displayModal.isActive())
    {
        displayModal.tick();
        displayModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }
    if (mqttModal.isActive())
    {
        mqttModal.tick();
        mqttModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }
    if (unitSettingsModal.isActive())
    {
        unitSettingsModal.tick();
        unitSettingsModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }
    if (weatherModal.isActive())
    {
        weatherModal.tick();
        weatherModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }
    if (tempestModal.isActive())
    {
        tempestModal.tick();
        tempestModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }
    if (calibrationModal.isActive())
    {
        calibrationModal.tick();
        calibrationModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }
    if (systemModal.isActive())
    {
        systemModal.tick();
        systemModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }
    if (locationModal.isActive())
    {
        locationModal.tick();
        locationModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }
    // --- BEGIN WORLD TIME FEATURE ---
    if (worldTimeModal.isActive())
    {
        worldTimeModal.tick();
        worldTimeModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }
    if (manageTzModal.isActive())
    {
        manageTzModal.tick();
        manageTzModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }
    // --- END WORLD TIME FEATURE ---
    if (alarmModal.isActive())
    {
        alarmModal.tick();
        alarmModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        if (alarmSlotSelection != alarmSlotShown)
        {
            alarmModal.hide();
            showAlarmSettingsModal();
        }
        delay(5);
        return;
    }
    if (noaaModal.isActive())
    {
        noaaModal.tick();
        noaaModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }
    if (scenePreviewModal.isActive())
    {
        scenePreviewModal.tick();
        scenePreviewModal.handleIR(IRCodes::legacyCodeForKey(getIRCodeNonBlocking()));
        delay(5);
        return;
    }

    if (isWeatherScenePreviewActive())
    {
        IRCodes::WxKey key = getIRCodeDebounced();
        if (key != IRCodes::WxKey::Unknown)
            handleWeatherScenePreviewIR(key);
        delay(5);
        return;
    }

    // --- 3. Handle WiFi SSID selection menu ---
    if (wifiSelecting && currentMenuLevel == MENU_WIFI_SELECT)
    {
        IRCodes::WxKey key = getIRCodeNonBlocking();
        if (key != IRCodes::WxKey::Unknown)
            handleIRKey(key); // Route IR to menu.cpp WiFi handler

        // If WiFi selection exited (e.g. keyboard opened), skip drawing list again.
        if (!wifiSelecting || currentMenuLevel != MENU_WIFI_SELECT || inKeyboardMode)
        {
            delay(10);
            return;
        }

        drawWiFiMenu(); // Draw scanned WiFi list
        delay(15);     // keep UI responsive while selecting WiFi
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
        IRCodes::WxKey key = getIRCodeDebounced();
        if (key == IRCodes::WxKey::Cancel || key == IRCodes::WxKey::Menu)
        {
            hideAllInfoScreens();
            showMainMenuModal();
            playBuzzerTone(3000, 100);
            return;
        }
        udpScreen.tick();
        udpScreen.handleIR(IRCodes::legacyCodeForKey(key));
        delay(5);
        return;
    }

    if (lightningScreen.isActive())
    {
        if (newTempestData)
        {
            showLightningScreen();
            newTempestData = false;
        }
        IRCodes::WxKey key = getIRCodeDebounced();
        if (key == IRCodes::WxKey::Cancel || key == IRCodes::WxKey::Menu)
        {
            hideAllInfoScreens();
            showMainMenuModal();
            playBuzzerTone(3000, 100);
            return;
        }
        lightningScreen.tick();
        lightningScreen.handleIR(IRCodes::legacyCodeForKey(key));
        delay(5);
        return;
    }

    if (forecastScreen.isActive())
    {
        IRCodes::WxKey key = getIRCodeDebounced();
        if (key == IRCodes::WxKey::Cancel || key == IRCodes::WxKey::Menu)
        {
            hideAllInfoScreens();
            showMainMenuModal();
            playBuzzerTone(3000, 100);
            return;
        }
        forecastScreen.tick();
        forecastScreen.handleIR(IRCodes::legacyCodeForKey(key));
        delay(5);
        return;
    }

    if (currentCondScreen.isActive())
    {
        IRCodes::WxKey key = getIRCodeDebounced();
        if (key == IRCodes::WxKey::Cancel || key == IRCodes::WxKey::Menu)
        {
            hideAllInfoScreens();
            showMainMenuModal();
            playBuzzerTone(3000, 100);
            return;
        }
        currentCondScreen.tick();
        currentCondScreen.handleIR(IRCodes::legacyCodeForKey(key));
        delay(5);
        return;
    }

    if (hourlyScreen.isActive())
    {
        IRCodes::WxKey key = getIRCodeDebounced();
        if (key == IRCodes::WxKey::Cancel || key == IRCodes::WxKey::Menu)
        {
            hideAllInfoScreens();
            showMainMenuModal();
            playBuzzerTone(3000, 100);
            return;
        }
        hourlyScreen.tick();
        hourlyScreen.handleIR(IRCodes::legacyCodeForKey(key));
        delay(5);
        return;
    }
    // --- 6. No modal/menu/keyboard/InfoScreen active: handle IR for menu or screen rotation ---
    IRCodes::WxKey key = (WXV_ENABLE_LUNAR_CALENDAR && WXV_ENABLE_LUNAR_LUCK && currentScreen == SCREEN_LUNAR_LUCK)
                             ? getIRCodeNonBlocking()
                             : getIRCodeDebounced();
    uint32_t code = IRCodes::legacyCodeForKey(key);
    if (WXV_ENABLE_LUNAR_CALENDAR && WXV_ENABLE_LUNAR_LUCK && currentScreen == SCREEN_LUNAR_LUCK)
    {
        if (handleLunarLuckInput(code))
        {
            return;
        }
    }
    // 24 Hours section navigation (same up/down behavior pattern as Prediction).
    if (is24HourSectionScreen(currentScreen))
    {
        if (key == IRCodes::WxKey::Down)
        {
            handle24HourSectionDownPress();
            return;
        }
        if (key == IRCodes::WxKey::Up)
        {
            handle24HourSectionUpPress();
            return;
        }
    }

    // Pause/resume Next 24h scroll with Down/Up when on prediction screen
    if (key == IRCodes::WxKey::Down && currentScreen == SCREEN_PREDICT)
    {
        handlePredictionDownPress();
        return;
    }
    if (key == IRCodes::WxKey::Up && currentScreen == SCREEN_PREDICT)
    {
        handlePredictionUpPress();
        return;
    }

    if (currentScreen == SCREEN_NOAA_ALERT &&
        (key == IRCodes::WxKey::Up || key == IRCodes::WxKey::Down))
    {
        stepNoaaAlertsScreen((key == IRCodes::WxKey::Down) ? 1 : -1);
        return;
    }
    if (currentScreen == SCREEN_NOAA_ALERT && key == IRCodes::WxKey::Ok)
    {
        NoaaManualFetchResult result = requestNoaaManualFetch();
        if (result == NOAA_MANUAL_FETCH_STARTED)
        {
            drawNoaaAlertsScreen();
            queueTemporaryAlertHeading("GETTING ALERT...", 1200);
        }
        else if (result == NOAA_MANUAL_FETCH_BUSY)
            showSectionHeading("FETCHING...", nullptr, 1200);
        else if (result == NOAA_MANUAL_FETCH_BLOCKED)
            showSectionHeading("WIFI BUSY", nullptr, 1200);
        else
            showSectionHeading("NOAA OFF", nullptr, 1200);
        return;
    }
    // --- BEGIN WORLD TIME FEATURE ---
    if (currentScreen == SCREEN_WORLD_CLOCK &&
        worldTimeHasSelections() &&
        (key == IRCodes::WxKey::Up || key == IRCodes::WxKey::Down || key == IRCodes::WxKey::Ok))
    {
        if (key == IRCodes::WxKey::Ok)
            handleWorldClockSelectPress();
        else
            worldClockHandleStep((key == IRCodes::WxKey::Up) ? -1 : 1);
        drawWorldClockScreen();
        return;
    }
    // --- END WORLD TIME FEATURE ---
    if (currentScreen == SCREEN_ASTRONOMY &&
        (key == IRCodes::WxKey::Up || key == IRCodes::WxKey::Down || key == IRCodes::WxKey::Ok))
    {
        if (key == IRCodes::WxKey::Down)
            handleAstronomyDownPress();
        else if (key == IRCodes::WxKey::Up)
            handleAstronomyUpPress();
        else
            handleAstronomySelectPress();
        return;
    }

    if (currentScreen == SCREEN_SKY_BRIEF &&
        (key == IRCodes::WxKey::Up || key == IRCodes::WxKey::Down || key == IRCodes::WxKey::Ok))
    {
        if (key == IRCodes::WxKey::Down)
            handleSkyBriefDownPress();
        else if (key == IRCodes::WxKey::Up)
            handleSkyBriefUpPress();
        return;
    }

    if (is24HourSectionScreen(currentScreen) && key == IRCodes::WxKey::Ok)
    {
        handle24HourSectionSelectPress();
        return;
    }

    if (currentScreen == SCREEN_PREDICT && key == IRCodes::WxKey::Ok)
    {
        handlePredictionSelectPress();
        return;
    }

    if (key == IRCodes::WxKey::Left)
    {
        rotateScreen(-1);
        return;
    }
    if (key == IRCodes::WxKey::Right)
    {
        rotateScreen(+1);
        return;
    }
    if (key == IRCodes::WxKey::Cancel || key == IRCodes::WxKey::Menu)
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
        mqttModal.isActive() ||
        weatherModal.isActive() ||
        tempestModal.isActive() ||
        calibrationModal.isActive() ||
        alarmModal.isActive() ||
        noaaModal.isActive() ||
        systemModal.isActive() ||
        locationModal.isActive() ||
        worldTimeModal.isActive() ||
        manageTzModal.isActive() ||
        inKeyboardMode ||
        udpScreen.isActive() ||
        lightningScreen.isActive() ||
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
    case SCREEN_LIGHTNING:
        if (!lightningScreen.isActive())
            showLightningScreen();
        break;
    case SCREEN_ENV_INDEX:
        if (!envQualityScreen.isActive())
            showEnvironmentalQualityScreen();
        break;
    case SCREEN_NOAA_ALERT:
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
    if (currentScreen == SCREEN_CLOCK)
    {
        static int lastClockMinute = -1;
        static int lastClockHour = -1;
        static int lastClockDay = -1;

        bool alarmActive = isAlarmCurrentlyActive();
        bool timeChanged = false;
        if (haveAlarmTime)
        {
            int curMinute = alarmNow.minute();
            int curHour = alarmNow.hour();
            int curDay = alarmNow.day();
            if (curMinute != lastClockMinute || curHour != lastClockHour || curDay != lastClockDay)
            {
                lastClockMinute = curMinute;
                lastClockHour = curHour;
                lastClockDay = curDay;
                timeChanged = true;
            }
        }

        unsigned long interval = alarmActive ? 400 : 10000;
        bool forceRefresh = needsClear;
        if (forceRefresh || timeChanged || clockSensorUpdatePending ||
            renderDue(RenderSlot::ClockMain, now, interval))
        {
            drawClockScreen();
            markRendered(RenderSlot::ClockMain, now);
            noteFrameDraw(now);
            needsClear = false;
            clockSensorUpdatePending = false;
        }

        // Keep the pulse dot lightweight without forcing a full clock redraw.
        if (renderDue(RenderSlot::ClockPulse, now, 1000UL))
        {
            int pulseSecond = haveAlarmTime ? alarmNow.second() : static_cast<int>((now / 1000UL) % 60UL);
            drawClockPulseDot(pulseSecond);
            markRendered(RenderSlot::ClockPulse, now);
            noteFrameDraw(now);
        }
        // --- BEGIN WORLD TIME FEATURE ---
        if (renderDue(RenderSlot::ClockMarquee, now, kRenderMarqueeMs))
        {
            tickClockWorldTimeMarquee();
            markRendered(RenderSlot::ClockMarquee, now);
            noteFrameDraw(now);
        }
        // --- END WORLD TIME FEATURE ---
    }

    if (currentScreen == SCREEN_WORLD_CLOCK)
    {
        bool forceRefresh = needsClear;
        if (forceRefresh || renderDue(RenderSlot::WorldClockMain, now, kRenderWorldClockMs))
        {
            drawWorldClockScreen();
            markRendered(RenderSlot::WorldClockMain, now);
            noteFrameDraw(now);
            needsClear = false;
        }
    }

    if (currentScreen == SCREEN_ASTRONOMY)
    {
        if (needsClear || renderDue(RenderSlot::AstronomyMain, now, 60000UL))
        {
            drawAstronomyScreen();
            markRendered(RenderSlot::AstronomyMain, now);
            noteFrameDraw(now);
            needsClear = false;
        }
        if (!needsClear && renderDue(RenderSlot::AstronomyTick, now, kRenderMarqueeMs))
        {
            tickAstronomyScreen();
            markRendered(RenderSlot::AstronomyTick, now);
            noteFrameDraw(now);
        }
    }

    if (currentScreen == SCREEN_SKY_BRIEF)
    {
        if (needsClear || renderDue(RenderSlot::SkyBriefMain, now, 60000UL))
        {
            drawSkyBriefScreen();
            markRendered(RenderSlot::SkyBriefMain, now);
            noteFrameDraw(now);
            needsClear = false;
        }
        if (!needsClear && renderDue(RenderSlot::SkyBriefTick, now, kRenderMarqueeMs))
        {
            tickSkyBriefScreen();
            markRendered(RenderSlot::SkyBriefTick, now);
            noteFrameDraw(now);
        }
    }

    if (WXV_ENABLE_LUNAR_CALENDAR && WXV_ENABLE_LUNAR_LUCK && currentScreen == SCREEN_LUNAR_LUCK)
    {
        static int lastLunarLuckDay = -1;
        static int lastLunarLuckMonth = -1;
        static int lastLunarLuckYear = -1;
        bool lunarLuckDateChanged = false;
        if (haveAlarmTime)
        {
            int curDay = alarmNow.day();
            int curMonth = alarmNow.month();
            int curYear = alarmNow.year();
            if (curDay != lastLunarLuckDay || curMonth != lastLunarLuckMonth || curYear != lastLunarLuckYear)
            {
                lastLunarLuckDay = curDay;
                lastLunarLuckMonth = curMonth;
                lastLunarLuckYear = curYear;
                lunarLuckDateChanged = true;
            }
        }
        bool forceRefresh = needsClear || lunarLuckDateChanged;
        if (forceRefresh || renderDue(RenderSlot::LunarLuckMain, now, 60000UL))
        {
            drawLunarLuckScreen();
            markRendered(RenderSlot::LunarLuckMain, now);
            noteFrameDraw(now);
            needsClear = false;
        }
        if (renderDue(RenderSlot::LunarLuckMarquee, now, kRenderMarqueeMs))
        {
            tickLunarLuckMarquee();
            markRendered(RenderSlot::LunarLuckMarquee, now);
            noteFrameDraw(now);
        }
    }

    if (currentScreen == SCREEN_CONDITION_SCENE)
    {
        bool shouldRedraw = needsClear || renderDue(RenderSlot::ConditionMain, now, kRenderConditionMs);
        if (shouldRedraw)
        {
            drawConditionSceneScreen();
            markRendered(RenderSlot::ConditionMain, now);
            noteFrameDraw(now);
            needsClear = false;
        }
        if (renderDue(RenderSlot::ConditionMarquee, now, kRenderMarqueeMs))
        {
            tickConditionSceneMarquee();
            markRendered(RenderSlot::ConditionMarquee, now);
            noteFrameDraw(now);
        }
    }

    if (is24HourSectionScreen(currentScreen))
    {
        if (!anyModalOrInfoScreenActive &&
            (needsClear || renderDue(RenderSlot::TempHistoryMain, now, kRenderChartMs)))
        {
            draw24HourSectionScreen();
            markRendered(RenderSlot::TempHistoryMain, now);
            noteFrameDraw(now);
            needsClear = false;
        }
        if (!needsClear && !anyModalOrInfoScreenActive &&
            renderDue(RenderSlot::TempHistoryMarquee, now, kRenderMarqueeMs))
        {
            tick24HourSection();
            markRendered(RenderSlot::TempHistoryMarquee, now);
            noteFrameDraw(now);
        }
        delay(5);
    }

    if (currentScreen == SCREEN_PREDICT)
    {
        if (!anyModalOrInfoScreenActive &&
            (needsClear || renderDue(RenderSlot::PredictMain, now, kRenderChartMs)))
        {
            drawPredictionScreen();
            markRendered(RenderSlot::PredictMain, now);
            noteFrameDraw(now);
            needsClear = false;
        }
        if (!needsClear && !anyModalOrInfoScreenActive &&
            renderDue(RenderSlot::PredictMarquee, now, kRenderMarqueeMs))
        {
            tickPredictionScreen();
            markRendered(RenderSlot::PredictMarquee, now);
            noteFrameDraw(now);
        }
        delay(5); // align tick cadence with other scrolling screens
    }

    if (currentScreen == SCREEN_NOAA_ALERT)
    {
        if (!anyModalOrInfoScreenActive &&
            (needsClear || renderDue(RenderSlot::NoaaMain, now, kRenderChartMs)))
        {
            drawNoaaAlertsScreen();
            markRendered(RenderSlot::NoaaMain, now);
            noteFrameDraw(now);
            needsClear = false;
        }
        if (!needsClear && !anyModalOrInfoScreenActive &&
            renderDue(RenderSlot::NoaaTick, now, kRenderMarqueeMs))
        {
            tickNoaaAlertsScreen();
            markRendered(RenderSlot::NoaaTick, now);
            noteFrameDraw(now);
        }
        delay(5);
    }

    if (envQualityScreen.isActive())
    {
        if (newAirQualityData || newAHT20_BMP280Data)
        {
            showEnvironmentalQualityScreen();
            newAirQualityData = false;
            newAHT20_BMP280Data = false;
        }
        IRCodes::WxKey key = getIRCodeNonBlocking();
        if (key == IRCodes::WxKey::Cancel || key == IRCodes::WxKey::Menu)
        {
            hideAllInfoScreens();
            showMainMenuModal();
            playBuzzerTone(3000, 100);
            return;
        }
        envQualityScreen.tick();
        envQualityScreen.handleIR(IRCodes::legacyCodeForKey(key));
        delay(5);
        return;
    }

    if (noaaAlertScreen.isActive())
    {
        getButton(); // tighter physical button response while NOAA screen is active
        bool exitScreen = false;
        for (int i = 0; i < 4; ++i)
        {
            IRCodes::WxKey key = getIRCodeNonBlocking();
            if (key == IRCodes::WxKey::Unknown)
                break;
            if (key == IRCodes::WxKey::Cancel || key == IRCodes::WxKey::Menu)
            {
                exitScreen = true;
                break;
            }
            if (key == IRCodes::WxKey::Up)
            {
                stepNoaaAlertsScreen(-1);
                continue;
            }
            if (key == IRCodes::WxKey::Down)
            {
                stepNoaaAlertsScreen(1);
                continue;
            }
            if (key == IRCodes::WxKey::Ok)
            {
                NoaaManualFetchResult result = requestNoaaManualFetch();
                if (result == NOAA_MANUAL_FETCH_STARTED)
                {
                    drawNoaaAlertsScreen();
                    queueTemporaryAlertHeading("GETTING ALERT...", 1200);
                }
                else if (result == NOAA_MANUAL_FETCH_BUSY)
                    showSectionHeading("FETCHING...", nullptr, 1200);
                else if (result == NOAA_MANUAL_FETCH_BLOCKED)
                    showSectionHeading("WIFI BUSY", nullptr, 1200);
                else
                    showSectionHeading("NOAA OFF", nullptr, 1200);
                continue;
            }
            if (key == IRCodes::WxKey::Left)
            {
                if (stepNoaaAlertSelection(-1))
                    continue;
                rotateScreen(-1);
                return;
            }
            if (key == IRCodes::WxKey::Right)
            {
                if (stepNoaaAlertSelection(1))
                    continue;
                rotateScreen(+1);
                return;
            }
            noaaAlertScreen.handleIR(IRCodes::legacyCodeForKey(key));
        }
        if (exitScreen)
        {
            hideAllInfoScreens();
            showMainMenuModal();
            playBuzzerTone(3000, 100);
            return;
        }
        noaaAlertScreen.tick();
        delay(20);
        return;
    }

    if (currentScreen == SCREEN_WIND_DIR)
    {
        static unsigned long lastDataUpdate = 0;
        const unsigned long dataUpdateInterval = 3000; // 3 seconds
        unsigned long now = millis();

        // Update data every 3 seconds or when new rapid wind data arrives
        if (newRapidWindData || (now - lastDataUpdate) > dataUpdateInterval)
        {
            // Just clear the flag, don't redraw full screen here
            newRapidWindData = false;
            lastDataUpdate = now;
        }
        // Animate and redraw frame every ~50 ms
        if (renderDue(RenderSlot::WindMain, now, kRenderWindMs))
        {
            showWindDirectionScreen(); // Draw frame with animation
            markRendered(RenderSlot::WindMain, now);
            noteFrameDraw(now);
        }
        // IR input handling for this screen
        IRCodes::WxKey key = getIRCodeDebounced();
        if (key != IRCodes::WxKey::Unknown)
        {
            if (key == IRCodes::WxKey::Left)
            {
                rotateScreen(-1);
                return;
            }
            if (key == IRCodes::WxKey::Right)
            {
                rotateScreen(+1);
                return;
            }
            if (key == IRCodes::WxKey::Cancel || key == IRCodes::WxKey::Menu)
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
        if (!screenIsAllowed(currentScreen))
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
            if (renderDue(RenderSlot::OwmScroll, now, kRenderMarqueeMs))
            {
                scrollWeatherTick();
                markRendered(RenderSlot::OwmScroll, now);
                noteFrameDraw(now);
            }

            if (reset_Time_and_Date_Display)
            {
                reset_Time_and_Date_Display = false;
                displayClock();
                displayDate();
                displayWeatherData();
            }

            if (renderDue(RenderSlot::OwmMain, now, kRenderOwmMainMs))
            {
                reset_Time_and_Date_Display = true;
                if (needsClear)
                {
                    dma_display->fillScreen(0);
                    noteFullClear();
                    needsClear = false;
                }
                drawOWMScreen(); // <- no createScrollingText() call inside this anymore
                markRendered(RenderSlot::OwmMain, now);
                noteFrameDraw(now);
            }
        }
    }

    delay(0);
}
