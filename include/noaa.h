#pragma once

#include <Arduino.h>

void initNoaaAlerts();
void tickNoaaAlerts(unsigned long nowMs);
void showNoaaAlertScreen();
void notifyNoaaSettingsChanged();
