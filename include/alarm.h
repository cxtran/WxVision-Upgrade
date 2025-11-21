#pragma once

#include <Arduino.h>
#include <RTClib.h>
#include "settings.h"

// Initializes runtime alarm state once settings are loaded.
void initAlarmModule();

// Re-evaluates one-shot arming and resets runtime flags.
void refreshAlarmArming();
void notifyAlarmSettingsChanged();

// Update flash/active state using the latest local time.
void tickAlarmState(const DateTime &now);

// Query helpers for display logic.
bool isAlarmCurrentlyActive();
bool isAlarmFlashVisible();
