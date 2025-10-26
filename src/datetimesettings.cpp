#include "datetimesettings.h"
#include <Preferences.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <cstring>
#include <time.h>
#include <sys/time.h>
#include <cstdlib>
#include <cstdio>
#include <strings.h>
#include <algorithm>
#include "display.h"


int tzOffset = 0;      // Effective offset (minutes)
int tzStandardOffset = 0; // Base offset without DST (minutes)
bool tzAutoDst = false;
int fmt24 = 1;
int dateFmt = 0;
extern RTC_DS3231 rtc;
char tzName[TZ_NAME_MAX] = "UTC";
char ntpServerHost[64] = "pool.ntp.org";
int ntpServerPreset = 1; // default to pool.ntp.org
static int tzSelectedIndex = -1;

static const char *const kNtpPresetHosts[] = {
    "time.google.com",
    "pool.ntp.org",
    "us.pool.ntp.org"
};

const int NTP_PRESET_COUNT = sizeof(kNtpPresetHosts) / sizeof(kNtpPresetHosts[0]);
const int NTP_PRESET_CUSTOM = NTP_PRESET_COUNT;

static void refreshNtpHostCache();
static void applySystemTimezone();
static void updateTimezoneOffsetInternal(const DateTime* referenceUtc);
static bool getUtcNow(DateTime &out);

struct TimezoneLabelBuffer
{
    char text[48];
};

static const TimezoneInfo kTimezones[] = {
    {"Pacific/Honolulu",        "Honolulu",        "USA",        -600, DstRule::None},
    {"America/Anchorage",       "Anchorage",       "USA",        -540, DstRule::NorthAmerica},
    {"America/Los_Angeles",     "Los Angeles",     "USA",        -480, DstRule::NorthAmerica},
    {"America/Denver",          "Denver",          "USA",        -420, DstRule::NorthAmerica},
    {"America/Chicago",         "Chicago",         "USA",        -360, DstRule::NorthAmerica},
    {"America/New_York",        "New York",        "USA",        -300, DstRule::NorthAmerica},
    {"America/Halifax",         "Halifax",         "Canada",     -240, DstRule::NorthAmerica},
    {"America/St_Johns",        "St. John's",      "Canada",     -210, DstRule::Newfoundland},
    {"America/Sao_Paulo",       "Sao Paulo",       "Brazil",     -180, DstRule::None},
    {"Atlantic/Azores",         "Azores",          "Portugal",   -60,  DstRule::Azores},
    {"UTC",                     "UTC",             "—",          0,    DstRule::None},
    {"Europe/London",           "London",          "United Kingdom", 0,    DstRule::Europe},
    {"Europe/Berlin",           "Berlin",          "Germany",    60,   DstRule::Europe},
    {"Europe/Athens",           "Athens",          "Greece",     120,  DstRule::Europe},
    {"Europe/Moscow",           "Moscow",          "Russia",     180,  DstRule::None},
    {"Asia/Dubai",              "Dubai",           "UAE",        240,  DstRule::None},
    {"Asia/Karachi",            "Karachi",         "Pakistan",   300,  DstRule::None},
    {"Asia/Kolkata",            "Mumbai/Delhi",    "India",      330,  DstRule::None},
    {"Asia/Dhaka",              "Dhaka",           "Bangladesh", 360,  DstRule::None},
    {"Asia/Bangkok",            "Bangkok",         "Thailand",   420,  DstRule::None},
    {"Asia/Hong_Kong",          "Hong Kong",       "China",      480,  DstRule::None},
    {"Asia/Tokyo",              "Tokyo",           "Japan",      540,  DstRule::None},
    {"Australia/Adelaide",      "Adelaide",        "Australia",  570,  DstRule::Australia},
    {"Australia/Sydney",        "Sydney",          "Australia",  600,  DstRule::Australia},
    {"Pacific/Noumea",          "Noumea",          "New Caledonia", 660, DstRule::None},
    {"Pacific/Auckland",        "Auckland",        "New Zealand",720, DstRule::NewZealand}
};

static const size_t kTimezoneCount = sizeof(kTimezones) / sizeof(kTimezones[0]);
static TimezoneLabelBuffer kTimezoneLabels[kTimezoneCount];
static bool timezoneLabelsInit = false;

static bool isLeapYear(int year)
{
    if ((year % 4) != 0)
        return false;
    if ((year % 100) != 0)
        return true;
    return (year % 400) == 0;
}

static int daysInMonth(int year, int month)
{
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int days = kDays[(month - 1) % 12];
    if (month == 2 && isLeapYear(year))
        days = 29;
    return days;
}

static int nthWeekdayOfMonth(int year, int month, int weekday, int nth)
{
    DateTime firstDay(year, month, 1, 0, 0, 0);
    int firstDow = firstDay.dayOfTheWeek(); // 0=Sunday
    int delta = (weekday - firstDow + 7) % 7;
    int day = 1 + delta + 7 * (nth - 1);
    int dim = daysInMonth(year, month);
    if (day > dim)
        day -= 7;
    return day;
}

static int lastWeekdayOfMonth(int year, int month, int weekday)
{
    int dim = daysInMonth(year, month);
    DateTime lastDate(year, month, dim, 0, 0, 0);
    int lastDow = lastDate.dayOfTheWeek();
    int delta = (lastDow - weekday + 7) % 7;
    return dim - delta;
}

static void computeDstLocalRange(const TimezoneInfo& tz, int year, DateTime& startLocal, DateTime& endLocal)
{
    switch (tz.dstRule)
    {
    case DstRule::NorthAmerica:
    case DstRule::Newfoundland:
    {
        int startDay = nthWeekdayOfMonth(year, 3, 0, 2);  // second Sunday March
        int endDay = nthWeekdayOfMonth(year, 11, 0, 1);   // first Sunday November
        startLocal = DateTime(year, 3, startDay, 2, 0, 0);
        endLocal = DateTime(year, 11, endDay, 2, 0, 0);
        break;
    }
    case DstRule::Europe:
    {
        int startDay = lastWeekdayOfMonth(year, 3, 0);    // last Sunday March
        int endDay = lastWeekdayOfMonth(year, 10, 0);     // last Sunday October
        startLocal = DateTime(year, 3, startDay, 2, 0, 0);
        endLocal = DateTime(year, 10, endDay, 3, 0, 0);
        break;
    }
    case DstRule::Azores:
    {
        int startDay = lastWeekdayOfMonth(year, 3, 0);
        int endDay = lastWeekdayOfMonth(year, 10, 0);
        startLocal = DateTime(year, 3, startDay, 0, 0, 0);
        endLocal = DateTime(year, 10, endDay, 1, 0, 0);
        break;
    }
    case DstRule::Australia:
    {
        int startDay = nthWeekdayOfMonth(year, 10, 0, 1); // first Sunday October
        int endDay = nthWeekdayOfMonth(year, 4, 0, 1);    // first Sunday April
        startLocal = DateTime(year, 10, startDay, 2, 0, 0);
        endLocal = DateTime(year, 4, endDay, 3, 0, 0);
        break;
    }
    case DstRule::NewZealand:
    {
        int startDay = lastWeekdayOfMonth(year, 9, 0);    // last Sunday September
        int endDay = nthWeekdayOfMonth(year, 4, 0, 1);    // first Sunday April
        startLocal = DateTime(year, 9, startDay, 2, 0, 0);
        endLocal = DateTime(year, 4, endDay, 3, 0, 0);
        break;
    }
    case DstRule::None:
    default:
        startLocal = endLocal = DateTime(2000, 1, 1, 0, 0, 0);
        break;
    }
}

static void computeDstUtcRange(const TimezoneInfo& tz, int year, DateTime& startUtc, DateTime& endUtc)
{
    DateTime startLocal, endLocal;
    computeDstLocalRange(tz, year, startLocal, endLocal);

    if (tz.dstRule == DstRule::None)
    {
        startUtc = endUtc = DateTime(2000, 1, 1, 0, 0, 0);
        return;
    }

    int64_t startEpoch = static_cast<int64_t>(startLocal.unixtime()) - static_cast<int64_t>(tz.offsetMinutes) * 60;
    int64_t endEpoch = static_cast<int64_t>(endLocal.unixtime()) - static_cast<int64_t>(tz.offsetMinutes + 60) * 60;

    if (startEpoch < 0)
        startEpoch = 0;
    if (endEpoch < 0)
        endEpoch = 0;

    startUtc = DateTime(static_cast<uint32_t>(startEpoch));
    endUtc = DateTime(static_cast<uint32_t>(endEpoch));
}

static bool isDstActiveUtc(const TimezoneInfo& tz, const DateTime& utc)
{
    if (tz.dstRule == DstRule::None)
        return false;

    DateTime startUtc, endUtc;
    computeDstUtcRange(tz, utc.year(), startUtc, endUtc);

    uint32_t current = utc.unixtime();
    uint32_t startEpoch = startUtc.unixtime();
    uint32_t endEpoch = endUtc.unixtime();

    if (startEpoch <= endEpoch)
        return current >= startEpoch && current < endEpoch;
    return current >= startEpoch || current < endEpoch;
}

static bool isDstActiveLocal(const TimezoneInfo& tz, const DateTime& local)
{
    if (tz.dstRule == DstRule::None)
        return false;

    DateTime startUtc, endUtc;
    computeDstUtcRange(tz, local.year(), startUtc, endUtc);

    TimeSpan stdOffset(tz.offsetMinutes * 60);
    TimeSpan dstOffset((tz.offsetMinutes + 60) * 60);

    DateTime startLocal = startUtc + stdOffset;
    DateTime endLocal = endUtc + dstOffset;

    if (startLocal.unixtime() <= endLocal.unixtime())
        return local >= startLocal && local < endLocal;
    return local >= startLocal || local < endLocal;
}

static void formatUtcOffset(int offsetMinutes, char *out, size_t len)
{
    int absMinutes = std::abs(offsetMinutes);
    int hours = absMinutes / 60;
    int minutes = absMinutes % 60;
    char sign = (offsetMinutes >= 0) ? '+' : '-';
    snprintf(out, len, "UTC%c%02d:%02d", sign, hours, minutes);
}

static void ensureTimezoneLabels()
{
    if (timezoneLabelsInit)
        return;
    for (size_t i = 0; i < kTimezoneCount; ++i)
    {
        char offsetBuf[16];
        formatUtcOffset(kTimezones[i].offsetMinutes, offsetBuf, sizeof(offsetBuf));
        snprintf(kTimezoneLabels[i].text, sizeof(kTimezoneLabels[i].text),
                 "%s, %s (%s)",
                 kTimezones[i].city,
                 kTimezones[i].country,
                 offsetBuf);
    }
    timezoneLabelsInit = true;
}

size_t timezoneCount()
{
    return kTimezoneCount;
}

const TimezoneInfo& timezoneInfoAt(size_t index)
{
    if (index >= kTimezoneCount)
        return kTimezones[0];
    return kTimezones[index];
}

const char* timezoneLabelAt(size_t index)
{
    ensureTimezoneLabels();
    if (index >= kTimezoneCount)
        return "";
    return kTimezoneLabels[index].text;
}

int timezoneIndexFromId(const char* id)
{
    if (!id || !id[0])
        return -1;
    for (size_t i = 0; i < kTimezoneCount; ++i)
    {
        if (strcasecmp(kTimezones[i].id, id) == 0)
            return static_cast<int>(i);
    }
    return -1;
}

int timezoneCurrentIndex()
{
    return tzSelectedIndex;
}

bool timezoneSupportsDst(size_t index)
{
    if (index >= kTimezoneCount)
        return false;
    return kTimezones[index].dstRule != DstRule::None;
}

bool timezoneIsCustom()
{
    return tzSelectedIndex < 0;
}

const char* currentTimezoneId()
{
    return timezoneIsCustom() ? "" : tzName;
}

static void setTimezoneInternal(int index)
{
    if (index < 0 || index >= static_cast<int>(kTimezoneCount))
    {
        tzSelectedIndex = -1;
        tzName[0] = '\0';
        return;
    }
    tzSelectedIndex = index;
    strncpy(tzName, kTimezones[index].id, TZ_NAME_MAX - 1);
    tzName[TZ_NAME_MAX - 1] = '\0';
    tzStandardOffset = kTimezones[index].offsetMinutes;
}

void selectTimezoneByIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(kTimezoneCount))
        return;

    setTimezoneInternal(index);

    if (!timezoneSupportsDst(index))
        tzAutoDst = false;

    updateTimezoneOffsetInternal(nullptr);
}

void setCustomTimezoneOffset(int offsetMinutes)
{
    offsetMinutes = std::max(-720, std::min(840, offsetMinutes));
    tzSelectedIndex = -1;
    tzStandardOffset = offsetMinutes;
    tzAutoDst = false;
    tzName[0] = '\0';
    updateTimezoneOffsetInternal(nullptr);
}

void setTimezoneAutoDst(bool enable)
{
    if (tzSelectedIndex < 0)
        enable = false;
    else if (!timezoneSupportsDst(static_cast<size_t>(tzSelectedIndex)))
        enable = false;

    if (tzAutoDst == enable)
        return;
    tzAutoDst = enable;
    updateTimezoneOffsetInternal(nullptr);
}

int timezoneOffsetForUtc(const DateTime& utc)
{
    int offset = tzStandardOffset;
    if (tzAutoDst && tzSelectedIndex >= 0)
    {
        const TimezoneInfo& info = kTimezones[tzSelectedIndex];
        if (isDstActiveUtc(info, utc))
            offset += 60;
    }
    return offset;
}

int timezoneOffsetForLocal(const DateTime& local)
{
    int offset = tzStandardOffset;
    if (tzAutoDst && tzSelectedIndex >= 0)
    {
        const TimezoneInfo& info = kTimezones[tzSelectedIndex];
        if (isDstActiveLocal(info, local))
            offset += 60;
    }
    return offset;
}

static void applySystemTimezone()
{
    char tzEnv[16];
    if (tzOffset == 0)
    {
        strcpy(tzEnv, "UTC0");
    }
    else
    {
        int absMinutes = std::abs(tzOffset);
        int hours = absMinutes / 60;
        int minutes = absMinutes % 60;
        char sign = (tzOffset >= 0) ? '-' : '+';
        if (minutes == 0)
        {
            snprintf(tzEnv, sizeof(tzEnv), "UTC%c%02d", sign, hours);
        }
        else
        {
            snprintf(tzEnv, sizeof(tzEnv), "UTC%c%02d:%02d", sign, hours, minutes);
        }
    }
    setenv("TZ", tzEnv, 1);
    tzset();
}

static bool getUtcNow(DateTime &out)
{
    if (rtcReady)
    {
        out = rtc.now();
        return true;
    }

    time_t now = time(nullptr);
    if (now > 0)
    {
        out = DateTime(static_cast<uint32_t>(now));
        return true;
    }
    return false;
}

void updateTimezoneOffset()
{
    updateTimezoneOffsetInternal(nullptr);
}

void updateTimezoneOffsetWithUtc(const DateTime& utc)
{
    updateTimezoneOffsetInternal(&utc);
}

static void updateTimezoneOffsetInternal(const DateTime* referenceUtc)
{
    bool forceApply = (referenceUtc != nullptr);
    DateTime utc;
    if (referenceUtc)
    {
        utc = *referenceUtc;
    }
    else
    {
        if (!getUtcNow(utc))
        {
            tzOffset = tzStandardOffset;
            applySystemTimezone();
            return;
        }
    }

    int newOffset = tzStandardOffset;
    if (tzAutoDst && tzSelectedIndex >= 0)
    {
        const TimezoneInfo& info = kTimezones[tzSelectedIndex];
        if (isDstActiveUtc(info, utc))
            newOffset += 60;
    }

    bool offsetChanged = (tzOffset != newOffset);
    tzOffset = newOffset;
    if (offsetChanged || forceApply)
    {
        applySystemTimezone();
    }
}

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

void setNtpServerFromHostString(const String& rawHost)
{
    String host = rawHost;
    host.trim();
    if (host.isEmpty())
    {
        host = "pool.ntp.org";
    }

    bool matched = false;
    for (int i = 0; i < NTP_PRESET_COUNT; ++i)
    {
        if (host.equalsIgnoreCase(kNtpPresetHosts[i]))
        {
            ntpServerPreset = i;
            matched = true;
            break;
        }
    }

    if (matched)
    {
        strncpy(ntpServerHost, kNtpPresetHosts[ntpServerPreset], sizeof(ntpServerHost) - 1);
        ntpServerHost[sizeof(ntpServerHost) - 1] = '\0';
    }
    else
    {
        ntpServerPreset = NTP_PRESET_CUSTOM;
        host.toCharArray(ntpServerHost, sizeof(ntpServerHost));
        ntpServerHost[sizeof(ntpServerHost) - 1] = '\0';
    }

    refreshNtpHostCache();
}

bool getLocalDateTime(DateTime &out)
{
    DateTime utc;
    if (getUtcNow(utc))
    {
        updateTimezoneOffsetWithUtc(utc);
        out = utcToLocal(utc, tzOffset);
        return true;
    }

    struct tm timeinfo{};
    if (!getLocalTime(&timeinfo))
    {
        return false;
    }

    DateTime local(timeinfo.tm_year + 1900,
                   timeinfo.tm_mon + 1,
                   timeinfo.tm_mday,
                   timeinfo.tm_hour,
                   timeinfo.tm_min,
                   timeinfo.tm_sec);

    int offset = timezoneOffsetForLocal(local);
    DateTime derivedUtc = localToUtc(local, offset);
    updateTimezoneOffsetWithUtc(derivedUtc);
    out = utcToLocal(derivedUtc, tzOffset);
    return true;
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
    updateTimezoneOffsetWithUtc(utc);
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
    int offset = timezoneOffsetForLocal(local);
    return localToUtc(local, offset);
}
#define EEPROM_ADDR_DT 0x00

void loadDateTimeSettings()
{
    Preferences prefs;
    prefs.begin("visionwx", true);

    int storedOffset = prefs.getInt("tz_offset", 0);
    int storedStd = prefs.getInt("tz_std", storedOffset);
    dateFmt = prefs.getInt("date_fmt", 0);
    fmt24 = prefs.getInt("time_24h", 1);
    bool storedAutoDst = prefs.getBool("tz_dst_auto", false);
    ntpServerPreset = prefs.getInt("ntp_preset", -1);
    String savedHost = prefs.getString("ntp_host", "pool.ntp.org");
    String storedName = prefs.getString("tz_name", "");
    prefs.end();

    savedHost.trim();
    if (savedHost.isEmpty())
    {
        savedHost = "pool.ntp.org";
    }

    storedName.trim();

    tzOffset = storedOffset;
    tzStandardOffset = storedStd;
    tzSelectedIndex = -1;
    tzName[0] = '\0';

    if (storedName.length() > 0)
    {
        int idx = timezoneIndexFromId(storedName.c_str());
        if (idx >= 0)
        {
            setTimezoneInternal(idx);
        }
    }

    if (tzSelectedIndex < 0)
    {
        for (size_t i = 0; i < kTimezoneCount; ++i)
        {
            if (kTimezones[i].offsetMinutes == storedStd)
            {
                setTimezoneInternal(static_cast<int>(i));
                break;
            }
        }
    }

    if (tzSelectedIndex < 0)
    {
        tzStandardOffset = storedStd;
    }

    if (tzSelectedIndex < 0)
    {
        tzAutoDst = false;
    }
    else
    {
        tzAutoDst = storedAutoDst && timezoneSupportsDst(static_cast<size_t>(tzSelectedIndex));
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
    applySystemTimezone();
    updateTimezoneOffset();
}

void saveDateTimeSettings()
{
    Preferences prefs;
    prefs.begin("visionwx", false);

    updateTimezoneOffset();

    prefs.putInt("tz_offset", tzOffset);
    prefs.putInt("tz_std", tzStandardOffset);
    prefs.putBool("tz_dst_auto", tzAutoDst);
    prefs.putString("tz_name", timezoneIsCustom() ? "" : tzName);
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

        setenv("TZ", "UTC0", 1);
        tzset();
        configTime(0, 0, host, "time.nist.gov", "pool.ntp.org");

        memset(&timeinfo, 0, sizeof(timeinfo));
        const int maxAttempts = 15;
        time_t base = time(nullptr);
        for (int attempt = 0; attempt < maxAttempts; ++attempt)
        {
            time_t candidate = time(nullptr);
            bool timeChanged = (base <= 0 || candidate <= 0)
                ? false
                : (llabs((long long)candidate - (long long)base) > 30);

            if (getLocalTime(&timeinfo))
            {
                if (timeChanged ||
                    (timeinfo.tm_year + 1900 >= 2020 && (base <= 0 || candidate <= 0)))
                {
                    return true;
                }
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
        applySystemTimezone();
        return false;
    }

    int year = timeinfo.tm_year + 1900;
    if (year < 2000 || year > 2099)
    {
        Serial.printf("[NTP] Invalid year from server: %d\n", year);
        applySystemTimezone();
        return false;
    }

    DateTime newUtc(
        year,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
    );

    updateTimezoneOffsetWithUtc(newUtc);
    DateTime localTime = utcToLocal(newUtc, tzOffset);

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
    applySystemTimezone();

    Serial.printf("[NTP] Time set (local): %04d-%02d-%02d %02d:%02d:%02d\n",
                  localTime.year(), localTime.month(), localTime.day(),
                 localTime.hour(), localTime.minute(), localTime.second());
    Serial.printf("[NTP] Stored UTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                  newUtc.year(), newUtc.month(), newUtc.day(),
                  newUtc.hour(), newUtc.minute(), newUtc.second());

    refreshNtpHostCache();

    return true;
}
