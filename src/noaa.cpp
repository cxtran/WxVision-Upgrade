#include "noaa.h"
#include <algorithm>
#include <vector>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <time.h>
#include "settings.h"
#include "datetimesettings.h"
#include "display.h"
#include "InfoScreen.h"
#include "screen_manager.h"
#include "tempest.h"

extern InfoScreen noaaAlertScreen;

static bool s_hasAlert = false;
static bool s_screenDirty = true;
static bool s_forceImmediateFetch = false;
static bool s_fetchInProgress = false;
static bool s_prevEnabled = false;
static bool s_prevWifiConnected = false;
static bool s_unreadAlert = false;
static bool s_manualFetchPending = false;
static std::vector<NwsAlert> s_alerts;
static size_t s_alertCount = 0;
static String s_lastCheckHHMM = "--:--";
static String s_activeAlertId;
static String s_lastReadAlertId;
static bool s_suppressClearHeading = false;
static unsigned long s_lastFetchAttempt = 0;
static unsigned long s_nextScheduledFetchMs = 0;
static unsigned long s_nextFetchAllowedMs = 0;
static unsigned long s_wifiConnectedSinceMs = 0;

static constexpr unsigned long NOAA_FETCH_INTERVAL_MS = 15UL * 60UL * 1000UL;
static constexpr unsigned long NOAA_RETRY_BACKOFF_MS = 30000UL;
static constexpr unsigned long NOAA_CONNECT_FAIL_BACKOFF_MS = 300000UL;
static constexpr unsigned long NOAA_WIFI_STABLE_MS = 5000UL;
static constexpr unsigned long NOAA_WIFI_RECONNECT_QUIET_MS = 5000UL;
static constexpr const char *NOAA_BASE_URL = "http://noaa.photokia.com";
static constexpr const char *NOAA_ALERTS_ACTIVE_PATH = "/api/alerts/active";
static constexpr uint16_t NOAA_HTTP_TIMEOUT_MS = 12000U;
static constexpr uint16_t NOAA_ALERT_HEADING_MS = 4000U;
static constexpr uint16_t NOAA_MANUAL_RESULT_HEADING_MS = 1800U;

static bool parseNoaaIsoUtc(const String &isoRaw, DateTime &utcOut);
static bool noaaCurrentUtc(DateTime &utcOut);

static void lockNoaaState()
{
}

static void unlockNoaaState()
{
}

static bool noaaCoordsValid(float lat, float lon)
{
    return isfinite(lat) && isfinite(lon) &&
           lat >= -90.0f && lat <= 90.0f &&
           lon >= -180.0f && lon <= 180.0f &&
           !(fabs(lat) < 0.001f && fabs(lon) < 0.001f);
}

static bool resolveNoaaCoordinates(float &lat, float &lon)
{
    if (noaaCoordsValid(noaaLatitude, noaaLongitude))
    {
        lat = noaaLatitude;
        lon = noaaLongitude;
        return true;
    }

    int tzIdx = timezoneCurrentIndex();
    if (tzIdx >= 0 && tzIdx < static_cast<int>(timezoneCount()))
    {
        const TimezoneInfo &tz = timezoneInfoAt(static_cast<size_t>(tzIdx));
        if (noaaCoordsValid(tz.latitude, tz.longitude))
        {
            lat = tz.latitude;
            lon = tz.longitude;
            return true;
        }
    }

    return false;
}

static void stampLastCheckNow()
{
    if (rtcReady)
    {
        DateTime utcNow = rtc.now();
        int offsetMinutes = timezoneIsCustom() ? tzStandardOffset : timezoneOffsetForUtc(utcNow);
        DateTime localNow = utcToLocal(utcNow, offsetMinutes);
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", localNow.hour(), localNow.minute());
        s_lastCheckHHMM = String(buf);
        return;
    }

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

static void clearAlertState()
{
    s_hasAlert = false;
    s_alertCount = 0;
    s_alerts.clear();
    s_activeAlertId = "";
}

static const char *noaaAlertEventText(const NwsAlert &alert)
{
    if (alert.event.length())
        return alert.event.c_str();
    if (alert.headline.length())
        return alert.headline.c_str();
    return "NOAA ALERT";
}

static bool noaaAlertExpiryUtc(const NwsAlert &alert, DateTime &expiryUtcOut)
{
    if (alert.ends.length() > 0 && parseNoaaIsoUtc(alert.ends, expiryUtcOut))
        return true;
    if (alert.expires.length() > 0 && parseNoaaIsoUtc(alert.expires, expiryUtcOut))
        return true;
    return false;
}

static bool noaaAlertIsActiveNow(const NwsAlert &alert, const DateTime &nowUtc)
{
    DateTime expiryUtc;
    if (noaaAlertExpiryUtc(alert, expiryUtc))
        return nowUtc.unixtime() <= expiryUtc.unixtime();
    return true;
}

static void pruneExpiredCachedAlerts()
{
    if (!s_hasAlert || s_alertCount == 0)
        return;

    DateTime nowUtc;
    if (!noaaCurrentUtc(nowUtc))
        return;

    std::vector<NwsAlert> activeAlerts;
    activeAlerts.reserve(s_alerts.size());
    for (const NwsAlert &alert : s_alerts)
    {
        if (noaaAlertIsActiveNow(alert, nowUtc))
            activeAlerts.push_back(alert);
    }

    if (activeAlerts.size() == s_alerts.size())
        return;

    const bool hadUnread = s_unreadAlert;
    const size_t previousCount = s_alertCount;
    s_alerts = activeAlerts;
    s_alertCount = s_alerts.size();
    s_hasAlert = (s_alertCount > 0);
    s_activeAlertId = s_hasAlert ? s_alerts.front().id : "";
    s_unreadAlert = s_hasAlert && hadUnread;
    s_screenDirty = true;
    if (!s_hasAlert)
        s_unreadAlert = false;
    if (!s_suppressClearHeading)
    {
        if (!s_hasAlert && !hadUnread)
            queueTemporaryAlertHeading("ALL ALERTS CLEARED", NOAA_MANUAL_RESULT_HEADING_MS);
        else if (previousCount > s_alertCount && s_hasAlert)
            queueTemporaryAlertHeading("ALERT CLEARED",
                                       NOAA_MANUAL_RESULT_HEADING_MS,
                                       0,
                                       (String(s_alertCount) + " REMAIN").c_str());
    }
}

static void markNoaaAlertRead()
{
    lockNoaaState();
    s_unreadAlert = false;
    if (s_activeAlertId.length() > 0)
        s_lastReadAlertId = s_activeAlertId;
    unlockNoaaState();
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

static bool snapshotPrimaryAlert(NwsAlert &out)
{
    if (!s_hasAlert || s_alertCount == 0)
        return false;
    out = s_alerts.front();
    return true;
}

static void applyNoaaLines(bool forceReset)
{
    String lines[INFOSCREEN_MAX_LINES];
    uint16_t colors[INFOSCREEN_MAX_LINES] = {0};
    int count = 0;
    bool useColors = false;
    NwsAlert primary;
    bool havePrimary = false;

    auto push = [&](const String &label, const String &value, uint16_t color) {
        if (count >= INFOSCREEN_MAX_LINES)
            return;
        lines[count] = label.length() ? (label + ": " + value) : value;
        colors[count] = color;
        if (color != 0)
            useColors = true;
        ++count;
    };

    lockNoaaState();
    havePrimary = snapshotPrimaryAlert(primary);
    unlockNoaaState();

    if (!noaaAlertsEnabled)
    {
        push("", "Alerts Disabled", 0);
        push("Severity", "--", 0);
        push("", "Enable NOAA alerts in menu.", 0);
    }
    else if (s_fetchInProgress)
    {
        push("", "Get Alert info ...", 0);
        push("Status", "Checking relay", 0);
        push("Last", s_lastCheckHHMM, 0);
    }
    else if (!s_hasAlert || !havePrimary)
    {
        push("", "No Active Alert", 0);
        push("Severity", "None", 0);
        push("", "Monitoring relay...", 0);
    }
    else
    {
        push("Event", primary.event.length() ? primary.event : "Alert", dma_display ? dma_display->color565(255, 255, 255) : 0);
        push("Severity", primary.severity.length() ? primary.severity : "Unknown", severityColor(primary.severity));
        if (primary.headline.length())
            push("Headline", primary.headline, 0);
        if (primary.areaDesc.length())
            push("Area", primary.areaDesc, 0);
        if (primary.expires.length())
            push("Expires", primary.expires, 0);
        if (primary.description.length() && primary.description != primary.headline)
            push("Description", primary.description, dma_display ? dma_display->color565(150, 200, 255) : 0);
    }

    if (count == 0)
    {
        lines[0] = "No alert data";
        count = 1;
    }

    bool resetPosition = forceReset || !noaaAlertScreen.isActive();
    noaaAlertScreen.setLines(lines, count, resetPosition, useColors ? colors : nullptr);
}

static bool parseNoaaIsoUtc(const String &isoRaw, DateTime &utcOut)
{
    String iso = isoRaw;
    iso.trim();
    if (iso.length() < 16)
        return false;

    auto toInt2 = [&](int pos) -> int {
        if (pos + 1 >= iso.length())
            return -1;
        char a = iso.charAt(pos);
        char b = iso.charAt(pos + 1);
        if (!isdigit(static_cast<unsigned char>(a)) || !isdigit(static_cast<unsigned char>(b)))
            return -1;
        return (a - '0') * 10 + (b - '0');
    };
    auto toInt4 = [&](int pos) -> int {
        if (pos + 3 >= iso.length())
            return -1;
        for (int i = 0; i < 4; ++i)
        {
            if (!isdigit(static_cast<unsigned char>(iso.charAt(pos + i))))
                return -1;
        }
        return (iso.charAt(pos) - '0') * 1000 +
               (iso.charAt(pos + 1) - '0') * 100 +
               (iso.charAt(pos + 2) - '0') * 10 +
               (iso.charAt(pos + 3) - '0');
    };

    int year = toInt4(0);
    int month = toInt2(5);
    int day = toInt2(8);
    int hour = toInt2(11);
    int minute = toInt2(14);
    int second = 0;
    if (iso.length() >= 19 && isdigit(static_cast<unsigned char>(iso.charAt(17))) && isdigit(static_cast<unsigned char>(iso.charAt(18))))
        second = toInt2(17);

    if (year < 2000 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59)
        return false;

    int srcOffsetMinutes = 0;
    int zPos = iso.indexOf('Z');
    if (zPos < 0)
        zPos = iso.indexOf('z');
    if (zPos < 0)
    {
        int signPos = -1;
        for (int i = iso.length() - 1; i >= 10; --i)
        {
            char c = iso.charAt(i);
            if (c == '+' || c == '-')
            {
                signPos = i;
                break;
            }
        }
        if (signPos > 0 && signPos + 5 < iso.length())
        {
            int oh = toInt2(signPos + 1);
            int om = toInt2(signPos + 4);
            if (oh >= 0 && om >= 0)
            {
                int sign = (iso.charAt(signPos) == '-') ? -1 : 1;
                srcOffsetMinutes = sign * (oh * 60 + om);
            }
        }
    }

    DateTime src(year, month, day, hour, minute, second);
    int64_t utcEpoch = static_cast<int64_t>(src.unixtime()) - static_cast<int64_t>(srcOffsetMinutes) * 60;
    if (utcEpoch < 0)
        return false;

    utcOut = DateTime(static_cast<uint32_t>(utcEpoch));
    return true;
}

static bool noaaCurrentUtc(DateTime &utcOut)
{
    if (rtcReady)
    {
        utcOut = rtc.now();
        return true;
    }

    time_t raw = time(nullptr);
    if (raw <= 0)
        return false;

    utcOut = DateTime(static_cast<uint32_t>(raw));
    return true;
}

static void deferNoaaUntil(unsigned long whenMs)
{
    if (s_nextFetchAllowedMs == 0 || static_cast<long>(whenMs - s_nextFetchAllowedMs) > 0)
        s_nextFetchAllowedMs = whenMs;
}

static uint32_t noaaAlertSignature(const String &id)
{
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < id.length(); ++i)
    {
        hash ^= static_cast<uint8_t>(id.charAt(i));
        hash *= 16777619UL;
    }
    return hash ? hash : 0x4E4F4141UL; // "NOAA"
}

static int noaaSeverityRank(const String &severity)
{
    String s = severity;
    s.toLowerCase();
    if (s == "extreme")
        return 0;
    if (s == "severe")
        return 1;
    if (s == "moderate")
        return 2;
    if (s == "minor")
        return 3;
    return 4;
}

static int noaaUrgencyRank(const String &urgency)
{
    String s = urgency;
    s.toLowerCase();
    if (s == "immediate")
        return 0;
    if (s == "expected")
        return 1;
    if (s == "future")
        return 2;
    if (s == "past")
        return 3;
    return 4;
}

static uint32_t noaaExpiryEpoch(const NwsAlert &alert)
{
    DateTime expiryUtc;
    if (noaaAlertExpiryUtc(alert, expiryUtc))
        return expiryUtc.unixtime();
    return 0xFFFFFFFFUL;
}

static bool noaaAlertPriorityLess(const NwsAlert &lhs, const NwsAlert &rhs)
{
    const int lhsSeverity = noaaSeverityRank(lhs.severity);
    const int rhsSeverity = noaaSeverityRank(rhs.severity);
    if (lhsSeverity != rhsSeverity)
        return lhsSeverity < rhsSeverity;

    const int lhsUrgency = noaaUrgencyRank(lhs.urgency);
    const int rhsUrgency = noaaUrgencyRank(rhs.urgency);
    if (lhsUrgency != rhsUrgency)
        return lhsUrgency < rhsUrgency;

    const uint32_t lhsExpiry = noaaExpiryEpoch(lhs);
    const uint32_t rhsExpiry = noaaExpiryEpoch(rhs);
    if (lhsExpiry != rhsExpiry)
        return lhsExpiry < rhsExpiry;

    return lhs.event < rhs.event;
}

static String relayField(JsonObject obj, const char *fullKey, const char *compactKey)
{
    const char *fullValue = obj[fullKey] | "";
    if (fullValue && fullValue[0] != '\0')
        return String(fullValue);
    const char *compactValue = obj[compactKey] | "";
    return String(compactValue ? compactValue : "");
}

static bool relayTopLevelBool(JsonDocument &doc, const char *fullKey, const char *compactKey, bool defaultValue = false)
{
    JsonVariant fullValue = doc[fullKey];
    if (!fullValue.isNull())
        return fullValue.as<bool>();

    JsonVariant compactValue = doc[compactKey];
    if (!compactValue.isNull())
        return compactValue.as<bool>();

    return defaultValue;
}

static bool parseRelayAlertsPayload(const String &payload)
{
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err)
    {
        Serial.printf("[NOAA] Relay JSON parse failed: %s\n", err.c_str());
        return false;
    }

    JsonArray alerts = doc["alerts"].as<JsonArray>();
    if (alerts.isNull())
        return false;

    if (alerts.size() == 0)
    {
        const bool hadAlert = s_hasAlert;
        clearAlertState();
        s_unreadAlert = false;
        s_screenDirty = true;
        if (hadAlert && !s_suppressClearHeading)
            queueTemporaryAlertHeading("ALL ALERTS CLEARED", NOAA_MANUAL_RESULT_HEADING_MS);
        Serial.println("[NOAA] Parsed 0 alert(s)");
        return true;
    }

    DateTime nowUtc;
    const bool haveNowUtc = noaaCurrentUtc(nowUtc);
    std::vector<NwsAlert> parsedAlerts;
    parsedAlerts.reserve(alerts.size());
    for (JsonObject alertObj : alerts)
    {
        NwsAlert candidate;
        candidate.id = relayField(alertObj, "id", "i");
        candidate.url = candidate.id;
        candidate.event = relayField(alertObj, "event", "e");
        candidate.severity = relayField(alertObj, "severity", "s");
        candidate.certainty = relayField(alertObj, "certainty", "y");
        candidate.headline = relayField(alertObj, "headline", "h");
        candidate.description = relayField(alertObj, "description", "d");
        candidate.instruction = relayField(alertObj, "instruction", "n");
        candidate.urgency = relayField(alertObj, "urgency", "g");
        candidate.response = relayField(alertObj, "response", "r");
        candidate.areaDesc = relayField(alertObj, "area", "a");
        candidate.onset = relayField(alertObj, "onset", "o");
        candidate.expires = relayField(alertObj, "expires", "x");
        candidate.ends = relayField(alertObj, "ends", "z");
        if (candidate.description.length() == 0)
            candidate.description = candidate.headline.length() ? candidate.headline : "Active alert.";
        if (candidate.headline.length() == 0 && candidate.description.length() > 0)
            candidate.headline = candidate.description;

        if (candidate.id.length() == 0 && candidate.headline.length() == 0 && candidate.event.length() == 0)
            continue;
        if (haveNowUtc && !noaaAlertIsActiveNow(candidate, nowUtc))
            continue;

        parsedAlerts.push_back(candidate);
    }

    if (parsedAlerts.empty())
    {
        clearAlertState();
        s_unreadAlert = false;
        s_screenDirty = true;
        Serial.println("[NOAA] Parsed 0 alert(s)");
        return true;
    }

    const String previousAlertId = s_activeAlertId;
    const size_t previousCount = s_alertCount;
    std::sort(parsedAlerts.begin(), parsedAlerts.end(), noaaAlertPriorityLess);
    s_alerts = parsedAlerts;
    s_alertCount = s_alerts.size();
    s_hasAlert = true;
    s_activeAlertId = s_alerts.front().id;
    s_screenDirty = true;

    if (noaaAlertScreen.isActive())
    {
        s_unreadAlert = false;
        s_lastReadAlertId = s_activeAlertId;
    }
    else if (s_activeAlertId.length() > 0 && s_activeAlertId != previousAlertId && s_activeAlertId != s_lastReadAlertId)
    {
        s_unreadAlert = true;
        if (s_alertCount > 1)
            queueTemporaryAlertHeading("WEATHER ALERTS",
                                       NOAA_ALERT_HEADING_MS,
                                       noaaAlertSignature(s_activeAlertId),
                                       (String(s_alertCount) + " ACTIVE").c_str());
        else
            queueTemporaryAlertHeading("WEATHER ALERT", NOAA_ALERT_HEADING_MS, noaaAlertSignature(s_activeAlertId), noaaAlertEventText(s_alerts.front()));
    }
    else if (!noaaAlertScreen.isActive() && s_alertCount > previousCount && !s_suppressClearHeading)
    {
        s_unreadAlert = true;
        queueTemporaryAlertHeading("WEATHER ALERTS",
                                   NOAA_ALERT_HEADING_MS,
                                   noaaAlertSignature(s_activeAlertId),
                                   (String(s_alertCount) + " ACTIVE").c_str());
    }

    Serial.printf("[NOAA] Parsed relay alert event=%s severity=%s expires=%s count=%u stale=%d\n",
                  s_alerts.front().event.c_str(),
                  s_alerts.front().severity.c_str(),
                  s_alerts.front().expires.c_str(),
                  static_cast<unsigned>(s_alertCount),
                  static_cast<int>(relayTopLevelBool(doc, "stale", "s", false)));
    return true;
}

static bool fetchAndParseNoaaSummaryPayload(float lat, float lon, bool *connectFailedOut = nullptr)
{
    if (connectFailedOut)
        *connectFailedOut = false;

    HTTPClient http;
    http.setConnectTimeout(NOAA_HTTP_TIMEOUT_MS);
    http.setTimeout(NOAA_HTTP_TIMEOUT_MS);
    http.setReuse(false);

    String url = String(NOAA_BASE_URL) + NOAA_ALERTS_ACTIVE_PATH +
                 "?lat=" + String(lat, 4) +
                 "&lon=" + String(lon, 4) +
                 "&compact=true";

    Serial.printf("[NOAA] Request URL: %s\n", url.c_str());
    if (!http.begin(url))
    {
        Serial.println("[NOAA] HTTP begin failed");
        if (connectFailedOut)
            *connectFailedOut = true;
        return false;
    }

    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "VisionWX/1.0 (weather display firmware)");

    const int status = http.GET();
    Serial.printf("[NOAA] HTTP status: %d\n", status);
    if (status <= 0)
    {
        if (connectFailedOut)
            *connectFailedOut = true;
        http.end();
        return false;
    }
    if (status != HTTP_CODE_OK)
    {
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    Serial.printf("[NOAA] Fetched %u bytes for %.4f,%.4f\n",
                  static_cast<unsigned>(payload.length()),
                  static_cast<double>(lat),
                  static_cast<double>(lon));
    return parseRelayAlertsPayload(payload);
}

static void fetchNoaaAlertSummarySync(bool preserveSchedule)
{
    bool forced = s_forceImmediateFetch;
    s_forceImmediateFetch = false;

    const unsigned long savedLastFetchAttempt = s_lastFetchAttempt;
    const unsigned long savedNextScheduledFetchMs = s_nextScheduledFetchMs;
    const unsigned long savedNextFetchAllowedMs = s_nextFetchAllowedMs;
    const unsigned long fetchStartMs = millis();

    float lat = NAN;
    float lon = NAN;
    if (!resolveNoaaCoordinates(lat, lon))
    {
        Serial.println("[NOAA] Missing coordinates (set device location or timezone)");
        if (preserveSchedule)
        {
            s_lastFetchAttempt = savedLastFetchAttempt;
            s_nextScheduledFetchMs = savedNextScheduledFetchMs;
            s_nextFetchAllowedMs = savedNextFetchAllowedMs;
        }
        s_fetchInProgress = false;
        if (forced)
            s_screenDirty = true;
        return;
    }

    if (!preserveSchedule)
        s_lastFetchAttempt = fetchStartMs;
    stampLastCheckNow();
    s_suppressClearHeading = preserveSchedule;

    bool connectFailed = false;
    const bool fetchOk = fetchAndParseNoaaSummaryPayload(lat, lon, &connectFailed);
    s_suppressClearHeading = false;
    if (fetchOk)
    {
        s_nextFetchAllowedMs = 0;
        if (!preserveSchedule)
            s_nextScheduledFetchMs = fetchStartMs + NOAA_FETCH_INTERVAL_MS;
        else if (!s_hasAlert)
            queueTemporaryAlertHeading("NO ACTIVE ALERT", NOAA_MANUAL_RESULT_HEADING_MS);
        else
        {
            if (s_alertCount > 1)
                queueTemporaryAlertHeading("ACTIVE ALERTS",
                                           NOAA_MANUAL_RESULT_HEADING_MS,
                                           noaaAlertSignature(s_activeAlertId),
                                           (String(s_alertCount) + " TOTAL").c_str());
            else
                queueTemporaryAlertHeading("WEATHER ALERT",
                                           NOAA_MANUAL_RESULT_HEADING_MS,
                                           noaaAlertSignature(s_activeAlertId),
                                           noaaAlertEventText(s_alerts.front()));
        }
    }
    else
    {
        Serial.println("[NOAA] Failed to fetch payload");
        s_nextFetchAllowedMs = millis() + (connectFailed ? NOAA_CONNECT_FAIL_BACKOFF_MS : NOAA_RETRY_BACKOFF_MS);
        if (forced)
            s_screenDirty = true;
    }

    if (preserveSchedule)
    {
        s_lastFetchAttempt = savedLastFetchAttempt;
        s_nextScheduledFetchMs = savedNextScheduledFetchMs;
        s_nextFetchAllowedMs = savedNextFetchAllowedMs;
    }

    s_fetchInProgress = false;
    s_screenDirty = true;
}

static void fetchNoaaAlertSummarySync()
{
    fetchNoaaAlertSummarySync(false);
}

void initNoaaAlerts()
{
    s_prevEnabled = noaaAlertsEnabled;
    s_screenDirty = true;
    clearAlertState();
    s_lastCheckHHMM = "--:--";
    s_lastFetchAttempt = 0;
    s_nextScheduledFetchMs = 0;
    s_nextFetchAllowedMs = 0;
    s_unreadAlert = false;
    s_lastReadAlertId = "";
    s_forceImmediateFetch = false;
    applyNoaaLines(true);
}

void tickNoaaAlerts(unsigned long nowMs)
{
    const bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    if (wifiConnected != s_prevWifiConnected)
    {
        s_prevWifiConnected = wifiConnected;
        if (wifiConnected)
        {
            s_wifiConnectedSinceMs = nowMs;
            if (s_forceImmediateFetch)
            {
                Serial.println("[NOAA] WiFi connected, arming immediate NOAA fetch");
                s_nextFetchAllowedMs = 0;
            }
            else
            {
                deferNoaaUntil(nowMs + NOAA_WIFI_RECONNECT_QUIET_MS);
            }
        }
        else
        {
            s_wifiConnectedSinceMs = 0;
            Serial.println("[NOAA] WiFi disconnected, delaying NOAA fetch");
        }
    }

    if (s_prevEnabled != noaaAlertsEnabled)
    {
        s_prevEnabled = noaaAlertsEnabled;
        s_screenDirty = true;
        if (noaaAlertsEnabled)
            s_forceImmediateFetch = true;
    }

    pruneExpiredCachedAlerts();

    if (s_screenDirty)
    {
        applyNoaaLines(false);
        s_screenDirty = false;
    }

    if (!noaaAlertsEnabled)
    {
        s_manualFetchPending = false;
        return;
    }

    if (s_manualFetchPending)
    {
        s_manualFetchPending = false;
        s_fetchInProgress = true;
        s_screenDirty = true;
        fetchNoaaAlertSummarySync(true);
        return;
    }

    if (!wifiConnected)
        return;
    if (s_wifiConnectedSinceMs == 0 || (nowMs - s_wifiConnectedSinceMs) < NOAA_WIFI_STABLE_MS)
        return;
    if (s_nextFetchAllowedMs != 0 && nowMs < s_nextFetchAllowedMs)
        return;

    if (!s_forceImmediateFetch)
    {
        if (s_nextScheduledFetchMs == 0)
            s_nextScheduledFetchMs = nowMs + NOAA_FETCH_INTERVAL_MS;
        if (static_cast<long>(nowMs - s_nextScheduledFetchMs) < 0)
            return;
    }

    s_fetchInProgress = true;
    s_screenDirty = true;
    fetchNoaaAlertSummarySync();
}

void showNoaaAlertScreen()
{
    if (!noaaAlertScreen.isActive())
    {
        applyNoaaLines(true);
        noaaAlertScreen.show([]() { currentScreen = homeScreenForDataSource(); });
    }

    markNoaaAlertRead();
}

void refreshNoaaAlertsForScreenEntry()
{
    markNoaaAlertRead();
    s_screenDirty = true;
    drawNoaaAlertsScreen();
}

void notifyNoaaSettingsChanged()
{
    const bool keepNoaaScreen = (currentScreen == SCREEN_NOAA_ALERT) && noaaAlertsEnabled;
    s_forceImmediateFetch = noaaAlertsEnabled;
    s_screenDirty = true;
    clearAlertState();
    s_lastFetchAttempt = 0;
    s_nextScheduledFetchMs = 0;
    s_nextFetchAllowedMs = 0;
    s_unreadAlert = false;
    s_manualFetchPending = false;
    s_lastReadAlertId = "";
    if (noaaAlertsEnabled)
        s_nextFetchAllowedMs = 0;

    ensureCurrentScreenAllowed();
    if (keepNoaaScreen && currentScreen == SCREEN_NOAA_ALERT)
        refreshNoaaAlertsForScreenEntry();
}

NoaaManualFetchResult requestNoaaManualFetch()
{
    if (!noaaAlertsEnabled)
        return NOAA_MANUAL_FETCH_OFF;
    if (s_fetchInProgress || s_manualFetchPending)
        return NOAA_MANUAL_FETCH_BUSY;
    if (WiFi.status() != WL_CONNECTED)
        return NOAA_MANUAL_FETCH_BLOCKED;
    s_fetchInProgress = true;
    s_screenDirty = true;
    s_manualFetchPending = true;
    return NOAA_MANUAL_FETCH_STARTED;
}

bool noaaHasActiveAlert()
{
    lockNoaaState();
    bool out = s_hasAlert;
    unlockNoaaState();
    return out;
}

bool noaaHasUnreadAlert()
{
    lockNoaaState();
    bool out = s_unreadAlert;
    unlockNoaaState();
    return out;
}

bool noaaFetchInProgress()
{
    lockNoaaState();
    bool out = s_fetchInProgress;
    unlockNoaaState();
    return out;
}

uint16_t noaaActiveColor()
{
    lockNoaaState();
    bool hasAlert = s_hasAlert;
    String severity = (!s_alerts.empty()) ? s_alerts.front().severity : "";
    unlockNoaaState();

    if (!hasAlert || !dma_display)
        return 0;
    return severityColor(severity);
}

size_t noaaAlertCount()
{
    lockNoaaState();
    size_t out = s_alertCount;
    unlockNoaaState();
    return out;
}

bool noaaGetAlert(size_t index, NwsAlert &out)
{
    lockNoaaState();
    if (index >= s_alertCount)
    {
        unlockNoaaState();
        return false;
    }
    out = s_alerts[index];
    unlockNoaaState();
    return true;
}

String noaaLastCheckHHMM()
{
    lockNoaaState();
    String out = s_lastCheckHHMM;
    unlockNoaaState();
    return out;
}
