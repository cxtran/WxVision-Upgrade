#pragma once

#include <Arduino.h>
#include <RTClib.h>

namespace wxv::astronomy
{
constexpr size_t kSkyFactMarqueeLen = 1024;

enum class SkyFactType : uint8_t
{
    Season = 0,
    EquinoxSolstice,
    Daylight,
    SunCountdown,
    Moon,
    Summary,
    YearProgress
};

enum class MoonPhase : uint8_t
{
    NewMoon = 0,
    WaxingCrescent,
    FirstQuarter,
    WaxingGibbous,
    FullMoon,
    WaningGibbous,
    LastQuarter,
    WaningCrescent
};

struct AstronomyData
{
    bool hasLocation = false;
    bool hasSunTimes = false;
    bool hasSunAzimuth = false;
    bool hasSunAltitude = false;
    bool hasMoonTimes = false;
    bool hasMoonAzimuth = false;
    bool hasMoonAltitude = false;
    double latitude = NAN;
    double longitude = NAN;
    float sunAzimuthDeg = NAN;
    float sunAltitudeDeg = NAN;
    float moonAzimuthDeg = NAN;
    float moonAltitudeDeg = NAN;
    int sunriseMinutes = -1;
    int sunsetMinutes = -1;
    int moonriseMinutes = -1;
    int moonsetMinutes = -1;
    int localMinutes = -1;
    float moonPhaseFraction = 0.0f;
    uint8_t moonIlluminationPct = 0;
    MoonPhase moonPhase = MoonPhase::NewMoon;
    float moonDistanceKm = NAN;
    int localDateKey = 0;
    int minuteBucket = -1;
    unsigned long lastRefreshMs = 0;
};

struct SkyFactPage
{
    SkyFactType type = SkyFactType::Season;
    bool valid = false;
    uint8_t lineCount = 0;
    int8_t trend = 0;
    uint8_t meterFill = 0;
    uint8_t meterTotal = 0;
    char title[16] = "";
    char line1[24] = "";
    char line2[24] = "";
    char line3[24] = "";
    char marquee[kSkyFactMarqueeLen] = "";
};

void updateAstronomyData(bool force = false);
const AstronomyData &astronomyData();
const char *moonPhaseLabel(MoonPhase phase);
void updateSkyFacts(bool force = false);
size_t skyFactCount();
const SkyFactPage &skyFactPage(size_t index);
const SkyFactPage &skySummaryPage();
} // namespace wxv::astronomy
