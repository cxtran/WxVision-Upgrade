#include <Arduino.h>
#include "buzzer.h"
#include "menu.h"
#include "display.h"
#include <vector>
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
#include "weather_countries.h"
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
InfoModal setupPromptModal("Welcome");
InfoModal wifiSettingsModal("WiFi Setting");
InfoModal unitSettingsModal("Units");

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
std::vector<String> foundSSIDs;
int selectedWifiIdx = 0;
String scannedSSIDs[16];
int wifiScanCount = 0;
int wifiSelectIndex = 0;
bool wifiSelectNeedsScan = false;

extern String wifiSSID;
extern String wifiPass;
extern String owmCity;
extern String owmApiKey;
extern String wfToken;
extern String wfStationId;
extern int humOffset;
extern int owmCountryIndex;
extern String owmCountryCustom;
extern int tempOffset;
extern int lightGain;
extern int dayFormat, forecastSrc, autoRotate, autoRotateInterval, manualScreen;
extern UnitPrefs units;
extern int theme, brightness, scrollSpeed, scrollLevel;
extern bool autoBrightness;
extern String customMsg;
extern int fmt24, dateFmt;
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

const char *mainMenu[] = {"Device Settings", "WiFi Settings", "Display Settings", "OW Map", "WF Tempest", "Calibration", "System", "Exit Menu"};
const int mainCount = sizeof(mainMenu) / sizeof(mainMenu[0]);

const char *deviceMenu[] = {"WiFi SSID", "WiFi Pass", "Day Format", "Forecast Src", "Manual Screen", "< Back"};
const int deviceCount = sizeof(deviceMenu) / sizeof(deviceMenu[0]);
const char *displayMenu[] = {"Theme", "Auto Brightness", "Brightness", "Scroll Spd", "Auto Rotate", "Rotate Interval", "Custom Msg", "< Back"};
const int displayCount = sizeof(displayMenu) / sizeof(displayMenu[0]);
const char *weatherMenu[] = {"Country", "Custom Code", "OWM City", "OWM API Key", "< Back"};
const int weatherCount = sizeof(weatherMenu) / sizeof(weatherMenu[0]);
const char *tempestMenu[] = {"WF Token", "WF Station ID", "< Back"};
const int tempestCount = sizeof(tempestMenu) / sizeof(tempestMenu[0]);
const char *calibMenu[] = {"Temp Offset", "Hum Offset", "Light Gain", "< Back"};
const int calibCount = sizeof(calibMenu) / sizeof(calibMenu[0]);
const char *systemMenu[] = {"Show System Info", "Set Date & Time", "Unit Settings", "WiFi Signal Test", "Quick Restore", "Reset Power", "Factory Reset", "Reboot", "< Back"};
const int systemCount = sizeof(systemMenu) / sizeof(systemMenu[0]);


void handleIR(uint32_t code)
{
    Serial.printf("IR Code: 0x%X\n", code);

    if (code == IR_SCREEN)
    {
        if (isScreenOff())
            setScreenOff(false);
        else
            setScreenOff(true);
        return;
    }
    if (isScreenOff())
        return;

    if (inKeyboardMode)
    {
        handleKeyboardIR(code);
        return;
    }
    if (sysInfoModal.isActive())
    {
        sysInfoModal.handleIR(code);
        return;
    }
    if (wifiInfoModal.isActive())
    {
        wifiInfoModal.handleIR(code);
        return;
    }
    if (dateModal.isActive())
    {
        dateModal.handleIR(code);
        return;
    }
    if (unitSettingsModal.isActive())
    {
        unitSettingsModal.handleIR(code);
        return;
    }
    if (mainMenuModal.isActive())
    {
        mainMenuModal.handleIR(code);
        return;
    }
    if (deviceModal.isActive())
    {
        deviceModal.handleIR(code);
        return;
    }
    if (displayModal.isActive())
    {
        displayModal.handleIR(code);
        return;
    }
    if (weatherModal.isActive())
    {
        weatherModal.handleIR(code);
        return;
    }
    if (tempestModal.isActive())
    {
        tempestModal.handleIR(code);
        return;
    }
    if (calibrationModal.isActive())
    {
        calibrationModal.handleIR(code);
        return;
    }
    if (systemModal.isActive())
    {
        systemModal.handleIR(code);
        return;
    }

    // WiFi select (not modal, custom menu)
    if (currentMenuLevel == MENU_WIFI_SELECT)
    {
        if (wifiScanCount == 0)
        {
            currentMenuLevel = MENU_DEVICE;
            drawMenu();
            return;
        }
        if (code == IR_UP)
        {
            handleUp();
            playBuzzerTone(1000, 60);
        }
        else if (code == IR_DOWN)
        {
            handleDown();
            playBuzzerTone(1300, 60);
        }
        else if (code == IR_OK)
        {
            if (wifiSelectIndex == wifiScanCount - 1)
            { // <Back>
                wifiSelecting = false;
                currentMenuLevel = MENU_MAIN; // Return to MAIN menu instead of DEVICE menu
                menuActive = true;
                currentMenuIndex = 0;
                menuScroll = 0;
                showMainMenuModal(); // Show main menu modal
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
                if (result) {
                    wifiPass = String(result);
                    saveDeviceSettings();
                    connectToWiFi();
                }
                drawMenu(); },"Enter Pwd:");
            playBuzzerTone(2200, 120);
            return;
        }
        else if (code == IR_CANCEL)
        {
            bool onboardingCancel = initialSetupAwaitingWifi;
            wifiSelecting = false;
            if (onboardingCancel)
            {
                menuActive = false;
                currentMenuLevel = MENU_NONE;
                menuScroll = 0;
                handleInitialSetupDecision(false);
            }
            else
            {
                currentMenuLevel = MENU_MAIN; // Return to MAIN menu here as well
                menuActive = true;
                currentMenuIndex = 0;
                menuScroll = 0;
                showMainMenuModal(); // Show main menu modal on cancel
            }
            playBuzzerTone(700, 80);
        }
        return;
    }

    static unsigned long lastMenuToggle = 0;
    if (!menuActive)
    {
        if (code == IR_LEFT)
        {
            handleScreenSwitch(-1);
            return;
        }
        if (code == IR_RIGHT)
        {
            handleScreenSwitch(+1);
            return;
        }
        if (code == IR_CANCEL)
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

    switch (code)
    {
    case IR_CANCEL:
        menuActive = false;
        dma_display->clearScreen();
        delay(50);
        fetchWeatherFromOWM();
        displayClock();
        displayDate();
        displayWeatherData();
        reset_Time_and_Date_Display = true;
        lastMenuToggle = millis();
        break;
    case IR_UP:
        handleUp();
        playBuzzerTone(1500, 100);
        break;
    case IR_DOWN:
        handleDown();
        playBuzzerTone(1200, 100);
        break;
    case IR_RIGHT:
        handleRight();
        playBuzzerTone(1800, 100);
        break;
    case IR_LEFT:
        handleLeft();
        playBuzzerTone(900, 100);
        break;
    case IR_OK:
        handleSelect();
        playBuzzerTone(2200, 100);
        break;
    default:
        Serial.printf("Unknown code: 0x%X\n", code);
        playBuzzerTone(500, 100);
        delay(100);
        playBuzzerTone(500, 100);
        break;
    }
}

// --- Modal-based Menu Functions --- //

void showInitialSetupPrompt()
{
    menuStack.clear();
    menuStack.push_back(MENU_INITIAL_SETUP);
    menuActive = true;
    currentMenuLevel = MENU_INITIAL_SETUP;

    String lines[] = { "Connect to WiFi?"};
    InfoFieldType types[] = {InfoLabel};
    setupPromptModal.setLines(lines, types, 1);

    String buttons[] = {"Connect", "Skip"};
    setupPromptModal.setButtons(buttons, 2);

    setupPromptModal.setCallback([](bool accepted, int btnIdx)
                                 {
        setupPromptModal.hide();
        menuStack.clear();
        if (!accepted)
        {
            handleInitialSetupDecision(false);
            return;
        }
        handleInitialSetupDecision(btnIdx == 0);
    });

    setupPromptModal.show();
}

void showMainMenuModal()
{
    // Main menu is always the root, so clear the menu stack here
    menuStack.clear();
    currentMenuLevel = MENU_MAIN;
    menuActive = true;

    String items[] = {
        "Device Settings", "WiFi Settings", "Display Settings",
        "OW Map", "WF Tempest", "Calibration", "System", "Exit Menu"};
    InfoFieldType types[] = {
        InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel};
    mainMenuModal.setLines(items, types, 8);

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
            case 3: showWeatherSettingsModal(); return;
            case 4: showWfTempestModal(); return;
            case 5: showCalibrationModal(); return;
            case 6: showSystemModal(); return;
            case 7: // Exit Menu
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
    if (currentMenuLevel != MENU_NONE)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_DISPLAY;
    menuActive = true;

    static int autoBrightnessInt;
    autoBrightnessInt = autoBrightness ? 1 : 0;
    static int brightnessTemp = brightness;
    static int scrollLevelTemp = 3;
    for (int i = 0; i < 10; ++i)
    {
        if (scrollSpeed >= scrollDelays[i])
        {
            scrollLevelTemp = i;
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

    String labels[] = {"Theme", "Auto Brightness", "Brightness", "Scroll Speed", "Auto Rotate", "Rotate Interval", "Custom Msg"};
    InfoFieldType types[] = {InfoChooser, InfoChooser, InfoNumber, InfoChooser, InfoChooser, InfoChooser, InfoText};
    int *chooserRefs[] = {&theme, &autoBrightnessInt, &scrollLevelTemp, &autoRotateTemp, &rotateIntervalIndex};
    static const char *themeOpts[] = {"Color", "Mono"};
    static const char *autoOpts[] = {"Off", "On"};
    static const char *speedOpts[] = {"1 - Slow", "2", "3", "4", "5", "6", "7", "8", "9", "10 - Fast"};
    static const char *autoRotateOpt[] = {"Off", "On"};
    const char *const *chooserOpts[] = {themeOpts, autoOpts, speedOpts, autoRotateOpt, rotateIntervalOpt};
    int chooserCounts[] = {2, 2, 10, 2, rotateIntervalCount};
    int *numberRefs[] = {&brightnessTemp};

    static char customMsgBuf[64];
    strncpy(customMsgBuf, customMsg.c_str(), sizeof(customMsgBuf));
    customMsgBuf[sizeof(customMsgBuf) - 1] = 0;
    char *textRefs[] = {customMsgBuf};
    int textSizes[] = {sizeof(customMsgBuf)};

    displayModal.setLines(labels, types, 7);
    displayModal.setValueRefs(numberRefs, 1, chooserRefs, 5, chooserOpts, chooserCounts, textRefs, 1, textSizes);

    displayModal.setCallback([](bool accepted, int)
    {
        if (accepted)
        {
            brightness = constrain(brightnessTemp, 1, 100);
            autoBrightness = (autoBrightnessInt > 0);
            scrollLevel = constrain(scrollLevelTemp, 0, 9);
            scrollSpeed = scrollDelays[scrollLevel];
            setAutoRotateEnabled(autoRotateTemp > 0, true);
            setAutoRotateInterval(rotateIntervalValues[rotateIntervalIndex], true);
            customMsg = String(customMsgBuf);
            saveDisplaySettings();
            Serial.printf("[Saved] brightness=%d, scrollLevel=%d -> scrollSpeed=%d autoBrightness=%d autoRotate=%d interval=%ds\n",
                          brightness, scrollLevel + 1, scrollSpeed, autoBrightness, autoRotate, autoRotateInterval);
        }
        displayModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    });
    displayModal.show();
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
        owmCountryIndex = owmCountryIndexTemp;
        owmCountryCustom = String(owmCountryCustomBuf);
        owmCity = String(owmCityBuf);
        owmApiKey = String(owmKeyBuf);

        if (owmCountryIndex < 10) {
            owmCountryCode = countryCodes[owmCountryIndex];
        } else {
            owmCountryCode = owmCountryCustom;
        }

        saveWeatherSettings();
        Serial.printf("[WeatherModal] Saved Country=%s (%s), City=%s\n",
            countryLabels[owmCountryIndex], owmCountryCode.c_str(), owmCity.c_str());
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
        wfToken = String(wfTokenBuf);
        wfStationId = String(wfStationBuf);
        saveWeatherSettings();
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

    // Bind directly to globals
    String labels[] = {"Temp Offset (C)", "Hum Offset (%)", "Light Gain (%)"};
    InfoFieldType types[] = {InfoNumber, InfoNumber, InfoNumber};
    int* numberRefs[] = { &tempOffset, &humOffset, &lightGain };

    calibrationModal.setLines(labels, types, 3);
    calibrationModal.setValueRefs(numberRefs, 3, nullptr, 0, nullptr, nullptr);

    // No final ???OK??? save. We autosave on each change in handleIR().
    calibrationModal.setCallback([](bool, int) {
        calibrationModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    });

    calibrationModal.show();
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
        "Show System Info",
        "Set Date & Time",
        "Unit Settings",
        "WiFi Signal Test",
        "Quick Restore",
        "Reset Power",
        "Factory Reset",
        "Reboot"};

    InfoFieldType types[] = {
        InfoButton, InfoButton, InfoButton, InfoButton, InfoButton, InfoButton, InfoButton, InfoButton};
    systemModal.setLines(labels, types, 8);
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

        if (action >= 0)
        {
            switch (action)
            {
            case 0:
                systemModal.hide();
                showSystemInfoScreen();
                return;
            case 1:
                systemModal.hide();
                showDateTimeModal();
                return;
            case 2:
                systemModal.hide();
                pendingModalFn = showUnitSettingsModal;
                pendingModalTime = millis();
                return;
            case 3:
                systemModal.hide();
                showWiFiSignalTest();
                return;
            case 4:
                systemModal.hide();
                quickRestore();
                break;
            case 5:
                systemModal.hide();
                resetPowerUsage();
                break;
            case 6:
                systemModal.hide();
                factoryReset();
                break;
            case 7:
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

void scanWiFiNetworks()
{
    wifiScanCount = 0;
    wifiSelecting = true;

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // ensure radio is awake
    delay(200);           // give it a moment to power up

    Serial.println("[WiFi] Scanning networks...");

    int n = WiFi.scanNetworks(false, true); // blocking, include hidden
    if (n <= 0) {
        Serial.println("[WiFi] No networks found, retrying...");
        delay(500);
        n = WiFi.scanNetworks(false, true);
    }

    int j = 0;
    for (int i = 0; i < n && j < 15; ++i)
    {
        String ssid = WiFi.SSID(i);
        ssid.trim();
        if (ssid.isEmpty()) continue;
        scannedSSIDs[j++] = ssid;
    }

    scannedSSIDs[j++] = "< Back>";
    wifiScanCount = j;
    wifiSelectIndex = 0;
    wifiMenuScroll = 0;
    wifiSelectNeedsScan = false;

    Serial.printf("[scanWiFiNetworks] Found %d networks (+Back)\n", wifiScanCount - 1);
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
    dma_display->print("Select WiFi:");

    // --- Handle no networks ---
    if (wifiScanCount == 0)
    {
        dma_display->setTextColor(dma_display->color565(255, 80, 80));
        dma_display->setCursor(0, 10);
        dma_display->print("No WiFi found.");
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

        uint16_t color = (idx == wifiSelectIndex)
                             ? dma_display->color565(255, 255, 0)   // yellow highlight
                             : dma_display->color565(255, 255, 255); // white text
        dma_display->setTextColor(color);
        dma_display->setCursor(0, listStartY + wifiLineHeight * i);
        dma_display->print(scannedSSIDs[idx]);
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
        "MAC: " + WiFi.macAddress(),
        "RSSI: " + String(WiFi.RSSI()) + " dBm",
        "RAM:   " + String(heapPct, 1) + "% (" + String(heapUsed) + "/" + String(heapTotal) + " B)",
        "Flash: " + String(flashPct, 1) + "% (" + String(sketchSize) + "/" + String(appPartition) + " B)",
        "SPIFFS: " + String(spiffsPct, 1) + "% (" + String(spiffsUsed / 1024) + "/" + String(spiffsTotal / 1024) + " KB)"};
    InfoFieldType types[] = {InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel};
    sysInfoModal.setLines(lines, types, 7);

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

    String lines[] = {"Year", "Month", "Day", "Hour", "Minute", "Second",
                      "Timezone", "Manual Offset (min)", "Auto DST",
                      "Time Format", "Date Format",
                      "NTP Preset", "NTP Server", "Sync NTP"};
    InfoFieldType types[] = {InfoNumber, InfoNumber, InfoNumber, InfoNumber, InfoNumber, InfoNumber,
                             InfoChooser, InfoNumber, InfoChooser,
                             InfoChooser, InfoChooser,
                             InfoChooser, InfoText, InfoButton};

    int *intRefs[] = {&dtYear, &dtMonth, &dtDay, &dtHour, &dtMinute,
                      &dtSecond, &dtManualOffset};
    int *chooserRefs[] = {&dtTimezoneIndex, &dtAutoDst, &dtFmt24, &dtDateFmt, &dtNtpPreset};
    const char *const *chooserOptPtrs[] = {timezoneOptions, autoDstOpts, fmt24Opts, dateFmtOpts, ntpPresetOptions};
    int chooserOptCounts[] = {timezoneChooserCount, 2, 2, 3, NTP_PRESET_CUSTOM + 1};
    char *textRefs[] = {ntpServerBuf};
    int textSizes[] = {64};

    dateModal.setLines(lines, types, 14);
    dateModal.setValueRefs(intRefs, 7, chooserRefs, 5,
                           chooserOptPtrs, chooserOptCounts,
                           textRefs, 1, textSizes);

    dateModal.setCallback([](bool accepted, int /*btnIdx*/)
    {
        int sel = dateModal.getSelIndex();
        constexpr int kSyncButtonIndex = 13;

        if (sel == kSyncButtonIndex) {
            dateModal.hide();
            dma_display->fillScreen(0);
            dma_display->setCursor(8, 12);
            dma_display->setTextColor(myWHITE);
            dma_display->print("Syncing NTP...");
            ntpServerPreset = dtNtpPreset;

            if (ntpServerPreset == NTP_PRESET_CUSTOM) {
                String customHost = String(ntpServerBuf);
                customHost.trim();
                if (customHost.length() == 0) customHost = "pool.ntp.org";
                customHost.toCharArray(ntpServerHost, sizeof(ntpServerHost));
                customHost.toCharArray(ntpServerBuf, sizeof(ntpServerBuf));
            } else {
                const char *resolved = ntpPresetHost(ntpServerPreset);
                strncpy(ntpServerHost, resolved, sizeof(ntpServerHost) - 1);
                ntpServerHost[sizeof(ntpServerHost) - 1] = '\0';
                strncpy(ntpServerBuf, ntpServerHost, sizeof(ntpServerBuf) - 1);
                ntpServerBuf[sizeof(ntpServerBuf) - 1] = '\0';
            }

            bool ok = syncTimeFromNTP();
            dma_display->fillScreen(0);
            dma_display->setCursor(4, 12);
            dma_display->setTextColor(ok ? myGREEN : myRED);
            dma_display->print(ok ? "NTP sync OK" : "NTP sync failed");
            saveDateTimeSettings();
            pendingModalFn = showDateTimeModal;
            pendingModalTime = millis() + 1500;
            return;
        }

        dtMonth = constrain(dtMonth, 1, 12);
        dtDay = constrain(dtDay, 1, 31);
        dtHour = constrain(dtHour, 0, 23);
        dtMinute = constrain(dtMinute, 0, 59);
        dtSecond = constrain(dtSecond, 0, 59);
        dtYear = constrain(dtYear, 2000, 2099);
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

        bool formatChanged = (fmt24 != dtFmt24);
        fmt24 = dtFmt24;
        units.clock24h = (fmt24 == 1);
        dateFmt = dtDateFmt;
        ntpServerPreset = dtNtpPreset;

        if (ntpServerPreset == NTP_PRESET_CUSTOM) {
            String customHost = String(ntpServerBuf);
            customHost.trim();
            if (customHost.length() == 0) customHost = "pool.ntp.org";
            customHost.toCharArray(ntpServerHost, sizeof(ntpServerHost));
            customHost.toCharArray(ntpServerBuf, sizeof(ntpServerBuf));
        } else {
            const char *resolved = ntpPresetHost(ntpServerPreset);
            strncpy(ntpServerHost, resolved, sizeof(ntpServerHost) - 1);
            ntpServerHost[sizeof(ntpServerHost) - 1] = '\0';
            strncpy(ntpServerBuf, ntpServerHost, sizeof(ntpServerBuf) - 1);
            ntpServerBuf[sizeof(ntpServerBuf) - 1] = '\0';
        }

        saveDateTimeSettings();
        saveAllSettings();

        if (formatChanged) {
            reset_Time_and_Date_Display = true;
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
            fetchWeatherFromOWM();
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
    String labels[] = {"Day Format", "Forecast Src", "Manual Screen"};
    InfoFieldType types[] = {InfoChooser, InfoChooser, InfoChooser};

    int *chooserRefs[] = {&dayFormat, &forecastSrc, &manualScreen};
    static const char *dayFmtOpt[]   = {"MM/DD", "DD/MM"};
    static const char *forecastOpt[] = {"OWM", "WF"};
    static const char *manualOpt[]   = {"Off", "On"};
    const char *const *chooserOpts[] = {dayFmtOpt, forecastOpt, manualOpt};
    int chooserCounts[] = {2, 2, 2};

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
    dma_display->clearScreen();
    delay(50);
    fetchWeatherFromOWM();
    displayClock();
    displayDate();
    displayWeatherData();
    reset_Time_and_Date_Display = true;
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
                dma_display->fillScreen(0);
                dma_display->setTextColor(dma_display->color565(0, 255, 255));
                dma_display->setCursor(1, 8);
                dma_display->println("Scanning");
           //     dma_display->setTextColor(dma_display->color565(180, 180, 180));
                dma_display->setCursor(25, 18);
                dma_display->println("WiFi...");
           //     dma_display->setCursor(1, 16);
           //     dma_display->println("Please wait...");
            //    dma_display->setTextColor(dma_display->color565(255, 255, 255));
                dma_display->setCursor(0, 28);

                // Perform scan (blocking but reliable)
                WiFi.mode(WIFI_STA);
                delay(100);
                int n = WiFi.scanNetworks();
                Serial.printf("[WiFiSettings] Scan complete: %d networks\n", n);

                if (n > 0)
                {
                    wifiScanCount = 0;
                    for (int i = 0; i < n && wifiScanCount < 15; ++i)
                    {
                        String ssid = WiFi.SSID(i);
                        ssid.trim();
                        if (ssid.length() == 0)
                            continue;
                        scannedSSIDs[wifiScanCount++] = ssid;
                    }
                    scannedSSIDs[wifiScanCount++] = "< Back";
                    wifiSelectIndex = 0;
                    wifiMenuScroll = 0;

                    // Go to WiFi select screen
                    wifiSelecting = true;
                    currentMenuLevel = MENU_WIFI_SELECT;
                    menuActive = true;
                    menuScroll = 0;
                    drawWiFiMenu();
                }
                else
                {
                    // Show "No networks found" briefly
                    dma_display->fillScreen(0);
                    dma_display->setCursor(5, 10);
                    dma_display->setTextColor(dma_display->color565(255, 100, 100));
                    dma_display->println("No WiFi found");
                    dma_display->setCursor(5, 20);
                    dma_display->setTextColor(dma_display->color565(200, 200, 200));
                    dma_display->println("Try again...");
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
                dma_display->fillScreen(0);
                dma_display->setCursor(2, 8);
                dma_display->setTextColor(dma_display->color565(255, 255, 0));
                dma_display->println("Connecting...");
                dma_display->setCursor(2, 16);
                dma_display->setTextColor(dma_display->color565(0, 200, 255));
                dma_display->println(wifiSSID);
                connectToWiFi();
                return;
            }
        }

        wifiSettingsModal.hide();
        showMainMenuModal();
    });

    wifiSettingsModal.show();
}


