#include <Preferences.h>
#include "settings.h"
#include "sensors.h"
#include "units.h"   // <-- add this
#include "display.h"

// Preferences storage object
Preferences prefs;

// ==== Global settings ====
// --- Device ---
int dayFormat = 0;        // 0 = MM/DD, 1 = DD/MM
int dataSource = DATA_SOURCE_OWM; // 0 = OpenWeather, 1 = WeatherFlow, 2 = None
int autoRotate = 1;       // 1 = on, 0 = off
int autoRotateInterval = 15; // seconds between screen rotations
int manualScreen = 0;     // 0 = Main, 1 = Weather, etc.
String wifiSSID = "";
String wifiPass = "";
bool alarmEnabled[3] = {false, false, false};
int alarmHour[3] = {7, 7, 7};
int alarmMinute[3] = {0, 0, 0};
AlarmRepeatMode alarmRepeatMode[3] = {ALARM_REPEAT_NONE, ALARM_REPEAT_NONE, ALARM_REPEAT_NONE};
int alarmWeeklyDay[3] = {1, 1, 1};
bool alarmOneShotPending[3] = {false, false, false};
bool noaaAlertsEnabled = false;
float noaaLatitude = 0.0f;
float noaaLongitude = 0.0f;

bool setupComplete = false;
bool initialSetupRequired = false;

// --- Display ---
int theme = 0;            // 0 = Day, 1 = Night
bool autoThemeSchedule = false;
int dayThemeStartMinutes = 6 * 60;
int nightThemeStartMinutes = 20 * 60;
int brightness = 10;      // 1???100
int scrollSpeed = 150;    // derived from scrollLevel
int scrollLevel = 7;      // 0 (slow) to 9 (fast)
String customMsg = "";
const int scrollDelays[] = {500, 300, 200, 150, 100, 75, 50, 30, 20, 10};
bool autoBrightness = true;
int splashDurationSec = 3;
bool themeRefreshPending = false;

static constexpr int MINUTES_PER_DAY = 24 * 60;
int normalizeThemeScheduleMinutes(int value)
{
    if (value < 0)
    {
        value %= MINUTES_PER_DAY;
        if (value < 0)
            value += MINUTES_PER_DAY;
    }
    else if (value >= MINUTES_PER_DAY)
    {
        value %= MINUTES_PER_DAY;
    }
    return value;
}

static int determineScheduledTheme(int currentMinutes)
{
    int dayStart = normalizeThemeScheduleMinutes(dayThemeStartMinutes);
    int nightStart = normalizeThemeScheduleMinutes(nightThemeStartMinutes);

    if (dayStart == nightStart)
    {
        // Same time -> treat as always Day.
        return 0;
    }

    if (dayStart < nightStart)
    {
        return (currentMinutes >= dayStart && currentMinutes < nightStart) ? 0 : 1;
    }
    // Day period wraps past midnight
    return (currentMinutes >= dayStart || currentMinutes < nightStart) ? 0 : 1;
}

static unsigned long s_lastAutoThemeCheck = 0;
static int s_lastAppliedScheduledTheme = -1;

// --- Weather ---
String owmCity = "";
String owmApiKey = "";
String wfToken = "";
String wfStationId = "";
int owmCountryIndex = 0;
String owmCountryCustom = "";

// --- Calibration ---
float tempOffset = 0.0f;   // degrees C
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
    int storedSource = prefs.getInt("forecast", DATA_SOURCE_OWM);
    setDataSource(storedSource);
    autoRotate   = prefs.getInt("autoRotate", 1);
    autoRotateInterval = prefs.getInt("autoRotInt", 15);
    autoRotateInterval = constrain(autoRotateInterval, 5, 300);
    manualScreen = prefs.getInt("manualScreen", 0);
    noaaAlertsEnabled = prefs.getBool("noaaEnabled", false);
    noaaLatitude = prefs.getFloat("noaaLat", 0.0f);
    noaaLongitude = prefs.getFloat("noaaLon", 0.0f);
    for (int i = 0; i < 3; ++i)
    {
        String idx = String(i);
        alarmEnabled[i] = prefs.getBool(("alarmEnabled" + idx).c_str(), false);
        alarmHour[i] = constrain(prefs.getInt(("alarmHour" + idx).c_str(), 7), 0, 23);
        alarmMinute[i] = constrain(prefs.getInt(("alarmMinute" + idx).c_str(), 0), 0, 59);
        alarmRepeatMode[i] = static_cast<AlarmRepeatMode>(prefs.getUChar(("alarmRepeat" + idx).c_str(), ALARM_REPEAT_NONE));
        if (alarmRepeatMode[i] > ALARM_REPEAT_WEEKEND)
            alarmRepeatMode[i] = ALARM_REPEAT_NONE;
        alarmWeeklyDay[i] = constrain(prefs.getInt(("alarmWeekDay" + idx).c_str(), 1), 0, 6);
        alarmOneShotPending[i] = prefs.getBool(("alarmOneShot" + idx).c_str(), false);
    }

    bool setupFlagExists = prefs.isKey("setupReady");
    setupComplete = setupFlagExists ? prefs.getBool("setupReady", false)
                                    : !wifiSSID.isEmpty();
    initialSetupRequired = !setupComplete;

    // Display
    theme        = prefs.getInt("theme", 0);
    theme        = constrain(theme, 0, 1);
    autoThemeSchedule = prefs.getBool("autoThemeSched", false);
    dayThemeStartMinutes = normalizeThemeScheduleMinutes(prefs.getInt("dayThemeStart", 6 * 60));
    nightThemeStartMinutes = normalizeThemeScheduleMinutes(prefs.getInt("nightThemeStart", 20 * 60));
    brightness   = prefs.getInt("brightness", 10);
    scrollLevel  = prefs.getInt("scrollLevel", 7); // default to 7 (fast)
    scrollLevel  = constrain(scrollLevel, 0, 9);
    scrollSpeed  = scrollDelays[scrollLevel];
    customMsg    = prefs.getString("customMsg", "");
    autoBrightness = prefs.getBool("autoBrightness", true);
    splashDurationSec = prefs.getInt("splashDur", 3);
    splashDurationSec = constrain(splashDurationSec, 1, 10);

    // Weather
    owmCity      = prefs.getString("owmCity", "");
    owmApiKey    = prefs.getString("owmApiKey", "");
    owmCountryIndex = prefs.getInt("owmCountryIndex", 0);
    owmCountryCustom = prefs.getString("owmCountryCustom", "");
    wfToken      = prefs.getString("wfToken", "");
    wfStationId  = prefs.getString("wfStationId", "");

    // Calibration
    if (prefs.isKey("tempOffsetF")) {
        tempOffset = prefs.getFloat("tempOffsetF", 0.0f);
    } else {
        tempOffset = static_cast<float>(prefs.getInt("tempOffset", 0));
    }
    tempOffset = constrain(tempOffset, -10.0f, 10.0f);
    humOffset    = prefs.getInt("humOffset", 0);
    lightGain    = prefs.getInt("lightGain", 100);

    loadDateTimeSettings();

    prefs.end();

    // Load unit preferences from Units module
    loadUnits();
    applyUnitPreferences();
}

void saveDeviceSettings() {
    prefs.begin("visionwx", false);
    prefs.putString("wifiSSID", wifiSSID);
    prefs.putString("wifiPass", wifiPass);
    prefs.putInt("dayFmt", dayFormat);
    prefs.putInt("forecast", dataSource);
    prefs.putInt("autoRotate", autoRotate);
    prefs.putInt("autoRotInt", autoRotateInterval);
    prefs.putInt("manualScreen", manualScreen);
    prefs.end();
    // Units are saved via saveUnits() (see saveAllSettings())
}

void saveDisplaySettings() {
    Preferences prefs;
    if (prefs.begin("visionwx", false)) {
        prefs.putInt("theme", theme);
        prefs.putBool("autoThemeSched", autoThemeSchedule);
        prefs.putInt("dayThemeStart", normalizeThemeScheduleMinutes(dayThemeStartMinutes));
        prefs.putInt("nightThemeStart", normalizeThemeScheduleMinutes(nightThemeStartMinutes));
        prefs.putBool("autoBrightness", autoBrightness);
        prefs.putInt("brightness", brightness);
        prefs.putInt("scrollLevel", scrollLevel);  // persist level only
        prefs.putString("customMsg", customMsg);
        splashDurationSec = constrain(splashDurationSec, 1, 10);
        prefs.putInt("splashDur", splashDurationSec);
        prefs.end();
        Serial.printf("[Prefs] Saved: theme=%d, schedule=%d, dayStart=%d, nightStart=%d, auto=%d, bright=%d, scrollLevel=%d\n",
            theme, autoThemeSchedule, dayThemeStartMinutes, nightThemeStartMinutes, autoBrightness, brightness, scrollLevel);
    } else {
        Serial.println("[Prefs] Failed to open namespace 'display'");
    }
}

void saveWeatherSettings() {
    prefs.begin("visionwx", false);
    prefs.putInt("forecast", dataSource);
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
    prefs.putFloat("tempOffsetF", tempOffset);
    if (prefs.isKey("tempOffset")) {
        prefs.remove("tempOffset");
    }
    prefs.putInt("humOffset", humOffset);
    prefs.putInt("lightGain", lightGain);
    prefs.end();
}

void saveAlarmSettings() {
    prefs.begin("visionwx", false);
    for (int i = 0; i < 3; ++i)
    {
        prefs.putBool(("alarmEnabled" + String(i)).c_str(), alarmEnabled[i]);
        prefs.putInt(("alarmHour" + String(i)).c_str(), constrain(alarmHour[i], 0, 23));
        prefs.putInt(("alarmMinute" + String(i)).c_str(), constrain(alarmMinute[i], 0, 59));
        prefs.putUChar(("alarmRepeat" + String(i)).c_str(), static_cast<uint8_t>(alarmRepeatMode[i]));
        prefs.putInt(("alarmWeekDay" + String(i)).c_str(), constrain(alarmWeeklyDay[i], 0, 6));
        prefs.putBool(("alarmOneShot" + String(i)).c_str(), alarmOneShotPending[i]);
    }
    prefs.end();
}

void saveNoaaSettings() {
    prefs.begin("visionwx", false);
    prefs.putBool("noaaEnabled", noaaAlertsEnabled);
    prefs.putFloat("noaaLat", noaaLatitude);
    prefs.putFloat("noaaLon", noaaLongitude);
    prefs.end();
}

void saveAllSettings() {
    saveDeviceSettings();
    saveDisplaySettings();
    saveWeatherSettings();
    saveCalibrationSettings();
    saveAlarmSettings();
    saveNoaaSettings();
    saveDateTimeSettings();
    // Persist unit preferences too
    saveUnits();
}

// --- Toggles ---
void toggleDayFormat(int dir) {
    dayFormat = (dayFormat + dir + 2) % 2;
}

void setDataSource(int source) {
    if (source < DATA_SOURCE_OWM || source > DATA_SOURCE_NONE) {
        source = DATA_SOURCE_OWM;
    }
    dataSource = source;
}

bool isDataSourceOwm() {
    return dataSource == DATA_SOURCE_OWM;
}

bool isDataSourceWeatherFlow() {
    return dataSource == DATA_SOURCE_WEATHERFLOW;
}

bool isDataSourceNone() {
    return dataSource == DATA_SOURCE_NONE;
}

void toggleDataSource(int dir) {
    if (dir == 0) return;
    int next = dataSource + (dir > 0 ? 1 : -1);
    if (next > DATA_SOURCE_NONE) {
        next = DATA_SOURCE_OWM;
    } else if (next < DATA_SOURCE_OWM) {
        next = DATA_SOURCE_NONE;
    }
    setDataSource(next);
}

void setAutoRotateEnabled(bool enabled, bool persist) {
    autoRotate = enabled ? 1 : 0;
    if (persist) {
        saveDeviceSettings();
    }
}

void setAutoRotateInterval(int seconds, bool persist) {
    autoRotateInterval = constrain(seconds, 5, 300);
    if (persist) {
        saveDeviceSettings();
    }
}

void markSetupComplete(bool complete) {
    setupComplete = complete;
    initialSetupRequired = !complete;

    Preferences local;
    if (local.begin("visionwx", false)) {
        local.putBool("setupReady", setupComplete);
        local.end();
    }
}

void toggleAutoRotate(int dir) {
    setAutoRotateEnabled(((autoRotate + dir + 2) % 2) == 1);
}

void toggleTheme(int dir) {
    int previous = theme;
    theme = (theme + dir + 2) % 2;
    if (theme != previous) {
        saveDisplaySettings();
        themeRefreshPending = true;
    }
}

void adjustBrightness(int dir) {
    brightness += dir * 5;
    brightness = constrain(brightness, 1, 100);
}

void adjustTempOffset(int dir) {
    float step = (units.temp == TempUnit::F) ? (5.0f / 9.0f * 0.1f) : 0.1f;
    tempOffset = constrain(tempOffset + dir * step, -10.0f, 10.0f);
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

void tickAutoThemeSchedule()
{
    if (!autoThemeSchedule)
    {
        s_lastAppliedScheduledTheme = -1;
        return;
    }

    unsigned long nowMs = millis();
    if (s_lastAppliedScheduledTheme != -1 && (nowMs - s_lastAutoThemeCheck) < 15000)
    {
        return;
    }
    s_lastAutoThemeCheck = nowMs;

    DateTime localNow;
    if (!getLocalDateTime(localNow))
    {
        return;
    }
    int minutes = normalizeThemeScheduleMinutes(localNow.hour() * 60 + localNow.minute());
    int desiredTheme = determineScheduledTheme(minutes);
    if (desiredTheme != theme)
    {
        theme = desiredTheme;
        themeRefreshPending = true;
        saveDisplaySettings();
    }
    s_lastAppliedScheduledTheme = desiredTheme;
}

void forceAutoThemeSchedule()
{
    s_lastAutoThemeCheck = 0;
    s_lastAppliedScheduledTheme = -1;
    tickAutoThemeSchedule();
}
