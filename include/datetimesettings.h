#pragma once
#include <Arduino.h>
#include <RTClib.h>
#define DMA_DISPLAY // Define this if using DMA display library
// --- Date/time settings values
extern int tzOffset;        // Minutes offset from UTC, e.g. 420 for UTC+7
extern int fmt24;           // 0 = 12h, 1 = 24h
extern int dateFmt;         // 0 = YYYY-MM-DD, etc.
extern char ntpServerHost[64];
extern int ntpServerPreset; // 0..NTP_PRESET_CUSTOM

extern const int NTP_PRESET_COUNT;
extern const int NTP_PRESET_CUSTOM;

const char *ntpPresetHost(int index);

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

// Helper for applying settings after NTP/RTC sync
void applyTimeZoneOffset(struct tm& tminfo, int offsetMins);

// Optionally: Validate and clamp time fields
void clampDateTimeFields(int& year, int& month, int& day, int& hour, int& min, int& sec);
