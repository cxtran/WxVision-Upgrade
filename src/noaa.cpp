#include "noaa.h"
#include <algorithm>
#include <vector>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>
#include "settings.h"
#include "datetimesettings.h"
#include "display.h"
#include "InfoScreen.h"
#include "screen_manager.h"
#include "tempest.h"
#include "psram_utils.h"

#if WXV_ENABLE_NOAA_ALERTS

extern InfoScreen noaaAlertScreen;

static bool s_hasAlert = false;
static bool s_screenDirty = true;
static bool s_forceImmediateFetch = false;
static bool s_fetchInProgress = false;
static bool s_prevEnabled = false;
static bool s_prevWifiConnected = false;
static bool s_unreadAlert = false;
static bool s_manualFetchPending = false;
using NoaaAlertVector = std::vector<NwsAlert, wxv::memory::PsramAllocator<NwsAlert>>;
using NoaaPayloadBuffer = std::vector<char, wxv::memory::PsramAllocator<char>>;

static NoaaAlertVector s_alerts;
static size_t s_alertCount = 0;
static String s_lastCheckHHMM = "--:--";
static String s_activeAlertId;
static String s_lastReadAlertId;
static bool s_suppressClearHeading = false;
static unsigned long s_lastFetchAttempt = 0;
static unsigned long s_nextScheduledFetchMs = 0;
static unsigned long s_nextFetchAllowedMs = 0;
static unsigned long s_wifiConnectedSinceMs = 0;
static StaticSemaphore_t s_noaaStateMutexBuffer;
static SemaphoreHandle_t s_noaaStateMutex = nullptr;

static constexpr unsigned long NOAA_FETCH_INTERVAL_MS = 15UL * 60UL * 1000UL;
static constexpr unsigned long NOAA_RETRY_BACKOFF_MS = 30000UL;
static constexpr unsigned long NOAA_CONNECT_FAIL_BACKOFF_MS = 300000UL;
static constexpr unsigned long NOAA_WIFI_STABLE_MS = 5000UL;
static constexpr unsigned long NOAA_WIFI_RECONNECT_QUIET_MS = 5000UL;
static constexpr const char *NOAA_RELAY_BASE_URL = "http://noaa.photokia.com";
static constexpr const char *NOAA_RELAY_ALERTS_ACTIVE_PATH = "/api/alerts/active";
static constexpr const char *NOAA_DIRECT_ALERTS_URL = "https://api.weather.gov/alerts/active";
static constexpr uint16_t NOAA_HTTP_TIMEOUT_MS = 12000U;
static constexpr size_t NOAA_MAX_BODY_BYTES = 32768U;
static constexpr uint16_t NOAA_ALERT_HEADING_MS = 4000U;
static constexpr uint16_t NOAA_MANUAL_RESULT_HEADING_MS = 1800U;

static bool parseNoaaIsoUtc(const String &isoRaw, DateTime &utcOut);
static bool noaaCurrentUtc(DateTime &utcOut);

static SemaphoreHandle_t ensureNoaaStateMutex()
{
    if (s_noaaStateMutex == nullptr)
        s_noaaStateMutex = xSemaphoreCreateMutexStatic(&s_noaaStateMutexBuffer);
    return s_noaaStateMutex;
}

static void lockNoaaState()
{
    SemaphoreHandle_t mutex = ensureNoaaStateMutex();
    if (mutex != nullptr)
        xSemaphoreTake(mutex, portMAX_DELAY);
}

static void unlockNoaaState()
{
    SemaphoreHandle_t mutex = ensureNoaaStateMutex();
    if (mutex != nullptr)
        xSemaphoreGive(mutex);
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
    String nextValue = "--:--";
    if (rtcReady)
    {
        DateTime utcNow = rtc.now();
        int offsetMinutes = timezoneIsCustom() ? tzStandardOffset : timezoneOffsetForUtc(utcNow);
        DateTime localNow = utcToLocal(utcNow, offsetMinutes);
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", localNow.hour(), localNow.minute());
        nextValue = String(buf);
    }
    else
    {
        time_t raw = time(nullptr);
        if (raw > 0)
        {
            struct tm *ti = localtime(&raw);
            if (ti != nullptr)
            {
                char buf[6];
                snprintf(buf, sizeof(buf), "%02d:%02d", ti->tm_hour, ti->tm_min);
                nextValue = String(buf);
            }
        }
    }
    lockNoaaState();
    s_lastCheckHHMM = nextValue;
    unlockNoaaState();
}

static void clearAlertState()
{
    lockNoaaState();
    s_hasAlert = false;
    s_alertCount = 0;
    s_alerts.clear();
    s_activeAlertId = "";
    unlockNoaaState();
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
    bool shouldShowCleared = false;
    bool shouldShowRemaining = false;
    size_t remainingCount = 0;

    lockNoaaState();
    if (!s_hasAlert || s_alertCount == 0)
    {
        unlockNoaaState();
        return;
    }

    DateTime nowUtc;
    if (!noaaCurrentUtc(nowUtc))
    {
        unlockNoaaState();
        return;
    }

    NoaaAlertVector activeAlerts;
    activeAlerts.reserve(s_alerts.size());
    for (const NwsAlert &alert : s_alerts)
    {
        if (noaaAlertIsActiveNow(alert, nowUtc))
            activeAlerts.push_back(alert);
    }

    if (activeAlerts.size() == s_alerts.size())
    {
        unlockNoaaState();
        return;
    }

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
            shouldShowCleared = true;
        else if (previousCount > s_alertCount && s_hasAlert)
        {
            shouldShowRemaining = true;
            remainingCount = s_alertCount;
        }
    }
    unlockNoaaState();

    if (shouldShowCleared)
        queueTemporaryAlertHeading("ALL ALERTS CLEARED", NOAA_MANUAL_RESULT_HEADING_MS);
    else if (shouldShowRemaining)
        queueTemporaryAlertHeading("ALERT CLEARED",
                                   NOAA_MANUAL_RESULT_HEADING_MS,
                                   0,
                                   (String(remainingCount) + " REMAIN").c_str());
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
    lockNoaaState();
    if (!s_hasAlert || s_alertCount == 0)
    {
        unlockNoaaState();
        return false;
    }
    out = s_alerts.front();
    unlockNoaaState();
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
    bool fetchInProgress = false;
    bool hasAlert = false;
    String lastCheck;

    auto push = [&](const String &label, const String &value, uint16_t color) {
        if (count >= INFOSCREEN_MAX_LINES)
            return;
        lines[count] = label.length() ? (label + ": " + value) : value;
        colors[count] = color;
        if (color != 0)
            useColors = true;
        ++count;
    };

    havePrimary = snapshotPrimaryAlert(primary);
    lockNoaaState();
    fetchInProgress = s_fetchInProgress;
    hasAlert = s_hasAlert;
    lastCheck = s_lastCheckHHMM;
    unlockNoaaState();

    if (!noaaAlertsEnabled)
    {
        push("", "Alerts Disabled", 0);
        push("Severity", "--", 0);
        push("", "Enable NOAA alerts in menu.", 0);
    }
    else if (fetchInProgress)
    {
        push("", "Get Alert info ...", 0);
        push("Status", "Checking relay", 0);
        push("Last", lastCheck, 0);
    }
    else if (!hasAlert || !havePrimary)
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

static const char *noaaFetchSourceName(NoaaFetchSource source)
{
    switch (source)
    {
    case NOAA_FETCH_SOURCE_DIRECT:
        return "direct";
    case NOAA_FETCH_SOURCE_RELAY:
    default:
        return "relay";
    }
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

static String directField(JsonObject obj, const char *key)
{
    const char *value = obj[key] | "";
    return String(value ? value : "");
}

static bool applyParsedAlerts(NoaaAlertVector &parsedAlerts, bool suppressClearHeading, const char *sourceTag, bool staleFlag)
{
    if (parsedAlerts.empty())
    {
        bool hadAlert = false;
        lockNoaaState();
        hadAlert = s_hasAlert;
        unlockNoaaState();
        clearAlertState();
        lockNoaaState();
        s_unreadAlert = false;
        s_screenDirty = true;
        unlockNoaaState();
        if (hadAlert && !suppressClearHeading)
            queueTemporaryAlertHeading("ALL ALERTS CLEARED", NOAA_MANUAL_RESULT_HEADING_MS);
        Serial.printf("[NOAA] Parsed 0 alert(s) from %s\n", sourceTag);
        return true;
    }

    std::sort(parsedAlerts.begin(), parsedAlerts.end(), noaaAlertPriorityLess);
    String previousAlertId;
    String activeAlertId;
    String activeEvent;
    String activeSeverity;
    String activeExpires;
    String lastReadAlertId;
    size_t alertCount = 0;
    size_t previousCount = 0;
    bool screenActive = noaaAlertScreen.isActive();
    bool showSingleAlert = false;
    bool showAlertCount = false;

    lockNoaaState();
    previousAlertId = s_activeAlertId;
    previousCount = s_alertCount;
    lastReadAlertId = s_lastReadAlertId;
    s_alerts = parsedAlerts;
    s_alertCount = s_alerts.size();
    s_hasAlert = true;
    s_activeAlertId = s_alerts.front().id;
    s_screenDirty = true;
    activeAlertId = s_activeAlertId;
    activeEvent = s_alerts.front().event;
    activeSeverity = s_alerts.front().severity;
    activeExpires = s_alerts.front().expires;
    alertCount = s_alertCount;

    if (screenActive)
    {
        s_unreadAlert = false;
        s_lastReadAlertId = s_activeAlertId;
    }
    else if (s_activeAlertId.length() > 0 && s_activeAlertId != previousAlertId && s_activeAlertId != lastReadAlertId)
    {
        s_unreadAlert = true;
        showAlertCount = (s_alertCount > 1);
        showSingleAlert = !showAlertCount;
    }
    else if (!screenActive && s_alertCount > previousCount && !suppressClearHeading)
    {
        s_unreadAlert = true;
        showAlertCount = true;
    }
    unlockNoaaState();

    if (showAlertCount)
        queueTemporaryAlertHeading("WEATHER ALERTS",
                                   NOAA_ALERT_HEADING_MS,
                                   noaaAlertSignature(activeAlertId),
                                   (String(alertCount) + " ACTIVE").c_str());
    else if (showSingleAlert)
        queueTemporaryAlertHeading("WEATHER ALERT",
                                   NOAA_ALERT_HEADING_MS,
                                   noaaAlertSignature(activeAlertId),
                                   activeEvent.length() ? activeEvent.c_str() : "NOAA ALERT");

    Serial.printf("[NOAA] Parsed %u %s alert(s) event=%s severity=%s expires=%s stale=%d\n",
                  static_cast<unsigned>(alertCount),
                  sourceTag,
                  activeEvent.c_str(),
                  activeSeverity.c_str(),
                  activeExpires.c_str(),
                  static_cast<int>(staleFlag));
    return true;
}

static bool parseRelayAlertsPayload(const String &payload)
{
    JsonDocument doc(wxv::memory::psramJsonAllocator());
    DeserializationError err = deserializeJson(doc, payload);
    if (err)
    {
        Serial.printf("[NOAA] Relay JSON parse failed: %s\n", err.c_str());
        return false;
    }

    JsonArray alerts = doc["alerts"].as<JsonArray>();
    if (alerts.isNull())
        return false;

    DateTime nowUtc;
    const bool haveNowUtc = noaaCurrentUtc(nowUtc);
    NoaaAlertVector parsedAlerts;
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

    bool suppressClearHeading = false;

    lockNoaaState();
    suppressClearHeading = s_suppressClearHeading;
    unlockNoaaState();
    return applyParsedAlerts(parsedAlerts,
                             suppressClearHeading,
                             "relay",
                             relayTopLevelBool(doc, "stale", "s", false));
}

static bool parseDirectAlertsPayload(const String &payload)
{
    JsonDocument doc(wxv::memory::psramJsonAllocator());
    DeserializationError err = deserializeJson(doc, payload);
    if (err)
    {
        Serial.printf("[NOAA] Direct JSON parse failed: %s\n", err.c_str());
        return false;
    }

    JsonArray features = doc["features"].as<JsonArray>();
    if (features.isNull())
        return false;

    DateTime nowUtc;
    const bool haveNowUtc = noaaCurrentUtc(nowUtc);
    NoaaAlertVector parsedAlerts;
    parsedAlerts.reserve(features.size());
    for (JsonObject featureObj : features)
    {
        JsonObject props = featureObj["properties"].as<JsonObject>();
        if (props.isNull())
            continue;

        NwsAlert candidate;
        candidate.url = directField(props, "@id");
        if (candidate.url.length() == 0)
            candidate.url = directField(featureObj, "id");
        candidate.id = directField(props, "id");
        if (candidate.id.length() == 0)
            candidate.id = candidate.url;
        candidate.sent = directField(props, "sent");
        candidate.effective = directField(props, "effective");
        candidate.onset = directField(props, "onset");
        candidate.event = directField(props, "event");
        candidate.status = directField(props, "status");
        candidate.messageType = directField(props, "messageType");
        candidate.category = directField(props, "category");
        candidate.severity = directField(props, "severity");
        candidate.certainty = directField(props, "certainty");
        candidate.urgency = directField(props, "urgency");
        candidate.areaDesc = directField(props, "areaDesc");
        candidate.sender = directField(props, "sender");
        candidate.senderName = directField(props, "senderName");
        candidate.headline = directField(props, "headline");
        candidate.description = directField(props, "description");
        candidate.instruction = directField(props, "instruction");
        candidate.response = directField(props, "response");
        candidate.note = directField(props, "note");
        candidate.expires = directField(props, "expires");
        candidate.ends = directField(props, "ends");
        candidate.scope = directField(props, "scope");
        candidate.language = directField(props, "language");
        candidate.web = directField(props, "web");
        if (candidate.description.length() == 0)
            candidate.description = candidate.headline.length() ? candidate.headline : "Active alert.";
        if (candidate.headline.length() == 0)
            candidate.headline = candidate.event.length() ? candidate.event : candidate.description;

        if (candidate.id.length() == 0 && candidate.headline.length() == 0 && candidate.event.length() == 0)
            continue;
        if (haveNowUtc && !noaaAlertIsActiveNow(candidate, nowUtc))
            continue;

        parsedAlerts.push_back(candidate);
    }

    bool suppressClearHeading = false;
    lockNoaaState();
    suppressClearHeading = s_suppressClearHeading;
    unlockNoaaState();
    return applyParsedAlerts(parsedAlerts, suppressClearHeading, "direct", false);
}

static bool parseRelayAlertsPayload(const char *payload, size_t length)
{
    JsonDocument doc(wxv::memory::psramJsonAllocator());
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err)
    {
        Serial.printf("[NOAA] Relay JSON parse failed: %s\n", err.c_str());
        return false;
    }

    JsonArray alerts = doc["alerts"].as<JsonArray>();
    if (alerts.isNull())
        return false;

    DateTime nowUtc;
    const bool haveNowUtc = noaaCurrentUtc(nowUtc);
    NoaaAlertVector parsedAlerts;
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

    bool suppressClearHeading = false;

    lockNoaaState();
    suppressClearHeading = s_suppressClearHeading;
    unlockNoaaState();
    return applyParsedAlerts(parsedAlerts,
                             suppressClearHeading,
                             "relay",
                             relayTopLevelBool(doc, "stale", "s", false));
}

static bool parseDirectAlertsPayload(const char *payload, size_t length)
{
    JsonDocument doc(wxv::memory::psramJsonAllocator());
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err)
    {
        Serial.printf("[NOAA] Direct JSON parse failed: %s\n", err.c_str());
        return false;
    }

    JsonArray features = doc["features"].as<JsonArray>();
    if (features.isNull())
        return false;

    DateTime nowUtc;
    const bool haveNowUtc = noaaCurrentUtc(nowUtc);
    NoaaAlertVector parsedAlerts;
    parsedAlerts.reserve(features.size());
    for (JsonObject featureObj : features)
    {
        JsonObject props = featureObj["properties"].as<JsonObject>();
        if (props.isNull())
            continue;

        NwsAlert candidate;
        candidate.url = directField(props, "@id");
        if (candidate.url.length() == 0)
            candidate.url = directField(featureObj, "id");
        candidate.id = directField(props, "id");
        if (candidate.id.length() == 0)
            candidate.id = candidate.url;
        candidate.sent = directField(props, "sent");
        candidate.effective = directField(props, "effective");
        candidate.onset = directField(props, "onset");
        candidate.event = directField(props, "event");
        candidate.status = directField(props, "status");
        candidate.messageType = directField(props, "messageType");
        candidate.category = directField(props, "category");
        candidate.severity = directField(props, "severity");
        candidate.certainty = directField(props, "certainty");
        candidate.urgency = directField(props, "urgency");
        candidate.areaDesc = directField(props, "areaDesc");
        candidate.sender = directField(props, "sender");
        candidate.senderName = directField(props, "senderName");
        candidate.headline = directField(props, "headline");
        candidate.description = directField(props, "description");
        candidate.instruction = directField(props, "instruction");
        candidate.response = directField(props, "response");
        candidate.note = directField(props, "note");
        candidate.expires = directField(props, "expires");
        candidate.ends = directField(props, "ends");
        candidate.scope = directField(props, "scope");
        candidate.language = directField(props, "language");
        candidate.web = directField(props, "web");
        if (candidate.description.length() == 0)
            candidate.description = candidate.headline.length() ? candidate.headline : "Active alert.";
        if (candidate.headline.length() == 0)
            candidate.headline = candidate.event.length() ? candidate.event : candidate.description;

        if (candidate.id.length() == 0 && candidate.headline.length() == 0 && candidate.event.length() == 0)
            continue;
        if (haveNowUtc && !noaaAlertIsActiveNow(candidate, nowUtc))
            continue;

        parsedAlerts.push_back(candidate);
    }

    bool suppressClearHeading = false;
    lockNoaaState();
    suppressClearHeading = s_suppressClearHeading;
    unlockNoaaState();
    return applyParsedAlerts(parsedAlerts, suppressClearHeading, "direct", false);
}

static bool readHttpPayloadToPsram(HTTPClient &http, NoaaPayloadBuffer &payloadOut)
{
    WiFiClient *stream = http.getStreamPtr();
    if (stream == nullptr)
    {
        Serial.println("[NOAA] Stream pointer unavailable");
        return false;
    }

    const int contentLength = http.getSize();
    payloadOut.clear();
    if (contentLength > 0)
    {
        if (contentLength > static_cast<int>(NOAA_MAX_BODY_BYTES))
        {
            Serial.printf("[NOAA] Payload too large: %d bytes\n", contentLength);
            return false;
        }
        payloadOut.reserve(static_cast<size_t>(contentLength) + 1u);
    }
    else
        payloadOut.reserve(4097u);

    char chunk[512];
    unsigned long lastDataMs = millis();
    const unsigned long startMs = lastDataMs;
    unsigned long lastYieldMs = startMs;
    while (http.connected() || stream->available() > 0)
    {
        const size_t available = static_cast<size_t>(stream->available());
        if (available == 0)
        {
            const unsigned long nowMs = millis();
            if ((nowMs - lastDataMs) >= NOAA_HTTP_TIMEOUT_MS)
            {
                Serial.printf("[NOAA] Body read timeout after %lu ms (connected=%d)\n",
                              nowMs - startMs,
                              http.connected() ? 1 : 0);
                break;
            }
            delay(0);
            continue;
        }

        const size_t toRead = (available > 0) ? min(available, sizeof(chunk)) : sizeof(chunk);
        const int bytesRead = stream->readBytes(chunk, toRead);
        if (bytesRead <= 0)
        {
            delay(0);
            continue;
        }
        if ((payloadOut.size() + static_cast<size_t>(bytesRead)) > NOAA_MAX_BODY_BYTES)
        {
            Serial.printf("[NOAA] Body exceeded %u bytes; aborting\n", static_cast<unsigned>(NOAA_MAX_BODY_BYTES));
            return false;
        }
        payloadOut.insert(payloadOut.end(), chunk, chunk + bytesRead);
        lastDataMs = millis();
        if ((lastDataMs - lastYieldMs) >= 25UL)
        {
            delay(0);
            lastYieldMs = lastDataMs;
        }
    }

    if (payloadOut.empty())
    {
        Serial.println("[NOAA] Empty response body");
        return false;
    }

    payloadOut.push_back('\0');
    return !payloadOut.empty();
}

static bool fetchAndParseNoaaSummaryPayload(float lat, float lon, bool *connectFailedOut = nullptr)
{
    if (connectFailedOut)
        *connectFailedOut = false;

    const NoaaFetchSource source = noaaFetchSource;
    const bool useTls = (source == NOAA_FETCH_SOURCE_DIRECT);
    String url;
    if (source == NOAA_FETCH_SOURCE_DIRECT)
    {
        url = String(NOAA_DIRECT_ALERTS_URL) +
              "?point=" + String(lat, 4) + "," + String(lon, 4);
    }
    else
    {
        url = String(NOAA_RELAY_BASE_URL) + NOAA_RELAY_ALERTS_ACTIVE_PATH +
              "?lat=" + String(lat, 4) +
              "&lon=" + String(lon, 4) +
              "&compact=true";
    }

    HTTPClient http;
    http.useHTTP10(true);
    http.setConnectTimeout(NOAA_HTTP_TIMEOUT_MS);
    http.setTimeout(NOAA_HTTP_TIMEOUT_MS);
    http.setReuse(false);

    bool began = false;
    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    if (useTls)
    {
        secureClient.setInsecure();
        began = http.begin(secureClient, url);
    }
    else
    {
        began = http.begin(plainClient, url);
    }

    Serial.printf("[NOAA] Request URL (%s): %s\n", noaaFetchSourceName(source), url.c_str());
    if (!began)
    {
        Serial.println("[NOAA] HTTP begin failed");
        if (connectFailedOut)
            *connectFailedOut = true;
        return false;
    }

    http.addHeader("Accept", (source == NOAA_FETCH_SOURCE_DIRECT) ? "application/geo+json" : "application/json");
    http.addHeader("User-Agent", "VisionWX/1.0 (weather display firmware)");
    http.addHeader("Accept-Encoding", "identity");
    http.addHeader("Connection", "close");

    const int status = http.GET();
    Serial.printf("[NOAA] HTTP status (%s): %d\n", noaaFetchSourceName(source), status);
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

    NoaaPayloadBuffer payload;
    if (!readHttpPayloadToPsram(http, payload))
    {
        http.end();
        return false;
    }
    http.end();

    Serial.printf("[NOAA] Fetched %u bytes for %.4f,%.4f\n",
                  static_cast<unsigned>(payload.size() > 0 ? payload.size() - 1u : 0u),
                  static_cast<double>(lat),
                  static_cast<double>(lon));
    const size_t payloadLength = (payload.size() > 0) ? (payload.size() - 1u) : 0u;
    return (source == NOAA_FETCH_SOURCE_DIRECT)
               ? parseDirectAlertsPayload(payload.data(), payloadLength)
               : parseRelayAlertsPayload(payload.data(), payloadLength);
}

static void fetchNoaaAlertSummarySync(bool preserveSchedule)
{
    bool forced = false;
    unsigned long savedLastFetchAttempt = 0;
    unsigned long savedNextScheduledFetchMs = 0;
    unsigned long savedNextFetchAllowedMs = 0;
    lockNoaaState();
    forced = s_forceImmediateFetch;
    s_forceImmediateFetch = false;
    savedLastFetchAttempt = s_lastFetchAttempt;
    savedNextScheduledFetchMs = s_nextScheduledFetchMs;
    savedNextFetchAllowedMs = s_nextFetchAllowedMs;
    unlockNoaaState();
    const unsigned long fetchStartMs = millis();

    float lat = NAN;
    float lon = NAN;
    if (!resolveNoaaCoordinates(lat, lon))
    {
        Serial.println("[NOAA] Missing coordinates (set device location or timezone)");
        if (preserveSchedule)
        {
            lockNoaaState();
            s_lastFetchAttempt = savedLastFetchAttempt;
            s_nextScheduledFetchMs = savedNextScheduledFetchMs;
            s_nextFetchAllowedMs = savedNextFetchAllowedMs;
            unlockNoaaState();
        }
        lockNoaaState();
        s_fetchInProgress = false;
        if (forced)
            s_screenDirty = true;
        unlockNoaaState();
        return;
    }

    if (!preserveSchedule)
    {
        lockNoaaState();
        s_lastFetchAttempt = fetchStartMs;
        unlockNoaaState();
    }
    stampLastCheckNow();
    lockNoaaState();
    s_suppressClearHeading = preserveSchedule;
    unlockNoaaState();

    bool connectFailed = false;
    const bool fetchOk = fetchAndParseNoaaSummaryPayload(lat, lon, &connectFailed);
    bool hasAlert = false;
    size_t alertCount = 0;
    String activeAlertId;
    String activeEvent;
    lockNoaaState();
    s_suppressClearHeading = false;
    hasAlert = s_hasAlert;
    alertCount = s_alertCount;
    if (hasAlert && !s_alerts.empty())
    {
        activeAlertId = s_activeAlertId;
        activeEvent = s_alerts.front().event;
    }
    unlockNoaaState();
    if (fetchOk)
    {
        lockNoaaState();
        s_nextFetchAllowedMs = 0;
        if (!preserveSchedule)
            s_nextScheduledFetchMs = fetchStartMs + NOAA_FETCH_INTERVAL_MS;
        unlockNoaaState();
        if (preserveSchedule && !hasAlert)
            queueTemporaryAlertHeading("NO ACTIVE ALERT", NOAA_MANUAL_RESULT_HEADING_MS);
        else if (preserveSchedule)
        {
            if (alertCount > 1)
                queueTemporaryAlertHeading("ACTIVE ALERTS",
                                           NOAA_MANUAL_RESULT_HEADING_MS,
                                           noaaAlertSignature(activeAlertId),
                                           (String(alertCount) + " TOTAL").c_str());
            else
                queueTemporaryAlertHeading("WEATHER ALERT",
                                           NOAA_MANUAL_RESULT_HEADING_MS,
                                           noaaAlertSignature(activeAlertId),
                                           activeEvent.length() ? activeEvent.c_str() : "NOAA ALERT");
        }
    }
    else
    {
        Serial.println("[NOAA] Failed to fetch payload");
        lockNoaaState();
        s_nextFetchAllowedMs = millis() + (connectFailed ? NOAA_CONNECT_FAIL_BACKOFF_MS : NOAA_RETRY_BACKOFF_MS);
        if (forced)
            s_screenDirty = true;
        unlockNoaaState();
    }

    if (preserveSchedule)
    {
        lockNoaaState();
        s_lastFetchAttempt = savedLastFetchAttempt;
        s_nextScheduledFetchMs = savedNextScheduledFetchMs;
        s_nextFetchAllowedMs = savedNextFetchAllowedMs;
        unlockNoaaState();
    }

    lockNoaaState();
    s_fetchInProgress = false;
    s_screenDirty = true;
    unlockNoaaState();
}

static void fetchNoaaAlertSummarySync()
{
    fetchNoaaAlertSummarySync(false);
}

void initNoaaAlerts()
{
    ensureNoaaStateMutex();
    lockNoaaState();
    s_prevEnabled = noaaAlertsEnabled;
    s_screenDirty = true;
    s_lastCheckHHMM = "--:--";
    s_lastFetchAttempt = 0;
    s_nextScheduledFetchMs = 0;
    s_nextFetchAllowedMs = 0;
    s_unreadAlert = false;
    s_lastReadAlertId = "";
    s_forceImmediateFetch = false;
    unlockNoaaState();
    clearAlertState();
    applyNoaaLines(true);
}

void tickNoaaAlerts(unsigned long nowMs)
{
    const bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    bool prevWifiConnected = false;
    bool forceImmediateFetch = false;
    lockNoaaState();
    prevWifiConnected = s_prevWifiConnected;
    forceImmediateFetch = s_forceImmediateFetch;
    unlockNoaaState();
    if (wifiConnected != prevWifiConnected)
    {
        lockNoaaState();
        s_prevWifiConnected = wifiConnected;
        if (wifiConnected)
        {
            s_wifiConnectedSinceMs = nowMs;
            if (forceImmediateFetch)
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
        unlockNoaaState();
    }

    bool prevEnabled = false;
    lockNoaaState();
    prevEnabled = s_prevEnabled;
    unlockNoaaState();
    if (prevEnabled != noaaAlertsEnabled)
    {
        lockNoaaState();
        s_prevEnabled = noaaAlertsEnabled;
        s_screenDirty = true;
        if (noaaAlertsEnabled)
            s_forceImmediateFetch = true;
        unlockNoaaState();
    }

    pruneExpiredCachedAlerts();

    bool screenDirty = false;
    lockNoaaState();
    screenDirty = s_screenDirty;
    unlockNoaaState();
    if (screenDirty)
    {
        applyNoaaLines(false);
        lockNoaaState();
        s_screenDirty = false;
        unlockNoaaState();
    }

    if (!noaaAlertsEnabled)
    {
        lockNoaaState();
        s_manualFetchPending = false;
        unlockNoaaState();
        return;
    }

    bool manualFetchPending = false;
    lockNoaaState();
    manualFetchPending = s_manualFetchPending;
    unlockNoaaState();
    if (manualFetchPending)
    {
        lockNoaaState();
        s_manualFetchPending = false;
        s_fetchInProgress = true;
        s_screenDirty = true;
        unlockNoaaState();
        fetchNoaaAlertSummarySync(true);
        return;
    }

    if (!wifiConnected)
        return;

    forceImmediateFetch = false;
    unsigned long nextScheduledFetchMs = 0;
    unsigned long nextFetchAllowedMs = 0;
    unsigned long wifiConnectedSinceMs = 0;
    lockNoaaState();
    forceImmediateFetch = s_forceImmediateFetch;
    nextScheduledFetchMs = s_nextScheduledFetchMs;
    nextFetchAllowedMs = s_nextFetchAllowedMs;
    wifiConnectedSinceMs = s_wifiConnectedSinceMs;
    unlockNoaaState();
    if (wifiConnectedSinceMs == 0 || (nowMs - wifiConnectedSinceMs) < NOAA_WIFI_STABLE_MS)
        return;
    if (nextFetchAllowedMs != 0 && nowMs < nextFetchAllowedMs)
        return;

    if (!forceImmediateFetch)
    {
        if (nextScheduledFetchMs == 0)
        {
            lockNoaaState();
            if (s_nextScheduledFetchMs == 0)
                s_nextScheduledFetchMs = nowMs + NOAA_FETCH_INTERVAL_MS;
            nextScheduledFetchMs = s_nextScheduledFetchMs;
            unlockNoaaState();
        }
        if (static_cast<long>(nowMs - nextScheduledFetchMs) < 0)
            return;
    }

    lockNoaaState();
    s_fetchInProgress = true;
    s_screenDirty = true;
    unlockNoaaState();
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
    lockNoaaState();
    s_screenDirty = true;
    unlockNoaaState();
    resetNoaaAlertsScreenPager();
    drawNoaaAlertsScreen();
}

void notifyNoaaSettingsChanged()
{
    const bool keepNoaaScreen = (currentScreen == SCREEN_NOAA_ALERT) && noaaAlertsEnabled;
    lockNoaaState();
    s_forceImmediateFetch = noaaAlertsEnabled;
    s_screenDirty = true;
    s_lastFetchAttempt = 0;
    s_nextScheduledFetchMs = 0;
    s_nextFetchAllowedMs = 0;
    s_unreadAlert = false;
    s_manualFetchPending = false;
    s_lastReadAlertId = "";
    if (noaaAlertsEnabled)
        s_nextFetchAllowedMs = 0;
    unlockNoaaState();
    clearAlertState();

    ensureCurrentScreenAllowed();
    if (keepNoaaScreen && currentScreen == SCREEN_NOAA_ALERT)
        refreshNoaaAlertsForScreenEntry();
}

NoaaManualFetchResult requestNoaaManualFetch()
{
    if (!noaaAlertsEnabled)
        return NOAA_MANUAL_FETCH_OFF;
    lockNoaaState();
    if (s_fetchInProgress || s_manualFetchPending)
    {
        unlockNoaaState();
        return NOAA_MANUAL_FETCH_BUSY;
    }
    if (WiFi.status() != WL_CONNECTED)
    {
        unlockNoaaState();
        return NOAA_MANUAL_FETCH_BLOCKED;
    }
    s_fetchInProgress = true;
    s_screenDirty = true;
    s_manualFetchPending = true;
    unlockNoaaState();
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

#endif
