#include <Arduino.h>
#include "buzzer.h"
#include "menu.h"
#include "display.h"
#include <Preferences.h>
#include "settings.h"
#include "system.h"
#include "utils.h"

MenuLevel currentMenuLevel = MENU_MAIN;
int currentMenuIndex = 0;
unsigned long lastMenuActivity = 0;
extern bool menuActive;
int menuScroll = 0;         // The index of the top-most visible item
const int visibleLines = 4; // Only 4 lines fit on your display
extern int scrollOffset; // For scrolling text

// For field editing:
bool editingField = false;
char editBuffer[32] = "";
int editCharIdx = 0;

// Declare these somewhere if not global:
extern String wifiSSID;
extern String wifiPass;
extern String owmCity;
extern String owmApiKey;
extern String wfToken;
extern String wfStationId;
extern int humOffset; // Humidity offset for calibration

const char *mainMenu[] = {"Device Settings", "Display Settings", "Weather Settings", "Calibration", "System Actions", "Save & Exit"};
const int mainCount = sizeof(mainMenu) / sizeof(mainMenu[0]);

// Device menu fields: WiFi (editable), then toggles/settings, then Exit/Back
const char* deviceMenu[] = {
    "WiFi SSID", "WiFi Pass", "Units", "Day Format", "Forecast Src", "Auto Rotate", "Manual Screen", "Exit & Save", "Back"
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


void handleIR(uint32_t code)
{
    // ---- 1. Edit Mode: Text Field Editing with IR Remote ----
    if (editingField) {
        // Define your IR codes:
        const uint32_t IR_UP    = 0xFF02FD; // CH+
        const uint32_t IR_DOWN  = 0xFF9867; // CH-
        const uint32_t IR_LEFT  = 0xFFE01F; // <<
        const uint32_t IR_RIGHT = 0xFF906F; // >>
        const uint32_t IR_OK    = 0xFFA857; // OK
        const uint32_t IR_CANCEL = 0xFFE21D; // Power/Menu as Cancel

        const char* charSet = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()-_=+[]{};:,.<>?/|";
        int charSetLen = strlen(charSet);

        char current = editBuffer[editCharIdx];
        int idx = strchr(charSet, current) ? (strchr(charSet, current) - charSet) : 0;

        switch (code) {
            case IR_UP:   // Next char
                idx = (idx + 1) % charSetLen;
                editBuffer[editCharIdx] = charSet[idx];
                break;
            case IR_DOWN: // Prev char
                idx = (idx - 1 + charSetLen) % charSetLen;
                editBuffer[editCharIdx] = charSet[idx];
                break;
            case IR_RIGHT: // Next char position
                if (editCharIdx < (int)sizeof(editBuffer) - 2) {
                    editCharIdx++;
                    // If it's a new position, initialize to ' '
                    if (editBuffer[editCharIdx] == 0) editBuffer[editCharIdx] = ' ';
                }
                break;
            case IR_LEFT: // Prev char position
                if (editCharIdx > 0) editCharIdx--;
                break;
            case IR_OK: // Save (finish edit)
                finishEditField();
                return;
            case IR_CANCEL: // Cancel edit
                editingField = false;
                drawMenu();
                return;
            default:
                playBuzzerTone(500, 100);
                break;
        }
        drawEditField();
        return;
    }

    // ---- 2. Menu NOT active: Only allow power/menu to turn ON ----
    if (!menuActive)
    {
        if (code == 0xFFE21D) // Power/Menu button code
        {
            menuActive = true;
            drawMenu();
            playBuzzerTone(3000, 100);
        }
        // All other buttons ignored when menu is not active
        return;
    }

    // ---- 3. Menu navigation mode ----
    switch (code)
    {
    case 0xFFE21D: // Power/Menu button code - exit menu (optional)
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
    case 0xFF02FD:
        handleUp();
        playBuzzerTone(1500, 100);
        break; // CH+
    case 0xFF9867:
        handleDown();
        playBuzzerTone(1200, 100);
        break; // CH-
    case 0xFF906F:
        handleRight();
        playBuzzerTone(1800, 100);
        break; // >>
    case 0xFFE01F:
        handleLeft();
        playBuzzerTone(900, 100);
        break; // <<
    case 0xFFA857:
        handleSelect();
        playBuzzerTone(2200, 100);
        break; // OK
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

    // 2. Menu UI
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

        char line[40];
        line[0] = 0;

        // MAIN MENU
        if (currentMenuLevel == MENU_MAIN && itemIdx < mainCount) {
            sprintf(line, "%s", mainMenu[itemIdx]);
        }
        // DEVICE MENU (with new mapping)
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
        // DISPLAY MENU
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
        // WEATHER MENU
        else if (currentMenuLevel == MENU_WEATHER && itemIdx < weatherCount) {
            if (itemIdx == 0)
                sprintf(line, "OWM City: %s", owmCity.c_str());
            else if (itemIdx == 1)
                sprintf(line, "OWM Key: %s", owmApiKey.c_str());
            else if (itemIdx == 2)
                sprintf(line, "WF Token: %s", wfToken.c_str());
            else if (itemIdx == 3)
                sprintf(line, "WF ID: %s", wfStationId.c_str());
            else if (itemIdx == weatherCount - 2)
                sprintf(line, "Exit & Save");
            else if (itemIdx == weatherCount - 1)
                sprintf(line, "Back");
        }
        // CALIBRATION MENU
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
        // SYSTEM MENU
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

        // -- Print or scroll the selected line
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
    scrollOffset = 0; // Reset scroll offset when navigating
    currentMenuIndex--;
    if (currentMenuIndex < 0)
        currentMenuIndex = 0;

    // Scroll up if necessary
    if (currentMenuIndex < menuScroll)
        menuScroll = currentMenuIndex;

    drawMenu();
}

void handleDown()
{
    lastMenuActivity = millis();
    scrollOffset = 0;  // Reset scroll for new selection!

    int count = (currentMenuLevel == MENU_MAIN) ? mainCount
              : (currentMenuLevel == MENU_DEVICE) ? deviceCount
              : (currentMenuLevel == MENU_DISPLAY) ? displayCount
              : (currentMenuLevel == MENU_WEATHER) ? weatherCount
              : (currentMenuLevel == MENU_CALIBRATION) ? calibCount
              : systemCount;

    currentMenuIndex++;
    if (currentMenuIndex >= count)
        currentMenuIndex = count - 1;

    // Scroll down if necessary
    if (currentMenuIndex >= menuScroll + visibleLines)
        menuScroll = currentMenuIndex - visibleLines + 1;

    drawMenu();
}

void handleLeft()
{
    lastMenuActivity = millis();
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
        // ManualScr (index 6) add logic if needed
    }
    else if (currentMenuLevel == MENU_DISPLAY)
    {
        if (currentMenuIndex == 0)
            toggleTheme(-1);
        if (currentMenuIndex == 1)
            adjustBrightness(-1);
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
        // ManualScr (index 6) add logic if needed
    }
    else if (currentMenuLevel == MENU_DISPLAY)
    {
        if (currentMenuIndex == 0)
            toggleTheme(1);
        if (currentMenuIndex == 1)
            adjustBrightness(1);
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

    // --- Handle confirmation if currently editing a field ---
    if (editingField) {
        finishEditField();
        return;
    }

    // --- MAIN MENU ---
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
        case 5: // Save & Exit
            saveAllSettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
            menuActive = false;
            dma_display->clearScreen();
            delay(50); // Give time for the display to clear
            fetchWeatherFromOWM();
            displayClock();
            displayDate();
            displayWeatherData();
            reset_Time_and_Date_Display = true;  
            break;
        }
    }
    // --- DEVICE MENU ---
    else if (currentMenuLevel == MENU_DEVICE)
    {
        if (currentMenuIndex == 0) {
            startEditField(wifiSSID.c_str());
            return;
        }
        if (currentMenuIndex == 1) {
            startEditField(wifiPass.c_str());
            return;
        }
        if (currentMenuIndex == deviceCount - 2) { // Exit & Save
            saveDeviceSettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == deviceCount - 1) { // Back
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        // Other device toggles are handled by left/right, not select!
    }
    // --- DISPLAY MENU ---
    else if (currentMenuLevel == MENU_DISPLAY)
    {
        if (currentMenuIndex == displayCount - 2) { // Exit & Save
            saveDisplaySettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == displayCount - 1) { // Back
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        // Optionally add edit logic for customMsg
    }
    // --- WEATHER MENU ---
    else if (currentMenuLevel == MENU_WEATHER)
    {
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
        if (currentMenuIndex == weatherCount - 2) { // Exit & Save
            saveWeatherSettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == weatherCount - 1) { // Back
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
    }
    // --- CALIBRATION MENU ---
    else if (currentMenuLevel == MENU_CALIBRATION)
    {
        if (currentMenuIndex == calibCount - 2) { // Exit & Save
            saveCalibrationSettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == calibCount - 1) { // Back
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
    }
    // --- SYSTEM MENU ---
    else if (currentMenuLevel == MENU_SYSTEM)
    {
        if (currentMenuIndex == systemCount - 2) { // Exit & Save
            saveSystemSettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == systemCount - 1) { // Back
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        else if (currentMenuIndex == 0) startOTA();
        else if (currentMenuIndex == 1) resetPowerUsage();
        else if (currentMenuIndex == 2) quickRestore();
        else if (currentMenuIndex == 3) factoryReset();
    }

    drawMenu();
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
    // Save the buffer into the correct setting based on context
    if (currentMenuLevel == MENU_DEVICE && currentMenuIndex == 0) wifiSSID = String(editBuffer);
    if (currentMenuLevel == MENU_DEVICE && currentMenuIndex == 1) wifiPass = String(editBuffer);
    if (currentMenuLevel == MENU_WEATHER && currentMenuIndex == 0) owmCity = String(editBuffer);
    if (currentMenuLevel == MENU_WEATHER && currentMenuIndex == 1) owmApiKey = String(editBuffer);
    if (currentMenuLevel == MENU_WEATHER && currentMenuIndex == 2) wfToken = String(editBuffer);
    if (currentMenuLevel == MENU_WEATHER && currentMenuIndex == 3) wfStationId = String(editBuffer);

    drawMenu();
}

void drawEditField() {
    dma_display->fillScreen(dma_display->color565(0, 0, 0));
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextColor(dma_display->color565(255,255,0));
    dma_display->setCursor(0, 0);
    dma_display->print("Edit:");

    // Print buffer with blinking cursor
    char shown[33]; strncpy(shown, editBuffer, sizeof(shown));
    if (millis()/500%2 && editCharIdx < (int)sizeof(shown)-1) shown[editCharIdx] = '_';
    shown[sizeof(shown)-1]=0;
    dma_display->setCursor(0, 12);
    dma_display->print(shown);
}

void updateMenu() { drawMenu(); }
