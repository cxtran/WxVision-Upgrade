#include "datetimesettings.h"
#include <Preferences.h>
#include <EEPROM.h>
#include "display.h"


int tzOffset = 0;      // Default UTC
int fmt24 = 1;
int dateFmt = 0;
extern RTC_DS3231 rtc;

// Assume EEPROM is already initialized somewhere else (usually at setup)
#define EEPROM_ADDR_DT 0x00

void loadDateTimeSettings() {
    Preferences prefs;
    prefs.begin("visionwx", true);
    tzOffset = prefs.getInt("tz_offset", 0);
    dateFmt  = prefs.getInt("date_fmt", 0);
    fmt24    = prefs.getInt("time_24h", 1);
    prefs.end();
}

void saveDateTimeSettings() {
    Preferences prefs;
    prefs.begin("visionwx", false);
    prefs.putInt("tz_offset", tzOffset);
    prefs.putInt("date_fmt", dateFmt);
    prefs.putInt("time_24h", fmt24);
    prefs.end();
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

// Optionally, your NTP server
const char* ntpServer = "pool.ntp.org";

void syncTimeFromNTP() {
    // Convert tzOffset (in minutes) to seconds for configTime()
    int tzSeconds = tzOffset * 60;
    int daylightSeconds = 0; // Add your DST logic if needed

    Serial.println("Syncing time from NTP...");

    // Set timezone and NTP server
    configTime(tzSeconds, daylightSeconds, ntpServer);

    // Wait for time to be set
    struct tm timeinfo;
    int attempts = 0;
    bool gotTime = false;
    while (attempts < 15) { // Wait up to 3 seconds
        if (getLocalTime(&timeinfo)) {
            gotTime = true;
            break;
        }
        delay(200);
        attempts++;
    }

    if (!gotTime) {
        Serial.println("❌ NTP sync failed (timeout)");
        return;
    }

    int year = timeinfo.tm_year + 1900;
    if (year < 2000 || year > 2099) {
        Serial.printf("⚠️ NTP gave invalid year: %d\n", year);
        return;
    }

    DateTime newTime(
        year,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
    );

    // Update RTC
    rtc.adjust(newTime);

    Serial.printf("✅ NTP time set: %04d-%02d-%02d %02d:%02d:%02d\n",
                  newTime.year(), newTime.month(), newTime.day(),
                  newTime.hour(), newTime.minute(), newTime.second());

}