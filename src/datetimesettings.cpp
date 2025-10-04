#include "datetimesettings.h"
#include <Preferences.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <cstring>
#include <time.h>
#include <sys/time.h>
#include "display.h"


int tzOffset = 0;      // Default UTC
int fmt24 = 1;
int dateFmt = 0;
extern RTC_DS3231 rtc;
char ntpServerHost[64] = "pool.ntp.org";
int ntpServerPreset = 1; // default to pool.ntp.org

static const char *const kNtpPresetHosts[] = {
    "time.google.com",
    "pool.ntp.org",
    "us.pool.ntp.org"
};

const int NTP_PRESET_COUNT = sizeof(kNtpPresetHosts) / sizeof(kNtpPresetHosts[0]);
const int NTP_PRESET_CUSTOM = NTP_PRESET_COUNT;

static void refreshNtpHostCache();

const char *ntpPresetHost(int index)
{
    if (index >= 0 && index < NTP_PRESET_COUNT)
    {
        return kNtpPresetHosts[index];
    }
    if (index == NTP_PRESET_CUSTOM)
    {
        return ntpServerHost;
    }
    return "";
}

bool getLocalDateTime(DateTime &out)
{
    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
        out = DateTime(timeinfo.tm_year + 1900,
                       timeinfo.tm_mon + 1,
                       timeinfo.tm_mday,
                       timeinfo.tm_hour,
                       timeinfo.tm_min,
                       timeinfo.tm_sec);
        return true;
    }
    return false;
}

void setSystemTimeFromDateTime(const DateTime &dt)
{
    struct timeval tv;
    tv.tv_sec = dt.unixtime();
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
}

DateTime utcToLocal(const DateTime &utc, int offsetMinutes)
{
    int32_t offsetSeconds = static_cast<int32_t>(offsetMinutes) * 60;
    TimeSpan offsetSpan(offsetSeconds);
    return utc + offsetSpan;
}

DateTime utcToLocal(const DateTime &utc)
{
    return utcToLocal(utc, tzOffset);
}

DateTime localToUtc(const DateTime &local, int offsetMinutes)
{
    int32_t offsetSeconds = static_cast<int32_t>(offsetMinutes) * 60;
    TimeSpan offsetSpan(offsetSeconds);
    return local - offsetSpan;
}

DateTime localToUtc(const DateTime &local)
{
    return localToUtc(local, tzOffset);
}
// Assume EEPROM is already initialized somewhere else (usually at setup)
#define EEPROM_ADDR_DT 0x00

void loadDateTimeSettings() {
    Preferences prefs;
    prefs.begin("visionwx", true);
    tzOffset = prefs.getInt("tz_offset", 0);
    dateFmt  = prefs.getInt("date_fmt", 0);
    fmt24    = prefs.getInt("time_24h", 1);
    ntpServerPreset = prefs.getInt("ntp_preset", -1);
    String savedHost = prefs.getString("ntp_host", "pool.ntp.org");
    savedHost.trim();
    if (savedHost.length() == 0)
    {
        savedHost = "pool.ntp.org";
    }

    if (ntpServerPreset < 0 || ntpServerPreset > NTP_PRESET_CUSTOM)
    {
        ntpServerPreset = NTP_PRESET_CUSTOM;
        for (int i = 0; i < NTP_PRESET_COUNT; ++i)
        {
            if (savedHost.equalsIgnoreCase(kNtpPresetHosts[i]))
            {
                ntpServerPreset = i;
                break;
            }
        }
    }

    if (ntpServerPreset >= 0 && ntpServerPreset < NTP_PRESET_COUNT)
    {
        strncpy(ntpServerHost, kNtpPresetHosts[ntpServerPreset], sizeof(ntpServerHost) - 1);
        ntpServerHost[sizeof(ntpServerHost) - 1] = '\0';
    }
    else
    {
        ntpServerPreset = NTP_PRESET_CUSTOM;
        savedHost.toCharArray(ntpServerHost, sizeof(ntpServerHost));
    }

    if (ntpServerPreset == NTP_PRESET_CUSTOM)
    {
        for (int i = 0; i < NTP_PRESET_COUNT; ++i)
        {
            if (savedHost.equalsIgnoreCase(kNtpPresetHosts[i]))
            {
                ntpServerPreset = i;
                strncpy(ntpServerHost, kNtpPresetHosts[i], sizeof(ntpServerHost) - 1);
                ntpServerHost[sizeof(ntpServerHost) - 1] = '\0';
                break;
            }
        }
    }

    refreshNtpHostCache();

    prefs.end();
}

void saveDateTimeSettings() {
    Preferences prefs;
    prefs.begin("visionwx", false);
    prefs.putInt("tz_offset", tzOffset);
    prefs.putInt("date_fmt", dateFmt);
    prefs.putInt("time_24h", fmt24);
    refreshNtpHostCache();
    const char *toStore = (ntpServerPreset >= 0 && ntpServerPreset < NTP_PRESET_COUNT)
                              ? kNtpPresetHosts[ntpServerPreset]
                              : ntpServerHost;
    prefs.putString("ntp_host", toStore);
    prefs.putInt("ntp_preset", ntpServerPreset);
    prefs.end();

    if (ntpServerPreset >= 0 && ntpServerPreset < NTP_PRESET_COUNT)
    {
        strncpy(ntpServerHost, toStore, sizeof(ntpServerHost) - 1);
        ntpServerHost[sizeof(ntpServerHost) - 1] = '\0';
    }
}

// Store the current RTC time into EEPROM (6 bytes: Y,M,D,H,M,S)
void saveDateTimeToEEPROM(const DateTime& dt) {
    EEPROM.write(EEPROM_ADDR_DT + 0, (dt.year() >> 8) & 0xFF);
    EEPROM.write(EEPROM_ADDR_DT + 1, dt.year() & 0xFF);
    EEPROM.write(EEPROM_ADDR_DT + 2, dt.month());
    EEPROM.write(EEPROM_ADDR_DT + 3, dt.day());
    EEPROM.write(EEPROM_ADDR_DT + 4, dt.hour());
    EEPROM.write(EEPROM_ADDR_DT + 5, dt.minute());
    EEPROM.write(EEPROM_ADDR_DT + 6, dt.second());
    EEPROM.commit();
}

DateTime loadDateTimeFromEEPROM() {
    int year = (EEPROM.read(EEPROM_ADDR_DT + 0) << 8) | EEPROM.read(EEPROM_ADDR_DT + 1);
    int month = EEPROM.read(EEPROM_ADDR_DT + 2);
    int day = EEPROM.read(EEPROM_ADDR_DT + 3);
    int hour = EEPROM.read(EEPROM_ADDR_DT + 4);
    int minute = EEPROM.read(EEPROM_ADDR_DT + 5);
    int second = EEPROM.read(EEPROM_ADDR_DT + 6);
    return DateTime(year, month, day, hour, minute, second);
}

// Utility: Apply TZ offset to struct tm (if you want)
void applyTimeZoneOffset(struct tm& tminfo, int offsetMins) {
    time_t t = mktime(&tminfo);
    t += offsetMins * 60;
    tminfo = *localtime(&t);
}

// Optional: Clamp values to legal range
void clampDateTimeFields(int& year, int& month, int& day, int& hour, int& min, int& sec) {
    if (year < 2020) year = 2020;
    if (year > 2099) year = 2099;
    if (month < 1) month = 1;
    if (month > 12) month = 12;
    if (day < 1) day = 1;
    if (day > 31) day = 31;
    if (hour < 0) hour = 0;
    if (hour > 23) hour = 23;
    if (min < 0) min = 0;
    if (min > 59) min = 59;
    if (sec < 0) sec = 0;
    if (sec > 59) sec = 59;
}

static void refreshNtpHostCache()
{
    if (ntpServerPreset >= 0 && ntpServerPreset < NTP_PRESET_COUNT)
    {
        const char *presetHost = kNtpPresetHosts[ntpServerPreset];
        strncpy(ntpServerHost, presetHost, sizeof(ntpServerHost) - 1);
        ntpServerHost[sizeof(ntpServerHost) - 1] = '\0';
    }
    else
    {
        ntpServerPreset = NTP_PRESET_CUSTOM;
        if (strlen(ntpServerHost) == 0)
        {
            strncpy(ntpServerHost, "pool.ntp.org", sizeof(ntpServerHost) - 1);
            ntpServerHost[sizeof(ntpServerHost) - 1] = '\0';
        }
    }
}



bool syncTimeFromNTP() {
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[NTP] Sync skipped (WiFi not connected)");
        return false;
    }

    Serial.println("Syncing time from NTP...");

    const int tzSeconds = tzOffset * 60;
    const int daylightSeconds = 0; // daylight offset handled via tzOffset

    refreshNtpHostCache();
    const char *primary = ntpServerHost;
    const char *fallbacks[] = {"time.google.com", "time.nist.gov", "pool.ntp.org"};

    struct tm timeinfo{};
    bool success = false;

    auto attemptHost = [&](const char *host) -> bool {
        if (!host || host[0] == '\0')
            return false;

        Serial.print("  trying host: ");
        Serial.println(host);

        configTime(tzSeconds, daylightSeconds, host);

        memset(&timeinfo, 0, sizeof(timeinfo));
        const int maxAttempts = 15;
        for (int attempt = 0; attempt < maxAttempts; ++attempt)
        {
            if (getLocalTime(&timeinfo))
            {
                return true;
            }
            delay(200);
        }
        Serial.println("  host timed out");
        return false;
    };

    const char *candidates[4] = {nullptr};
    size_t candidateCount = 0;

    auto addCandidate = [&](const char *host) {
        if (!host || host[0] == '\0')
            return;
        for (size_t i = 0; i < candidateCount; ++i)
        {
            if (strcmp(candidates[i], host) == 0)
                return;
        }
        if (candidateCount < sizeof(candidates) / sizeof(candidates[0]))
        {
            candidates[candidateCount++] = host;
        }
    };

    addCandidate(primary);
    for (const char *fallback : fallbacks)
    {
        addCandidate(fallback);
    }

    for (size_t i = 0; i < candidateCount; ++i)
    {
        if (attemptHost(candidates[i]))
        {
            success = true;
            break;
        }
    }

    if (!success)
    {
        Serial.println("[NTP] Sync failed for all servers");
        return false;
    }

    int year = timeinfo.tm_year + 1900;
    if (year < 2000 || year > 2099)
    {
        Serial.printf("[NTP] Invalid year from server: %d\n", year);
        return false;
    }

    DateTime localTime(
        year,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
    );

    DateTime newUtc = localToUtc(localTime);

    bool rtcOk = rtcReady;
    if (!rtcOk)
    {
        rtcOk = rtc.begin();
        rtcReady = rtcOk;
    }

    if (rtcOk)
    {
        rtc.adjust(newUtc);
    }
    else
    {
        Serial.println("[NTP] RTC not detected; using system clock only");
    }

    setSystemTimeFromDateTime(newUtc);

    Serial.printf("[NTP] Time set (local): %04d-%02d-%02d %02d:%02d:%02d\n",
                  localTime.year(), localTime.month(), localTime.day(),
                  localTime.hour(), localTime.minute(), localTime.second());
    Serial.printf("[NTP] Stored UTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                  newUtc.year(), newUtc.month(), newUtc.day(),
                  newUtc.hour(), newUtc.minute(), newUtc.second());

    refreshNtpHostCache();

    return true;
}





