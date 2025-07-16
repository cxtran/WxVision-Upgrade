#include <Arduino.h>
#include "buzzer.h"
#include "menu.h"
#include "display.h"
#include <Preferences.h>
#include "settings.h"
#include "system.h"
#include "utils.h"
#include "WiFi.h"
#include "bluetoothsettings.h"

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

// For field editing:
bool editingField = false;
char editBuffer[64] = "";
int editCharIdx = 0;
void drawBLEMenu();


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

const char* deviceMenu[] = {
    "WiFi SSID", "WiFi Pass", "Bluetooth", "Units", "Day Format", "Forecast Src", "Auto Rotate", "Manual Screen", "Exit & Save", "Back"
};
const int deviceCount = sizeof(deviceMenu)/sizeof(deviceMenu[0]);

const char* displayMenu[] = {"Theme", "Brightness", "Scroll Spd", "Custom Msg", "Exit & Save", "Back"};
const int displayCount = sizeof(displayMenu)/sizeof(displayMenu[0]);

const char* weatherMenu[] = {"OWM City", "OWM API Key", "WF Token", "WF Station ID", "Exit & Save", "Back"};
const int weatherCount = sizeof(weatherMenu)/sizeof(weatherMenu[0]);

const char* calibMenu[] = {"Temp Offset", "Hum Offset", "Light Gain", "Exit & Save", "Back"};
const int calibCount = sizeof(calibMenu)/sizeof(calibMenu[0]);

const char* systemMenu[] = {"OTA Update", "Reset Power", "Quick Restore", "Factory Reset", "Exit & Save", "Back"};
const int systemCount = sizeof(systemMenu)/sizeof(systemMenu[0]);



void scanWiFiNetworks() {
    wifiScanCount = 0;
    int n = WiFi.scanNetworks();
    if (n <= 0) {
        scannedSSIDs[0] = "(No networks)";
        wifiScanCount = 1;
    } else {
        int j = 0;
        for (int i = 0; i < n && j < 16; ++i) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue; // skip blank SSIDs!
            scannedSSIDs[j++] = ssid;
        }
        wifiScanCount = j > 0 ? j : 1;
        if (wifiScanCount == 1 && scannedSSIDs[0].length() == 0) {
            scannedSSIDs[0] = "(No networks)";
        }
    }
    wifiSelectIndex = 0;
    wifiMenuScroll = 0;
    wifiSelectNeedsScan = false;
}


void handleIR(uint32_t code)
{
    Serial.printf("IR Code: 0x%X\n", code);

    // -------- 1. WiFi Select Mode --------
    if (currentMenuLevel == MENU_WIFI_SELECT) {
        const uint32_t IR_UP    = 0xFFFF30CF;  // CH+
        const uint32_t IR_DOWN  = 0xFFFF906F;  // CH-
        const uint32_t IR_OK    = 0xFFFF48B7;  // OK
        const uint32_t IR_CANCEL = 0xFFFF08F7; // Power/Menu

        if (wifiScanCount == 0) {
            // No networks scanned, just return to device menu
            currentMenuLevel = MENU_DEVICE;
            drawMenu();
            return;
        }

        if (code == IR_UP && wifiSelectIndex > 0) {
            wifiSelectIndex--;
            Serial.printf("WiFi Select: %d/%d %s\n", wifiSelectIndex, wifiScanCount, scannedSSIDs[wifiSelectIndex].c_str());
            drawWiFiMenu();
            playBuzzerTone(1000, 60);
        } else if (code == IR_DOWN && wifiSelectIndex < wifiScanCount - 1) {
            wifiSelectIndex++;
            Serial.printf("WiFi Select: %d/%d %s\n", wifiSelectIndex, wifiScanCount, scannedSSIDs[wifiSelectIndex].c_str());
            drawWiFiMenu();
            playBuzzerTone(1300, 60);
        } else if (code == IR_OK) {
            // Picked a network: set SSID, prompt for password (or reuse old pass)
            wifiSSID = scannedSSIDs[wifiSelectIndex];
            Serial.printf("Selected SSID: %s\n", wifiSSID.c_str());
            currentMenuLevel = MENU_DEVICE;
            currentMenuIndex = 1; // WiFi Pass
            menuScroll = 0;
            drawMenu();
            startEditField(wifiPass.c_str());
            playBuzzerTone(2200, 120);
            return;
        } else if (code == IR_CANCEL) {
            currentMenuLevel = MENU_DEVICE;
            drawMenu();
            playBuzzerTone(700, 80);
        }
        return; // No further menu processing
    }

    // -------- 2. Edit Mode --------
    if (editingField) {
        const uint32_t IR_UP    = 0xFFFF30CF;
        const uint32_t IR_DOWN  = 0xFFFF906F;
        const uint32_t IR_LEFT  = 0xFFFF50AF;
        const uint32_t IR_RIGHT = 0xFFFFE01F;
        const uint32_t IR_OK    = 0xFFFF48B7;
        const uint32_t IR_CANCEL = 0xFFFF08F7;

        const char* charSet = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()-_=+[]{};:,.<>?/|";
        int charSetLen = strlen(charSet);

        char current = editBuffer[editCharIdx];
        int idx = strchr(charSet, current) ? (strchr(charSet, current) - charSet) : 0;

        switch (code) {
            case IR_UP:
                idx = (idx + 1) % charSetLen;
                editBuffer[editCharIdx] = charSet[idx];
                playBuzzerTone(1000, 40);
                break;
            case IR_DOWN:
                idx = (idx - 1 + charSetLen) % charSetLen;
                editBuffer[editCharIdx] = charSet[idx];
                playBuzzerTone(1000, 40);
                break;
            case IR_RIGHT:
                if (editCharIdx < (int)sizeof(editBuffer) - 2) {
                    editCharIdx++;
                    if (editBuffer[editCharIdx] == 0) editBuffer[editCharIdx] = ' ';
                }
                playBuzzerTone(1500, 40);
                break;
            case IR_LEFT:
                if (editCharIdx > 0) editCharIdx--;
                playBuzzerTone(800, 40);
                break;
            case IR_OK:
                finishEditField();
                playBuzzerTone(2200, 100);
                return;
            case IR_CANCEL:
                editingField = false;
                drawMenu();
                playBuzzerTone(700, 80);
                return;
            default:
                playBuzzerTone(500, 100);
                break;
        }
        drawEditField();
        return;
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
    // 1. If editing a field, show the edit UI instead of the menu!
    if (editingField) {
        drawEditField();
        return;
    }

    // 2. If WiFi Select Mode, always call drawWiFiMenu() and return
    if (currentMenuLevel == MENU_WIFI_SELECT) {
        drawWiFiMenu();
        return;
    }

    // 3. Normal menu rendering
    dma_display->fillScreen(dma_display->color565(0, 0, 0));
    dma_display->setCursor(0, 0);
    dma_display->setFont(&Font5x7Uts);

    int count = (currentMenuLevel == MENU_MAIN) ? mainCount
        : (currentMenuLevel == MENU_DEVICE) ? deviceCount
        : (currentMenuLevel == MENU_DISPLAY) ? displayCount
        : (currentMenuLevel == MENU_WEATHER) ? weatherCount
        : (currentMenuLevel == MENU_CALIBRATION) ? calibCount
        : systemCount;

    for (int i = 0; i < visibleLines; i++) {
        int itemIdx = menuScroll + i;
        if (itemIdx >= count) break;

        bool isSelected = (itemIdx == currentMenuIndex);
        uint16_t color = isSelected ? dma_display->color565(255, 0, 0) : dma_display->color565(255, 255, 255);

        char line[64];
        line[0] = 0;

        if (currentMenuLevel == MENU_MAIN && itemIdx < mainCount) {
            sprintf(line, "%s", mainMenu[itemIdx]);
        }
        else if (currentMenuLevel == MENU_DEVICE && itemIdx < deviceCount) {
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
                sprintf(line, "Back");
        }
        else if (currentMenuLevel == MENU_DISPLAY && itemIdx < displayCount) {
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
                sprintf(line, "Back");
        }
        else if (currentMenuLevel == MENU_WEATHER && itemIdx < weatherCount) {
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
                sprintf(line, "Back");
        }
        else if (currentMenuLevel == MENU_CALIBRATION && itemIdx < calibCount) {
            if (itemIdx == 0)
                sprintf(line, "TempOff: %+d", tempOffset);
            else if (itemIdx == 1)
                sprintf(line, "HumOff: %+d", humOffset);
            else if (itemIdx == 2)
                sprintf(line, "LightG: %d%%", lightGain);
            else if (itemIdx == calibCount - 2)
                sprintf(line, "Exit & Save");
            else if (itemIdx == calibCount - 1)
                sprintf(line, "Back");
        }
        else if (currentMenuLevel == MENU_SYSTEM && itemIdx < systemCount) {
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
                sprintf(line, "Back");
        }
        else {
            strcpy(line, "");
        }

        if (isSelected && needsScroll(line)) {
            drawScrollingText(line, i * 8, color, itemIdx);
        } else {
            dma_display->setTextColor(color);
            dma_display->setCursor(0, i * 8);
            dma_display->print(line);
        }
    }
}
void handleUp()
{
    lastMenuActivity = millis();
    scrollOffset = 0;

    if (currentMenuLevel == MENU_WIFI_SELECT) {
        if (wifiSelectIndex > 0) {
            wifiSelectIndex--;
            // If highlight moves above visible window, scroll window up
            if (wifiSelectIndex < wifiMenuScroll) {
                wifiMenuScroll = wifiSelectIndex;
            }
        }
        // Clamp scroll
        if (wifiMenuScroll < 0) wifiMenuScroll = 0;
        drawWiFiMenu();
        return;
    }

    currentMenuIndex--;
    if (currentMenuIndex < 0) currentMenuIndex = 0;
    if (currentMenuIndex < menuScroll) menuScroll = currentMenuIndex;
    drawMenu();
}


void handleDown()
{
    lastMenuActivity = millis();
    scrollOffset = 0;

    if (currentMenuLevel == MENU_WIFI_SELECT) {
        if (wifiSelectIndex < wifiScanCount - 1) {
            wifiSelectIndex++;
            // If highlight moves below visible window, scroll window down
            if (wifiSelectIndex >= wifiMenuScroll + wifiVisibleLines) {
                wifiMenuScroll = wifiSelectIndex - wifiVisibleLines + 1;
            }
        }
        // Clamp scroll to prevent blank window
        int maxScroll = max(0, wifiScanCount - wifiVisibleLines);
        if (wifiMenuScroll > maxScroll) wifiMenuScroll = maxScroll;
        drawWiFiMenu();
        return;
    }

    int count = (currentMenuLevel == MENU_MAIN) ? mainCount
        : (currentMenuLevel == MENU_DEVICE) ? deviceCount
        : (currentMenuLevel == MENU_DISPLAY) ? displayCount
        : (currentMenuLevel == MENU_WEATHER) ? weatherCount
        : (currentMenuLevel == MENU_CALIBRATION) ? calibCount
        : systemCount;

    currentMenuIndex++;
    if (currentMenuIndex >= count) currentMenuIndex = count - 1;
    if (currentMenuIndex >= menuScroll + visibleLines)
        menuScroll = currentMenuIndex - visibleLines + 1;
    drawMenu();
}

void handleLeft()
{
    lastMenuActivity = millis();

    if (currentMenuLevel == MENU_WIFI_SELECT) return; // no left/right in wifi select

    if (currentMenuLevel == MENU_DEVICE) {
        if (currentMenuIndex == 2) toggleUnits(-1);
        if (currentMenuIndex == 3) toggleDayFormat(-1);
        if (currentMenuIndex == 4) toggleForecastSrc(-1);
        if (currentMenuIndex == 5) toggleAutoRotate(-1);
    }
    else if (currentMenuLevel == MENU_DISPLAY) {
        if (currentMenuIndex == 0) toggleTheme(-1);
        if (currentMenuIndex == 1) adjustBrightness(-1);
        if (currentMenuIndex == 2) adjustScrollSpeed(-1);
    }
    else if (currentMenuLevel == MENU_CALIBRATION) {
        if (currentMenuIndex == 0) adjustTempOffset(-1);
        if (currentMenuIndex == 1) adjustHumOffset(-1);
        if (currentMenuIndex == 2) adjustLightGain(-1);
    }
    drawMenu();
}

void handleRight()
{
    lastMenuActivity = millis();

    if (currentMenuLevel == MENU_WIFI_SELECT) return; // no left/right in wifi select

    if (currentMenuLevel == MENU_DEVICE) {
        if (currentMenuIndex == 2) toggleUnits(1);
        if (currentMenuIndex == 3) toggleDayFormat(1);
        if (currentMenuIndex == 4) toggleForecastSrc(1);
        if (currentMenuIndex == 5) toggleAutoRotate(1);
    }
    else if (currentMenuLevel == MENU_DISPLAY) {
        if (currentMenuIndex == 0) toggleTheme(1);
        if (currentMenuIndex == 1) adjustBrightness(1);
        if (currentMenuIndex == 2) adjustScrollSpeed(1);
    }
    else if (currentMenuLevel == MENU_CALIBRATION) {
        if (currentMenuIndex == 0) adjustTempOffset(1);
        if (currentMenuIndex == 1) adjustHumOffset(1);
        if (currentMenuIndex == 2) adjustLightGain(1);
    }
    drawMenu();
}
void handleSelect()
{
    lastMenuActivity = millis();

    // Defensive: clamp index
    int count = (currentMenuLevel == MENU_MAIN) ? mainCount
        : (currentMenuLevel == MENU_DEVICE) ? deviceCount
        : (currentMenuLevel == MENU_DISPLAY) ? displayCount
        : (currentMenuLevel == MENU_WEATHER) ? weatherCount
        : (currentMenuLevel == MENU_CALIBRATION) ? calibCount
        : systemCount;
    if (currentMenuIndex < 0) currentMenuIndex = 0;
    if (currentMenuIndex >= count) currentMenuIndex = count - 1;

    if (editingField) {
        finishEditField();
        return;
    }

    if (currentMenuLevel == MENU_MAIN) {
        switch (currentMenuIndex) {
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
        if (currentMenuIndex == 0) {
            // Scan for WiFi every time you select this menu item
            scanWiFiNetworks();
            currentMenuLevel = MENU_WIFI_SELECT;
            wifiSelectIndex = 0;
            wifiMenuScroll = 0;
            drawWiFiMenu();
            return;
        }
        if (currentMenuIndex == 1) {
            startEditField(wifiPass.c_str());
            return;
        }
        if (currentMenuIndex == 2) {
            // ---- NEW: BLE SCAN ----
            scanBLENetworks();      // <--- Implement this
            currentMenuLevel = MENU_BLE_SELECT;
            bleSelectIndex = 0;
            bleMenuScroll = 0;
            drawBLEMenu();          // <--- Implement this (same style as WiFi)
            return;
        }
        if (currentMenuIndex == deviceCount - 2) {
            saveDeviceSettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == deviceCount - 1) {
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
    }
    else if (currentMenuLevel == MENU_DISPLAY) {
        if (currentMenuIndex == displayCount - 2) {
            saveDisplaySettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == displayCount - 1) {
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
    }
    else if (currentMenuLevel == MENU_WEATHER) {
        if (currentMenuIndex == 0) {
            startEditField(owmCity.c_str());
            return;
        }
        if (currentMenuIndex == 1) {
            startEditField(owmApiKey.c_str());
            return;
        }
        if (currentMenuIndex == 2) {
            startEditField(wfToken.c_str());
            return;
        }
        if (currentMenuIndex == 3) {
            startEditField(wfStationId.c_str());
            return;
        }
        if (currentMenuIndex == weatherCount - 2) {
            saveWeatherSettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == weatherCount - 1) {
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
    }
    else if (currentMenuLevel == MENU_CALIBRATION) {
        if (currentMenuIndex == calibCount - 2) {
            saveCalibrationSettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == calibCount - 1) {
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
    }
    else if (currentMenuLevel == MENU_SYSTEM) {
        if (currentMenuIndex == systemCount - 2) {
            saveSystemSettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == systemCount - 1) {
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == 0) startOTA();
        else if (currentMenuIndex == 1) resetPowerUsage();
        else if (currentMenuIndex == 2) quickRestore();
        else if (currentMenuIndex == 3) factoryReset();
    }

    // --- Draw correct menu depending on currentMenuLevel
    if (currentMenuLevel == MENU_WIFI_SELECT) {
        drawWiFiMenu();
    } else if (currentMenuLevel == MENU_BLE_SELECT) {
        drawBLEMenu();
    } else {
        drawMenu();
    }
}

// ------- Text Field Editing Logic --------
void startEditField(const char* currentValue) {
    editingField = true;
    strncpy(editBuffer, currentValue, sizeof(editBuffer)-1);
    editBuffer[sizeof(editBuffer)-1] = '\0';
    editCharIdx = strlen(editBuffer);
    drawEditField();
}

void finishEditField() {
    editingField = false;
    if (currentMenuLevel == MENU_DEVICE && currentMenuIndex == 0) wifiSSID = String(editBuffer);
    if (currentMenuLevel == MENU_DEVICE && currentMenuIndex == 1) {
        wifiPass = String(editBuffer);
        saveDeviceSettings(); // Save SSID/pass to Preferences!
        connectToWiFi();      // Optionally auto-connect here!
    }
    if (currentMenuLevel == MENU_WEATHER && currentMenuIndex == 0) owmCity = String(editBuffer);
    if (currentMenuLevel == MENU_WEATHER && currentMenuIndex == 1) owmApiKey = String(editBuffer);
    if (currentMenuLevel == MENU_WEATHER && currentMenuIndex == 2) wfToken = String(editBuffer);
    if (currentMenuLevel == MENU_WEATHER && currentMenuIndex == 3) wfStationId = String(editBuffer);

    drawMenu();
}

void drawEditField() {
    const int VISIBLE_LEN = 16; // or however many chars fit your display
    int bufLen = strlen(editBuffer);
    int startIdx = 0;

    if (editCharIdx >= VISIBLE_LEN)
        startIdx = editCharIdx - VISIBLE_LEN + 1;

    char shown[VISIBLE_LEN + 1];
    memset(shown, 0, sizeof(shown));
    strncpy(shown, editBuffer + startIdx, VISIBLE_LEN);

    if (millis()/500%2 && editCharIdx >= startIdx && editCharIdx < startIdx + VISIBLE_LEN)
        shown[editCharIdx - startIdx] = '_';

    dma_display->fillScreen(dma_display->color565(0, 0, 0));
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextColor(dma_display->color565(255,255,0));
    dma_display->setCursor(0, 0);
    dma_display->print("Edit:");
    dma_display->setCursor(0, 12);
    dma_display->print(shown);
}

void updateMenu() { drawMenu(); }
void drawWiFiMenu() {
    ensureWiFiListFresh();

    dma_display->fillScreen(dma_display->color565(0, 0, 0));
    dma_display->setFont(&Font5x7Uts);

    // Label
    dma_display->setTextColor(dma_display->color565(0,255,255));
    dma_display->setCursor(0, 0);
    dma_display->print("Select WiFi:");

    // Show 3 lines starting from wifiMenuScroll
    const int labelHeight = 7;
    const int listStartY = labelHeight + 1;
    const int wifiLineHeight = 8;
    for (int i = 0; i < wifiVisibleLines; ++i) {
        int idx = wifiMenuScroll + i;
        if (idx >= wifiScanCount) break;
        dma_display->setCursor(0, listStartY + wifiLineHeight * i);
        if (idx == wifiSelectIndex)
            dma_display->setTextColor(dma_display->color565(255,255,0));
        else
            dma_display->setTextColor(dma_display->color565(255,255,255));
        dma_display->print(scannedSSIDs[idx]);
    }
}


void ensureWiFiListFresh() {
    if (wifiScanCount == 1 && scannedSSIDs[0] == "(No networks)") {
        scanWiFiNetworks();
    }
}




