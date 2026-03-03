#include "noaa.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Arduino_JSON.h>
#include <time.h>
#include "settings.h"
#include "display.h"
#include "InfoScreen.h"

extern InfoScreen noaaAlertScreen;

static bool s_hasAlert = false;
static bool s_screenDirty = true;
static bool s_forceImmediateFetch = false;
static bool s_prevEnabled = false;
static constexpr size_t NOAA_MAX_ALERTS = 8;
static NwsAlert s_alerts[NOAA_MAX_ALERTS];
static size_t s_alertCount = 0;
static String s_lastCheckHHMM = "--:--";
static String s_event;
static String s_severity;
static String s_description;
static String s_instruction;
static String s_areaDesc;
static String s_headline;
static String s_urgency;
static String s_certainty;
static String s_response;
static String s_effective;
static String s_expires;
static String s_ends;
static unsigned long s_lastFetchAttempt = 0;

static constexpr unsigned long NOAA_FETCH_INTERVAL_MS = 15UL * 60UL * 1000UL;

static void stampLastCheckNow()
{
    time_t raw = time(nullptr);
    if (raw > 0)
    {
        struct tm *ti = localtime(&raw);
        if (ti != nullptr)
        {
            char buf[6];
            snprintf(buf, sizeof(buf), "%02d:%02d", ti->tm_hour, ti->tm_min);
            s_lastCheckHHMM = String(buf);
            return;
        }
    }
    s_lastCheckHHMM = "--:--";
}

static void clearAlertCache()
{
    s_alertCount = 0;
    for (size_t i = 0; i < NOAA_MAX_ALERTS; ++i)
        s_alerts[i] = NwsAlert{};
}

static void clearAlertFields()
{
    s_event = "";
    s_severity = "";
    s_description = "";
    s_instruction = "";
    s_areaDesc = "";
    s_headline = "";
    s_urgency = "";
    s_certainty = "";
    s_response = "";
    s_effective = "";
    s_expires = "";
    s_ends = "";
}

static bool isJsonUndefined(JSONVar value)
{
    return JSON.typeof_(value) == "undefined";
}

static bool isJsonArray(JSONVar value)
{
    return JSON.typeof_(value) == "array";
}

static void cleanRawExtractInPlace(String &raw)
{
    raw.trim();
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
}

static String extractRawField(const String &payload, const String &key)
{
    String needle = "\"" + key + "\"";
    int pos = payload.indexOf(needle);
    if (pos < 0)
        return "";
    pos = payload.indexOf(':', pos + needle.length());
    if (pos < 0)
        return "";
    // Move past ':' then skip whitespace/newlines
    pos++;
    while (pos < (int)payload.length() && (payload[pos] == ' ' || payload[pos] == '\t' || payload[pos] == '\n' || payload[pos] == '\r'))
        pos++;
    if (pos >= (int)payload.length())
        return "";
    // Expect a quoted string
    if (pos >= (int)payload.length() - 1 || payload[pos] != '"')
        return "";
    int start = pos;
    int end = -1;
    for (int i = start + 1; i < (int)payload.length(); ++i)
    {
        if (payload[i] == '"' && payload[i - 1] != '\\')
        {
            end = i;
            break;
        }
    }
    if (end == -1)
        return "";
    return payload.substring(start + 1, end);
}

static bool tryRawFallback(const String &payload)
{
    // Raw fallback is only for small JSON-ish text payloads.
    // Avoid processing compressed/binary or very large bodies to prevent heap churn.
    if (payload.length() == 0 || payload.length() > 24576)
        return false;
    if (payload.length() >= 2 &&
        static_cast<uint8_t>(payload[0]) == 0x1F &&
        static_cast<uint8_t>(payload[1]) == 0x8B)
    {
        return false;
    }

    clearAlertFields();

    String rawEvent = extractRawField(payload, "event");
    String rawSeverity = extractRawField(payload, "severity");
    String rawDesc = extractRawField(payload, "description");
    String rawInstr = extractRawField(payload, "instruction");

    if (!(rawEvent.length() || rawSeverity.length() || rawDesc.length() || rawInstr.length()))
        return false;

    s_event = rawEvent;
    cleanRawExtractInPlace(s_event);
    if (s_event.length() == 0)
        s_event = "NOAA Alert";
    s_severity = rawSeverity;
    cleanRawExtractInPlace(s_severity);
    s_description = rawDesc;
    cleanRawExtractInPlace(s_description);
    s_instruction = rawInstr;
    cleanRawExtractInPlace(s_instruction);
    if (s_description.length() == 0)
        s_description = "No description provided.";
    clearAlertCache();
    s_alerts[0].event = s_event;
    s_alerts[0].severity = s_severity;
    s_alerts[0].description = s_description;
    s_alerts[0].instruction = s_instruction;
    s_alertCount = 1;
    s_hasAlert = true;
    s_screenDirty = true;
    return true;
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
    String lines[INFOSCREEN_MAX_LINES];
    uint16_t colors[INFOSCREEN_MAX_LINES] = {0};
    int count = 0;
    bool useColors = false;

    auto push = [&](const String &label, const String &value, uint16_t color) {
        if (count >= INFOSCREEN_MAX_LINES)
            return;
        String line;
        if (label.length())
            line = label + ": " + value;
        else
            line = value;
        lines[count] = line;
        colors[count] = color;
        if (color != 0)
            useColors = true;
        count++;
    };

    if (!noaaAlertsEnabled)
    {
        push("", "Alerts Disabled", 0);
        push("Severity", "--", 0);
        push("", "Enable NOAA alerts in menu.", 0);
    }
    else if (!s_hasAlert)
    {
        push("", "No Active Alert", 0);
        push("Severity", "None", 0);
        push("", "Monitoring NOAA feed...", 0);
    }
    else
    {
        push("Event", s_event.length() ? s_event : "Alert", dma_display ? dma_display->color565(255, 255, 255) : 0);
        push("Severity", s_severity.length() ? s_severity : "Unknown", severityColor(s_severity));
        if (s_headline.length())
            push("Headline", s_headline, 0);
        if (s_areaDesc.length())
            push("Area", s_areaDesc, 0);
        if (s_urgency.length())
            push("Urgency", s_urgency, 0);
        if (s_certainty.length())
            push("Certainty", s_certainty, 0);
        if (s_response.length())
            push("Response", s_response, 0);
        if (s_effective.length())
            push("Effective", s_effective, 0);
        if (s_expires.length())
            push("Expires", s_expires, 0);
        if (s_ends.length())
            push("Ends", s_ends, 0);
        push("Description", s_description.length() ? s_description : "No description provided.", dma_display ? dma_display->color565(150, 200, 255) : 0);
        if (s_instruction.length())
            push("Instruction", s_instruction, dma_display ? dma_display->color565(150, 200, 255) : 0);
    }

    if (count == 0)
    {
        lines[0] = "No alert data";
        count = 1;
    }

    bool resetPosition = forceReset || !noaaAlertScreen.isActive();
    noaaAlertScreen.setLines(lines, count, resetPosition, useColors ? colors : nullptr);
}

static void handleNoaaPayload(const String &payload)
{
    JSONVar doc = JSON.parse(payload);
    if (isJsonUndefined(doc))
    {
        Serial.println("[NOAA] Failed to parse JSON; using fallback");
        s_screenDirty = true;
        if (tryRawFallback(payload))
        {
            // Fallback populates fields
        }
        return;
    }

    JSONVar features = doc["features"];
    clearAlertFields();
    clearAlertCache();

    String featType = JSON.typeof_(features);
    int featLen = (featType == "array" || featType == "object") ? (int)features.length() : 0;

    if (featLen == 0)
    {
        // Fallback: try a minimal raw parse to avoid losing alerts due to parser quirks.
        if (payload.indexOf("\"features\"") != -1 && payload.indexOf("\"properties\"") != -1 && tryRawFallback(payload))
        {
            return;
        }
        if (s_hasAlert)
        {
            s_hasAlert = false;
            clearAlertFields();
            s_screenDirty = true;
        }
        return;
    }

    JSONVar first = features[0];
    if (JSON.typeof_(first) != "object")
    {
        Serial.println("[NOAA] First feature is not an object");
        return;
    }
    for (int i = 0; i < featLen && s_alertCount < NOAA_MAX_ALERTS; ++i)
    {
        JSONVar feature = features[i];
        if (JSON.typeof_(feature) != "object")
            continue;
        JSONVar props = feature["properties"];
        if (JSON.typeof_(props) != "object")
            continue;

        NwsAlert a;
        a.event = cleanJsonString(props["event"]);
        if (a.event.length() == 0)
            a.event = "NOAA Alert";
        a.severity = cleanJsonString(props["severity"]);
        a.urgency = cleanJsonString(props["urgency"]);
        a.areaDesc = cleanJsonString(props["areaDesc"]);
        a.headline = cleanJsonString(props["headline"]);
        a.expires = cleanJsonString(props["expires"]);
        a.description = cleanJsonString(props["description"]);
        a.instruction = cleanJsonString(props["instruction"]);
        if (a.description.length() == 0)
            a.description = "No description provided.";

        s_alerts[s_alertCount++] = a;
    }

    if (s_alertCount == 0)
    {
        s_hasAlert = false;
        s_screenDirty = true;
        return;
    }

    const NwsAlert &a0 = s_alerts[0];
    s_event = a0.event;
    s_severity = a0.severity;
    s_headline = a0.headline;
    s_areaDesc = a0.areaDesc;
    s_urgency = a0.urgency;
    s_expires = a0.expires;
    s_description = a0.description;
    s_instruction = a0.instruction;
    s_certainty = "";
    s_response = "";
    s_effective = "";
    s_ends = "";
    s_hasAlert = true;
    s_screenDirty = true;
}

static void fetchNoaaAlert()
{
    s_lastFetchAttempt = millis();
    stampLastCheckNow();
    bool forced = s_forceImmediateFetch;
    s_forceImmediateFetch = false;

    String url = "https://api.weather.gov/alerts/active?point=" +
                 String(noaaLatitude, 4) + "," + String(noaaLongitude, 4);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(12000);
    http.setUserAgent("WxVision NOAA/1.0 (wxvision.local)");
    http.addHeader("Accept", "application/geo+json");
    // Force plain (non-gzip) response; gzip can break Arduino_JSON parsing.
    http.addHeader("Accept-Encoding", "identity");

    if (!http.begin(client, url))
    {
        Serial.println("[NOAA] Failed to begin HTTP connection");
        return;
    }

    int code = http.GET();
    if (code == HTTP_CODE_OK)
    {
        String payload = http.getString();
        if (payload.startsWith("\x1f\x8b"))
        {
            Serial.println("[NOAA] Warning: gzip payload received; forcing identity failed");
            http.end();
            return;
        }
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
    clearAlertCache();
    clearAlertFields();
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
    clearAlertCache();
    clearAlertFields();
    s_lastFetchAttempt = 0;
}

bool noaaHasActiveAlert()
{
    return s_hasAlert;
}

uint16_t noaaActiveColor()
{
    if (!s_hasAlert || !dma_display)
        return 0;
    return severityColor(s_severity);
}

size_t noaaAlertCount()
{
    return s_alertCount;
}

bool noaaGetAlert(size_t index, NwsAlert &out)
{
    if (index >= s_alertCount)
        return false;
    out = s_alerts[index];
    return true;
}

String noaaLastCheckHHMM()
{
    return s_lastCheckHHMM;
}
