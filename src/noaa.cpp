#include "noaa.h"
#include <WiFiClientSecure.h>
#include <time.h>
#include <ESP.h>
#include "settings.h"
#include "datetimesettings.h"
#include "display.h"
#include "InfoScreen.h"
#include "tempest.h"

extern InfoScreen noaaAlertScreen;
extern WiFiUDP udp;
extern bool udpListening;
extern int localPort;

static bool s_hasAlert = false;
static bool s_screenDirty = true;
static bool s_forceImmediateFetch = false;
static bool s_fetchInProgress = false;
static bool s_prevEnabled = false;
static constexpr size_t NOAA_MAX_ALERTS = 3;
static NwsAlert s_alerts[NOAA_MAX_ALERTS];
static size_t s_alertCount = 0;
static String s_lastCheckHHMM = "--:--";
static String s_activeAlertUrl;
static String s_lastDetailedAlertUrl;
static String s_lastReadAlertUrl;
static unsigned long s_lastFetchAttempt = 0;
static unsigned long s_nextScheduledFetchMs = 0;
static unsigned long s_nextFetchAllowedMs = 0;
static bool s_prevWifiConnected = false;
static unsigned long s_wifiConnectedSinceMs = 0;
static bool s_detailFetchPending = false;
static bool s_unreadAlert = false;
static bool s_manualFetchPending = false;

static constexpr unsigned long NOAA_FETCH_INTERVAL_MS = 15UL * 60UL * 1000UL;
static constexpr unsigned long NOAA_RETRY_BACKOFF_MS = 30000UL;
static constexpr unsigned long NOAA_CONNECT_FAIL_BACKOFF_MS = 300000UL;
static constexpr unsigned long NOAA_WIFI_STABLE_MS = 30000UL;
static constexpr unsigned long NOAA_SCREEN_ENTRY_DELAY_MS = 1500UL;
static constexpr unsigned long NOAA_BOOT_SETTLE_MS = 180000UL;
static constexpr unsigned long NOAA_WIFI_RECONNECT_QUIET_MS = 45000UL;
static constexpr size_t NOAA_MAX_BODY_BYTES = 16384;
static constexpr const char *NOAA_HOST = "api.weather.gov";
static constexpr uint16_t NOAA_PORT = 443;
static constexpr unsigned long NOAA_CONNECT_TIMEOUT_MS = 5000UL;
static constexpr unsigned long NOAA_READ_TIMEOUT_MS = 12000UL;
static constexpr uint8_t NOAA_CONNECT_RETRIES = 1;
static constexpr uint32_t NOAA_MIN_FREE_HEAP = 64000UL;
static constexpr uint32_t NOAA_MIN_MAX_ALLOC = 32000UL;

static bool parseNoaaIsoUtc(const String &isoRaw, DateTime &utcOut);
static bool noaaCurrentUtc(DateTime &utcOut);
static bool noaaNeedsDetailFetch();

static bool noaaCoordsValid(float lat, float lon)
{
    return isfinite(lat) && isfinite(lon) &&
           lat >= -90.0f && lat <= 90.0f &&
           lon >= -180.0f && lon <= 180.0f &&
           !(fabs(lat) < 0.001f && fabs(lon) < 0.001f);
}

static bool resolveNoaaCoordinates(float &lat, float &lon)
{
    // Prefer the device's configured location first.
    if (noaaCoordsValid(noaaLatitude, noaaLongitude))
    {
        lat = noaaLatitude;
        lon = noaaLongitude;
        return true;
    }

    // Fall back to the current timezone/city coordinates when device location
    // has not been configured yet.
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

static bool pauseUdpTrafficForNoaa()
{
    if (!udpListening)
        return false;
    udp.stop();
    udpListening = false;
    Serial.println("[NOAA] Paused UDP listener for NOAA fetch");
    delay(150);
    return true;
}

static void resumeUdpTrafficAfterNoaa(bool pausedUdp)
{
    if (!pausedUdp)
        return;
    if (WiFi.status() != WL_CONNECTED)
        return;
    if (udp.begin(localPort))
    {
        udpListening = true;
        Serial.println("[NOAA] Resumed UDP listener after NOAA fetch");
    }
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

static void clearAlertCache()
{
    s_alertCount = 0;
    for (size_t i = 0; i < NOAA_MAX_ALERTS; ++i)
        s_alerts[i] = NwsAlert{};
}

static void clearAlertFields()
{
    s_activeAlertUrl = "";
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
    if (alert.status.length() > 0 && !alert.status.equalsIgnoreCase("Actual"))
        return false;
    if (alert.messageType.equalsIgnoreCase("Cancel"))
        return false;

    DateTime expiryUtc;
    if (noaaAlertExpiryUtc(alert, expiryUtc))
        return nowUtc.unixtime() <= expiryUtc.unixtime();

    return true;
}

static void pruneExpiredCachedAlerts()
{
    if (s_alertCount == 0)
        return;

    DateTime nowUtc;
    if (!noaaCurrentUtc(nowUtc))
        return;

    size_t writeIndex = 0;
    for (size_t readIndex = 0; readIndex < s_alertCount; ++readIndex)
    {
        if (!noaaAlertIsActiveNow(s_alerts[readIndex], nowUtc))
            continue;

        if (writeIndex != readIndex)
            s_alerts[writeIndex] = s_alerts[readIndex];
        ++writeIndex;
    }

    if (writeIndex == s_alertCount)
        return;

    for (size_t i = writeIndex; i < NOAA_MAX_ALERTS; ++i)
        s_alerts[i] = NwsAlert{};

    s_alertCount = writeIndex;
    s_hasAlert = (s_alertCount > 0);
    s_activeAlertUrl = s_hasAlert ? s_alerts[0].url : "";
    if (!s_hasAlert)
    {
        s_lastDetailedAlertUrl = "";
        s_unreadAlert = false;
        s_detailFetchPending = false;
    }
    else
    {
        s_detailFetchPending = noaaNeedsDetailFetch();
        if (s_activeAlertUrl == s_lastReadAlertUrl)
            s_unreadAlert = false;
    }
    s_screenDirty = true;
}

static void initNoaaStringStorage()
{
    // Keep NOAA state lazy-allocated so TLS has the largest contiguous heap
    // window possible when WiFiClientSecure starts the handshake.
}

static void lockNoaaState()
{
}

static void unlockNoaaState()
{
}

static void markNoaaAlertRead()
{
    lockNoaaState();
    s_unreadAlert = false;
    if (s_activeAlertUrl.length() > 0)
        s_lastReadAlertUrl = s_activeAlertUrl;
    unlockNoaaState();
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

static String extractRawFieldAfter(const String &payload, const String &key, int searchFrom)
{
    String needle = "\"" + key + "\"";
    int pos = payload.indexOf(needle, searchFrom);
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

static void assignRawField(String &dest, const String &payload, const String &key, int searchFrom)
{
    dest = extractRawFieldAfter(payload, key, searchFrom);
    cleanRawExtractInPlace(dest);
}

static int skipJsonWhitespace(const String &payload, int pos)
{
    while (pos < payload.length())
    {
        char c = payload.charAt(pos);
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            break;
        ++pos;
    }
    return pos;
}

static bool rawArrayKeyIsEmpty(const String &payload, const String &key, int searchFrom)
{
    String needle = "\"" + key + "\"";
    int pos = payload.indexOf(needle, searchFrom);
    if (pos < 0)
        return false;
    pos = payload.indexOf(':', pos + needle.length());
    if (pos < 0)
        return false;
    pos = skipJsonWhitespace(payload, pos + 1);
    if (pos >= payload.length() || payload.charAt(pos) != '[')
        return false;
    pos = skipJsonWhitespace(payload, pos + 1);
    return (pos < payload.length() && payload.charAt(pos) == ']');
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
    out = s_alerts[0];
    return true;
}

static void applyNoaaLines(bool forceReset)
{
    String lines[INFOSCREEN_MAX_LINES];
    uint16_t colors[INFOSCREEN_MAX_LINES] = {0};
    int count = 0;
    bool useColors = false;
    bool alertsEnabled = false;
    bool fetchInProgress = false;
    bool hasAlert = false;
    String lastCheck;
    NwsAlert primary;
    bool havePrimary = false;

    lockNoaaState();
    alertsEnabled = noaaAlertsEnabled;
    fetchInProgress = s_fetchInProgress;
    hasAlert = s_hasAlert;
    lastCheck = s_lastCheckHHMM;
    havePrimary = snapshotPrimaryAlert(primary);
    unlockNoaaState();

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

    if (!alertsEnabled)
    {
        push("", "Alerts Disabled", 0);
        push("Severity", "--", 0);
        push("", "Enable NOAA alerts in menu.", 0);
    }
    else if (fetchInProgress)
    {
        push("", "Get Alert info ...", 0);
        push("Status", "Checking NOAA feed", 0);
        push("Last", lastCheck, 0);
    }
    else if (!hasAlert)
    {
        push("", "No Active Alert", 0);
        push("Severity", "None", 0);
        push("", "Monitoring NOAA feed...", 0);
    }
    else
    {
        if (!havePrimary)
            primary = NwsAlert{};
        push("Event", primary.event.length() ? primary.event : "Alert", dma_display ? dma_display->color565(255, 255, 255) : 0);
        push("Severity", primary.severity.length() ? primary.severity : "Unknown", severityColor(primary.severity));
        if (primary.headline.length())
            push("Headline", primary.headline, 0);
        if (primary.areaDesc.length())
            push("Area", primary.areaDesc, 0);
        if (primary.senderName.length())
            push("Sender", primary.senderName, 0);
        if (primary.urgency.length())
            push("Urgency", primary.urgency, 0);
        if (primary.certainty.length())
            push("Certainty", primary.certainty, 0);
        if (primary.response.length())
            push("Response", primary.response, 0);
        if (primary.effective.length())
            push("Effective", primary.effective, 0);
        if (primary.expires.length())
            push("Expires", primary.expires, 0);
        if (primary.ends.length())
            push("Ends", primary.ends, 0);
        push("Description", primary.description.length() ? primary.description : "No description provided.", dma_display ? dma_display->color565(150, 200, 255) : 0);
        if (primary.instruction.length())
            push("Instruction", primary.instruction, dma_display ? dma_display->color565(150, 200, 255) : 0);
        if (primary.note.length())
            push("Note", primary.note, dma_display ? dma_display->color565(150, 200, 255) : 0);
    }

    if (count == 0)
    {
        lines[0] = "No alert data";
        count = 1;
    }

    bool resetPosition = forceReset || !noaaAlertScreen.isActive();
    noaaAlertScreen.setLines(lines, count, resetPosition, useColors ? colors : nullptr);
}

static bool waitForClientData(WiFiClientSecure &client, unsigned long timeoutMs)
{
    unsigned long startMs = millis();
    while ((millis() - startMs) < timeoutMs)
    {
        if (WiFi.status() != WL_CONNECTED)
            return false;
        if (client.available() > 0)
            return true;
        if (!client.connected())
            return false;
        delay(1);
    }
    return client.available() > 0;
}

static bool noaaHandshakeHeapOkay()
{
    const uint32_t freeHeap = ESP.getFreeHeap();
    const uint32_t maxAlloc = ESP.getMaxAllocHeap();
    if (freeHeap < NOAA_MIN_FREE_HEAP || maxAlloc < NOAA_MIN_MAX_ALLOC)
    {
        Serial.printf("[NOAA] Skip fetch, heap low free=%u/%u maxAlloc=%u/%u\n",
                      static_cast<unsigned>(freeHeap),
                      static_cast<unsigned>(NOAA_MIN_FREE_HEAP),
                      static_cast<unsigned>(maxAlloc),
                      static_cast<unsigned>(NOAA_MIN_MAX_ALLOC));
        return false;
    }
    return true;
}

static void logNoaaFetchDiagnostics(const char *phase)
{
    Serial.printf("[NOAA] Diag %s RSSI=%d dBm ch=%d freeHeap=%u maxAlloc=%u wifi=%d\n",
                  phase ? phase : "",
                  WiFi.RSSI(),
                  WiFi.channel(),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxAllocHeap()),
                  static_cast<int>(WiFi.status()));
}

static void deferNoaaUntil(unsigned long whenMs)
{
    if (s_nextFetchAllowedMs == 0 || static_cast<long>(whenMs - s_nextFetchAllowedMs) > 0)
        s_nextFetchAllowedMs = whenMs;
}

static bool summaryPayloadHasFullAlertText(const String &description, const String &instruction)
{
    return description.length() > 0 || instruction.length() > 0;
}

static bool parseNoaaAlertFromPropertiesSlice(const String &propsPayload, NwsAlert &alertOut, String &alertUrlOut)
{
    alertOut = NwsAlert{};
    alertUrlOut = "";

    assignRawField(alertUrlOut, propsPayload, "@id", 0);
    alertOut.url = alertUrlOut;
    assignRawField(alertOut.id, propsPayload, "id", 0);
    if (alertOut.id.length() == 0)
        alertOut.id = alertUrlOut;
    assignRawField(alertOut.sent, propsPayload, "sent", 0);
    assignRawField(alertOut.effective, propsPayload, "effective", 0);
    assignRawField(alertOut.onset, propsPayload, "onset", 0);
    assignRawField(alertOut.event, propsPayload, "event", 0);
    if (alertOut.event.length() == 0)
        alertOut.event = "NOAA Alert";
    assignRawField(alertOut.status, propsPayload, "status", 0);
    assignRawField(alertOut.messageType, propsPayload, "messageType", 0);
    assignRawField(alertOut.category, propsPayload, "category", 0);
    assignRawField(alertOut.severity, propsPayload, "severity", 0);
    assignRawField(alertOut.certainty, propsPayload, "certainty", 0);
    assignRawField(alertOut.urgency, propsPayload, "urgency", 0);
    assignRawField(alertOut.areaDesc, propsPayload, "areaDesc", 0);
    assignRawField(alertOut.sender, propsPayload, "sender", 0);
    assignRawField(alertOut.senderName, propsPayload, "senderName", 0);
    assignRawField(alertOut.headline, propsPayload, "headline", 0);
    assignRawField(alertOut.description, propsPayload, "description", 0);
    assignRawField(alertOut.instruction, propsPayload, "instruction", 0);
    assignRawField(alertOut.response, propsPayload, "response", 0);
    assignRawField(alertOut.note, propsPayload, "note", 0);
    assignRawField(alertOut.expires, propsPayload, "expires", 0);
    assignRawField(alertOut.ends, propsPayload, "ends", 0);
    assignRawField(alertOut.scope, propsPayload, "scope", 0);
    assignRawField(alertOut.language, propsPayload, "language", 0);
    assignRawField(alertOut.web, propsPayload, "web", 0);

    if (alertOut.description.length() == 0)
        alertOut.description = alertOut.headline.length() ? alertOut.headline : "Detail update pending.";

    return alertOut.event.length() > 0 || alertOut.headline.length() > 0 || alertOut.description.length() > 0;
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

static bool noaaAlertIdExists(const NwsAlert *alerts, size_t count, const String &id)
{
    if (id.length() == 0)
        return false;
    for (size_t i = 0; i < count; ++i)
    {
        if (alerts[i].id == id)
            return true;
    }
    return false;
}

static int findNextPropertiesPos(const String &payload, int searchFrom)
{
    return payload.indexOf("\"properties\"", searchFrom);
}

static int findMatchingBrace(const String &payload, int openBracePos)
{
    if (openBracePos < 0 || openBracePos >= payload.length() || payload.charAt(openBracePos) != '{')
        return -1;

    bool inString = false;
    bool escape = false;
    int depth = 0;
    for (int i = openBracePos; i < payload.length(); ++i)
    {
        char c = payload.charAt(i);
        if (inString)
        {
            if (escape)
            {
                escape = false;
            }
            else if (c == '\\')
            {
                escape = true;
            }
            else if (c == '"')
            {
                inString = false;
            }
            continue;
        }

        if (c == '"')
        {
            inString = true;
            continue;
        }
        if (c == '{')
        {
            ++depth;
            continue;
        }
        if (c == '}')
        {
            --depth;
            if (depth == 0)
                return i;
        }
    }
    return -1;
}

static int findMatchingBracket(const String &payload, int openBracketPos)
{
    if (openBracketPos < 0 || openBracketPos >= payload.length() || payload.charAt(openBracketPos) != '[')
        return -1;

    bool inString = false;
    bool escape = false;
    int depth = 0;
    for (int i = openBracketPos; i < payload.length(); ++i)
    {
        char c = payload.charAt(i);
        if (inString)
        {
            if (escape)
                escape = false;
            else if (c == '\\')
                escape = true;
            else if (c == '"')
                inString = false;
            continue;
        }

        if (c == '"')
        {
            inString = true;
            continue;
        }
        if (c == '[')
        {
            ++depth;
            continue;
        }
        if (c == ']')
        {
            --depth;
            if (depth == 0)
                return i;
        }
    }
    return -1;
}

static bool extractPropertiesPayloadFromFeature(const String &featurePayload, String &propsPayloadOut)
{
    propsPayloadOut = "";
    const int propsPos = featurePayload.indexOf("\"properties\"");
    if (propsPos < 0)
        return false;

    int colonPos = featurePayload.indexOf(':', propsPos);
    if (colonPos < 0)
        return false;
    int bracePos = skipJsonWhitespace(featurePayload, colonPos + 1);
    if (bracePos < 0 || bracePos >= featurePayload.length() || featurePayload.charAt(bracePos) != '{')
        return false;

    int closePos = findMatchingBrace(featurePayload, bracePos);
    if (closePos < 0)
        return false;

    propsPayloadOut = featurePayload.substring(bracePos, closePos + 1);
    return true;
}

static bool parseNoaaSummaryPayload(const String &payload)
{
    const int featuresPos = payload.indexOf("\"features\"");
    if (featuresPos < 0)
        return false;

    if (rawArrayKeyIsEmpty(payload, "features", featuresPos))
    {
        lockNoaaState();
        clearAlertFields();
        clearAlertCache();
        s_hasAlert = false;
        s_screenDirty = true;
        unlockNoaaState();
        return true;
    }

    const int featuresColonPos = payload.indexOf(':', featuresPos);
    if (featuresColonPos < 0)
        return false;
    const int featuresArrayPos = skipJsonWhitespace(payload, featuresColonPos + 1);
    if (featuresArrayPos < 0 || payload.charAt(featuresArrayPos) != '[')
        return false;
    const int featuresArrayEnd = findMatchingBracket(payload, featuresArrayPos);
    String featuresPayload;
    if (featuresArrayEnd < 0)
    {
        Serial.println("[NOAA] Incomplete features array; parsing partial payload");
        featuresPayload = payload.substring(featuresArrayPos + 1);
    }
    else
    {
        featuresPayload = payload.substring(featuresArrayPos + 1, featuresArrayEnd);
    }

    size_t parsedAlerts = 0;
    NwsAlert parsedSummaries[NOAA_MAX_ALERTS];
    DateTime nowUtc;
    const bool haveNowUtc = noaaCurrentUtc(nowUtc);
    int searchPos = 0;
    NwsAlert primaryAlert;
    bool primaryHasFullText = false;
    while (parsedAlerts < NOAA_MAX_ALERTS)
    {
        searchPos = skipJsonWhitespace(featuresPayload, searchPos);
        if (searchPos < 0 || searchPos >= featuresPayload.length())
            break;
        if (featuresPayload.charAt(searchPos) == ',')
        {
            ++searchPos;
            continue;
        }
        if (featuresPayload.charAt(searchPos) != '{')
            break;

        const int featureEnd = findMatchingBrace(featuresPayload, searchPos);
        if (featureEnd < 0)
            break;

        String featurePayload = featuresPayload.substring(searchPos, featureEnd + 1);
        String propsPayload;
        const int nextSearchPos = featureEnd + 1;
        featurePayload.reserve(static_cast<unsigned>(featureEnd - searchPos + 2));
        propsPayload.reserve(static_cast<unsigned>(featureEnd - searchPos + 2));
        if (!extractPropertiesPayloadFromFeature(featurePayload, propsPayload))
        {
            searchPos = nextSearchPos;
            continue;
        }

        NwsAlert alert;
        String alertUrl;
        if (!parseNoaaAlertFromPropertiesSlice(propsPayload, alert, alertUrl))
        {
            searchPos = nextSearchPos;
            continue;
        }

        if (haveNowUtc && !noaaAlertIsActiveNow(alert, nowUtc))
        {
            searchPos = nextSearchPos;
            continue;
        }

        if (noaaAlertIdExists(parsedSummaries, parsedAlerts, alert.id))
        {
            searchPos = nextSearchPos;
            continue;
        }

        parsedSummaries[parsedAlerts] = alert;

        if (parsedAlerts == 0)
        {
            primaryAlert = alert;
            primaryHasFullText = summaryPayloadHasFullAlertText(alert.description, alert.instruction);
        }

        Serial.printf("[NOAA] Alert %u event=%u headline=%u desc=%u instr=%u url=%u\n",
                      static_cast<unsigned>(parsedAlerts + 1),
                      static_cast<unsigned>(alert.event.length()),
                      static_cast<unsigned>(alert.headline.length()),
                      static_cast<unsigned>(alert.description.length()),
                      static_cast<unsigned>(alert.instruction.length()),
                      static_cast<unsigned>(alertUrl.length()));

        ++parsedAlerts;
        searchPos = nextSearchPos;
    }

    if (parsedAlerts == 0)
    {
        const int propsPos = payload.indexOf("\"properties\"");
        if (propsPos >= 0)
        {
            NwsAlert fallbackAlert;
            String fallbackUrl;
            String fallbackProps = payload.substring(propsPos);
            if (parseNoaaAlertFromPropertiesSlice(fallbackProps, fallbackAlert, fallbackUrl))
            {
                if (haveNowUtc && !noaaAlertIsActiveNow(fallbackAlert, nowUtc))
                {
                    lockNoaaState();
                    clearAlertFields();
                    clearAlertCache();
                    s_hasAlert = false;
                    s_unreadAlert = false;
                    s_lastDetailedAlertUrl = "";
                    s_screenDirty = true;
                    unlockNoaaState();
                    Serial.println("[NOAA] Parsed 0 alert(s)");
                    return true;
                }
                lockNoaaState();
                clearAlertFields();
                clearAlertCache();
                s_activeAlertUrl = fallbackAlert.url;
                s_alerts[0] = fallbackAlert;
                s_alertCount = 1;
                s_hasAlert = true;
                s_lastDetailedAlertUrl = summaryPayloadHasFullAlertText(fallbackAlert.description, fallbackAlert.instruction) ? fallbackAlert.url : "";
                s_screenDirty = true;
                unlockNoaaState();
                Serial.printf("[NOAA] Fallback parsed first alert event=%u headline=%u desc=%u instr=%u\n",
                              static_cast<unsigned>(fallbackAlert.event.length()),
                              static_cast<unsigned>(fallbackAlert.headline.length()),
                              static_cast<unsigned>(fallbackAlert.description.length()),
                              static_cast<unsigned>(fallbackAlert.instruction.length()));
                return true;
            }
        }

        lockNoaaState();
        clearAlertFields();
        clearAlertCache();
        s_hasAlert = false;
        s_unreadAlert = false;
        s_lastDetailedAlertUrl = "";
        s_screenDirty = true;
        unlockNoaaState();
        Serial.println("[NOAA] Parsed 0 alert(s)");
        return true;
    }

    lockNoaaState();
    clearAlertFields();
    clearAlertCache();
    s_alertCount = parsedAlerts;
    for (size_t i = 0; i < parsedAlerts; ++i)
        s_alerts[i] = parsedSummaries[i];
    s_hasAlert = true;
    s_activeAlertUrl = s_alerts[0].url;
    s_lastDetailedAlertUrl = primaryHasFullText ? s_activeAlertUrl : "";
    s_screenDirty = true;
    unlockNoaaState();
    Serial.printf("[NOAA] Summary fields desc=%u instr=%u certainty=%u response=%u effective=%u ends=%u\n",
                  static_cast<unsigned>(primaryAlert.description.length()),
                  static_cast<unsigned>(primaryAlert.instruction.length()),
                  static_cast<unsigned>(primaryAlert.certainty.length()),
                  static_cast<unsigned>(primaryAlert.response.length()),
                  static_cast<unsigned>(primaryAlert.effective.length()),
                  static_cast<unsigned>(primaryAlert.ends.length()));
    Serial.printf("[NOAA] Parsed %u alert(s)\n", static_cast<unsigned>(parsedAlerts));
    return true;
}

static bool parseNoaaDetailPayload(const String &payload)
{
    const int propsPos = payload.indexOf("\"properties\"");
    if (propsPos < 0)
        return false;

    String description;
    String instruction;
    String note;
    String effective;
    String ends;
    String certainty;
    String response;
    assignRawField(description, payload, "description", propsPos);
    assignRawField(instruction, payload, "instruction", propsPos);
    assignRawField(note, payload, "note", propsPos);
    assignRawField(effective, payload, "effective", propsPos);
    assignRawField(ends, payload, "ends", propsPos);
    assignRawField(certainty, payload, "certainty", propsPos);
    assignRawField(response, payload, "response", propsPos);
    if (description.length() == 0)
        description = "No description provided.";

    lockNoaaState();
    if (s_alertCount > 0)
    {
        s_alerts[0].description = description;
        s_alerts[0].instruction = instruction;
        s_alerts[0].note = note;
        s_alerts[0].effective = effective;
        s_alerts[0].ends = ends;
        s_alerts[0].certainty = certainty;
        s_alerts[0].response = response;
    }
    s_screenDirty = true;
    unlockNoaaState();
    return true;
}

static bool readNoaaBody(WiFiClientSecure &client, int contentLength, String &bodyOut)
{
    bodyOut = "";
    if (contentLength > 0 && contentLength > static_cast<int>(NOAA_MAX_BODY_BYTES))
    {
        Serial.printf("[NOAA] Payload too large: %d bytes\n", contentLength);
        return false;
    }

    if (contentLength > 0)
        bodyOut.reserve(static_cast<unsigned>(contentLength + 1));
    else
        bodyOut.reserve(4096);

    char buffer[256];
    unsigned long lastDataMs = millis();
    while (client.connected() || client.available() > 0)
    {
        int available = client.available();
        if (available <= 0)
        {
            if ((millis() - lastDataMs) >= NOAA_READ_TIMEOUT_MS)
                break;
            delay(1);
            continue;
        }

        int toRead = available;
        if (toRead > static_cast<int>(sizeof(buffer)))
            toRead = static_cast<int>(sizeof(buffer));

        int n = client.readBytes(buffer, static_cast<size_t>(toRead));
        if (n <= 0)
            continue;

        lastDataMs = millis();

        if ((bodyOut.length() + n) > NOAA_MAX_BODY_BYTES)
        {
            Serial.printf("[NOAA] Body exceeded %u bytes\n", static_cast<unsigned>(NOAA_MAX_BODY_BYTES));
            return false;
        }

        for (int i = 0; i < n; ++i)
        {
            if (buffer[i] != '\0')
                bodyOut += buffer[i];
        }

        if (contentLength > 0 && bodyOut.length() >= contentLength)
            break;
    }

    return bodyOut.length() > 0;
}

static bool fetchAndParseNoaaPath(const String &path, bool detailRequest, float lat, float lon, bool &connectFailed)
{
    connectFailed = false;
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[NOAA] WiFi not connected");
        connectFailed = true;
        return false;
    }

    logNoaaFetchDiagnostics("before connect");

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(NOAA_CONNECT_TIMEOUT_MS / 1000UL);

    bool connected = false;
    for (uint8_t attempt = 1; attempt <= NOAA_CONNECT_RETRIES; ++attempt)
    {
        Serial.printf("[NOAA] Connect attempt %u/%u\n", static_cast<unsigned>(attempt), static_cast<unsigned>(NOAA_CONNECT_RETRIES));
        if (client.connect(NOAA_HOST, NOAA_PORT, NOAA_CONNECT_TIMEOUT_MS))
        {
            connected = true;
            break;
        }
        client.stop();
        delay(150);
    }

    if (!connected)
    {
        logNoaaFetchDiagnostics("connect failed");
        Serial.println("[NOAA] Failed to connect to api.weather.gov");
        connectFailed = true;
        return false;
    }

    client.setTimeout(NOAA_READ_TIMEOUT_MS / 1000UL);

    Serial.printf("[NOAA] Request URL: https://%s%s\n", NOAA_HOST, path.c_str());
    client.print(
        String("GET ") + path + " HTTP/1.1\r\n" +
        "Host: " + NOAA_HOST + "\r\n" +
        "User-Agent: VisionWX/1.0 (weather display firmware)\r\n" +
        "Accept: application/geo+json\r\n" +
        "Accept-Encoding: identity\r\n" +
        "Connection: close\r\n\r\n");

    if (!waitForClientData(client, NOAA_READ_TIMEOUT_MS))
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            logNoaaFetchDiagnostics("wifi dropped");
            Serial.println("[NOAA] WiFi dropped while waiting for HTTP response");
        }
        else
            Serial.println("[NOAA] Timed out waiting for HTTP response");
        client.stop();
        return false;
    }

    String statusLine = client.readStringUntil('\n');
    statusLine.trim();
    if (statusLine.length() == 0)
    {
        Serial.println("[NOAA] Empty HTTP status line");
        client.stop();
        return false;
    }

    int firstSpace = statusLine.indexOf(' ');
    int secondSpace = (firstSpace >= 0) ? statusLine.indexOf(' ', firstSpace + 1) : -1;
    int httpStatus = (firstSpace >= 0)
                         ? statusLine.substring(firstSpace + 1, secondSpace > 0 ? secondSpace : statusLine.length()).toInt()
                         : -1;
    Serial.printf("[NOAA] HTTP status: %d\n", httpStatus);
    if (httpStatus != 200)
    {
        if (WiFi.status() != WL_CONNECTED)
            connectFailed = true;
        client.stop();
        return false;
    }

    int contentLength = -1;
    while (client.connected())
    {
        if (!waitForClientData(client, NOAA_READ_TIMEOUT_MS))
            break;
        String line = client.readStringUntil('\n');
        if (line == "\r" || line.length() == 0)
            break;
        line.trim();
        if (line.startsWith("Content-Length:"))
            contentLength = line.substring(strlen("Content-Length:")).toInt();
    }

    String payload;
    if (!readNoaaBody(client, contentLength, payload))
    {
        client.stop();
        Serial.println("[NOAA] Failed to read payload body");
        return false;
    }

    client.stop();
    Serial.printf("[NOAA] Fetched %u bytes for %.4f,%.4f\n",
                  static_cast<unsigned>(payload.length()),
                  static_cast<double>(lat),
                  static_cast<double>(lon));
    bool ok = detailRequest ? parseNoaaDetailPayload(payload) : parseNoaaSummaryPayload(payload);
    if (!ok && WiFi.status() != WL_CONNECTED)
        Serial.println("[NOAA] WiFi dropped during payload read");
    Serial.printf("[NOAA] %s parse %s for %.4f,%.4f\n",
                  detailRequest ? "detail" : "summary",
                  ok ? "ok" : "failed",
                  static_cast<double>(lat),
                  static_cast<double>(lon));
    return ok;
}

static bool fetchAndParseNoaaSummaryPayload(const float lat, const float lon, bool *connectFailedOut = nullptr)
{
    String path = "/alerts/active?point=" + String(lat, 4) + "," + String(lon, 4);
    Serial.printf("[NOAA] Summary request for %.4f,%.4f\n",
                  static_cast<double>(lat),
                  static_cast<double>(lon));
    bool connectFailed = false;
    bool ok = fetchAndParseNoaaPath(path, false, lat, lon, connectFailed);
    if (connectFailedOut)
        *connectFailedOut = connectFailed;
    if (!ok && connectFailed)
        deferNoaaUntil(millis() + NOAA_CONNECT_FAIL_BACKOFF_MS);
    return ok;
}

static bool pathFromNoaaUrl(const String &url, String &pathOut)
{
    const String prefix = String("https://") + NOAA_HOST;
    if (!url.startsWith(prefix))
        return false;
    int slash = url.indexOf('/', prefix.length());
    if (slash < 0)
        return false;
    pathOut = url.substring(slash);
    return pathOut.length() > 0;
}

static bool fetchAndParseNoaaDetailPayload(const float lat, const float lon, bool *connectFailedOut = nullptr)
{
    if (s_alertCount == 0 || s_alerts[0].url.length() == 0)
        return false;

    String path;
    if (!pathFromNoaaUrl(s_alerts[0].url, path))
    {
        Serial.printf("[NOAA] Unsupported detail URL: %s\n", s_alerts[0].url.c_str());
        return false;
    }

    Serial.printf("[NOAA] Detail request source URL: %s\n", s_alerts[0].url.c_str());
    bool connectFailed = false;
    bool ok = fetchAndParseNoaaPath(path, true, lat, lon, connectFailed);
    if (connectFailedOut)
        *connectFailedOut = connectFailed;
    if (!ok && connectFailed)
        deferNoaaUntil(millis() + NOAA_CONNECT_FAIL_BACKOFF_MS);
    return ok;
}

static bool noaaNeedsDetailFetch()
{
    return s_hasAlert &&
           s_alertCount > 0 &&
           s_alerts[0].url.length() > 0 &&
           s_alerts[0].url != s_lastDetailedAlertUrl;
}

static void fetchNoaaAlertSummarySync(bool preserveSchedule)
{
    bool forced = s_forceImmediateFetch;
    s_forceImmediateFetch = false;
    bool pausedUdp = false;
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

      if (!noaaHandshakeHeapOkay())
      {
          if (preserveSchedule)
          {
              s_lastFetchAttempt = savedLastFetchAttempt;
              s_nextScheduledFetchMs = savedNextScheduledFetchMs;
              s_nextFetchAllowedMs = savedNextFetchAllowedMs;
          }
          else
          {
              s_nextFetchAllowedMs = millis() + NOAA_RETRY_BACKOFF_MS;
          }
          s_fetchInProgress = false;
          s_screenDirty = true;
          return;
      }
      pausedUdp = pauseUdpTrafficForNoaa();
      bool connectFailed = false;
      bool fetchOk = fetchAndParseNoaaSummaryPayload(lat, lon, &connectFailed);
      if (fetchOk)
      {
        if (s_hasAlert && s_activeAlertUrl.length() > 0)
        {
            if (noaaAlertScreen.isActive())
                markNoaaAlertRead();
            else if (s_activeAlertUrl != s_lastReadAlertUrl)
                s_unreadAlert = true;
        }
        else
        {
            s_unreadAlert = false;
        }
        s_detailFetchPending = noaaNeedsDetailFetch();
        if (!s_hasAlert)
            s_lastDetailedAlertUrl = "";
        s_nextFetchAllowedMs = 0;
        if (!preserveSchedule)
            s_nextScheduledFetchMs = fetchStartMs + NOAA_FETCH_INTERVAL_MS;
        if (preserveSchedule)
            showSectionHeading("ALERT DONE", nullptr, 1200);
    }
    else
    {
        Serial.println("[NOAA] Failed to fetch payload");
        s_nextFetchAllowedMs = millis() + NOAA_RETRY_BACKOFF_MS;
          if (forced)
          {
              s_screenDirty = true;
          }
      }
      if (preserveSchedule)
      {
          s_lastFetchAttempt = savedLastFetchAttempt;
          s_nextScheduledFetchMs = savedNextScheduledFetchMs;
          s_nextFetchAllowedMs = savedNextFetchAllowedMs;
      }
      resumeUdpTrafficAfterNoaa(pausedUdp);
      s_fetchInProgress = false;
      s_screenDirty = true;
  }

static void fetchNoaaAlertSummarySync()
{
    fetchNoaaAlertSummarySync(false);
}

static void fetchNoaaAlertDetailSync()
{
    if (!noaaNeedsDetailFetch())
    {
        s_detailFetchPending = false;
        s_fetchInProgress = false;
        s_screenDirty = true;
        return;
    }

      float lat = NAN;
      float lon = NAN;
      bool pausedUdp = false;
      if (!resolveNoaaCoordinates(lat, lon))
      {
          Serial.println("[NOAA] Missing coordinates for detail fetch");
          s_fetchInProgress = false;
          s_screenDirty = true;
        return;
    }

      if (!noaaHandshakeHeapOkay())
      {
          s_nextFetchAllowedMs = millis() + NOAA_RETRY_BACKOFF_MS;
          s_fetchInProgress = false;
          s_screenDirty = true;
          return;
      }
      pausedUdp = pauseUdpTrafficForNoaa();
      bool connectFailed = false;
      bool fetchOk = fetchAndParseNoaaDetailPayload(lat, lon, &connectFailed);
      if (fetchOk)
      {
        s_lastDetailedAlertUrl = s_activeAlertUrl;
        if (s_alertCount > 0)
            s_lastDetailedAlertUrl = s_alerts[0].url;
        s_detailFetchPending = false;
        s_nextFetchAllowedMs = 0;
    }
      else
      {
          Serial.println("[NOAA] Failed to fetch detail payload");
          s_nextFetchAllowedMs = millis() + NOAA_RETRY_BACKOFF_MS;
      }

      resumeUdpTrafficAfterNoaa(pausedUdp);
      s_fetchInProgress = false;
      s_screenDirty = true;
}

void initNoaaAlerts()
{
    initNoaaStringStorage();
    s_prevEnabled = noaaAlertsEnabled;
    s_screenDirty = true;
    s_hasAlert = false;
    clearAlertCache();
    clearAlertFields();
    s_lastFetchAttempt = 0;
    s_nextScheduledFetchMs = 0;
    s_nextFetchAllowedMs = 0;
    s_detailFetchPending = false;
    s_unreadAlert = false;
    s_lastReadAlertUrl = "";
    s_forceImmediateFetch = noaaAlertsEnabled;
    if (noaaAlertsEnabled)
        deferNoaaUntil(millis() + NOAA_BOOT_SETTLE_MS);
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
            Serial.println("[NOAA] WiFi connected, waiting for link to stabilize");
            deferNoaaUntil(nowMs + NOAA_WIFI_RECONNECT_QUIET_MS);
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

    if (noaaAlertScreen.isActive() && s_detailFetchPending)
    {
        s_fetchInProgress = true;
        s_screenDirty = true;
        fetchNoaaAlertDetailSync();
        return;
    }

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
    s_forceImmediateFetch = noaaAlertsEnabled;
    s_screenDirty = true;
    s_hasAlert = false;
    clearAlertCache();
    clearAlertFields();
    s_lastFetchAttempt = 0;
    s_nextScheduledFetchMs = 0;
    s_nextFetchAllowedMs = 0;
    s_detailFetchPending = false;
    s_unreadAlert = false;
    s_manualFetchPending = false;
    s_lastReadAlertUrl = "";
    if (noaaAlertsEnabled)
        deferNoaaUntil(millis() + NOAA_BOOT_SETTLE_MS);
}

NoaaManualFetchResult requestNoaaManualFetch()
{
    if (!noaaAlertsEnabled)
        return NOAA_MANUAL_FETCH_OFF;
    if (s_fetchInProgress || s_manualFetchPending)
        return NOAA_MANUAL_FETCH_BUSY;
    if (WiFi.status() != WL_CONNECTED)
        return NOAA_MANUAL_FETCH_BLOCKED;
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
    String severity = (s_alertCount > 0) ? s_alerts[0].severity : "";
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
