#include "worldtime.h"

#include <Preferences.h>
#include <algorithm>
#include <time.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "datetimesettings.h"
#include "units.h"

static std::vector<int> s_worldTimeZoneIndices;
static int s_worldTimeViewIndex = -1; // -1 => system clock view, >=0 => index into s_worldTimeZoneIndices

static constexpr const char *kPrefsNs = "visionwx";
static constexpr const char *kPrefsKeyIds = "wt_ids";
static constexpr const char *kWorldWeatherHost = "api.open-meteo.com";
static constexpr uint16_t kWorldWeatherPort = 80;
static constexpr unsigned long kWorldWeatherRefreshMs = 15UL * 60UL * 1000UL;
static constexpr unsigned long kWorldWeatherRetryMs = 60UL * 1000UL;
static constexpr unsigned long kWorldWeatherTimeoutMs = 6500UL;
static constexpr unsigned long kWorldWeatherConnectTimeoutMs = 1200UL;
static constexpr unsigned long kWorldWeatherStartGapMs = 5000UL;
static constexpr size_t kWorldWeatherMaxPayload = 2300;

enum class WorldWeatherFetchState : uint8_t
{
    Idle = 0,
    Reading
};

struct WorldWeatherRequest
{
    WorldWeatherFetchState state = WorldWeatherFetchState::Idle;
    WiFiClient client;
    int tzIndex = -1;
    unsigned long startedAt = 0;
    unsigned long lastRxAt = 0;
    String response;
};

static std::vector<WorldWeather> s_worldWeatherByTz;
static std::vector<unsigned long> s_worldWeatherRetryAfter;
static WorldWeatherRequest s_worldWeatherReq;
static int s_worldWeatherRoundRobin = 0;
static unsigned long s_worldWeatherNextStartMs = 0;

static void ensureWorldWeatherSlots()
{
    const size_t n = timezoneCount();
    if (s_worldWeatherByTz.size() != n)
    {
        s_worldWeatherByTz.assign(n, WorldWeather{"", NAN, 0UL, false});
    }
    if (s_worldWeatherRetryAfter.size() != n)
    {
        s_worldWeatherRetryAfter.assign(n, 0UL);
    }
}

static int currentWorldTzIndex()
{
    if (!worldTimeIsWorldView())
        return -1;
    return s_worldTimeZoneIndices[static_cast<size_t>(s_worldTimeViewIndex)];
}

static String weatherConditionFromCode(int code)
{
    switch (code)
    {
    case 0: return "Clear";
    case 1: return "Mostly Clear";
    case 2: return "Partly Cloudy";
    case 3: return "Overcast";
    case 45:
    case 48: return "Fog";
    case 51:
    case 53:
    case 55: return "Drizzle";
    case 56:
    case 57: return "Freezing Drizzle";
    case 61:
    case 63:
    case 65: return "Rain";
    case 66:
    case 67: return "Freezing Rain";
    case 71:
    case 73:
    case 75:
    case 77: return "Snow";
    case 80:
    case 81:
    case 82: return "Rain Showers";
    case 85:
    case 86: return "Snow Showers";
    case 95: return "Thunderstorm";
    case 96:
    case 99: return "Thunderstorm";
    default: return "Weather";
    }
}

static String worldTimeUtcTagForIndex(int tzIndex)
{
    if (tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount()))
        return "UTC";

    time_t now = time(nullptr);
    DateTime utcNow = (now > 0) ? DateTime(static_cast<uint32_t>(now)) : DateTime(2000, 1, 1, 0, 0, 0);
    int offset = timezoneOffsetForUtcAtIndex(tzIndex, utcNow);
    int absMinutes = abs(offset);
    int hh = absMinutes / 60;
    int mm = absMinutes % 60;
    char sign = (offset >= 0) ? '+' : '-';
    char buf[16];
    snprintf(buf, sizeof(buf), "UTC%c%02d:%02d", sign, hh, mm);
    return String(buf);
}

static const char *countryIso2FromName(const char *country)
{
    if (!country || country[0] == '\0')
        return "--";
    if (strcmp(country, "USA") == 0)
        return "US";
    if (strcmp(country, "Canada") == 0)
        return "CA";
    if (strcmp(country, "Brazil") == 0)
        return "BR";
    if (strcmp(country, "Portugal") == 0)
        return "PT";
    if (strcmp(country, "United Kingdom") == 0)
        return "GB";
    if (strcmp(country, "Germany") == 0)
        return "DE";
    if (strcmp(country, "Greece") == 0)
        return "GR";
    if (strcmp(country, "Russia") == 0)
        return "RU";
    if (strcmp(country, "UAE") == 0)
        return "AE";
    if (strcmp(country, "Pakistan") == 0)
        return "PK";
    if (strcmp(country, "India") == 0)
        return "IN";
    if (strcmp(country, "Bangladesh") == 0)
        return "BD";
    if (strcmp(country, "Thailand") == 0)
        return "TH";
    if (strcmp(country, "Vietnam") == 0)
        return "VN";
    if (strcmp(country, "China") == 0)
        return "CN";
    if (strcmp(country, "Japan") == 0)
        return "JP";
    if (strcmp(country, "Australia") == 0)
        return "AU";
    if (strcmp(country, "New Caledonia") == 0)
        return "NC";
    if (strcmp(country, "New Zealand") == 0)
        return "NZ";
    return "--";
}

static int pickNextTimezoneToRefresh(unsigned long nowMs)
{
    ensureWorldWeatherSlots();
    if (!worldTimeHasSelections())
        return -1;

    const int current = currentWorldTzIndex();
    if (current >= 0 && current < static_cast<int>(timezoneCount()))
    {
        const WorldWeather &cw = s_worldWeatherByTz[static_cast<size_t>(current)];
        bool due = !cw.valid || (nowMs - cw.lastUpdate >= kWorldWeatherRefreshMs);
        if (due && nowMs >= s_worldWeatherRetryAfter[static_cast<size_t>(current)])
            return current;
    }

    const size_t total = worldTimeSelectionCount();
    if (total == 0)
        return -1;

    for (size_t i = 0; i < total; ++i)
    {
        const int pos = (s_worldWeatherRoundRobin + static_cast<int>(i)) % static_cast<int>(total);
        int tzIndex = worldTimeSelectionAt(static_cast<size_t>(pos));
        if (tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount()))
            continue;

        const WorldWeather &w = s_worldWeatherByTz[static_cast<size_t>(tzIndex)];
        if (nowMs < s_worldWeatherRetryAfter[static_cast<size_t>(tzIndex)])
            continue;

        bool due = !w.valid || (nowMs - w.lastUpdate >= kWorldWeatherRefreshMs);
        if (due)
        {
            s_worldWeatherRoundRobin = (pos + 1) % static_cast<int>(total);
            return tzIndex;
        }
    }
    return -1;
}

static bool startWorldWeatherRequest(int tzIndex, unsigned long nowMs)
{
    ensureWorldWeatherSlots();
    if (tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount()))
        return false;
    if (WiFi.status() != WL_CONNECTED)
        return false;

    const TimezoneInfo &tz = timezoneInfoAt(static_cast<size_t>(tzIndex));
    s_worldWeatherReq.client.stop();
    s_worldWeatherReq.client.setTimeout(20);
    if (!s_worldWeatherReq.client.connect(kWorldWeatherHost, kWorldWeatherPort, kWorldWeatherConnectTimeoutMs))
    {
        s_worldWeatherRetryAfter[static_cast<size_t>(tzIndex)] = nowMs + kWorldWeatherRetryMs;
        s_worldWeatherNextStartMs = nowMs + kWorldWeatherStartGapMs;
        return false;
    }

    char path[220];
    snprintf(path, sizeof(path),
             "/v1/forecast?latitude=%.4f&longitude=%.4f&current_weather=true&current=temperature_2m,weather_code",
             static_cast<double>(tz.latitude), static_cast<double>(tz.longitude));

    s_worldWeatherReq.client.printf(
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: WxVision/1.0\r\n"
        "Accept: application/json\r\n"
        "Accept-Encoding: identity\r\n"
        "Connection: close\r\n\r\n",
        path, kWorldWeatherHost);

    s_worldWeatherReq.state = WorldWeatherFetchState::Reading;
    s_worldWeatherReq.tzIndex = tzIndex;
    s_worldWeatherReq.startedAt = nowMs;
    s_worldWeatherReq.lastRxAt = nowMs;
    s_worldWeatherReq.response = "";
    s_worldWeatherReq.response.reserve(kWorldWeatherMaxPayload + 16);
    s_worldWeatherNextStartMs = nowMs + kWorldWeatherStartGapMs;
    return true;
}

static void failWorldWeatherRequest(unsigned long nowMs)
{
    if (s_worldWeatherReq.tzIndex >= 0 &&
        s_worldWeatherReq.tzIndex < static_cast<int>(s_worldWeatherRetryAfter.size()))
    {
        s_worldWeatherRetryAfter[static_cast<size_t>(s_worldWeatherReq.tzIndex)] = nowMs + kWorldWeatherRetryMs;
    }
    s_worldWeatherReq.client.stop();
    s_worldWeatherReq.state = WorldWeatherFetchState::Idle;
    s_worldWeatherReq.tzIndex = -1;
    s_worldWeatherReq.response = "";
    s_worldWeatherNextStartMs = nowMs + kWorldWeatherStartGapMs;
}

static bool parseOpenMeteoPayload(const String &raw, String &outCondition, float &outTemp)
{
    int jsonStart = raw.indexOf('{');
    int jsonEnd = raw.lastIndexOf('}');
    if (jsonStart < 0 || jsonEnd <= jsonStart)
        return false;

    String body = raw.substring(jsonStart, jsonEnd + 1);
    if (body.length() == 0)
        return false;

    DynamicJsonDocument doc(1400);
    DeserializationError err = deserializeJson(doc, body);
    if (err)
        return false;

    outCondition = "";
    outTemp = NAN;
    int code = -1;

    if (doc["current_weather"].is<JsonObject>())
    {
        JsonObject cw = doc["current_weather"].as<JsonObject>();
        outTemp = cw["temperature"] | NAN;
        code = cw["weathercode"] | -1;
    }
    else if (doc["current"].is<JsonObject>())
    {
        JsonObject c = doc["current"].as<JsonObject>();
        outTemp = c["temperature_2m"] | NAN;
        code = c["weather_code"] | -1;
    }

    outCondition = weatherConditionFromCode(code);
    return !isnan(outTemp);
}

static void completeWorldWeatherRequest(unsigned long nowMs)
{
    String condition;
    float temp = NAN;
    if (!parseOpenMeteoPayload(s_worldWeatherReq.response, condition, temp))
    {
        failWorldWeatherRequest(nowMs);
        return;
    }

    if (s_worldWeatherReq.tzIndex >= 0 &&
        s_worldWeatherReq.tzIndex < static_cast<int>(s_worldWeatherByTz.size()))
    {
        WorldWeather &slot = s_worldWeatherByTz[static_cast<size_t>(s_worldWeatherReq.tzIndex)];
        slot.condition = condition;
        slot.temperature = temp;
        slot.lastUpdate = nowMs;
        slot.valid = true;
    }

    s_worldWeatherReq.client.stop();
    s_worldWeatherReq.state = WorldWeatherFetchState::Idle;
    s_worldWeatherReq.tzIndex = -1;
    s_worldWeatherReq.response = "";
    s_worldWeatherNextStartMs = nowMs + kWorldWeatherStartGapMs;
}

static void sortSelectionsByTimezoneIndex()
{
    std::sort(s_worldTimeZoneIndices.begin(), s_worldTimeZoneIndices.end());
}

void loadWorldTimeSettings()
{
    s_worldTimeZoneIndices.clear();

    Preferences prefs;
    if (!prefs.begin(kPrefsNs, true))
        return;

    String raw = prefs.getString(kPrefsKeyIds, "");
    prefs.end();

    raw.trim();
    if (raw.isEmpty())
    {
        s_worldTimeViewIndex = -1;
        return;
    }

    int start = 0;
    while (start < raw.length())
    {
        int sep = raw.indexOf('|', start);
        if (sep < 0)
            sep = raw.length();
        String token = raw.substring(start, sep);
        token.trim();
        if (!token.isEmpty())
        {
            int tzIndex = timezoneIndexFromId(token.c_str());
            if (tzIndex >= 0)
            {
                bool exists = false;
                for (size_t i = 0; i < s_worldTimeZoneIndices.size(); ++i)
                {
                    if (s_worldTimeZoneIndices[i] == tzIndex)
                    {
                        exists = true;
                        break;
                    }
                }
                if (!exists)
                {
                    s_worldTimeZoneIndices.push_back(tzIndex);
                }
            }
        }
        start = sep + 1;
    }

    s_worldTimeViewIndex = -1;
    sortSelectionsByTimezoneIndex();
}

void saveWorldTimeSettings()
{
    String serialized;
    for (size_t i = 0; i < s_worldTimeZoneIndices.size(); ++i)
    {
        int tzIndex = s_worldTimeZoneIndices[i];
        if (tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount()))
            continue;
        if (serialized.length() > 0)
            serialized += "|";
        serialized += timezoneInfoAt(static_cast<size_t>(tzIndex)).id;
    }

    Preferences prefs;
    if (!prefs.begin(kPrefsNs, false))
        return;
    prefs.putString(kPrefsKeyIds, serialized);
    prefs.end();
}

size_t worldTimeSelectionCount()
{
    return s_worldTimeZoneIndices.size();
}

bool worldTimeHasSelections()
{
    return !s_worldTimeZoneIndices.empty();
}

int worldTimeSelectionAt(size_t index)
{
    if (index >= s_worldTimeZoneIndices.size())
        return -1;
    return s_worldTimeZoneIndices[index];
}

bool worldTimeIsSelected(int tzIndex)
{
    if (tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount()))
        return false;
    for (size_t i = 0; i < s_worldTimeZoneIndices.size(); ++i)
    {
        if (s_worldTimeZoneIndices[i] == tzIndex)
            return true;
    }
    return false;
}

bool worldTimeToggleTimezone(int tzIndex)
{
    if (tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount()))
        return false;

    for (size_t i = 0; i < s_worldTimeZoneIndices.size(); ++i)
    {
        if (s_worldTimeZoneIndices[i] == tzIndex)
        {
            s_worldTimeZoneIndices.erase(s_worldTimeZoneIndices.begin() + static_cast<long long>(i));
            if (s_worldTimeZoneIndices.empty())
            {
                s_worldTimeViewIndex = -1;
            }
            else if (s_worldTimeViewIndex >= static_cast<int>(s_worldTimeZoneIndices.size()))
            {
                s_worldTimeViewIndex = static_cast<int>(s_worldTimeZoneIndices.size()) - 1;
            }
            return true;
        }
    }

    s_worldTimeZoneIndices.push_back(tzIndex);
    sortSelectionsByTimezoneIndex();
    return true;
}

bool worldTimeAddTimezone(int tzIndex)
{
    if (tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount()))
        return false;

    for (size_t i = 0; i < s_worldTimeZoneIndices.size(); ++i)
    {
        if (s_worldTimeZoneIndices[i] == tzIndex)
            return false;
    }
    s_worldTimeZoneIndices.push_back(tzIndex);
    sortSelectionsByTimezoneIndex();
    return true;
}

bool worldTimeRemoveTimezoneAt(size_t index)
{
    if (index >= s_worldTimeZoneIndices.size())
        return false;

    s_worldTimeZoneIndices.erase(s_worldTimeZoneIndices.begin() + static_cast<long long>(index));
    if (s_worldTimeZoneIndices.empty())
    {
        s_worldTimeViewIndex = -1;
        return true;
    }

    if (s_worldTimeViewIndex >= static_cast<int>(s_worldTimeZoneIndices.size()))
    {
        s_worldTimeViewIndex = static_cast<int>(s_worldTimeZoneIndices.size()) - 1;
    }
    return true;
}

void worldTimeClearSelections()
{
    s_worldTimeZoneIndices.clear();
    s_worldTimeViewIndex = -1;
}

void worldTimeResetView()
{
    s_worldTimeViewIndex = -1;
}

bool worldTimeCycleView(int delta)
{
    if (s_worldTimeZoneIndices.empty() || delta == 0)
        return false;

    const int total = static_cast<int>(s_worldTimeZoneIndices.size());

    // From system view, first UP/DOWN press enters the world-time list.
    if (s_worldTimeViewIndex < 0 || s_worldTimeViewIndex >= total)
    {
        s_worldTimeViewIndex = (delta > 0) ? 0 : (total - 1);
        return true;
    }

    if (delta > 0)
    {
        // Moving forward from the last world zone returns to local/system view.
        if (s_worldTimeViewIndex == total - 1)
            s_worldTimeViewIndex = -1;
        else
            s_worldTimeViewIndex++;
    }
    else
    {
        // Moving backward from the first world zone returns to local/system view.
        if (s_worldTimeViewIndex == 0)
            s_worldTimeViewIndex = -1;
        else
            s_worldTimeViewIndex--;
    }
    return true;
}

bool worldTimeIsWorldView()
{
    return s_worldTimeViewIndex >= 0 &&
           s_worldTimeViewIndex < static_cast<int>(s_worldTimeZoneIndices.size());
}

String worldTimeCurrentZoneLabel()
{
    if (!worldTimeIsWorldView())
        return "";

    const int tzIndex = s_worldTimeZoneIndices[static_cast<size_t>(s_worldTimeViewIndex)];
    if (tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount()))
        return "";

    const TimezoneInfo &tz = timezoneInfoAt(static_cast<size_t>(tzIndex));
    return String(tz.city);
}

String worldTimeCurrentZoneId()
{
    if (!worldTimeIsWorldView())
        return "";

    const int tzIndex = s_worldTimeZoneIndices[static_cast<size_t>(s_worldTimeViewIndex)];
    if (tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount()))
        return "";
    return String(timezoneInfoAt(static_cast<size_t>(tzIndex)).id);
}

bool worldTimeGetCurrentDateTime(DateTime &outLocal)
{
    if (!worldTimeIsWorldView())
        return false;

    const int tzIndex = s_worldTimeZoneIndices[static_cast<size_t>(s_worldTimeViewIndex)];
    if (tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount()))
        return false;

    // Use WxVision's current local timezone state as the reference baseline.
    DateTime baseLocal;
    if (!getLocalDateTime(baseLocal))
        return false;

    int baseOffset = timezoneOffsetForLocal(baseLocal);
    DateTime referenceUtc = localToUtc(baseLocal, baseOffset);

    int worldOffset = timezoneOffsetForUtcAtIndex(tzIndex, referenceUtc);
    outLocal = utcToLocal(referenceUtc, worldOffset);
    return true;
}

String worldTimeSelectionCityLabel(size_t selectionIndex)
{
    int tzIndex = worldTimeSelectionAt(selectionIndex);
    if (tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount()))
        return "";
    return String(timezoneInfoAt(static_cast<size_t>(tzIndex)).city);
}

bool worldTimeGetSelectionDateTime(size_t selectionIndex, DateTime &outLocal)
{
    int tzIndex = worldTimeSelectionAt(selectionIndex);
    if (tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount()))
        return false;

    DateTime baseLocal;
    if (!getLocalDateTime(baseLocal))
        return false;

    int baseOffset = timezoneOffsetForLocal(baseLocal);
    DateTime referenceUtc = localToUtc(baseLocal, baseOffset);
    int worldOffset = timezoneOffsetForUtcAtIndex(tzIndex, referenceUtc);
    outLocal = utcToLocal(referenceUtc, worldOffset);
    return true;
}

void worldTimeWeatherTick(bool allowStart)
{
    ensureWorldWeatherSlots();

    const unsigned long nowMs = millis();
    if (WiFi.status() != WL_CONNECTED)
    {
        if (s_worldWeatherReq.state != WorldWeatherFetchState::Idle)
        {
            failWorldWeatherRequest(nowMs);
        }
        return;
    }

    if (s_worldWeatherReq.state == WorldWeatherFetchState::Idle)
    {
        if (!allowStart || nowMs < s_worldWeatherNextStartMs)
            return;

        int tzIndex = pickNextTimezoneToRefresh(nowMs);
        if (tzIndex >= 0)
        {
            startWorldWeatherRequest(tzIndex, nowMs);
        }
        return;
    }

    if (s_worldWeatherReq.state == WorldWeatherFetchState::Reading)
    {
        if (!s_worldWeatherReq.client.connected() && !s_worldWeatherReq.client.available())
        {
            completeWorldWeatherRequest(nowMs);
            return;
        }

        size_t budget = 320;
        while (budget > 0 && s_worldWeatherReq.client.available())
        {
            uint8_t buf[64];
            const int want = static_cast<int>(budget > sizeof(buf) ? sizeof(buf) : budget);
            int n = s_worldWeatherReq.client.read(buf, want);
            if (n <= 0)
                break;

            s_worldWeatherReq.lastRxAt = nowMs;
            for (int i = 0; i < n; ++i)
            {
                s_worldWeatherReq.response += static_cast<char>(buf[i]);
            }

            if (s_worldWeatherReq.response.length() > kWorldWeatherMaxPayload)
            {
                failWorldWeatherRequest(nowMs);
                return;
            }

            budget -= static_cast<size_t>(n);
        }

        if ((nowMs - s_worldWeatherReq.lastRxAt) > kWorldWeatherTimeoutMs ||
            (nowMs - s_worldWeatherReq.startedAt) > (kWorldWeatherTimeoutMs * 2UL))
        {
            failWorldWeatherRequest(nowMs);
            return;
        }
    }
}

bool worldTimeCurrentWeather(WorldWeather &out)
{
    ensureWorldWeatherSlots();
    int tzIndex = currentWorldTzIndex();
    if (tzIndex < 0 || tzIndex >= static_cast<int>(s_worldWeatherByTz.size()))
        return false;

    const WorldWeather &slot = s_worldWeatherByTz[static_cast<size_t>(tzIndex)];
    if (!slot.valid)
        return false;
    out = slot;
    return true;
}

bool worldTimeGetSelectionWeather(size_t selectionIndex, WorldWeather &out)
{
    ensureWorldWeatherSlots();
    int tzIndex = worldTimeSelectionAt(selectionIndex);
    if (tzIndex < 0 || tzIndex >= static_cast<int>(s_worldWeatherByTz.size()))
        return false;

    const WorldWeather &slot = s_worldWeatherByTz[static_cast<size_t>(tzIndex)];
    if (!slot.valid)
        return false;
    out = slot;
    return true;
}

String worldTimeBuildCurrentHeaderText()
{
    int tzIndex = currentWorldTzIndex();
    if (tzIndex < 0 || tzIndex >= static_cast<int>(timezoneCount()))
        return "";

    const TimezoneInfo &tz = timezoneInfoAt(static_cast<size_t>(tzIndex));
    String city = String(tz.city);
    if (city.length() == 0)
        city = worldTimeCurrentZoneLabel();
    city += " ";
    city += countryIso2FromName(tz.country);

    WorldWeather weather;
    if (worldTimeCurrentWeather(weather))
    {
        String tempTag = isnan(weather.temperature) ? String("--") : fmtTemp(weather.temperature, 0);

        String text = city;
        text += " - ";
        text += weather.condition;
        text += " ";
        text += tempTag;
        return text;
    }

    String text = city;
    text += " (";
    text += worldTimeUtcTagForIndex(tzIndex);
    text += ")";
    return text;
}
