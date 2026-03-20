#include <Preferences.h>
#include "settings.h"
#include "sensors.h"
#include "units.h"   // <-- add this
#include "display.h"
#include "default_values.h"
#include "noaa.h"

// Preferences storage object
Preferences prefs;

// ==== Global settings ====
// --- Device ---
int dayFormat = wxv::defaults::kDefaults.dayFormat;        // 0 = MM/DD, 1 = DD/MM
int dataSource = static_cast<int>(wxv::defaults::toStorage(wxv::defaults::kDefaults.provider)); // 0 = OpenWeather, 1 = WeatherFlow, 2 = None
int autoRotate = wxv::defaults::kDefaults.autoRotate;       // 1 = on, 0 = off
int autoRotateInterval = wxv::defaults::kDefaults.autoRotateIntervalSec; // seconds between screen rotations
int manualScreen = wxv::defaults::kDefaults.manualScreen;     // 0 = Main, 1 = Weather, etc.
String wifiSSID = "";
String wifiPass = "";
bool alarmEnabled[3] = {false, false, false};
int alarmHour[3] = {7, 7, 7};
int alarmMinute[3] = {0, 0, 0};
AlarmRepeatMode alarmRepeatMode[3] = {ALARM_REPEAT_NONE, ALARM_REPEAT_NONE, ALARM_REPEAT_NONE};
int alarmWeeklyDay[3] = {1, 1, 1};
bool alarmOneShotPending[3] = {false, false, false};
bool noaaAlertsEnabled = wxv::defaults::kDefaults.noaaAlertsEnabled;
float noaaLatitude = wxv::defaults::kDefaults.noaaLatitude;
float noaaLongitude = wxv::defaults::kDefaults.noaaLongitude;

bool setupComplete = false;
bool initialSetupRequired = false;

// --- Display ---
int theme = wxv::defaults::kDefaults.themeIndex;            // 0 = Day, 1 = Night
bool autoThemeSchedule = wxv::defaults::kDefaults.autoThemeScheduleLegacy;
bool autoThemeAmbient = false;
int autoThemeLightThreshold = wxv::defaults::kDefaults.autoThemeLuxThreshold;
int dayThemeStartMinutes = wxv::defaults::kDefaults.dayThemeStartMinutes;
int nightThemeStartMinutes = wxv::defaults::kDefaults.nightThemeStartMinutes;
int brightness = wxv::defaults::kDefaults.brightness;      // 1???100
int scrollSpeed = 150;    // derived from scrollLevel
int verticalScrollSpeed = 150; // independent vertical scroll speed
int scrollLevel = wxv::defaults::kDefaults.scrollLevel;      // 0 (slow) to 9 (fast)
int verticalScrollLevel = wxv::defaults::kDefaults.scrollLevel;
String customMsg = "";
const int scrollDelays[] = {
    wxv::defaults::kScrollDelays[0], wxv::defaults::kScrollDelays[1], wxv::defaults::kScrollDelays[2],
    wxv::defaults::kScrollDelays[3], wxv::defaults::kScrollDelays[4], wxv::defaults::kScrollDelays[5],
    wxv::defaults::kScrollDelays[6], wxv::defaults::kScrollDelays[7], wxv::defaults::kScrollDelays[8],
    wxv::defaults::kScrollDelays[9]};
bool autoBrightness = wxv::defaults::kDefaults.autoBrightnessEnabled;
bool sceneClockEnabled = wxv::defaults::kDefaults.sceneClockEnabled;
int splashDurationSec = wxv::defaults::kDefaults.splashDurationSec;
bool themeRefreshPending = false;
int buzzerVolume = wxv::defaults::kDefaults.buzzerVolume;
int buzzerToneSet = wxv::defaults::kDefaults.buzzerToneSet; // 0=Bright,1=Soft,2=Click,3=Chime,4=Pulse,5=Warm,6=Melody
int alarmSoundMode = wxv::defaults::kDefaults.alarmSoundMode; // 0=Tone,1=FurElise,2=SwanLake,3=TurkeyMarch,4=Moonlight
int forecastLinesPerDay = wxv::defaults::kDefaults.forecastLinesPerDay;
int forecastPauseMs = wxv::defaults::kDefaults.forecastPauseMs;
int forecastIconSize = wxv::defaults::kDefaults.forecastIconSize;

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
static unsigned long s_lastAmbientThemeCheck = 0;
static int s_lastAppliedAmbientTheme = -1;

// --- Weather ---
String owmCity = "";
String owmApiKey = "";
String wfToken = "";
String wfStationId = "";
int owmCountryIndex = 0;
String owmCountryCustom = "";

// --- Calibration ---
float tempOffset = wxv::defaults::kTempOffsetDefaultC;   // degrees C
int humOffset = wxv::defaults::kHumOffsetDefault;    // %
int lightGain = wxv::defaults::kLightGainDefaultPercent;  // %
int envAlertCo2Threshold = 1200;
float envAlertTempThresholdC = 26.5f;
int envAlertHumidityLowThreshold = 30;
int envAlertHumidityHighThreshold = 60;
bool envAlertCo2Enabled = wxv::defaults::kDefaults.envAlertCo2Enabled;
bool envAlertTempEnabled = wxv::defaults::kDefaults.envAlertTempEnabled;
bool envAlertHumidityEnabled = wxv::defaults::kDefaults.envAlertHumidityEnabled;

// --- Date/Time/Timezone ---
int dstAuto = 0;
int timeZoneOffsetMinutes = 0;
int dateFormat = wxv::defaults::kDefaults.dateFormatStorage;
int timeFormat24h = wxv::defaults::toStorage(wxv::defaults::kDefaults.timeFormat);

static void applyNoaaBuildDefaults()
{
#if !WXV_ENABLE_NOAA_ALERTS
    noaaAlertsEnabled = false;
#endif
}

void loadSettings() {
    prefs.begin("visionwx", true);

    // Device
    wifiSSID     = prefs.getString("wifiSSID", "");
    wifiPass     = prefs.getString("wifiPass", "");
    dayFormat    = prefs.getInt("dayFmt", wxv::defaults::kDefaults.dayFormat);
    int storedSource = prefs.getInt("forecast", static_cast<int>(wxv::defaults::toStorage(wxv::defaults::kDefaults.provider)));
    setDataSource(storedSource);
    autoRotate   = prefs.getInt("autoRotate", wxv::defaults::kDefaults.autoRotate);
    autoRotateInterval = prefs.getInt("autoRotInt", wxv::defaults::kDefaults.autoRotateIntervalSec);
    autoRotateInterval = constrain(autoRotateInterval, 5, 300);
    manualScreen = prefs.getInt("manualScreen", wxv::defaults::kDefaults.manualScreen);
    noaaAlertsEnabled = prefs.getBool("noaaEnabled", wxv::defaults::kDefaults.noaaAlertsEnabled);
    noaaLatitude = prefs.getFloat("noaaLat", wxv::defaults::kDefaults.noaaLatitude);
    noaaLongitude = prefs.getFloat("noaaLon", wxv::defaults::kDefaults.noaaLongitude);
    applyNoaaBuildDefaults();
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
    theme        = prefs.getInt("theme", wxv::defaults::kDefaults.themeIndex);
    theme        = constrain(theme, 0, 1);
    int storedAutoThemeMode = prefs.getInt("autoThemeMode", -1);
    bool legacyAutoTheme = prefs.getBool("autoThemeSched", wxv::defaults::kDefaults.autoThemeScheduleLegacy);
    autoThemeSchedule = (storedAutoThemeMode == 1) || (storedAutoThemeMode < 0 && legacyAutoTheme);
    autoThemeAmbient = (storedAutoThemeMode == 2);
    autoThemeLightThreshold = prefs.getInt("autoThemeLux", wxv::defaults::kDefaults.autoThemeLuxThreshold);
    autoThemeLightThreshold = constrain(autoThemeLightThreshold, 1, 5000);
    dayThemeStartMinutes = normalizeThemeScheduleMinutes(prefs.getInt("dayThemeStart", wxv::defaults::kDefaults.dayThemeStartMinutes));
    nightThemeStartMinutes = normalizeThemeScheduleMinutes(prefs.getInt("nightThemeStart", wxv::defaults::kDefaults.nightThemeStartMinutes));
    brightness   = prefs.getInt("brightness", wxv::defaults::kDefaults.brightness);
    scrollLevel  = prefs.getInt("scrollLevel", wxv::defaults::kDefaults.scrollLevel); // default to 7 (fast)
    scrollLevel  = constrain(scrollLevel, 0, 9);
    scrollSpeed  = scrollDelays[scrollLevel];
    verticalScrollLevel = prefs.getInt("vScrollLevel", scrollLevel);
    verticalScrollLevel = constrain(verticalScrollLevel, 0, 9);
    verticalScrollSpeed = scrollDelays[verticalScrollLevel];
    customMsg    = prefs.getString("customMsg", "");
    autoBrightness = prefs.getBool("autoBrightness", wxv::defaults::kDefaults.autoBrightnessEnabled);
      sceneClockEnabled = prefs.getBool("sceneClock", wxv::defaults::kDefaults.sceneClockEnabled);
      splashDurationSec = prefs.getInt("splashDur", wxv::defaults::kDefaults.splashDurationSec);
      splashDurationSec = constrain(splashDurationSec, 1, 10);
      buzzerVolume = constrain(prefs.getInt("buzzVol", wxv::defaults::kDefaults.buzzerVolume), 0, 100);
      buzzerToneSet = constrain(prefs.getInt("buzzTone", wxv::defaults::kDefaults.buzzerToneSet), 0, 6);
      alarmSoundMode = constrain(prefs.getInt("alarmSound", wxv::defaults::kDefaults.alarmSoundMode), 0, 4);
      forecastLinesPerDay = constrain(prefs.getInt("fcLines", wxv::defaults::kDefaults.forecastLinesPerDay), 2, 3);
      forecastPauseMs = constrain(prefs.getInt("fcPause", wxv::defaults::kDefaults.forecastPauseMs), 0, 10000);
      forecastIconSize = prefs.getInt("fcIcon", wxv::defaults::kDefaults.forecastIconSize);
      if (forecastIconSize != 0 && forecastIconSize != 16)
          forecastIconSize = 16;

    // Weather
    owmCity      = prefs.getString("owmCity", "");
    owmApiKey    = prefs.getString("owmApiKey", "");
    owmCountryIndex = prefs.getInt("owmCountryIndex", 0);
    owmCountryCustom = prefs.getString("owmCountryCustom", "");
    wfToken      = prefs.getString("wfToken", "");
    wfStationId  = prefs.getString("wfStationId", "");

    // Calibration
    if (prefs.isKey("tempOffsetF")) {
        tempOffset = prefs.getFloat("tempOffsetF", wxv::defaults::kTempOffsetDefaultC);
    } else {
        tempOffset = static_cast<float>(prefs.getInt("tempOffset", static_cast<int>(wxv::defaults::kTempOffsetDefaultC)));
    }
    tempOffset = constrain(tempOffset, wxv::defaults::kTempOffsetMinC, wxv::defaults::kTempOffsetMaxC);
    humOffset    = prefs.getInt("humOffset", wxv::defaults::kHumOffsetDefault);
    lightGain    = constrain(prefs.getInt("lightGain", wxv::defaults::kLightGainDefaultPercent), wxv::defaults::kLightGainMinPercent, wxv::defaults::kLightGainMaxPercent);
    envAlertCo2Threshold = constrain(prefs.getInt("envCo2Thr", 1200), 400, 5000);
    envAlertTempThresholdC = static_cast<float>(prefs.getInt("envTempThr", 265)) / 10.0f;
    envAlertTempThresholdC = constrain(envAlertTempThresholdC, 10.0f, 50.0f);
    envAlertHumidityLowThreshold = constrain(prefs.getInt("envHumLow", 30), 0, 100);
    envAlertHumidityHighThreshold = constrain(prefs.getInt("envHumHigh", 60), 0, 100);
    envAlertCo2Enabled = prefs.getBool("envCo2En", wxv::defaults::kDefaults.envAlertCo2Enabled);
    envAlertTempEnabled = prefs.getBool("envTempEn", wxv::defaults::kDefaults.envAlertTempEnabled);
    envAlertHumidityEnabled = prefs.getBool("envHumEn", wxv::defaults::kDefaults.envAlertHumidityEnabled);
    if (envAlertHumidityLowThreshold > envAlertHumidityHighThreshold)
        envAlertHumidityLowThreshold = envAlertHumidityHighThreshold;

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
    prefs.putInt("buzzVol", constrain(buzzerVolume, 0, 100));
    prefs.putInt("buzzTone", constrain(buzzerToneSet, 0, 6));
    prefs.putInt("alarmSound", constrain(alarmSoundMode, 0, 4));
    prefs.end();
    // Units are saved via saveUnits() (see saveAllSettings())
}

void saveDisplaySettings() {
    Preferences prefs;
    if (prefs.begin("visionwx", false)) {
        prefs.putInt("theme", theme);
        int autoThemeMode = autoThemeAmbient ? 2 : (autoThemeSchedule ? 1 : 0);
        prefs.putInt("autoThemeMode", autoThemeMode);
        prefs.putBool("autoThemeSched", autoThemeSchedule);
        prefs.putInt("autoThemeLux", autoThemeLightThreshold);
        prefs.putInt("dayThemeStart", normalizeThemeScheduleMinutes(dayThemeStartMinutes));
        prefs.putInt("nightThemeStart", normalizeThemeScheduleMinutes(nightThemeStartMinutes));
        prefs.putBool("autoBrightness", autoBrightness);
        prefs.putBool("sceneClock", sceneClockEnabled);
        prefs.putInt("brightness", brightness);
        prefs.putInt("scrollLevel", scrollLevel);  // persist level only
        prefs.putInt("vScrollLevel", verticalScrollLevel);
        prefs.putString("customMsg", customMsg);
        splashDurationSec = constrain(splashDurationSec, 1, 10);
        prefs.putInt("splashDur", splashDurationSec);
        prefs.putInt("fcLines", constrain(forecastLinesPerDay, 2, 3));
        prefs.putInt("fcPause", constrain(forecastPauseMs, 0, 10000));
        prefs.putInt("fcIcon", (forecastIconSize == 0) ? 0 : 16);
        prefs.end();
        Serial.printf("[Prefs] Saved: theme=%d, autoThemeMode=%d, luxThr=%d, dayStart=%d, nightStart=%d, auto=%d, bright=%d, scrollLevel=%d vScrollLevel=%d\n",
            theme, autoThemeMode, autoThemeLightThreshold, dayThemeStartMinutes, nightThemeStartMinutes, autoBrightness, brightness, scrollLevel, verticalScrollLevel);
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
    lightGain = constrain(lightGain, wxv::defaults::kLightGainMinPercent, wxv::defaults::kLightGainMaxPercent);
    prefs.putInt("lightGain", lightGain);
    envAlertCo2Threshold = constrain(envAlertCo2Threshold, 400, 5000);
    envAlertTempThresholdC = constrain(envAlertTempThresholdC, 10.0f, 50.0f);
    envAlertHumidityLowThreshold = constrain(envAlertHumidityLowThreshold, 0, 100);
    envAlertHumidityHighThreshold = constrain(envAlertHumidityHighThreshold, 0, 100);
    if (envAlertHumidityLowThreshold > envAlertHumidityHighThreshold)
        envAlertHumidityLowThreshold = envAlertHumidityHighThreshold;
    prefs.putInt("envCo2Thr", envAlertCo2Threshold);
    prefs.putInt("envTempThr", static_cast<int>(lroundf(envAlertTempThresholdC * 10.0f)));
    prefs.putInt("envHumLow", envAlertHumidityLowThreshold);
    prefs.putInt("envHumHigh", envAlertHumidityHighThreshold);
    prefs.putBool("envCo2En", envAlertCo2Enabled);
    prefs.putBool("envTempEn", envAlertTempEnabled);
    prefs.putBool("envHumEn", envAlertHumidityEnabled);
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
    prefs.putInt("alarmSound", constrain(alarmSoundMode, 0, 4));
    prefs.end();
}

void saveNoaaSettings() {
    prefs.begin("visionwx", false);
    applyNoaaBuildDefaults();
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
    if (source < DATA_SOURCE_OWM || source > DATA_SOURCE_OPEN_METEO) {
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

bool isDataSourceOpenMeteo() {
    return dataSource == DATA_SOURCE_OPEN_METEO;
}

bool isDataSourceForecastModel() {
    return dataSource == DATA_SOURCE_WEATHERFLOW || dataSource == DATA_SOURCE_OPEN_METEO;
}

bool isDataSourceNone() {
    return dataSource == DATA_SOURCE_NONE;
}

void toggleDataSource(int dir) {
    if (dir == 0) return;
    int next = dataSource + (dir > 0 ? 1 : -1);
    if (next > DATA_SOURCE_OPEN_METEO) {
        next = DATA_SOURCE_OWM;
    } else if (next < DATA_SOURCE_OWM) {
        next = DATA_SOURCE_OPEN_METEO;
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
    float step = (units.temp == TempUnit::F) ? (5.0f / 9.0f * wxv::defaults::kTempOffsetStepC) : wxv::defaults::kTempOffsetStepC;
    tempOffset = constrain(tempOffset + dir * step, wxv::defaults::kTempOffsetMinC, wxv::defaults::kTempOffsetMaxC);
}

void adjustHumOffset(int dir) {
    humOffset = constrain(humOffset + dir * wxv::defaults::kHumOffsetStep, wxv::defaults::kHumOffsetMin, wxv::defaults::kHumOffsetMax);
}

void adjustLightGain(int dir) {
    lightGain += dir * wxv::defaults::kLightGainStepPercent;
    lightGain = constrain(lightGain, wxv::defaults::kLightGainMinPercent, wxv::defaults::kLightGainMaxPercent);
    float lux = readBrightnessSensor();
    setDisplayBrightnessFromLux(lux);
}

void adjustScrollSpeed(int dir) {
    scrollLevel += dir;
    if (scrollLevel < 0) scrollLevel = 0;
    if (scrollLevel > 9) scrollLevel = 9;
    scrollSpeed = scrollDelays[scrollLevel];
}

void adjustVerticalScrollSpeed(int dir) {
    verticalScrollLevel += dir;
    if (verticalScrollLevel < 0) verticalScrollLevel = 0;
    if (verticalScrollLevel > 9) verticalScrollLevel = 9;
    verticalScrollSpeed = scrollDelays[verticalScrollLevel];
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

void tickAutoThemeAmbient(float lux, bool persist, bool force)
{
    if (!autoThemeAmbient)
    {
        s_lastAppliedAmbientTheme = -1;
        return;
    }

    unsigned long nowMs = millis();
    if (!force && s_lastAppliedAmbientTheme != -1 && (nowMs - s_lastAmbientThemeCheck) < 5000)
    {
        return;
    }
    s_lastAmbientThemeCheck = nowMs;

    if (isnan(lux) || lux < 0.0f)
    {
        Serial.println("[ThemeAmbient] lux invalid; skipping");
        return;
    }

    int desiredTheme = (lux < autoThemeLightThreshold) ? 1 : 0;
    if (desiredTheme != theme)
    {
        theme = desiredTheme;
        themeRefreshPending = true;
        if (persist)
        {
            saveDisplaySettings();
        }
    }
    s_lastAppliedAmbientTheme = desiredTheme;
}

void forceAutoThemeSchedule()
{
    s_lastAutoThemeCheck = 0;
    s_lastAppliedScheduledTheme = -1;
    tickAutoThemeSchedule();
}

