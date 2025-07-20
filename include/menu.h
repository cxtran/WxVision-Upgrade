#pragma once
#include <Arduino.h>
#include <vector>
#include "InfoModal.h"

// --- Menu Core Functions ---
void handleIR(uint32_t code);
void connectToWiFi();
void drawWiFiMenu();
void onWiFiConnectFailed();
void updateMenu();
void drawMenu();
void handleUp();
void handleDown();
void handleSelect();
void handleLeft();
void handleRight();

// --- Feature handlers for new System Menu items ---
void showSystemInfoScreen();
void showDateTimeModal();
void showWiFiSignalTest();



// WiFi Signal Test

// --- WiFi/Network Globals ---
extern std::vector<String> foundSSIDs;
extern int selectedWifiIdx;
extern bool wifiSelecting;
extern int wifiScanCount;

// --- Menu Identifiers ---
enum MenuLevel {
    MENU_MAIN,
    MENU_DEVICE,
    MENU_DISPLAY,
    MENU_WEATHER,
    MENU_CALIBRATION,
    MENU_SYSTEM,
    MENU_MANUAL_SCREEN,
    MENU_WIFI_SELECT = 99,
    MENU_BLE_SELECT
};

// --- Menu State ---
extern MenuLevel currentMenuLevel;
extern int currentMenuIndex;

// --- External references to core app logic ---
extern void saveDeviceSettings();
extern void displayClock();
extern void displayDate();
extern void displayWeatherData();
extern void fetchWeatherFromOWM();
extern bool reset_Time_and_Date_Display;

// --- Country Code (OWM) support (for weather menu) ---
struct CountryEntry {
    const char *name;
    const char *code;
};
extern CountryEntry countries[];
extern const int numCountries;
extern int owmCountryIndex;
extern String owmCountryCustom;

// --- Settings for Weather/Device/Display/Calibration (extern if needed) ---
extern String wifiSSID;
extern String wifiPass;
extern String owmCity;
extern String owmApiKey;
extern String wfToken;
extern String wfStationId;
extern int units;
extern int dayFormat;
extern int forecastSrc;
extern int autoRotate;
extern int manualScreen;
extern int theme;
extern int brightness;
extern int scrollSpeed;
extern String customMsg;
extern int tempOffset;
extern int humOffset;
extern int lightGain;

// --- Any additional new globals or helpers as needed (add here) ---
