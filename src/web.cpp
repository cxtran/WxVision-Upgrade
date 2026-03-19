#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Update.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <memory>
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
#include "default_values.h"
#include "notifications.h"
#include "menu.h"
#include "noaa.h"
#include "worldtime.h"
#include "app_state.h"
#include "weather_provider.h"
#include "screen_manager.h"

static AppState &app = appState();
bool otaInProgress = false;

#define dayFormat app.dayFormat
#define dataSource app.dataSource
#define autoRotate app.autoRotate
#define manualScreen app.manualScreen
#define autoRotateInterval app.autoRotateInterval
#define units app.units
#define theme app.theme
#define brightness app.brightness
#define scrollSpeed app.scrollSpeed
#define scrollLevel app.scrollLevel
#define splashDurationSec app.splashDurationSec
#define autoBrightness app.autoBrightness
#define customMsg app.customMsg
#define wifiSSID app.wifiSSID
#define wifiPass app.wifiPass
#define owmCity app.owmCity
#define owmApiKey app.owmApiKey
#define wfToken app.wfToken
#define wfStationId app.wfStationId
#define owmCountryIndex app.owmCountryIndex
#define owmCountryCustom app.owmCountryCustom
#define tempOffset app.tempOffset
#define humOffset app.humOffset
#define lightGain app.lightGain
#define buzzerVolume app.buzzerVolume
#define buzzerToneSet app.buzzerToneSet
#define alarmSoundMode app.alarmSoundMode
#define alarmEnabled app.alarmEnabled
#define alarmHour app.alarmHour
#define alarmMinute app.alarmMinute
#define alarmRepeatMode app.alarmRepeatMode
#define alarmWeeklyDay app.alarmWeeklyDay
#define noaaAlertsEnabled app.noaaAlertsEnabled
#define noaaLatitude app.noaaLatitude
#define noaaLongitude app.noaaLongitude
#define forecastLinesPerDay app.forecastLinesPerDay
#define forecastPauseMs app.forecastPauseMs
#define forecastIconSize app.forecastIconSize
#define rtc app.rtc
#define tzOffset app.tzOffset
#define dateFmt app.dateFmt
#define fmt24 app.fmt24
#define ntpServerHost app.ntpServerHost
#define scrollDelays app.scrollDelays
#define reset_Time_and_Date_Display app.reset_Time_and_Date_Display

#define str_Weather_Conditions app.str_Weather_Conditions
#define str_Temp app.str_Temp
#define str_Humd app.str_Humd
#define chr_t_hour app.chr_t_hour
#define chr_t_minute app.chr_t_minute
#define chr_t_second app.chr_t_second
#define deviceHostname app.deviceHostname
#define owmCountryCode app.owmCountryCode

AsyncWebServer server(80);
static AsyncWebSocket g_appWs("/ws/app");
static bool webServerRunning = false;
static constexpr size_t kMaxSettingsBodyBytes = 8192;
static constexpr size_t kMaxTimeBodyBytes = 2048;
static constexpr size_t kMaxWorldTimeBodyBytes = 8192;
static constexpr size_t kMaxAppBodyBytes = 1024;
static constexpr uint32_t kAppWeatherBroadcastMinMs = 5000u;
static constexpr uint32_t kAppIndoorBroadcastMinMs = 5000u;
static constexpr uint32_t kAppDeviceBroadcastMinMs = 10000u;
static constexpr uint32_t kAppAlertBroadcastMinMs = 1000u;
static constexpr uint32_t kAppLightningBroadcastMinMs = 5000u;

namespace
{
volatile bool g_webPendingQuickRestore = false;
volatile bool g_webPendingFactoryReset = false;
volatile bool g_webPendingReboot = false;
volatile uint32_t g_webRebootAtMs = 0;

volatile bool g_ntpSyncRunning = false;
volatile bool g_ntpSyncLastOk = false;

bool g_wifiScanPrimed = false;
bool g_wifiScanIncludeHidden = false;
} // namespace

static bool queueNtpSyncTask();
static long currentEpoch();
static const char *dataSourceLabel(int value);
static const char *screenModeLabel(ScreenMode mode);
static IRCodes::WxKey irKeyForButton(String btn);
namespace
{
struct AppRuntimeState
{
  struct DeviceInfo
  {
    char name[33] = "";
    char ip[16] = "";
    char mac[18] = "";
    bool online = false;
    uint32_t uptimeSec = 0;
    char currentScreen[24] = "";
    char dataSourceName[24] = "";
  } device;

  struct WifiInfo
  {
    char ssid[33] = "";
    int rssi = 0;
  } wifi;

  struct TimeInfo
  {
    char localIso[32] = "";
    char timezone[48] = "";
    char display[16] = "";
  } time;

  struct LocationInfo
  {
    float lat = NAN;
    float lon = NAN;
  } location;

  struct WeatherInfo
  {
    char source[24] = "";
    float tempF = NAN;
    int humidity = -1;
    char condition[40] = "";
    char updatedIso[32] = "";
    uint32_t updatedEpoch = 0;
  } weather;

  struct IndoorInfo
  {
    float tempF = NAN;
    int humidity = -1;
    float ahtTempF = NAN;
    int ahtHumidity = -1;
    int co2ppm = 0;
    float pressureInHg = NAN;
  } indoor;

  struct AlertItem
  {
    char id[28] = "";
    char source[12] = "";
    char severity[16] = "";
    char headline[72] = "";
    char expires[32] = "";
  };

  struct AlertsInfo
  {
    size_t activeCount = 0;
    char highestSeverity[16] = "none";
    AlertItem items[3];
    size_t itemCount = 0;
  } alerts;

  struct LightningInfo
  {
    bool enabled = false;
    float lastDistanceMi = NAN;
    char lastTimestamp[32] = "";
    int strikeCount = 0;
    char level[16] = "none";
  } lightning;

  struct SettingsInfo
  {
    bool autoRotateEnabled = false;
    int rotateIntervalSec = 15;
    char manualScreenName[24] = "Main";
    bool noaaEnabled = false;
    bool lightningEnabled = false;
    bool soundEnabled = false;
  } settings;

  String stateJson;
  String dashboardJson;
  String alertsJson;
  String lightningJson;
  String deviceJson;
  String settingsJson;
  String weatherWsJson;
  String indoorWsJson;
  String deviceWsJson;
  String alertWsJson;
  String lightningWsJson;
  String weatherWsSig;
  String indoorWsSig;
  String deviceWsSig;
  String alertWsSig;
  String lightningWsSig;
  uint32_t lastRefreshMs = 0;
  uint32_t lastWeatherBroadcastMs = 0;
  uint32_t lastIndoorBroadcastMs = 0;
  uint32_t lastDeviceBroadcastMs = 0;
  uint32_t lastAlertBroadcastMs = 0;
  uint32_t lastLightningBroadcastMs = 0;
};

AppRuntimeState g_appRuntime;
String g_lastWeatherWsSig;
String g_lastIndoorWsSig;
String g_lastDeviceWsSig;
String g_lastAlertWsSig;
String g_lastLightningWsSig;
bool g_appLightningEnabled = true;

static const char *manualScreenLabel(int value)
{
  switch (value)
  {
  case 0:
    return "Main";
  case 1:
    return "Weather";
  case 2:
    return "Forecast";
  case 3:
    return "Clock";
  case 4:
    return "Lightning";
  case 5:
    return "Current";
  case 6:
    return "Hourly";
  default:
    return "Main";
  }
}

static int manualScreenValueFromName(const String &name)
{
  String value = name;
  value.trim();
  value.toLowerCase();
  if (value == "main")
    return 0;
  if (value == "weather")
    return 1;
  if (value == "forecast")
    return 2;
  if (value == "clock")
    return 3;
  if (value == "lightning")
    return 4;
  if (value == "current")
    return 5;
  if (value == "hourly")
    return 6;
  return manualScreen;
}

static float celsiusToFahrenheitValue(float tempC)
{
  if (!isfinite(tempC))
    return NAN;
  return tempC * 9.0f / 5.0f + 32.0f;
}

static float hpaToInHgValue(float pressureHpa)
{
  if (!isfinite(pressureHpa))
    return NAN;
  return pressureHpa * 0.0295299831f;
}

static float kmToMilesValue(float km)
{
  if (!isfinite(km))
    return NAN;
  return km * 0.621371f;
}

static void copyToBuffer(char *dest, size_t destSize, const String &value)
{
  if (destSize == 0)
    return;
  strlcpy(dest, value.c_str(), destSize);
}

static void copyToBuffer(char *dest, size_t destSize, const char *value)
{
  if (destSize == 0)
    return;
  strlcpy(dest, value ? value : "", destSize);
}

static String formatIsoLocalTime(time_t epoch)
{
  if (epoch <= 0)
    return String("");
  struct tm tmLocal = {};
  if (!localtime_r(&epoch, &tmLocal))
    return String("");

  char buf[40];
  if (strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tmLocal) == 0)
    return String("");

  String iso(buf);
  if (iso.length() == 24)
  {
    iso = iso.substring(0, 22) + ":" + iso.substring(22);
  }
  return iso;
}

static String formatCurrentIsoLocalTime()
{
  return formatIsoLocalTime(static_cast<time_t>(currentEpoch()));
}

static String highestAlertSeverity()
{
  static const char *kNone = "none";
  static const char *kWatch = "watch";
  static const char *kAdvisory = "advisory";
  static const char *kWarning = "warning";

  size_t count = noaaAlertCount();
  if (count == 0)
    return String(kNone);

  int bestRank = 0;
  String best = kNone;
  NwsAlert alert;
  for (size_t i = 0; i < count; ++i)
  {
    if (!noaaGetAlert(i, alert))
      continue;

    String severity = alert.severity;
    severity.toLowerCase();
    int rank = 1;
    if (severity.indexOf("warning") >= 0)
    {
      rank = 4;
      severity = kWarning;
    }
    else if (severity.indexOf("watch") >= 0)
    {
      rank = 3;
      severity = kWatch;
    }
    else if (severity.indexOf("advisory") >= 0)
    {
      rank = 2;
      severity = kAdvisory;
    }

    if (rank > bestRank)
    {
      bestRank = rank;
      best = severity;
    }
  }
  return best;
}

static void serializeCompactFloat(JsonVariant dst, float value, uint8_t digits = 1)
{
  if (isfinite(value))
    dst.set(serialized(String(value, static_cast<unsigned int>(digits))));
  else
    dst.set(nullptr);
}

static void rebuildAppRuntimeJson(AppRuntimeState &state)
{
  {
    JsonDocument doc;
    JsonObject device = doc["device"].to<JsonObject>();
    device["name"] = state.device.name;
    device["ip"] = state.device.ip;
    device["online"] = state.device.online;
    device["uptimeSec"] = state.device.uptimeSec;
    device["currentScreen"] = state.device.currentScreen;

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["ssid"] = state.wifi.ssid;
    wifi["rssi"] = state.wifi.rssi;

    JsonObject time = doc["time"].to<JsonObject>();
    time["local"] = state.time.localIso;
    time["timezone"] = state.time.timezone;
    time["display"] = state.time.display;

    JsonObject location = doc["location"].to<JsonObject>();
    serializeCompactFloat(location["lat"], state.location.lat, 4);
    serializeCompactFloat(location["lon"], state.location.lon, 4);

    JsonObject weather = doc["weather"].to<JsonObject>();
    weather["source"] = state.weather.source;
    serializeCompactFloat(weather["tempF"], state.weather.tempF);
    if (state.weather.humidity >= 0)
      weather["humidity"] = state.weather.humidity;
    else
      weather["humidity"] = nullptr;
    weather["condition"] = state.weather.condition;
    weather["updated"] = state.weather.updatedIso[0] ? state.weather.updatedIso : nullptr;

    JsonObject indoor = doc["indoor"].to<JsonObject>();
    serializeCompactFloat(indoor["tempF"], state.indoor.tempF);
    if (state.indoor.humidity >= 0)
      indoor["humidity"] = state.indoor.humidity;
    else
      indoor["humidity"] = nullptr;
    serializeCompactFloat(indoor["ahtTempF"], state.indoor.ahtTempF);
    if (state.indoor.ahtHumidity >= 0)
      indoor["ahtHumidity"] = state.indoor.ahtHumidity;
    else
      indoor["ahtHumidity"] = nullptr;
    if (state.indoor.co2ppm > 0)
      indoor["co2ppm"] = state.indoor.co2ppm;
    else
      indoor["co2ppm"] = nullptr;
    serializeCompactFloat(indoor["pressureInHg"], state.indoor.pressureInHg, 2);

    JsonObject alerts = doc["alerts"].to<JsonObject>();
    alerts["activeCount"] = state.alerts.activeCount;
    alerts["highestSeverity"] = state.alerts.highestSeverity;

    JsonObject lightning = doc["lightning"].to<JsonObject>();
    lightning["enabled"] = state.lightning.enabled;
    serializeCompactFloat(lightning["lastDistanceMi"], state.lightning.lastDistanceMi);
    lightning["strikeCount"] = state.lightning.strikeCount;

    state.stateJson = "";
    serializeJson(doc, state.stateJson);
  }

  {
    JsonDocument doc;
    doc["time"] = state.time.display;
    serializeCompactFloat(doc["tempF"], state.weather.tempF);
    if (state.weather.humidity >= 0)
      doc["humidity"] = state.weather.humidity;
    else
      doc["humidity"] = nullptr;
    doc["condition"] = state.weather.condition;
    serializeCompactFloat(doc["indoorTempF"], state.indoor.tempF);
    if (state.indoor.humidity >= 0)
      doc["indoorHumidity"] = state.indoor.humidity;
    else
      doc["indoorHumidity"] = nullptr;
    if (state.indoor.co2ppm > 0)
      doc["co2ppm"] = state.indoor.co2ppm;
    else
      doc["co2ppm"] = nullptr;
    doc["alertCount"] = state.alerts.activeCount;
    doc["currentScreen"] = state.device.currentScreen;
    doc["rssi"] = state.wifi.rssi;

    state.dashboardJson = "";
    serializeJson(doc, state.dashboardJson);
  }

  {
    JsonDocument doc;
    doc["activeCount"] = state.alerts.activeCount;
    doc["highestSeverity"] = state.alerts.highestSeverity;
    JsonArray items = doc["items"].to<JsonArray>();
    for (size_t i = 0; i < state.alerts.itemCount; ++i)
    {
      JsonObject item = items.add<JsonObject>();
      item["id"] = state.alerts.items[i].id;
      item["source"] = state.alerts.items[i].source;
      item["severity"] = state.alerts.items[i].severity;
      item["headline"] = state.alerts.items[i].headline;
      if (state.alerts.items[i].expires[0])
        item["expires"] = state.alerts.items[i].expires;
      else
        item["expires"] = nullptr;
    }

    state.alertsJson = "";
    serializeJson(doc, state.alertsJson);
  }

  {
    JsonDocument doc;
    doc["enabled"] = state.lightning.enabled;
    serializeCompactFloat(doc["lastDistanceMi"], state.lightning.lastDistanceMi);
    doc["lastTimestamp"] = state.lightning.lastTimestamp[0] ? state.lightning.lastTimestamp : nullptr;
    doc["strikeCount"] = state.lightning.strikeCount;
    doc["level"] = state.lightning.level;

    state.lightningJson = "";
    serializeJson(doc, state.lightningJson);
  }

  {
    JsonDocument doc;
    doc["name"] = state.device.name;
    doc["ip"] = state.device.ip;
    doc["mac"] = state.device.mac;
    doc["ssid"] = state.wifi.ssid;
    doc["rssi"] = state.wifi.rssi;
    doc["uptimeSec"] = state.device.uptimeSec;
    doc["currentScreen"] = state.device.currentScreen;
    doc["dataSource"] = state.device.dataSourceName;

    state.deviceJson = "";
    serializeJson(doc, state.deviceJson);
  }

  {
    JsonDocument doc;
    JsonObject display = doc["display"].to<JsonObject>();
    display["autoRotate"] = state.settings.autoRotateEnabled;
    display["rotateIntervalSec"] = state.settings.rotateIntervalSec;
    display["manualScreen"] = state.settings.manualScreenName;

    JsonObject alerts = doc["alerts"].to<JsonObject>();
    alerts["noaaEnabled"] = state.settings.noaaEnabled;
    alerts["lightningEnabled"] = state.settings.lightningEnabled;
    alerts["soundEnabled"] = state.settings.soundEnabled;

    state.settingsJson = "";
    serializeJson(doc, state.settingsJson);
  }

  {
    JsonDocument doc;
    doc["type"] = "weather_update";
    doc["ts"] = state.time.localIso;
    JsonObject data = doc["data"].to<JsonObject>();
    serializeCompactFloat(data["tempF"], state.weather.tempF);
    if (state.weather.humidity >= 0)
      data["humidity"] = state.weather.humidity;
    else
      data["humidity"] = nullptr;
    data["condition"] = state.weather.condition;

    state.weatherWsJson = "";
    serializeJson(doc, state.weatherWsJson);
    state.weatherWsSig = String(state.weather.tempF, 1) + "|" + state.weather.humidity + "|" + state.weather.condition;
  }

  {
    JsonDocument doc;
    doc["type"] = "indoor_update";
    doc["ts"] = state.time.localIso;
    JsonObject data = doc["data"].to<JsonObject>();
    serializeCompactFloat(data["tempF"], state.indoor.tempF);
    if (state.indoor.humidity >= 0)
      data["humidity"] = state.indoor.humidity;
    else
      data["humidity"] = nullptr;
    if (state.indoor.co2ppm > 0)
      data["co2ppm"] = state.indoor.co2ppm;
    else
      data["co2ppm"] = nullptr;
    serializeCompactFloat(data["pressureInHg"], state.indoor.pressureInHg, 2);

    state.indoorWsJson = "";
    serializeJson(doc, state.indoorWsJson);
    state.indoorWsSig = String(state.indoor.tempF, 1) + "|" + state.indoor.humidity + "|" +
                        state.indoor.co2ppm + "|" + String(state.indoor.pressureInHg, 2);
  }

  {
    JsonDocument doc;
    doc["type"] = "device_update";
    doc["ts"] = state.time.localIso;
    JsonObject data = doc["data"].to<JsonObject>();
    data["currentScreen"] = state.device.currentScreen;
    data["uptimeSec"] = state.device.uptimeSec;
    data["rssi"] = state.wifi.rssi;

    state.deviceWsJson = "";
    serializeJson(doc, state.deviceWsJson);
    state.deviceWsSig = String(state.device.currentScreen) + "|" + state.device.uptimeSec + "|" + state.wifi.rssi;
  }

  {
    JsonDocument doc;
    doc["type"] = "alert_update";
    doc["ts"] = state.time.localIso;
    JsonObject data = doc["data"].to<JsonObject>();
    data["activeCount"] = state.alerts.activeCount;
    data["highestSeverity"] = state.alerts.highestSeverity;
    if (state.alerts.itemCount > 0)
      data["headline"] = state.alerts.items[0].headline;
    else
      data["headline"] = nullptr;

    state.alertWsJson = "";
    serializeJson(doc, state.alertWsJson);
    state.alertWsSig = String(state.alerts.activeCount) + "|" + state.alerts.highestSeverity + "|" +
                       ((state.alerts.itemCount > 0) ? String(state.alerts.items[0].headline) : String(""));
  }

  {
    JsonDocument doc;
    doc["type"] = "lightning_update";
    doc["ts"] = state.time.localIso;
    JsonObject data = doc["data"].to<JsonObject>();
    serializeCompactFloat(data["lastDistanceMi"], state.lightning.lastDistanceMi);
    data["strikeCount"] = state.lightning.strikeCount;

    state.lightningWsJson = "";
    serializeJson(doc, state.lightningWsJson);
    state.lightningWsSig = String(state.lightning.lastDistanceMi, 1) + "|" + state.lightning.strikeCount + "|" +
                           state.lightning.level;
  }
}

static void refreshAppRuntimeState(bool force = false)
{
  uint32_t nowMs = millis();
  if (!force && g_appRuntime.lastRefreshMs != 0 &&
      static_cast<uint32_t>(nowMs - g_appRuntime.lastRefreshMs) < 1000u)
  {
    return;
  }

  AppRuntimeState next;
  copyToBuffer(next.device.name, sizeof(next.device.name), deviceHostname.length() ? deviceHostname : String("WxVision"));
  copyToBuffer(next.device.ip, sizeof(next.device.ip), WiFi.localIP().toString());
  copyToBuffer(next.device.mac, sizeof(next.device.mac), WiFi.macAddress());
  next.device.online = (WiFi.status() == WL_CONNECTED);
  next.device.uptimeSec = millis() / 1000UL;
  copyToBuffer(next.device.currentScreen, sizeof(next.device.currentScreen),
               isScreenOff() ? "Screen Off" : screenModeLabel(currentScreen));
  copyToBuffer(next.device.dataSourceName, sizeof(next.device.dataSourceName), dataSourceLabel(dataSource));

  copyToBuffer(next.wifi.ssid, sizeof(next.wifi.ssid), WiFi.SSID());
  next.wifi.rssi = WiFi.RSSI();

  String nowIso = formatCurrentIsoLocalTime();
  copyToBuffer(next.time.localIso, sizeof(next.time.localIso), nowIso);
  copyToBuffer(next.time.timezone, sizeof(next.time.timezone), timezoneIsCustom() ? "Custom" : currentTimezoneId());
  snprintf(next.time.display, sizeof(next.time.display), "%s:%s:%s %s",
           chr_t_hour, chr_t_minute, chr_t_second, (fmt24 ? "" : ((atoi(chr_t_hour) >= 12) ? "PM" : "AM")));
  if (fmt24)
  {
    snprintf(next.time.display, sizeof(next.time.display), "%s:%s:%s", chr_t_hour, chr_t_minute, chr_t_second);
  }

  next.location.lat = noaaLatitude;
  next.location.lon = noaaLongitude;

  wxv::provider::WeatherSnapshot snapshot;
  wxv::provider::readActiveProviderSnapshot(snapshot);
  copyToBuffer(next.weather.source, sizeof(next.weather.source), dataSourceLabel(dataSource));
  if (snapshot.hasCurrent)
  {
    if (!isnan(snapshot.current.tempC))
      next.weather.tempF = celsiusToFahrenheitValue(snapshot.current.tempC);
    if (snapshot.current.humidityPct >= 0)
      next.weather.humidity = snapshot.current.humidityPct;
    if (snapshot.current.condition.length() > 0)
      copyToBuffer(next.weather.condition, sizeof(next.weather.condition), snapshot.current.condition);
    next.weather.updatedEpoch = currentCond.time;
  }
  if (isnan(next.weather.tempF) && str_Temp.length() > 0)
    next.weather.tempF = celsiusToFahrenheitValue(static_cast<float>(atof(str_Temp.c_str())));
  if (next.weather.humidity < 0 && str_Humd.length() > 0)
    next.weather.humidity = str_Humd.toInt();
  if (next.weather.condition[0] == '\0' && str_Weather_Conditions.length() > 0)
    copyToBuffer(next.weather.condition, sizeof(next.weather.condition), str_Weather_Conditions);
  copyToBuffer(next.weather.updatedIso, sizeof(next.weather.updatedIso),
               formatIsoLocalTime(static_cast<time_t>(next.weather.updatedEpoch)));

  if (!isnan(SCD40_temp))
    next.indoor.tempF = celsiusToFahrenheitValue(SCD40_temp + tempOffset);
  else if (!isnan(aht20_temp))
    next.indoor.tempF = celsiusToFahrenheitValue(aht20_temp + tempOffset);
  if (!isnan(SCD40_hum))
    next.indoor.humidity = static_cast<int>(roundf(constrain(SCD40_hum + static_cast<float>(humOffset), 0.0f, 100.0f)));
  if (!isnan(aht20_temp))
    next.indoor.ahtTempF = celsiusToFahrenheitValue(aht20_temp + tempOffset);
  if (!isnan(aht20_hum))
    next.indoor.ahtHumidity = static_cast<int>(roundf(constrain(aht20_hum + static_cast<float>(humOffset), 0.0f, 100.0f)));
  next.indoor.co2ppm = (SCD40_co2 > 0) ? static_cast<int>(SCD40_co2) : 0;
  if (!isnan(bmp280_pressure))
    next.indoor.pressureInHg = hpaToInHgValue(bmp280_pressure);

  next.alerts.activeCount = noaaAlertCount();
  copyToBuffer(next.alerts.highestSeverity, sizeof(next.alerts.highestSeverity), highestAlertSeverity());
  next.alerts.itemCount = min(next.alerts.activeCount, static_cast<size_t>(3));
  for (size_t i = 0; i < next.alerts.itemCount; ++i)
  {
    NwsAlert alert;
    if (!noaaGetAlert(i, alert))
      continue;
    copyToBuffer(next.alerts.items[i].id, sizeof(next.alerts.items[i].id), alert.id);
    copyToBuffer(next.alerts.items[i].source, sizeof(next.alerts.items[i].source), "NOAA");
    String severity = alert.severity;
    severity.toLowerCase();
    copyToBuffer(next.alerts.items[i].severity, sizeof(next.alerts.items[i].severity), severity);
    copyToBuffer(next.alerts.items[i].headline, sizeof(next.alerts.items[i].headline), alert.headline);
    copyToBuffer(next.alerts.items[i].expires, sizeof(next.alerts.items[i].expires), alert.expires);
  }

  next.lightning.enabled = g_appLightningEnabled && (dataSource == DATA_SOURCE_WEATHERFLOW);
  next.lightning.strikeCount = tempest.strikeCount;
  if (!isnan(tempest.lightningLastEventDistanceKm))
  {
    next.lightning.lastDistanceMi = kmToMilesValue(static_cast<float>(tempest.lightningLastEventDistanceKm));
  }
  else if (!isnan(tempest.strikeDist))
  {
    next.lightning.lastDistanceMi = kmToMilesValue(static_cast<float>(tempest.strikeDist));
  }
  copyToBuffer(next.lightning.lastTimestamp, sizeof(next.lightning.lastTimestamp),
               formatIsoLocalTime(static_cast<time_t>(tempest.lightningLastEventEpoch)));
  if (isfinite(next.lightning.lastDistanceMi))
  {
    if (next.lightning.lastDistanceMi <= 5.0f)
      copyToBuffer(next.lightning.level, sizeof(next.lightning.level), "nearby");
    else if (next.lightning.lastDistanceMi <= 15.0f)
      copyToBuffer(next.lightning.level, sizeof(next.lightning.level), "watch");
    else
      copyToBuffer(next.lightning.level, sizeof(next.lightning.level), "distant");
  }
  else
  {
    copyToBuffer(next.lightning.level, sizeof(next.lightning.level), "none");
  }

  next.settings.autoRotateEnabled = autoRotate != 0;
  next.settings.rotateIntervalSec = autoRotateInterval;
  copyToBuffer(next.settings.manualScreenName, sizeof(next.settings.manualScreenName), manualScreenLabel(manualScreen));
  next.settings.noaaEnabled = noaaAlertsEnabled;
  next.settings.lightningEnabled = g_appLightningEnabled;
  next.settings.soundEnabled = (buzzerVolume > 0);
  next.lastRefreshMs = nowMs;
  next.lastWeatherBroadcastMs = g_appRuntime.lastWeatherBroadcastMs;
  next.lastIndoorBroadcastMs = g_appRuntime.lastIndoorBroadcastMs;
  next.lastDeviceBroadcastMs = g_appRuntime.lastDeviceBroadcastMs;
  next.lastAlertBroadcastMs = g_appRuntime.lastAlertBroadcastMs;
  next.lastLightningBroadcastMs = g_appRuntime.lastLightningBroadcastMs;

  rebuildAppRuntimeJson(next);
  g_appRuntime = std::move(next);
}

static void sendAppJson(AsyncWebServerRequest *req, const String &payload)
{
  AsyncWebServerResponse *res = req->beginResponse(200, "application/json", payload);
  res->addHeader("Cache-Control", "no-store, max-age=0");
  req->send(res);
}

static String appHelloMessage()
{
  refreshAppRuntimeState();
  JsonDocument doc;
  doc["type"] = "hello";
  doc["ts"] = g_appRuntime.time.localIso;
  JsonObject data = doc["data"].to<JsonObject>();
  data["deviceName"] = g_appRuntime.device.name;
  data["protocolVersion"] = 1;
  String payload;
  serializeJson(doc, payload);
  return payload;
}

static void appRuntimeBroadcastTick()
{
  refreshAppRuntimeState();
  if (g_appWs.count() == 0)
  {
    return;
  }

  uint32_t nowMs = millis();
  if (g_appRuntime.weatherWsSig != g_lastWeatherWsSig &&
      static_cast<uint32_t>(nowMs - g_appRuntime.lastWeatherBroadcastMs) >= kAppWeatherBroadcastMinMs)
  {
    g_appWs.textAll(g_appRuntime.weatherWsJson);
    g_lastWeatherWsSig = g_appRuntime.weatherWsSig;
    g_appRuntime.lastWeatherBroadcastMs = nowMs;
  }
  if (g_appRuntime.indoorWsSig != g_lastIndoorWsSig &&
      static_cast<uint32_t>(nowMs - g_appRuntime.lastIndoorBroadcastMs) >= kAppIndoorBroadcastMinMs)
  {
    g_appWs.textAll(g_appRuntime.indoorWsJson);
    g_lastIndoorWsSig = g_appRuntime.indoorWsSig;
    g_appRuntime.lastIndoorBroadcastMs = nowMs;
  }
  if (g_appRuntime.deviceWsSig != g_lastDeviceWsSig &&
      static_cast<uint32_t>(nowMs - g_appRuntime.lastDeviceBroadcastMs) >= kAppDeviceBroadcastMinMs)
  {
    g_appWs.textAll(g_appRuntime.deviceWsJson);
    g_lastDeviceWsSig = g_appRuntime.deviceWsSig;
    g_appRuntime.lastDeviceBroadcastMs = nowMs;
  }
  if (g_appRuntime.alertWsSig != g_lastAlertWsSig &&
      static_cast<uint32_t>(nowMs - g_appRuntime.lastAlertBroadcastMs) >= kAppAlertBroadcastMinMs)
  {
    g_appWs.textAll(g_appRuntime.alertWsJson);
    g_lastAlertWsSig = g_appRuntime.alertWsSig;
    g_appRuntime.lastAlertBroadcastMs = nowMs;
  }
  if (g_appRuntime.lightningWsSig != g_lastLightningWsSig &&
      static_cast<uint32_t>(nowMs - g_appRuntime.lastLightningBroadcastMs) >= kAppLightningBroadcastMinMs)
  {
    g_appWs.textAll(g_appRuntime.lightningWsJson);
    g_lastLightningWsSig = g_appRuntime.lightningWsSig;
    g_appRuntime.lastLightningBroadcastMs = nowMs;
  }
}

static bool enqueueAppButton(const String &button)
{
  String value = button;
  value.trim();
  value.toLowerCase();
  if (value == "toggle_screen")
    return enqueueVirtualIRKey(IRCodes::WxKey::Screen);

  IRCodes::WxKey key = irKeyForButton(value);
  if (key == IRCodes::WxKey::Unknown)
    return false;
  return enqueueVirtualIRKey(key);
}
} // namespace

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

struct AppSettingsDirtyFlags
{
  bool device = false;
  bool unitPrefs = false;
  bool display = false;
  bool weather = false;
  bool calibration = false;
  bool alarms = false;
  bool noaa = false;
  bool dateTime = false;
  bool worldTime = false;
};

static const char *tempUnitName(TempUnit value)
{
  return (value == TempUnit::F) ? "F" : "C";
}

static const char *windUnitName(WindUnit value)
{
  switch (value)
  {
  case WindUnit::MPH:
    return "mph";
  case WindUnit::KTS:
    return "kts";
  case WindUnit::KPH:
    return "kph";
  default:
    return "mps";
  }
}

static const char *pressUnitName(PressUnit value)
{
  return (value == PressUnit::INHG) ? "inHg" : "hPa";
}

static const char *precipUnitName(PrecipUnit value)
{
  return (value == PrecipUnit::INCH) ? "in" : "mm";
}

static TempUnit tempUnitFromName(String value, bool &ok)
{
  value.trim();
  value.toUpperCase();
  if (value == "F")
  {
    ok = true;
    return TempUnit::F;
  }
  if (value == "C")
  {
    ok = true;
    return TempUnit::C;
  }
  ok = false;
  return units.temp;
}

static WindUnit windUnitFromName(String value, bool &ok)
{
  value.trim();
  value.toLowerCase();
  if (value == "mph")
  {
    ok = true;
    return WindUnit::MPH;
  }
  if (value == "kts" || value == "kt")
  {
    ok = true;
    return WindUnit::KTS;
  }
  if (value == "kph" || value == "km/h")
  {
    ok = true;
    return WindUnit::KPH;
  }
  if (value == "mps" || value == "m/s")
  {
    ok = true;
    return WindUnit::MPS;
  }
  ok = false;
  return units.wind;
}

static PressUnit pressUnitFromName(String value, bool &ok)
{
  value.trim();
  value.toLowerCase();
  if (value == "inhg")
  {
    ok = true;
    return PressUnit::INHG;
  }
  if (value == "hpa")
  {
    ok = true;
    return PressUnit::HPA;
  }
  ok = false;
  return units.press;
}

static PrecipUnit precipUnitFromName(String value, bool &ok)
{
  value.trim();
  value.toLowerCase();
  if (value == "in" || value == "inch" || value == "inches")
  {
    ok = true;
    return PrecipUnit::INCH;
  }
  if (value == "mm")
  {
    ok = true;
    return PrecipUnit::MM;
  }
  ok = false;
  return units.precip;
}

static const char *alarmRepeatName(AlarmRepeatMode mode)
{
  switch (mode)
  {
  case ALARM_REPEAT_DAILY:
    return "daily";
  case ALARM_REPEAT_WEEKLY:
    return "weekly";
  case ALARM_REPEAT_WEEKDAY:
    return "weekday";
  case ALARM_REPEAT_WEEKEND:
    return "weekend";
  default:
    return "none";
  }
}

static AlarmRepeatMode alarmRepeatModeFromValue(JsonVariantConst value, bool &ok)
{
  if (value.is<int>())
  {
    int raw = value.as<int>();
    if (raw >= ALARM_REPEAT_NONE && raw <= ALARM_REPEAT_WEEKEND)
    {
      ok = true;
      return static_cast<AlarmRepeatMode>(raw);
    }
    ok = false;
    return ALARM_REPEAT_NONE;
  }

  String text = value.as<String>();
  text.trim();
  text.toLowerCase();
  if (text == "daily")
  {
    ok = true;
    return ALARM_REPEAT_DAILY;
  }
  if (text == "weekly")
  {
    ok = true;
    return ALARM_REPEAT_WEEKLY;
  }
  if (text == "weekday" || text == "weekdays")
  {
    ok = true;
    return ALARM_REPEAT_WEEKDAY;
  }
  if (text == "weekend" || text == "weekends")
  {
    ok = true;
    return ALARM_REPEAT_WEEKEND;
  }
  if (text == "none" || text.length() == 0)
  {
    ok = true;
    return ALARM_REPEAT_NONE;
  }
  ok = false;
  return ALARM_REPEAT_NONE;
}

static void setFieldError(JsonObject fieldErrors, const char *field, const char *message)
{
  if (field && message && fieldErrors[field].isNull())
    fieldErrors[field] = message;
}

static void serializeAppDeviceSettings(JsonObject obj)
{
  obj["wifiSsid"] = wifiSSID;
  obj["wifiPassword"] = "";
  obj["wifiPasswordSet"] = !wifiPass.isEmpty();
  obj["dayFormat"] = dayFormat;
  obj["dataSource"] = dataSource;
  obj["dataSourceLabel"] = dataSourceLabel(dataSource);
  obj["autoRotate"] = autoRotate != 0;
  obj["rotateIntervalSec"] = autoRotateInterval;
  obj["manualScreen"] = manualScreenLabel(manualScreen);
}

static void serializeAppUnitsSettings(JsonObject obj)
{
  obj["temperature"] = tempUnitName(units.temp);
  obj["wind"] = windUnitName(units.wind);
  obj["pressure"] = pressUnitName(units.press);
  obj["rain"] = precipUnitName(units.precip);
  obj["clock24h"] = units.clock24h;
}

static void serializeAppDisplaySettings(JsonObject obj)
{
  obj["theme"] = theme;
  obj["autoThemeMode"] = autoThemeAmbient ? 2 : (autoThemeSchedule ? 1 : 0);
  obj["autoThemeSchedule"] = autoThemeSchedule;
  obj["autoThemeAmbient"] = autoThemeAmbient;
  obj["themeLightThreshold"] = autoThemeLightThreshold;
  obj["dayThemeStartMinutes"] = dayThemeStartMinutes;
  obj["nightThemeStartMinutes"] = nightThemeStartMinutes;
  obj["brightness"] = brightness;
  obj["autoBrightness"] = autoBrightness;
  obj["scrollLevel"] = scrollLevel;
  obj["scrollSpeed"] = scrollSpeed;
  obj["verticalScrollLevel"] = verticalScrollLevel;
  obj["verticalScrollSpeed"] = verticalScrollSpeed;
  obj["customMessage"] = customMsg;
  obj["sceneClockEnabled"] = sceneClockEnabled;
  obj["splashDurationSec"] = splashDurationSec;
}

static void serializeAppWfTempestSettings(JsonObject obj)
{
  obj["selected"] = (dataSource == DATA_SOURCE_WEATHERFLOW);
  obj["token"] = "";
  obj["tokenSet"] = !wfToken.isEmpty();
  obj["stationId"] = wfStationId;
}

static void serializeAppForecastUiSettings(JsonObject obj)
{
  obj["linesPerDay"] = forecastLinesPerDay;
  obj["pauseMs"] = forecastPauseMs;
  obj["iconSize"] = forecastIconSize;
}

static void serializeAppCalibrationSettings(JsonObject obj)
{
  obj["tempOffset"] = serialized(String(dispTempOffset(tempOffset), 1));
  obj["humidityOffset"] = humOffset;
  obj["lightGain"] = lightGain;
  obj["co2Threshold"] = envAlertCo2Threshold;
  obj["tempThreshold"] = serialized(String(dispTemp(envAlertTempThresholdC), 1));
  obj["humidityLowThreshold"] = envAlertHumidityLowThreshold;
  obj["humidityHighThreshold"] = envAlertHumidityHighThreshold;
}

static void serializeAppAlarmsSettings(JsonObject obj)
{
  JsonArray alarms = obj["items"].to<JsonArray>();
  for (int i = 0; i < 3; ++i)
  {
    JsonObject item = alarms.add<JsonObject>();
    item["index"] = i;
    item["enabled"] = alarmEnabled[i];
    item["hour"] = alarmHour[i];
    item["minute"] = alarmMinute[i];
    item["repeat"] = alarmRepeatName(alarmRepeatMode[i]);
    item["repeatCode"] = static_cast<uint8_t>(alarmRepeatMode[i]);
    item["weekDay"] = alarmWeeklyDay[i];
    item["oneShotPending"] = alarmOneShotPending[i];
  }
  obj["alarmSound"] = alarmSoundMode;
  obj["co2Threshold"] = envAlertCo2Threshold;
  obj["tempThreshold"] = serialized(String(dispTemp(envAlertTempThresholdC), 1));
  obj["humidityLowThreshold"] = envAlertHumidityLowThreshold;
  obj["humidityHighThreshold"] = envAlertHumidityHighThreshold;
}

static void serializeAppNoaaSettings(JsonObject obj)
{
  obj["enabled"] = noaaAlertsEnabled;
  obj["latitude"] = serialized(String(noaaLatitude, 4));
  obj["longitude"] = serialized(String(noaaLongitude, 4));
}

static void serializeAppLocationSettings(JsonObject obj)
{
  obj["latitude"] = serialized(String(noaaLatitude, 4));
  obj["longitude"] = serialized(String(noaaLongitude, 4));
}

static void serializeAppDateTimeSettings(JsonObject obj)
{
  obj["timezone"] = timezoneIsCustom() ? "" : currentTimezoneId();
  obj["tzOffset"] = tzOffset;
  obj["tzStandardOffset"] = tzStandardOffset;
  obj["autoDst"] = tzAutoDst;
  obj["dateFormat"] = dateFmt;
  obj["clock24h"] = units.clock24h;
  obj["ntpServer"] = ntpServerHost;
  obj["ntpPreset"] = ntpServerPreset;
}

static void serializeAppWorldTimeSettings(JsonObject obj)
{
  obj["autoCycle"] = worldTimeAutoCycleEnabled();
  JsonArray items = obj["items"].to<JsonArray>();
  for (size_t i = 0; i < worldTimeSelectionCount(); ++i)
  {
    int tzIndex = worldTimeSelectionAt(i);
    if (tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount()))
      continue;
    const TimezoneInfo &info = timezoneInfoAt(static_cast<size_t>(tzIndex));
    JsonObject item = items.add<JsonObject>();
    item["label"] = info.city;
    item["timezone"] = info.id;
    item["index"] = tzIndex;
  }

  JsonArray custom = obj["customCities"].to<JsonArray>();
  for (size_t i = 0; i < worldTimeCustomCityCount(); ++i)
  {
    WorldTimeCustomCity city;
    if (!worldTimeGetCustomCity(i, city))
      continue;
    JsonObject item = custom.add<JsonObject>();
    item["name"] = city.name;
    item["lat"] = city.lat;
    item["lon"] = city.lon;
    item["enabled"] = city.enabled;
    item["tzIndex"] = city.tzIndex;
    if (city.tzIndex >= 0 && city.tzIndex < static_cast<int>(timezoneCount()))
      item["timezone"] = timezoneInfoAt(static_cast<size_t>(city.tzIndex)).id;
  }
}

static void serializeAppSoundSettings(JsonObject obj)
{
  obj["enabled"] = buzzerVolume > 0;
  obj["volume"] = buzzerVolume;
  obj["toneSet"] = buzzerToneSet;
  obj["alarmSound"] = alarmSoundMode;
}

static void serializeAppSettingsSection(JsonObject root, const char *section)
{
  JsonObject obj = root[section].to<JsonObject>();
  if (strcmp(section, "device") == 0)
    serializeAppDeviceSettings(obj);
  else if (strcmp(section, "units") == 0)
    serializeAppUnitsSettings(obj);
  else if (strcmp(section, "display") == 0)
    serializeAppDisplaySettings(obj);
  else if (strcmp(section, "wf-tempest") == 0)
    serializeAppWfTempestSettings(obj);
  else if (strcmp(section, "forecast-ui") == 0)
    serializeAppForecastUiSettings(obj);
  else if (strcmp(section, "calibration") == 0)
    serializeAppCalibrationSettings(obj);
  else if (strcmp(section, "alarms") == 0)
    serializeAppAlarmsSettings(obj);
  else if (strcmp(section, "noaa") == 0)
    serializeAppNoaaSettings(obj);
  else if (strcmp(section, "location") == 0)
    serializeAppLocationSettings(obj);
  else if (strcmp(section, "datetime") == 0)
    serializeAppDateTimeSettings(obj);
  else if (strcmp(section, "world-time") == 0)
    serializeAppWorldTimeSettings(obj);
  else if (strcmp(section, "sound") == 0)
    serializeAppSoundSettings(obj);
}

static bool isKnownAppSettingsSection(const char *section)
{
  static const char *const kSections[] = {
      "device", "units", "display", "wf-tempest", "forecast-ui", "calibration",
      "alarms", "noaa", "location", "datetime", "world-time", "sound"};
  for (const char *name : kSections)
  {
    if (strcmp(name, section) == 0)
      return true;
  }
  return false;
}

static String buildAppSettingsJson(const char *section = nullptr)
{
  JsonDocument doc;
  if (section && *section)
  {
    if (strcmp(section, "device") == 0)
      serializeAppDeviceSettings(doc.to<JsonObject>());
    else if (strcmp(section, "units") == 0)
      serializeAppUnitsSettings(doc.to<JsonObject>());
    else if (strcmp(section, "display") == 0)
      serializeAppDisplaySettings(doc.to<JsonObject>());
    else if (strcmp(section, "wf-tempest") == 0)
      serializeAppWfTempestSettings(doc.to<JsonObject>());
    else if (strcmp(section, "forecast-ui") == 0)
      serializeAppForecastUiSettings(doc.to<JsonObject>());
    else if (strcmp(section, "calibration") == 0)
      serializeAppCalibrationSettings(doc.to<JsonObject>());
    else if (strcmp(section, "alarms") == 0)
      serializeAppAlarmsSettings(doc.to<JsonObject>());
    else if (strcmp(section, "noaa") == 0)
      serializeAppNoaaSettings(doc.to<JsonObject>());
    else if (strcmp(section, "location") == 0)
      serializeAppLocationSettings(doc.to<JsonObject>());
    else if (strcmp(section, "datetime") == 0)
      serializeAppDateTimeSettings(doc.to<JsonObject>());
    else if (strcmp(section, "world-time") == 0)
      serializeAppWorldTimeSettings(doc.to<JsonObject>());
    else if (strcmp(section, "sound") == 0)
      serializeAppSoundSettings(doc.to<JsonObject>());
  }
  else
  {
    static const char *const kSections[] = {
        "device", "units", "display", "wf-tempest", "forecast-ui", "calibration",
        "alarms", "noaa", "location", "datetime", "world-time", "sound"};
    for (const char *name : kSections)
      serializeAppSettingsSection(doc.to<JsonObject>(), name);
  }

  String payload;
  serializeJson(doc, payload);
  return payload;
}

static void sendAppValidationError(AsyncWebServerRequest *req, JsonObject fieldErrors)
{
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = "validation_failed";
  JsonObject out = doc["fieldErrors"].to<JsonObject>();
  for (JsonPair pair : fieldErrors)
    out[pair.key().c_str()] = pair.value().as<const char *>();

  String payload;
  serializeJson(doc, payload);
  req->send(400, "application/json", payload);
}

static void broadcastAppSettingsUpdate(const char *section)
{
  if (g_appWs.count() == 0)
    return;

  JsonDocument doc;
  doc["type"] = "settings_update";
  doc["ts"] = formatCurrentIsoLocalTime();
  JsonObject data = doc["data"].to<JsonObject>();
  data["section"] = section;
  String payload;
  serializeJson(doc, payload);
  g_appWs.textAll(payload);
}

static void persistAppSettingsChanges(const AppSettingsDirtyFlags &dirty)
{
  if (dirty.unitPrefs)
    saveUnits();
  fmt24 = units.clock24h ? 1 : 0;
  if (dirty.device)
    saveDeviceSettings();
  if (dirty.display)
    saveDisplaySettings();
  if (dirty.weather)
    saveWeatherSettings();
  if (dirty.calibration)
    saveCalibrationSettings();
  if (dirty.alarms)
    saveAlarmSettings();
  if (dirty.noaa)
  {
    saveNoaaSettings();
    notifyNoaaSettingsChanged();
  }
  if (dirty.dateTime)
    saveDateTimeSettings();
  if (dirty.worldTime)
    saveWorldTimeSettings();
}

static bool applyAppDeviceSettings(JsonObjectConst obj, JsonObject fieldErrors, AppSettingsDirtyFlags &dirty)
{
  if (obj.isNull())
    return true;

  if (!obj["wifiSsid"].isNull())
  {
    wifiSSID = obj["wifiSsid"].as<String>();
    dirty.device = true;
  }
  if (!obj["wifiPassword"].isNull())
  {
    String value = obj["wifiPassword"].as<String>();
    if (!value.isEmpty())
    {
      wifiPass = value;
      dirty.device = true;
    }
  }
  if (!obj["dayFormat"].isNull())
  {
    int value = obj["dayFormat"].as<int>();
    if (value < 0 || value > 1)
      setFieldError(fieldErrors, "dayFormat", "must be 0 or 1");
    else
    {
      dayFormat = value;
      dirty.device = true;
    }
  }
  if (!obj["dataSource"].isNull())
  {
    int value = obj["dataSource"].as<int>();
    if (value < DATA_SOURCE_OWM || value > DATA_SOURCE_OPEN_METEO)
      setFieldError(fieldErrors, "dataSource", "must be between 0 and 3");
    else
    {
      setDataSource(value);
      dirty.device = true;
      dirty.weather = true;
    }
  }
  if (!obj["autoRotate"].isNull())
  {
    setAutoRotateEnabled(obj["autoRotate"].as<bool>(), false);
    dirty.device = true;
  }
  if (!obj["rotateIntervalSec"].isNull())
  {
    int value = obj["rotateIntervalSec"].as<int>();
    if (value < 5 || value > 300)
      setFieldError(fieldErrors, "rotateIntervalSec", "must be between 5 and 300");
    else
    {
      setAutoRotateInterval(value, false);
      dirty.device = true;
    }
  }
  if (!obj["manualScreen"].isNull())
  {
    int nextScreen = manualScreen;
    if (obj["manualScreen"].is<int>())
      nextScreen = obj["manualScreen"].as<int>();
    else
      nextScreen = manualScreenValueFromName(obj["manualScreen"].as<String>());
    if (nextScreen < 0 || nextScreen > 6)
      setFieldError(fieldErrors, "manualScreen", "must be a supported screen");
    else
    {
      manualScreen = nextScreen;
      dirty.device = true;
    }
  }

  return fieldErrors.size() == 0;
}

static bool applyAppUnitsSettings(JsonObjectConst obj, JsonObject fieldErrors, AppSettingsDirtyFlags &dirty)
{
  if (obj.isNull())
    return true;

  if (!obj["temperature"].isNull())
  {
    bool ok = false;
    TempUnit value = tempUnitFromName(obj["temperature"].as<String>(), ok);
    if (!ok)
      setFieldError(fieldErrors, "temperature", "must be C or F");
    else
    {
      units.temp = value;
      dirty.unitPrefs = true;
    }
  }
  if (!obj["wind"].isNull())
  {
    bool ok = false;
    WindUnit value = windUnitFromName(obj["wind"].as<String>(), ok);
    if (!ok)
      setFieldError(fieldErrors, "wind", "must be mps, mph, kts, or kph");
    else
    {
      units.wind = value;
      dirty.unitPrefs = true;
    }
  }
  if (!obj["pressure"].isNull())
  {
    bool ok = false;
    PressUnit value = pressUnitFromName(obj["pressure"].as<String>(), ok);
    if (!ok)
      setFieldError(fieldErrors, "pressure", "must be hPa or inHg");
    else
    {
      units.press = value;
      dirty.unitPrefs = true;
    }
  }
  if (!obj["rain"].isNull())
  {
    bool ok = false;
    PrecipUnit value = precipUnitFromName(obj["rain"].as<String>(), ok);
    if (!ok)
      setFieldError(fieldErrors, "rain", "must be mm or in");
    else
    {
      units.precip = value;
      dirty.unitPrefs = true;
    }
  }
  if (!obj["clock24h"].isNull())
  {
    units.clock24h = obj["clock24h"].as<bool>();
    dirty.unitPrefs = true;
    dirty.dateTime = true;
  }

  return fieldErrors.size() == 0;
}

static bool applyAppDisplaySettings(JsonObjectConst obj, JsonObject fieldErrors, AppSettingsDirtyFlags &dirty)
{
  if (obj.isNull())
    return true;

  if (!obj["theme"].isNull())
  {
    int value = obj["theme"].as<int>();
    if (value < 0 || value > 1)
      setFieldError(fieldErrors, "theme", "must be 0 or 1");
    else
    {
      theme = value;
      dirty.display = true;
    }
  }
  if (!obj["autoThemeMode"].isNull())
  {
    int value = obj["autoThemeMode"].as<int>();
    if (value < 0 || value > 2)
      setFieldError(fieldErrors, "autoThemeMode", "must be between 0 and 2");
    else
    {
      autoThemeSchedule = (value == 1);
      autoThemeAmbient = (value == 2);
      dirty.display = true;
    }
  }
  if (!obj["themeLightThreshold"].isNull())
  {
    autoThemeLightThreshold = constrain(obj["themeLightThreshold"].as<int>(), 0, 200000);
    dirty.display = true;
  }
  if (!obj["dayThemeStartMinutes"].isNull())
  {
    dayThemeStartMinutes = normalizeThemeScheduleMinutes(obj["dayThemeStartMinutes"].as<int>());
    dirty.display = true;
  }
  if (!obj["nightThemeStartMinutes"].isNull())
  {
    nightThemeStartMinutes = normalizeThemeScheduleMinutes(obj["nightThemeStartMinutes"].as<int>());
    dirty.display = true;
  }
  if (!obj["brightness"].isNull())
  {
    int value = obj["brightness"].as<int>();
    if (value < 1 || value > 100)
      setFieldError(fieldErrors, "brightness", "must be between 1 and 100");
    else
    {
      brightness = value;
      dirty.display = true;
    }
  }
  if (!obj["autoBrightness"].isNull())
  {
    autoBrightness = obj["autoBrightness"].as<bool>();
    dirty.display = true;
  }
  if (!obj["scrollLevel"].isNull())
  {
    int value = obj["scrollLevel"].as<int>();
    if (value < 0 || value > 9)
      setFieldError(fieldErrors, "scrollLevel", "must be between 0 and 9");
    else
    {
      scrollLevel = value;
      scrollSpeed = scrollDelays[scrollLevel];
      dirty.display = true;
    }
  }
  if (!obj["verticalScrollLevel"].isNull())
  {
    int value = obj["verticalScrollLevel"].as<int>();
    if (value < 0 || value > 9)
      setFieldError(fieldErrors, "verticalScrollLevel", "must be between 0 and 9");
    else
    {
      verticalScrollLevel = value;
      verticalScrollSpeed = scrollDelays[verticalScrollLevel];
      dirty.display = true;
    }
  }
  if (!obj["customMessage"].isNull())
  {
    customMsg = obj["customMessage"].as<String>();
    dirty.display = true;
  }
  if (!obj["sceneClockEnabled"].isNull())
  {
    sceneClockEnabled = obj["sceneClockEnabled"].as<bool>();
    dirty.display = true;
  }
  if (!obj["splashDurationSec"].isNull())
  {
    int value = obj["splashDurationSec"].as<int>();
    if (value < 1 || value > 10)
      setFieldError(fieldErrors, "splashDurationSec", "must be between 1 and 10");
    else
    {
      splashDurationSec = value;
      dirty.display = true;
    }
  }

  return fieldErrors.size() == 0;
}

static bool applyAppWfTempestSettings(JsonObjectConst obj, JsonObject fieldErrors, AppSettingsDirtyFlags &dirty)
{
  if (obj.isNull())
    return true;

  if (!obj["selected"].isNull())
  {
    if (obj["selected"].as<bool>())
      setDataSource(DATA_SOURCE_WEATHERFLOW);
    else if (dataSource == DATA_SOURCE_WEATHERFLOW)
      setDataSource(DATA_SOURCE_OWM);
    dirty.device = true;
    dirty.weather = true;
  }
  if (!obj["token"].isNull())
  {
    String value = obj["token"].as<String>();
    if (!value.isEmpty())
    {
      wfToken = value;
      dirty.weather = true;
    }
  }
  if (!obj["stationId"].isNull())
  {
    wfStationId = obj["stationId"].as<String>();
    dirty.weather = true;
  }

  return fieldErrors.size() == 0;
}

static bool applyAppForecastUiSettings(JsonObjectConst obj, JsonObject fieldErrors, AppSettingsDirtyFlags &dirty)
{
  if (obj.isNull())
    return true;

  if (!obj["linesPerDay"].isNull())
  {
    int value = obj["linesPerDay"].as<int>();
    if (value < 2 || value > 3)
      setFieldError(fieldErrors, "linesPerDay", "must be 2 or 3");
    else
    {
      forecastLinesPerDay = value;
      dirty.display = true;
    }
  }
  if (!obj["pauseMs"].isNull())
  {
    int value = obj["pauseMs"].as<int>();
    if (value < 0 || value > 10000)
      setFieldError(fieldErrors, "pauseMs", "must be between 0 and 10000");
    else
    {
      forecastPauseMs = value;
      dirty.display = true;
    }
  }
  if (!obj["iconSize"].isNull())
  {
    int value = obj["iconSize"].as<int>();
    if (value != 0 && value != 16)
      setFieldError(fieldErrors, "iconSize", "must be 0 or 16");
    else
    {
      forecastIconSize = value;
      dirty.display = true;
    }
  }

  return fieldErrors.size() == 0;
}

static bool applyAppCalibrationSettings(JsonObjectConst obj, JsonObject fieldErrors, AppSettingsDirtyFlags &dirty)
{
  if (obj.isNull())
    return true;

  if (!obj["tempOffset"].isNull())
  {
    float valueC = static_cast<float>(tempOffsetToC(obj["tempOffset"].as<float>()));
    if (valueC < wxv::defaults::kTempOffsetMinC || valueC > wxv::defaults::kTempOffsetMaxC)
      setFieldError(fieldErrors, "tempOffset", "must be within the supported calibration range");
    else
    {
      tempOffset = valueC;
      dirty.calibration = true;
    }
  }
  if (!obj["humidityOffset"].isNull())
  {
    int value = obj["humidityOffset"].as<int>();
    if (value < -50 || value > 50)
      setFieldError(fieldErrors, "humidityOffset", "must be between -50 and 50");
    else
    {
      humOffset = value;
      dirty.calibration = true;
    }
  }
  if (!obj["lightGain"].isNull())
  {
    int value = obj["lightGain"].as<int>();
    if (value < LIGHT_GAIN_MIN || value > LIGHT_GAIN_MAX)
      setFieldError(fieldErrors, "lightGain", "must be between 1 and 300");
    else
    {
      lightGain = value;
      dirty.calibration = true;
    }
  }
  if (!obj["co2Threshold"].isNull())
  {
    int value = obj["co2Threshold"].as<int>();
    if (value < 400 || value > 5000)
      setFieldError(fieldErrors, "co2Threshold", "must be between 400 and 5000");
    else
    {
      envAlertCo2Threshold = value;
      dirty.calibration = true;
    }
  }
  if (!obj["tempThreshold"].isNull())
  {
    float value = obj["tempThreshold"].as<float>();
    float tempC = (units.temp == TempUnit::F)
                      ? static_cast<float>((value - 32.0f) * 5.0f / 9.0f)
                      : value;
    if (tempC < 10.0f || tempC > 50.0f)
      setFieldError(fieldErrors, "tempThreshold", "must be between 10C and 50C equivalent");
    else
    {
      envAlertTempThresholdC = tempC;
      dirty.calibration = true;
    }
  }
  if (!obj["humidityLowThreshold"].isNull())
  {
    int value = obj["humidityLowThreshold"].as<int>();
    if (value < 0 || value > 100)
      setFieldError(fieldErrors, "humidityLowThreshold", "must be between 0 and 100");
    else
    {
      envAlertHumidityLowThreshold = value;
      dirty.calibration = true;
    }
  }
  if (!obj["humidityHighThreshold"].isNull())
  {
    int value = obj["humidityHighThreshold"].as<int>();
    if (value < 0 || value > 100)
      setFieldError(fieldErrors, "humidityHighThreshold", "must be between 0 and 100");
    else
    {
      envAlertHumidityHighThreshold = value;
      dirty.calibration = true;
    }
  }
  if (envAlertHumidityLowThreshold > envAlertHumidityHighThreshold)
    setFieldError(fieldErrors, "humidityLowThreshold", "must be less than or equal to humidityHighThreshold");

  return fieldErrors.size() == 0;
}

static bool applyAppAlarmsSettings(JsonObjectConst obj, JsonObject fieldErrors, AppSettingsDirtyFlags &dirty)
{
  if (obj.isNull())
    return true;

  if (obj["items"].is<JsonArrayConst>())
  {
    JsonArrayConst alarms = obj["items"].as<JsonArrayConst>();
    for (JsonObjectConst item : alarms)
    {
      int index = item["index"] | -1;
      if (index < 0 || index >= 3)
      {
        setFieldError(fieldErrors, "items", "alarm index must be between 0 and 2");
        continue;
      }
      if (!item["enabled"].isNull())
        alarmEnabled[index] = item["enabled"].as<bool>();
      if (!item["hour"].isNull())
      {
        int value = item["hour"].as<int>();
        if (value < 0 || value > 23)
          setFieldError(fieldErrors, "items.hour", "hour must be between 0 and 23");
        else
          alarmHour[index] = value;
      }
      if (!item["minute"].isNull())
      {
        int value = item["minute"].as<int>();
        if (value < 0 || value > 59)
          setFieldError(fieldErrors, "items.minute", "minute must be between 0 and 59");
        else
          alarmMinute[index] = value;
      }
      if (!item["repeat"].isNull())
      {
        bool ok = false;
        AlarmRepeatMode mode = alarmRepeatModeFromValue(item["repeat"], ok);
        if (!ok)
          setFieldError(fieldErrors, "items.repeat", "repeat must be a supported value");
        else
          alarmRepeatMode[index] = mode;
      }
      if (!item["weekDay"].isNull())
      {
        int value = item["weekDay"].as<int>();
        if (value < 0 || value > 6)
          setFieldError(fieldErrors, "items.weekDay", "weekDay must be between 0 and 6");
        else
          alarmWeeklyDay[index] = value;
      }
      dirty.alarms = true;
    }
  }
  if (!obj["alarmSound"].isNull())
  {
    int value = obj["alarmSound"].as<int>();
    if (value < 0 || value > 4)
      setFieldError(fieldErrors, "alarmSound", "must be between 0 and 4");
    else
    {
      alarmSoundMode = value;
      dirty.alarms = true;
      dirty.device = true;
    }
  }

  return fieldErrors.size() == 0;
}

static bool applyAppNoaaSettings(JsonObjectConst obj, JsonObject fieldErrors, AppSettingsDirtyFlags &dirty)
{
  if (obj.isNull())
    return true;

  if (!obj["enabled"].isNull())
  {
    noaaAlertsEnabled = obj["enabled"].as<bool>();
    dirty.noaa = true;
  }
  if (!obj["latitude"].isNull())
  {
    float value = obj["latitude"].as<float>();
    if (!isfinite(value) || value < -90.0f || value > 90.0f)
      setFieldError(fieldErrors, "latitude", "must be between -90 and 90");
    else
    {
      noaaLatitude = value;
      dirty.noaa = true;
    }
  }
  if (!obj["longitude"].isNull())
  {
    float value = obj["longitude"].as<float>();
    if (!isfinite(value) || value < -180.0f || value > 180.0f)
      setFieldError(fieldErrors, "longitude", "must be between -180 and 180");
    else
    {
      noaaLongitude = value;
      dirty.noaa = true;
    }
  }

  return fieldErrors.size() == 0;
}

static bool applyAppLocationSettings(JsonObjectConst obj, JsonObject fieldErrors, AppSettingsDirtyFlags &dirty)
{
  return applyAppNoaaSettings(obj, fieldErrors, dirty);
}

static bool applyAppDateTimeSettings(JsonObjectConst obj, JsonObject fieldErrors, AppSettingsDirtyFlags &dirty)
{
  if (obj.isNull())
    return true;

  bool timezoneUpdated = false;
  if (!obj["timezone"].isNull())
  {
    String value = obj["timezone"].as<String>();
    value.trim();
    if (value.isEmpty())
    {
      if (!obj["tzOffset"].isNull())
      {
        int offset = obj["tzOffset"].as<int>();
        if (offset < -720 || offset > 840)
          setFieldError(fieldErrors, "tzOffset", "must be between -720 and 840");
        else
        {
          setCustomTimezoneOffset(offset);
          timezoneUpdated = true;
          dirty.dateTime = true;
        }
      }
    }
    else
    {
      int idx = timezoneIndexFromId(value.c_str());
      if (idx < 0)
        setFieldError(fieldErrors, "timezone", "must be a supported timezone");
      else
      {
        selectTimezoneByIndex(idx);
        timezoneUpdated = true;
        dirty.dateTime = true;
      }
    }
  }
  if (!timezoneUpdated && !obj["tzOffset"].isNull())
  {
    int offset = obj["tzOffset"].as<int>();
    if (offset < -720 || offset > 840)
      setFieldError(fieldErrors, "tzOffset", "must be between -720 and 840");
    else
    {
      setCustomTimezoneOffset(offset);
      dirty.dateTime = true;
    }
  }
  if (!obj["autoDst"].isNull())
  {
    setTimezoneAutoDst(obj["autoDst"].as<bool>());
    dirty.dateTime = true;
  }
  if (!obj["dateFormat"].isNull())
  {
    int value = obj["dateFormat"].as<int>();
    if (value < 0 || value > 2)
      setFieldError(fieldErrors, "dateFormat", "must be between 0 and 2");
    else
    {
      dateFmt = value;
      dirty.dateTime = true;
    }
  }
  if (!obj["clock24h"].isNull())
  {
    units.clock24h = obj["clock24h"].as<bool>();
    dirty.unitPrefs = true;
    dirty.dateTime = true;
  }
  if (!obj["ntpPreset"].isNull())
  {
    int value = obj["ntpPreset"].as<int>();
    if (value < 0 || value > NTP_PRESET_CUSTOM)
      setFieldError(fieldErrors, "ntpPreset", "must be a supported preset");
    else
    {
      ntpServerPreset = value;
      if (value != NTP_PRESET_CUSTOM)
      {
        strncpy(ntpServerHost, ntpPresetHost(value), sizeof(ntpServerHost) - 1);
        ntpServerHost[sizeof(ntpServerHost) - 1] = '\0';
      }
      dirty.dateTime = true;
    }
  }
  if (!obj["ntpServer"].isNull())
  {
    String host = obj["ntpServer"].as<String>();
    host.trim();
    if (host.isEmpty())
      host = "pool.ntp.org";
    setNtpServerFromHostString(host);
    dirty.dateTime = true;
  }

  return fieldErrors.size() == 0;
}

static bool applyAppWorldTimeSettings(JsonObjectConst obj, JsonObject fieldErrors, AppSettingsDirtyFlags &dirty)
{
  if (obj.isNull())
    return true;

  bool nextAutoCycle = worldTimeAutoCycleEnabled();
  if (!obj["autoCycle"].isNull())
    nextAutoCycle = obj["autoCycle"].as<bool>();

  bool hasItems = obj["items"].is<JsonArrayConst>();
  bool hasCustom = obj["customCities"].is<JsonArrayConst>();
  std::vector<int> selections;
  std::vector<WorldTimeCustomCity> customCities;

  if (hasItems)
  {
    for (JsonObjectConst item : obj["items"].as<JsonArrayConst>())
    {
      int tzIndex = -1;
      if (!item["timezone"].isNull())
        tzIndex = timezoneIndexFromId(item["timezone"].as<const char *>());
      if (tzIndex < 0 && !item["index"].isNull())
        tzIndex = item["index"].as<int>();
      if (tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount()))
      {
        setFieldError(fieldErrors, "items", "contains an unsupported timezone");
        continue;
      }
      selections.push_back(tzIndex);
    }
  }

  if (hasCustom)
  {
    for (JsonObjectConst item : obj["customCities"].as<JsonArrayConst>())
    {
      String name = item["name"] | "";
      name.trim();
      float lat = item["lat"] | NAN;
      float lon = item["lon"] | NAN;
      int tzIndex = -1;
      if (!item["timezone"].isNull())
        tzIndex = timezoneIndexFromId(item["timezone"].as<const char *>());
      if (tzIndex < 0 && !item["tzIndex"].isNull())
        tzIndex = item["tzIndex"].as<int>();
      if (name.isEmpty() || !isfinite(lat) || !isfinite(lon) || tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount()))
      {
        setFieldError(fieldErrors, "customCities", "contains an invalid custom city");
        continue;
      }
      WorldTimeCustomCity city;
      city.name = name;
      city.lat = constrain(lat, -90.0f, 90.0f);
      city.lon = constrain(lon, -180.0f, 180.0f);
      city.tzIndex = tzIndex;
      city.enabled = item["enabled"].isNull() ? true : item["enabled"].as<bool>();
      customCities.push_back(city);
    }
  }

  if (fieldErrors.size() != 0)
    return false;

  worldTimeSetAutoCycleEnabled(nextAutoCycle);
  if (!hasItems && !hasCustom)
  {
    dirty.worldTime = true;
    return true;
  }

  worldTimeClearSelections();
  worldTimeClearCustomCities();
  for (int tzIndex : selections)
    worldTimeAddTimezone(tzIndex);
  for (const WorldTimeCustomCity &city : customCities)
    worldTimeAddCustomCity(city);
  worldTimeResetView();
  dirty.worldTime = true;
  return fieldErrors.size() == 0;
}

static bool applyAppSoundSettings(JsonObjectConst obj, JsonObject fieldErrors, AppSettingsDirtyFlags &dirty)
{
  if (obj.isNull())
    return true;

  if (!obj["enabled"].isNull())
  {
    bool enabled = obj["enabled"].as<bool>();
    buzzerVolume = enabled ? max(buzzerVolume, 35) : 0;
    dirty.device = true;
  }
  if (!obj["volume"].isNull())
  {
    int value = obj["volume"].as<int>();
    if (value < 0 || value > 100)
      setFieldError(fieldErrors, "volume", "must be between 0 and 100");
    else
    {
      buzzerVolume = value;
      dirty.device = true;
    }
  }
  if (!obj["toneSet"].isNull())
  {
    int value = obj["toneSet"].as<int>();
    if (value < 0 || value > 6)
      setFieldError(fieldErrors, "toneSet", "must be between 0 and 6");
    else
    {
      buzzerToneSet = value;
      dirty.device = true;
    }
  }
  if (!obj["alarmSound"].isNull())
  {
    int value = obj["alarmSound"].as<int>();
    if (value < 0 || value > 4)
      setFieldError(fieldErrors, "alarmSound", "must be between 0 and 4");
    else
    {
      alarmSoundMode = value;
      dirty.device = true;
      dirty.alarms = true;
    }
  }

  return fieldErrors.size() == 0;
}

static bool applyAppSettingsSection(const char *section, JsonVariantConst value, JsonObject fieldErrors, AppSettingsDirtyFlags &dirty)
{
  JsonObjectConst obj = value.as<JsonObjectConst>();
  if (!value.is<JsonObjectConst>())
  {
    setFieldError(fieldErrors, "body", "section payload must be an object");
    return false;
  }

  if (strcmp(section, "device") == 0)
    return applyAppDeviceSettings(obj, fieldErrors, dirty);
  if (strcmp(section, "units") == 0)
    return applyAppUnitsSettings(obj, fieldErrors, dirty);
  if (strcmp(section, "display") == 0)
    return applyAppDisplaySettings(obj, fieldErrors, dirty);
  if (strcmp(section, "wf-tempest") == 0)
    return applyAppWfTempestSettings(obj, fieldErrors, dirty);
  if (strcmp(section, "forecast-ui") == 0)
    return applyAppForecastUiSettings(obj, fieldErrors, dirty);
  if (strcmp(section, "calibration") == 0)
    return applyAppCalibrationSettings(obj, fieldErrors, dirty);
  if (strcmp(section, "alarms") == 0)
    return applyAppAlarmsSettings(obj, fieldErrors, dirty);
  if (strcmp(section, "noaa") == 0)
    return applyAppNoaaSettings(obj, fieldErrors, dirty);
  if (strcmp(section, "location") == 0)
    return applyAppLocationSettings(obj, fieldErrors, dirty);
  if (strcmp(section, "datetime") == 0)
    return applyAppDateTimeSettings(obj, fieldErrors, dirty);
  if (strcmp(section, "world-time") == 0)
    return applyAppWorldTimeSettings(obj, fieldErrors, dirty);
  if (strcmp(section, "sound") == 0)
    return applyAppSoundSettings(obj, fieldErrors, dirty);

  setFieldError(fieldErrors, "section", "unsupported section");
  return false;
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
  case 3:
    return "Open-Meteo";
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
  case SCREEN_WORLD_CLOCK:
    return "World Clock";
  case SCREEN_ASTRONOMY:
    return "Astronomy";
  case SCREEN_SKY_FACTS:
    return "Sky Facts";
  case SCREEN_SKY_BRIEF:
    return "Sky Brief";
  case SCREEN_OWM:
    return "Forecast (OWM)";
  case SCREEN_UDP_DATA:
      return "UDP Live Weather";
  case SCREEN_LIGHTNING:
      return "Lightning";
  case SCREEN_UDP_FORECAST:
    return "UDP Forecast";
  case SCREEN_WIND_DIR:
    return "Wind Direction";
  case SCREEN_ENV_INDEX:
    return "Air Quality";
  case SCREEN_TEMP_HISTORY:
  case SCREEN_HUM_HISTORY:
  case SCREEN_CO2_HISTORY:
  case SCREEN_BARO_HISTORY:
    return "24 Hours";
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

static IRCodes::WxKey irKeyForButton(String btn)
{
  btn.trim();
  btn.toLowerCase();
  if (btn.length() == 0)
    return IRCodes::WxKey::Unknown;

  if (btn == "up")
    return IRCodes::WxKey::Up;
  if (btn == "down")
    return IRCodes::WxKey::Down;
  if (btn == "left")
    return IRCodes::WxKey::Left;
  if (btn == "right")
    return IRCodes::WxKey::Right;
  if (btn == "select" || btn == "enter" || btn == "ok")
    return IRCodes::WxKey::Ok;
  if (btn == "menu" || btn == "setup" || btn == "cancel")
    return IRCodes::WxKey::Cancel;
  if (btn == "screen" || btn == "shutdown" || btn == "power")
    return IRCodes::WxKey::Screen;
  if (btn == "theme" || btn == "color")
    return IRCodes::WxKey::Theme;

  // allow raw hex codes prefixed with 0x or without
  if (btn.startsWith("0x"))
    btn.remove(0, 2);
  if (btn.length() > 0)
  {
    char *endptr = nullptr;
    uint32_t code = strtoul(btn.c_str(), &endptr, 16);
    if (endptr && *endptr == '\0')
      return IRCodes::mapLegacyCodeToKey(code);
  }

  return IRCodes::WxKey::Unknown;
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

static String formatEpochLocalTime(uint32_t epoch)
{
  if (epoch == 0)
    return String("--");
  time_t rawTime = static_cast<time_t>(epoch);
  struct tm *ti = localtime(&rawTime);
  if (!ti)
    return String("--");
  char buf[22];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ti);
  return String(buf);
}

static void ntpSyncTask(void *param)
{
  (void)param;
  bool ok = syncTimeFromNTP();
  g_ntpSyncLastOk = ok;
  g_ntpSyncRunning = false;
  vTaskDelete(nullptr);
}

static bool queueNtpSyncTask()
{
  if (g_ntpSyncRunning)
  {
    return false;
  }

  g_ntpSyncRunning = true;
  g_ntpSyncLastOk = false;
  BaseType_t created = xTaskCreatePinnedToCore(
      ntpSyncTask,
      "wxv-ntp-sync",
      6144,
      nullptr,
      1,
      nullptr,
      0);
  if (created != pdPASS)
  {
    g_ntpSyncRunning = false;
    return false;
  }
  return true;
}

void webTick()
{
  g_appWs.cleanupClients();
  appRuntimeBroadcastTick();

  if (g_webPendingReboot && static_cast<int32_t>(millis() - g_webRebootAtMs) >= 0)
  {
    g_webPendingReboot = false;
    ESP.restart();
    return;
  }

  if (g_webPendingQuickRestore)
  {
    g_webPendingQuickRestore = false;
    quickRestore();
    return;
  }

  if (g_webPendingFactoryReset)
  {
    g_webPendingFactoryReset = false;
    factoryReset();
    return;
  }
}

void setupWebServer() {
  if (webServerRunning) {
    return;
  }
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed!");
    return;
  }

  Preferences appPrefs;
  if (appPrefs.begin("visionwx", true)) {
    g_appLightningEnabled = appPrefs.getBool("appLtngEn", true);
    appPrefs.end();
  }

  // ---------- JSON endpoints ----------
  // Place dynamic endpoints before the catch-all static handler so they aren't shadowed.
  server.on("/trend.json", HTTP_GET, [](AsyncWebServerRequest *req) {
    const auto &log = getSensorLog();
    // Cap payload to keep client/server responsive.
    // Optional query: /trend.json?limit=120
    constexpr size_t kDefaultTrendSamples = 200;
    constexpr size_t kMinTrendSamples = 20;
    constexpr size_t kMaxTrendSamples = 1000;
    size_t requestedSamples = kDefaultTrendSamples;
    if (req->hasParam("limit"))
    {
      String raw = req->getParam("limit")->value();
      long parsed = raw.toInt();
      if (parsed > 0)
      {
        requestedSamples = static_cast<size_t>(parsed);
      }
    }
    if (requestedSamples < kMinTrendSamples)
      requestedSamples = kMinTrendSamples;
    if (requestedSamples > kMaxTrendSamples)
      requestedSamples = kMaxTrendSamples;

    struct TrendChunkState
    {
      const std::vector<SensorSample> *logPtr = nullptr;
      size_t total = 0;
      size_t stride = 1;
      size_t nextIndex = 0;
      size_t emittedCount = 0;
      bool emitLastSample = false;
      bool sentLastSample = false;
      bool sentOpen = false;
      bool sentClose = false;
      String pending;
      size_t pendingOffset = 0;

      static void formatFloat(float value, uint8_t digits, char *out, size_t outLen)
      {
        if (isnan(value) || !isfinite(value))
        {
          snprintf(out, outLen, "null");
          return;
        }
        snprintf(out, outLen, "%.*f", digits, value);
      }

      void buildNextToken()
      {
        pending = "";
        pendingOffset = 0;

        if (!sentOpen)
        {
          pending = "[";
          sentOpen = true;
          return;
        }

        if (logPtr && total > 0 && nextIndex < total)
        {
          const SensorSample &s = (*logPtr)[nextIndex];
          nextIndex += stride;
          char tempBuf[24];
          char humBuf[24];
          char pressBuf[24];
          char luxBuf[24];
          char co2Buf[24];
          formatFloat(s.temp, 5, tempBuf, sizeof(tempBuf));
          formatFloat(s.hum, 5, humBuf, sizeof(humBuf));
          formatFloat(s.press, 3, pressBuf, sizeof(pressBuf));
          formatFloat(s.lux, 6, luxBuf, sizeof(luxBuf));
          if (isnan(s.co2) || !isfinite(s.co2) || s.co2 <= 0.0f)
            snprintf(co2Buf, sizeof(co2Buf), "null");
          else
            formatFloat(s.co2, 3, co2Buf, sizeof(co2Buf));

          char objBuf[220];
          snprintf(objBuf, sizeof(objBuf),
                   "%s{\"ts\":%lu,\"temp\":%s,\"hum\":%s,\"press\":%s,\"lux\":%s,\"co2\":%s}",
                   (emittedCount == 0) ? "" : ",",
                   static_cast<unsigned long>(s.ts),
                   tempBuf,
                   humBuf,
                   pressBuf,
                   luxBuf,
                   co2Buf);
          pending = objBuf;
          ++emittedCount;
          return;
        }

        if (logPtr && total > 0 && emitLastSample && !sentLastSample)
        {
          const SensorSample &s = (*logPtr)[total - 1];
          sentLastSample = true;
          char tempBuf[24];
          char humBuf[24];
          char pressBuf[24];
          char luxBuf[24];
          char co2Buf[24];
          formatFloat(s.temp, 5, tempBuf, sizeof(tempBuf));
          formatFloat(s.hum, 5, humBuf, sizeof(humBuf));
          formatFloat(s.press, 3, pressBuf, sizeof(pressBuf));
          formatFloat(s.lux, 6, luxBuf, sizeof(luxBuf));
          if (isnan(s.co2) || !isfinite(s.co2) || s.co2 <= 0.0f)
            snprintf(co2Buf, sizeof(co2Buf), "null");
          else
            formatFloat(s.co2, 3, co2Buf, sizeof(co2Buf));

          char objBuf[220];
          snprintf(objBuf, sizeof(objBuf),
                   "%s{\"ts\":%lu,\"temp\":%s,\"hum\":%s,\"press\":%s,\"lux\":%s,\"co2\":%s}",
                   (emittedCount == 0) ? "" : ",",
                   static_cast<unsigned long>(s.ts),
                   tempBuf,
                   humBuf,
                   pressBuf,
                   luxBuf,
                   co2Buf);
          pending = objBuf;
          ++emittedCount;
          return;
        }

        if (!sentClose)
        {
          pending = "]";
          sentClose = true;
          return;
        }
      }

      size_t fill(uint8_t *buffer, size_t maxLen)
      {
        if (maxLen == 0)
          return 0;

        size_t written = 0;
        while (written < maxLen)
        {
          if (pendingOffset >= pending.length())
          {
            buildNextToken();
            if (pending.length() == 0)
              break;
          }

          size_t remaining = pending.length() - pendingOffset;
          size_t room = maxLen - written;
          size_t chunk = (remaining < room) ? remaining : room;
          memcpy(buffer + written, pending.c_str() + pendingOffset, chunk);
          pendingOffset += chunk;
          written += chunk;
        }
        return written;
      }
    };

    auto state = std::make_shared<TrendChunkState>();
    state->logPtr = &log;
    state->total = log.size();
    if (state->total > 0 && requestedSamples > 0)
    {
      state->stride = (state->total + requestedSamples - 1) / requestedSamples; // ceil
      if (state->stride < 1)
        state->stride = 1;
      state->emitLastSample = ((state->total - 1) % state->stride) != 0;
    }

    AsyncWebServerResponse *res = req->beginChunkedResponse(
        "application/json",
        [state](uint8_t *buffer, size_t maxLen, size_t index) -> size_t
        {
          (void)index;
          return state->fill(buffer, maxLen);
        });
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
    static String cachedPayload;
    static uint32_t cacheBuiltAt = 0;
    uint32_t now = millis();
    bool refreshCache = cachedPayload.isEmpty() || static_cast<uint32_t>(now - cacheBuiltAt) >= 2000u;
    if (refreshCache) {
      // Capture heap before request-local JSON/String allocations so this aligns
      // with System Info style reporting.
      size_t heapTotal = 327680; // align with System Info modal
      size_t heapFree = ESP.getFreeHeap();
      if (heapFree > heapTotal) {
        heapTotal = heapFree;
      }
      size_t heapUsed = (heapTotal > heapFree) ? (heapTotal - heapFree) : 0;

      JsonDocument doc;
      String dispTemp;
      String humidityValue;
      String conditionsValue = str_Weather_Conditions;
      uint32_t weatherUpdatedEpoch = 0;

      if (isDataSourceForecastModel()) {
        dispTemp = fmtTemp(currentCond.temp, 0);
        humidityValue = (currentCond.humidity >= 0) ? String(currentCond.humidity) : "--";
        if (!currentCond.cond.isEmpty()) conditionsValue = currentCond.cond;
        weatherUpdatedEpoch = currentCond.time;
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
      float luxNow = getLastRawLux();
      doc["lux"] = isfinite(luxNow) ? luxNow : NAN;

      unsigned long uptimeSec = millis() / 1000UL;
      doc["uptimeSec"] = uptimeSec;
      doc["uptime"] = formatUptime(uptimeSec);

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
      doc["humidity"] = (isDataSourceForecastModel() && currentCond.humidity >= 0) ? String(currentCond.humidity) : str_Humd;
      doc["conditions"] = (isDataSourceForecastModel() && currentCond.cond.length() > 0) ? currentCond.cond : str_Weather_Conditions;
      doc["time"] = String(chr_t_hour) + ":" + String(chr_t_minute) + ":" + String(chr_t_second);
      doc["locationLat"] = noaaLatitude;
      doc["locationLon"] = noaaLongitude;
      doc["weatherUpdatedEpoch"] = weatherUpdatedEpoch;
      doc["weatherUpdated"] = formatEpochLocalTime(weatherUpdatedEpoch);

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

      cachedPayload = "";
      serializeJson(doc, cachedPayload);
      cacheBuiltAt = now;
    }

    AsyncWebServerResponse *res = req->beginResponse(200, "application/json", cachedPayload);
    res->addHeader("Cache-Control", "no-store, max-age=0");
    req->send(res);
  });

  server.on("/status-brief.json", HTTP_GET, [](AsyncWebServerRequest *req) {
    static String cachedPayload;
    static uint32_t cacheBuiltAt = 0;
    uint32_t now = millis();
    bool refreshCache = cachedPayload.isEmpty() || static_cast<uint32_t>(now - cacheBuiltAt) >= 5000u;
    if (refreshCache) {
      JsonDocument doc;
      uint32_t weatherUpdatedEpoch = 0;

      String dispTemp;
      if (isDataSourceForecastModel()) {
        dispTemp = fmtTemp(currentCond.temp, 0);
        weatherUpdatedEpoch = currentCond.time;
      } else {
        dispTemp = fmtTemp(atof(str_Temp.c_str()), 0);
      }

      doc["wifiSSID"] = WiFi.SSID();
      doc["ip"] = WiFi.localIP().toString();
      doc["temp"] = dispTemp;
      doc["humidity"] = (isDataSourceForecastModel() && currentCond.humidity >= 0)
        ? String(currentCond.humidity)
        : str_Humd;
      doc["time"] = String(chr_t_hour) + ":" + String(chr_t_minute) + ":" + String(chr_t_second);
      doc["locationLat"] = noaaLatitude;
      doc["locationLon"] = noaaLongitude;
      doc["weatherUpdatedEpoch"] = weatherUpdatedEpoch;
      doc["weatherUpdated"] = formatEpochLocalTime(weatherUpdatedEpoch);

      int currentIdx = timezoneCurrentIndex();
      if (currentIdx >= 0) {
        doc["tzLabel"] = timezoneLabelAt(static_cast<size_t>(currentIdx));
      } else {
        doc["tzLabel"] = "Custom Offset";
      }
      doc["tzOffset"] = tzOffset;

      cachedPayload = "";
      serializeJson(doc, cachedPayload);
      cacheBuiltAt = now;
    }

    AsyncWebServerResponse *res = req->beginResponse(200, "application/json", cachedPayload);
    res->addHeader("Cache-Control", "no-store, max-age=0");
    req->send(res);
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

    bool startScan = !g_wifiScanPrimed || (g_wifiScanIncludeHidden != includeHidden);
    if (startScan)
    {
      WiFi.scanDelete();
      WiFi.scanNetworks(true, includeHidden);
      g_wifiScanPrimed = true;
      g_wifiScanIncludeHidden = includeHidden;
    }

    int found = WiFi.scanComplete();
    if (found == WIFI_SCAN_FAILED)
    {
      WiFi.scanDelete();
      WiFi.scanNetworks(true, includeHidden);
      found = 0;
    }
    bool scanning = (found == WIFI_SCAN_RUNNING || found == -1);
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
    doc["scanning"] = scanning;
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

    if (!scanning)
    {
      WiFi.scanDelete();
    }
    if (restoreMode)
    {
      WiFi.mode(prevMode);
    }

    AsyncResponseStream *res = req->beginResponseStream("application/json");
    serializeJson(doc, *res);
    req->send(res);
  });

  server.on("/ir", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("btn"))
    {
      req->send(400, "text/plain", "missing btn");
      return;
    }

    String btn = req->getParam("btn")->value();
    IRCodes::WxKey key = irKeyForButton(btn);
    if (key == IRCodes::WxKey::Unknown)
    {
      req->send(400, "text/plain", "unknown button");
      return;
    }

    if (!enqueueVirtualIRKey(key))
    {
      req->send(503, "text/plain", "busy");
      return;
    }

    req->send(204);
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

  server.on("/action/quick-restore", HTTP_POST, [](AsyncWebServerRequest *req) {
    g_webPendingQuickRestore = true;
    req->send(202, "application/json", "{\"ok\":true,\"message\":\"Settings reset queued (Wi-Fi + logs kept).\"}");
  });

  server.on("/action/factory-reset", HTTP_POST, [](AsyncWebServerRequest *req) {
    g_webPendingFactoryReset = true;
    req->send(202, "application/json", "{\"ok\":true,\"message\":\"Factory reset queued (erasing Wi-Fi + logs).\"}");
  });

  server.on("/action/reboot", HTTP_POST, [](AsyncWebServerRequest *req) {
    g_webPendingReboot = true;
    g_webRebootAtMs = millis() + 150;
    req->send(202, "application/json", "{\"ok\":true,\"message\":\"Reboot queued.\"}");
  });

  server.on("/action/learn-remote", HTTP_POST, [](AsyncWebServerRequest *req) {
    bool ok = startUniversalRemoteLearning();
    if (!ok) {
      req->send(409, "application/json", "{\"ok\":false,\"error\":\"Remote learning unavailable.\"}");
      return;
    }
    req->send(200, "application/json", "{\"ok\":true,\"message\":\"Remote learning started.\"}");
  });

  server.on("/action/clear-learned-remote", HTTP_POST, [](AsyncWebServerRequest *req) {
    bool ok = clearUniversalRemoteLearning();
    if (!ok) {
      req->send(409, "application/json", "{\"ok\":false,\"error\":\"No learned remote to clear.\"}");
      return;
    }
    req->send(200, "application/json", "{\"ok\":true,\"message\":\"Learned remote cleared.\"}");
  });

  server.on("/action/wifi-signal-test", HTTP_POST, [](AsyncWebServerRequest *req) {
    showWiFiSignalTest();
    req->send(200, "application/json", "{\"ok\":true,\"message\":\"WiFi signal test opened on device.\"}");
  });

  server.on("/action/preview-screens", HTTP_POST, [](AsyncWebServerRequest *req) {
    showScenePreviewModal();
    req->send(200, "application/json", "{\"ok\":true,\"message\":\"Preview screens opened on device.\"}");
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
      JsonObject forecastUi = doc.createNestedObject("forecastUi");
      forecastUi["linesPerDay"] = forecastLinesPerDay;
      forecastUi["pauseMs"] = forecastPauseMs;
      forecastUi["iconSize"] = forecastIconSize;
      doc["customMsg"]        = customMsg;
    doc["buzzerVolume"]     = buzzerVolume;
  doc["buzzerTone"]       = buzzerToneSet;
    doc["alarmSound"]       = alarmSoundMode;
    // Live sensor snapshot
    float luxNow = getLastRawLux();
    if (!isfinite(luxNow)) {
      luxNow = readBrightnessSensor();
    }
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
    doc["envAlertCo2Threshold"] = envAlertCo2Threshold;
    doc["envAlertTempThreshold"] = dispTemp(envAlertTempThresholdC);
    doc["envAlertHumidityLowThreshold"] = envAlertHumidityLowThreshold;
    doc["envAlertHumidityHighThreshold"] = envAlertHumidityHighThreshold;
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
    AsyncResponseStream *res = req->beginResponseStream("application/json");
    serializeJson(doc, *res);
    req->send(res);
  });

  // ---------- POST /settings (chunk-safe) ----------
  server.on("/settings", HTTP_POST,
    [](AsyncWebServerRequest *req) {},   // onRequest
    nullptr,                              // onUpload
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (total > kMaxSettingsBodyBytes) {
          req->send(413, "text/plain", "Payload too large");
          return;
        }
        req->_tempObject = new String();
        ((String*)req->_tempObject)->reserve(total);
      }
      if (!req->_tempObject) {
        return;
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

        bool dirtyDevice = false;
        bool dirtyDisplay = false;
        bool dirtyWeather = false;
        bool dirtyCalibration = false;
        bool dirtyAlarm = false;
        bool dirtyNoaa = false;
        bool dirtyDateTime = false;
        bool dirtyUnits = false;

        // Device
        if (!doc["wifiSSID"].isNull()) {
          wifiSSID = doc["wifiSSID"] | wifiSSID;
          dirtyDevice = true;
        }
        if (!doc["wifiPass"].isNull()) {
          wifiPass = doc["wifiPass"] | wifiPass;
          dirtyDevice = true;
        }
        if (!doc["units"].isNull()) {
          jsonToUnits(doc["units"]);
          dirtyUnits = true;
        }
        fmt24 = units.clock24h ? 1 : 0;   // keep device clock format in sync with Unit card
        if (!doc["dayFormat"].isNull()) {
          dayFormat = doc["dayFormat"] | dayFormat;
          dirtyDevice = true;
        }

        int newSource = dataSource;
        if (!doc["dataSource"].isNull()) {
          newSource = doc["dataSource"].as<int>();
        } else if (!doc["forecastSrc"].isNull()) {
          newSource = doc["forecastSrc"].as<int>();
        }
        setDataSource(newSource);
        if (!doc["dataSource"].isNull() || !doc["forecastSrc"].isNull()) {
          dirtyDevice = true;
          dirtyWeather = true;
        }
        if (!doc["autoRotate"].isNull()) {
          bool autoRotateValue = (doc["autoRotate"] | autoRotate) != 0;
          setAutoRotateEnabled(autoRotateValue, false);
          dirtyDevice = true;
        }
        if (!doc["autoRotateInterval"].isNull()) {
          int newInterval = constrain((int)(doc["autoRotateInterval"] | autoRotateInterval), 5, 300);
          setAutoRotateInterval(newInterval, false);
          dirtyDevice = true;
        }
        if (!doc["manualScreen"].isNull()) {
          manualScreen = doc["manualScreen"] | manualScreen;
          dirtyDevice = true;
        }

        // Display
        if (!doc["theme"].isNull()) {
          theme = doc["theme"] | theme;
          dirtyDisplay = true;
        }
        int incomingAutoThemeMode = -1;
        if (!doc["autoThemeMode"].isNull()) {
          incomingAutoThemeMode = doc["autoThemeMode"].as<int>();
          if (incomingAutoThemeMode < 0) incomingAutoThemeMode = 0;
          if (incomingAutoThemeMode > 2) incomingAutoThemeMode = 2;
          autoThemeSchedule = (incomingAutoThemeMode == 1);
          autoThemeAmbient = (incomingAutoThemeMode == 2);
          dirtyDisplay = true;
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
          dirtyDisplay = true;
        }
        // Enforce mutually-exclusive modes
        if (autoThemeAmbient) {
          autoThemeSchedule = false;
        }
        if (!doc["dayThemeStart"].isNull()) {
          dayThemeStartMinutes = normalizeThemeScheduleMinutes(doc["dayThemeStart"].as<int>());
          dirtyDisplay = true;
        }
        if (!doc["nightThemeStart"].isNull()) {
          nightThemeStartMinutes = normalizeThemeScheduleMinutes(doc["nightThemeStart"].as<int>());
          dirtyDisplay = true;
        }
        if (!doc["themeLightThreshold"].isNull()) {
          int thr = doc["themeLightThreshold"].as<int>();
          autoThemeLightThreshold = constrain(thr, 1, 5000);
          dirtyDisplay = true;
        }
        if (!doc["brightness"].isNull()) {
          brightness = constrain((int)(doc["brightness"] | brightness), 1, 100);
          dirtyDisplay = true;
        }
        if (!doc["autoBrightness"].isNull()) {
          JsonVariant v = doc["autoBrightness"];
          if (v.is<bool>()) autoBrightness = v.as<bool>();
          else if (v.is<int>()) autoBrightness = (v.as<int>() != 0);
          else if (v.is<const char*>()) {
            const char* s = v.as<const char*>();
            autoBrightness = (strcmp(s, "1")==0 || strcasecmp(s, "true")==0);
          }
          dirtyDisplay = true;
        }
          if (!doc["splashDuration"].isNull()) {
            int dur = doc["splashDuration"].as<int>();
            splashDurationSec = constrain(dur, 1, 10);
            dirtyDisplay = true;
          }
          if (!doc["forecastUi"].isNull() && doc["forecastUi"].is<JsonObject>())
          {
            JsonObject f = doc["forecastUi"].as<JsonObject>();
            if (!f["linesPerDay"].isNull())
              forecastLinesPerDay = constrain(f["linesPerDay"].as<int>(), 2, 3);
            if (!f["pauseMs"].isNull())
              forecastPauseMs = constrain(f["pauseMs"].as<int>(), 0, 10000);
            if (!f["iconSize"].isNull())
            {
              int sz = f["iconSize"].as<int>();
              forecastIconSize = (sz == 0) ? 0 : 16;
            }
            dirtyDisplay = true;
          }
          if (!doc["buzzerVolume"].isNull()) {
            buzzerVolume = constrain(doc["buzzerVolume"].as<int>(), 0, 100);
            dirtyDevice = true;
          }
        if (!doc["buzzerTone"].isNull()) {
          buzzerToneSet = constrain(doc["buzzerTone"].as<int>(), 0, 6);
          dirtyDevice = true;
        }
        if (!doc["alarmSound"].isNull()) {
          alarmSoundMode = constrain(doc["alarmSound"].as<int>(), 0, 4);
          dirtyAlarm = true;
          dirtyDevice = true;
        }
        if (!doc["scrollLevel"].isNull()) {
          scrollLevel = constrain((int)(doc["scrollLevel"] | scrollLevel), 0, 9);
          scrollSpeed = scrollDelays[scrollLevel];
          dirtyDisplay = true;
        }
        if (!doc["customMsg"].isNull()) {
          customMsg = doc["customMsg"] | customMsg;
          dirtyDisplay = true;
        }

        if (!doc["ntpPreset"].isNull())
        {
          int preset = doc["ntpPreset"].as<int>();
          if (preset < 0) preset = 0;
          if (preset > NTP_PRESET_CUSTOM) preset = NTP_PRESET_CUSTOM;
          ntpServerPreset = preset;
          dirtyDateTime = true;
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
          dirtyDateTime = true;
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
          dirtyWeather = true;
        }
        if (!doc["owmCountryIndex"].isNull()) {
          int updated = doc["owmCountryIndex"].as<int>();
          if (updated != owmCountryIndex) {
            owmSettingsChanged = true;
          }
          owmCountryIndex = updated;
          dirtyWeather = true;
        }
        if (!doc["owmCountryCustom"].isNull()) {
          String updated = doc["owmCountryCustom"].as<String>();
          updated.trim();
          if (!updated.equals(owmCountryCustom)) {
            owmSettingsChanged = true;
          }
          owmCountryCustom = updated;
          dirtyWeather = true;
        }
        if (!doc["owmApiKey"].isNull()) {
          String updated = doc["owmApiKey"].as<String>();
          updated.trim();
          if (!updated.equals(owmApiKey)) {
            owmSettingsChanged = true;
          }
          owmApiKey = updated;
          dirtyWeather = true;
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
          dirtyWeather = true;
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
          dirtyWeather = true;
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
          tempOffset = constrain(offsetC, wxv::defaults::kTempOffsetMinC, wxv::defaults::kTempOffsetMaxC);
          dirtyCalibration = true;
        }
        if (!doc["humOffset"].isNull()) {
          JsonVariant v = doc["humOffset"];
          humOffset = v.is<int>() ? v.as<int>() : atoi(v.as<const char*>());
          dirtyCalibration = true;
        }
        if (!doc["lightGain"].isNull()) {
          JsonVariant v = doc["lightGain"];
          lightGain = v.is<int>() ? v.as<int>() : atoi(v.as<const char*>());
          dirtyCalibration = true;
        }
        if (!doc["envAlertCo2Threshold"].isNull()) {
          JsonVariant v = doc["envAlertCo2Threshold"];
          envAlertCo2Threshold = v.is<int>() ? v.as<int>() : atoi(v.as<const char*>());
          dirtyCalibration = true;
        }
        if (!doc["envAlertTempThreshold"].isNull()) {
          JsonVariant v = doc["envAlertTempThreshold"];
          double incoming = 0.0;
          if (v.is<double>() || v.is<float>()) {
            incoming = v.as<double>();
          } else if (v.is<int>() || v.is<long>() || v.is<long long>()) {
            incoming = static_cast<double>(v.as<long long>());
          } else if (v.is<const char*>()) {
            incoming = atof(v.as<const char*>());
          }
          envAlertTempThresholdC = (units.temp == TempUnit::F)
                                     ? static_cast<float>((incoming - 32.0) * 5.0 / 9.0)
                                     : static_cast<float>(incoming);
          dirtyCalibration = true;
        }
        if (!doc["envAlertHumidityLowThreshold"].isNull()) {
          JsonVariant v = doc["envAlertHumidityLowThreshold"];
          envAlertHumidityLowThreshold = v.is<int>() ? v.as<int>() : atoi(v.as<const char*>());
          dirtyCalibration = true;
        }
        if (!doc["envAlertHumidityHighThreshold"].isNull()) {
          JsonVariant v = doc["envAlertHumidityHighThreshold"];
          envAlertHumidityHighThreshold = v.is<int>() ? v.as<int>() : atoi(v.as<const char*>());
          dirtyCalibration = true;
        }
        tempOffset = constrain(tempOffset, wxv::defaults::kTempOffsetMinC, wxv::defaults::kTempOffsetMaxC);
        humOffset  = constrain(humOffset, wxv::defaults::kHumOffsetMin, wxv::defaults::kHumOffsetMax);
        lightGain  = constrain(lightGain, wxv::defaults::kLightGainMinPercent, wxv::defaults::kLightGainMaxPercent);
        envAlertCo2Threshold = constrain(envAlertCo2Threshold, 400, 5000);
        envAlertTempThresholdC = constrain(envAlertTempThresholdC, 10.0f, 50.0f);
        envAlertHumidityLowThreshold = constrain(envAlertHumidityLowThreshold, 0, 100);
        envAlertHumidityHighThreshold = constrain(envAlertHumidityHighThreshold, 0, 100);
        if (envAlertHumidityLowThreshold > envAlertHumidityHighThreshold) {
          envAlertHumidityLowThreshold = envAlertHumidityHighThreshold;
        }

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
          dirtyAlarm = true;
        }

        // NOAA
        if (!doc["noaa"].isNull() && doc["noaa"].is<JsonObject>())
        {
          JsonObject noaa = doc["noaa"].as<JsonObject>();
          if (!noaa["enabled"].isNull()) noaaAlertsEnabled = noaa["enabled"].as<bool>();
          if (!noaa["lat"].isNull()) noaaLatitude = noaa["lat"].as<float>();
          if (!noaa["lon"].isNull()) noaaLongitude = noaa["lon"].as<float>();
          dirtyNoaa = true;
        }

        if (dirtyDateTime) saveDateTimeSettings();
        if (dirtyDevice) saveDeviceSettings();
        if (dirtyDisplay) saveDisplaySettings();
        if (dirtyWeather) saveWeatherSettings();
        if (dirtyCalibration) saveCalibrationSettings();
        if (dirtyAlarm) saveAlarmSettings();
        if (dirtyNoaa)
        {
          saveNoaaSettings();
          notifyNoaaSettingsChanged();
        }
        if (dirtyUnits) saveUnits();
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
          requestScrollRebuild();
          reset_Time_and_Date_Display = true;
        }

        if (wfCredsChanged && isDataSourceWeatherFlow()) {
          reset_Time_and_Date_Display = true;
        } else if (isDataSourceOpenMeteo()) {
          reset_Time_and_Date_Display = true;
        }
      }
    }
  );

  // ---------- Timezones / Time ----------
  server.on("/timezones.json", HTTP_GET, [](AsyncWebServerRequest* req){
    static String cachedPayload;
    if (cachedPayload.isEmpty()) {
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
      serializeJson(doc, cachedPayload);
    }
    AsyncWebServerResponse *res = req->beginResponse(200, "application/json", cachedPayload);
    res->addHeader("Cache-Control", "public, max-age=3600");
    req->send(res);
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
    AsyncResponseStream *res = req->beginResponseStream("application/json");
    serializeJson(doc, *res);
    req->send(res);
  });

  server.on("/time", HTTP_POST,
    [](AsyncWebServerRequest*){}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      if (index == 0) {
        if (total > kMaxTimeBodyBytes) {
          req->send(413, "text/plain", "Payload too large");
          return;
        }
        req->_tempObject = new String();
        ((String*)req->_tempObject)->reserve(total);
      }
      if (!req->_tempObject) {
        return;
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

  server.on("/worldtime.json", HTTP_GET, [](AsyncWebServerRequest* req){
    JsonDocument doc;
    JsonArray ids = doc["ids"].to<JsonArray>();
    JsonArray selections = doc["selections"].to<JsonArray>();
    size_t count = worldTimeSelectionCount();
    for (size_t i = 0; i < count; ++i) {
      int tzIndex = worldTimeSelectionAt(i);
      if (tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount())) {
        continue;
      }
      const TimezoneInfo& tz = timezoneInfoAt(static_cast<size_t>(tzIndex));
      ids.add(tz.id);
      JsonObject item = selections.add<JsonObject>();
      item["id"] = tz.id;
      item["index"] = tzIndex;
      item["city"] = tz.city;
      item["label"] = timezoneLabelAt(static_cast<size_t>(tzIndex));
    }
    doc["autoCycle"] = worldTimeAutoCycleEnabled();
    JsonArray customCities = doc["customCities"].to<JsonArray>();
    for (size_t i = 0; i < worldTimeCustomCityCount(); ++i) {
      WorldTimeCustomCity city;
      if (!worldTimeGetCustomCity(i, city)) continue;
      JsonObject c = customCities.add<JsonObject>();
      c["name"] = city.name;
      c["lat"] = city.lat;
      c["lon"] = city.lon;
      c["enabled"] = city.enabled;
      c["tzIndex"] = city.tzIndex;
      if (city.tzIndex >= 0 && city.tzIndex < static_cast<int>(timezoneCount())) {
        c["tzId"] = timezoneInfoAt(static_cast<size_t>(city.tzIndex)).id;
        c["tzLabel"] = timezoneLabelAt(static_cast<size_t>(city.tzIndex));
      }
    }
    doc["count"] = selections.size();
    AsyncResponseStream *res = req->beginResponseStream("application/json");
    serializeJson(doc, *res);
    req->send(res);
  });

  server.on("/worldtime", HTTP_POST,
    [](AsyncWebServerRequest*){}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      if (index == 0) {
        if (total > kMaxWorldTimeBodyBytes) {
          req->send(413, "text/plain", "Payload too large");
          return;
        }
        req->_tempObject = new String();
        ((String*)req->_tempObject)->reserve(total);
      }
      if (!req->_tempObject) {
        return;
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

        worldTimeClearSelections();
        worldTimeClearCustomCities();
        if (!doc["autoCycle"].isNull()) {
          worldTimeSetAutoCycleEnabled(doc["autoCycle"].as<bool>());
        }

        if (doc["ids"].is<JsonArray>()) {
          JsonArray ids = doc["ids"].as<JsonArray>();
          for (JsonVariant v : ids) {
            if (!v.is<const char*>()) continue;
            const char* tzId = v.as<const char*>();
            int tzIndex = timezoneIndexFromId(tzId);
            if (tzIndex >= 0) {
              worldTimeAddTimezone(tzIndex);
            }
          }
        } else if (doc["indices"].is<JsonArray>()) {
          JsonArray indices = doc["indices"].as<JsonArray>();
          for (JsonVariant v : indices) {
            if (!v.is<int>()) continue;
            int tzIndex = v.as<int>();
            if (tzIndex >= 0 && tzIndex < static_cast<int>(timezoneCount())) {
              worldTimeAddTimezone(tzIndex);
            }
          }
        }

        if (doc["customCities"].is<JsonArray>()) {
          JsonArray custom = doc["customCities"].as<JsonArray>();
          for (JsonObject obj : custom) {
            String name = obj["name"] | "";
            name.trim();
            if (name.length() == 0) continue;
            float lat = obj["lat"] | NAN;
            float lon = obj["lon"] | NAN;
            if (!isfinite(lat) || !isfinite(lon)) continue;
            lat = constrain(lat, -90.0f, 90.0f);
            lon = constrain(lon, -180.0f, 180.0f);
            int tzIndex = -1;
            if (!obj["tzId"].isNull()) {
              tzIndex = timezoneIndexFromId(obj["tzId"].as<const char*>());
            }
            if (tzIndex < 0 && !obj["tzIndex"].isNull()) {
              tzIndex = obj["tzIndex"].as<int>();
            }
            if (tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount())) continue;
            WorldTimeCustomCity city;
            city.name = name;
            city.lat = lat;
            city.lon = lon;
            city.tzIndex = tzIndex;
            city.enabled = obj["enabled"].isNull() ? true : obj["enabled"].as<bool>();
            worldTimeAddCustomCity(city);
          }
        }

        worldTimeResetView();
        saveWorldTimeSettings();
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

    bool started = queueNtpSyncTask();
    if (started)
    {
      req->send(202, "application/json", "{\"ok\":true,\"queued\":true}");
      return;
    }

    req->send(200, "application/json", g_ntpSyncRunning
                                       ? "{\"ok\":true,\"queued\":false,\"busy\":true}"
                                       : (g_ntpSyncLastOk
                                              ? "{\"ok\":true,\"queued\":false,\"lastResult\":true}"
                                       : "{\"ok\":false,\"queued\":false,\"lastResult\":false}"));
  });

  server.on("/api/app/state", HTTP_GET, [](AsyncWebServerRequest *req) {
    refreshAppRuntimeState();
    sendAppJson(req, g_appRuntime.stateJson);
  });

  server.on("/api/app/dashboard", HTTP_GET, [](AsyncWebServerRequest *req) {
    refreshAppRuntimeState();
    sendAppJson(req, g_appRuntime.dashboardJson);
  });

  server.on("/api/app/alerts", HTTP_GET, [](AsyncWebServerRequest *req) {
    refreshAppRuntimeState();
    sendAppJson(req, g_appRuntime.alertsJson);
  });

  server.on("/api/app/lightning", HTTP_GET, [](AsyncWebServerRequest *req) {
    refreshAppRuntimeState();
    sendAppJson(req, g_appRuntime.lightningJson);
  });

  server.on("/api/app/device", HTTP_GET, [](AsyncWebServerRequest *req) {
    refreshAppRuntimeState();
    sendAppJson(req, g_appRuntime.deviceJson);
  });

  auto registerAppSettingsGet = [&](const char *path, const char *section) {
    server.on(path, HTTP_GET, [section](AsyncWebServerRequest *req) {
      sendAppJson(req, buildAppSettingsJson(section));
    });
  };

  auto registerAppSettingsPost = [&](const char *path, const char *section) {
    server.on(path, HTTP_POST,
      [](AsyncWebServerRequest *req) {},
      nullptr,
      [section](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0) {
          if (total > kMaxSettingsBodyBytes) {
            req->send(413, "text/plain", "Payload too large");
            return;
          }
          req->_tempObject = new String();
          static_cast<String *>(req->_tempObject)->reserve(total);
        }
        if (!req->_tempObject) {
          return;
        }
        String *body = static_cast<String *>(req->_tempObject);
        body->concat(reinterpret_cast<const char *>(data), len);
        if (index + len != total) {
          return;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, *body);
        delete body;
        req->_tempObject = nullptr;
        if (err || !doc.is<JsonObject>()) {
          req->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
          return;
        }

        JsonDocument errorDoc;
        JsonObject fieldErrors = errorDoc.to<JsonObject>();
        AppSettingsDirtyFlags dirty;
        applyAppSettingsSection(section, doc.as<JsonObjectConst>(), fieldErrors, dirty);
        if (fieldErrors.size() != 0) {
          sendAppValidationError(req, fieldErrors);
          return;
        }

        persistAppSettingsChanges(dirty);
        refreshAppRuntimeState(true);
        broadcastAppSettingsUpdate(section);

        JsonDocument resp;
        resp["ok"] = true;
        resp["saved"] = true;
        resp["applied"] = true;
        resp["section"] = section;
        String payload;
        serializeJson(resp, payload);
        req->send(200, "application/json", payload);
      }
    );
  };

  registerAppSettingsGet("/api/app/settings/device", "device");
  registerAppSettingsGet("/api/app/settings/units", "units");
  registerAppSettingsGet("/api/app/settings/display", "display");
  registerAppSettingsGet("/api/app/settings/wf-tempest", "wf-tempest");
  registerAppSettingsGet("/api/app/settings/forecast-ui", "forecast-ui");
  registerAppSettingsGet("/api/app/settings/calibration", "calibration");
  registerAppSettingsGet("/api/app/settings/alarms", "alarms");
  registerAppSettingsGet("/api/app/settings/noaa", "noaa");
  registerAppSettingsGet("/api/app/settings/location", "location");
  registerAppSettingsGet("/api/app/settings/datetime", "datetime");
  registerAppSettingsGet("/api/app/settings/world-time", "world-time");
  registerAppSettingsGet("/api/app/settings/sound", "sound");

  server.on("/api/app/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
    sendAppJson(req, buildAppSettingsJson());
  });

  registerAppSettingsPost("/api/app/settings/device", "device");
  registerAppSettingsPost("/api/app/settings/units", "units");
  registerAppSettingsPost("/api/app/settings/display", "display");
  registerAppSettingsPost("/api/app/settings/wf-tempest", "wf-tempest");
  registerAppSettingsPost("/api/app/settings/forecast-ui", "forecast-ui");
  registerAppSettingsPost("/api/app/settings/calibration", "calibration");
  registerAppSettingsPost("/api/app/settings/alarms", "alarms");
  registerAppSettingsPost("/api/app/settings/noaa", "noaa");
  registerAppSettingsPost("/api/app/settings/location", "location");
  registerAppSettingsPost("/api/app/settings/datetime", "datetime");
  registerAppSettingsPost("/api/app/settings/world-time", "world-time");
  registerAppSettingsPost("/api/app/settings/sound", "sound");

  server.on("/api/app/remote", HTTP_POST,
    [](AsyncWebServerRequest *req) {},
    nullptr,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (total > kMaxAppBodyBytes) {
          req->send(413, "text/plain", "Payload too large");
          return;
        }
        req->_tempObject = new String();
        static_cast<String *>(req->_tempObject)->reserve(total);
      }
      if (!req->_tempObject) {
        return;
      }
      String *body = static_cast<String *>(req->_tempObject);
      body->concat(reinterpret_cast<const char *>(data), len);
      if (index + len != total) {
        return;
      }

      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, *body);
      delete body;
      req->_tempObject = nullptr;
      if (err) {
        req->send(400, "text/plain", "Invalid JSON");
        return;
      }

      String button = doc["button"].as<String>();
      button.trim();
      if (button.isEmpty()) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"missing button\"}");
        return;
      }
      if (!enqueueAppButton(button)) {
        req->send(503, "application/json", "{\"ok\":false,\"error\":\"busy or invalid button\"}");
        return;
      }

      JsonDocument resp;
      resp["ok"] = true;
      resp["button"] = button;
      resp["queued"] = true;
      String payload;
      serializeJson(resp, payload);
      req->send(200, "application/json", payload);
    }
  );

  server.on("/api/app/settings", HTTP_POST,
    [](AsyncWebServerRequest *req) {},
    nullptr,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (total > kMaxSettingsBodyBytes) {
          req->send(413, "text/plain", "Payload too large");
          return;
        }
        req->_tempObject = new String();
        static_cast<String *>(req->_tempObject)->reserve(total);
      }
      if (!req->_tempObject) {
        return;
      }
      String *body = static_cast<String *>(req->_tempObject);
      body->concat(reinterpret_cast<const char *>(data), len);
      if (index + len != total) {
        return;
      }

      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, *body);
      delete body;
      req->_tempObject = nullptr;
      if (err || !doc.is<JsonObject>()) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
        return;
      }

      JsonDocument errorsDoc;
      JsonObject allErrors = errorsDoc.to<JsonObject>();
      AppSettingsDirtyFlags dirty;
      JsonObjectConst root = doc.as<JsonObjectConst>();
      for (JsonPairConst pair : root) {
        const char *section = pair.key().c_str();
        if (!isKnownAppSettingsSection(section))
          continue;

        JsonDocument sectionErrorsDoc;
        JsonObject sectionErrors = sectionErrorsDoc.to<JsonObject>();
        applyAppSettingsSection(section, pair.value(), sectionErrors, dirty);
        if (sectionErrors.size() != 0) {
          JsonObject out = allErrors[section].to<JsonObject>();
          for (JsonPair item : sectionErrors)
            out[item.key().c_str()] = item.value().as<const char *>();
        }
      }

      if (allErrors.size() != 0) {
        JsonDocument resp;
        resp["ok"] = false;
        resp["error"] = "validation_failed";
        JsonObject out = resp["fieldErrors"].to<JsonObject>();
        for (JsonPair pair : allErrors) {
          JsonObject sectionOut = out[pair.key().c_str()].to<JsonObject>();
          for (JsonPairConst item : pair.value().as<JsonObjectConst>())
            sectionOut[item.key().c_str()] = item.value().as<const char *>();
        }
        String payload;
        serializeJson(resp, payload);
        req->send(400, "application/json", payload);
        return;
      }

      persistAppSettingsChanges(dirty);
      refreshAppRuntimeState(true);
      for (JsonPairConst pair : root) {
        const char *section = pair.key().c_str();
        if (isKnownAppSettingsSection(section))
          broadcastAppSettingsUpdate(section);
      }
      req->send(200, "application/json", "{\"ok\":true,\"saved\":true,\"applied\":true}");
    }
  );

  server.on("/api/app/action", HTTP_POST,
    [](AsyncWebServerRequest *req) {},
    nullptr,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (total > kMaxAppBodyBytes) {
          req->send(413, "text/plain", "Payload too large");
          return;
        }
        req->_tempObject = new String();
        static_cast<String *>(req->_tempObject)->reserve(total);
      }
      if (!req->_tempObject) {
        return;
      }
      String *body = static_cast<String *>(req->_tempObject);
      body->concat(reinterpret_cast<const char *>(data), len);
      if (index + len != total) {
        return;
      }

      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, *body);
      delete body;
      req->_tempObject = nullptr;
      if (err) {
        req->send(400, "text/plain", "Invalid JSON");
        return;
      }

      String action = doc["action"].as<String>();
      action.trim();
      action.toLowerCase();

      bool ok = false;
      bool queued = false;
      if (action == "sync_ntp") {
        ok = true;
        queued = queueNtpSyncTask();
      } else if (action == "refresh_alerts") {
        NoaaManualFetchResult result = requestNoaaManualFetch();
        ok = (result == NOAA_MANUAL_FETCH_STARTED || result == NOAA_MANUAL_FETCH_BUSY);
        queued = (result == NOAA_MANUAL_FETCH_STARTED);
      } else if (action == "reboot") {
        g_webPendingReboot = true;
        g_webRebootAtMs = millis() + 150;
        ok = true;
        queued = true;
      } else if (action == "reconnect_wifi") {
        wifiMarkManualConnect();
        ok = startBackgroundWifiReconnect(isAccessPointActive());
        queued = ok;
      } else if (action == "save_settings") {
        saveAllSettings();
        saveWorldTimeSettings();
        ok = true;
      } else if (action == "restore_defaults") {
        g_webPendingQuickRestore = true;
        ok = true;
        queued = true;
      }

      JsonDocument resp;
      resp["ok"] = ok;
      resp["action"] = action;
      resp["queued"] = queued;
      if (!ok) {
        resp["error"] = "unsupported action";
      }
      String payload;
      serializeJson(resp, payload);
      req->send(ok ? 200 : 400, "application/json", payload);
    }
  );

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
    g_webPendingReboot = true;
    g_webRebootAtMs = millis() + 150;
    req->send(202, "text/plain", "Reboot queued.");
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
          wxv::notify::showNotification(wxv::notify::NotifyId::Upgrading, dma_display->color565(0, 255, 255));
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

  g_appWs.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                     void *arg, uint8_t *data, size_t len) {
    (void)server;
    if (type == WS_EVT_CONNECT) {
      client->text(appHelloMessage());
      return;
    }
    if (type == WS_EVT_DATA) {
      AwsFrameInfo *info = reinterpret_cast<AwsFrameInfo *>(arg);
      if (info && info->final && info->index == 0 && info->opcode == WS_TEXT && len == 4 &&
          memcmp(data, "ping", 4) == 0) {
        client->text("{\"type\":\"pong\"}");
      }
    }
  });
  server.addHandler(&g_appWs);

  server.begin();
  webServerRunning = true;
  Serial.println("[Web] Async server started.");
}




















