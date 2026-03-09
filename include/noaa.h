#pragma once

#include <Arduino.h>

struct NwsAlert
{
    String url;
    String id;
    String sent;
    String effective;
    String onset;
    String event;
    String status;
    String messageType;
    String category;
    String severity;
    String certainty;
    String urgency;
    String areaDesc;
    String sender;
    String senderName;
    String headline;
    String description;
    String instruction;
    String response;
    String note;
    String expires;
    String ends;
    String scope;
    String language;
    String web;
};

enum NoaaManualFetchResult
{
    NOAA_MANUAL_FETCH_STARTED = 0,
    NOAA_MANUAL_FETCH_OFF,
    NOAA_MANUAL_FETCH_BUSY,
    NOAA_MANUAL_FETCH_BLOCKED
};

void initNoaaAlerts();
void tickNoaaAlerts(unsigned long nowMs);
void showNoaaAlertScreen();
void refreshNoaaAlertsForScreenEntry();
void notifyNoaaSettingsChanged();
NoaaManualFetchResult requestNoaaManualFetch();
bool noaaHasActiveAlert();
bool noaaHasUnreadAlert();
bool noaaFetchInProgress();
uint16_t noaaActiveColor();
size_t noaaAlertCount();
bool noaaGetAlert(size_t index, NwsAlert &out);
String noaaLastCheckHHMM();
