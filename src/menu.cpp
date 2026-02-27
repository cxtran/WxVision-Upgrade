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
#include <cstring>
//#include <esp_partition.h>
#include <esp_ota_ops.h>

// --- System Info Colors ---
#define SYSINFO_HEADER dma_display->color565(0, 255, 80)
#define SYSINFO_HEADERBG dma_display->color565(0, 20, 60)
#define SYSINFO_UNSELXBG dma_display->color565(40, 40, 180)
#define SYSINFO_SELXBG dma_display->color565(255, 0, 0)
#define SYSINFO_XCOLOR dma_display->color565(255, 255, 255)
#define SYSINFO_ULINE dma_display->color565(180, 180, 255)
#define SYSINFO_SEL dma_display->color565(255, 255, 64)
#define SYSINFO_UNSEL dma_display->color565(0, 255, 255)

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
InfoModal noaaModal("NOAA Alerts");
InfoModal worldTimeModal("World Time");
InfoModal manageTzModal("Manage TZ");

int alarmSlotSelection = 0;
int alarmSlotShown = 0;
int alarmEnabledTemp = 0;
int alarmHourTemp = 0;
int alarmMinuteTemp = 0;
int alarmRepeatTemp = 0;
int alarmWeeklyDayTemp = 0;
int alarmAmPmTemp = 0;

static void refreshAlarmTemps()
{
    alarmSlotSelection = constrain(alarmSlotSelection, 0, 2);
    alarmSlotShown = alarmSlotSelection;
    alarmEnabledTemp = alarmEnabled[alarmSlotSelection] ? 1 : 0;
    alarmAmPmTemp = 0;
    alarmHourTemp = alarmHour[alarmSlotSelection];
    alarmMinuteTemp = alarmMinute[alarmSlotSelection];
    alarmRepeatTemp = static_cast<int>(alarmRepeatMode[alarmSlotSelection]);
    alarmWeeklyDayTemp = alarmWeeklyDay[alarmSlotSelection];
    bool use12h = !units.clock24h;
    if (use12h)
    {
        alarmAmPmTemp = (alarmHour[alarmSlotSelection] >= 12) ? 1 : 0;
        int hour12 = alarmHour[alarmSlotSelection] % 12;
        if (hour12 == 0)
            hour12 = 12;
        alarmHourTemp = hour12;
    }
}

// Exposed for InfoModal to refresh alarm fields without resetting scroll
void handleAlarmSlotChangedInModal()
{
    refreshAlarmTemps();
    alarmModal.redraw();
}

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

extern String wifiSSID;
extern String wifiPass;
extern String owmCity;
extern String owmApiKey;
extern String wfToken;
extern String wfStationId;
extern int humOffset;
extern int owmCountryIndex;
extern String owmCountryCustom;
extern float tempOffset;
extern int lightGain;
extern int dayFormat, dataSource, autoRotate, autoRotateInterval, manualScreen;
extern UnitPrefs units;
extern int theme, brightness, scrollSpeed, verticalScrollSpeed, scrollLevel, verticalScrollLevel;
extern bool autoBrightness;
extern String customMsg;
extern int fmt24, dateFmt;
extern void handleInitialSetupDecision(bool wantsWiFi);
extern bool initialSetupAwaitingWifi;
extern String deviceHostname;

static int tempOffsetDisplayTenths = 0;
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
const char *systemMenu[] = {"Show System Info", "Set Date & Time", "Unit Settings", "WiFi Signal Test", "Preview Screens", "Quick Restore", "Factory Reset", "Reboot"};
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
        "NOAA Alerts", "OW Map", "WF Tempest", "Calibration", "System", "Exit Menu"};
    InfoFieldType types[] = {
        InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel,
        InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel};
    mainMenuModal.setLines(items, types, 11);
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
            case 5: showNoaaSettingsModal(); return;
            case 6: showWeatherSettingsModal(); return;
            case 7: showWfTempestModal(); return;
            case 8: showCalibrationModal(); return;
            case 9: showSystemModal(); return;
            case 10: // Exit Menu
                mainMenuModal.hide(); // Explicitly hide for "Exit Menu" selection
                exitToHomeScreen();
                return;
        default:
            Serial.println("?????? Invalid main menu selection");
            return;
    } });

    mainMenuModal.show();
}

void showDisplaySettingsModal()
{
    // Avoid stacking duplicate Display entries when rebuilding the modal after Theme Mode changes
    bool rebuildingDisplay = (currentMenuLevel == MENU_DISPLAY && preserveDisplayModeTemp);
    if (currentMenuLevel != MENU_NONE && !rebuildingDisplay)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_DISPLAY;
    menuActive = true;

    // Persist across rebuilds so we can re-open with the just-selected mode
    if (preserveDisplayModeTemp)
    {
        autoThemeModeTemp = constrain(cachedDisplayModeTemp, 0, 2);
        preserveDisplayModeTemp = false;
    }
    else
    {
        autoThemeModeTemp = autoThemeAmbient ? 2 : (autoThemeSchedule ? 1 : 0);
    }
    cachedDisplayModeTemp = autoThemeModeTemp;
    preserveDisplayModeTemp = false;
    static int dayThemeStartTemp;
    static int nightThemeStartTemp;
    static int lightThresholdTemp;
    dayThemeStartTemp = dayThemeStartMinutes;
    nightThemeStartTemp = nightThemeStartMinutes;
    lightThresholdTemp = autoThemeLightThreshold;
    static int autoBrightnessInt;
    autoBrightnessInt = autoBrightness ? 1 : 0;
    static int brightnessTemp = brightness;
    static int scrollLevelTemp = 3;
    static int vScrollLevelTemp = 3;
    for (int i = 0; i < 10; ++i)
    {
        if (scrollSpeed >= scrollDelays[i])
        {
            scrollLevelTemp = i;
            break;
        }
    }
    for (int i = 0; i < 10; ++i)
    {
        if (verticalScrollSpeed >= scrollDelays[i])
        {
            vScrollLevelTemp = i;
            break;
        }
    }

    static int autoRotateTemp;
    autoRotateTemp = autoRotate ? 1 : 0;

    static const int rotateIntervalValues[] = {5, 10, 15, 20, 30, 45, 60, 90, 120};
    static const char *rotateIntervalOpt[] = {"5 s", "10 s", "15 s", "20 s", "30 s", "45 s", "60 s", "90 s", "120 s"};
    const int rotateIntervalCount = sizeof(rotateIntervalValues) / sizeof(rotateIntervalValues[0]);

    static int rotateIntervalIndex = 0;
    rotateIntervalIndex = 0;
    int bestIntervalDiff = (rotateIntervalValues[0] > autoRotateInterval)
        ? rotateIntervalValues[0] - autoRotateInterval
        : autoRotateInterval - rotateIntervalValues[0];
    for (int i = 1; i < rotateIntervalCount; ++i)
    {
        int diff = (rotateIntervalValues[i] > autoRotateInterval)
            ? rotateIntervalValues[i] - autoRotateInterval
            : autoRotateInterval - rotateIntervalValues[i];
        if (diff < bestIntervalDiff)
        {
            bestIntervalDiff = diff;
            rotateIntervalIndex = i;
        }
    }

    float currentLux = readBrightnessSensor();
    static const char *themeOpts[] = {"Day", "Night"};
    static const char *themeModeOpts[] = {"Manual", "Scheduled", "Light Sensor"};
    static const char *autoOpts[] = {"Off", "On"};
    static const char *speedOpts[] = {"1 - Slow", "2", "3", "4", "5", "6", "7", "8", "9", "10 - Fast"};
    static const char *autoRotateOpt[] = {"Off", "On"};

    // Build dynamic lines based on theme mode selection
    const int MAX_LINES = 12;
    String labels[MAX_LINES];
    InfoFieldType types[MAX_LINES];
    int *numberRefs[MAX_LINES];
    int numberCount = 0;
    int *chooserRefs[MAX_LINES];
    const char *const *chooserOpts[MAX_LINES];
    int chooserCounts[MAX_LINES];
    int chooserCount = 0;
    int lineCount = 0;

    auto addChooserLine = [&](const String &label, int *ref, const char *const *opts, int count) {
        labels[lineCount] = label;
        types[lineCount] = InfoChooser;
        chooserRefs[chooserCount] = ref;
        chooserOpts[chooserCount] = opts;
        chooserCounts[chooserCount] = count;
        ++chooserCount;
        ++lineCount;
    };
    auto addNumberLine = [&](const String &label, int *ref) {
        labels[lineCount] = label;
        types[lineCount] = InfoNumber;
        numberRefs[numberCount] = ref;
        ++numberCount;
        ++lineCount;
    };

    // Place Theme Mode above Theme so users choose behavior before palette
    addChooserLine("Theme Mode", &autoThemeModeTemp, themeModeOpts, 3);
    addChooserLine("Theme", &theme, themeOpts, 2);

    if (autoThemeModeTemp == 1)
    {
        addNumberLine("Day Theme Start", &dayThemeStartTemp);
        addNumberLine("Night Theme Start", &nightThemeStartTemp);
    }
    else if (autoThemeModeTemp == 2)
    {
        String lightLabel = "Light Threshold (Lux)";
        if (!isnan(currentLux))
        {
            lightLabel += " [" + String(currentLux, 1) + " lx]";
        }
        addNumberLine(lightLabel, &lightThresholdTemp);
    }

    addChooserLine("Auto Brightness", &autoBrightnessInt, autoOpts, 2);
    addNumberLine("Brightness", &brightnessTemp);
    addChooserLine("Scroll Speed", &scrollLevelTemp, speedOpts, 10);
    addChooserLine("Vert Scroll", &vScrollLevelTemp, speedOpts, 10);
    addChooserLine("Auto Rotate", &autoRotateTemp, autoRotateOpt, 2);
    addChooserLine("Rotate Interval", &rotateIntervalIndex, rotateIntervalOpt, rotateIntervalCount);

    static char customMsgBuf[64];
    strncpy(customMsgBuf, customMsg.c_str(), sizeof(customMsgBuf));
    customMsgBuf[sizeof(customMsgBuf) - 1] = 0;
    labels[lineCount] = "Custom Msg";
    types[lineCount] = InfoText;
    char *textRefs[] = {customMsgBuf};
    int textSizes[] = {sizeof(customMsgBuf)};
    ++lineCount;

    displayModal.setLines(labels, types, lineCount);
    displayModal.setValueRefs(numberRefs, numberCount, chooserRefs, chooserCount, chooserOpts, chooserCounts, textRefs, 1, textSizes);
    displayModal.setShowNumberArrows(true); // show left/right arrows for numeric fields (e.g., Light Threshold)

    displayModal.setCallback([](bool /*accepted*/, int)
    {
        brightness = constrain(brightnessTemp, 1, 100);
        autoBrightness = (autoBrightnessInt > 0);
        scrollLevel = constrain(scrollLevelTemp, 0, 9);
        scrollSpeed = scrollDelays[scrollLevel];
        verticalScrollLevel = constrain(vScrollLevelTemp, 0, 9);
        verticalScrollSpeed = scrollDelays[verticalScrollLevel];
        setAutoRotateEnabled(autoRotateTemp > 0, true);
        setAutoRotateInterval(rotateIntervalValues[rotateIntervalIndex], true);
        customMsg = String(customMsgBuf);
        bool prevAutoTheme = autoThemeSchedule;
        bool prevAmbient = autoThemeAmbient;
        int prevLightThr = autoThemeLightThreshold;
        int prevDayStart = dayThemeStartMinutes;
        int prevNightStart = nightThemeStartMinutes;
        autoThemeSchedule = (autoThemeModeTemp == 1);
        autoThemeAmbient = (autoThemeModeTemp == 2);
        dayThemeStartMinutes = normalizeThemeScheduleMinutes(dayThemeStartTemp);
        nightThemeStartMinutes = normalizeThemeScheduleMinutes(nightThemeStartTemp);
        autoThemeLightThreshold = constrain(lightThresholdTemp, 1, 5000);
        bool scheduleChanged = (prevAutoTheme != autoThemeSchedule) ||
                               (prevDayStart != dayThemeStartMinutes) ||
                               (prevNightStart != nightThemeStartMinutes);
        bool ambientChanged = (prevAmbient != autoThemeAmbient) || (prevLightThr != autoThemeLightThreshold);
        saveDisplaySettings();
        if ((scheduleChanged && autoThemeSchedule) || (ambientChanged && autoThemeAmbient))
        {
            forceAutoThemeSchedule();
        }
        Serial.printf("[Saved] brightness=%d, scrollLevel=%d -> scrollSpeed=%d vScrollLevel=%d -> vScrollSpeed=%d autoBrightness=%d autoRotate=%d interval=%ds dayStart=%d nightStart=%d autoThemeMode=%d luxThr=%d\n",
                      brightness, scrollLevel + 1, scrollSpeed, verticalScrollLevel + 1, verticalScrollSpeed, autoBrightness, autoRotate, autoRotateInterval,
                      dayThemeStartMinutes, nightThemeStartMinutes, autoThemeModeTemp, autoThemeLightThreshold);
        displayModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    });
    displayModal.show();
}

// --- BEGIN WORLD TIME FEATURE ---
static void showManageTzModal();
static void queueWorldTimeModal(void (*fn)());
static int manageTzPage = 0;
static int manageTzRestoreSel = -1;

static void queueWorldTimeModal(void (*fn)())
{
    pendingModalFn = fn;
    pendingModalTime = millis() + 10;
}

void showWorldTimeModal()
{
    if (currentMenuLevel != MENU_NONE)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_WORLDTIME;
    menuActive = true;

    loadWorldTimeSettings();

    const size_t selectedCount = worldTimeSelectionCount();
    String lines[6];
    InfoFieldType types[6];

    lines[0] = "Manage TZ";
    types[0] = InfoLabel;
    lines[1] = "Selected TZ:";
    types[1] = InfoLabel;

    for (int i = 0; i < 3; ++i)
    {
        if (static_cast<size_t>(i) < selectedCount)
        {
            int tzIndex = worldTimeSelectionAt(static_cast<size_t>(i));
            if (tzIndex >= 0 && tzIndex < static_cast<int>(timezoneCount()))
            {
                lines[2 + i] = "- " + String(timezoneLabelAt(static_cast<size_t>(tzIndex)));
            }
            else
            {
                lines[2 + i] = "-";
            }
        }
        else
        {
            lines[2 + i] = "-";
        }
        types[2 + i] = InfoLabel;
    }

    lines[5] = (selectedCount > 3) ? "- More selected timezone" : "-";
    types[5] = InfoLabel;

    worldTimeModal.setLines(lines, types, 6);
    worldTimeModal.setShowForwardArrow(true);
    worldTimeModal.setForwardArrowOnlyIndex(0);
    worldTimeModal.setKeepOpenOnSelect(false);
    worldTimeModal.setCallback([](bool accepted, int) {
        if (!accepted)
        {
            worldTimeModal.hide();
            currentMenuLevel = MENU_MAIN;
            showMainMenuModal();
            return;
        }

        int sel = worldTimeModal.getSelIndex();
        if (sel == 0)
        {
            queueWorldTimeModal(showManageTzModal);
            return;
        }

        // Display-only rows: no action.
        queueWorldTimeModal(showWorldTimeModal);
    });
    worldTimeModal.show();
}

static void showManageTzModal()
{
    currentMenuLevel = MENU_WORLDTIME;
    menuActive = true;
    loadWorldTimeSettings();

    const int rowsPerPage = InfoModal::MAX_LINES - 1; // line 1 is "Page X/Y"
    const int tzCount = static_cast<int>(timezoneCount());
    int totalPages = (tzCount + rowsPerPage - 1) / rowsPerPage;
    if (totalPages < 1)
        totalPages = 1;
    manageTzPage = constrain(manageTzPage, 0, totalPages - 1);

    int start = manageTzPage * rowsPerPage;
    int end = start + rowsPerPage;
    if (end > tzCount)
        end = tzCount;

    String lines[InfoModal::MAX_LINES];
    InfoFieldType types[InfoModal::MAX_LINES];
    int lineCount = 0;

    lines[lineCount] = "Page " + String(manageTzPage + 1) + "/" + String(totalPages);
    types[lineCount++] = InfoLabel;

    for (int i = start; i < end && lineCount < InfoModal::MAX_LINES; ++i)
    {
        bool selected = worldTimeIsSelected(i);
        lines[lineCount] = String(selected ? "[x] " : "[ ] ") + String(timezoneLabelAt(static_cast<size_t>(i)));
        types[lineCount++] = InfoLabel;
    }

    manageTzModal.setLines(lines, types, lineCount);
    manageTzModal.setShowForwardArrow(false);
    manageTzModal.setForwardArrowOnlyIndex(-1);
    manageTzModal.setKeepOpenOnSelect(true);
    manageTzModal.setCallback([](bool accepted, int) {
        if (!accepted)
        {
            queueWorldTimeModal(showWorldTimeModal);
            return;
        }

        int sel = manageTzModal.getSelIndex();
        if (sel == 0)
        {
            const int rowsPerPage = InfoModal::MAX_LINES - 1;
            const int tzCountNow = static_cast<int>(timezoneCount());
            int totalPagesNow = (tzCountNow + rowsPerPage - 1) / rowsPerPage;
            if (totalPagesNow < 1)
                totalPagesNow = 1;

            manageTzPage = (manageTzPage + 1) % totalPagesNow;
            int startNow = manageTzPage * rowsPerPage;
            int endNow = startNow + rowsPerPage;
            if (endNow > tzCountNow)
                endNow = tzCountNow;

            String refreshLines[InfoModal::MAX_LINES];
            InfoFieldType refreshTypes[InfoModal::MAX_LINES];
            int refreshCount = 0;

            refreshLines[refreshCount] = "Page " + String(manageTzPage + 1) + "/" + String(totalPagesNow);
            refreshTypes[refreshCount++] = InfoLabel;

            for (int i = startNow; i < endNow && refreshCount < InfoModal::MAX_LINES; ++i)
            {
                bool selectedNow = worldTimeIsSelected(i);
                refreshLines[refreshCount] = String(selectedNow ? "[x] " : "[ ] ") + String(timezoneLabelAt(static_cast<size_t>(i)));
                refreshTypes[refreshCount++] = InfoLabel;
            }

            manageTzModal.setLines(refreshLines, refreshTypes, refreshCount);
            manageTzModal.setShowForwardArrow(false);
            manageTzModal.setForwardArrowOnlyIndex(-1);
            manageTzModal.setKeepOpenOnSelect(true);
            manageTzModal.setSelIndex(0);
            manageTzModal.redraw();
            return;
        }

        const int rowsPerPage = InfoModal::MAX_LINES - 1;
        int start = manageTzPage * rowsPerPage;
        int tzIndex = start + (sel - 1);
        if (tzIndex >= 0 && tzIndex < static_cast<int>(timezoneCount()))
        {
            worldTimeToggleTimezone(tzIndex);
            saveWorldTimeSettings();
            const int tzCountNow = static_cast<int>(timezoneCount());
            int totalPagesNow = (tzCountNow + rowsPerPage - 1) / rowsPerPage;
            if (totalPagesNow < 1)
                totalPagesNow = 1;
            manageTzPage = constrain(manageTzPage, 0, totalPagesNow - 1);

            int startNow = manageTzPage * rowsPerPage;
            int endNow = startNow + rowsPerPage;
            if (endNow > tzCountNow)
                endNow = tzCountNow;

            String refreshLines[InfoModal::MAX_LINES];
            InfoFieldType refreshTypes[InfoModal::MAX_LINES];
            int refreshCount = 0;

            refreshLines[refreshCount] = "Page " + String(manageTzPage + 1) + "/" + String(totalPagesNow);
            refreshTypes[refreshCount++] = InfoLabel;

            for (int i = startNow; i < endNow && refreshCount < InfoModal::MAX_LINES; ++i)
            {
                bool selectedNow = worldTimeIsSelected(i);
                refreshLines[refreshCount] = String(selectedNow ? "[x] " : "[ ] ") + String(timezoneLabelAt(static_cast<size_t>(i)));
                refreshTypes[refreshCount++] = InfoLabel;
            }

            manageTzModal.setLines(refreshLines, refreshTypes, refreshCount);
            manageTzModal.setShowForwardArrow(false);
            manageTzModal.setForwardArrowOnlyIndex(-1);
            manageTzModal.setKeepOpenOnSelect(true);
            manageTzModal.setSelIndex(sel);
            manageTzModal.redraw();
            return;
        }
        manageTzModal.redraw();
    });
    manageTzModal.show();
    if (manageTzRestoreSel >= 0)
    {
        manageTzModal.setSelIndex(manageTzRestoreSel);
        manageTzModal.redraw();
        manageTzRestoreSel = -1;
    }
}
// --- END WORLD TIME FEATURE ---

void showAlarmSettingsModal()
{
    if (currentMenuLevel != MENU_NONE)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_ALARM;
    menuActive = true;

    refreshAlarmTemps();

    String labels[8];
    InfoFieldType types[8];
    int lineCount = 0;

    int *numberRefs[2];
    int numberCount = 0;
    int *chooserRefs[6];
    const char *const *chooserOpts[6];
    int chooserCounts[6];
    int chooserCount = 0;

    auto addNumberLine = [&](const String &label, int *ref) {
        labels[lineCount] = label;
        types[lineCount++] = InfoNumber;
        numberRefs[numberCount++] = ref;
    };
    auto addChooserLine = [&](const String &label, int *ref, const char *const *opts, int count) {
        labels[lineCount] = label;
        types[lineCount++] = InfoChooser;
        chooserRefs[chooserCount] = ref;
        chooserOpts[chooserCount] = opts;
        chooserCounts[chooserCount] = count;
        ++chooserCount;
    };

    static const char *enableOpts[] = {"Off", "On"};
    static const char *alarmSlotOpts[] = {"1", "2", "3"};
    static const char *repeatOpts[] = {"No Repeat", "Daily", "Weekly", "Weekdays", "Weekend"};
    static const char *dowOpts[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *ampmOpts[] = {"AM", "PM"};
    static const char *alarmSoundOpts[] = {"Tone", "Fur Elise", "Swan Lake", "Turkey March", "Moon Light Sonata"};

    addChooserLine("Select Alarm", &alarmSlotSelection, alarmSlotOpts, 3);
    addChooserLine("Alarm Enabled", &alarmEnabledTemp, enableOpts, 2);
    bool use12h = !units.clock24h;
    if (use12h)
    {
        addChooserLine("AM/PM", &alarmAmPmTemp, ampmOpts, 2);
    }
    addNumberLine(use12h ? "Hour (1-12)" : "Hour (0-23)", &alarmHourTemp);
    addNumberLine("Minute (0-59)", &alarmMinuteTemp);
    addChooserLine("Repeat Mode", &alarmRepeatTemp, repeatOpts, 5);
    addChooserLine("Weekly Day", &alarmWeeklyDayTemp, dowOpts, 7);
    addChooserLine("Alarm Sound", &alarmSoundMode, alarmSoundOpts, 5);

    alarmModal.setLines(labels, types, lineCount);
    alarmModal.setValueRefs(numberRefs, numberCount, chooserRefs, chooserCount, chooserOpts, chooserCounts, nullptr, 0, nullptr);
    alarmModal.setShowNumberArrows(true);
    alarmModal.setShowChooserArrows(true);

    alarmModal.setCallback([](bool /*accepted*/, int) {
        alarmSlotSelection = constrain(alarmSlotSelection, 0, 2);
        if (alarmSlotSelection != alarmSlotShown)
        {
            // Update temp values in-place without hiding/reopening so scroll/position stay intact
            refreshAlarmTemps();
            // Redraw modal in-place without resetting scroll/offset
            alarmModal.redraw();
            return;
        }
        int slot = alarmSlotSelection;
        alarmHourTemp = constrain(alarmHourTemp, 0, 23);
        alarmMinuteTemp = constrain(alarmMinuteTemp, 0, 59);
        alarmWeeklyDayTemp = constrain(alarmWeeklyDayTemp, 0, 6);
        alarmEnabled[slot] = (alarmEnabledTemp > 0);
        int repeatIdx = constrain(alarmRepeatTemp, static_cast<int>(ALARM_REPEAT_NONE), static_cast<int>(ALARM_REPEAT_WEEKEND));
        alarmRepeatMode[slot] = static_cast<AlarmRepeatMode>(repeatIdx);
        bool use12h = !units.clock24h;
        if (use12h)
        {
            alarmHourTemp = constrain(alarmHourTemp, 1, 12);
            int hourCore = alarmHourTemp % 12;
            if (alarmAmPmTemp > 0)
            {
                hourCore += 12;
            }
            else if (hourCore == 0)
            {
                hourCore = 0;
            }
            alarmHour[slot] = hourCore;
        }
        else
        {
            alarmHour[slot] = constrain(alarmHourTemp, 0, 23);
        }
        alarmMinute[slot] = constrain(alarmMinuteTemp, 0, 59);
        alarmWeeklyDay[slot] = constrain(alarmWeeklyDayTemp, 0, 6);
        alarmSoundMode = constrain(alarmSoundMode, 0, 4);

        refreshAlarmArming();
        saveAlarmSettings();
        notifyAlarmSettingsChanged();

        Serial.printf("[Alarm] Saved enabled=%d time=%02d:%02d repeat=%d weekday=%d\n",
                      alarmEnabled[slot], alarmHour[slot], alarmMinute[slot], alarmRepeatMode[slot], alarmWeeklyDay[slot]);

        alarmModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    });

    alarmModal.show();
}

void showNoaaSettingsModal()
{
    if (currentMenuLevel != MENU_NONE)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_NOAA;
    menuActive = true;

    static int noaaEnabledTemp = 0;
    static char latBuf[16];
    static char lonBuf[16];

    noaaEnabledTemp = noaaAlertsEnabled ? 1 : 0;
    snprintf(latBuf, sizeof(latBuf), "%.4f", noaaLatitude);
    snprintf(lonBuf, sizeof(lonBuf), "%.4f", noaaLongitude);

    String labels[] = {"Alerts", "Latitude", "Longitude"};
    InfoFieldType types[] = {InfoChooser, InfoText, InfoText};
    int *chooserRefs[] = {&noaaEnabledTemp};
    static const char *alertsOpts[] = {"Off", "On"};
    const char *const *chooserOpts[] = {alertsOpts};
    int chooserCounts[] = {2};
    char *textRefs[] = {latBuf, lonBuf};
    int textSizes[] = {static_cast<int>(sizeof(latBuf)), static_cast<int>(sizeof(lonBuf))};

    noaaModal.setLines(labels, types, 3);
    noaaModal.setValueRefs(nullptr, 0, chooserRefs, 1, chooserOpts, chooserCounts, textRefs, 2, textSizes);

    noaaModal.setCallback([](bool /*accepted*/, int) {
        noaaAlertsEnabled = (noaaEnabledTemp > 0);
        String latStr = String(latBuf);
        String lonStr = String(lonBuf);
        latStr.trim();
        lonStr.trim();
        float lat = latStr.toFloat();
        float lon = lonStr.toFloat();
        noaaLatitude = constrain(lat, -90.0f, 90.0f);
        noaaLongitude = constrain(lon, -180.0f, 180.0f);

        saveNoaaSettings();
        notifyNoaaSettingsChanged();

        Serial.printf("[NOAA] enabled=%d lat=%.4f lon=%.4f\n",
                      noaaAlertsEnabled, noaaLatitude, noaaLongitude);

        noaaModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    });

    noaaModal.show();
}

void showWeatherSettingsModal()
{

    if (currentMenuLevel != MENU_NONE)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_WEATHER;
    menuActive = true;

    static int owmCountryIndexTemp = owmCountryIndex;
    static char owmCountryCustomBuf[4] = "";
    static char owmCityBuf[32];
    static char owmKeyBuf[48];
    owmCountryIndexTemp = owmCountryIndex;
    strncpy(owmCountryCustomBuf, owmCountryCustom.c_str(), sizeof(owmCountryCustomBuf));
    strncpy(owmCityBuf, owmCity.c_str(), sizeof(owmCityBuf));
    strncpy(owmKeyBuf, owmApiKey.c_str(), sizeof(owmKeyBuf));

    String labels[] = {"Country", "Custom Code", "City", "OWM API Key"};
    InfoFieldType types[] = {InfoChooser, InfoText, InfoText, InfoText};
    int *chooserRefs[] = {&owmCountryIndexTemp};
    const char *const *chooserOpts[] = {countryLabels};
    int chooserCounts[] = {countryCount};
    char *textRefs[] = {owmCountryCustomBuf, owmCityBuf, owmKeyBuf};
    int textSizes[] = {sizeof(owmCountryCustomBuf), sizeof(owmCityBuf), sizeof(owmKeyBuf)};

    weatherModal.setLines(labels, types, 4);
    weatherModal.setValueRefs(nullptr, 0, chooserRefs, 1, chooserOpts, chooserCounts, textRefs, 3, textSizes);

    weatherModal.setCallback([](bool ok, int btnIdx)
                             {
        String prevCity = owmCity;
        String prevApiKey = owmApiKey;
        int prevCountryIndex = owmCountryIndex;
        String prevCountryCustom = owmCountryCustom;
        String prevCountryCode = owmCountryCode;

        int newCountryIndex = owmCountryIndexTemp;
        String newCountryCustom = String(owmCountryCustomBuf);
        newCountryCustom.trim();
        String newCity = String(owmCityBuf);
        newCity.trim();
        String newApiKey = String(owmKeyBuf);
        newApiKey.trim();

        bool settingsChanged =
            (newCountryIndex != prevCountryIndex) ||
            !newCountryCustom.equals(prevCountryCustom) ||
            !newCity.equals(prevCity) ||
            !newApiKey.equals(prevApiKey);

        owmCountryIndex = newCountryIndex;
        owmCountryCustom = newCountryCustom;
        owmCity = newCity;
        owmApiKey = newApiKey;

        if (owmCountryIndex < 10) {
            owmCountryCode = countryCodes[owmCountryIndex];
        } else {
            owmCountryCode = owmCountryCustom;
        }

        saveWeatherSettings();
        Serial.printf("[WeatherModal] Saved Country=%s (%s), City=%s\n",
            countryLabels[owmCountryIndex], owmCountryCode.c_str(), owmCity.c_str());

        if (settingsChanged)
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                fetchWeatherFromOWM();
                requestScrollRebuild();
                serviceScrollRebuild();
                displayWeatherData();
            }
            reset_Time_and_Date_Display = true;
        }

        weatherModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0; });
    weatherModal.show();
}

void showWfTempestModal()
{
    if (currentMenuLevel != MENU_NONE)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_TEMPEST;
    menuActive = true;

    static char wfTokenBuf[48];
    static char wfStationBuf[16];
    strncpy(wfTokenBuf, wfToken.c_str(), sizeof(wfTokenBuf));
    wfTokenBuf[sizeof(wfTokenBuf) - 1] = '\0';
    strncpy(wfStationBuf, wfStationId.c_str(), sizeof(wfStationBuf));
    wfStationBuf[sizeof(wfStationBuf) - 1] = '\0';

    String labels[] = {"WF Token", "WF Station ID"};
    InfoFieldType types[] = {InfoText, InfoText};
    char *textRefs[] = {wfTokenBuf, wfStationBuf};
    int textSizes[] = {sizeof(wfTokenBuf), sizeof(wfStationBuf)};

    tempestModal.setLines(labels, types, 2);
    tempestModal.setValueRefs(nullptr, 0, nullptr, 0, nullptr, nullptr, textRefs, 2, textSizes);

    tempestModal.setCallback([](bool, int) {
        String prevToken = wfToken;
        String prevStation = wfStationId;
        prevToken.trim();
        prevStation.trim();

        String newToken = String(wfTokenBuf);
        String newStation = String(wfStationBuf);
        newToken.trim();
        newStation.trim();

        bool credsChanged = !newToken.equals(prevToken) || !newStation.equals(prevStation);

        wfToken = newToken;
        wfStationId = newStation;
        saveWeatherSettings();

        if (credsChanged && isDataSourceWeatherFlow())
        {
            fetchForecastData();
        }

        tempestModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    });

    tempestModal.show();
}

void showCalibrationModal()
{
    if (currentMenuLevel != MENU_NONE) {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_CALIBRATION;
    menuActive = true;

    bool displayInF = (units.temp == TempUnit::F);
    String tempLabel = displayInF ? "Temp Offset (F)" : "Temp Offset (C)";
    float displayOffset = static_cast<float>(dispTempOffset(tempOffset));
    tempOffsetDisplayTenths = static_cast<int>(lroundf(displayOffset * 10.0f));

    // Bind directly to globals
    String labels[] = {tempLabel, "Hum Offset (%)", "Light Gain (%)"};
    InfoFieldType types[] = {InfoNumber, InfoNumber, InfoNumber};
    int* numberRefs[] = { &tempOffsetDisplayTenths, &humOffset, &lightGain };

    calibrationModal.setLines(labels, types, 3);
    calibrationModal.setValueRefs(numberRefs, 3, nullptr, 0, nullptr, nullptr);
    calibrationModal.setShowNumberArrows(true);

    // No final ???OK??? save. We autosave on each change in handleIR().
    calibrationModal.setCallback([](bool, int) {
        calibrationModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    });

    calibrationModal.show();
}

static void showSplashUntilButton()
{
    // Do not reuse startup splash visuals at runtime.
    // Keep this as a short non-blocking-style pause preview instead.
    getIRCodeNonBlocking(); // clear any pending input
    const unsigned long startMs = millis();
    while ((millis() - startMs) < 1000UL)
    {
        IRCodes::WxKey key = getIRCodeNonBlocking();
        if (key != IRCodes::WxKey::Unknown)
            break;
        delay(20);
    }
    delay(120);
}

struct WeatherScenePreviewOption
{
    const char *label;
    WeatherSceneKind kind;
};

static const WeatherScenePreviewOption WEATHER_SCENE_PREVIEW_OPTIONS[] = {
    {"Sunny (Day)", WeatherSceneKind::Sunny},
    {"Sunny (Night)", WeatherSceneKind::SunnyNight},
    {"Cloudy (Day)", WeatherSceneKind::Cloudy},
    {"Cloudy (Night)", WeatherSceneKind::CloudyNight},
    {"Rain (Day)", WeatherSceneKind::Rain},
    {"Rain (Night)", WeatherSceneKind::RainNight},
    {"Thunderstorm (Day)", WeatherSceneKind::Thunderstorm},
    {"Thunderstorm (Night)", WeatherSceneKind::ThunderstormNight},
    {"Snow (Day)", WeatherSceneKind::Snow},
    {"Snow (Night)", WeatherSceneKind::SnowNight},
    {"Clear Night", WeatherSceneKind::ClearNight}};

static constexpr int WEATHER_SCENE_PREVIEW_COUNT =
    sizeof(WEATHER_SCENE_PREVIEW_OPTIONS) / sizeof(WEATHER_SCENE_PREVIEW_OPTIONS[0]);

static bool weatherScenePreviewActive = false;
static int weatherScenePreviewIndex = 0;

static int wrapPreviewIndex(int idx)
{
    if (WEATHER_SCENE_PREVIEW_COUNT == 0)
        return 0;
    int mod = idx % WEATHER_SCENE_PREVIEW_COUNT;
    if (mod < 0)
        mod += WEATHER_SCENE_PREVIEW_COUNT;
    return mod;
}

static void renderWeatherScenePreview()
{
    if (WEATHER_SCENE_PREVIEW_COUNT == 0)
        return;

    weatherScenePreviewIndex = wrapPreviewIndex(weatherScenePreviewIndex);
    const WeatherScenePreviewOption &opt = WEATHER_SCENE_PREVIEW_OPTIONS[weatherScenePreviewIndex];

    dma_display->fillScreen(0);
    drawWeatherConditionScene(opt.kind);
}

static void startWeatherScenePreview(int index)
{
    weatherScenePreviewIndex = wrapPreviewIndex(index);
    weatherScenePreviewActive = true;
    menuActive = false;
    renderWeatherScenePreview();
}

static void cycleWeatherScenePreview(int delta)
{
    if (WEATHER_SCENE_PREVIEW_COUNT == 0)
        return;
    weatherScenePreviewIndex = wrapPreviewIndex(weatherScenePreviewIndex + delta);
    renderWeatherScenePreview();
}

void showScenePreviewModal()
{
    weatherScenePreviewActive = false;

    if (currentMenuLevel != MENU_NONE && currentMenuLevel != MENU_SCENE_PREVIEW)
    {
        pushMenu(currentMenuLevel);
    }

    currentMenuLevel = MENU_SCENE_PREVIEW;
    menuActive = true;

    constexpr int menuItemCount = WEATHER_SCENE_PREVIEW_COUNT + 1;
    String labels[menuItemCount];
    InfoFieldType types[menuItemCount];
    for (int i = 0; i < WEATHER_SCENE_PREVIEW_COUNT; ++i)
    {
        labels[i] = WEATHER_SCENE_PREVIEW_OPTIONS[i].label;
        types[i] = InfoButton;
    }
    labels[WEATHER_SCENE_PREVIEW_COUNT] = "Splash Screen";
    types[WEATHER_SCENE_PREVIEW_COUNT] = InfoButton;

    scenePreviewModal.setLines(labels, types, menuItemCount);
    scenePreviewModal.setCallback([](bool accepted, int btnIdx)
                                  {
        if (!accepted)
        {
            scenePreviewModal.hide();
            showSystemModal();
            return;
        }

        int action = (btnIdx >= 0) ? btnIdx : scenePreviewModal.getSelIndex();
        if (action < 0)
            action = 0;

        scenePreviewModal.hide();

        if (action >= WEATHER_SCENE_PREVIEW_COUNT)
        {
            showSplashUntilButton();
            pendingModalFn = showScenePreviewModal;
            pendingModalTime = millis() + 200;
            return;
        }

        startWeatherScenePreview(action); });
    scenePreviewModal.resetState();
    scenePreviewModal.show();
}

bool isWeatherScenePreviewActive()
{
    return weatherScenePreviewActive;
}

void handleWeatherScenePreviewIR(IRCodes::WxKey key)
{
    if (!weatherScenePreviewActive)
        return;

    switch (key)
    {
    case IRCodes::WxKey::Left:
    case IRCodes::WxKey::Up:
        cycleWeatherScenePreview(-1);
        break;
    case IRCodes::WxKey::Right:
    case IRCodes::WxKey::Down:
        cycleWeatherScenePreview(+1);
        break;
    case IRCodes::WxKey::Ok:
    case IRCodes::WxKey::Cancel:
    case IRCodes::WxKey::Menu:
        weatherScenePreviewActive = false;
        showScenePreviewModal();
        break;
    default:
        break;
    }
}

void showSystemModal()
{
    if (currentMenuLevel != MENU_NONE && currentMenuLevel != MENU_MAIN)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_SYSTEM;
    menuActive = true;

    String labels[] = {
        "Sound Volume (0-100)",
        "Sound Profile",
        "Set Date & Time",
        "Unit Settings",
        "Show System Info",
        "WiFi Signal Test",
        "Preview Screens",
        "Learn Remote",
        "Clear Learned Remote",
        "Quick Restore",
        "Factory Reset",
        "Reboot"};

    InfoFieldType types[] = {
        InfoNumber, InfoChooser, InfoButton, InfoButton, InfoButton,
        InfoButton, InfoButton, InfoButton, InfoButton, InfoButton, InfoButton, InfoButton};
    int *numberRefs[] = {&buzzerVolume};
    int *chooserRefs[] = {&buzzerToneSet};
    static const char *toneOpts[] = {"Bright", "Soft", "Click", "Chime", "Pulse", "Warm", "Melody"};
    const char *const *chooserOpts[] = {toneOpts};
    int chooserCounts[] = {7};

    systemModal.setLines(labels, types, 12);
    systemModal.setValueRefs(numberRefs, 1, chooserRefs, 1, chooserOpts, chooserCounts, nullptr, 0, nullptr);
    systemModal.setShowNumberArrows(true);
    systemModal.setShowChooserArrows(true);
    systemModal.setShowForwardArrow(true);
    systemModal.setCallback([](bool accepted, int btnIdx)
                            {
        int action = -1;
        if (btnIdx >= 0)
        {
            action = btnIdx;
        }
        else if (accepted)
        {
            action = systemModal.getSelIndex();
        }

        // Persist volume/profile regardless of which action chosen
        buzzerVolume = constrain(buzzerVolume, 0, 100);
        buzzerToneSet = constrain(buzzerToneSet, 0, 6);
        saveDeviceSettings();

        if (action >= 0)
        {
            switch (action)
            {
            case 0: // volume row -> no navigation
                break;
            case 1: // tone profile row
                break;
            case 2:
                systemModal.hide();
                showDateTimeModal();
                return;
            case 3:
                systemModal.hide();
                pendingModalFn = showUnitSettingsModal;
                pendingModalTime = millis();
                return;
            case 4:
                systemModal.hide();
                showSystemInfoScreen();
                return;
            case 5:
                systemModal.hide();
                showWiFiSignalTest();
                return;
            case 6:
                systemModal.hide();
                showScenePreviewModal();
                return;
            case 7:
                systemModal.hide();
                startUniversalRemoteLearning();
                return;
            case 8:
                systemModal.hide();
                clearUniversalRemoteLearning();
                return;
            case 9:
                systemModal.hide();
                quickRestore();
                break;
            case 10:
                systemModal.hide();
                factoryReset();
                break;
            case 11:
                systemModal.hide();
                ESP.restart();
                return;
            default:
                break;
            }
        }
        // Always return to main menu after hiding systemModal
        systemModal.hide();
        menuStack.clear();
        currentMenuLevel = MENU_MAIN;
        menuActive = true;
        showMainMenuModal(); });
    systemModal.show();
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
    uint16_t headerBg = dma_display->color565(0, 40, 80);   // dark blue background
    uint16_t headerFg = dma_display->color565(0, 255, 255); // cyan text
    dma_display->fillRect(0, 0, screenW, 8, headerBg);
    dma_display->setTextColor(headerFg);
    dma_display->setCursor(1, 0);
    dma_display->print("WIFI LIST");

    // --- Handle no networks ---
    if (wifiScanCount == 0)
    {
        dma_display->setTextColor(dma_display->color565(255, 80, 80));
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
        uint16_t color = selected
                             ? dma_display->color565(255, 255, 0)   // yellow highlight
                             : dma_display->color565(255, 255, 255); // white text
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

// --- System Info/DateTime/WiFi Test Modals (same as before) ---
void showWiFiSignalTest()
{

    if (currentMenuLevel != MENU_NONE)
    {
        pushMenu(currentMenuLevel); // Push current menu to stack
    }
    currentMenuLevel = MENU_SYSWIFI; // You can define this enum, or use MENU_SYSTEM if preferred
    menuActive = true;
    scanWiFiNetworks();
    const int maxLines = 32;
    String lines[maxLines];
    InfoFieldType types[maxLines];
    int lineIndex = 0;

    lines[lineIndex] = "RSSI: " + String(WiFi.RSSI()) + " dBm";
    types[lineIndex++] = InfoLabel;
    lines[lineIndex] = "MAC: " + WiFi.macAddress();
    types[lineIndex++] = InfoLabel;
    lines[lineIndex] = "SSID: " + wifiSSID;
    types[lineIndex++] = InfoLabel;
    lines[lineIndex] = "IP: " + WiFi.localIP().toString();
    types[lineIndex++] = InfoLabel;
    lines[lineIndex] = "Subnet: " + WiFi.subnetMask().toString();
    types[lineIndex++] = InfoLabel;
    lines[lineIndex] = "Gateway: " + WiFi.gatewayIP().toString();
    types[lineIndex++] = InfoLabel;
    lines[lineIndex] = "DNS1: " + WiFi.dnsIP(0).toString();
    types[lineIndex++] = InfoLabel;
    lines[lineIndex] = "Channel: " + String(WiFi.channel());
    types[lineIndex++] = InfoLabel;

    for (int i = 0; i < wifiScanCount - 1 && lineIndex < maxLines; i++)
    {
        lines[lineIndex] = "Net[" + String(i + 1) + "]: " + scannedSSIDs[i];
        types[lineIndex++] = InfoLabel;
    }
    wifiInfoModal.setLines(lines, types, lineIndex);
    wifiInfoModal.show();
}

void showSystemInfoScreen()
{

    if (currentMenuLevel != MENU_NONE && currentMenuLevel != MENU_MAIN)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_SYSINFO; // You can define this enum, or use MENU_SYSTEM if preferred
    menuActive = true;

    uint32_t flashChipSize = ESP.getFlashChipSize();
    uint32_t sketchSize = ESP.getSketchSize();

    const esp_partition_t *running = esp_ota_get_running_partition();
    uint32_t appPartition = running ? running->size : 0;  // dynamic slot size

    uint32_t heapFree = ESP.getFreeHeap();
    uint32_t heapTotal = 327680;
    uint32_t heapUsed = heapTotal - heapFree;
    float heapPct = 100.0 * heapUsed / heapTotal;
    float flashPct = (appPartition > 0) ? (100.0 * sketchSize / appPartition) : 0;

    uint32_t spiffsTotal = SPIFFS.totalBytes();
    uint32_t spiffsUsed = SPIFFS.usedBytes();
    float spiffsPct = (spiffsTotal > 0) ? (100.0 * spiffsUsed / spiffsTotal) : 0;


    String lines[] = {
        "FW: 1.0.0",
        "IP: " + WiFi.localIP().toString(),
        "mDNS: " + deviceHostname + ".local",
        "MAC: " + WiFi.macAddress(),
        "RSSI: " + String(WiFi.RSSI()) + " dBm",
        "RAM:   " + String(heapPct, 1) + "% (" + String(heapUsed) + "/" + String(heapTotal) + " B)",
        "Flash: " + String(flashPct, 1) + "% (" + String(sketchSize) + "/" + String(appPartition) + " B)",
        "SPIFFS: " + String(spiffsPct, 1) + "% (" + String(spiffsUsed / 1024) + "/" + String(spiffsTotal / 1024) + " KB)"};
    InfoFieldType types[] = {InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel};
    sysInfoModal.setLines(lines, types, 8);

    // Minimal callback enables stack-based X/cancel navigation!
    sysInfoModal.setCallback([](bool, int) {});

    sysInfoModal.show();
}


void showDateTimeModal()
{
    if (currentMenuLevel != MENU_NONE)
        pushMenu(currentMenuLevel);
    currentMenuLevel = MENU_SYSDATETIME;
    menuActive = true;

    DateTime now;
    if (rtcReady)
        now = utcToLocal(rtc.now());
    else if (!getLocalDateTime(now))
        now = DateTime(2000, 1, 1, 0, 0, 0);

    dtYear = now.year();
    dtMonth = now.month();
    dtDay = now.day();
    dtHour = now.hour();
    dtMinute = now.minute();
    dtSecond = now.second();

    size_t tzCount = timezoneCount();
    if (tzCount > 31)
        tzCount = 31;

    int currentIndex = timezoneCurrentIndex();
    dtManualOffset = tzStandardOffset;
    dtAutoDst = tzAutoDst ? 1 : 0;
    if (currentIndex >= 0 && currentIndex < static_cast<int>(tzCount))
    {
        dtTimezoneIndex = currentIndex;
        dtManualOffset = timezoneInfoAt(currentIndex).offsetMinutes;
    }
    else
    {
        dtTimezoneIndex = static_cast<int>(tzCount);
        dtAutoDst = 0;
    }
    dtManualOffset = constrain(dtManualOffset, -720, 840);

    dtFmt24 = (fmt24 < 0 || fmt24 > 1) ? 1 : fmt24;
    dtDateFmt = (dateFmt < 0 || dateFmt > 2) ? 0 : dateFmt;

    static char ntpServerBuf[64];
    dtNtpPreset = ntpServerPreset;
    if (dtNtpPreset < 0 || dtNtpPreset > NTP_PRESET_CUSTOM)
        dtNtpPreset = NTP_PRESET_CUSTOM;

    if (dtNtpPreset == NTP_PRESET_CUSTOM)
        strncpy(ntpServerBuf, ntpServerHost, sizeof(ntpServerBuf) - 1);
    else
        strncpy(ntpServerBuf, ntpPresetHost(dtNtpPreset), sizeof(ntpServerBuf) - 1);
    ntpServerBuf[sizeof(ntpServerBuf) - 1] = '\0';

    static const char *const ntpPresetOptions[] = {
        ntpPresetHost(0), ntpPresetHost(1), ntpPresetHost(2), "Custom"};

    static const char *timezoneOptions[32];
    size_t tzOptCount = tzCount;
    for (size_t i = 0; i < tzOptCount; ++i)
    {
        timezoneOptions[i] = timezoneLabelAt(i);
    }
    timezoneOptions[tzOptCount] = "Custom Offset";
    int timezoneChooserCount = static_cast<int>(tzOptCount) + 1;

    static const char *fmt24Opts[] = {"12h", "24h"};
    static const char *dateFmtOpts[] = {"YYYY-MM-DD", "MM/DD/YYYY", "DD/MM/YYYY"};
    static const char *autoDstOpts[] = {"Off", "On"};

    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", dtHour, dtMinute, dtSecond);

    int activeOffset = timezoneOffsetForLocal(now);
    char offsetBuf[16];
    char offsetSign = (activeOffset >= 0) ? '+' : '-';
    int absOffset = abs(activeOffset);
    int offsetHours = absOffset / 60;
    int offsetMinutes = absOffset % 60;
    snprintf(offsetBuf, sizeof(offsetBuf), "UTC%c%02d:%02d", offsetSign, offsetHours, offsetMinutes);

    int currentIdx = timezoneCurrentIndex();
    bool dstActive = tzAutoDst && (activeOffset != tzStandardOffset);

    String tzShort = "Custom";
    if (currentIdx >= 0 && !timezoneIsCustom())
    {
        tzShort = timezoneInfoAt(static_cast<size_t>(currentIdx)).city;
    }

    String localSummary = "Local ";
    localSummary += timeBuf;
    localSummary += " (";
    localSummary += offsetBuf;
    if (tzAutoDst)
    {
        localSummary += dstActive ? " DST" : " Std";
    }
    else if (currentIdx >= 0 && timezoneSupportsDst(static_cast<size_t>(currentIdx)))
    {
        localSummary += " DST-off";
    }
    if (tzShort.length() > 0)
    {
        localSummary += ", ";
        localSummary += tzShort;
    }
    localSummary += ")";

    if (localSummary.length() > 44)
    {
        localSummary.remove(44);
        localSummary += "...";
    }

    String rtcNote = "RTC stores UTC; display applies TZ & DST";

    String lines[16];
    InfoFieldType types[16];
    int lineIdx = 0;

    lines[lineIdx] = localSummary;
    types[lineIdx++] = InfoLabel;

    lines[lineIdx] = "Timezone";
    types[lineIdx++] = InfoChooser;
    lines[lineIdx] = "Manual Offset (min)";
    types[lineIdx++] = InfoNumber;
    lines[lineIdx] = "Auto DST";
    types[lineIdx++] = InfoChooser;
    lines[lineIdx] = "Year";
    types[lineIdx++] = InfoNumber;
    lines[lineIdx] = "Month";
    types[lineIdx++] = InfoNumber;
    lines[lineIdx] = "Day";
    types[lineIdx++] = InfoNumber;
    lines[lineIdx] = "Hour";
    types[lineIdx++] = InfoNumber;
    lines[lineIdx] = "Minute";
    types[lineIdx++] = InfoNumber;
    lines[lineIdx] = "Second";
    types[lineIdx++] = InfoNumber;
    lines[lineIdx] = "Time Format";
    types[lineIdx++] = InfoChooser;
    lines[lineIdx] = "Date Format";
    types[lineIdx++] = InfoChooser;
    lines[lineIdx] = "NTP Preset";
    types[lineIdx++] = InfoChooser;
    lines[lineIdx] = "NTP Server";
    types[lineIdx++] = InfoText;
    lines[lineIdx] = "Sync NTP";
    types[lineIdx++] = InfoButton;
    lines[lineIdx] = rtcNote;
    types[lineIdx++] = InfoLabel;

    int *intRefs[] = {&dtManualOffset, &dtYear, &dtMonth, &dtDay, &dtHour,
                      &dtMinute, &dtSecond};
    int *chooserRefs[] = {&dtTimezoneIndex, &dtAutoDst, &dtFmt24, &dtDateFmt, &dtNtpPreset};
    const char *const *chooserOptPtrs[] = {timezoneOptions, autoDstOpts, fmt24Opts, dateFmtOpts, ntpPresetOptions};
    int chooserOptCounts[] = {timezoneChooserCount, 2, 2, 3, NTP_PRESET_CUSTOM + 1};
    char *textRefs[] = {ntpServerBuf};
    int textSizes[] = {64};

    dateModal.setLines(lines, types, lineIdx);
    dateModal.setValueRefs(intRefs, 7, chooserRefs, 5,
                           chooserOptPtrs, chooserOptCounts,
                           textRefs, 1, textSizes);
    dateModal.setShowNumberArrows(true);

    dateModal.setCallback([](bool accepted, int /*btnIdx*/)
    {
        int sel = dateModal.getSelIndex();
        constexpr int kSyncButtonIndex = 14;
        // --- BEGIN FIX ---
        auto applyCurrentTimezoneSelectionForSync = []() {
            int tzCountInt = static_cast<int>(timezoneCount());
            if (tzCountInt > 31)
                tzCountInt = 31;

            dtManualOffset = constrain(dtManualOffset, -720, 840);
            dtTimezoneIndex = constrain(dtTimezoneIndex, 0, tzCountInt);
            bool useCustomTz = (dtTimezoneIndex == tzCountInt);

            if (useCustomTz)
            {
                setCustomTimezoneOffset(dtManualOffset);
                dtAutoDst = 0;
            }
            else
            {
                selectTimezoneByIndex(dtTimezoneIndex);
                setTimezoneAutoDst(dtAutoDst != 0);
                dtManualOffset = tzStandardOffset;
                dtAutoDst = tzAutoDst ? 1 : 0;
            }
        };
        // --- END FIX ---

        if (sel == kSyncButtonIndex) {
            // --- BEGIN FIX ---
            // Apply current in-memory timezone choice before NTP sync.
            applyCurrentTimezoneSelectionForSync();
            // --- END FIX ---
            dateModal.hide();
            wxv::notify::showNotification(wxv::notify::NotifyId::NtpSync, myWHITE);
            String chosenHost;
            if (dtNtpPreset == NTP_PRESET_CUSTOM) {
                chosenHost = String(ntpServerBuf);
            } else {
                const char *presetHost = ntpPresetHost(dtNtpPreset);
                if (presetHost) chosenHost = String(presetHost);
            }
            setNtpServerFromHostString(chosenHost);
            dtNtpPreset = ntpServerPreset;
            strncpy(ntpServerBuf, ntpServerHost, sizeof(ntpServerBuf) - 1);
            ntpServerBuf[sizeof(ntpServerBuf) - 1] = '\0';

            bool ok = syncTimeFromNTP();
            wxv::notify::showNotification(ok ? wxv::notify::NotifyId::NtpOk : wxv::notify::NotifyId::NtpFail,
                                          ok ? myGREEN : myRED);
            if (ok)
            {
                getTimeFromRTC();
                DateTime localNow;
                if (!getLocalDateTime(localNow))
                {
                    localNow = DateTime(2000, 1, 1, 0, 0, 0);
                }
                dtYear = localNow.year();
                dtMonth = localNow.month();
                dtDay = localNow.day();
                dtHour = localNow.hour();
                dtMinute = localNow.minute();
                dtSecond = localNow.second();
                reset_Time_and_Date_Display = true;
            }
            saveDateTimeSettings();
            pendingModalFn = showDateTimeModal;
            pendingModalTime = millis() + 1500;
            return;
        }

        clampDateTimeFields(dtYear, dtMonth, dtDay, dtHour, dtMinute, dtSecond);
        dtManualOffset = constrain(dtManualOffset, -720, 840);

        int tzCountInt = static_cast<int>(timezoneCount());
        if (tzCountInt > 31)
            tzCountInt = 31;
        dtTimezoneIndex = constrain(dtTimezoneIndex, 0, tzCountInt);
        bool useCustomTz = (dtTimezoneIndex == tzCountInt);

        if (useCustomTz)
        {
            setCustomTimezoneOffset(dtManualOffset);
            dtAutoDst = 0;
        }
        else
        {
            selectTimezoneByIndex(dtTimezoneIndex);
            setTimezoneAutoDst(dtAutoDst != 0);
            dtManualOffset = tzStandardOffset;
        }

        DateTime manualLocal(dtYear, dtMonth, dtDay,
                             dtHour, dtMinute, dtSecond);
        int effectiveOffset = timezoneOffsetForLocal(manualLocal);
        DateTime manualUtc = localToUtc(manualLocal, effectiveOffset);

        if (!rtcReady)
            rtcReady = rtc.begin();
        if (rtcReady)
            rtc.adjust(manualUtc);
        else
            Serial.println("[RTC] Module not available; system clock updated only");

        setSystemTimeFromDateTime(manualUtc);
        updateTimezoneOffsetWithUtc(manualUtc);
        getTimeFromRTC();

        bool formatChanged = (fmt24 != dtFmt24);
        fmt24 = dtFmt24;
        units.clock24h = (fmt24 == 1);
        dateFmt = dtDateFmt;
        String hostSelection;
        if (dtNtpPreset == NTP_PRESET_CUSTOM) {
            hostSelection = String(ntpServerBuf);
        } else {
            const char *presetHost = ntpPresetHost(dtNtpPreset);
            if (presetHost) hostSelection = String(presetHost);
        }
        setNtpServerFromHostString(hostSelection);
        dtNtpPreset = ntpServerPreset;
        strncpy(ntpServerBuf, ntpServerHost, sizeof(ntpServerBuf) - 1);
        ntpServerBuf[sizeof(ntpServerBuf) - 1] = '\0';

        saveDateTimeSettings();
        saveAllSettings();

        reset_Time_and_Date_Display = true;
        if (formatChanged) {
            dma_display->clearScreen();
            drawClockScreen();
            displayDate();
            displayWeatherData();
        }

        dateModal.hide();
    });

    dateModal.show();
}

void showUnitSettingsModal()
{
    if (currentMenuLevel != MENU_NONE)
        pushMenu(currentMenuLevel);
    currentMenuLevel = MENU_SYSUNITS;
    menuActive = true;

    unitTempSel = (units.temp == TempUnit::F) ? 1 : 0;
    unitPressSel = (units.press == PressUnit::INHG) ? 1 : 0;
    unitClockSel = units.clock24h ? 1 : 0;
    switch (units.wind)
    {
    case WindUnit::MPH:
        unitWindSel = 1;
        break;
    case WindUnit::KTS:
        unitWindSel = 2;
        break;
    case WindUnit::KPH:
        unitWindSel = 3;
        break;
    default:
        unitWindSel = 0;
        break;
    }
    unitPrecipSel = (units.precip == PrecipUnit::INCH) ? 1 : 0;

    static const char *tempOpts[] = {"Celsius", "Fahrenheit"};
    static const char *pressOpts[] = {"hPa", "inHg"};
    static const char *clockOpts[] = {"12h", "24h"};
    static const char *windOpts[] = {"m/s", "mph", "knots", "km/h"};
    static const char *precipOpts[] = {"mm", "inches"};

    String labels[] = {"Temperature", "Pressure", "Clock Format", "Wind Speed", "Precipitation"};
    InfoFieldType types[] = {InfoChooser, InfoChooser, InfoChooser, InfoChooser, InfoChooser};
    int *chooserRefs[] = {&unitTempSel, &unitPressSel, &unitClockSel, &unitWindSel, &unitPrecipSel};
    const char *const *chooserOpts[] = {tempOpts, pressOpts, clockOpts, windOpts, precipOpts};
    int chooserCounts[] = {2, 2, 2, 4, 2};

    unitSettingsModal.setLines(labels, types, 5);
    unitSettingsModal.setValueRefs(nullptr, 0, chooserRefs, 5, chooserOpts, chooserCounts);

    unitSettingsModal.setCallback([](bool /*accepted*/, int)
    {
        unitTempSel = constrain(unitTempSel, 0, 1);
        unitPressSel = constrain(unitPressSel, 0, 1);
        unitClockSel = constrain(unitClockSel, 0, 1);
        unitWindSel = constrain(unitWindSel, 0, 3);
        unitPrecipSel = constrain(unitPrecipSel, 0, 1);

        uint16_t prevSig = unitSignature();
        int prevFmt24 = fmt24;

        units.temp = (unitTempSel == 1) ? TempUnit::F : TempUnit::C;
        units.press = (unitPressSel == 1) ? PressUnit::INHG : PressUnit::HPA;
        fmt24 = (unitClockSel == 1) ? 1 : 0;
        units.clock24h = (fmt24 == 1);
        switch (unitWindSel)
        {
        case 1:
            units.wind = WindUnit::MPH;
            break;
        case 2:
            units.wind = WindUnit::KTS;
            break;
        case 3:
            units.wind = WindUnit::KPH;
            break;
        default:
            units.wind = WindUnit::MPS;
            break;
        }
        units.precip = (unitPrecipSel == 1) ? PrecipUnit::INCH : PrecipUnit::MM;

        uint16_t newSig = unitSignature();

        applyUnitPreferences();
        saveUnits();
        saveDateTimeSettings();

        bool clockChanged = (prevFmt24 != fmt24);
        bool signatureChanged = (newSig != prevSig);

        if (signatureChanged)
        {
            displayWeatherData();
            requestScrollRebuild();
            serviceScrollRebuild();
            if (isDataSourceOwm())
            {
                fetchWeatherFromOWM();
            }
        }
        if (clockChanged)
        {
            reset_Time_and_Date_Display = true;
            displayClock();
            displayDate();
        }

        unitSettingsModal.hide();
        currentMenuLevel = MENU_SYSTEM;
    });

    unitSettingsModal.show();
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

void showDeviceSettingsModal()
{
    if (currentMenuLevel != MENU_NONE)
        pushMenu(currentMenuLevel);

    currentMenuLevel = MENU_DEVICE;
    menuActive = true;

    // --- Settings without Wi-Fi setup ---
    String labels[] = {"Day Format", "Data Source", "Manual Screen"};
    InfoFieldType types[] = {InfoChooser, InfoChooser, InfoChooser};

    int *chooserRefs[] = {&dayFormat, &dataSource, &manualScreen};
    static const char *dayFmtOpt[]   = {"MM/DD", "DD/MM"};
    static const char *dataSourceOpt[] = {"OWM", "WeatherFlow", "None"};
    static const char *manualOpt[]   = {"Off", "On"};
    const char *const *chooserOpts[] = {dayFmtOpt, dataSourceOpt, manualOpt};
    int chooserCounts[] = {2, 3, 2};

    // Configure modal (no text fields, no Wi-Fi choosers)
    deviceModal.setLines(labels, types, 3);
    deviceModal.setValueRefs(
        nullptr, 0,                     // no numeric fields
        chooserRefs, 3,                 // 3 chooser fields
        chooserOpts, chooserCounts,     // their options/counts
        nullptr, 0, nullptr             // no text fields
    );

    deviceModal.setButtons(nullptr, 0);

    deviceModal.setCallback([](bool accepted, int)
    {
        if (accepted)
        {
            saveDeviceSettings();
        }
        deviceModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    });

    deviceModal.show();
}

void exitToHomeScreen()
{
    menuStack.clear();
    menuActive = false;
    ScreenMode desiredHome = homeScreenForDataSource();
    currentScreen = enforceAllowedScreen(desiredHome);
    if (currentScreen == SCREEN_OWM)
    {
        dma_display->fillScreen(0);
        if (isDataSourceOwm())
        {
            fetchWeatherFromOWM();
        }
        drawOWMScreen();
    }
    else
    {
        dma_display->fillScreen(0);
        drawClockScreen();
    }
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
