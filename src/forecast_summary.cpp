#include "forecast_summary.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "alarm.h"
#include "noaa.h"
#include "settings.h"
#include "units.h"
#include "weather_provider.h"

namespace
{
using wxv::provider::WeatherSnapshot;

static constexpr int kNearRainProbabilityPct = 40;
static constexpr float kNearRainAmountMm = 0.25f;
static constexpr int kTodayRainProbabilityPct = 50;
static constexpr float kTodayRainAmountMm = 1.0f;
static constexpr float kWindySpeedMps = 6.7f;      // 15 mph
static constexpr float kWindyGustMps = 8.9f;       // 20 mph
static constexpr float kCoolTonightC = 10.0f;      // 50 F
static constexpr float kColdTonightC = 4.4f;       // 40 F
static constexpr float kHotTodayC = 29.0f;         // 84 F
static constexpr float kHotTomorrowC = 30.0f;      // 86 F
static constexpr unsigned long kHighCooldownMs = 15UL * 60UL * 1000UL;
static constexpr unsigned long kMediumCooldownMs = 22UL * 60UL * 1000UL;
static constexpr unsigned long kLowCooldownMs = 30UL * 60UL * 1000UL;
static constexpr unsigned long kEvalIntervalMs = 60UL * 1000UL;
static constexpr int kMorningSummaryStartHour = 5;
static constexpr int kMorningSummaryEndHour = 11;
static constexpr int kEveningSummaryStartHour = 17;

struct NormalizedForecastHour
{
    uint32_t forecastTime = 0;
    float tempC = NAN;
    String conditionText;
    int precipProbability = -1;
    float precipAmountMm = NAN;
    float windSpeedMps = NAN;
    float windGustMps = NAN;
};

struct NormalizedForecastDay
{
    bool available = false;
    uint32_t dayEpoch = 0;
    float minTempC = NAN;
    float maxTempC = NAN;
    String conditionText;
    int precipProbability = -1;
    float precipAmountMm = NAN;
    float windSpeedMps = NAN;
    float windGustMps = NAN;
    uint32_t sunrise = 0;
    uint32_t sunset = 0;
};

struct NormalizedForecast
{
    const char *sourceName = "unknown";
    uint32_t currentTime = 0;
    NormalizedForecastHour nextHourlyEntries[6];
    int nextHourlyCount = 0;
    NormalizedForecastDay todayDaily;
    NormalizedForecastDay tonightDaily;
    NormalizedForecastDay tomorrowDaily;
    uint32_t sunrise = 0;
    uint32_t sunset = 0;
};

ForecastSummaryMessage s_currentMessage;
WeatherSnapshot s_snapshotCache;
NormalizedForecast s_normalizedCache;
uint32_t s_lastDataSignature = 0;
unsigned long s_lastEvalMs = 0;
bool s_screenActive = false;
bool s_autoPresentationPending = false;

uint32_t fnv1aAppend(uint32_t hash, const void *data, size_t size)
{
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

uint32_t fnv1aAppendString(uint32_t hash, const String &value)
{
    for (int i = 0; i < value.length(); ++i)
    {
        hash ^= static_cast<uint8_t>(value.charAt(i));
        hash *= 16777619u;
    }
    return hash;
}

uint32_t dataSignature(const WeatherSnapshot &snapshot)
{
    uint32_t hash = 2166136261u;
    hash = fnv1aAppend(hash, &snapshot.provider, sizeof(snapshot.provider));
    hash = fnv1aAppend(hash, &snapshot.updatedMs, sizeof(snapshot.updatedMs));
    hash = fnv1aAppend(hash, &snapshot.dailyCount, sizeof(snapshot.dailyCount));
    hash = fnv1aAppend(hash, &snapshot.hourlyCount, sizeof(snapshot.hourlyCount));
    const int dailyCount = min(snapshot.dailyCount, 3);
    const int hourlyCount = min(snapshot.hourlyCount, 6);
    for (int i = 0; i < dailyCount; ++i)
    {
        hash = fnv1aAppend(hash, &snapshot.daily[i].highTemp, sizeof(snapshot.daily[i].highTemp));
        hash = fnv1aAppend(hash, &snapshot.daily[i].lowTemp, sizeof(snapshot.daily[i].lowTemp));
        hash = fnv1aAppend(hash, &snapshot.daily[i].rainChance, sizeof(snapshot.daily[i].rainChance));
        hash = fnv1aAppend(hash, &snapshot.daily[i].precipAmount, sizeof(snapshot.daily[i].precipAmount));
        hash = fnv1aAppend(hash, &snapshot.daily[i].windSpeed, sizeof(snapshot.daily[i].windSpeed));
        hash = fnv1aAppend(hash, &snapshot.daily[i].windGust, sizeof(snapshot.daily[i].windGust));
        hash = fnv1aAppendString(hash, snapshot.daily[i].conditions);
    }
    for (int i = 0; i < hourlyCount; ++i)
    {
        hash = fnv1aAppend(hash, &snapshot.hourly[i].time, sizeof(snapshot.hourly[i].time));
        hash = fnv1aAppend(hash, &snapshot.hourly[i].temp, sizeof(snapshot.hourly[i].temp));
        hash = fnv1aAppend(hash, &snapshot.hourly[i].rainChance, sizeof(snapshot.hourly[i].rainChance));
        hash = fnv1aAppend(hash, &snapshot.hourly[i].precipAmount, sizeof(snapshot.hourly[i].precipAmount));
        hash = fnv1aAppend(hash, &snapshot.hourly[i].windSpeed, sizeof(snapshot.hourly[i].windSpeed));
        hash = fnv1aAppend(hash, &snapshot.hourly[i].windGust, sizeof(snapshot.hourly[i].windGust));
        hash = fnv1aAppendString(hash, snapshot.hourly[i].conditions);
    }
    return hash;
}

String lowerCopy(const String &input)
{
    String value = input;
    value.toLowerCase();
    return value;
}

bool containsAny(const String &value, const char *const *terms, size_t count)
{
    String lower = lowerCopy(value);
    for (size_t i = 0; i < count; ++i)
    {
        if (lower.indexOf(terms[i]) >= 0)
            return true;
    }
    return false;
}

bool isRainLike(const String &condition)
{
    static const char *const kTerms[] = {"rain", "shower", "drizzle", "storm", "thunder", "sleet"};
    return containsAny(condition, kTerms, sizeof(kTerms) / sizeof(kTerms[0]));
}

bool isClearLike(const String &condition)
{
    static const char *const kTerms[] = {"clear", "sunny", "fair", "mostly clear"};
    return containsAny(condition, kTerms, sizeof(kTerms) / sizeof(kTerms[0]));
}

bool isPleasantLike(const String &condition)
{
    static const char *const kTerms[] = {"clear", "sunny", "fair", "partly cloudy", "mostly clear"};
    return containsAny(condition, kTerms, sizeof(kTerms) / sizeof(kTerms[0]));
}

bool sameLocalDay(uint32_t lhs, uint32_t rhs)
{
    if (lhs == 0 || rhs == 0)
        return false;
    time_t a = static_cast<time_t>(lhs);
    time_t b = static_cast<time_t>(rhs);
    struct tm aTm = {};
    struct tm bTm = {};
    localtime_r(&a, &aTm);
    localtime_r(&b, &bTm);
    return aTm.tm_year == bTm.tm_year && aTm.tm_yday == bTm.tm_yday;
}

int localHourForEpoch(uint32_t epoch)
{
    if (epoch == 0)
        return -1;
    time_t raw = static_cast<time_t>(epoch);
    struct tm localTm = {};
    localtime_r(&raw, &localTm);
    return localTm.tm_hour;
}

bool isMorningSummaryWindow(uint32_t nowEpoch)
{
    const int hour = localHourForEpoch(nowEpoch);
    return hour >= kMorningSummaryStartHour && hour <= kMorningSummaryEndHour;
}

bool isEveningSummaryWindow(uint32_t nowEpoch)
{
    const int hour = localHourForEpoch(nowEpoch);
    return hour >= kEveningSummaryStartHour;
}

uint32_t dayEpochFor(const ForecastDay &day)
{
    if (day.yearNum > 0 && day.monthNum > 0 && day.dayNum > 0)
    {
        struct tm tmValue = {};
        tmValue.tm_year = day.yearNum - 1900;
        tmValue.tm_mon = day.monthNum - 1;
        tmValue.tm_mday = day.dayNum;
        tmValue.tm_hour = 12;
        return static_cast<uint32_t>(mktime(&tmValue));
    }
    if (day.sunrise > 0)
        return day.sunrise;
    if (day.sunset > 0)
        return day.sunset;
    return 0;
}

NormalizedForecastDay normalizeDay(const ForecastDay &day)
{
    NormalizedForecastDay out;
    out.available = true;
    out.dayEpoch = dayEpochFor(day);
    out.minTempC = static_cast<float>(day.lowTemp);
    out.maxTempC = static_cast<float>(day.highTemp);
    out.conditionText = day.conditions;
    out.precipProbability = day.rainChance;
    out.precipAmountMm = static_cast<float>(day.precipAmount);
    out.windSpeedMps = static_cast<float>(day.windSpeed);
    out.windGustMps = static_cast<float>(day.windGust);
    out.sunrise = day.sunrise;
    out.sunset = day.sunset;
    return out;
}

NormalizedForecastHour normalizeHour(const ForecastHour &hour)
{
    NormalizedForecastHour out;
    out.forecastTime = hour.time;
    out.tempC = static_cast<float>(hour.temp);
    out.conditionText = hour.conditions;
    out.precipProbability = hour.rainChance;
    out.precipAmountMm = static_cast<float>(hour.precipAmount);
    out.windSpeedMps = static_cast<float>(hour.windSpeed);
    out.windGustMps = static_cast<float>(hour.windGust);
    return out;
}

bool buildNormalizedForecast(NormalizedForecast &out, WeatherSnapshot &snapshot)
{
    if (!wxv::provider::readActiveProviderSnapshot(snapshot))
        return false;

    out = NormalizedForecast{};
    out.sourceName = wxv::provider::providerNameFromDataSource(dataSource);

    time_t nowTs = time(nullptr);
    if (nowTs <= 0)
        nowTs = static_cast<time_t>(snapshot.hourlyCount > 0 ? snapshot.hourly[0].time : 0);
    out.currentTime = static_cast<uint32_t>(nowTs);

    const uint32_t nowEpoch = out.currentTime;
    for (int i = 0; i < snapshot.hourlyCount && out.nextHourlyCount < 6; ++i)
    {
        if (snapshot.hourly[i].time + 900U < nowEpoch)
            continue;
        out.nextHourlyEntries[out.nextHourlyCount++] = normalizeHour(snapshot.hourly[i]);
    }

    for (int i = 0; i < snapshot.dailyCount; ++i)
    {
        const NormalizedForecastDay day = normalizeDay(snapshot.daily[i]);
        if (!out.todayDaily.available && sameLocalDay(day.dayEpoch, nowEpoch))
        {
            out.todayDaily = day;
            continue;
        }
        if (!out.tomorrowDaily.available && day.dayEpoch > nowEpoch && !sameLocalDay(day.dayEpoch, nowEpoch))
        {
            out.tomorrowDaily = day;
            break;
        }
    }

    if (!out.todayDaily.available && snapshot.dailyCount > 0)
        out.todayDaily = normalizeDay(snapshot.daily[0]);
    if (!out.tomorrowDaily.available && snapshot.dailyCount > 1)
        out.tomorrowDaily = normalizeDay(snapshot.daily[1]);

    out.tonightDaily = out.todayDaily;
    out.sunrise = out.todayDaily.sunrise;
    out.sunset = out.todayDaily.sunset;
    return true;
}

void setMessage(ForecastSummaryMessage &msg,
                const char *line1,
                const char *line2,
                ForecastSummaryImportance importance,
                uint16_t durationMs,
                bool useTypewriter,
                uint32_t validUntilEpoch,
                const char *reason)
{
    msg.available = true;
    strncpy(msg.title, "FORECAST", sizeof(msg.title) - 1);
    msg.title[sizeof(msg.title) - 1] = '\0';
    strncpy(msg.line1, line1 ? line1 : "", sizeof(msg.line1) - 1);
    msg.line1[sizeof(msg.line1) - 1] = '\0';
    strncpy(msg.line2, line2 ? line2 : "", sizeof(msg.line2) - 1);
    msg.line2[sizeof(msg.line2) - 1] = '\0';
    msg.importance = importance;
    msg.displayDurationMs = durationMs;
    msg.useTypewriter = useTypewriter;
    msg.validUntilEpoch = validUntilEpoch;
    strncpy(msg.debugReason, reason ? reason : "", sizeof(msg.debugReason) - 1);
    msg.debugReason[sizeof(msg.debugReason) - 1] = '\0';

    uint32_t signature = 2166136261u;
    signature = fnv1aAppendString(signature, String(msg.line1));
    signature = fnv1aAppendString(signature, String(msg.line2));
    signature = fnv1aAppend(signature, &msg.importance, sizeof(msg.importance));
    msg.signature = signature;
}

bool selectRainMessage(const NormalizedForecast &forecast, ForecastSummaryMessage &msg)
{
    const uint32_t nowEpoch = forecast.currentTime;
    for (int i = 0; i < forecast.nextHourlyCount; ++i)
    {
        const NormalizedForecastHour &hour = forecast.nextHourlyEntries[i];
        if (hour.forecastTime <= nowEpoch)
            continue;
        const uint32_t deltaSec = hour.forecastTime - nowEpoch;
        if (deltaSec > 3U * 3600U)
            break;
        if ((hour.precipProbability >= kNearRainProbabilityPct) ||
            (!isnan(hour.precipAmountMm) && hour.precipAmountMm >= kNearRainAmountMm) ||
            isRainLike(hour.conditionText))
        {
            const int hoursAway = max(1, static_cast<int>((deltaSec + 1800U) / 3600U));
            if (deltaSec <= 75U * 60U)
            {
                setMessage(msg, "RAIN SOON", "", ForecastSummaryImportance::High, 5000, false, hour.forecastTime + 3600U, "hourly precip >= threshold in next hour");
            }
            else
            {
                char line2[24];
                snprintf(line2, sizeof(line2), "%d HOUR%s", hoursAway, (hoursAway == 1) ? "" : "S");
                setMessage(msg, "RAIN IN", line2, ForecastSummaryImportance::High, 5600, false, hour.forecastTime + 3600U, "hourly precip >= threshold in next 3 hours");
            }
            return true;
        }
    }

    if (forecast.todayDaily.available &&
        isMorningSummaryWindow(nowEpoch) &&
        ((forecast.todayDaily.precipProbability >= kTodayRainProbabilityPct) ||
         (!isnan(forecast.todayDaily.precipAmountMm) && forecast.todayDaily.precipAmountMm >= kTodayRainAmountMm) ||
         isRainLike(forecast.todayDaily.conditionText)))
    {
        if (forecast.nextHourlyCount > 0)
        {
            const uint32_t firstHour = forecast.nextHourlyEntries[0].forecastTime;
            if (firstHour > nowEpoch + 3U * 3600U)
            {
                setMessage(msg, "SHOWERS", "LATER", ForecastSummaryImportance::Medium, 5000, false, forecast.todayDaily.sunset, "daily precip signal later today");
                return true;
            }
        }
        setMessage(msg, "RAIN TODAY", "", ForecastSummaryImportance::Medium, 4500, false, forecast.todayDaily.sunset, "daily precip threshold exceeded");
        return true;
    }

    if (forecast.tomorrowDaily.available &&
        isEveningSummaryWindow(nowEpoch) &&
        ((forecast.tomorrowDaily.precipProbability >= kTodayRainProbabilityPct) ||
         (!isnan(forecast.tomorrowDaily.precipAmountMm) && forecast.tomorrowDaily.precipAmountMm >= kTodayRainAmountMm) ||
         isRainLike(forecast.tomorrowDaily.conditionText)))
    {
        setMessage(msg, "RAIN TOMORROW", "", ForecastSummaryImportance::Medium, 4500, false, forecast.tomorrowDaily.sunset, "tomorrow precip threshold exceeded during evening window");
        return true;
    }

    return false;
}

bool selectWindMessage(const NormalizedForecast &forecast, ForecastSummaryMessage &msg)
{
    const uint32_t nowEpoch = forecast.currentTime;
    for (int i = 0; i < forecast.nextHourlyCount; ++i)
    {
        const NormalizedForecastHour &hour = forecast.nextHourlyEntries[i];
        const bool windy = (!isnan(hour.windSpeedMps) && hour.windSpeedMps >= kWindySpeedMps) ||
                           (!isnan(hour.windGustMps) && hour.windGustMps >= kWindyGustMps);
        if (!windy)
            continue;

        time_t tt = static_cast<time_t>(hour.forecastTime);
        struct tm localTm = {};
        localtime_r(&tt, &localTm);
        if (sameLocalDay(hour.forecastTime, nowEpoch) && localTm.tm_hour >= 12 && localTm.tm_hour <= 18)
        {
            setMessage(msg, "WINDY PM", "", ForecastSummaryImportance::Medium, 4500, false, hour.forecastTime + 3U * 3600U, "hourly wind threshold this afternoon");
        }
        else if (hour.forecastTime <= nowEpoch + 6U * 3600U)
        {
            setMessage(msg, "GUSTY LATER", "", ForecastSummaryImportance::Medium, 4500, false, hour.forecastTime + 3U * 3600U, "hourly gust threshold in next 6 hours");
        }
        else
        {
            setMessage(msg, "WINDY TODAY", "", ForecastSummaryImportance::Medium, 4500, false, forecast.todayDaily.sunset, "daily wind threshold exceeded");
        }
        return true;
    }

    if (forecast.todayDaily.available)
    {
        const bool windy = (!isnan(forecast.todayDaily.windSpeedMps) && forecast.todayDaily.windSpeedMps >= kWindySpeedMps) ||
                           (!isnan(forecast.todayDaily.windGustMps) && forecast.todayDaily.windGustMps >= kWindyGustMps);
        if (windy && isMorningSummaryWindow(nowEpoch))
        {
            setMessage(msg, "WINDY TODAY", "", ForecastSummaryImportance::Medium, 4500, false, forecast.todayDaily.sunset, "daily wind threshold exceeded");
            return true;
        }
    }

    if (forecast.tomorrowDaily.available && isEveningSummaryWindow(nowEpoch))
    {
        const bool windy = (!isnan(forecast.tomorrowDaily.windSpeedMps) && forecast.tomorrowDaily.windSpeedMps >= kWindySpeedMps) ||
                           (!isnan(forecast.tomorrowDaily.windGustMps) && forecast.tomorrowDaily.windGustMps >= kWindyGustMps);
        if (windy)
        {
            setMessage(msg, "WINDY TOMORROW", "", ForecastSummaryImportance::Medium, 4500, false, forecast.tomorrowDaily.sunset, "tomorrow wind threshold exceeded during evening window");
            return true;
        }
    }

    return false;
}

bool selectTemperatureMessage(const NormalizedForecast &forecast, ForecastSummaryMessage &msg)
{
    if (forecast.todayDaily.available &&
        isMorningSummaryWindow(forecast.currentTime) &&
        !isnan(forecast.todayDaily.maxTempC) &&
        forecast.todayDaily.maxTempC >= kHotTodayC)
    {
        setMessage(msg, "HOT TODAY", "", ForecastSummaryImportance::Medium, 4500, false, forecast.todayDaily.sunset, "today high temp >= hot threshold during morning window");
        return true;
    }

    if (forecast.tonightDaily.available && !isnan(forecast.tonightDaily.minTempC))
    {
        if (forecast.tonightDaily.minTempC <= kColdTonightC)
        {
            setMessage(msg, "COLD TONIGHT", "", ForecastSummaryImportance::Medium, 4500, false, forecast.tonightDaily.sunrise, "tonight low temp <= cold threshold");
            return true;
        }
        if (forecast.tonightDaily.minTempC <= kCoolTonightC)
        {
            setMessage(msg, "COOL TONIGHT", "", ForecastSummaryImportance::Medium, 4500, false, forecast.tonightDaily.sunrise, "tonight low temp <= cool threshold");
            return true;
        }
    }

    if (forecast.tomorrowDaily.available &&
        isEveningSummaryWindow(forecast.currentTime) &&
        !isnan(forecast.tomorrowDaily.maxTempC) &&
        forecast.tomorrowDaily.maxTempC >= kHotTomorrowC)
    {
        setMessage(msg, "HOT TOMORROW", "", ForecastSummaryImportance::Medium, 4500, false, forecast.tomorrowDaily.sunset, "tomorrow high temp >= hot threshold during evening window");
        return true;
    }

    return false;
}

bool selectCalmMessage(const NormalizedForecast &forecast, ForecastSummaryMessage &msg)
{
    if (forecast.todayDaily.available)
    {
        const bool dry = forecast.todayDaily.precipProbability >= 0 && forecast.todayDaily.precipProbability < 20;
        const bool calm = (isnan(forecast.todayDaily.windSpeedMps) || forecast.todayDaily.windSpeedMps < 5.0f) &&
                          (isnan(forecast.todayDaily.windGustMps) || forecast.todayDaily.windGustMps < 7.0f);
        if (isMorningSummaryWindow(forecast.currentTime) && dry && calm && isClearLike(forecast.todayDaily.conditionText))
        {
            setMessage(msg, "CLEAR TODAY", "", ForecastSummaryImportance::Low, 4000, false, forecast.todayDaily.sunset, "daily clear and stable conditions");
            return true;
        }
        if (isMorningSummaryWindow(forecast.currentTime) && dry && calm && isPleasantLike(forecast.todayDaily.conditionText))
        {
            setMessage(msg, "NICE TODAY", "", ForecastSummaryImportance::Low, 4000, false, forecast.todayDaily.sunset, "daily pleasant and stable conditions");
            return true;
        }
    }

    if (forecast.tonightDaily.available)
    {
        const bool dry = forecast.tonightDaily.precipProbability >= 0 && forecast.tonightDaily.precipProbability < 20;
        if (dry && isClearLike(forecast.tonightDaily.conditionText))
        {
            setMessage(msg, "CLEAR TONIGHT", "", ForecastSummaryImportance::Low, 4000, false, forecast.tonightDaily.sunrise, "tonight clear and dry conditions");
            return true;
        }
    }

    if (forecast.tomorrowDaily.available && isEveningSummaryWindow(forecast.currentTime))
    {
        const bool dry = forecast.tomorrowDaily.precipProbability >= 0 && forecast.tomorrowDaily.precipProbability < 20;
        const bool calm = (isnan(forecast.tomorrowDaily.windSpeedMps) || forecast.tomorrowDaily.windSpeedMps < 5.0f) &&
                          (isnan(forecast.tomorrowDaily.windGustMps) || forecast.tomorrowDaily.windGustMps < 7.0f);
        if (dry && calm && isClearLike(forecast.tomorrowDaily.conditionText))
        {
            setMessage(msg, "CLEAR TOMORROW", "", ForecastSummaryImportance::Low, 4000, false, forecast.tomorrowDaily.sunset, "tomorrow clear and stable conditions during evening window");
            return true;
        }
        if (dry && calm && isPleasantLike(forecast.tomorrowDaily.conditionText))
        {
            setMessage(msg, "NICE TOMORROW", "", ForecastSummaryImportance::Low, 4000, false, forecast.tomorrowDaily.sunset, "tomorrow pleasant and stable conditions during evening window");
            return true;
        }
    }

    return false;
}

ForecastSummaryMessage buildForecastSummaryMessage(const NormalizedForecast &forecast)
{
    ForecastSummaryMessage msg;
    if (selectRainMessage(forecast, msg))
        return msg;
    if (selectWindMessage(forecast, msg))
        return msg;
    if (selectTemperatureMessage(forecast, msg))
        return msg;
    if (selectCalmMessage(forecast, msg))
        return msg;
    return msg;
}

unsigned long cooldownFor(ForecastSummaryImportance importance)
{
    switch (importance)
    {
    case ForecastSummaryImportance::High:
        return kHighCooldownMs;
    case ForecastSummaryImportance::Medium:
        return kMediumCooldownMs;
    case ForecastSummaryImportance::Low:
    default:
        return kLowCooldownMs;
    }
}

void logSummarySelection(const ForecastSummaryMessage &msg, const char *sourceName)
{
    if (msg.available)
    {
        Serial.printf("[ForecastSummary] source=%s selected=\"%s%s%s\" reason=%s\n",
                      sourceName ? sourceName : "unknown",
                      msg.line1,
                      msg.line2[0] ? " / " : "",
                      msg.line2[0] ? msg.line2 : "",
                      msg.debugReason);
    }
    else
    {
        Serial.println("[ForecastSummary] no summary selected");
    }
}

void evaluateForecastSummary(bool forceLog)
{
    ForecastSummaryMessage next;
    const bool haveData = buildNormalizedForecast(s_normalizedCache, s_snapshotCache);
    if (haveData)
        next = buildForecastSummaryMessage(s_normalizedCache);

    const bool changed = (next.available != s_currentMessage.available) ||
                         (next.signature != s_currentMessage.signature);
    if (changed || forceLog)
        logSummarySelection(next, s_normalizedCache.sourceName);

    if (changed)
    {
        s_currentMessage = next;
        s_autoPresentationPending = next.available;
    }
}
} // namespace

void forecastSummaryTick()
{
    const unsigned long now = millis();
    if ((now - s_lastEvalMs) < kEvalIntervalMs)
        return;

    s_lastEvalMs = now;
    if (!wxv::provider::readActiveProviderSnapshot(s_snapshotCache))
    {
        if (s_currentMessage.available)
        {
            s_currentMessage = ForecastSummaryMessage{};
            Serial.println("[ForecastSummary] no summary selected");
        }
        return;
    }

    const uint32_t sig = dataSignature(s_snapshotCache);
    const bool force = (sig != s_lastDataSignature);
    s_lastDataSignature = sig;
    evaluateForecastSummary(force);
}

bool forecastSummaryHasMessage()
{
    return s_currentMessage.available;
}

bool forecastSummaryScreenAllowed()
{
    if (!s_currentMessage.available)
        return false;
    if (isAlarmCurrentlyActive() || noaaHasActiveAlert())
        return false;
    return true;
}

const ForecastSummaryMessage &currentForecastSummaryMessage()
{
    return s_currentMessage;
}

void beginForecastSummaryDisplay()
{
    if (!s_currentMessage.available)
        return;
    s_screenActive = true;
    s_autoPresentationPending = false;
}

void finishForecastSummaryDisplay()
{
    s_screenActive = false;
}

bool forecastSummaryDisplayExpired()
{
    return false;
}

bool forecastSummaryScreenActive()
{
    return s_screenActive;
}

bool forecastSummaryShouldAutoPresent()
{
    if (!s_currentMessage.available)
        return false;
    if (isAlarmCurrentlyActive() || noaaHasActiveAlert())
        return false;
    return s_autoPresentationPending;
}

void acknowledgeForecastSummaryAutoPresent()
{
    s_autoPresentationPending = false;
}

void resetForecastSummaryState()
{
    s_currentMessage = ForecastSummaryMessage{};
    s_lastDataSignature = 0;
    s_lastEvalMs = 0;
    s_screenActive = false;
    s_autoPresentationPending = false;
}
