#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Update.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <time.h>

#include "settings.h"
#include "units.h"

// ---- externs ----
extern int dayFormat, forecastSrc, autoRotate, manualScreen;
extern UnitPrefs units;
extern int theme, brightness, scrollSpeed, scrollLevel;
extern bool autoBrightness;
extern String customMsg;
extern String wifiSSID, wifiPass;
extern String owmCity, owmApiKey, wfToken, wfStationId;
extern int owmCountryIndex;
extern String owmCountryCustom;
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
extern void syncTimeFromNTP();

// from settings.h
extern const int scrollDelays[10];

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
  if (obj.containsKey("temp"))
    units.temp = static_cast<TempUnit>(obj["temp"].as<uint8_t>());
  if (obj.containsKey("wind"))
    units.wind = static_cast<WindUnit>(obj["wind"].as<uint8_t>());
  if (obj.containsKey("press"))
    units.press = static_cast<PressUnit>(obj["press"].as<uint8_t>());
  if (obj.containsKey("precip"))
    units.precip = static_cast<PrecipUnit>(obj["precip"].as<uint8_t>());
  if (obj.containsKey("clock24h"))
    units.clock24h = obj["clock24h"].as<bool>();
}

// Always use RTC so web clock matches on-device display
static long currentEpoch()
{
  DateTime now = rtc.now();
  return (long)now.unixtime();
}

// --- Simple timezone list ---
struct TzItem
{
  const char *id;
  const char *city;
  int offsetMin;
};
static const TzItem kTimezones[] PROGMEM = {
    {"Pacific/Honolulu", "Honolulu", -600}, {"America/Anchorage", "Anchorage", -540}, {"America/Los_Angeles", "Los Angeles", -480}, {"America/Denver", "Denver", -420}, {"America/Chicago", "Chicago", -360}, {"America/New_York", "New York", -300}, {"America/Halifax", "Halifax", -240}, {"America/St_Johns", "St. John's", -210}, {"America/Sao_Paulo", "São Paulo", -180}, {"Atlantic/Azores", "Azores", -60}, {"UTC", "UTC", 0}, {"Europe/London", "London", 0}, {"Europe/Berlin", "Berlin", 60}, {"Europe/Athens", "Athens", 120}, {"Europe/Moscow", "Moscow", 180}, {"Asia/Dubai", "Dubai", 240}, {"Asia/Karachi", "Karachi", 300}, {"Asia/Kolkata", "Mumbai/Delhi", 330}, {"Asia/Dhaka", "Dhaka", 360}, {"Asia/Bangkok", "Bangkok", 420}, {"Asia/Hong_Kong", "Hong Kong", 480}, {"Asia/Tokyo", "Tokyo", 540}, {"Australia/Adelaide", "Adelaide", 570}, {"Australia/Sydney", "Sydney", 600}, {"Pacific/Noumea", "Nouméa", 660}, {"Pacific/Auckland", "Auckland", 720}};
static const size_t kTimezoneCount = sizeof(kTimezones) / sizeof(kTimezones[0]);

static String fmtUtcLabel(int offsetMin)
{
  char buf[16];
  int m = abs(offsetMin), h = m / 60, mi = m % 60;
  snprintf(buf, sizeof(buf), "UTC%s%02d:%02d", (offsetMin >= 0 ? "+" : "-"), h, mi);
  return String(buf);
}
static const char *tzNameFromOffset(int offsetMin)
{
  for (size_t i = 0; i < kTimezoneCount; i++)
    if (kTimezones[i].offsetMin == offsetMin)
      return kTimezones[i].id;
  return "Custom";
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
    doc["wifiSSID"] = WiFi.SSID();
    doc["ip"]       = WiFi.localIP().toString();
    doc["temp"]     = str_Temp;
    doc["humidity"] = str_Humd;
    doc["conditions"] = str_Weather_Conditions;
    doc["time"]     = String(chr_t_hour) + ":" + String(chr_t_minute) + ":" + String(chr_t_second);
    String json; serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.on("/settings.json", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["wifiSSID"]         = wifiSSID;
    doc["wifiPass"]         = wifiPass;
    unitsToJson(doc.createNestedObject("units"));
    doc["dayFormat"]        = dayFormat;
    doc["forecastSrc"]      = forecastSrc;
    doc["autoRotate"]       = autoRotate;
    doc["manualScreen"]     = manualScreen;
    doc["theme"]            = theme;
    doc["brightness"]       = brightness;
    doc["autoBrightness"]   = autoBrightness;
    doc["scrollSpeed"]      = scrollSpeed;
    doc["scrollLevel"]      = scrollLevel;
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
        wifiSSID        = doc["wifiSSID"]         | "";
        wifiPass        = doc["wifiPass"]         | "";
        if (doc.containsKey("units")) jsonToUnits(doc["units"]);
        fmt24 = units.clock24h ? 1 : 0;   // keep device clock format in sync with Unit card
        saveDateTimeSettings();
        dayFormat       = doc["dayFormat"]        | 0;
        forecastSrc     = doc["forecastSrc"]      | 0;
        autoRotate      = doc["autoRotate"]       | 1;
        manualScreen    = doc["manualScreen"]     | 0;

        // Display
        theme           = doc["theme"]            | 0;
        brightness      = constrain((int)(doc["brightness"] | 50), 1, 100);
        if (doc.containsKey("autoBrightness")) {
          JsonVariant v = doc["autoBrightness"];
          if (v.is<bool>()) autoBrightness = v.as<bool>();
          else if (v.is<int>()) autoBrightness = (v.as<int>() != 0);
          else if (v.is<const char*>()) {
            const char* s = v.as<const char*>();
            autoBrightness = (strcmp(s, "1")==0 || strcasecmp(s, "true")==0);
          }
        }
        scrollLevel = constrain((int)(doc["scrollLevel"] | scrollLevel), 0, 9);
        scrollSpeed = scrollDelays[scrollLevel];
        customMsg   = doc["customMsg"] | "";

        // Weather
        owmCity          = doc["owmCity"]          | "";
        owmCountryIndex  = doc["owmCountryIndex"]  | 0;
        owmCountryCustom = doc["owmCountryCustom"] | "";
        owmApiKey        = doc["owmApiKey"]        | "";
        wfToken          = doc["wfToken"]          | "";
        wfStationId      = doc["wfStationId"]      | "";

        // Calibration (robust)
        if (doc.containsKey("tempOffset")) {
          JsonVariant v = doc["tempOffset"];
          tempOffset = v.is<int>() ? v.as<int>() : atoi(v.as<const char*>());
        }
        if (doc.containsKey("humOffset")) {
          JsonVariant v = doc["humOffset"];
          humOffset = v.is<int>() ? v.as<int>() : atoi(v.as<const char*>());
        }
        if (doc.containsKey("lightGain")) {
          JsonVariant v = doc["lightGain"];
          lightGain = v.is<int>() ? v.as<int>() : atoi(v.as<const char*>());
        }
        tempOffset = constrain(tempOffset, -10, 10);
        humOffset  = constrain(humOffset, -20, 20);
        lightGain  = constrain(lightGain, 1, 150);

        saveAllSettings();
        Serial.println("[/settings] Saved OK");
        req->send(200, "application/json", "{\"ok\":true}");
      }
    }
  );

  // ---------- Timezones / Time ----------
  server.on("/timezones.json", HTTP_GET, [](AsyncWebServerRequest* req){
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (size_t i=0;i<kTimezoneCount;i++) {
      JsonObject o = arr.add<JsonObject>();
      o["id"]     = kTimezones[i].id;
      o["city"]   = kTimezones[i].city;
      o["offset"] = kTimezones[i].offsetMin;
      o["label"]  = String(kTimezones[i].city) + " (" + fmtUtcLabel(kTimezones[i].offsetMin) + ")";
    }
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/time.json", HTTP_GET, [](AsyncWebServerRequest* req){
    JsonDocument doc;
    doc["epoch"]     = (long)currentEpoch();  // <- RTC
    doc["tzOffset"]  = tzOffset;
    doc["tzName"]    = tzNameFromOffset(tzOffset);
    doc["dateFmt"]   = dateFmt;
    doc["ntpServer"] = "pool.ntp.org";
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

        if (doc.containsKey("tzName")) {
          String tz = doc["tzName"].as<String>();
          bool found = false;
          for (size_t i=0;i<kTimezoneCount;i++) {
            if (tz.equalsIgnoreCase(kTimezones[i].id) || tz.equalsIgnoreCase(kTimezones[i].city)) {
              tzOffset = kTimezones[i].offsetMin;
              found = true; break;
            }
          }
          if (!found && doc.containsKey("tzOffset")) tzOffset = (int)doc["tzOffset"].as<int>();
        } else if (doc.containsKey("tzOffset")) {
          tzOffset = (int)doc["tzOffset"].as<int>();
        }

        if (doc.containsKey("dateFmt")) dateFmt = (int)doc["dateFmt"].as<int>();

        if (doc.containsKey("epoch")) { // write RTC
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
    // (optional) read ?server= param if your sync accepts it
    // if (req->hasParam("server")) { String s = req->getParam("server")->value(); ... }
    syncTimeFromNTP();
    req->send(200, "text/plain", "syncing");
  });

  // ---------- Status / OTA / Reboot ----------
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    const char* degSym = (units.temp == TempUnit::F) ? "°F" : "°C";
    String status = "<html><body><h2>Status</h2>";
    status += "<p>WiFi: " + String(WiFi.SSID()) + "</p>";
    status += "<p>Weather: " + str_Weather_Conditions + " " + str_Temp + degSym + "</p>";
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

  // ---------- JSON status for index.html ----------
  server.on("/status.json", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["wifiSSID"] = WiFi.SSID();
    doc["ip"]       = WiFi.localIP().toString();
    doc["temp"]     = str_Temp;
    doc["humidity"] = str_Humd;
    doc["conditions"] = str_Weather_Conditions;
    doc["time"]     = String(chr_t_hour) + ":" + String(chr_t_minute) + ":" + String(chr_t_second);
    String json; serializeJson(doc, json);
    req->send(200, "application/json", json);
  });



  // ---------- 404 ----------
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  });

  server.begin();
}
