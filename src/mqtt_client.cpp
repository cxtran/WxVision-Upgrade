#include "mqtt_client.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <math.h>

#include "config.h"
#include "display.h"
#include "keyboard.h"
#include "menu.h"
#include "noaa.h"
#include "screen_manager.h"
#include "sensors.h"
#include "settings.h"
#include "system.h"

namespace
{
WiFiClient s_mqttNetClient;
PubSubClient s_mqttClient(s_mqttNetClient);

StaticJsonDocument<768> s_discoveryDoc;
char s_discoveryPayload[MQTT_BUFFER_SIZE];

constexpr char kAvailabilityTopic[] = "wxvision/status";
constexpr char kTempStateTopic[] = "wxvision/state/temp";
constexpr char kHumidityStateTopic[] = "wxvision/state/humidity";
constexpr char kCo2StateTopic[] = "wxvision/state/co2";
constexpr char kPressureStateTopic[] = "wxvision/state/pressure";
constexpr char kLightStateTopic[] = "wxvision/state/light";
constexpr char kRssiStateTopic[] = "wxvision/system/rssi";
constexpr char kHeapStateTopic[] = "wxvision/system/free_heap";
constexpr char kUptimeStateTopic[] = "wxvision/system/uptime";
constexpr char kFwVersionStateTopic[] = "wxvision/system/fw_version";
constexpr char kNoaaActiveTopic[] = "wxvision/alert/noaa_active";
constexpr char kNoaaCountTopic[] = "wxvision/alert/noaa_count";
constexpr char kNoaaHeadlineTopic[] = "wxvision/alert/noaa_headline";
constexpr char kBrightnessStateTopic[] = "wxvision/display/brightness/state";
constexpr char kBrightnessSetTopic[] = "wxvision/display/brightness/set";
constexpr char kAutoBrightnessStateTopic[] = "wxvision/display/auto_brightness/state";
constexpr char kAutoBrightnessSetTopic[] = "wxvision/display/auto_brightness/set";
constexpr char kDisplayEnabledStateTopic[] = "wxvision/display/enabled/state";
constexpr char kDisplayEnabledSetTopic[] = "wxvision/display/enabled/set";
constexpr char kScreenCmdTopic[] = "wxvision/cmd/screen";
constexpr char kRestartCmdTopic[] = "wxvision/cmd/restart";
constexpr char kMqttConnectedTopic[] = "wxvision/system/mqtt_connected";

constexpr char kDiscTemp[] = "homeassistant/sensor/wxvision_temp/config";
constexpr char kDiscHumidity[] = "homeassistant/sensor/wxvision_humidity/config";
constexpr char kDiscCo2[] = "homeassistant/sensor/wxvision_co2/config";
constexpr char kDiscPressure[] = "homeassistant/sensor/wxvision_pressure/config";
constexpr char kDiscLight[] = "homeassistant/sensor/wxvision_light/config";
constexpr char kDiscRssi[] = "homeassistant/sensor/wxvision_rssi/config";
constexpr char kDiscHeap[] = "homeassistant/sensor/wxvision_free_heap/config";
constexpr char kDiscUptime[] = "homeassistant/sensor/wxvision_uptime/config";
constexpr char kDiscFwVersion[] = "homeassistant/sensor/wxvision_fw_version/config";
constexpr char kDiscNoaaActive[] = "homeassistant/binary_sensor/wxvision_noaa_alert_active/config";
constexpr char kDiscNoaaCount[] = "homeassistant/sensor/wxvision_noaa_alert_count/config";
constexpr char kDiscNoaaHeadline[] = "homeassistant/sensor/wxvision_noaa_alert_headline/config";
constexpr char kDiscBrightness[] = "homeassistant/number/wxvision_brightness/config";
constexpr char kDiscAutoBrightness[] = "homeassistant/switch/wxvision_auto_brightness/config";
constexpr char kDiscDisplayEnabled[] = "homeassistant/switch/wxvision_display_enabled/config";
constexpr char kDiscRestart[] = "homeassistant/button/wxvision_restart/config";
constexpr char kDiscMqttConnected[] = "homeassistant/binary_sensor/wxvision_mqtt_connected/config";

constexpr unsigned long kDiagnosticsIntervalMs = 60000UL;
constexpr unsigned long kAlertIntervalMs = 5000UL;
constexpr size_t kNoaaHeadlineMaxLen = 48;
constexpr char kFirmwareVersion[] = __DATE__;

bool s_discoveryPublished = false;
bool s_seenWifiSinceBoot = false;
unsigned long s_lastConnectAttemptMs = 0;
unsigned long s_lastSensorPublishMs = 0;
unsigned long s_lastDiagnosticsPublishMs = 0;
unsigned long s_lastAlertPublishMs = 0;
int s_lastPublishedBrightness = -1;
int s_lastPublishedAutoBrightness = -1;
int s_lastPublishedDisplayEnabled = -1;
bool s_lastPublishedNoaaActive = false;
int s_lastPublishedNoaaCount = -1;
char s_lastPublishedNoaaHeadline[kNoaaHeadlineMaxLen + 1] = {0};

bool publishRetained(const char *topic, const char *payload)
{
    if (!s_mqttClient.connected())
        return false;
    return s_mqttClient.publish(topic, payload, true);
}

void addAvailability(JsonObject root)
{
    root["availability_topic"] = kAvailabilityTopic;
    root["payload_available"] = "online";
    root["payload_not_available"] = "offline";
}

void addDevice(JsonObject root)
{
    JsonObject device = root["device"].to<JsonObject>();
    JsonArray identifiers = device["identifiers"].to<JsonArray>();
    identifiers.add(mqttDeviceId.c_str());
    device["name"] = "WxVision";
    device["manufacturer"] = "Photokia";
    device["model"] = "WxVision";
    device["sw_version"] = __DATE__;
}

bool publishDiscoveryDoc(const char *topic)
{
    size_t len = serializeJson(s_discoveryDoc, s_discoveryPayload, sizeof(s_discoveryPayload));
    if (len == 0 || len >= sizeof(s_discoveryPayload))
        return false;
    return publishRetained(topic, s_discoveryPayload);
}

bool publishSensorDiscovery(const char *topic,
                            const char *name,
                            const char *uniqueId,
                            const char *stateTopic,
                            const char *unit,
                            const char *deviceClass,
                            const char *stateClass = nullptr,
                            const char *entityCategory = nullptr,
                            const char *icon = nullptr)
{
    s_discoveryDoc.clear();
    JsonObject root = s_discoveryDoc.to<JsonObject>();
    root["name"] = name;
    root["unique_id"] = uniqueId;
    root["state_topic"] = stateTopic;
    if (unit)
        root["unit_of_measurement"] = unit;
    if (deviceClass)
        root["device_class"] = deviceClass;
    if (stateClass)
        root["state_class"] = stateClass;
    if (entityCategory)
        root["entity_category"] = entityCategory;
    if (icon)
        root["icon"] = icon;
    addAvailability(root);
    addDevice(root);
    return publishDiscoveryDoc(topic);
}

bool publishBinarySensorDiscovery(const char *topic,
                                  const char *name,
                                  const char *uniqueId,
                                  const char *stateTopic,
                                  const char *payloadOn,
                                  const char *payloadOff,
                                  const char *deviceClass,
                                  const char *entityCategory = nullptr,
                                  const char *icon = nullptr)
{
    s_discoveryDoc.clear();
    JsonObject root = s_discoveryDoc.to<JsonObject>();
    root["name"] = name;
    root["unique_id"] = uniqueId;
    root["state_topic"] = stateTopic;
    root["payload_on"] = payloadOn;
    root["payload_off"] = payloadOff;
    if (deviceClass)
        root["device_class"] = deviceClass;
    if (entityCategory)
        root["entity_category"] = entityCategory;
    if (icon)
        root["icon"] = icon;
    addAvailability(root);
    addDevice(root);
    return publishDiscoveryDoc(topic);
}

bool publishSwitchDiscovery(const char *topic,
                            const char *name,
                            const char *uniqueId,
                            const char *stateTopic,
                            const char *commandTopic,
                            const char *icon)
{
    s_discoveryDoc.clear();
    JsonObject root = s_discoveryDoc.to<JsonObject>();
    root["name"] = name;
    root["unique_id"] = uniqueId;
    root["state_topic"] = stateTopic;
    root["command_topic"] = commandTopic;
    root["payload_on"] = "ON";
    root["payload_off"] = "OFF";
    root["state_on"] = "ON";
    root["state_off"] = "OFF";
    root["entity_category"] = "config";
    root["icon"] = icon;
    addAvailability(root);
    addDevice(root);
    return publishDiscoveryDoc(topic);
}

bool publishNumberDiscovery(const char *topic,
                            const char *name,
                            const char *uniqueId,
                            const char *stateTopic,
                            const char *commandTopic,
                            const char *icon)
{
    s_discoveryDoc.clear();
    JsonObject root = s_discoveryDoc.to<JsonObject>();
    root["name"] = name;
    root["unique_id"] = uniqueId;
    root["state_topic"] = stateTopic;
    root["command_topic"] = commandTopic;
    root["entity_category"] = "config";
    root["icon"] = icon;
    root["mode"] = "slider";
    root["min"] = 1;
    root["max"] = 100;
    root["step"] = 1;
    addAvailability(root);
    addDevice(root);
    return publishDiscoveryDoc(topic);
}

bool publishButtonDiscovery(const char *topic,
                            const char *name,
                            const char *uniqueId,
                            const char *commandTopic,
                            const char *icon)
{
    s_discoveryDoc.clear();
    JsonObject root = s_discoveryDoc.to<JsonObject>();
    root["name"] = name;
    root["unique_id"] = uniqueId;
    root["command_topic"] = commandTopic;
    root["payload_press"] = "PRESS";
    root["entity_category"] = "config";
    root["icon"] = icon;
    addAvailability(root);
    addDevice(root);
    return publishDiscoveryDoc(topic);
}

float currentTemperatureC()
{
    if (!isnan(SCD40_temp))
        return SCD40_temp + tempOffset;
    return NAN;
}

float currentHumidityPercent()
{
    if (!isnan(SCD40_hum))
        return constrain(SCD40_hum + humOffset, 0.0f, 100.0f);
    return NAN;
}

float currentPressureHpa()
{
    return (!isnan(bmp280_pressure) && bmp280_pressure > 200.0f) ? bmp280_pressure : NAN;
}

int currentCo2Ppm()
{
    return (SCD40_co2 > 0) ? static_cast<int>(SCD40_co2) : -1;
}

float currentAmbientLightLux()
{
    float rawLux = getLastRawLux();
    if (!isnan(rawLux))
        return rawLux;
    return readBrightnessSensor();
}

void publishSensorStates()
{
    char payload[24];

    if (mqttPublishTemp)
    {
        float value = currentTemperatureC();
        if (!isnan(value))
        {
            snprintf(payload, sizeof(payload), "%.1f", value);
            publishRetained(kTempStateTopic, payload);
        }
    }

    if (mqttPublishHumidity)
    {
        float value = currentHumidityPercent();
        if (!isnan(value))
        {
            snprintf(payload, sizeof(payload), "%.1f", value);
            publishRetained(kHumidityStateTopic, payload);
        }
    }

    if (mqttPublishCO2)
    {
        int value = currentCo2Ppm();
        if (value > 0)
        {
            snprintf(payload, sizeof(payload), "%d", value);
            publishRetained(kCo2StateTopic, payload);
        }
    }

    if (mqttPublishPressure)
    {
        float value = currentPressureHpa();
        if (!isnan(value))
        {
            snprintf(payload, sizeof(payload), "%.1f", value);
            publishRetained(kPressureStateTopic, payload);
        }
    }

    if (mqttPublishLight)
    {
        float value = currentAmbientLightLux();
        if (!isnan(value))
        {
            snprintf(payload, sizeof(payload), "%d", static_cast<int>(lroundf(value)));
            publishRetained(kLightStateTopic, payload);
        }
    }
}

void publishDiagnostics(bool force)
{
    if (!s_mqttClient.connected())
        return;

    char payload[24];
    snprintf(payload, sizeof(payload), "%d", WiFi.RSSI());
    publishRetained(kRssiStateTopic, payload);
    snprintf(payload, sizeof(payload), "%u", ESP.getFreeHeap());
    publishRetained(kHeapStateTopic, payload);
    snprintf(payload, sizeof(payload), "%lu", millis() / 1000UL);
    publishRetained(kUptimeStateTopic, payload);
    if (force)
    {
        publishRetained(kFwVersionStateTopic, kFirmwareVersion);
        publishRetained(kMqttConnectedTopic, "online");
    }
}

void publishDisplayState(bool force)
{
    int brightnessNow = constrain(brightness, 1, 100);
    int autoBrightnessNow = autoBrightness ? 1 : 0;
    int displayEnabledNow = isScreenOff() ? 0 : 1;
    char payload[16];

    if (force || brightnessNow != s_lastPublishedBrightness)
    {
        snprintf(payload, sizeof(payload), "%d", brightnessNow);
        publishRetained(kBrightnessStateTopic, payload);
        s_lastPublishedBrightness = brightnessNow;
    }

    if (force || autoBrightnessNow != s_lastPublishedAutoBrightness)
    {
        publishRetained(kAutoBrightnessStateTopic, autoBrightnessNow ? "ON" : "OFF");
        s_lastPublishedAutoBrightness = autoBrightnessNow;
    }

    if (force || displayEnabledNow != s_lastPublishedDisplayEnabled)
    {
        publishRetained(kDisplayEnabledStateTopic, displayEnabledNow ? "ON" : "OFF");
        s_lastPublishedDisplayEnabled = displayEnabledNow;
    }
}

void publishAlertState(bool force)
{
    bool noaaActive = noaaHasActiveAlert();
    int noaaCount = static_cast<int>(noaaAlertCount());
    char headline[kNoaaHeadlineMaxLen + 1] = {0};

    if (noaaCount > 0)
    {
        NwsAlert firstAlert;
        if (noaaGetAlert(0, firstAlert))
        {
            const char *source = firstAlert.headline.length() ? firstAlert.headline.c_str()
                                                              : (firstAlert.event.length() ? firstAlert.event.c_str() : "NOAA ALERT");
            strncpy(headline, source, sizeof(headline) - 1);
            headline[sizeof(headline) - 1] = '\0';
        }
    }

    if (force || noaaActive != s_lastPublishedNoaaActive)
    {
        publishRetained(kNoaaActiveTopic, noaaActive ? "ON" : "OFF");
        s_lastPublishedNoaaActive = noaaActive;
    }
    if (force || noaaCount != s_lastPublishedNoaaCount)
    {
        char payload[16];
        snprintf(payload, sizeof(payload), "%d", noaaCount);
        publishRetained(kNoaaCountTopic, payload);
        s_lastPublishedNoaaCount = noaaCount;
    }
    if (force || strcmp(headline, s_lastPublishedNoaaHeadline) != 0)
    {
        publishRetained(kNoaaHeadlineTopic, headline);
        strncpy(s_lastPublishedNoaaHeadline, headline, sizeof(s_lastPublishedNoaaHeadline) - 1);
        s_lastPublishedNoaaHeadline[sizeof(s_lastPublishedNoaaHeadline) - 1] = '\0';
    }
}

bool parseOnOffPayload(const char *payload, bool &valueOut)
{
    if (!payload)
        return false;
    if (strcasecmp(payload, "1") == 0 || strcasecmp(payload, "true") == 0 || strcasecmp(payload, "on") == 0)
    {
        valueOut = true;
        return true;
    }
    if (strcasecmp(payload, "0") == 0 || strcasecmp(payload, "false") == 0 || strcasecmp(payload, "off") == 0)
    {
        valueOut = false;
        return true;
    }
    return false;
}

void dismissUiForRemoteControl()
{
    inKeyboardMode = false;
    sysInfoModal.hide();
    wifiInfoModal.hide();
    dateModal.hide();
    mainMenuModal.hide();
    deviceModal.hide();
    wifiSettingsModal.hide();
    displayModal.hide();
    alarmModal.hide();
    mqttModal.hide();
    noaaModal.hide();
    weatherModal.hide();
    tempestModal.hide();
    calibrationModal.hide();
    systemModal.hide();
    locationModal.hide();
    worldTimeModal.hide();
    manageTzModal.hide();
    scenePreviewModal.hide();
    setupPromptModal.hide();
    menuStack.clear();
    menuActive = false;
    wifiSelecting = false;
    currentMenuLevel = MENU_NONE;
}

void applyScreenCommand(const char *screenId)
{
    if (!screenId || !*screenId)
        return;

    ScreenMode target = currentScreen;
    if (strcasecmp(screenId, "clock") == 0)
        target = SCREEN_CLOCK;
    else if (strcasecmp(screenId, "forecast") == 0)
        target = screenIsAllowed(SCREEN_UDP_FORECAST) ? SCREEN_UDP_FORECAST : SCREEN_OWM;
    else if (strcasecmp(screenId, "weather") == 0)
        target = screenIsAllowed(SCREEN_CURRENT) ? SCREEN_CURRENT : SCREEN_OWM;
    else if (strcasecmp(screenId, "air") == 0)
        target = SCREEN_ENV_INDEX;
    else if (strcasecmp(screenId, "astronomy") == 0)
        target = SCREEN_ASTRONOMY;
    else if (strcasecmp(screenId, "hourly") == 0)
        target = SCREEN_HOURLY;
    else if (strcasecmp(screenId, "lightning") == 0)
        target = SCREEN_LIGHTNING;
    else if (strcasecmp(screenId, "wind") == 0)
        target = SCREEN_WIND_DIR;
    else if (strcasecmp(screenId, "world") == 0 || strcasecmp(screenId, "worldclock") == 0)
        target = SCREEN_WORLD_CLOCK;
    else if (strcasecmp(screenId, "scene") == 0)
        target = SCREEN_CONDITION_SCENE;
    else if (strcasecmp(screenId, "noaa") == 0)
        target = SCREEN_NOAA_ALERT;
    else
        return;

    dismissUiForRemoteControl();
    transitionToScreen(enforceAllowedScreen(target));
}

void handleBrightnessCommand(const char *payload)
{
    int requested = constrain(atoi(payload), 1, 100);
    autoBrightness = false;
    brightness = requested;
    if (!isScreenOff())
        setPanelBrightness(map(brightness, 1, 100, 3, 255));
    saveDisplaySettings();
    publishDisplayState(true);
}

void handleAutoBrightnessCommand(const char *payload)
{
    bool enabled = autoBrightness;
    if (!parseOnOffPayload(payload, enabled))
        return;

    autoBrightness = enabled;
    if (autoBrightness)
        setDisplayBrightnessFromLux(readBrightnessSensor());
    else if (!isScreenOff())
        setPanelBrightness(map(brightness, 1, 100, 3, 255));
    saveDisplaySettings();
    publishDisplayState(true);
}

void handleDisplayEnabledCommand(const char *payload)
{
    bool enabled = !isScreenOff();
    if (!parseOnOffPayload(payload, enabled))
        return;
    setScreenOff(!enabled);
    publishDisplayState(true);
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    char buffer[64];
    unsigned int copyLen = (length < sizeof(buffer) - 1U) ? length : (sizeof(buffer) - 1U);
    memcpy(buffer, payload, copyLen);
    buffer[copyLen] = '\0';

    if (strcmp(topic, kBrightnessSetTopic) == 0)
        handleBrightnessCommand(buffer);
    else if (strcmp(topic, kAutoBrightnessSetTopic) == 0)
        handleAutoBrightnessCommand(buffer);
    else if (strcmp(topic, kDisplayEnabledSetTopic) == 0)
        handleDisplayEnabledCommand(buffer);
    else if (strcmp(topic, kScreenCmdTopic) == 0)
        applyScreenCommand(buffer);
    else if (strcmp(topic, kRestartCmdTopic) == 0 && strcasecmp(buffer, "PRESS") == 0)
        ESP.restart();
}

bool connectClient()
{
    if (!mqttEnabled || WiFi.status() != WL_CONNECTED || mqttHost.isEmpty())
        return false;

    unsigned long now = millis();
    if ((now - s_lastConnectAttemptMs) < MQTT_RETRY_INTERVAL_MS)
        return false;
    s_lastConnectAttemptMs = now;

    s_mqttClient.setServer(mqttHost.c_str(), mqttPort);
    bool connected = mqttUser.isEmpty()
                         ? s_mqttClient.connect(mqttDeviceId.c_str(), kAvailabilityTopic, 0, true, "offline")
                         : s_mqttClient.connect(mqttDeviceId.c_str(), mqttUser.c_str(), mqttPass.c_str(),
                                                kAvailabilityTopic, 0, true, "offline");
    if (!connected)
        return false;

    s_mqttClient.subscribe(kBrightnessSetTopic);
    s_mqttClient.subscribe(kAutoBrightnessSetTopic);
    s_mqttClient.subscribe(kDisplayEnabledSetTopic);
    s_mqttClient.subscribe(kScreenCmdTopic);
    s_mqttClient.subscribe(kRestartCmdTopic);

    s_discoveryPublished = false;
    s_lastSensorPublishMs = 0;
    s_lastDiagnosticsPublishMs = 0;
    s_lastAlertPublishMs = 0;
    return true;
}
} // namespace

void mqttInit()
{
    s_mqttClient.setClient(s_mqttNetClient);
    s_mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
    s_mqttClient.setKeepAlive(MQTT_KEEPALIVE_SEC);
    s_mqttClient.setCallback(mqttCallback);
    s_mqttNetClient.setTimeout(1);
}

bool mqttIsConnected()
{
    return s_mqttClient.connected();
}

void mqttPublishDiscovery()
{
    if (!s_mqttClient.connected() || !mqttEnabled)
        return;

    if (mqttPublishTemp)
        publishSensorDiscovery(kDiscTemp, "Temperature", "wxvision_temp", kTempStateTopic, "\xC2\xB0""C", "temperature", "measurement");
    if (mqttPublishHumidity)
        publishSensorDiscovery(kDiscHumidity, "Humidity", "wxvision_humidity", kHumidityStateTopic, "%", "humidity", "measurement");
    if (mqttPublishCO2)
        publishSensorDiscovery(kDiscCo2, "CO2", "wxvision_co2", kCo2StateTopic, "ppm", "carbon_dioxide", "measurement");
    if (mqttPublishPressure)
        publishSensorDiscovery(kDiscPressure, "Pressure", "wxvision_pressure", kPressureStateTopic, "hPa", "atmospheric_pressure", "measurement");
    if (mqttPublishLight)
        publishSensorDiscovery(kDiscLight, "Light", "wxvision_light", kLightStateTopic, "lx", "illuminance", "measurement");

    publishSensorDiscovery(kDiscRssi, "WiFi RSSI", "wxvision_rssi", kRssiStateTopic, "dBm", "signal_strength", "measurement", "diagnostic");
    publishSensorDiscovery(kDiscHeap, "Free Heap", "wxvision_free_heap", kHeapStateTopic, "B", nullptr, "measurement", "diagnostic");
    publishSensorDiscovery(kDiscUptime, "Uptime", "wxvision_uptime", kUptimeStateTopic, "s", nullptr, "total_increasing", "diagnostic");
    publishSensorDiscovery(kDiscFwVersion, "Firmware Version", "wxvision_fw_version", kFwVersionStateTopic, nullptr, nullptr, nullptr, "diagnostic", "mdi:chip");
    publishBinarySensorDiscovery(kDiscNoaaActive, "NOAA Alert Active", "wxvision_noaa_alert_active", kNoaaActiveTopic, "ON", "OFF", "problem");
    publishSensorDiscovery(kDiscNoaaCount, "NOAA Alert Count", "wxvision_noaa_alert_count", kNoaaCountTopic, nullptr, nullptr, "measurement", nullptr, "mdi:alert");
    publishSensorDiscovery(kDiscNoaaHeadline, "NOAA Alert Headline", "wxvision_noaa_alert_headline", kNoaaHeadlineTopic, nullptr, nullptr, nullptr, nullptr, "mdi:alert-outline");
    publishNumberDiscovery(kDiscBrightness, "Brightness", "wxvision_brightness", kBrightnessStateTopic, kBrightnessSetTopic, "mdi:brightness-6");
    publishSwitchDiscovery(kDiscAutoBrightness, "Auto Brightness", "wxvision_auto_brightness", kAutoBrightnessStateTopic, kAutoBrightnessSetTopic, "mdi:brightness-auto");
    publishSwitchDiscovery(kDiscDisplayEnabled, "Display Enabled", "wxvision_display_enabled", kDisplayEnabledStateTopic, kDisplayEnabledSetTopic, "mdi:monitor");
    publishButtonDiscovery(kDiscRestart, "Restart", "wxvision_restart", kRestartCmdTopic, "mdi:restart");
    publishBinarySensorDiscovery(kDiscMqttConnected, "MQTT Connected", "wxvision_mqtt_connected", kMqttConnectedTopic, "online", "offline", "connectivity", "diagnostic");

    s_discoveryPublished = true;
}

void mqttPublishAllStates()
{
    if (!s_mqttClient.connected() || !mqttEnabled)
        return;
    publishSensorStates();
    publishDiagnostics(true);
    publishDisplayState(true);
    publishAlertState(true);
    publishRetained(kAvailabilityTopic, "online");
    publishRetained(kMqttConnectedTopic, "online");
}

void mqttRefreshDiscoveryForEnabledSensors()
{
    mqttPublishDiscovery();
}

void mqttApplySettings()
{
    mqttDeviceId.trim();
    if (mqttDeviceId.isEmpty())
        mqttDeviceId = MQTT_DEVICE_ID;
    mqttHost.trim();
    mqttUser.trim();
    mqttPort = static_cast<uint16_t>(constrain(static_cast<unsigned int>(mqttPort), 1U, 65535U));

    s_mqttClient.setServer(mqttHost.c_str(), mqttPort);
    s_discoveryPublished = false;
    s_lastConnectAttemptMs = 0;

    if (!mqttEnabled && s_mqttClient.connected())
    {
        publishRetained(kMqttConnectedTopic, "offline");
        publishRetained(kAvailabilityTopic, "offline");
        s_mqttClient.disconnect();
    }
}

void mqttOnWifiConnected()
{
    s_seenWifiSinceBoot = true;
    s_lastConnectAttemptMs = 0;
}

void mqttOnWifiDisconnected()
{
    if (!s_seenWifiSinceBoot)
        return;
    s_discoveryPublished = false;
    publishRetained(kMqttConnectedTopic, "offline");
    s_mqttClient.disconnect();
}

void mqttLoop()
{
    if (!mqttEnabled || WiFi.status() != WL_CONNECTED)
        return;

    if (!s_mqttClient.connected() && !connectClient())
        return;

    s_mqttClient.loop();

    if (!s_discoveryPublished)
    {
        mqttPublishDiscovery();
        mqttPublishAllStates();
        s_lastSensorPublishMs = millis();
        s_lastDiagnosticsPublishMs = s_lastSensorPublishMs;
        s_lastAlertPublishMs = s_lastSensorPublishMs;
        return;
    }

    unsigned long now = millis();
    publishDisplayState(false);

    if ((now - s_lastSensorPublishMs) >= MQTT_PUB_INTERVAL_MS)
    {
        s_lastSensorPublishMs = now;
        publishSensorStates();
    }

    if ((now - s_lastDiagnosticsPublishMs) >= kDiagnosticsIntervalMs)
    {
        s_lastDiagnosticsPublishMs = now;
        publishDiagnostics(false);
    }

    if ((now - s_lastAlertPublishMs) >= kAlertIntervalMs)
    {
        s_lastAlertPublishMs = now;
        publishAlertState(false);
    }
}
