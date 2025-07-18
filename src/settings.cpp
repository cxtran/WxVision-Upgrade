#include <Preferences.h>
#include "settings.h"

// Preferences storage object
Preferences prefs;

// ==== Global settings ====
// --- Device ---
int units = 0;            // 0 = F+mph, 1 = C+m/s
int dayFormat = 0;        // 0 = MM/DD, 1 = DD/MM
int forecastSrc = 0;      // 0 = OpenWeather, 1 = WeatherFlow
int autoRotate = 1;       // 1 = on, 0 = off
int manualScreen = 0;     // 0 = Main, 1 = Weather, etc.
String wifiSSID = "";
String wifiPass = "";

// --- Display ---
int theme = 0;            // 0 = Color, 1 = Mono
int brightness = 50;      // 1-100
int scrollSpeed = 2;      // 1-5
String customMsg = "";

// --- Weather ---
String owmCity = "";
String owmApiKey = "";
String wfToken = "";
String wfStationId = "";
int owmCountryIndex = 0;
String owmCountryCustom = "";

// --- Calibration ---
int tempOffset = 0;   // degrees
int humOffset = 0;    // %
int lightGain = 100;  // %

void loadSettings() {
    prefs.begin("visionwx", true); // read-only

    // Device
    wifiSSID     = prefs.getString("wifiSSID", "");
    wifiPass     = prefs.getString("wifiPass", "");
    units        = prefs.getInt("units", 0);
    dayFormat    = prefs.getInt("dayFmt", 0);
    forecastSrc  = prefs.getInt("forecast", 0);
    autoRotate   = prefs.getInt("autoRotate", 1);
    manualScreen = prefs.getInt("manualScreen", 0);

    // Display
    theme        = prefs.getInt("theme", 0);
    brightness   = prefs.getInt("brightness", 50);
    scrollSpeed  = prefs.getInt("scrollSpeed", 2);
    customMsg    = prefs.getString("customMsg", "");

    // Weather
    owmCity      = prefs.getString("owmCity", "");
    owmApiKey    = prefs.getString("owmApiKey", "");
    owmCountryIndex = prefs.getInt("owmCountryIndex", 0);
    owmCountryCustom = prefs.getString("owmCountryCustom", "");
    wfToken      = prefs.getString("wfToken", "");
    wfStationId  = prefs.getString("wfStationId", "");

    // Calibration
    tempOffset   = prefs.getInt("tempOffset", 0);
    humOffset    = prefs.getInt("humOffset", 0);
    lightGain    = prefs.getInt("lightGain", 100);

    prefs.end();
}

void saveDeviceSettings() {
    prefs.begin("visionwx", false);
    prefs.putString("wifiSSID", wifiSSID);
    prefs.putString("wifiPass", wifiPass);
    prefs.putInt("units", units);
    prefs.putInt("dayFmt", dayFormat);
    prefs.putInt("forecast", forecastSrc);
    prefs.putInt("autoRotate", autoRotate);
    prefs.putInt("manualScreen", manualScreen);
    prefs.end();
}

void saveDisplaySettings() {
    prefs.begin("visionwx", false);
    prefs.putInt("theme", theme);
    prefs.putInt("brightness", brightness);
    prefs.putInt("scrollSpeed", scrollSpeed);
    prefs.putString("customMsg", customMsg);
    prefs.end();
}

void saveWeatherSettings() {
    prefs.begin("visionwx", false);
    prefs.putInt("forecast", forecastSrc); // Allow changing source here too
    prefs.putString("owmCity", owmCity);
    prefs.putString("owmApiKey", owmApiKey);
    prefs.putInt("owmCountryIndex", owmCountryIndex);
    prefs.putString("owmCountryCustom", owmCountryCustom);
    prefs.putString("wfToken", wfToken);
    prefs.putString("wfStationId", wfStationId);
    prefs.end();
}

void saveCalibrationSettings() {
    prefs.begin("visionwx", false);
    prefs.putInt("tempOffset", tempOffset);
    prefs.putInt("humOffset", humOffset);
    prefs.putInt("lightGain", lightGain);
    prefs.end();
}

void saveSystemSettings() {
    prefs.begin("visionwx", false);
    // Add any future system-wide settings here
    prefs.end();
}

void saveAllSettings() {
    saveDeviceSettings();
    saveDisplaySettings();
    saveWeatherSettings();
    saveCalibrationSettings();
    saveSystemSettings();
}

// --- Value toggles/adjusts (unchanged) ---
void toggleUnits(int dir) {
    units += dir;
    if (units < 0) units = 1;
    if (units > 1) units = 0;
}

void toggleDayFormat(int dir) {
    dayFormat += dir;
    if (dayFormat < 0) dayFormat = 1;
    if (dayFormat > 1) dayFormat = 0;
}

void toggleForecastSrc(int dir) {
    forecastSrc += dir;
    if (forecastSrc < 0) forecastSrc = 1;
    if (forecastSrc > 1) forecastSrc = 0;
}

void toggleAutoRotate(int dir) {
    autoRotate += dir;
    if (autoRotate < 0) autoRotate = 1;
    if (autoRotate > 1) autoRotate = 0;
}

void toggleTheme(int dir) {
    theme += dir;
    if (theme < 0) theme = 1;
    if (theme > 1) theme = 0;
}

void adjustBrightness(int dir) {
    brightness += dir * 5;
    if (brightness < 1) brightness = 1;
    if (brightness > 100) brightness = 100;
}

void adjustScrollSpeed(int dir) {
    scrollSpeed += dir;
    if (scrollSpeed < 1) scrollSpeed = 1;
    if (scrollSpeed > 5) scrollSpeed = 5;
}

void adjustTempOffset(int dir) {
    tempOffset += dir;
    if (tempOffset < -10) tempOffset = -10;
    if (tempOffset > 10) tempOffset = 10;
}

void adjustHumOffset(int dir) {
    humOffset += dir;
    if (humOffset < -20) humOffset = -20;
    if (humOffset > 20) humOffset = 20;
}

void adjustLightGain(int dir) {
    lightGain += dir * 5;
    if (lightGain < 50) lightGain = 50;
    if (lightGain > 150) lightGain = 150;
}
