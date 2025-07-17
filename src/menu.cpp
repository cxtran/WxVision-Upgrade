#include <Arduino.h>
#include "buzzer.h"
#include "menu.h"
#include "display.h"
#include <Preferences.h>
#include "settings.h"
#include "system.h"
#include "utils.h"
#include "WiFi.h"
#include "keyboard.h"   // <-- Required for keyboard mode

MenuLevel currentMenuLevel = MENU_MAIN;
int currentMenuIndex = 0;
unsigned long lastMenuActivity = 0;
extern bool menuActive;
int menuScroll = 0;
const int visibleLines = 4;
extern int scrollOffset;
int wifiMenuScroll = 0;
const int wifiVisibleLines = 3;

bool wifiSelecting = false;

std::vector<String> foundSSIDs;
int selectedWifiIdx = 0;

String scannedSSIDs[16]; // Up to 16 SSIDs
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

const char *mainMenu[] = {"Device Settings", "Display Settings", "Weather Settings", "Calibration", "System Actions", "Save & Exit"};
const int mainCount = sizeof(mainMenu) / sizeof(mainMenu[0]);

const char *deviceMenu[] = {
    "WiFi SSID", "WiFi Pass", "Units", "Day Format", "Forecast Src", "Auto Rotate", "Manual Screen", "Exit & Save", "< Back"};
const int deviceCount = sizeof(deviceMenu) / sizeof(deviceMenu[0]);

const char *displayMenu[] = {"Theme", "Brightness", "Scroll Spd", "Custom Msg", "Exit & Save", "< Back"};
const int displayCount = sizeof(displayMenu) / sizeof(displayMenu[0]);

const char *weatherMenu[] = {"OWM City", "OWM API Key", "WF Token", "WF Station ID", "Exit & Save", "< Back"};
const int weatherCount = sizeof(weatherMenu) / sizeof(weatherMenu[0]);

const char *calibMenu[] = {"Temp Offset", "Hum Offset", "Light Gain", "Exit & Save", "< Back"};
const int calibCount = sizeof(calibMenu) / sizeof(calibMenu[0]);

const char *systemMenu[] = {"OTA Update", "Reset Power", "Quick Restore", "Factory Reset", "Exit & Save", "< Back"};
const int systemCount = sizeof(systemMenu) / sizeof(systemMenu[0]);

void scanWiFiNetworks() {
    wifiScanCount = 0;
    wifiSelecting = true;

    WiFi.mode(WIFI_STA);      // Set WiFi to Station mode (required for scanning)
    delay(100);

    int n = WiFi.scanNetworks();
    int j = 0;
    for (int i = 0; i < n && j < 15; ++i) {  // Leave room for "Back"
        String ssid = WiFi.SSID(i);
        ssid.trim();
        if (ssid.length() == 0) continue;
        scannedSSIDs[j++] = ssid;
    }
    // Always add "< Back" as last menu entry
    scannedSSIDs[j++] = "< Back";
    wifiScanCount = j;
    wifiSelectIndex = 0;
    wifiMenuScroll = 0;
    wifiSelectNeedsScan = false;
    Serial.printf("[scanWiFiNetworks] Found %d networks (+Back)\n", wifiScanCount-1);
}

void handleIR(uint32_t code)
{
    // --- Keyboard mode always has top priority ---
    if (inKeyboardMode) {
        handleKeyboardIR(code);
        return;
    }

    Serial.printf("IR Code: 0x%X\n", code);

    // -------- 1. WiFi Select Mode --------
    if (currentMenuLevel == MENU_WIFI_SELECT)
    {
        const uint32_t IR_UP = 0xFFFF30CF;     // CH+
        const uint32_t IR_DOWN = 0xFFFF906F;   // CH-
        const uint32_t IR_OK = 0xFFFF48B7;     // OK
        const uint32_t IR_CANCEL = 0xFFFF08F7; // Power/Menu

        if (wifiScanCount == 0)
        {
            // No networks scanned, just return to device menu
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
            // --- Handle Back option ---
            if (wifiSelectIndex == wifiScanCount - 1) // Last entry is "< Back"
            {
                wifiSelecting = false;
                currentMenuLevel = MENU_DEVICE;
                drawMenu();
                playBuzzerTone(900, 80);
                return;
            }
            // Block selecting "(No networks)"!
            if (scannedSSIDs[wifiSelectIndex] == "(No networks)")
            {
                playBuzzerTone(500, 100); // error tone
                return;                   // Do nothing if fake SSID is selected
            }
            // Picked a network: set SSID, prompt for password (or reuse old pass)
            wifiSSID = scannedSSIDs[wifiSelectIndex];
            Serial.printf("Selected SSID: %s\n", wifiSSID.c_str());
            currentMenuLevel = MENU_DEVICE;
            currentMenuIndex = 1; // WiFi Pass
            menuScroll = 0;
            drawMenu();
            // --- Use keyboard for password entry ---
            startKeyboardEntry(wifiPass.c_str(), [](const char* result){
                if(result) {
                    wifiPass = String(result);
                    saveDeviceSettings();
                    connectToWiFi();
                }
                drawMenu();
            });
            playBuzzerTone(2200, 120);
            return;
        }
        else if (code == IR_CANCEL)
        {
            wifiSelecting = false;
            currentMenuLevel = MENU_DEVICE;
            drawMenu();
            playBuzzerTone(700, 80);
        }
        return; // No further menu processing
    }

    // -------- 3. Menu NOT Active --------
    if (!menuActive)
    {
        if (code == 0xFFFF08F7) // Power/Menu
        {
            menuActive = true;
            drawMenu();
            playBuzzerTone(3000, 100);
        }
        return;
    }

    // -------- 4. Normal Menu Navigation --------
    switch (code)
    {
    case 0xFFFF08F7: // Power/Menu (exit menu)
        menuActive = false;
        dma_display->clearScreen();
        delay(50);
        playBuzzerTone(3000, 100);
        fetchWeatherFromOWM();
        displayClock();
        displayDate();
        displayWeatherData();
        reset_Time_and_Date_Display = true;
        break;
    case 0xFFFF30CF:
        handleUp();
        playBuzzerTone(1500, 100);
        break;
    case 0xFFFF906F:
        handleDown();
        playBuzzerTone(1200, 100);
        break;
    case 0xFFFFE01F:
        handleRight();
        playBuzzerTone(1800, 100);
        break;
    case 0xFFFF50AF:
        handleLeft();
        playBuzzerTone(900, 100);
        break;
    case 0xFFFF48B7:
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

void drawMenu()
{
    // If keyboard mode, let keyboard handle drawing!
    if (inKeyboardMode) {
        drawKeyboard();
        return;
    }

    // 2. If WiFi Select Mode, always call drawWiFiMenu() and return
    if (currentMenuLevel == MENU_WIFI_SELECT)
    {
        drawWiFiMenu();
        return;
    }

    // 3. Normal menu rendering (unchanged)
    dma_display->fillScreen(dma_display->color565(0, 0, 0));
    dma_display->setCursor(0, 0);
    dma_display->setFont(&Font5x7Uts);

    int count = (currentMenuLevel == MENU_MAIN)          ? mainCount
                : (currentMenuLevel == MENU_DEVICE)      ? deviceCount
                : (currentMenuLevel == MENU_DISPLAY)     ? displayCount
                : (currentMenuLevel == MENU_WEATHER)     ? weatherCount
                : (currentMenuLevel == MENU_CALIBRATION) ? calibCount
                                                         : systemCount;

    for (int i = 0; i < visibleLines; i++)
    {
        int itemIdx = menuScroll + i;
        if (itemIdx >= count)
            break;

        bool isSelected = (itemIdx == currentMenuIndex);
        uint16_t color = isSelected ? dma_display->color565(255, 0, 0) : dma_display->color565(255, 255, 255);

        char line[64];
        line[0] = 0;

        if (currentMenuLevel == MENU_MAIN && itemIdx < mainCount)
        {
            sprintf(line, "%s", mainMenu[itemIdx]);
        }
        else if (currentMenuLevel == MENU_DEVICE && itemIdx < deviceCount)
        {
            if (itemIdx == 0)
                sprintf(line, "WiFi SSID: %s", wifiSSID.c_str());
            else if (itemIdx == 1)
                sprintf(line, "WiFi Pass: %s", wifiPass.c_str());
            else if (itemIdx == 2)
                sprintf(line, "Units: %s", (units == 0) ? "F+mph" : "C+m/s");
            else if (itemIdx == 3)
                sprintf(line, "DayFmt: %s", (dayFormat == 0) ? "MM/DD" : "DD/MM");
            else if (itemIdx == 4)
                sprintf(line, "Forecast: %s", (forecastSrc == 0) ? "OWM" : "WF");
            else if (itemIdx == 5)
                sprintf(line, "AutoRot: %s", (autoRotate == 0) ? "Off" : "On");
            else if (itemIdx == 6)
                sprintf(line, "ManualScr");
            else if (itemIdx == deviceCount - 2)
                sprintf(line, "Exit & Save");
            else if (itemIdx == deviceCount - 1)
                sprintf(line, "< Back");
        }
        else if (currentMenuLevel == MENU_DISPLAY && itemIdx < displayCount)
        {
            if (itemIdx == 0)
                sprintf(line, "Theme: %s", (theme == 0) ? "Color" : "Mono");
            else if (itemIdx == 1)
                sprintf(line, "Bright: %d%%", brightness);
            else if (itemIdx == 2)
                sprintf(line, "Scroll: %d", scrollSpeed);
            else if (itemIdx == 3)
                sprintf(line, "Custom Msg");
            else if (itemIdx == displayCount - 2)
                sprintf(line, "Exit & Save");
            else if (itemIdx == displayCount - 1)
                sprintf(line, "< Back");
        }
        else if (currentMenuLevel == MENU_WEATHER && itemIdx < weatherCount)
        {
            if (itemIdx == 0)
                sprintf(line, "OWM City: %s", owmCity.c_str());
            else if (itemIdx == 1)
                snprintf(line, sizeof(line), "OWM Key: %.30s", owmApiKey.c_str());
            else if (itemIdx == 2)
                sprintf(line, "WF Token: %s", wfToken.c_str());
            else if (itemIdx == 3)
                sprintf(line, "WF ID: %s", wfStationId.c_str());
            else if (itemIdx == weatherCount - 2)
                sprintf(line, "Exit & Save");
            else if (itemIdx == weatherCount - 1)
                sprintf(line, "< Back");
        }
        else if (currentMenuLevel == MENU_CALIBRATION && itemIdx < calibCount)
        {
            if (itemIdx == 0)
                sprintf(line, "TempOff: %+d", tempOffset);
            else if (itemIdx == 1)
                sprintf(line, "HumOff: %+d", humOffset);
            else if (itemIdx == 2)
                sprintf(line, "LightG: %d%%", lightGain);
            else if (itemIdx == calibCount - 2)
                sprintf(line, "Exit & Save");
            else if (itemIdx == calibCount - 1)
                sprintf(line, "< Back");
        }
        else if (currentMenuLevel == MENU_SYSTEM && itemIdx < systemCount)
        {
            if (itemIdx == 0)
                sprintf(line, "OTA Update");
            else if (itemIdx == 1)
                sprintf(line, "Reset Power");
            else if (itemIdx == 2)
                sprintf(line, "Quick Restore");
            else if (itemIdx == 3)
                sprintf(line, "Factory Reset");
            else if (itemIdx == systemCount - 2)
                sprintf(line, "Exit & Save");
            else if (itemIdx == systemCount - 1)
                sprintf(line, "< Back");
        }
        else
        {
            strcpy(line, "");
        }

        if (isSelected && needsScroll(line))
        {
            drawScrollingText(line, i * 8, color, itemIdx);
        }
        else
        {
            dma_display->setTextColor(color);
            dma_display->setCursor(0, i * 8);
            dma_display->print(line);
        }
    }
}

void handleUp() {
    lastMenuActivity = millis();
    scrollOffset = 0;

    if (currentMenuLevel == MENU_WIFI_SELECT)
    {
        if (wifiScanCount > 0)
        {
            if (wifiSelectIndex > 0)
            {
                wifiSelectIndex--;
                // Scroll window up if selection goes above the visible window
                if (wifiSelectIndex < wifiMenuScroll)
                {
                    wifiMenuScroll = wifiSelectIndex;
                }
            }
            // Clamp scroll (for when list shrinks)
            if (wifiMenuScroll < 0)
                wifiMenuScroll = 0;
            int maxScroll = max(0, wifiScanCount - wifiVisibleLines);
            if (wifiMenuScroll > maxScroll)
                wifiMenuScroll = maxScroll;
        }
        drawWiFiMenu();
        return;
    }

    // Other menu navigation unchanged
    currentMenuIndex--;
    if (currentMenuIndex < 0)
        currentMenuIndex = 0;
    if (currentMenuIndex < menuScroll)
        menuScroll = currentMenuIndex;
    Serial.printf("[UP] Index: %d, Scroll: %d, Visible: %d, Count: %d\n", wifiSelectIndex, wifiMenuScroll, wifiVisibleLines, wifiScanCount);

    drawMenu();
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
                // Scroll window down if selection goes below the visible window
                if (wifiSelectIndex >= wifiMenuScroll + wifiVisibleLines)
                {
                    wifiMenuScroll = wifiSelectIndex - wifiVisibleLines + 1;
                }
            }
            // Clamp scroll so we never show blank window
            int maxScroll = max(0, wifiScanCount - wifiVisibleLines);
            if (wifiMenuScroll > maxScroll)
                wifiMenuScroll = maxScroll;
            if (wifiMenuScroll < 0)
                wifiMenuScroll = 0;
        }
        drawWiFiMenu();
        return;
    }

    // Other menu navigation unchanged
    int count = (currentMenuLevel == MENU_MAIN)          ? mainCount
                : (currentMenuLevel == MENU_DEVICE)      ? deviceCount
                : (currentMenuLevel == MENU_DISPLAY)     ? displayCount
                : (currentMenuLevel == MENU_WEATHER)     ? weatherCount
                : (currentMenuLevel == MENU_CALIBRATION) ? calibCount
                                                         : systemCount;

    currentMenuIndex++;
    if (currentMenuIndex >= count)
        currentMenuIndex = count - 1;
    if (currentMenuIndex >= menuScroll + visibleLines)
        menuScroll = currentMenuIndex - visibleLines + 1;

    Serial.printf("[DOWN] Index: %d, Scroll: %d, Visible: %d, Count: %d\n", wifiSelectIndex, wifiMenuScroll, wifiVisibleLines, wifiScanCount);

    drawMenu();
}

void handleLeft()
{
    lastMenuActivity = millis();

    if (currentMenuLevel == MENU_WIFI_SELECT)
        return; // no left/right in wifi select

    if (currentMenuLevel == MENU_DEVICE)
    {
        if (currentMenuIndex == 2)
            toggleUnits(-1);
        if (currentMenuIndex == 3)
            toggleDayFormat(-1);
        if (currentMenuIndex == 4)
            toggleForecastSrc(-1);
        if (currentMenuIndex == 5)
            toggleAutoRotate(-1);
    }
    else if (currentMenuLevel == MENU_DISPLAY)
    {
        if (currentMenuIndex == 0)
            toggleTheme(-1);
        if (currentMenuIndex == 1)
        {
            // Decrease brightness, clamp to 0..255, update hardware
            brightness--;
            if (brightness < 4)
                brightness = 3;
            dma_display->setBrightness8(brightness);
        }
        if (currentMenuIndex == 2)
            adjustScrollSpeed(-1);
    }
    else if (currentMenuLevel == MENU_CALIBRATION)
    {
        if (currentMenuIndex == 0)
            adjustTempOffset(-1);
        if (currentMenuIndex == 1)
            adjustHumOffset(-1);
        if (currentMenuIndex == 2)
            adjustLightGain(-1);
    }
    drawMenu();
}

void handleRight()
{
    lastMenuActivity = millis();

    if (currentMenuLevel == MENU_WIFI_SELECT)
        return; // no left/right in wifi select

    if (currentMenuLevel == MENU_DEVICE)
    {
        if (currentMenuIndex == 2)
            toggleUnits(1);
        if (currentMenuIndex == 3)
            toggleDayFormat(1);
        if (currentMenuIndex == 4)
            toggleForecastSrc(1);
        if (currentMenuIndex == 5)
            toggleAutoRotate(1);
    }
    else if (currentMenuLevel == MENU_DISPLAY)
    {
        if (currentMenuIndex == 0)
            toggleTheme(1);
        if (currentMenuIndex == 1)
        {
            // Increase brightness, clamp to 0..255, update hardware
            brightness++;
            if (brightness < 0)
                brightness = 0;
            dma_display->setBrightness8(brightness);
        }
        if (currentMenuIndex == 2)
            adjustScrollSpeed(1);
    }
    else if (currentMenuLevel == MENU_CALIBRATION)
    {
        if (currentMenuIndex == 0)
            adjustTempOffset(1);
        if (currentMenuIndex == 1)
            adjustHumOffset(1);
        if (currentMenuIndex == 2)
            adjustLightGain(1);
    }
    drawMenu();
}

void handleSelect()
{
    lastMenuActivity = millis();

    // Defensive: clamp index
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
        return; // If keyboard is active, do nothing

    if (currentMenuLevel == MENU_MAIN)
    {
        switch (currentMenuIndex)
        {
        case 0:
            currentMenuLevel = MENU_DEVICE;
            currentMenuIndex = 0;
            menuScroll = 0;
            break;
        case 1:
            currentMenuLevel = MENU_DISPLAY;
            currentMenuIndex = 0;
            menuScroll = 0;
            break;
        case 2:
            currentMenuLevel = MENU_WEATHER;
            currentMenuIndex = 0;
            menuScroll = 0;
            break;
        case 3:
            currentMenuLevel = MENU_CALIBRATION;
            currentMenuIndex = 0;
            menuScroll = 0;
            break;
        case 4:
            currentMenuLevel = MENU_SYSTEM;
            currentMenuIndex = 0;
            menuScroll = 0;
            break;
        case 5:
            saveAllSettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
            menuActive = false;
            dma_display->clearScreen();
            delay(50);
            fetchWeatherFromOWM();
            displayClock();
            displayDate();
            displayWeatherData();
            reset_Time_and_Date_Display = true;
            break;
        }
    }
    else if (currentMenuLevel == MENU_DEVICE)
    {
        if (currentMenuIndex == 0)
        {
            // Always scan for WiFi every time you enter this menu!
            scanWiFiNetworks(); // <---- ALWAYS scan here
            currentMenuLevel = MENU_WIFI_SELECT;
            wifiSelectIndex = 0;
            wifiMenuScroll = 0;
            drawWiFiMenu();
            return;
        }
        if (currentMenuIndex == 1)
        {
            // ---- Use keyboard for password entry ----
            startKeyboardEntry(wifiPass.c_str(), [](const char* result){
                if(result) {
                    wifiPass = String(result);
                    saveDeviceSettings();
                    connectToWiFi();
                }
                drawMenu();
            });
            return;
        }

        if (currentMenuIndex == deviceCount - 2)
        {
            saveDeviceSettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == deviceCount - 1)
        {
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
    }
    else if (currentMenuLevel == MENU_DISPLAY)
    {
        if (currentMenuIndex == displayCount - 2)
        {
            saveDisplaySettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == displayCount - 1)
        {
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        // Optionally add keyboard entry for custom message:
        else if (currentMenuIndex == 3) {
            // Keyboard entry for Custom Msg
            startKeyboardEntry(customMsg.c_str(), [](const char* result){
                if(result) customMsg = String(result);
                drawMenu();
            });
            return;
        }
    }
    else if (currentMenuLevel == MENU_WEATHER)
    {
        if (currentMenuIndex == 0)
        {
            startKeyboardEntry(owmCity.c_str(), [](const char* result){
                if(result) owmCity = String(result);
                drawMenu();
            });
            return;
        }
        if (currentMenuIndex == 1)
        {
            startKeyboardEntry(owmApiKey.c_str(), [](const char* result){
                if(result) owmApiKey = String(result);
                drawMenu();
            });
            return;
        }
        if (currentMenuIndex == 2)
        {
            startKeyboardEntry(wfToken.c_str(), [](const char* result){
                if(result) wfToken = String(result);
                drawMenu();
            });
            return;
        }
        if (currentMenuIndex == 3)
        {
            startKeyboardEntry(wfStationId.c_str(), [](const char* result){
                if(result) wfStationId = String(result);
                drawMenu();
            });
            return;
        }
        if (currentMenuIndex == weatherCount - 2)
        {
            saveWeatherSettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == weatherCount - 1)
        {
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
    }
    else if (currentMenuLevel == MENU_CALIBRATION)
    {
        if (currentMenuIndex == calibCount - 2)
        {
            saveCalibrationSettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == calibCount - 1)
        {
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
    }
    else if (currentMenuLevel == MENU_SYSTEM)
    {
        if (currentMenuIndex == systemCount - 2)
        {
            saveSystemSettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == systemCount - 1)
        {
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == 0)
            startOTA();
        else if (currentMenuIndex == 1)
            resetPowerUsage();
        else if (currentMenuIndex == 2)
            quickRestore();
        else if (currentMenuIndex == 3)
            factoryReset();
    }

    // --- Draw correct menu depending on currentMenuLevel
    if (currentMenuLevel == MENU_WIFI_SELECT)
    {
        drawWiFiMenu();
    }
    else
    {
        drawMenu();
    }
}

void updateMenu() { drawMenu(); }

bool wifiScanInProgress = false;

void drawWiFiMenu()
{
    dma_display->fillScreen(dma_display->color565(0, 0, 0)); // Clear screen
    dma_display->setFont(&Font5x7Uts);

    // Draw label
    dma_display->setTextColor(dma_display->color565(0, 255, 255));
    dma_display->setCursor(0, 0);
    dma_display->print("Select WiFi:");

    // Show "Scanning..." if scan is in progress
    if (wifiScanInProgress)
    {
        dma_display->setTextColor(dma_display->color565(255, 255, 80));
        dma_display->setCursor(0, 10);
        dma_display->print("Scanning...");
        return;
    }

    // Show "No WiFi found" if scan is complete but none are found
    if (wifiScanCount == 0)
    {
        dma_display->setTextColor(dma_display->color565(255, 80, 80));
        dma_display->setCursor(0, 10);
        dma_display->print("No WiFi found.");
        return;
    }

    // Clamp scroll to avoid blanks
    int maxScroll = max(0, wifiScanCount - wifiVisibleLines);
    if (wifiMenuScroll > maxScroll)
        wifiMenuScroll = maxScroll;
    if (wifiMenuScroll < 0)
        wifiMenuScroll = 0;

    const int labelHeight = 7;
    const int listStartY = labelHeight + 1;
    const int wifiLineHeight = 8;

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
    // Do a scan and update scannedSSIDs[] like your menu code
    scanWiFiNetworks(); // <-- Your safe menu-managed scanner
    wifiSelectIndex = 0;
    wifiMenuScroll = 0;
    currentMenuLevel = MENU_WIFI_SELECT;
    menuActive = true;
    drawMenu(); // Will call drawWiFiMenu() as needed
}
