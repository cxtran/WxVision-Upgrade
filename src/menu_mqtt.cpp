#include <Arduino.h>
#include <cstring>

#include "config.h"
#include "menu.h"
#include "mqtt_client.h"
#include "settings.h"

void showMqttSettingsModal()
{
    if (currentMenuLevel != MENU_NONE)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_MQTT;
    menuActive = true;

    static int mqttEnabledTemp = 0;
    static int mqttTempPublishTemp = 0;
    static int mqttHumidityPublishTemp = 0;
    static int mqttCo2PublishTemp = 0;
    static int mqttPressurePublishTemp = 0;
    static int mqttLightPublishTemp = 0;
    static int mqttPortTemp = MQTT_PORT;
    static char mqttHostBuf[48];
    static char mqttUserBuf[32];
    static char mqttPassBuf[32];
    static char mqttDeviceIdBuf[24];

    mqttEnabledTemp = mqttEnabled ? 1 : 0;
    mqttTempPublishTemp = mqttPublishTemp ? 1 : 0;
    mqttHumidityPublishTemp = mqttPublishHumidity ? 1 : 0;
    mqttCo2PublishTemp = mqttPublishCO2 ? 1 : 0;
    mqttPressurePublishTemp = mqttPublishPressure ? 1 : 0;
    mqttLightPublishTemp = mqttPublishLight ? 1 : 0;
    mqttPortTemp = mqttPort;

    strncpy(mqttHostBuf, mqttHost.c_str(), sizeof(mqttHostBuf) - 1);
    mqttHostBuf[sizeof(mqttHostBuf) - 1] = '\0';
    strncpy(mqttUserBuf, mqttUser.c_str(), sizeof(mqttUserBuf) - 1);
    mqttUserBuf[sizeof(mqttUserBuf) - 1] = '\0';
    strncpy(mqttPassBuf, mqttPass.c_str(), sizeof(mqttPassBuf) - 1);
    mqttPassBuf[sizeof(mqttPassBuf) - 1] = '\0';
    strncpy(mqttDeviceIdBuf, mqttDeviceId.c_str(), sizeof(mqttDeviceIdBuf) - 1);
    mqttDeviceIdBuf[sizeof(mqttDeviceIdBuf) - 1] = '\0';

    static const char *toggleOpts[] = {"Off", "On"};
    String labels[] = {
        "MQTT Enabled",
        "Publish Temp",
        "Publish Humidity",
        "Publish CO2",
        "Publish Pressure",
        "Publish Ambient",
        "Broker Host",
        "Broker Port",
        "Username",
        "Password",
        "Device ID"};
    InfoFieldType types[] = {
        InfoChooser,
        InfoChooser,
        InfoChooser,
        InfoChooser,
        InfoChooser,
        InfoChooser,
        InfoText,
        InfoNumber,
        InfoText,
        InfoText,
        InfoText};

    int *chooserRefs[] = {
        &mqttEnabledTemp,
        &mqttTempPublishTemp,
        &mqttHumidityPublishTemp,
        &mqttCo2PublishTemp,
        &mqttPressurePublishTemp,
        &mqttLightPublishTemp};
    const char *const *chooserOpts[] = {
        toggleOpts,
        toggleOpts,
        toggleOpts,
        toggleOpts,
        toggleOpts,
        toggleOpts};
    int chooserCounts[] = {2, 2, 2, 2, 2, 2};

    int *numberRefs[] = {&mqttPortTemp};

    char *textRefs[] = {
        mqttHostBuf,
        mqttUserBuf,
        mqttPassBuf,
        mqttDeviceIdBuf};
    int textSizes[] = {
        static_cast<int>(sizeof(mqttHostBuf)),
        static_cast<int>(sizeof(mqttUserBuf)),
        static_cast<int>(sizeof(mqttPassBuf)),
        static_cast<int>(sizeof(mqttDeviceIdBuf))};

    mqttModal.setLines(labels, types, 11);
    mqttModal.setValueRefs(numberRefs, 1, chooserRefs, 6, chooserOpts, chooserCounts, textRefs, 4, textSizes);
    mqttModal.setShowNumberArrows(true);

    NumberFieldConfig portCfg;
    portCfg.step = 1;
    portCfg.minValue = 1;
    portCfg.maxValue = 65535;
    portCfg.hasBounds = true;
    portCfg.accelerateOnHold = true;
    mqttModal.setNumberFieldConfig(7, portCfg);

    mqttModal.setCallback([](bool, int) {
        mqttEnabled = (mqttEnabledTemp > 0);
        mqttPublishTemp = (mqttTempPublishTemp > 0);
        mqttPublishHumidity = (mqttHumidityPublishTemp > 0);
        mqttPublishCO2 = (mqttCo2PublishTemp > 0);
        mqttPublishPressure = (mqttPressurePublishTemp > 0);
        mqttPublishLight = (mqttLightPublishTemp > 0);
        mqttHost = String(mqttHostBuf);
        mqttHost.trim();
        mqttPort = static_cast<uint16_t>(constrain(mqttPortTemp, 1, 65535));
        mqttUser = String(mqttUserBuf);
        mqttUser.trim();
        mqttPass = String(mqttPassBuf);
        mqttPass.trim();
        mqttDeviceId = String(mqttDeviceIdBuf);
        mqttDeviceId.trim();
        if (mqttDeviceId.isEmpty())
            mqttDeviceId = MQTT_DEVICE_ID;

        saveMqttSettings();
        mqttApplySettings();

        mqttModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    });

    mqttModal.show();
}
