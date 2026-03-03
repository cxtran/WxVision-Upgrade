#pragma once

#include <Arduino.h>

struct NwsAlert
{
    String event;
    String severity;
    String urgency;
    String areaDesc;
    String headline;
    String description;
    String instruction;
    String expires;
};

void initNoaaAlerts();
void tickNoaaAlerts(unsigned long nowMs);
void showNoaaAlertScreen();
void notifyNoaaSettingsChanged();
bool noaaHasActiveAlert();
uint16_t noaaActiveColor();
size_t noaaAlertCount();
bool noaaGetAlert(size_t index, NwsAlert &out);
String noaaLastCheckHHMM();
