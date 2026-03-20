#pragma once

#include <Arduino.h>

#ifndef WXV_ENABLE_NOAA_ALERTS
#define WXV_ENABLE_NOAA_ALERTS 1
#endif

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

#if WXV_ENABLE_NOAA_ALERTS
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
#else
inline void initNoaaAlerts() {}
inline void tickNoaaAlerts(unsigned long) {}
inline void showNoaaAlertScreen() {}
inline void refreshNoaaAlertsForScreenEntry() {}
inline void notifyNoaaSettingsChanged() {}
inline NoaaManualFetchResult requestNoaaManualFetch() { return NOAA_MANUAL_FETCH_OFF; }
inline bool noaaHasActiveAlert() { return false; }
inline bool noaaHasUnreadAlert() { return false; }
inline bool noaaFetchInProgress() { return false; }
inline uint16_t noaaActiveColor() { return 0; }
inline size_t noaaAlertCount() { return 0; }
inline bool noaaGetAlert(size_t, NwsAlert &) { return false; }
inline String noaaLastCheckHHMM() { return "--:--"; }
#endif
