#include <Arduino.h>
#include "buzzer.h"
#include "menu.h"
#include "display.h"
#include <Preferences.h>
#include "settings.h"
#include "system.h"

MenuLevel currentMenuLevel = MENU_MAIN;
int currentMenuIndex = 0;
unsigned long lastMenuActivity = 0;
extern bool menuActive;
int menuScroll = 0;         // The index of the top-most visible item
const int visibleLines = 4; // Only 4 lines fit on your display

void handleIR(uint32_t code)
{
    // 1. If menu is NOT active, only allow the menu ON button to do anything
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

    // 2. If menu IS active, handle navigation/buttons as normal
    switch (code)
    {
    case 0xFFE21D: // Power/Menu button code - optionally let this exit menu
        // If you want this to close the menu, uncomment next two lines:
        // menuActive = false;
        // playBuzzerTone(3000, 100);
        // Optionally: displayClock(); displayDate(); displayWeatherData();
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

const char *mainMenu[] = {"Device Settings", "Display Settings", "Weather Settings", "Calibration", "System Actions", "Save & Exit"};
const int mainCount = sizeof(mainMenu) / sizeof(mainMenu[0]);

const char *deviceMenu[] = {"Units", "Day Format", "Forecast Src", "Auto Rotate", "Manual Screen", "Exit & Save"};
const int deviceCount = sizeof(deviceMenu) / sizeof(deviceMenu[0]);

const char *displayMenu[] = {"Theme", "Brightness", "Scroll Spd", "Custom Msg", "Exit & Save"};
const int displayCount = sizeof(displayMenu) / sizeof(displayMenu[0]);

const char *weatherMenu[] = {"OWM City", "OWM API Key", "WF Token", "WF Station ID", "Exit & Save"};
const int weatherCount = sizeof(weatherMenu) / sizeof(weatherMenu[0]);

const char *calibMenu[] = {"Temp Offset", "Hum Offset", "Light Gain", "Exit & Save"};
const int calibCount = sizeof(calibMenu) / sizeof(calibMenu[0]);

const char *systemMenu[] = {"OTA Update", "Reset Power", "Quick Restore", "Factory Reset", "Exit & Save"};
const int systemCount = sizeof(systemMenu) / sizeof(systemMenu[0]);

void drawMenu()
{
    dma_display->fillScreen(dma_display->color565(0, 0, 0));
    dma_display->setCursor(0, 0);
    dma_display->setFont(&Font5x7Uts);

    int count = (currentMenuLevel == MENU_MAIN) ? mainCount : (currentMenuLevel == MENU_DEVICE)    ? deviceCount
                                                          : (currentMenuLevel == MENU_DISPLAY)     ? displayCount
                                                          : (currentMenuLevel == MENU_WEATHER)     ? weatherCount
                                                          : (currentMenuLevel == MENU_CALIBRATION) ? calibCount
                                                                                                   : systemCount;

    for (int i = 0; i < visibleLines; i++)
    {
        int itemIdx = menuScroll + i;
        if (itemIdx >= count)
            break;

        dma_display->setTextColor(itemIdx == currentMenuIndex ? dma_display->color565(255, 0, 0) : dma_display->color565(255, 255, 255));
        dma_display->setCursor(0, i * 8);

        char line[32];
        if (currentMenuLevel == MENU_MAIN && itemIdx < mainCount)
            sprintf(line, "%s", mainMenu[itemIdx]);
        else if (currentMenuLevel == MENU_DEVICE && itemIdx < deviceCount)
        {
            if (itemIdx == 0)
                sprintf(line, "Units: %s", (units == 0) ? "F+mph" : "C+m/s");
            else if (itemIdx == 1)
                sprintf(line, "DayFmt: %s", (dayFormat == 0) ? "MM/DD" : "DD/MM");
            else if (itemIdx == 2)
                sprintf(line, "Forecast: %s", (forecastSrc == 0) ? "OWM" : "WF");
            else if (itemIdx == 3)
                sprintf(line, "AutoRot: %s", (autoRotate == 0) ? "Off" : "On");
            else if (itemIdx == 4)
                sprintf(line, "ManualScr");
            else if (itemIdx == 5)
                sprintf(line, "Exit & Save");
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
            else if (itemIdx == 4)
                sprintf(line, "Exit & Save");
        }
        else if (currentMenuLevel == MENU_WEATHER && itemIdx < weatherCount)
        {
            sprintf(line, "%s", weatherMenu[itemIdx]);
        }
        else if (currentMenuLevel == MENU_CALIBRATION && itemIdx < calibCount)
        {
            if (itemIdx == 0)
                sprintf(line, "TempOff: %+d", tempOffset);
            else if (itemIdx == 1)
                sprintf(line, "HumOff: %+d", humOffset);
            else if (itemIdx == 2)
                sprintf(line, "LightG: %d%%", lightGain);
            else if (itemIdx == 3)
                sprintf(line, "Exit & Save");
        }
        else if (currentMenuLevel == MENU_SYSTEM && itemIdx < systemCount)
        {
            sprintf(line, "%s", systemMenu[itemIdx]);
        }
        else
            strcpy(line, "");

        dma_display->print(line);
    }
}

void handleUp()
{
    lastMenuActivity = millis();
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
    int count = (currentMenuLevel == MENU_MAIN) ? mainCount : (currentMenuLevel == MENU_DEVICE)    ? deviceCount
                                                          : (currentMenuLevel == MENU_DISPLAY)     ? displayCount
                                                          : (currentMenuLevel == MENU_WEATHER)     ? weatherCount
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
        if (currentMenuIndex == 0)
            toggleUnits(-1);
        if (currentMenuIndex == 1)
            toggleDayFormat(-1);
        if (currentMenuIndex == 2)
            toggleForecastSrc(-1);
        if (currentMenuIndex == 3)
            toggleAutoRotate(-1);
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
        if (currentMenuIndex == 0)
            toggleUnits(1);
        if (currentMenuIndex == 1)
            toggleDayFormat(1);
        if (currentMenuIndex == 2)
            toggleForecastSrc(1);
        if (currentMenuIndex == 3)
            toggleAutoRotate(1);
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
            fetchWeatherFromOWM();
            displayClock();
            displayDate();
            displayWeatherData();
            reset_Time_and_Date_Display = false;
            break;
        }
    }
    else if (currentMenuLevel == MENU_DEVICE && currentMenuIndex == deviceCount - 1)
    {
        saveDeviceSettings();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    }
    else if (currentMenuLevel == MENU_DISPLAY && currentMenuIndex == displayCount - 1)
    {
        saveDisplaySettings();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    }
    else if (currentMenuLevel == MENU_WEATHER && currentMenuIndex == weatherCount - 1)
    {
        saveWeatherSettings();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    }
    else if (currentMenuLevel == MENU_CALIBRATION && currentMenuIndex == calibCount - 1)
    {
        saveCalibrationSettings();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    }
    else if (currentMenuLevel == MENU_SYSTEM)
    {
        if (currentMenuIndex == systemCount - 1)
        {
            saveSystemSettings();
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
    drawMenu();
}

void updateMenu() { drawMenu(); }
