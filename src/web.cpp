#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Update.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <time.h>

#include "settings.h"
#include "units.h"
#include "display.h"
#include "datetimesettings.h"
#include "tempest.h"
#include "weather_countries.h"
#include "sensors.h"
#include "ir_codes.h"

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
extern int tempOffset, humOffset, lightGain;
extern void saveAllSettings();
extern void loadSettings();
extern String str_Weather_Conditions, str_Temp, str_Humd;
extern char chr_t_hour[3], chr_t_minute[3], chr_t_second[3];

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
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed!");
    return;
  }

  // Serve index.html at root
  // Static files with dev-friendly caching
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  server.serveStatic("/config.html", SPIFFS, "/config.html").setCacheControl("no-cache");
  server.serveStatic("/style.css",   SPIFFS, "/style.css").setCacheControl("no-cache");
  server.serveStatic("/script.js",   SPIFFS, "/script.js").setCacheControl("no-cache");


  // Explicit route for config.html
  server.on("/config.html", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(SPIFFS, "/config.html", "text/html");
  });

  // ---------- JSON endpoints ----------
  server.on("/status.json", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    String dispTemp = fmtTemp(atof(str_Temp.c_str()), 0);
    doc["wifiSSID"] = WiFi.SSID();
    doc["ip"]       = WiFi.localIP().toString();
    doc["temp"]     = dispTemp;
    doc["tempUnit"] = (units.temp == TempUnit::F) ? "°F" : "°C";
    doc["humidity"] = str_Humd;
    doc["conditions"] = str_Weather_Conditions;
    doc["time"]     = String(chr_t_hour) + ":" + String(chr_t_minute) + ":" + String(chr_t_second);
    String json; serializeJson(doc, json);
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
    doc["brightness"]       = brightness;
    doc["autoBrightness"]   = autoBrightness;
    doc["scrollSpeed"]      = scrollSpeed;
    doc["scrollLevel"]      = scrollLevel;
    doc["splashDuration"]   = splashDurationSec;
    doc["customMsg"]        = customMsg;
    doc["owmCity"]          = owmCity;
    doc["owmCountryIndex"]  = owmCountryIndex;
    doc["owmCountryCustom"] = owmCountryCustom;
    doc["owmApiKey"]        = owmApiKey;
    doc["wfToken"]          = wfToken;
    doc["wfStationId"]      = wfStationId;
    doc["tempOffset"]       = tempOffset;
    doc["humOffset"]        = humOffset;
    doc["lightGain"]        = lightGain;
    doc["ntpServer"]        = ntpServerHost;
    doc["ntpPreset"]        = ntpServerPreset;
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
          tempOffset = v.is<int>() ? v.as<int>() : atoi(v.as<const char*>());
        }
        if (!doc["humOffset"].isNull()) {
          JsonVariant v = doc["humOffset"];
          humOffset = v.is<int>() ? v.as<int>() : atoi(v.as<const char*>());
        }
        if (!doc["lightGain"].isNull()) {
          JsonVariant v = doc["lightGain"];
          lightGain = v.is<int>() ? v.as<int>() : atoi(v.as<const char*>());
        }
        tempOffset = constrain(tempOffset, -10, 10);
        humOffset  = constrain(humOffset, -20, 20);
        lightGain  = constrain(lightGain, 1, 150);

        saveDateTimeSettings();
        saveAllSettings();
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
      host.trim();
      if (host.length() == 0)
      {
        host = "pool.ntp.org";
      }

      // Try to match a known preset; otherwise treat as custom
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
    String html = "<form method='POST' action='/update' enctype='multipart/form-data'>"
                  "<input type='file' name='firmware'><input type='submit' value='Upload'></form>";
    req->send(200, "text/html", html);
  });

  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest *req) {
      bool ok = !Update.hasError();
      req->send(200, "text/html", ok ?
        "<h2>Update Successful!</h2><a href='/'>Return to Settings</a><script>setTimeout(()=>location.href='/',2000);</script>"
        : "<h2>Update FAILED!</h2><a href='/ota'>Try Again</a>");
      delay(1000);
      if (ok) ESP.restart();
    },
    [](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (!index) { Serial.printf("OTA Update Start: %s\n", filename.c_str()); Update.begin(UPDATE_SIZE_UNKNOWN); }
      if (Update.write(data, len) != len) { Serial.println("OTA Write Fail!"); }
      if (final) {
        if (Update.end(true)) { Serial.println("OTA Update Success."); }
        else { Serial.println("OTA Update Error!"); }
      }
    }
  );

  // ---------- 404 ----------
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  });

  server.begin();
}





