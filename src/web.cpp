#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Update.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <time.h>
#include <stdlib.h>
#include "esp_ota_ops.h"

#include "settings.h"
#include "units.h"
#include "display.h"
#include "datetimesettings.h"
#include "tempest.h"
#include "system.h"
#include "datalogger.h"
#include "weather_countries.h"
#include "sensors.h"
#include "ir_codes.h"
#include "wifisettings.h"

// ---- externs ----
extern int dayFormat, dataSource, autoRotate, manualScreen, autoRotateInterval;
extern UnitPrefs units;
extern int theme, brightness, scrollSpeed, scrollLevel, splashDurationSec;
extern bool autoBrightness;
extern String customMsg;
extern String wifiSSID, wifiPass;
extern String owmCity, owmApiKey, wfToken, wfStationId;
extern int owmCountryIndex;
extern String owmCountryCustom;
extern String owmCountryCode;
extern float tempOffset;
extern int humOffset, lightGain;
extern int buzzerVolume, buzzerToneSet, alarmSoundMode;
extern void saveAllSettings();
extern void loadSettings();
extern String str_Weather_Conditions, str_Temp, str_Humd;
extern char chr_t_hour[3], chr_t_minute[3], chr_t_second[3];
extern String deviceHostname;
bool otaInProgress = false;
extern bool alarmEnabled[3];
extern int alarmHour[3];
extern int alarmMinute[3];
extern AlarmRepeatMode alarmRepeatMode[3];
extern int alarmWeeklyDay[3];
extern bool noaaAlertsEnabled;
extern float noaaLatitude;
extern float noaaLongitude;

// Date/Time bits
extern RTC_DS3231 rtc;
extern int tzOffset; // minutes
extern int dateFmt;  // 0/1/2
extern int fmt24;    // 0/1
extern void saveDateTimeSettings();
extern bool syncTimeFromNTP();
extern char ntpServerHost[64];

// from settings.h
extern const int scrollDelays[10];
extern bool reset_Time_and_Date_Display;

AsyncWebServer server(80);
static bool webServerRunning = false;

// --- UnitPrefs helpers ---
static void unitsToJson(JsonObject obj)
{
  obj["temp"] = static_cast<uint8_t>(units.temp);
  obj["wind"] = static_cast<uint8_t>(units.wind);
  obj["press"] = static_cast<uint8_t>(units.press);
  obj["precip"] = static_cast<uint8_t>(units.precip);
  obj["clock24h"] = units.clock24h;
}
static void applyLegacyUnitsInt(int legacy)
{
  if (legacy == 0)
  {
    units.temp = TempUnit::F;
    units.wind = WindUnit::MPH;
    units.press = PressUnit::INHG;
    units.precip = PrecipUnit::INCH;
  }
  else
  {
    units.temp = TempUnit::C;
    units.wind = WindUnit::MPS;
    units.press = PressUnit::HPA;
    units.precip = PrecipUnit::MM;
  }
}
static void jsonToUnits(JsonVariantConst v)
{
  if (v.is<int>())
  {
    applyLegacyUnitsInt(v.as<int>());
    return;
  }
  if (!v.is<JsonObjectConst>())
    return;
  JsonObjectConst obj = v.as<JsonObjectConst>();
  if (!obj["temp"].isNull())
    units.temp = static_cast<TempUnit>(obj["temp"].as<uint8_t>());
  if (!obj["wind"].isNull())
    units.wind = static_cast<WindUnit>(obj["wind"].as<uint8_t>());
  if (!obj["press"].isNull())
    units.press = static_cast<PressUnit>(obj["press"].as<uint8_t>());
  if (!obj["precip"].isNull())
    units.precip = static_cast<PrecipUnit>(obj["precip"].as<uint8_t>());
  if (!obj["clock24h"].isNull())
    units.clock24h = obj["clock24h"].as<bool>();
}

static String formatUptime(unsigned long seconds)
{
  unsigned long days = seconds / 86400UL;
  seconds %= 86400UL;
  unsigned int hours = seconds / 3600UL;
  seconds %= 3600UL;
  unsigned int minutes = seconds / 60UL;
  unsigned int secs = seconds % 60UL;

  char buffer[40];
  if (days > 0)
    snprintf(buffer, sizeof(buffer), "%lu d %02u:%02u:%02u", days, hours, minutes, secs);
  else
    snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u", hours, minutes, secs);
  return String(buffer);
}

static const char *dataSourceLabel(int value)
{
  switch (value)
  {
  case 0:
    return "Open Weather Map";
  case 1:
    return "WeatherFlow Tempest";
  case 2:
    return "Offline";
  default:
    return "Unknown";
  }
}

static const char *screenModeLabel(ScreenMode mode)
{
  switch (mode)
  {
  case SCREEN_CLOCK:
    return "Clock";
  case SCREEN_OWM:
    return "Forecast (OWM)";
  case SCREEN_UDP_DATA:
    return "UDP Live Weather";
  case SCREEN_UDP_FORECAST:
    return "UDP Forecast";
  case SCREEN_WIND_DIR:
    return "Wind Direction";
  case SCREEN_ENV_INDEX:
    return "Air Quality";
  case SCREEN_TEMP_HISTORY:
    return "Temp (24h)";
  case SCREEN_CO2_HISTORY:
    return "CO2 (24h)";
  case SCREEN_CONDITION_SCENE:
    return "Weather Scene";
  case SCREEN_CURRENT:
    return "Current Conditions";
  case SCREEN_HOURLY:
    return "Hourly Forecast";
  default:
    return "Info Screen";
  }
}

static uint32_t irCodeForButton(String btn)
{
  btn.trim();
  btn.toLowerCase();
  if (btn.length() == 0)
    return 0;

  if (btn == "up")
    return IR_UP;
  if (btn == "down")
    return IR_DOWN;
  if (btn == "left")
    return IR_LEFT;
  if (btn == "right")
    return IR_RIGHT;
  if (btn == "select" || btn == "enter" || btn == "ok")
    return IR_OK;
  if (btn == "menu" || btn == "setup" || btn == "cancel")
    return IR_MENU;
  if (btn == "screen" || btn == "shutdown" || btn == "power")
    return IR_SCREEN;
  if (btn == "theme" || btn == "color")
    return IR_THEME;

  // allow raw hex codes prefixed with 0x or without
  if (btn.startsWith("0x"))
    btn.remove(0, 2);
  if (btn.length() > 0)
  {
    char *endptr = nullptr;
    uint32_t code = strtoul(btn.c_str(), &endptr, 16);
    if (endptr && *endptr == '\0')
      return code;
  }

  return 0;
}

static const char *wifiAuthLabel(wifi_auth_mode_t mode)
{
  switch (mode)
  {
  case WIFI_AUTH_OPEN:
    return "Open";
  case WIFI_AUTH_WEP:
    return "WEP";
  case WIFI_AUTH_WPA_PSK:
    return "WPA";
  case WIFI_AUTH_WPA2_PSK:
    return "WPA2";
  case WIFI_AUTH_WPA_WPA2_PSK:
    return "WPA/WPA2";
  case WIFI_AUTH_WPA2_ENTERPRISE:
    return "WPA2-Enterprise";
  case WIFI_AUTH_WPA3_PSK:
    return "WPA3";
  case WIFI_AUTH_WPA2_WPA3_PSK:
    return "WPA2/WPA3";
  case WIFI_AUTH_WAPI_PSK:
    return "WAPI";
  default:
    return "Unknown";
  }
}

// Always use RTC so web clock matches on-device display
static long currentEpoch()
{
  DateTime utcNow;
  if (rtcReady)
  {
    utcNow = rtc.now();
  }
  else
  {
    DateTime localNow;
    if (!getLocalDateTime(localNow))
    {
      DateTime fallback(2000, 1, 1, 0, 0, 0);
      return (long)fallback.unixtime();
    }
    utcNow = localToUtc(localNow);
  }
  updateTimezoneOffsetWithUtc(utcNow);
  return (long)utcNow.unixtime();
}
void setupWebServer() {
  if (webServerRunning) {
    return;
  }
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed!");
    return;
  }

  // ---------- JSON endpoints ----------
  // Place dynamic endpoints before the catch-all static handler so they aren't shadowed.
  server.on("/trend.json", HTTP_GET, [](AsyncWebServerRequest *req) {
    const auto &log = getSensorLog();
    // Cap payload to keep client/server responsive
    constexpr size_t kMaxTrendSamples = 200;
    size_t sampleCount = (log.size() < kMaxTrendSamples) ? log.size() : kMaxTrendSamples;
    size_t capacity = JSON_ARRAY_SIZE(sampleCount) + sampleCount * JSON_OBJECT_SIZE(6) + 256;
    if (capacity < 1024) capacity = 1024;
    DynamicJsonDocument doc(capacity);
    sensorLogToJsonDownsample(doc, kMaxTrendSamples);

    AsyncResponseStream *res = req->beginResponseStream("application/json");
    serializeJson(doc, *res);
    req->send(res);
  });

  // Serve index.html at root
  // Static files with dev-friendly caching
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html").setCacheControl("max-age=86400, public");
  server.serveStatic("/config.html", SPIFFS, "/config.html").setCacheControl("no-cache");
  server.serveStatic("/style.css",   SPIFFS, "/style.css").setCacheControl("max-age=86400, public");
  server.serveStatic("/script.js",   SPIFFS, "/script.js").setCacheControl("max-age=86400, public");
  server.serveStatic("/sensor-log.json", SPIFFS, "/sensor_log.bin"); // fallback if needed


  // Explicit route for config.html
  server.on("/config.html", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(SPIFFS, "/config.html", "text/html");
  });
  server.on("/status.json", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
      String dispTemp;
    String humidityValue;
    String conditionsValue = str_Weather_Conditions;

    if (dataSource == 1) {
      dispTemp = fmtTemp(currentCond.temp, 0);
      humidityValue = (currentCond.humidity >= 0) ? String(currentCond.humidity) : "--";
      if (!currentCond.cond.isEmpty()) conditionsValue = currentCond.cond;
    } else {
      dispTemp = fmtTemp(atof(str_Temp.c_str()), 0);
      humidityValue = str_Humd;
    }

    doc["wifiSSID"] = WiFi.SSID();
    doc["wifiStatus"] = (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected";
    doc["hostname"] = deviceHostname;
    doc["ip"] = WiFi.localIP().toString();
    doc["mac"] = WiFi.macAddress();
    doc["rssi"] = WiFi.RSSI();
    float luxNow = readBrightnessSensor();
    doc["lux"] = luxNow;

    unsigned long uptimeSec = millis() / 1000UL;
    doc["uptimeSec"] = uptimeSec;
    doc["uptime"] = formatUptime(uptimeSec);

    size_t heapTotal = 327680; // align with System Info modal
    size_t heapFree = ESP.getFreeHeap();
    if (heapFree > heapTotal) {
      heapTotal = heapFree;
    }
    size_t heapUsed = (heapTotal > heapFree) ? (heapTotal - heapFree) : 0;
    doc["freeHeap"] = heapFree; // legacy field
    doc["heapTotal"] = heapTotal;
    doc["heapFree"] = heapFree;
    doc["heapUsed"] = heapUsed;
    doc["heapUsedPercent"] = (heapTotal > 0) ? static_cast<uint8_t>((heapUsed * 100 + heapTotal / 2) / heapTotal) : 0;

    size_t fsTotal = SPIFFS.totalBytes();
    size_t fsUsed = SPIFFS.usedBytes();
    size_t fsFree = (fsTotal > fsUsed) ? (fsTotal - fsUsed) : 0;
    doc["fsTotal"] = fsTotal;
    doc["fsUsed"] = fsUsed;
    doc["fsFree"] = fsFree;
    doc["fsUsedPercent"] = (fsTotal > 0) ? static_cast<uint8_t>((fsUsed * 100 + fsTotal / 2) / fsTotal) : 0;

    size_t sketchSize = ESP.getSketchSize();
    const esp_partition_t *running = esp_ota_get_running_partition();
    size_t flashTotal = running ? running->size : 0;
    size_t flashFree = (flashTotal > sketchSize) ? (flashTotal - sketchSize) : 0;
    if (flashTotal == 0) {
      size_t sketchFree = ESP.getFreeSketchSpace();
      flashTotal = sketchSize + sketchFree;
      flashFree = (flashTotal > sketchSize) ? (flashTotal - sketchSize) : 0;
    }
    doc["flashTotal"] = flashTotal;
    doc["flashUsed"] = sketchSize;
    doc["flashFree"] = flashFree;
    doc["flashUsedPercent"] = (flashTotal > 0) ? static_cast<uint8_t>((sketchSize * 100 + flashTotal / 2) / flashTotal) : 0;

    doc["dataSource"] = dataSource;
    doc["dataSourceLabel"] = dataSourceLabel(dataSource);
    bool screenOff = isScreenOff();
    doc["screenOff"] = screenOff;
    doc["screen"] = static_cast<uint8_t>(currentScreen);
    doc["screenLabel"] = screenOff ? "Screen Off" : screenModeLabel(currentScreen);

    doc["temp"] = dispTemp;
    doc["humidity"] = (dataSource == 1 && currentCond.humidity >= 0) ? String(currentCond.humidity) : str_Humd;
    doc["conditions"] = (dataSource == 1 && currentCond.cond.length() > 0) ? currentCond.cond : str_Weather_Conditions;
    doc["time"] = String(chr_t_hour) + ":" + String(chr_t_minute) + ":" + String(chr_t_second);

    if (!isnan(SCD40_temp)) {
      float indoorCal = SCD40_temp + tempOffset;
      doc["indoorTemp"] = fmtTemp(indoorCal, 1);
      doc["indoorTempRaw"] = indoorCal;
      doc["indoorTempSensor"] = SCD40_temp;
    }
    if (!isnan(SCD40_hum)) {
      float indoorHumCal = SCD40_hum + static_cast<float>(humOffset);
      if (indoorHumCal < 0.0f) indoorHumCal = 0.0f;
      if (indoorHumCal > 100.0f) indoorHumCal = 100.0f;
      doc["indoorHumidity"] = String(static_cast<int>(indoorHumCal + 0.5f)) + "%";
      doc["indoorHumidityRaw"] = indoorHumCal;
      doc["indoorHumiditySensor"] = SCD40_hum;
    }
    if (SCD40_co2 > 0) {
      doc["co2"] = SCD40_co2;
    }

    if (!isnan(aht20_temp)) {
      float ahtCal = aht20_temp + tempOffset;
      doc["ahtTemp"] = fmtTemp(ahtCal, 1);
      doc["ahtTempRaw"] = ahtCal;
      doc["ahtTempSensor"] = aht20_temp;
    }
    if (!isnan(aht20_hum)) {
      float ahtHumCal = aht20_hum + static_cast<float>(humOffset);
      if (ahtHumCal < 0.0f) ahtHumCal = 0.0f;
      if (ahtHumCal > 100.0f) ahtHumCal = 100.0f;
      doc["ahtHumidity"] = String(static_cast<int>(ahtHumCal + 0.5f)) + "%";
      doc["ahtHumidityRaw"] = ahtHumCal;
      doc["ahtHumiditySensor"] = aht20_hum;
    }
    if (!isnan(bmp280_pressure)) {
      doc["pressure"] = fmtPress(bmp280_pressure, 1);
      doc["pressureRaw"] = bmp280_pressure;
    }

    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.on("/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
    bool includeHidden = false;
    if (req->hasParam("hidden"))
    {
      String hiddenParam = req->getParam("hidden")->value();
      includeHidden = hiddenParam.toInt() != 0;
    }

    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    bool apActive = isAccessPointActive();
    wifi_mode_t prevMode = WiFi.getMode();
    bool restoreMode = false;

    if (prevMode == WIFI_OFF)
    {
      WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
      restoreMode = true;
    }
    else if (prevMode == WIFI_AP && !wifiConnected)
    {
      WiFi.mode(WIFI_AP_STA);
      restoreMode = true;
    }

    if (!wifiConnected)
    {
      WiFi.disconnect(false, false);
    }

    WiFi.scanDelete();
    delay(120);
    int found = WiFi.scanNetworks(false, includeHidden);
    if (found < 0)
      found = 0;

    JsonDocument doc;
    JsonArray arr = doc.createNestedArray("networks");
    const int MAX_NETWORKS = 25;
    int emitted = 0;
    for (int i = 0; i < found && emitted < MAX_NETWORKS; ++i)
    {
      String ssid = WiFi.SSID(i);
      ssid.trim();
      if (ssid.isEmpty())
        continue;

      JsonObject net = arr.add<JsonObject>();
      net["ssid"] = ssid;
      net["bssid"] = WiFi.BSSIDstr(i);
      net["channel"] = WiFi.channel(i);
      net["rssi"] = WiFi.RSSI(i);
      wifi_auth_mode_t auth = WiFi.encryptionType(i);
      net["auth"] = static_cast<uint8_t>(auth);
      net["security"] = wifiAuthLabel(auth);
      net["secure"] = (auth != WIFI_AUTH_OPEN);
      emitted++;
    }
    doc["count"] = emitted;
    doc["connected"] = wifiConnected;
    if (wifiConnected)
    {
      doc["connectedSSID"] = WiFi.SSID();
      doc["ip"] = WiFi.localIP().toString();
    }
    doc["apActive"] = apActive;
    if (apActive)
    {
      doc["apSSID"] = getAccessPointSSID();
      doc["apIP"] = getAccessPointIP().toString();
    }
    doc["timestamp"] = millis();

    WiFi.scanDelete();
    if (restoreMode)
    {
      WiFi.mode(prevMode);
    }

    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.on("/ir", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("btn"))
    {
      JsonDocument doc;
      doc["error"] = "missing btn";
      String json;
      serializeJson(doc, json);
      req->send(400, "application/json", json);
      return;
    }

    String btn = req->getParam("btn")->value();
    uint32_t code = irCodeForButton(btn);
    if (code == 0)
    {
      JsonDocument doc;
      doc["error"] = "unknown button";
      doc["btn"] = btn;
      String json;
      serializeJson(doc, json);
      req->send(400, "application/json", json);
      return;
    }

    if (!enqueueVirtualIRCode(code))
    {
      JsonDocument doc;
      doc["error"] = "busy";
      String json;
      serializeJson(doc, json);
      req->send(503, "application/json", json);
      return;
    }

    JsonDocument doc;
    doc["status"] = "queued";
    doc["btn"] = btn;
    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.on("/screen", HTTP_GET, [](AsyncWebServerRequest *req) {
    bool requestedOff = false;
    bool requestedOn = false;
    bool toggled = false;

    if (req->hasParam("state"))
    {
      String state = req->getParam("state")->value();
      state.trim();
      state.toLowerCase();
      if (state == "off" || state == "0" || state == "false")
      {
        requestedOff = true;
      }
      else if (state == "on" || state == "1" || state == "true")
      {
        requestedOn = true;
      }
    }
    else if (req->hasParam("toggle"))
    {
      toggled = true;
    }

    bool before = isScreenOff();
    if (requestedOn)
    {
      setScreenOff(false);
    }
    else if (requestedOff)
    {
      setScreenOff(true);
    }
    else if (toggled)
    {
      toggleScreenPower();
    }

    bool after = isScreenOff();

    JsonDocument doc;
    doc["screenOff"] = after;
    doc["state"] = after ? "off" : "on";
    doc["changed"] = (before != after);
    doc["brightness"] = currentPanelBrightness;

    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.on("/settings.json", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["wifiSSID"]         = wifiSSID;
    doc["wifiPass"]         = wifiPass;
    unitsToJson(doc.createNestedObject("units"));
    doc["dayFormat"]        = dayFormat;
    doc["dataSource"]       = dataSource;
    doc["forecastSrc"]      = dataSource; // legacy field for older clients
    doc["autoRotate"]       = autoRotate;
    doc["autoRotateInterval"] = autoRotateInterval;
    doc["manualScreen"]     = manualScreen;
    doc["theme"]            = theme;
    bool isAmbientMode = autoThemeAmbient;
    bool isScheduledMode = (!isAmbientMode && autoThemeSchedule);
    doc["autoThemeSchedule"]= isScheduledMode; // legacy field
    doc["autoThemeMode"]    = isAmbientMode ? 2 : (isScheduledMode ? 1 : 0);
    doc["dayThemeStart"]    = dayThemeStartMinutes;
    doc["nightThemeStart"]  = nightThemeStartMinutes;
    doc["themeLightThreshold"] = autoThemeLightThreshold;
    doc["brightness"]       = brightness;
    doc["autoBrightness"]   = autoBrightness;
    doc["scrollSpeed"]      = scrollSpeed;
    doc["scrollLevel"]      = scrollLevel;
    doc["splashDuration"]   = splashDurationSec;
    doc["customMsg"]        = customMsg;
    doc["buzzerVolume"]     = buzzerVolume;
  doc["buzzerTone"]       = buzzerToneSet;
    doc["alarmSound"]       = alarmSoundMode;
    // Live sensor snapshot
    float luxNow = readBrightnessSensor();
    doc["currentLux"] = luxNow;
    doc["lux"] = luxNow; // alias for status parity
    doc["owmCity"]          = owmCity;
    doc["owmCountryIndex"]  = owmCountryIndex;
    doc["owmCountryCustom"] = owmCountryCustom;
    doc["owmApiKey"]        = owmApiKey;
    doc["wfToken"]          = wfToken;
    doc["wfStationId"]      = wfStationId;
    doc["tempOffset"]       = dispTempOffset(tempOffset);
    doc["humOffset"]        = humOffset;
    doc["lightGain"]        = lightGain;
    doc["ntpServer"]        = ntpServerHost;
    doc["ntpPreset"]        = ntpServerPreset;
    JsonArray alarms = doc.createNestedArray("alarms");
    for (int i = 0; i < 3; ++i)
    {
      JsonObject a = alarms.add<JsonObject>();
      a["enabled"] = alarmEnabled[i];
      a["hour"] = alarmHour[i];
      a["minute"] = alarmMinute[i];
      a["repeat"] = static_cast<uint8_t>(alarmRepeatMode[i]);
      a["weekDay"] = alarmWeeklyDay[i];
    }
    JsonObject noaa = doc.createNestedObject("noaa");
    noaa["enabled"] = noaaAlertsEnabled;
    noaa["lat"] = noaaLatitude;
    noaa["lon"] = noaaLongitude;
    String json; serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  // ---------- POST /settings (chunk-safe) ----------
  server.on("/settings", HTTP_POST,
    [](AsyncWebServerRequest *req) {},   // onRequest
    nullptr,                              // onUpload
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        req->_tempObject = new String();
        ((String*)req->_tempObject)->reserve(total);
      }
      String* body = (String*)req->_tempObject;
      body->concat((const char*)data, len);

      if (index + len == total) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, *body);
        delete body; req->_tempObject = nullptr;

        if (err) {
          Serial.printf("[/settings] JSON parse error: %s\n", err.c_str());
          req->send(400, "text/plain", "Invalid JSON");
          return;
        }

        // Device
        if (!doc["wifiSSID"].isNull()) {
          wifiSSID = doc["wifiSSID"] | wifiSSID;
        }
        if (!doc["wifiPass"].isNull()) {
          wifiPass = doc["wifiPass"] | wifiPass;
        }
        if (!doc["units"].isNull()) jsonToUnits(doc["units"]);
        fmt24 = units.clock24h ? 1 : 0;   // keep device clock format in sync with Unit card
        if (!doc["dayFormat"].isNull()) {
          dayFormat = doc["dayFormat"] | dayFormat;
        }

        int newSource = dataSource;
        if (!doc["dataSource"].isNull()) {
          newSource = doc["dataSource"].as<int>();
        } else if (!doc["forecastSrc"].isNull()) {
          newSource = doc["forecastSrc"].as<int>();
        }
        setDataSource(newSource);
        if (!doc["autoRotate"].isNull()) {
          bool autoRotateValue = (doc["autoRotate"] | autoRotate) != 0;
          setAutoRotateEnabled(autoRotateValue, false);
        }
        if (!doc["autoRotateInterval"].isNull()) {
          int newInterval = constrain((int)(doc["autoRotateInterval"] | autoRotateInterval), 5, 300);
          setAutoRotateInterval(newInterval, false);
        }
        if (!doc["manualScreen"].isNull()) {
          manualScreen = doc["manualScreen"] | manualScreen;
        }

        // Display
        if (!doc["theme"].isNull()) {
          theme = doc["theme"] | theme;
        }
        int incomingAutoThemeMode = -1;
        if (!doc["autoThemeMode"].isNull()) {
          incomingAutoThemeMode = doc["autoThemeMode"].as<int>();
          if (incomingAutoThemeMode < 0) incomingAutoThemeMode = 0;
          if (incomingAutoThemeMode > 2) incomingAutoThemeMode = 2;
          autoThemeSchedule = (incomingAutoThemeMode == 1);
          autoThemeAmbient = (incomingAutoThemeMode == 2);
        }
        if (!doc["autoThemeSchedule"].isNull()) {
          JsonVariant v = doc["autoThemeSchedule"];
          bool value = false;
          if (v.is<bool>()) value = v.as<bool>();
          else if (v.is<int>()) value = (v.as<int>() != 0);
          else if (v.is<const char*>()) {
            const char *s = v.as<const char*>();
            value = (strcmp(s, "1")==0 || strcasecmp(s, "true")==0 || strcasecmp(s, "on")==0);
          }
          if (incomingAutoThemeMode < 0) {
            autoThemeSchedule = value;
            autoThemeAmbient = false;
          }
        }
        // Enforce mutually-exclusive modes
        if (autoThemeAmbient) {
          autoThemeSchedule = false;
        }
        if (!doc["dayThemeStart"].isNull()) {
          dayThemeStartMinutes = normalizeThemeScheduleMinutes(doc["dayThemeStart"].as<int>());
        }
        if (!doc["nightThemeStart"].isNull()) {
          nightThemeStartMinutes = normalizeThemeScheduleMinutes(doc["nightThemeStart"].as<int>());
        }
        if (!doc["themeLightThreshold"].isNull()) {
          int thr = doc["themeLightThreshold"].as<int>();
          autoThemeLightThreshold = constrain(thr, 1, 5000);
        }
        if (!doc["brightness"].isNull()) {
          brightness = constrain((int)(doc["brightness"] | brightness), 1, 100);
        }
        if (!doc["autoBrightness"].isNull()) {
          JsonVariant v = doc["autoBrightness"];
          if (v.is<bool>()) autoBrightness = v.as<bool>();
          else if (v.is<int>()) autoBrightness = (v.as<int>() != 0);
          else if (v.is<const char*>()) {
            const char* s = v.as<const char*>();
            autoBrightness = (strcmp(s, "1")==0 || strcasecmp(s, "true")==0);
          }
        }
        if (!doc["splashDuration"].isNull()) {
          int dur = doc["splashDuration"].as<int>();
          splashDurationSec = constrain(dur, 1, 10);
        }
        if (!doc["buzzerVolume"].isNull()) {
          buzzerVolume = constrain(doc["buzzerVolume"].as<int>(), 0, 100);
        }
        if (!doc["buzzerTone"].isNull()) {
          buzzerToneSet = constrain(doc["buzzerTone"].as<int>(), 0, 6);
        }
        if (!doc["alarmSound"].isNull()) {
          alarmSoundMode = constrain(doc["alarmSound"].as<int>(), 0, 4);
        }
        if (!doc["scrollLevel"].isNull()) {
          scrollLevel = constrain((int)(doc["scrollLevel"] | scrollLevel), 0, 9);
          scrollSpeed = scrollDelays[scrollLevel];
        }
        if (!doc["customMsg"].isNull()) {
          customMsg = doc["customMsg"] | customMsg;
        }

        if (!doc["ntpPreset"].isNull())
        {
          int preset = doc["ntpPreset"].as<int>();
          if (preset < 0) preset = 0;
          if (preset > NTP_PRESET_CUSTOM) preset = NTP_PRESET_CUSTOM;
          ntpServerPreset = preset;
        }

        if (!doc["ntpServer"].isNull())
        {
          String host = doc["ntpServer"].as<String>();
          host.trim();
          if (host.length() == 0)
          {
            host = "pool.ntp.org";
          }

          bool matched = false;
          for (int i = 0; i < NTP_PRESET_COUNT; ++i)
          {
            if (host.equalsIgnoreCase(ntpPresetHost(i)))
            {
              ntpServerPreset = i;
              matched = true;
              break;
            }
          }
          if (!matched)
          {
            ntpServerPreset = NTP_PRESET_CUSTOM;
            host.toCharArray(ntpServerHost, sizeof(ntpServerHost));
          }
          else
          {
            strncpy(ntpServerHost, ntpPresetHost(ntpServerPreset), sizeof(ntpServerHost) - 1);
            ntpServerHost[sizeof(ntpServerHost) - 1] = '\0';
          }
        }

        // Weather
        bool owmSettingsChanged = false;
        if (!doc["owmCity"].isNull()) {
          String updated = doc["owmCity"].as<String>();
          updated.trim();
          if (!updated.equals(owmCity)) {
            owmSettingsChanged = true;
          }
          owmCity = updated;
        }
        if (!doc["owmCountryIndex"].isNull()) {
          int updated = doc["owmCountryIndex"].as<int>();
          if (updated != owmCountryIndex) {
            owmSettingsChanged = true;
          }
          owmCountryIndex = updated;
        }
        if (!doc["owmCountryCustom"].isNull()) {
          String updated = doc["owmCountryCustom"].as<String>();
          updated.trim();
          if (!updated.equals(owmCountryCustom)) {
            owmSettingsChanged = true;
          }
          owmCountryCustom = updated;
        }
        if (!doc["owmApiKey"].isNull()) {
          String updated = doc["owmApiKey"].as<String>();
          updated.trim();
          if (!updated.equals(owmApiKey)) {
            owmSettingsChanged = true;
          }
          owmApiKey = updated;
        }
        bool wfCredsChanged = false;
        if (!doc["wfToken"].isNull()) {
          String prev = wfToken;
          prev.trim();
          String updated = doc["wfToken"].as<String>();
          updated.trim();
          if (!updated.equals(prev)) {
            wfCredsChanged = true;
          }
          wfToken = updated;
        }
        if (!doc["wfStationId"].isNull()) {
          String prev = wfStationId;
          prev.trim();
          String updated = doc["wfStationId"].as<String>();
          updated.trim();
          if (!updated.equals(prev)) {
            wfCredsChanged = true;
          }
          wfStationId = updated;
        }

        // Calibration (robust)
        if (!doc["tempOffset"].isNull()) {
          JsonVariant v = doc["tempOffset"];
          double incoming = 0.0;
          if (v.is<double>() || v.is<float>()) {
            incoming = v.as<double>();
          } else if (v.is<int>() || v.is<long>() || v.is<long long>()) {
            incoming = static_cast<double>(v.as<long long>());
          } else if (v.is<const char*>()) {
            incoming = atof(v.as<const char*>());
          }
          float offsetC = static_cast<float>(tempOffsetToC(incoming));
          tempOffset = constrain(offsetC, -10.0f, 10.0f);
        }
        if (!doc["humOffset"].isNull()) {
          JsonVariant v = doc["humOffset"];
          humOffset = v.is<int>() ? v.as<int>() : atoi(v.as<const char*>());
        }
        if (!doc["lightGain"].isNull()) {
          JsonVariant v = doc["lightGain"];
          lightGain = v.is<int>() ? v.as<int>() : atoi(v.as<const char*>());
        }
        tempOffset = constrain(tempOffset, -10.0f, 10.0f);
        humOffset  = constrain(humOffset, -20, 20);
        lightGain  = constrain(lightGain, LIGHT_GAIN_MIN, LIGHT_GAIN_MAX);

        // Alarms
        if (!doc["alarms"].isNull() && doc["alarms"].is<JsonArray>())
        {
          JsonArray arr = doc["alarms"].as<JsonArray>();
          int idx = 0;
          for (JsonObject a : arr)
          {
            if (idx >= 3) break;
            if (!a["enabled"].isNull()) alarmEnabled[idx] = a["enabled"].as<bool>();
            if (!a["hour"].isNull()) alarmHour[idx] = constrain(a["hour"].as<int>(), 0, 23);
            if (!a["minute"].isNull()) alarmMinute[idx] = constrain(a["minute"].as<int>(), 0, 59);
            if (!a["repeat"].isNull())
            {
              int r = a["repeat"].as<int>();
              if (r < ALARM_REPEAT_NONE) r = ALARM_REPEAT_NONE;
              if (r > ALARM_REPEAT_WEEKEND) r = ALARM_REPEAT_NONE;
              alarmRepeatMode[idx] = static_cast<AlarmRepeatMode>(r);
            }
            if (!a["weekDay"].isNull()) alarmWeeklyDay[idx] = constrain(a["weekDay"].as<int>(), 0, 6);
            idx++;
          }
        }

        // NOAA
        if (!doc["noaa"].isNull() && doc["noaa"].is<JsonObject>())
        {
          JsonObject noaa = doc["noaa"].as<JsonObject>();
          if (!noaa["enabled"].isNull()) noaaAlertsEnabled = noaa["enabled"].as<bool>();
          if (!noaa["lat"].isNull()) noaaLatitude = noaa["lat"].as<float>();
          if (!noaa["lon"].isNull()) noaaLongitude = noaa["lon"].as<float>();
        }

        saveDateTimeSettings();
        saveAllSettings();
        forceAutoThemeSchedule();
        if (autoThemeAmbient)
        {
          float lux = readBrightnessSensor();
          tickAutoThemeAmbient(lux);
        }
        Serial.println("[/settings] Saved OK");
        req->send(200, "application/json", "{\"ok\":true}");

        if (owmCountryIndex >= 0 && owmCountryIndex < (countryCount - 1)) {
          owmCountryCode = countryCodes[owmCountryIndex];
        } else {
          owmCountryCode = owmCountryCustom;
        }

        if (owmSettingsChanged) {
          if (WiFi.status() == WL_CONNECTED) {
            fetchWeatherFromOWM();
            requestScrollRebuild();
            serviceScrollRebuild();
            displayWeatherData();
          }
          reset_Time_and_Date_Display = true;
        }

        if (wfCredsChanged && isDataSourceWeatherFlow()) {
          fetchForecastData();
        }
      }
    }
  );

  // ---------- Timezones / Time ----------
  server.on("/timezones.json", HTTP_GET, [](AsyncWebServerRequest* req){
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    size_t count = timezoneCount();
    for (size_t i = 0; i < count; ++i) {
      const TimezoneInfo& info = timezoneInfoAt(i);
      JsonObject o = arr.add<JsonObject>();
      o["id"]        = info.id;
      o["city"]      = info.city;
      o["country"]   = info.country;
      o["offset"]    = info.offsetMinutes;
      o["label"]     = timezoneLabelAt(i);
      o["supportsDst"] = timezoneSupportsDst(i);
    }
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/time.json", HTTP_GET, [](AsyncWebServerRequest* req){
    JsonDocument doc;
    doc["epoch"]     = (long)currentEpoch();  // <- RTC
    doc["tzOffset"]  = tzOffset;
    doc["tzStdOffset"] = tzStandardOffset;
    doc["tzName"]    = timezoneIsCustom() ? "" : currentTimezoneId();
    doc["tzAutoDst"] = tzAutoDst;
    int currentIdx = timezoneCurrentIndex();
    bool hasSelection = currentIdx >= 0;
    if (hasSelection)
    {
      doc["tzLabel"] = timezoneLabelAt(static_cast<size_t>(currentIdx));
      doc["tzSupportsDst"] = timezoneSupportsDst(static_cast<size_t>(currentIdx));
    }
    else
    {
      doc["tzLabel"] = "Custom Offset";
      doc["tzSupportsDst"] = false;
    }
    doc["dateFmt"]   = dateFmt;
    doc["ntpServer"] = ntpServerHost;
    doc["ntpPreset"] = ntpServerPreset;
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/time", HTTP_POST,
    [](AsyncWebServerRequest*){}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      if (index == 0) {
        req->_tempObject = new String();
        ((String*)req->_tempObject)->reserve(total);
      }
      String* body = (String*)req->_tempObject;
      body->concat((const char*)data, len);

      if (index + len == total) {
        JsonDocument doc;
        if (deserializeJson(doc, *body)) {
          delete body; req->_tempObject = nullptr;
          req->send(400, "text/plain", "bad json");
          return;
        }
        delete body; req->_tempObject = nullptr;

        bool timezoneUpdated = false;
        if (!doc["tzName"].isNull()) {
          String tz = doc["tzName"].as<String>();
          tz.trim();
          if (tz.length() > 0) {
            int idx = timezoneIndexFromId(tz.c_str());
            if (idx < 0) {
              for (size_t i = 0; i < timezoneCount(); ++i) {
                const TimezoneInfo& info = timezoneInfoAt(i);
                if (tz.equalsIgnoreCase(info.city)) {
                  idx = static_cast<int>(i);
                  break;
                }
              }
            }
            if (idx >= 0) {
              selectTimezoneByIndex(idx);
              timezoneUpdated = true;
            }
          }
        }
        if (!timezoneUpdated && !doc["tzOffset"].isNull()) {
          int offset = doc["tzOffset"].as<int>();
          setCustomTimezoneOffset(offset);
          timezoneUpdated = true;
        }

        if (!doc["tzAutoDst"].isNull())
        {
          setTimezoneAutoDst(doc["tzAutoDst"].as<bool>());
        }
        else if (!doc["autoDst"].isNull())
        {
          setTimezoneAutoDst(doc["autoDst"].as<bool>());
        }

        if (!doc["dateFmt"].isNull()) dateFmt = (int)doc["dateFmt"].as<int>();

        if (!doc["ntpPreset"].isNull())
        {
          int preset = doc["ntpPreset"].as<int>();
          if (preset < 0) preset = 0;
          if (preset > NTP_PRESET_CUSTOM) preset = NTP_PRESET_CUSTOM;
          ntpServerPreset = preset;
        }

        if (!doc["ntpServer"].isNull())
        {
          String host = doc["ntpServer"].as<String>();
          host.trim();
          if (host.length() == 0)
          {
            host = "pool.ntp.org";
          }

          bool matched = false;
          for (int i = 0; i < NTP_PRESET_COUNT; ++i)
          {
            if (host.equalsIgnoreCase(ntpPresetHost(i)))
            {
              ntpServerPreset = i;
              matched = true;
              break;
            }
          }
          if (!matched)
          {
            ntpServerPreset = NTP_PRESET_CUSTOM;
            host.toCharArray(ntpServerHost, sizeof(ntpServerHost));
          }
          else
          {
            strncpy(ntpServerHost, ntpPresetHost(ntpServerPreset), sizeof(ntpServerHost) - 1);
            ntpServerHost[sizeof(ntpServerHost) - 1] = '\0';
          }
        }

        if (!doc["epoch"].isNull()) { // write RTC
          time_t t = (time_t)doc["epoch"].as<long>();
          rtc.adjust(DateTime(t));
        }

        saveDateTimeSettings();
        req->send(200, "application/json", "{\"ok\":true}");
      }
    }
  );

  // ---------- NTP sync ----------
  server.on("/syncntp", HTTP_GET, [](AsyncWebServerRequest* req){
    // Optional override via query param (?server=host)
    if (req->hasParam("server"))
    {
      String host = req->getParam("server")->value();
      setNtpServerFromHostString(host);
      // Persist the latest choice so it survives reboot
      saveDateTimeSettings();
    }

    bool ok = syncTimeFromNTP();
    req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  // ---------- Status / OTA / Reboot ----------
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    String tempDisp = fmtTemp(atof(str_Temp.c_str()), 0);
    String status = "<html><body><h2>Status</h2>";
    status += "<p>WiFi: " + String(WiFi.SSID()) + "</p>";
    status += "<p>Weather: " + str_Weather_Conditions + " " + tempDisp + "</p>";
    status += "<p>Humidity: " + str_Humd + "%</p>";
    status += "<p>Time: " + String(chr_t_hour) + ":" + String(chr_t_minute) + ":" + String(chr_t_second) + "</p>";
    status += "<p><a href='/ota'>Start OTA</a> | <a href='/reboot'>Reboot</a> | <a href='/'>Settings</a></p></body></html>";
    req->send(200, "text/html", status);
  });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/plain", "Rebooting...");
    delay(1000);
    ESP.restart();
  });

  server.on("/ota", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(SPIFFS, "/ota.html", "text/html");
  });

  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest *req) {
      bool ok = !Update.hasError();
      otaInProgress = false;
      if (ok) {
        // OTA page handles success messaging; keep response minimal
        req->send(200, "text/plain", "OK");
      } else {
        req->send(200, "text/html",
                  "<h2>Update FAILED!</h2><p>Please check the firmware file and try again.</p>"
                  "<a href='/ota'>Back to OTA page</a>");
      }
    },
    [](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (!index) {
        Serial.printf("OTA Update Start: %s\n", filename.c_str());
        Update.begin(UPDATE_SIZE_UNKNOWN);
        otaInProgress = true;
        // Show upgrade message on display
        if (dma_display) {
          dma_display->fillScreen(0);
          dma_display->setTextColor(dma_display->color565(0, 255, 255));
          dma_display->setCursor(2, 8);
          dma_display->print("Upgrading...");
        }
      }
      if (Update.write(data, len) != len) { Serial.println("OTA Write Fail!"); }
      if (final) {
        if (Update.end(true)) { Serial.println("OTA Update Success."); }
        else { Serial.println("OTA Update Error!"); }
        otaInProgress = false;
      }
    }
  );

  // ---------- 404 ----------
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  });

  server.begin();
  webServerRunning = true;
  Serial.println("[Web] Async server started.");
}





















