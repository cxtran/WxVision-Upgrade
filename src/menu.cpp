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

extern ScreenMode currentScreen;
// extern const int SCREEN_COUNT;
void handleScreenSwitch(int dir);

InfoModal sysInfoModal("Sys Info");
InfoModal wifiInfoModal("WiFi Info");
InfoModal dateModal("Date/Time");
InfoModal mainMenuModal("Main Menu");
InfoModal deviceModal("Device");
InfoModal displayModal("Display");
InfoModal weatherModal("Weather");
InfoModal calibrationModal("Calibration");
InfoModal systemModal("System");

char wifiSSIDBuf[33]; // max SSID length + 1
char wifiPassBuf[65];

// --- Country Info for Weather Modal ---
const char *countryLabels[] = {
    "Vietnam (VN)", "United States (US)", "Japan (JP)", "Germany (DE)", "India (IN)",
    "France (FR)", "Canada (CA)", "United Kingdom (GB)", "Australia (AU)", "Brazil (BR)", "Custom"};
const char *countryCodes[] = {
    "VN", "US", "JP", "DE", "IN", "FR", "CA", "GB", "AU", "BR", ""};
const int countryCount = sizeof(countryLabels) / sizeof(countryLabels[0]);
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
extern int units, dayFormat, forecastSrc, autoRotate, manualScreen;
extern int theme, brightness, scrollSpeed, scrollLevel;
extern bool autoBrightness;
extern String customMsg;
extern int fmt24, dateFmt, tzOffset;
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
int dtTimezone;
int dtFmt24;
int dtDateFmt;

const char *mainMenu[] = {"Device Settings", "Display Settings", "Weather Settings", "Calibration", "System", "Exit Menu"};
const int mainCount = sizeof(mainMenu) / sizeof(mainMenu[0]);
const char *deviceMenu[] = {"WiFi SSID", "WiFi Pass", "Units", "Day Format", "Forecast Src", "Auto Rotate", "Manual Screen", "< Back"};
const int deviceCount = sizeof(deviceMenu) / sizeof(deviceMenu[0]);
const char *displayMenu[] = {"Theme", "Brightness", "Scroll Spd", "Custom Msg", "< Back"};
const int displayCount = sizeof(displayMenu) / sizeof(displayMenu[0]);
const char *weatherMenu[] = {"OWM City", "Country", "OWM API Key", "WF Token", "WF Station ID", "< Back"};
const int weatherCount = sizeof(weatherMenu) / sizeof(weatherMenu[0]);
const char *calibMenu[] = {"Temp Offset", "Hum Offset", "Light Gain", "< Back"};
const int calibCount = sizeof(calibMenu) / sizeof(calibMenu[0]);
const char *systemMenu[] = {"Show System Info", "Set Date & Time", "WiFi Signal Test", "Quick Restore", "Reset Power", "Factory Reset", "Reboot", "< Back"};
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
                drawMenu(); });
            playBuzzerTone(2200, 120);
            return;
        }
        else if (code == IR_CANCEL)
        {
            wifiSelecting = false;
            currentMenuLevel = MENU_MAIN; // Return to MAIN menu here as well
            menuActive = true;
            currentMenuIndex = 0;
            menuScroll = 0;
            showMainMenuModal(); // Show main menu modal on cancel
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

void showMainMenuModal()
{
    // Main menu is always the root, so clear the menu stack here
    menuStack.clear();
    currentMenuLevel = MENU_MAIN;
    menuActive = true;

    String items[] = {
        "Device Settings", "Display Settings", "Weather Settings",
        "Calibration", "System", "Exit Menu"};
    InfoFieldType types[] = {
        InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel};
    mainMenuModal.setLines(items, types, 6);

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
        case 1: showDisplaySettingsModal(); return;
        case 2: showWeatherSettingsModal(); return;
        case 3: showCalibrationModal(); return;
        case 4: showSystemModal(); return;
        case 5: // Exit Menu
            mainMenuModal.hide(); // Explicitly hide for "Exit Menu" selection
            exitToHomeScreen();
            return;
        default:
            Serial.println("⚠️ Invalid main menu selection");
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
    String labels[] = {"Theme", "Auto Brightness", "Brightness", "Scroll Speed", "Custom Msg"};
    InfoFieldType types[] = {InfoChooser, InfoChooser, InfoNumber, InfoChooser, InfoText};
    int *chooserRefs[] = {&theme, &autoBrightnessInt, &scrollLevelTemp};
    static const char *themeOpts[] = {"Color", "Mono"};
    static const char *autoOpts[] = {"Off", "On"};
    static const char *speedOpts[] = {
        "1 - Slow", "2", "3", "4", "5", "6", "7", "8", "9", "10 - Fast"};
    static const char *const *chooserOpts[] = {themeOpts, autoOpts, speedOpts};
    int chooserCounts[] = {2, 2, 10};
    int *numberRefs[] = {&brightnessTemp};
    static char customMsgBuf[64];
    strncpy(customMsgBuf, customMsg.c_str(), sizeof(customMsgBuf));
    customMsgBuf[sizeof(customMsgBuf) - 1] = 0;
    char *textRefs[] = {customMsgBuf};
    int textSizes[] = {sizeof(customMsgBuf)};
    displayModal.setLines(labels, types, 5);
    displayModal.setValueRefs(numberRefs, 1, chooserRefs, 3, chooserOpts, chooserCounts, textRefs, 1, textSizes);

    displayModal.setCallback([](bool accepted, int btnIdx)
                             {
        
        if (accepted) {
            brightness = constrain(brightnessTemp, 1, 100);
            autoBrightness = (autoBrightnessInt > 0);
            scrollLevel = constrain(scrollLevelTemp, 0, 9);
            scrollSpeed = scrollDelays[scrollLevel];
            customMsg = String(customMsgBuf);
            saveDisplaySettings();
            Serial.printf("[Saved] brightness=%d, scrollLevel=%d → scrollSpeed=%d autoBrightness=%d\n",
                brightness, scrollLevel + 1, scrollSpeed, autoBrightness);
        }
        displayModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0; });
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
    static char wfTokenBuf[48];
    static char wfStationBuf[16];
    strncpy(owmCountryCustomBuf, owmCountryCustom.c_str(), sizeof(owmCountryCustomBuf));
    strncpy(owmCityBuf, owmCity.c_str(), sizeof(owmCityBuf));
    strncpy(owmKeyBuf, owmApiKey.c_str(), sizeof(owmKeyBuf));
    strncpy(wfTokenBuf, wfToken.c_str(), sizeof(wfTokenBuf));
    strncpy(wfStationBuf, wfStationId.c_str(), sizeof(wfStationBuf));

    String labels[] = {"Country", "Custom Code", "City", "OWM API Key", "WF Token", "WF Station ID"};
    InfoFieldType types[] = {InfoChooser, InfoText, InfoText, InfoText, InfoText, InfoText};
    int *chooserRefs[] = {&owmCountryIndexTemp};
    const char *const *chooserOpts[] = {countryLabels};
    int chooserCounts[] = {countryCount};
    char *textRefs[] = {owmCountryCustomBuf, owmCityBuf, owmKeyBuf, wfTokenBuf, wfStationBuf};
    int textSizes[] = {sizeof(owmCountryCustomBuf), sizeof(owmCityBuf), sizeof(owmKeyBuf), sizeof(wfTokenBuf), sizeof(wfStationBuf)};

    weatherModal.setLines(labels, types, 6);
    weatherModal.setValueRefs(nullptr, 0, chooserRefs, 1, chooserOpts, chooserCounts, textRefs, 5, textSizes);

    weatherModal.setCallback([](bool ok, int btnIdx)
                             {
        owmCountryIndex = owmCountryIndexTemp;
        owmCountryCustom = String(owmCountryCustomBuf);
        owmCity = String(owmCityBuf);
        owmApiKey = String(owmKeyBuf);
        wfToken = String(wfTokenBuf);
        wfStationId = String(wfStationBuf);

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

void showCalibrationModal()
{
    if (currentMenuLevel != MENU_NONE)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_CALIBRATION;
    menuActive = true;

    static int tempOffsetTemp = tempOffset;
    static int humOffsetTemp = humOffset;
    static int lightGainTemp = lightGain;
    String labels[] = {"Temp Offset", "Hum Offset", "Light Gain"};
    InfoFieldType types[] = {InfoNumber, InfoNumber, InfoNumber};
    int *numberRefs[] = {&tempOffsetTemp, &humOffsetTemp, &lightGainTemp};
    calibrationModal.setLines(labels, types, 3);
    calibrationModal.setValueRefs(numberRefs, 3, nullptr, 0, nullptr, nullptr);

    calibrationModal.setCallback([](bool accepted, int btnIdx)
                                 {
        tempOffset = constrain(tempOffsetTemp, -10, 10);
        humOffset = constrain(humOffsetTemp, -20, 20);
        lightGain = constrain(lightGainTemp, 1, 150);
        saveCalibrationSettings();
        float lux = readBrightnessSensor();
        setDisplayBrightnessFromLux(lux);
        Serial.printf("[Saved] tempOffset=%d, humOffset=%d, lightGain=%d\n", tempOffset, humOffset, lightGain);
        calibrationModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0; });
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
        "WiFi Signal Test",
        "Quick Restore",
        "Reset Power",
        "Factory Reset",
        "Reboot"};

    InfoFieldType types[] = {
        InfoButton, InfoButton, InfoButton, InfoButton, InfoButton, InfoButton, InfoButton};
    systemModal.setLines(labels, types, 7);
    systemModal.setCallback([](bool accepted, int btnIdx)
                            {
        int sel = systemModal.getSelIndex();
        if (accepted && btnIdx >= 0) {
            switch (sel) {
                case 0: showSystemInfoScreen(); return;
                case 1: showDateTimeModal();    return;
                case 2: showWiFiSignalTest();   return;
                case 3: quickRestore();         break;
                case 4: resetPowerUsage();      break;
                case 5: factoryReset();         break;
                case 6: ESP.restart();          break;
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
    delay(100);

    int n = WiFi.scanNetworks();
    int j = 0;
    for (int i = 0; i < n && j < 15; ++i)
    {
        String ssid = WiFi.SSID(i);
        ssid.trim();
        if (ssid.length() == 0)
            continue;
        scannedSSIDs[j++] = ssid;
    }
    scannedSSIDs[j++] = "< Back";
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
    int count = (currentMenuLevel == MENU_MAIN)          ? mainCount
                : (currentMenuLevel == MENU_DEVICE)      ? deviceCount
                : (currentMenuLevel == MENU_DISPLAY)     ? displayCount
                : (currentMenuLevel == MENU_WEATHER)     ? weatherCount
                : (currentMenuLevel == MENU_CALIBRATION) ? calibCount
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
    dma_display->setTextColor(dma_display->color565(0, 255, 255));
    dma_display->setCursor(0, 0);
    dma_display->print("Select WiFi:");
    if (wifiScanCount == 0)
    {
        dma_display->setTextColor(dma_display->color565(255, 80, 80));
        dma_display->setCursor(0, 10);
        dma_display->print("No WiFi found.");
        return;
    }
    int maxScroll = max(0, wifiScanCount - wifiVisibleLines);
    if (wifiMenuScroll > maxScroll)
        wifiMenuScroll = maxScroll;
    if (wifiMenuScroll < 0)
        wifiMenuScroll = 0;
    const int labelHeight = 7, listStartY = labelHeight + 1, wifiLineHeight = 8;
    for (int i = 0; i < wifiVisibleLines; ++i)
    {
        int idx = wifiMenuScroll + i;
        if (idx >= wifiScanCount)
            break;
        if (idx == wifiSelectIndex)
            dma_display->setTextColor(dma_display->color565(255, 255, 0));
        else
            dma_display->setTextColor(dma_display->color565(255, 255, 255));
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
    uint32_t appPartition = 0x190000;
    uint32_t heapFree = ESP.getFreeHeap();
    uint32_t heapTotal = 327680;
    uint32_t heapUsed = heapTotal - heapFree;
    float heapPct = 100.0 * heapUsed / heapTotal;
    float flashPct = 100.0 * sketchSize / appPartition;
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
    {
        pushMenu(currentMenuLevel); // Push current menu to stack
    }
    currentMenuLevel = MENU_SYSDATETIME; // You can define this enum, or use MENU_SYSTEM if preferred
    menuActive = true;

    DateTime now = rtc.now();
    dtYear = now.year();
    dtMonth = now.month();
    dtDay = now.day();
    dtHour = now.hour();
    dtMinute = now.minute();
    dtSecond = now.second();
    dtTimezone = tzOffset;
    dtFmt24 = (fmt24 < 0 || fmt24 > 1) ? 1 : fmt24;
    dtDateFmt = (dateFmt < 0 || dateFmt > 2) ? 0 : dateFmt;
    static char ntpServer[64] = "pool.ntp.org";

    String lines[] = {"Year", "Month", "Day", "Hour", "Minute", "Second", "TimeZone", "TimeFmt", "DateFmt", "NTP Server", "Sync NTP"};
    InfoFieldType types[] = {InfoNumber, InfoNumber, InfoNumber, InfoNumber, InfoNumber, InfoNumber, InfoNumber, InfoChooser, InfoChooser, InfoText, InfoButton};

    int *intRefs[] = {&dtYear, &dtMonth, &dtDay, &dtHour, &dtMinute, &dtSecond, &dtTimezone};
    int *chooserRefs[] = {&dtFmt24, &dtDateFmt};
    static const char *fmt24Opts[] = {"12h", "24h"};
    static const char *dateFmtOpts[] = {"YYYY-MM-DD", "MM/DD/YYYY", "DD/MM/YYYY"};
    static const char *const *chooserOptPtrs[] = {fmt24Opts, dateFmtOpts};
    int chooserOptCounts[] = {2, 3};
    char *textRefs[] = {ntpServer};
    int textSizes[] = {64};

    dateModal.setLines(lines, types, 11);
    dateModal.setValueRefs(intRefs, 7, chooserRefs, 2, chooserOptPtrs, chooserOptCounts, textRefs, 1, textSizes);

    dateModal.setCallback([](bool accepted, int btnIdx)
                          {
        int sel = dateModal.getSelIndex();
        if (sel == 10) {
            dateModal.hide();
            dma_display->fillScreen(0);
            dma_display->setCursor(8, 12);
            dma_display->setTextColor(myWHITE);
            dma_display->print("Syncing NTP...");
            syncTimeFromNTP();
            pendingModalFn = showDateTimeModal;
            pendingModalTime = millis() + 50;
            return;
        }
        dtMonth = constrain(dtMonth, 1, 12);
        dtDay = constrain(dtDay, 1, 31);
        dtHour = constrain(dtHour, 0, 23);
        dtMinute = constrain(dtMinute, 0, 59);
        dtSecond = constrain(dtSecond, 0, 59);
        dtYear = constrain(dtYear, 2000, 2099);
        dtTimezone = constrain(dtTimezone, -720, 840);
        rtc.adjust(DateTime(dtYear, dtMonth, dtDay, dtHour, dtMinute, dtSecond));
        tzOffset = dtTimezone;
        fmt24 = dtFmt24;
        dateFmt = dtDateFmt;
        saveDateTimeSettings();
        dateModal.hide(); });
    dateModal.show();
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
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_DEVICE;
    menuActive = true;

    // If WiFi is connected, skip scanning now.
    // Instead just prepare SSID chooser with current connected SSID only.
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);

    if (!wifiConnected)
    {
        // Only scan WiFi networks if not connected and scan needed
        if (wifiSelectNeedsScan || wifiScanCount == 0)
        {
            scanWiFiNetworks();
            wifiSelectNeedsScan = false;
        }
    }
    else
    {
        // If connected, show only current SSID in the chooser (no scan)
        wifiScanCount = 1;
        scannedSSIDs[0] = WiFi.SSID();
    }

    // Find index of current SSID for default selection
    wifiSelectIndex = 0;
    for (int i = 0; i < wifiScanCount; ++i)
    {
        if (scannedSSIDs[i] == wifiSSID)
        {
            wifiSelectIndex = i;
            break;
        }
    }

    // Prepare persistent storage for SSID chooser
    static String ssidStringStore[16];
    static const char *ssidOptions[16];
    int modalSsidCount = wifiScanCount;
    if (modalSsidCount > 0 && scannedSSIDs[modalSsidCount - 1] == "< Back")
        modalSsidCount--;

    for (int i = 0; i < modalSsidCount; ++i)
    {
        ssidStringStore[i] = scannedSSIDs[i];
        ssidOptions[i] = ssidStringStore[i].c_str();
    }

    // Prepare password buffer
    static char wifiPassBuf[65];
    strncpy(wifiPassBuf, wifiPass.c_str(), sizeof(wifiPassBuf) - 1);
    wifiPassBuf[sizeof(wifiPassBuf) - 1] = 0;

    // Modal labels and field types
    String labels[] = {
        "WiFi SSID", "WiFi Pass",
        "Units", "Day Format", "Forecast Src", "Auto Rotate", "Manual Screen"};
    InfoFieldType types[] = {
        InfoChooser, InfoText,
        InfoChooser, InfoChooser, InfoChooser, InfoChooser, InfoChooser};

    int *chooserRefs[] = {&wifiSelectIndex, &units, &dayFormat, &forecastSrc, &autoRotate, &manualScreen};
    static const char *unitsOpt[] = {"F+mph", "C+m/s"};
    static const char *dayFmtOpt[] = {"MM/DD", "DD/MM"};
    static const char *forecastOpt[] = {"OWM", "WF"};
    static const char *rotateOpt[] = {"Off", "On"};
    static const char *manualOpt[] = {"Off", "On"};
    const char *const *chooserOpts[] = {ssidOptions, unitsOpt, dayFmtOpt, forecastOpt, rotateOpt, manualOpt};
    int chooserCounts[] = {modalSsidCount, 2, 2, 2, 2, 2};

    char *textRefs[] = {wifiPassBuf};
    int textSizes[] = {sizeof(wifiPassBuf)};

    deviceModal.setLines(labels, types, 7);
    deviceModal.setValueRefs(
        nullptr, 0,
        chooserRefs, 6,
        chooserOpts, chooserCounts,
        textRefs, 1, textSizes);
    deviceModal.setButtons(nullptr, 0);

    deviceModal.setCallback([](bool accepted, int btnIdx)
                            {
        int sel = deviceModal.getSelIndex();
        if (accepted) {
            if (sel == 0) {
                // User selected WiFi SSID chooser -> switch to WiFi selecting mode
                deviceModal.hide();
                wifiSelecting = true;
                currentMenuLevel = MENU_WIFI_SELECT;
                scanWiFiNetworks();
                menuActive = true;
                drawWiFiMenu();
                return;
            }
            // Save other settings normally
            if (wifiSelectIndex >= 0 && wifiSelectIndex < wifiScanCount)
                wifiSSID = scannedSSIDs[wifiSelectIndex];
            wifiPass = String(wifiPassBuf);
            saveDeviceSettings();
        }
        deviceModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0; });

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
