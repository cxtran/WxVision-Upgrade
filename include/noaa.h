#pragma once

#include <Arduino.h>

void initNoaaAlerts();
void tickNoaaAlerts(unsigned long nowMs);
void showNoaaAlertScreen();
void notifyNoaaSettingsChanged();
bool noaaHasActiveAlert();
uint16_t noaaActiveColor();
