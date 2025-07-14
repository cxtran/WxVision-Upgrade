#include <Preferences.h>
#include "settings.h"

Preferences prefs;

// Global settings
int units = 0;        // 0 = F+mph, 1 = C+m/s
int dayFormat = 0;    // 0 = MM/DD/YYYY, 1 = DD/MM/YYYY
int forecastSrc = 0;  // 0 = OpenWeather, 1 = WeatherFlow
int autoRotate = 1;   // 1=on

int theme = 0;        // 0 = Color, 1 = Monochrome
int brightness = 50;  // 1 - 100
int scrollSpeed = 2;  // 1-5

int tempOffset = 0;   // degrees
int humOffset = 0;    // %
int lightGain = 100;  // %

void loadSettings() {
    prefs.begin("visionwx", true); // read-only
    units = prefs.getInt("units", 0);
    dayFormat = prefs.getInt("dayFmt", 0);
    forecastSrc = prefs.getInt("forecast", 0);
    autoRotate = prefs.getInt("autoRotate", 1);
    theme = prefs.getInt("theme", 0);
    brightness = prefs.getInt("brightness", 50);
    scrollSpeed = prefs.getInt("scrollSpeed", 2);
    tempOffset = prefs.getInt("tempOffset", 0);
    humOffset = prefs.getInt("humOffset", 0);
    lightGain = prefs.getInt("lightGain", 100);
    prefs.end();
}

void saveDeviceSettings() {
    prefs.begin("visionwx", false);
    prefs.putInt("units", units);
    prefs.putInt("dayFmt", dayFormat);
    prefs.putInt("forecast", forecastSrc);
    prefs.putInt("autoRotate", autoRotate);
    prefs.end();
}

void saveDisplaySettings() {
    prefs.begin("visionwx", false);
    prefs.putInt("theme", theme);
    prefs.putInt("brightness", brightness);
    prefs.putInt("scrollSpeed", scrollSpeed);
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
    // if you add more system-wide settings like OTA passwords
    prefs.begin("visionwx", false);
    prefs.end();
}

void saveAllSettings() {
    saveDeviceSettings();
    saveDisplaySettings();
    saveCalibrationSettings();
}

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



// You probably want to save API keys, tokens, city, station ID later.
// For now we just save the forecast source.

void saveWeatherSettings() {
    prefs.begin("visionwx", false);
    prefs.putInt("forecast", forecastSrc);  // already using your global
    prefs.end();
}
