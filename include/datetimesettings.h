#pragma once
#include <Arduino.h>
#include <RTClib.h>
#define DMA_DISPLAY // Define this if using DMA display library
#include <stddef.h>

// --- Date/time settings values
extern int tzOffset;             // Effective offset from UTC (minutes), includes DST when active
extern int tzStandardOffset;     // Base offset without DST (minutes)
extern bool tzAutoDst;           // true if automatic DST adjustment enabled
extern int fmt24;                // 0 = 12h, 1 = 24h
extern int dateFmt;              // 0 = YYYY-MM-DD, etc.
extern char ntpServerHost[64];
extern int ntpServerPreset;      // 0..NTP_PRESET_CUSTOM

constexpr size_t TZ_NAME_MAX = 48;
extern char tzName[TZ_NAME_MAX]; // IANA timezone identifier or empty for custom offset

enum class DstRule : uint8_t
{
    None = 0,
    NorthAmerica,
    Newfoundland,
    Europe,
    Azores,
    Australia,
    NewZealand
};

struct TimezoneInfo
{
    const char *id;       // IANA identifier
    const char *city;     // Display city name
    const char *country;  // Country/region label
    int offsetMinutes;    // Standard offset from UTC (minutes)
    DstRule dstRule;      // DST behaviour
    float latitude;       // Representative city latitude
    float longitude;      // Representative city longitude
};

extern const int NTP_PRESET_COUNT;
extern const int NTP_PRESET_CUSTOM;

const char *ntpPresetHost(int index);
void setNtpServerFromHostString(const String& host);

bool syncTimeFromNTP();
bool getLocalDateTime(DateTime &out);
void setSystemTimeFromDateTime(const DateTime &dt);

DateTime utcToLocal(const DateTime &utc);
DateTime utcToLocal(const DateTime &utc, int offsetMinutes);
DateTime localToUtc(const DateTime &local);
DateTime localToUtc(const DateTime &local, int offsetMinutes);

void loadDateTimeSettings();
void saveDateTimeSettings();

void saveDateTimeToEEPROM(const DateTime& dt);
DateTime loadDateTimeFromEEPROM();

// Timezone helpers
size_t timezoneCount();
const TimezoneInfo& timezoneInfoAt(size_t index);
const char* timezoneLabelAt(size_t index);
int timezoneIndexFromId(const char* id);
int timezoneCurrentIndex();
bool timezoneSupportsDst(size_t index);
bool timezoneIsCustom();
void selectTimezoneByIndex(int index);
void setCustomTimezoneOffset(int offsetMinutes);
void setTimezoneAutoDst(bool enable);
int timezoneOffsetForUtc(const DateTime& utc);
int timezoneOffsetForUtcAtIndex(int index, const DateTime& utc);
int timezoneOffsetForLocal(const DateTime& local);
void updateTimezoneOffset();
void updateTimezoneOffsetWithUtc(const DateTime& utc);
const char* currentTimezoneId();

// Helper for applying settings after NTP/RTC sync
void applyTimeZoneOffset(struct tm& tminfo, int offsetMins);

// Optionally: Validate and clamp time fields
void clampDateTimeFields(int& year, int& month, int& day, int& hour, int& min, int& sec);
