#pragma once

void mqttInit();
void mqttLoop();
bool mqttIsConnected();
void mqttPublishAllStates();
void mqttPublishDiscovery();
void mqttRefreshDiscoveryForEnabledSensors();
void mqttApplySettings();
void mqttOnWifiConnected();
void mqttOnWifiDisconnected();
