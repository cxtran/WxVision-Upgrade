#include <Preferences.h>
#include "settings.h"
#include "sensors.h"
#include "units.h"   // <-- add this

// Preferences storage object
Preferences prefs;

// ==== Global settings ====
// --- Device ---
int dayFormat = 0;        // 0 = MM/DD, 1 = DD/MM
int forecastSrc = 0;      // 0 = OpenWeather, 1 = WeatherFlow
int autoRotate = 1;       // 1 = on, 0 = off
int manualScreen = 0;     // 0 = Main, 1 = Weather, etc.
String wifiSSID = "";
String wifiPass = "";

// --- Display ---
int theme = 0;            // 0 = Color, 1 = Mono
int brightness = 10;      // 1–100
int scrollSpeed = 150;    // derived from scrollLevel
int scrollLevel = 7;      // 0 (slow) to 9 (fast)
String customMsg = "";
const int scrollDelays[] = {500, 300, 200, 150, 100, 75, 50, 30, 20, 10};
bool autoBrightness = true;

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

// --- Date/Time/Timezone ---
int dstAuto = 0;
int timeZoneOffsetMinutes = 0;
int dateFormat = 0;
int timeFormat24h = 1;

void loadSettings() {
    prefs.begin("visionwx", true);

    // Device
    wifiSSID     = prefs.getString("wifiSSID", "");
    wifiPass     = prefs.getString("wifiPass", "");
    dayFormat    = prefs.getInt("dayFmt", 0);
    forecastSrc  = prefs.getInt("forecast", 0);
    autoRotate   = prefs.getInt("autoRotate", 1);
    manualScreen = prefs.getInt("manualScreen", 0);

    // Display
    theme        = prefs.getInt("theme", 0);
    theme        = constrain(theme, 0, 1);
    brightness   = prefs.getInt("brightness", 10);
    scrollLevel  = prefs.getInt("scrollLevel", 7); // default to 7 (fast)
    scrollLevel  = constrain(scrollLevel, 0, 9);
    scrollSpeed  = scrollDelays[scrollLevel];
    customMsg    = prefs.getString("customMsg", "");
    autoBrightness = prefs.getBool("autoBrightness", true);

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

    loadDateTimeSettings();

    prefs.end();

    // Load unit preferences from Units module
    loadUnits();
}

void saveDeviceSettings() {
    prefs.begin("visionwx", false);
    prefs.putString("wifiSSID", wifiSSID);
    prefs.putString("wifiPass", wifiPass);
    prefs.putInt("dayFmt", dayFormat);
    prefs.putInt("forecast", forecastSrc);
    prefs.putInt("autoRotate", autoRotate);
    prefs.putInt("manualScreen", manualScreen);
    prefs.end();
    // Units are saved via saveUnits() (see saveAllSettings())
}

void saveDisplaySettings() {
    Preferences prefs;
    if (prefs.begin("visionwx", false)) {
        prefs.putInt("theme", theme);
        prefs.putBool("autoBrightness", autoBrightness);
        prefs.putInt("brightness", brightness);
        prefs.putInt("scrollLevel", scrollLevel);  // persist level only
        prefs.putString("customMsg", customMsg);
        prefs.end();
        Serial.printf("[Prefs] Saved: theme=%d, auto=%d, bright=%d, scrollLevel=%d\n",
            theme, autoBrightness, brightness, scrollLevel);
    } else {
        Serial.println("[Prefs] Failed to open namespace 'display'");
    }
}

void saveWeatherSettings() {
    prefs.begin("visionwx", false);
    prefs.putInt("forecast", forecastSrc);
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

void saveAllSettings() {
    saveDeviceSettings();
    saveDisplaySettings();
    saveWeatherSettings();
    saveCalibrationSettings();
    saveDateTimeSettings();
    // Persist unit preferences too
    saveUnits();
}

// --- Toggles ---
void toggleDayFormat(int dir) {
    dayFormat = (dayFormat + dir + 2) % 2;
}

void toggleForecastSrc(int dir) {
    forecastSrc = (forecastSrc + dir + 2) % 2;
}

void toggleAutoRotate(int dir) {
    autoRotate = (autoRotate + dir + 2) % 2;
}

void toggleTheme(int dir) {
    int previous = theme;
    theme = (theme + dir + 2) % 2;
    if (theme != previous) {
        saveDisplaySettings();
    }
}

void adjustBrightness(int dir) {
    brightness += dir * 5;
    brightness = constrain(brightness, 1, 100);
}

void adjustTempOffset(int dir) {
    tempOffset = constrain(tempOffset + dir, -10, 10);
}

void adjustHumOffset(int dir) {
    humOffset = constrain(humOffset + dir, -20, 20);
}

void adjustLightGain(int dir) {
    lightGain += dir * 5;
    lightGain = constrain(lightGain, 1, 150);
    float lux = readBrightnessSensor();
    setDisplayBrightnessFromLux(lux);
}

void adjustScrollSpeed(int dir) {
    scrollLevel += dir;
    if (scrollLevel < 0) scrollLevel = 0;
    if (scrollLevel > 9) scrollLevel = 9;
    scrollSpeed = scrollDelays[scrollLevel];
}
