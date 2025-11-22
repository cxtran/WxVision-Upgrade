#include "noaa.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Arduino_JSON.h>
#include "settings.h"
#include "display.h"
#include "InfoScreen.h"

extern InfoScreen noaaAlertScreen;

static bool s_hasAlert = false;
static bool s_screenDirty = true;
static bool s_forceImmediateFetch = false;
static bool s_prevEnabled = false;
static String s_event;
static String s_severity;
static String s_description;
static String s_instruction;
static unsigned long s_lastFetchAttempt = 0;

static constexpr unsigned long NOAA_FETCH_INTERVAL_MS = 15UL * 60UL * 1000UL;

static bool isJsonUndefined(JSONVar value)
{
    return JSON.stringify(value) == "undefined";
}

static bool isJsonArray(JSONVar value)
{
    if (isJsonUndefined(value))
        return false;
    String repr = JSON.stringify(value);
    repr.trim();
    return repr.startsWith("[");
}

static String cleanJsonString(JSONVar value)
{
    if (isJsonUndefined(value))
    {
        return "";
    }

    String raw = JSON.stringify(value);
    raw.trim();
    if (raw.equalsIgnoreCase("null"))
    {
        return "";
    }

    if (raw.length() >= 2 && raw[0] == '"' && raw[raw.length() - 1] == '"')
    {
        raw = raw.substring(1, raw.length() - 1);
    }

    raw.replace("\\n", " ");
    raw.replace("\\r", " ");
    raw.replace("\\\"", "\"");
    while (raw.indexOf("  ") != -1)
    {
        raw.replace("  ", " ");
    }
    raw.trim();
    return raw;
}

static uint16_t severityColor(const String &severity)
{
    if (!dma_display)
        return 0;

    String lower = severity;
    lower.toLowerCase();
    if (lower == "extreme")
        return dma_display->color565(255, 64, 64);
    if (lower == "severe")
        return dma_display->color565(255, 140, 0);
    if (lower == "moderate")
        return dma_display->color565(255, 220, 120);
    if (lower == "minor")
        return dma_display->color565(120, 200, 255);
    return dma_display->color565(180, 180, 200);
}

static void applyNoaaLines(bool forceReset)
{
    String lines[3];
    if (!noaaAlertsEnabled)
    {
        lines[0] = "Alerts Disabled";
        lines[1] = "Severity: --";
        lines[2] = "Enable NOAA alerts in menu.";
    }
    else if (!s_hasAlert)
    {
        lines[0] = "No Active Alert";
        lines[1] = "Severity: None";
        lines[2] = "Monitoring NOAA feed...";
    }
    else
    {
        lines[0] = s_event.length() ? s_event : "Alert";
        lines[1] = "Severity: " + (s_severity.length() ? s_severity : "Unknown");
        String marquee = s_description;
        if (s_instruction.length())
        {
            if (marquee.length())
                marquee += " | ";
            marquee += s_instruction;
        }
        if (marquee.length() == 0)
            marquee = "No description provided.";
        lines[2] = marquee;
    }

    uint16_t colors[3] = {0};
    bool useColors = false;
    if (noaaAlertsEnabled && s_hasAlert && dma_display)
    {
        colors[0] = dma_display->color565(255, 255, 255);
        colors[1] = severityColor(s_severity);
        colors[2] = dma_display->color565(150, 200, 255);
        useColors = true;
    }

    bool resetPosition = forceReset || !noaaAlertScreen.isActive();
    noaaAlertScreen.setLines(lines, 3, resetPosition, useColors ? colors : nullptr);
}

static void handleNoaaPayload(const String &payload)
{
    JSONVar doc = JSON.parse(payload);
    if (isJsonUndefined(doc))
    {
        Serial.println("[NOAA] Failed to parse JSON");
        return;
    }

    JSONVar features = doc["features"];
    if (!isJsonArray(features) || features.length() == 0)
    {
        if (s_hasAlert)
        {
            s_hasAlert = false;
            s_event = "";
            s_severity = "";
            s_description = "";
            s_instruction = "";
            s_screenDirty = true;
        }
        return;
    }

    JSONVar first = features[0];
    JSONVar props = first["properties"];

    s_event = cleanJsonString(props["event"]);
    if (s_event.length() == 0)
        s_event = "NOAA Alert";

    s_severity = cleanJsonString(props["severity"]);
    s_description = cleanJsonString(props["description"]);
    s_instruction = cleanJsonString(props["instruction"]);
    if (s_description.length() == 0)
        s_description = "No description provided.";

    s_hasAlert = true;
    s_screenDirty = true;
}

static void fetchNoaaAlert()
{
    s_lastFetchAttempt = millis();
    bool forced = s_forceImmediateFetch;
    s_forceImmediateFetch = false;

    String url = "https://api.weather.gov/alerts/active?point=" +
                 String(noaaLatitude, 4) + "," + String(noaaLongitude, 4);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(12000);
    http.setUserAgent("VisionWX NOAA/1.0 (visionwx.local)");
    http.addHeader("Accept", "application/geo+json");

    if (!http.begin(client, url))
    {
        Serial.println("[NOAA] Failed to begin HTTP connection");
        return;
    }

    int code = http.GET();
    if (code == HTTP_CODE_OK)
    {
        String payload = http.getString();
        handleNoaaPayload(payload);
    }
    else
    {
        Serial.printf("[NOAA] HTTP error: %d\n", code);
        if (forced)
        {
            s_screenDirty = true;
        }
    }
    http.end();
}

void initNoaaAlerts()
{
    s_prevEnabled = noaaAlertsEnabled;
    s_screenDirty = true;
    s_hasAlert = false;
    s_lastFetchAttempt = 0;
    s_forceImmediateFetch = noaaAlertsEnabled;
    applyNoaaLines(true);
}

void tickNoaaAlerts(unsigned long nowMs)
{
    if (s_prevEnabled != noaaAlertsEnabled)
    {
        s_prevEnabled = noaaAlertsEnabled;
        s_screenDirty = true;
        if (noaaAlertsEnabled)
            s_forceImmediateFetch = true;
    }

    if (s_screenDirty)
    {
        applyNoaaLines(false);
        s_screenDirty = false;
    }

    if (!noaaAlertsEnabled)
        return;

    if (WiFi.status() != WL_CONNECTED)
        return;

    if (!s_forceImmediateFetch && (nowMs - s_lastFetchAttempt) < NOAA_FETCH_INTERVAL_MS)
        return;

    fetchNoaaAlert();
}

void showNoaaAlertScreen()
{
    if (!noaaAlertScreen.isActive())
    {
        applyNoaaLines(true);
        noaaAlertScreen.show([]() { currentScreen = homeScreenForDataSource(); });
    }
}

void notifyNoaaSettingsChanged()
{
    s_forceImmediateFetch = noaaAlertsEnabled;
    s_screenDirty = true;
    s_hasAlert = false;
    s_lastFetchAttempt = 0;
}
