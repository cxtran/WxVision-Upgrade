#include <Arduino.h>
#include "buzzer.h"
#include "menu.h"
#include "display.h"
#include <Preferences.h>
#include "settings.h"
#include "system.h"
#include "utils.h"
#include "WiFi.h"
#include "keyboard.h" // <-- Required for keyboard mode
#include "InfoModal.h"
#include "datetimesettings.h"
#include "ir_codes.h"

// --- System Info Colors ---
#define SYSINFO_HEADER dma_display->color565(0, 255, 80)
#define SYSINFO_HEADERBG dma_display->color565(0, 20, 60)
#define SYSINFO_UNSELXBG dma_display->color565(40, 40, 180)
#define SYSINFO_SELXBG dma_display->color565(255, 0, 0)
#define SYSINFO_XCOLOR dma_display->color565(255, 255, 255)
#define SYSINFO_ULINE dma_display->color565(180, 180, 255)
#define SYSINFO_SEL dma_display->color565(255, 255, 64)
#define SYSINFO_UNSEL dma_display->color565(0, 255, 255)

extern ScreenMode currentScreen;
extern const int SCREEN_COUNT;
void handleScreenSwitch(int dir);

InfoModal sysInfoModal("Sys Info");
InfoModal wifiInfoModal("WiFi Info");
InfoModal dateModal("Set Date/Time");

// Add these at top of file (not inside function)
bool atScrollEnd = false;
unsigned long scrollPauseStart = 0;

// Number of display columns and rows for info screen
const int SYSINFO_MAXCOLS = 12;
const int SYSINFO_CHARH = 8;
const int SYSINFO_MAXROWS = 4;
const int SYSINFO_INFOROWS = SYSINFO_MAXROWS - 1;
const int SYSINFO_SCROLLSPEED = 50;
const int SYSINFO_ENDPAUSE = 300;
const int SYSINFO_SCREEN_WIDTH = 64; // Your display width in pixels
static int sysInfoScrollOffset = 0;  // Pixel offset for scrolling
static int lastSysInfoSelIndex = -1;
static unsigned long sysInfoScrollTime = 0;
static bool sysInfoFirstScroll = true;

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

const char *mainMenu[] = {"Device Settings", "Display Settings", "Weather Settings", "Calibration", "System", "Exit Menu"};
const int mainCount = sizeof(mainMenu) / sizeof(mainMenu[0]);

const char *deviceMenu[] = {
    "WiFi SSID", "WiFi Pass", "Units", "Day Format", "Forecast Src", "Auto Rotate", "Manual Screen", "< Back"};
const int deviceCount = sizeof(deviceMenu) / sizeof(deviceMenu[0]);

const char *displayMenu[] = {"Theme", "Brightness", "Scroll Spd", "Custom Msg", "< Back"};
const int displayCount = sizeof(displayMenu) / sizeof(displayMenu[0]);

const char *weatherMenu[] = {"OWM City", "Country", "OWM API Key", "WF Token", "WF Station ID", "< Back"};
const int weatherCount = sizeof(weatherMenu) / sizeof(weatherMenu[0]);

const char *calibMenu[] = {"Temp Offset", "Hum Offset", "Light Gain", "< Back"};
const int calibCount = sizeof(calibMenu) / sizeof(calibMenu[0]);

const char *systemMenu[] = {"Show System Info", "Set Date & Time", "WiFi Signal Test", "Quick Restore", "Reset Power", "Factory Reset", "Reboot", "< Back"};

const int systemCount = sizeof(systemMenu) / sizeof(systemMenu[0]);

CountryEntry countries[] = {
    {"", "US"},
    {"", "GB"},
    {"", "VN"},
    {"", "CA"},
    {"", "DE"},
    {"", "FR"},
    {"", "AU"},
    {"", "IN"},
    // ... add more as you like ...
    {"Custom...", ""} // Last item triggers keyboard
};
const int numCountries = sizeof(countries) / sizeof(countries[0]);
int countryIndex = 0;          // Should be persisted with your settings!
String customCountryCode = ""; // Stores user's custom entry

// Define actual settings as global/static variables

int dtYear, dtMonth, dtDay, dtHour, dtMinute, dtSecond;
int dtTimezone; // in minutes, e.g., -480 for UTC-8
int dtFmt24;
int dtDateFmt;

const char *const fmt24Opts[] = {"12h", "24h"};
const char *const dateFmtOpts[] = {"YYYY-MM-DD", "MM/DD/YYYY", "DD/MM/YYYY"};

void scanWiFiNetworks()
{
    wifiScanCount = 0;
    wifiSelecting = true;

    WiFi.mode(WIFI_STA); // Set WiFi to Station mode (required for scanning)
    delay(100);

    int n = WiFi.scanNetworks();
    int j = 0;
    for (int i = 0; i < n && j < 15; ++i)
    { // Leave room for "Back"
        String ssid = WiFi.SSID(i);
        ssid.trim();
        if (ssid.length() == 0)
            continue;
        scannedSSIDs[j++] = ssid;
    }
    // Always add "< Back" as last menu entry
    scannedSSIDs[j++] = "< Back";
    wifiScanCount = j;
    wifiSelectIndex = 0;
    wifiMenuScroll = 0;
    wifiSelectNeedsScan = false;
    Serial.printf("[scanWiFiNetworks] Found %d networks (+Back)\n", wifiScanCount - 1);
}

void handleIR(uint32_t code)
{
    Serial.printf("IR Code: 0x%X\n", code);

    // --- Handle Screen On/Off IR code ---
    //   const uint32_t IR_SCREEN = 0xFFFFF00F; // Power/Menu (toggle screen on/off)
    if (code == IR_SCREEN)
    {
        if (isScreenOff())
        {
            setScreenOff(false); // Turn ON (restore brightness)
        }
        else
        {
            setScreenOff(true); // Turn OFF (set brightness to 0)
        }
        return; // Do not process further menu actions for this code
    }

    if (isScreenOff())
    {
        return; // Ignore all IR codes when screen is off
    }
    // --- Keyboard mode always has top priority ---
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

    // --- Handle WiFi Info Modal ---
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

    // -------- 1. WiFi Select Mode --------
    if (currentMenuLevel == MENU_WIFI_SELECT)
    {

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
            startKeyboardEntry(wifiPass.c_str(), [](const char *result)
                               {
                if(result) {
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
            currentMenuLevel = MENU_DEVICE;
            drawMenu();
            playBuzzerTone(700, 80);
        }
        return; // No further menu processing
    }

    // -------- 3. Menu NOT Active --------
    // -------- 3. Menu NOT Active --------
    unsigned long lastMenuToggle = 0;

    // In handleIR()
    if (!menuActive)
    {
        if (code == IR_LEFT)
        {
            handleScreenSwitch(-1);
            return;
        }
        else if (code == IR_RIGHT)
        {
            handleScreenSwitch(+1);
            return;
        }
        else if (code == IR_CANCEL)
        {
            if (millis() - lastMenuToggle < 500)
                return; // Prevent toggle flood
            menuActive = true;
            drawMenu();
            playBuzzerTone(3000, 100);
            lastMenuToggle = millis();
            return;
        }
        return;
    }

    // -------- 4. Normal Menu Navigation --------
    switch (code)
    {
    case IR_CANCEL:
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
        menuActive = false;
        dma_display->clearScreen();
        delay(50);
        playBuzzerTone(3000, 100);
        fetchWeatherFromOWM();
        displayClock();
        displayDate();
        displayWeatherData();
        reset_Time_and_Date_Display = true;
        lastMenuToggle = millis(); // << Add this!
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

void drawMenu()
{
    // If keyboard mode, let keyboard handle drawing!
    if (inKeyboardMode)
    {
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
            else if (itemIdx == deviceCount - 1)
                sprintf(line, "< Back");
        }
        else if (currentMenuLevel == MENU_DISPLAY && itemIdx < displayCount)
        {
            if (itemIdx == 0)
                sprintf(line, "Theme: %s", (theme == 0) ? "Color" : "Mono");
            else if (itemIdx == 1)
                if (autoBrightness)
                    sprintf(line, "Bright: auto");
                else
                    sprintf(line, "Bright: %d%%", brightness);

            else if (itemIdx == 2)
                sprintf(line, "Scroll: %d", scrollSpeed);
            else if (itemIdx == 3)
                sprintf(line, "Custom Msg");
            else if (itemIdx == displayCount - 1)
                sprintf(line, "< Back");
        }
        else if (currentMenuLevel == MENU_WEATHER && itemIdx < weatherCount)
        {
            if (itemIdx == 0)
                sprintf(line, "OWM City: %s", owmCity.c_str());
            else if (itemIdx == 1)
            {
                if (owmCountryIndex < numCountries - 1)
                {
                    sprintf(line, "Country: %s (%s)", countries[owmCountryIndex].name, countries[owmCountryIndex].code);
                }
                else
                {
                    sprintf(line, "Country: Custom (%s)",
                            owmCountryCustom.length() ? owmCountryCustom.c_str() : "--");
                }
            }
            else if (itemIdx == 2)
                snprintf(line, sizeof(line), "OWM Key: %.30s", owmApiKey.c_str());
            else if (itemIdx == 3)
                sprintf(line, "WF Token: %s", wfToken.c_str());
            else if (itemIdx == 4)
                sprintf(line, "WF ID: %s", wfStationId.c_str());
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
            else if (itemIdx == calibCount - 1)
                sprintf(line, "< Back");
        }
        else if (currentMenuLevel == MENU_SYSTEM && itemIdx < systemCount)
        {
            if (itemIdx == 0)
                sprintf(line, "System Info");
            else if (itemIdx == 1)
                sprintf(line, "Set Date & Time");
            else if (itemIdx == 2)
                sprintf(line, "WiFi Signal Test");
            else if (itemIdx == 3)
                sprintf(line, "Quick Restore");
            else if (itemIdx == 4)
                sprintf(line, "Reset Power");
            else if (itemIdx == 5)
                sprintf(line, "Factory Reset");
            else if (itemIdx == 6)
                sprintf(line, "Reboot");
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
            // If auto mode, turn OFF auto and allow manual brightness
            if (autoBrightness)
            {
                autoBrightness = false;
                playBuzzerTone(1200, 80);
            }
            else
            {
                // Manual decrease brightness, clamp 3-100
                brightness--;
                if (brightness < 3)
                    brightness = 3;
                int hardwareBrightness = map(brightness, 1, 100, 3, 255);
                dma_display->setBrightness8(hardwareBrightness);
                playBuzzerTone(900, 80);
            }
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
    else if (currentMenuLevel == MENU_WEATHER)
    {
        if (currentMenuIndex == 1)
        { // OWM Country Code
            if (owmCountryIndex > 0)
                owmCountryIndex--;
            else
                owmCountryIndex = numCountries - 1;
            drawMenu();
            return;
        }
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

        else if (currentMenuIndex == 1)
        {
            // -- NEW: If in auto, toggle to manual (and vice versa elsewhere)
            if (autoBrightness)
            {
                autoBrightness = false;
                playBuzzerTone(1900, 80);
            }
            else
            {
                // manual: increment brightness %
                brightness++;
                if (brightness > 100)
                    brightness = 100;
                if (brightness < 1)
                    brightness = 1;
                int hardwareBrightness = map(brightness, 1, 100, 3, 255);
                dma_display->setBrightness8(hardwareBrightness);
                playBuzzerTone(2200, 60);
            }
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
    else if (currentMenuLevel == MENU_WEATHER)
    {
        if (currentMenuIndex == 1)
        { // OWM Country Code
            if (owmCountryIndex < numCountries - 1)
                owmCountryIndex++;
            else
                owmCountryIndex = 0;
            drawMenu();
            return;
        }
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
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
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
        }
    }
    else if (currentMenuLevel == MENU_DEVICE)
    {
        if (currentMenuIndex == 0)
        {
            // Always scan for WiFi every time you enter this menu!
            scanWiFiNetworks();
            currentMenuLevel = MENU_WIFI_SELECT;
            wifiSelectIndex = 0;
            wifiMenuScroll = 0;
            drawWiFiMenu();
            return;
        }
        if (currentMenuIndex == 1)
        {
            // ---- Use keyboard for password entry ----
            startKeyboardEntry(wifiPass.c_str(), [](const char *result)
                               {
                if(result) {
                    wifiPass = String(result);
                    saveDeviceSettings();
                    connectToWiFi();
                }
                drawMenu(); });
            return;
        }
        if (currentMenuIndex == deviceCount - 1)
        {
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
    }
    else if (currentMenuLevel == MENU_DISPLAY)
    {
        // --- Toggle Auto/Manual Brightness with Select ---
        if (currentMenuIndex == 1) // Brightness row
        {
            autoBrightness = !autoBrightness;
            playBuzzerTone(2000, 100);
            drawMenu();
            return;
        }
        // If "< Back"
        if (currentMenuIndex == displayCount - 1)
        {
            saveDisplaySettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
        // Optionally add keyboard entry for custom message:
        else if (currentMenuIndex == 3)
        {
            // Keyboard entry for Custom Msg
            startKeyboardEntry(customMsg.c_str(), [](const char *result)
                               {
                if(result) customMsg = String(result);
                drawMenu(); });
            return;
        }
        // "Theme", "Brightness", "Scroll Spd", "Custom Msg", "< Back"
    }
    else if (currentMenuLevel == MENU_WEATHER)
    {
        if (currentMenuIndex == 0)
        {
            startKeyboardEntry(owmCity.c_str(), [](const char *result)
                               {
            if(result) owmCity = String(result);
            drawMenu(); });
            return;
        }
        if (currentMenuIndex == 1)
        {
            if (countryIndex == numCountries - 1)
            {
                // Custom...
                startKeyboardEntry(owmCountryCustom.c_str(), [](const char *result)
                                   {
                if(result) owmCountryCustom = String(result);
                drawMenu(); });
            }
            // Otherwise, selecting does nothing (left/right changes)
            return;
        }
        if (currentMenuIndex == 2)
        {
            startKeyboardEntry(owmApiKey.c_str(), [](const char *result)
                               {
            if(result) owmApiKey = String(result);
            drawMenu(); });
            return;
        }
        if (currentMenuIndex == 3)
        {
            startKeyboardEntry(wfToken.c_str(), [](const char *result)
                               {
            if(result) wfToken = String(result);
            drawMenu(); });
            return;
        }
        if (currentMenuIndex == 4)
        {
            startKeyboardEntry(wfStationId.c_str(), [](const char *result)
                               {
            if(result) wfStationId = String(result);
            drawMenu(); });
            return;
        }
        if (currentMenuIndex == weatherCount - 1)
        {
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
    }
    else if (currentMenuLevel == MENU_CALIBRATION)
    {
        if (currentMenuIndex == calibCount - 1)
        {
            saveCalibrationSettings();
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
    }
    else if (currentMenuLevel == MENU_SYSTEM)
    {
        if (currentMenuIndex == 0)
            showSystemInfoScreen();
        else if (currentMenuIndex == 1)
            showDateTimeModal();
        else if (currentMenuIndex == 2)
            showWiFiSignalTest();
        else if (currentMenuIndex == 3)
        {
            quickRestore();
        }
        else if (currentMenuIndex == 4)
        {
            resetPowerUsage();
            return;
        }
        else if (currentMenuIndex == 5)
        {
            factoryReset();
            return;
        }
        else if (currentMenuIndex == 6)
        {
            dma_display->fillScreen(0);
            dma_display->setCursor(0, 0);
            dma_display->setTextColor(dma_display->color565(255, 255, 0));
            dma_display->print("Rebooting...");
            delay(300);
            ESP.restart();
            return;
        }
        else if (currentMenuIndex == systemCount - 1)
        {
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
        }
    }

    if (!menuActive)
        return; // Do NOT redraw menu if you just exited!

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

void showWiFiSignalTest()
{
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

    // Add networks (skip <Back> entry)
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
    String lines[] = {
        "FW: 1.0.0",
        "IP: " + WiFi.localIP().toString(),
        "MAC: " + WiFi.macAddress(),
        "RSSI: " + String(WiFi.RSSI()) + " dBm",
        "Heap: " + String(ESP.getFreeHeap()) + " B"};
    InfoFieldType types[] = {
        InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel};
    sysInfoModal.setLines(lines, types, 5);
    sysInfoModal.show();
}

void showDateTimeModal()
{
    // Read RTC and current config into working vars:
    DateTime now = rtc.now();
    dtYear = now.year();
    dtMonth = now.month();
    dtDay = now.day();
    dtHour = now.hour();
    dtMinute = now.minute();
    dtSecond = now.second();
    dtTimezone = tzOffset; // from settings

    // Clamp chooser values BEFORE modal to ensure valid mapping!
    dtFmt24 = (fmt24 < 0 || fmt24 > 1) ? 1 : fmt24;
    dtDateFmt = (dateFmt < 0 || dateFmt > 2) ? 0 : dateFmt;

    // --- Modal fields: NO BUTTONS HERE ---
    String lines[] = {
        "Year", "Month", "Day", "Hour", "Minute", "Second",
        "TimeZone", "TimeFmt", "DateFmt"};
    InfoFieldType types[] = {
        InfoNumber, InfoNumber, InfoNumber, InfoNumber, InfoNumber, InfoNumber,
        InfoNumber, InfoChooser, InfoChooser};

    int *intRefs[] = {
        &dtYear, &dtMonth, &dtDay, &dtHour, &dtMinute, &dtSecond,
        &dtTimezone};
    int *chooserRefs[] = {&dtFmt24, &dtDateFmt};
    static const char *fmt24Opts[] = {"12h", "24h"};
    static const char *dateFmtOpts[] = {"YYYY-MM-DD", "MM/DD/YYYY", "DD/MM/YYYY"};
    static const char *const *chooserOptPtrs[] = {fmt24Opts, dateFmtOpts};
    int chooserOptCounts[] = {2, 3};

    dateModal.setLines(lines, types, 9);
    dateModal.setValueRefs(
        intRefs, 7,
        chooserRefs, 2,
        chooserOptPtrs, chooserOptCounts);

    // --- Horizontal icon button bar ---
    const String btns[] = {"OK", "x", "NTP"}; // Save, Cancel, SyncNTP
    dateModal.setButtons(btns, 3);

    dateModal.setCallback([](bool accepted, int btnIdx)
                          {
        Serial.printf("Calback: Accept %d %d", accepted, btnIdx);
        // btnIdx: 0=Save(✓), 1=Cancel(✗), 2=SyncNTP(N)
        if (btnIdx == 2) // Sync NTP
        {
            syncTimeFromNTP();
            // Reload modal working vars with the newly synced RTC time
            DateTime now = rtc.now();
            dtYear = now.year();
            dtMonth = now.month();
            dtDay = now.day();
            dtHour = now.hour();
            dtMinute = now.minute();
            dtSecond = now.second();
            showDateTimeModal(); // Re-show modal with new values
            return;
        }
        if (btnIdx == 0 && accepted) // Save (✓)
        {
            // Clamp and validate values before saving
            if (dtMonth < 1) dtMonth = 1;
            if (dtMonth > 12) dtMonth = 12;
            if (dtDay < 1) dtDay = 1;
            if (dtDay > 31) dtDay = 31;
            if (dtHour < 0) dtHour = 0;
            if (dtHour > 23) dtHour = 23;
            if (dtMinute < 0) dtMinute = 0;
            if (dtMinute > 59) dtMinute = 59;
            if (dtSecond < 0) dtSecond = 0;
            if (dtSecond > 59) dtSecond = 59;
            if (dtYear < 2000) dtYear = 2000;
            if (dtYear > 2099) dtYear = 2099;
            if (dtTimezone < -720) dtTimezone = -720;
            if (dtTimezone > 840) dtTimezone = 840;

            rtc.adjust(DateTime(dtYear, dtMonth, dtDay, dtHour, dtMinute, dtSecond));
            tzOffset = dtTimezone;
            fmt24 = dtFmt24;
            dateFmt = dtDateFmt;
            saveDateTimeSettings();
        }
        // btnIdx==1 or !accepted => Cancel: do nothing
            dateModal.hide();
            drawMenu(); });

    dateModal.show();
}

void handleScreenSwitch(int dir)
{
    // dir: +1 for right, -1 for left

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
