#pragma once

#include <Arduino.h>
#include <RTClib.h>
#include <vector>

// Persistent selection storage
void loadWorldTimeSettings();
void saveWorldTimeSettings();
bool worldTimeAutoCycleEnabled();
void worldTimeSetAutoCycleEnabled(bool enabled);

// Selection management
size_t worldTimeSelectionCount();
bool worldTimeHasSelections();
int worldTimeSelectionAt(size_t index);
bool worldTimeIsSelected(int tzIndex);
bool worldTimeToggleTimezone(int tzIndex);
bool worldTimeAddTimezone(int tzIndex);
bool worldTimeRemoveTimezoneAt(size_t index);
void worldTimeClearSelections();

// Clock view state (display-only)
void worldTimeResetView();
bool worldTimeCycleView(int delta);
bool worldTimeIsWorldView();
String worldTimeCurrentZoneLabel();
String worldTimeCurrentZoneId();
bool worldTimeGetCurrentDateTime(DateTime &outLocal);

// World-time weather cache (display only)
struct WorldWeather
{
    String condition;
    float temperature;
    unsigned long lastUpdate;
    bool valid;
};

void worldTimeWeatherTick(bool allowStart = true);
bool worldTimeCurrentWeather(WorldWeather &out);
String worldTimeBuildCurrentHeaderText();

// Selection-index helpers for dedicated world clock screen
String worldTimeSelectionCityLabel(size_t selectionIndex);
bool worldTimeGetSelectionDateTime(size_t selectionIndex, DateTime &outLocal);
bool worldTimeGetSelectionWeather(size_t selectionIndex, WorldWeather &out);

// Custom city entries (for World Clock)
struct WorldTimeCustomCity
{
    String name;
    float lat;
    float lon;
    int tzIndex; // reference timezone used for local time conversion
    bool enabled;
};

size_t worldTimeCustomCityCount();
bool worldTimeGetCustomCity(size_t index, WorldTimeCustomCity &out);
bool worldTimeAddCustomCity(const WorldTimeCustomCity &city);
bool worldTimeRemoveCustomCityAt(size_t index);
bool worldTimeSetCustomCityEnabled(size_t index, bool enabled);
void worldTimeClearCustomCities();

// Combined display entries (selected tz + custom cities)
size_t worldTimeDisplayCount();
String worldTimeDisplayCityLabel(size_t index);
bool worldTimeGetDisplayDateTime(size_t index, DateTime &outLocal);
bool worldTimeGetDisplayWeather(size_t index, WorldWeather &out);
