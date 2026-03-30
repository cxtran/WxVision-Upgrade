#include <Arduino.h>
#include "buzzer.h"
#include "menu.h"
#include "display.h"
#include <vector>
#include <math.h>
#include <Preferences.h>
#include "settings.h"
#include "system.h"
#include "utils.h"
#include "WiFi.h"
#include "keyboard.h"
#include "InfoModal.h"
#include "datetimesettings.h"
#include "ir_codes.h"
#include "SPIFFS.h"
#include "units.h"
#include "sensors.h"
#include "weather_countries.h"
#include "tempest.h"
#include "alarm.h"
#include "noaa.h"
#include "wifisettings.h"
#include "config.h"
#include "worldtime.h"
#include "notifications.h"
#include "app_state.h"
#include "ui_theme.h"
#include <cstring>
//#include <esp_partition.h>
#include <esp_ota_ops.h>

std::vector<MenuLevel> menuStack;
bool menuActive = false;

void (*pendingModalFn)() = nullptr;
unsigned long pendingModalTime = 0;

// Display modal state helpers
bool preserveDisplayModeTemp = false;
int cachedDisplayModeTemp = 0;
int autoThemeModeTemp = 0;

extern void connectToWiFi();
extern ScreenMode currentScreen;
// extern const int SCREEN_COUNT;
void handleScreenSwitch(int dir);
static AppState &app = appState();
#define wifiSSID app.wifiSSID
#define wifiPass app.wifiPass
#define owmCity app.owmCity
#define owmApiKey app.owmApiKey
#define wfToken app.wfToken
#define wfStationId app.wfStationId
#define humOffset app.humOffset
#define owmCountryIndex app.owmCountryIndex
#define owmCountryCustom app.owmCountryCustom
#define tempOffset app.tempOffset
#define lightGain app.lightGain
#define dayFormat app.dayFormat
#define dataSource app.dataSource
#define autoRotate app.autoRotate
#define autoRotateInterval app.autoRotateInterval
#define manualScreen app.manualScreen
#define units app.units
#define theme app.theme
#define brightness app.brightness
#define scrollSpeed app.scrollSpeed
#define verticalScrollSpeed app.verticalScrollSpeed
#define scrollLevel app.scrollLevel
#define verticalScrollLevel app.verticalScrollLevel
#define autoBrightness app.autoBrightness
#define customMsg app.customMsg
#define fmt24 app.fmt24
#define dateFmt app.dateFmt
#define deviceHostname app.deviceHostname

InfoModal sysInfoModal("Sys Info");
InfoModal wifiInfoModal("WiFi Info");
InfoModal dateModal("Date/Time");
InfoModal mainMenuModal("Main Menu");
InfoModal deviceModal("Device");
InfoModal displayModal("Display");
InfoModal weatherModal("OW Map");
InfoModal tempestModal("WF Tempest");
InfoModal calibrationModal("Calibration");
InfoModal systemModal("System");
InfoModal scenePreviewModal("Preview Scr");
InfoModal setupPromptModal("Welcome");
InfoModal wifiSettingsModal("WiFi Setting");
InfoModal unitSettingsModal("Units");
InfoModal alarmModal("Alarm");
InfoModal mqttModal("MQTT");
InfoModal noaaModal("Alerts");
InfoModal locationModal("Location");
InfoModal worldTimeModal("World Time");
InfoModal manageTzModal("Manage TZ");

int alarmSlotSelection = 0;
int alarmSlotShown = 0;

char wifiSSIDBuf[33]; // max SSID length + 1
char wifiPassBuf[65];

// --- Country Info for Weather Modal ---
String owmCountryCode = "";

// --- Menu State ---
MenuLevel currentMenuLevel = MENU_MAIN;
int currentMenuIndex = 0;
unsigned long lastMenuActivity = 0;

int menuScroll = 0;
const int visibleLines = 4;
// int scrollOffset = 0;
int wifiMenuScroll = 0;
const int wifiVisibleLines = 3;

bool wifiSelecting = false;
static bool wifiSelectReturnToSettings = false;
std::vector<String> foundSSIDs;
int selectedWifiIdx = 0;
String scannedSSIDs[16];
String scannedSSIDLabels[16];
int scannedSSIDsRSSI[16];
int wifiScanCount = 0;
int wifiSelectIndex = 0;
bool wifiSelectNeedsScan = false;
static int wifiHScrollIndex = -1;
static int wifiHScrollOffset = 0;
static unsigned long wifiHScrollLast = 0;
static unsigned long s_onboardingWifiOkGuardUntilMs = 0;

extern void handleInitialSetupDecision(bool wantsWiFi);
extern bool initialSetupAwaitingWifi;
/*
void pushMenu(MenuLevel newMenu)
{
    // Only push if different from current to avoid duplicates
    if (menuStack.empty() || menuStack.back() != newMenu) {
        menuStack.push_back(newMenu);
    }
}
*/
void pushMenu(MenuLevel newMenu)
{
    menuStack.push_back(newMenu);
    Serial.printf("[PUSH] Pushed %d, stack size now %d\n", newMenu, menuStack.size());
}

void updateMenu() { drawMenu(); }
// Date/time modal working variables
int dtYear, dtMonth, dtDay, dtHour, dtMinute, dtSecond;
int dtTimezoneIndex;
int dtManualOffset;
int dtFmt24;
int dtDateFmt;
int dtNtpPreset;
int dtAutoDst;
int unitTempSel, unitPressSel, unitClockSel, unitWindSel, unitPrecipSel;

const char *mainMenu[] = {"Device", "WiFi", "Display", "OW Map", "WF Tempest", "Calibration", "System", "World Time", "Exit Menu"};
const int mainCount = sizeof(mainMenu) / sizeof(mainMenu[0]);

const char *deviceMenu[] = {"WiFi SSID", "WiFi Pass", "Day Format", "Data Source", "Manual Screen", "< Back"};
const int deviceCount = sizeof(deviceMenu) / sizeof(deviceMenu[0]);
const char *displayMenu[] = {"Theme", "Auto Brightness", "Brightness", "Scroll Spd", "Auto Rotate", "Rotate Interval", "Custom Msg", "< Back"};
const int displayCount = sizeof(displayMenu) / sizeof(displayMenu[0]);
const char *weatherMenu[] = {"Country", "Custom Code", "OWM City", "OWM API Key", "< Back"};
const int weatherCount = sizeof(weatherMenu) / sizeof(weatherMenu[0]);
const char *tempestMenu[] = {"WF Token", "WF Station ID", "< Back"};
const int tempestCount = sizeof(tempestMenu) / sizeof(tempestMenu[0]);
const char *calibMenu[] = {"Temp Offset", "Hum Offset", "Light Gain", "< Back"};
const int calibCount = sizeof(calibMenu) / sizeof(calibMenu[0]);
const char *systemMenu[] = {"System Info", "Date & Time", "Units", "WiFi Signal Test", "Preview Screens", "Reset Settings", "Factory Reset (Erase Wi-Fi + Logs)", "Reboot"};
const int systemCount = sizeof(systemMenu) / sizeof(systemMenu[0]);


bool handleGlobalIRKey(IRCodes::WxKey key)
{
    if (key == IRCodes::WxKey::Unknown)
        return false;

    if (key == IRCodes::WxKey::Screen)
    {
        toggleScreenPower();
        return true;
    }
    if (key == IRCodes::WxKey::Theme)
    {
        if (autoThemeSchedule)
        {
            autoThemeSchedule = false;
            saveDisplaySettings();
        }
        toggleTheme(+1);
        reset_Time_and_Date_Display = true;
        requestScrollRebuild();
        return true;
    }
    return false;
}

bool handleGlobalIRCode(uint32_t code)
{
    return handleGlobalIRKey(IRCodes::mapLegacyCodeToKey(code));
}

void handleIRKey(IRCodes::WxKey key)
{
    if (key == IRCodes::WxKey::Unknown)
        return;

    Serial.printf("IR Key: %s\n", IRCodes::keyName(key));

    if (handleGlobalIRKey(key))
    {
        return;
    }
    if (isScreenOff())
        return;

    const uint32_t legacyCode = IRCodes::legacyCodeForKey(key);
    if (inKeyboardMode)
    {
        handleKeyboardIR(legacyCode);
        return;
    }
    if (sysInfoModal.isActive())
    {
        sysInfoModal.handleIR(legacyCode);
        return;
    }
    if (wifiInfoModal.isActive())
    {
        wifiInfoModal.handleIR(legacyCode);
        return;
    }
    if (dateModal.isActive())
    {
        dateModal.handleIR(legacyCode);
        return;
    }
    if (unitSettingsModal.isActive())
    {
        unitSettingsModal.handleIR(legacyCode);
        return;
    }
    if (mainMenuModal.isActive())
    {
        mainMenuModal.handleIR(legacyCode);
        return;
    }
    if (deviceModal.isActive())
    {
        deviceModal.handleIR(legacyCode);
        return;
    }
    if (displayModal.isActive())
    {
        displayModal.handleIR(legacyCode);
        return;
    }
    if (mqttModal.isActive())
    {
        mqttModal.handleIR(legacyCode);
        return;
    }
    if (weatherModal.isActive())
    {
        weatherModal.handleIR(legacyCode);
        return;
    }
    if (tempestModal.isActive())
    {
        tempestModal.handleIR(legacyCode);
        return;
    }
    if (calibrationModal.isActive())
    {
        calibrationModal.handleIR(legacyCode);
        return;
    }
    if (systemModal.isActive())
    {
        systemModal.handleIR(legacyCode);
        return;
    }
    if (locationModal.isActive())
    {
        locationModal.handleIR(legacyCode);
        return;
    }
    if (worldTimeModal.isActive())
    {
        worldTimeModal.handleIR(legacyCode);
        return;
    }
    if (manageTzModal.isActive())
    {
        manageTzModal.handleIR(legacyCode);
        return;
    }

    // WiFi select (not modal, custom menu)
    if (currentMenuLevel == MENU_WIFI_SELECT)
    {
        if (wifiScanCount == 0)
        {
            wifiSelectReturnToSettings = false;
            currentMenuLevel = MENU_DEVICE;
            drawMenu();
            return;
        }
        if (key == IRCodes::WxKey::Up)
        {
            handleUp();
            playBuzzerTone(1000, 60);
        }
        else if (key == IRCodes::WxKey::Down)
        {
            handleDown();
            playBuzzerTone(1300, 60);
        }
        else if (key == IRCodes::WxKey::Ok)
        {
            if (initialSetupAwaitingWifi && millis() < s_onboardingWifiOkGuardUntilMs)
            {
                // Ignore the carried-over OK press from onboarding "Yes".
                return;
            }
            if (wifiSelectIndex == wifiScanCount - 1)
            { // <Back>
                wifiSelecting = false;
                if (initialSetupAwaitingWifi)
                {
                    menuActive = false;
                    currentMenuLevel = MENU_NONE;
                    menuScroll = 0;
                    handleInitialSetupDecision(false);
                    playBuzzerTone(900, 80);
                    return;
                }
                bool returnToWifiSettings = wifiSelectReturnToSettings;
                wifiSelectReturnToSettings = false;
                if (returnToWifiSettings)
                {
                    currentMenuLevel = MENU_NONE;
                    menuActive = true;
                    menuScroll = 0;
                    showWiFiSettingsModal();
                }
                else
                {
                    currentMenuLevel = MENU_MAIN; // Return to MAIN menu instead of DEVICE menu
                    menuActive = true;
                    currentMenuIndex = 0;
                    menuScroll = 0;
                    showMainMenuModal(); // Show main menu modal
                }
                playBuzzerTone(900, 80);
                return;
            }
            if (scannedSSIDs[wifiSelectIndex] == "(No networks)")
            {
                playBuzzerTone(500, 100);
                return;
            }
            wifiSSID = scannedSSIDs[wifiSelectIndex];
            Serial.printf("Selected SSID: %s\n", wifiSSID.c_str());
            currentMenuLevel = MENU_DEVICE;
            currentMenuIndex = 1;
            menuScroll = 0;
            drawMenu();
            startKeyboardEntry(wifiPass.c_str(), [](const char *result)
                               {
                if (result && *result) {
                    wifiPass = String(result);
                    saveDeviceSettings();
                    wifiMarkManualConnect();
                    connectToWiFi();
                    wifiSelectReturnToSettings = false;
                } else {
                    wifiSelecting = false;
                    if (wifiSelectReturnToSettings) {
                        wifiSelectReturnToSettings = false;
                        currentMenuLevel = MENU_NONE;
                        menuActive = true;
                        menuScroll = 0;
                        showWiFiSettingsModal();
                        return;
                    }
                }
                drawMenu(); },"Enter Pwd:");
            playBuzzerTone(2200, 120);
            return;
        }
        else if (key == IRCodes::WxKey::Cancel || key == IRCodes::WxKey::Menu)
        {
            bool onboardingCancel = initialSetupAwaitingWifi;
            wifiSelecting = false;
            bool returnToWifiSettings = wifiSelectReturnToSettings;
            wifiSelectReturnToSettings = false;
            if (onboardingCancel)
            {
                menuActive = false;
                currentMenuLevel = MENU_NONE;
                menuScroll = 0;
                handleInitialSetupDecision(false);
            }
            else
            {
                if (returnToWifiSettings)
                {
                    currentMenuLevel = MENU_NONE;
                    menuActive = true;
                    menuScroll = 0;
                    showWiFiSettingsModal();
                }
                else
                {
                    currentMenuLevel = MENU_MAIN; // Return to MAIN menu here as well
                    menuActive = true;
                    currentMenuIndex = 0;
                    menuScroll = 0;
                    showMainMenuModal(); // Show main menu modal on cancel
                }
            }
            playBuzzerTone(700, 80);
        }
        return;
    }

    static unsigned long lastMenuToggle = 0;
    if (!menuActive)
    {
        // Always provide audible feedback when not in a menu
        playBuzzerTone(1200, 80);
        if (key == IRCodes::WxKey::Left)
        {
            handleScreenSwitch(-1);
            return;
        }
        if (key == IRCodes::WxKey::Right)
        {
            handleScreenSwitch(+1);
            return;
        }
        if (key == IRCodes::WxKey::Cancel || key == IRCodes::WxKey::Menu || key == IRCodes::WxKey::Ok)
        {
            if (millis() - lastMenuToggle < 500)
                return;
            menuActive = true;
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
            showMainMenuModal();
            playBuzzerTone(3000, 100);
            lastMenuToggle = millis();
            return;
        }
        return;
    }

    switch (key)
    {
    case IRCodes::WxKey::Cancel:
    case IRCodes::WxKey::Menu:
        exitToHomeScreen();
        lastMenuToggle = millis();
        break;
    case IRCodes::WxKey::Up:
        handleUp();
        playBuzzerTone(1500, 100);
        break;
    case IRCodes::WxKey::Down:
        handleDown();
        playBuzzerTone(1200, 100);
        break;
    case IRCodes::WxKey::Right:
        handleRight();
        playBuzzerTone(1800, 100);
        break;
    case IRCodes::WxKey::Left:
        handleLeft();
        playBuzzerTone(900, 100);
        break;
    case IRCodes::WxKey::Ok:
        handleSelect();
        playBuzzerTone(2200, 100);
        break;
    default:
        Serial.printf("Unknown key: %s\n", IRCodes::keyName(key));
        playBuzzerTone(500, 100);
        delay(100);
        playBuzzerTone(500, 100);
        break;
    }
}

void handleIR(uint32_t code)
{
    handleIRKey(IRCodes::mapLegacyCodeToKey(code));
}

// --- Modal-based Menu Functions --- //

void showInitialSetupPrompt()
{
    menuStack.clear();
    menuStack.push_back(MENU_INITIAL_SETUP);
    menuActive = true;
    currentMenuLevel = MENU_INITIAL_SETUP;

    // Add a leading spacer line so the question sits closer to vertical center.
    String lines[] = {"", "Setup Wifi?"};
    InfoFieldType types[] = {InfoLabel, InfoLabel};
    setupPromptModal.setLines(lines, types, 2);

    String buttons[] = {"Yes", "No"};
    setupPromptModal.setButtons(buttons, 2);

    setupPromptModal.setCallback([](bool accepted, int btnIdx)
                                 {
        setupPromptModal.hide();
        menuStack.clear();
        if (btnIdx != 0)
        {
            handleInitialSetupDecision(false);
            return;
        }
        // Prevent the same OK press from auto-selecting SSID #1 in WiFi list.
        s_onboardingWifiOkGuardUntilMs = millis() + 1000UL;
        handleInitialSetupDecision(true);
    });

    setupPromptModal.show();
    // Setup prompt must only navigate between Yes/No buttons.
    setupPromptModal.inButtonBar = true;
    setupPromptModal.btnSel = 0;
    setupPromptModal.redraw();
}

void showMainMenuModal()
{
    // Main menu is always the root, so clear the menu stack here
    menuStack.clear();
    currentMenuLevel = MENU_MAIN;
    menuActive = true;

    String items[] = {
        "Device", "WiFi", "Display", "World Time", "Alarm",
        "MQTT", "Alerts", "OW Map", "WF Tempest", "Calibration", "System", "Exit Menu"};
    InfoFieldType types[] = {
        InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel,
        InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel};
    mainMenuModal.setLines(items, types, 12);
    mainMenuModal.setShowForwardArrow(true);

    mainMenuModal.setCallback([](bool accepted, int btnIdx)
                              {
    if (!accepted) {
        // Only exit to home; don't call .hide() here (modal already hidden)
        exitToHomeScreen();
        return;
    }
    int selected = mainMenuModal.getSelIndex();
    Serial.printf("[MainMenu] selected=%d\n", selected);
        switch (selected) {
            case 0: showDeviceSettingsModal(); return;
            case 1: showWiFiSettingsModal(); return;
            case 2: showDisplaySettingsModal(); return;
            case 3: showWorldTimeModal(); return;
            case 4: showAlarmSettingsModal(); return;
            case 5: showMqttSettingsModal(); return;
            case 6: showNoaaSettingsModal(); return;
            case 7: showWeatherSettingsModal(); return;
            case 8: showWfTempestModal(); return;
            case 9: showCalibrationModal(); return;
            case 10: showSystemModal(); return;
            case 11: // Exit Menu
                mainMenuModal.hide(); // Explicitly hide for "Exit Menu" selection
                exitToHomeScreen();
                return;
        default:
            Serial.println("?????? Invalid main menu selection");
            return;
    } });

    mainMenuModal.show();
}


// --- Helper functions (WiFi scan, drawMenu, drawWiFiMenu, etc.) ---

static int performWiFiScan(uint8_t attempts = 3)
{
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // keep radio awake for scanning

    if (WiFi.status() != WL_CONNECTED)
    {
        WiFi.disconnect(false, false); // stop any stale connection attempts
    }

    WiFi.scanDelete(); // clear any cached results before starting
    delay(120);        // brief pause to let the radio settle

    int networks = -1;
    for (uint8_t tryIdx = 0; tryIdx < attempts; ++tryIdx)
    {
        networks = WiFi.scanNetworks(false, true); // blocking scan, include hidden
        if (networks > 0)
            break;
        Serial.println("[WiFi] No networks found, retrying...");
        delay(200);
    }

    if (networks < 0)
        networks = 0;

    return networks;
}

int scanWiFiNetworks()
{
    wifiScanCount = 0;
    wifiSelecting = true;

    Serial.println("[WiFi] Scanning networks...");
    int found = performWiFiScan();

    int actualNetworks = 0;
    int uniqueCount = 0;
    const int maxEntries = (int)(sizeof(scannedSSIDs) / sizeof(scannedSSIDs[0])) - 1; // reserve for "< Back>"

    if (found > 0)
    {
        for (int i = 0; i < found; ++i)
        {
            String ssid = WiFi.SSID(i);
            ssid.trim();
            if (ssid.isEmpty())
                continue;

            int rssi = WiFi.RSSI(i);
            int existing = -1;
            for (int j = 0; j < uniqueCount; ++j)
            {
                if (scannedSSIDs[j] == ssid)
                {
                    existing = j;
                    break;
                }
            }

            if (existing >= 0)
            {
                if (rssi > scannedSSIDsRSSI[existing])
                {
                    scannedSSIDsRSSI[existing] = rssi;
                }
                continue;
            }

            if (uniqueCount >= maxEntries)
                continue;

            scannedSSIDs[uniqueCount] = ssid;
            scannedSSIDsRSSI[uniqueCount] = rssi;
            ++uniqueCount;
        }
    }

    // Sort strongest to weakest
    for (int i = 0; i < uniqueCount - 1; ++i)
    {
        for (int j = i + 1; j < uniqueCount; ++j)
        {
            if (scannedSSIDsRSSI[j] > scannedSSIDsRSSI[i])
            {
                int rssiTmp = scannedSSIDsRSSI[i];
                scannedSSIDsRSSI[i] = scannedSSIDsRSSI[j];
                scannedSSIDsRSSI[j] = rssiTmp;
                String ssidTmp = scannedSSIDs[i];
                scannedSSIDs[i] = scannedSSIDs[j];
                scannedSSIDs[j] = ssidTmp;
            }
        }
    }

    int j = 0;
    if (uniqueCount > 0)
    {
        for (int i = 0; i < uniqueCount && j < maxEntries; ++i)
        {
            scannedSSIDLabels[j] = scannedSSIDs[i] + " (" + String(scannedSSIDsRSSI[i]) + " dBm)";
            ++j;
            actualNetworks++;
        }
    }

    if (actualNetworks == 0)
    {
        scannedSSIDs[j++] = "(No networks)";
        scannedSSIDLabels[j - 1] = scannedSSIDs[j - 1];
    }
    scannedSSIDs[j++] = "< Back>";
    scannedSSIDLabels[j - 1] = scannedSSIDs[j - 1];

    wifiScanCount = j;
    wifiSelectIndex = (actualNetworks > 0) ? 0 : (wifiScanCount > 1 ? 1 : 0);
    wifiMenuScroll = 0;
    wifiSelectNeedsScan = false;

    Serial.printf("[scanWiFiNetworks] Found %d networks (+Back)\n", actualNetworks);

    WiFi.scanDelete(); // free scan results now that we've copied what we need
    return actualNetworks;
}


void drawMenu()
{
    // If keyboard mode, let keyboard handle drawing!
    if (inKeyboardMode)
    {
        drawKeyboard();
        return;
    }
    if (currentMenuLevel == MENU_MAIN)
        return;
    if (currentMenuLevel == MENU_WIFI_SELECT)
    {
        drawWiFiMenu();
        return;
    }
    // You can remove any further legacy drawing.
}

void handleUp()
{
    lastMenuActivity = millis();
    scrollOffset = 0;
    if (currentMenuLevel == MENU_WIFI_SELECT)
    {
        if (wifiScanCount > 0)
        {
            if (wifiSelectIndex > 0)
            {
                wifiSelectIndex--;
                if (wifiSelectIndex < wifiMenuScroll)
                    wifiMenuScroll = wifiSelectIndex;
            }
            if (wifiMenuScroll < 0)
                wifiMenuScroll = 0;
            int maxScroll = max(0, wifiScanCount - wifiVisibleLines);
            if (wifiMenuScroll > maxScroll)
                wifiMenuScroll = maxScroll;
        }
        drawWiFiMenu();
        return;
    }
}

void handleDown()
{
    lastMenuActivity = millis();
    scrollOffset = 0;
    if (currentMenuLevel == MENU_WIFI_SELECT)
    {
        if (wifiScanCount > 0)
        {
            if (wifiSelectIndex < wifiScanCount - 1)
            {
                wifiSelectIndex++;
                if (wifiSelectIndex >= wifiMenuScroll + wifiVisibleLines)
                    wifiMenuScroll = wifiSelectIndex - wifiVisibleLines + 1;
            }
            int maxScroll = max(0, wifiScanCount - wifiVisibleLines);
            if (wifiMenuScroll > maxScroll)
                wifiMenuScroll = maxScroll;
            if (wifiMenuScroll < 0)
                wifiMenuScroll = 0;
        }
        drawWiFiMenu();
        return;
    }
}

void handleLeft()
{
    lastMenuActivity = millis();
    if (currentMenuLevel == MENU_WIFI_SELECT)
        return;
}

void handleRight()
{
    lastMenuActivity = millis();
    if (currentMenuLevel == MENU_WIFI_SELECT)
        return;
}

void handleSelect()
{
    lastMenuActivity = millis();
    // If no menu is active, open the main menu on Select
    if (currentMenuLevel == MENU_NONE)
    {
        showMainMenuModal();
        return;
    }
    int count = (currentMenuLevel == MENU_MAIN)           ? mainCount
               : (currentMenuLevel == MENU_DEVICE)        ? deviceCount
               : (currentMenuLevel == MENU_DISPLAY)       ? displayCount
               : (currentMenuLevel == MENU_WEATHER)       ? weatherCount
               : (currentMenuLevel == MENU_TEMPEST)       ? tempestCount
               : (currentMenuLevel == MENU_CALIBRATION)   ? calibCount
                                                         : systemCount;
    if (currentMenuIndex < 0)
        currentMenuIndex = 0;
    if (currentMenuIndex >= count)
        currentMenuIndex = count - 1;
    if (inKeyboardMode)
        return;

    if (currentMenuLevel == MENU_MAIN)
    {
        showMainMenuModal();
        return;
    }
    else if (currentMenuLevel == MENU_DISPLAY)
    {
        showDisplaySettingsModal();
        return;
    }
    else if (currentMenuLevel == MENU_WEATHER)
    {
        showWeatherSettingsModal();
        return;
    }
    else if (currentMenuLevel == MENU_TEMPEST)
    {
        showWfTempestModal();
        return;
    }
    else if (currentMenuLevel == MENU_CALIBRATION)
    {
        showCalibrationModal();
        return;
    }
    else if (currentMenuLevel == MENU_SYSTEM)
    {
        showSystemModal();
        return;
    }

    if (!menuActive)
        return;
    if (currentMenuLevel == MENU_WIFI_SELECT)
    {
        drawWiFiMenu();
    }
    else
    {
        drawMenu();
    }
}

void drawWiFiMenu()
{
    dma_display->fillScreen(dma_display->color565(0, 0, 0));
    dma_display->setFont(&Font5x7Uts);

    // Get actual screen width dynamically
    int screenW = dma_display->width();

    // --- Draw header background bar ---
    uint16_t headerBg = ui_theme::wifiHeaderBg();
    uint16_t headerFg = ui_theme::wifiHeaderFg();
    dma_display->fillRect(0, 0, screenW, 8, headerBg);
    dma_display->setTextColor(headerFg);
    dma_display->setCursor(1, 0);
    dma_display->print("WIFI LIST");

    // --- Handle no networks ---
    if (wifiScanCount == 0)
    {
        dma_display->setTextColor(ui_theme::wifiErrorText());
        dma_display->setCursor(0, 10);
        dma_display->print("NO NET");
        return;
    }

    // --- Scroll bounds ---
    int maxScroll = max(0, wifiScanCount - wifiVisibleLines);
    wifiMenuScroll = constrain(wifiMenuScroll, 0, maxScroll);

    const int labelHeight = 8;
    const int listStartY = labelHeight + 1;
    const int wifiLineHeight = 8;

    // --- List scanned SSIDs ---
    for (int i = 0; i < wifiVisibleLines; ++i)
    {
        int idx = wifiMenuScroll + i;
        if (idx >= wifiScanCount) break;

        bool selected = (idx == wifiSelectIndex);
        uint16_t color = selected ? ui_theme::wifiTextSelected() : ui_theme::wifiTextNormal();
        dma_display->setTextColor(color);
        int cursorX = 0;
        if (selected)
        {
            if (wifiHScrollIndex != idx)
            {
                wifiHScrollIndex = idx;
                wifiHScrollOffset = 0;
                wifiHScrollLast = millis();
            }
            int textW = getTextWidth(scannedSSIDLabels[idx].c_str());
            if (textW > screenW)
            {
                unsigned long now = millis();
                if (now - wifiHScrollLast > (unsigned long)scrollSpeed)
                {
                    wifiHScrollLast = now;
                    wifiHScrollOffset++;
                    if (wifiHScrollOffset > (textW + 8))
                        wifiHScrollOffset = -screenW;
                }
                cursorX = -wifiHScrollOffset;
            }
            else
            {
                wifiHScrollOffset = 0;
            }
        }
        dma_display->setCursor(cursorX, listStartY + wifiLineHeight * i);
        dma_display->print(scannedSSIDLabels[idx]);
    }
}


void onWiFiConnectFailed()
{
    scanWiFiNetworks();
    wifiSelectIndex = 0;
    wifiMenuScroll = 0;
    currentMenuLevel = MENU_WIFI_SELECT;
    menuActive = true;
    drawMenu();
}


void handleScreenSwitch(int dir)
{
    int next = (int)currentScreen + dir;
    if (next < 0)
        next = SCREEN_COUNT - 1;
    if (next >= SCREEN_COUNT)
        next = 0;
    currentScreen = (ScreenMode)next;
    if (currentScreen == ScreenMode::SCREEN_OWM)
    {
        dma_display->clearScreen();
        delay(50);
    }
    playBuzzerTone(2000 + dir * 200, 80);
}


void exitToHomeScreen()
{
    menuStack.clear();
    menuActive = false;
    currentScreen = SCREEN_CLOCK;
    dma_display->fillScreen(0);
    drawClockScreen();
    reset_Time_and_Date_Display = false;
}


void showWiFiSettingsModal()
{
    if (currentMenuLevel != MENU_NONE)
        pushMenu(currentMenuLevel);

    currentMenuLevel = MENU_MAIN;
    menuActive = true;

    // Build dynamic labels
    String ssidLabel = "WiFi SSID: ";
    ssidLabel += wifiSSID.length() ? wifiSSID : "(not set)";
    String passLabel = wifiPass.isEmpty() ? "Enter Password" : "Change Password";

    String labels[] = {ssidLabel, passLabel, "Connect Now"};
    InfoFieldType types[] = {InfoButton, InfoButton, InfoButton};

    wifiSettingsModal.setLines(labels, types, 3);
    wifiSettingsModal.setValueRefs(nullptr, 0, nullptr, 0, nullptr, nullptr, nullptr, 0, nullptr);
    wifiSettingsModal.setShowForwardArrow(true);

    wifiSettingsModal.setCallback([](bool accepted, int) {
        int sel = wifiSettingsModal.getSelIndex();
        if (!accepted) {
            wifiSettingsModal.hide();
            showMainMenuModal();
            return;
        }

        switch (sel)
        {
            // ====== WiFi SSID ======
            case 0:
            {
                wifiSettingsModal.hide();

                // Visual feedback while scanning
                wxv::notify::showNotification(wxv::notify::NotifyId::WifiScan, dma_display->color565(0, 255, 255));

                int found = scanWiFiNetworks();
                Serial.printf("[WiFiSettings] Scan complete: %d networks\n", found);

                if (found > 0)
                {
                    // Go to WiFi select screen
                    wifiSelecting = true;
                    currentMenuLevel = MENU_WIFI_SELECT;
                    menuActive = true;
                    menuScroll = 0;
                    wifiSelectReturnToSettings = true;
                    drawWiFiMenu();
                }
                else
                {
                    wifiSelecting = false;
                    wifiScanCount = 0;
                    wifiSelectIndex = 0;
                    wifiMenuScroll = 0;

                    // Show "No networks found" briefly
                    wxv::notify::showNotification(wxv::notify::NotifyId::WifiNoNet,
                                                  dma_display->color565(255, 100, 100),
                                                  dma_display->color565(200, 200, 200),
                                                  "TRY AGAIN");
                    delay(1500);
                    showWiFiSettingsModal();
                }
                return;
            }

            // ====== Password ======
            case 1:
            {
                wifiSettingsModal.hide();
                startKeyboardEntry(
                    "",
                    [](const char *result)
                    {
                        if (result && *result)
                        {
                            wifiPass = String(result);
                            saveDeviceSettings();
                        }
                        showWiFiSettingsModal();
                    },
                    "WiFi Password");
                return;
            }

            // ====== Connect Now ======
            case 2:
            {
                saveDeviceSettings();
                wxv::notify::showNotification(wxv::notify::NotifyId::WifiConnecting,
                                              dma_display->color565(255, 255, 0),
                                              dma_display->color565(0, 200, 255));
                wifiMarkManualConnect();
                connectToWiFi();
                return;
            }
        }

        wifiSettingsModal.hide();
        showMainMenuModal();
    });

    wifiSettingsModal.show();
}
