#include "astronomy.h"

#include <math.h>
#include <string.h>

#include "datetimesettings.h"
#include "display.h"
#include "settings.h"

namespace wxv::astronomy
{
namespace
{
AstronomyData s_data;
constexpr size_t kMaxSkyFactPages = 5;
SkyFactPage s_skyFactPages[kMaxSkyFactPages];
SkyFactPage s_summaryPage;
size_t s_skyFactCount = 0;
int s_skyFactsDateKey = 0;
int s_skyFactsMinute = -1;
double s_skyFactsLat = NAN;
double s_skyFactsLon = NAN;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kMoonSynodicDays = 29.530588853;

double normalizeDegrees(double deg)
{
    while (deg < 0.0)
        deg += 360.0;
    while (deg >= 360.0)
        deg -= 360.0;
    return deg;
}

double normalizeSignedDegrees(double deg)
{
    deg = normalizeDegrees(deg);
    if (deg > 180.0)
        deg -= 360.0;
    return deg;
}

bool validCoordinates(double lat, double lon)
{
    return isfinite(lat) && isfinite(lon) &&
           lat >= -90.0 && lat <= 90.0 &&
           lon >= -180.0 && lon <= 180.0 &&
           !(fabs(lat) < 0.001 && fabs(lon) < 0.001);
}

bool resolveCoordinates(double &lat, double &lon)
{
    if (validCoordinates(noaaLatitude, noaaLongitude))
    {
        lat = static_cast<double>(noaaLatitude);
        lon = static_cast<double>(noaaLongitude);
        return true;
    }

    if (!timezoneIsCustom())
    {
        int tzIndex = timezoneCurrentIndex();
        if (tzIndex >= 0 && static_cast<size_t>(tzIndex) < timezoneCount())
        {
            const TimezoneInfo &tz = timezoneInfoAt(static_cast<size_t>(tzIndex));
            if (validCoordinates(tz.latitude, tz.longitude))
            {
                lat = static_cast<double>(tz.latitude);
                lon = static_cast<double>(tz.longitude);
                return true;
            }
        }
    }

    lat = NAN;
    lon = NAN;
    return false;
}

bool currentTimes(DateTime &localNow, DateTime &utcNow)
{
    if (rtcReady)
    {
        utcNow = rtc.now();
        int offsetMinutes = timezoneIsCustom() ? tzStandardOffset : timezoneOffsetForUtc(utcNow);
        localNow = utcToLocal(utcNow, offsetMinutes);
        return true;
    }

    if (!getLocalDateTime(localNow))
        return false;

    utcNow = localToUtc(localNow);
    return true;
}

int dayOfYear(int year, int month, int day)
{
    static const int daysBeforeMonth[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int doy = daysBeforeMonth[constrain(month, 1, 12) - 1] + day;
    const bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
    if (leap && month > 2)
        ++doy;
    return doy;
}

double julianDateFromUnix(time_t epoch)
{
    return static_cast<double>(epoch) / 86400.0 + 2440587.5;
}

double computeSunEclipticLongitude(double jd)
{
    const double n = jd - 2451545.0;
    const double L = normalizeDegrees(280.460 + 0.9856474 * n);
    const double g = normalizeDegrees(357.528 + 0.9856003 * n) * kDegToRad;
    return normalizeDegrees(L + 1.915 * sin(g) + 0.020 * sin(2.0 * g));
}

bool isLeapYear(int year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

int daysInYear(int year)
{
    return isLeapYear(year) ? 366 : 365;
}

int daysBetweenDates(const DateTime &start, const DateTime &end)
{
    const int32_t delta = static_cast<int32_t>(end.unixtime() - start.unixtime());
    return delta / 86400;
}

DateTime addDays(const DateTime &date, int days)
{
    const int64_t shifted = static_cast<int64_t>(date.unixtime()) + static_cast<int64_t>(days) * 86400LL;
    return DateTime(static_cast<uint32_t>(shifted < 0 ? 0 : shifted));
}

struct SeasonalMarker
{
    int month;
    int day;
    const char *seasonNameNorth;
    const char *seasonNameSouth;
    const char *eventName;
};

const SeasonalMarker kSeasonMarkers[] = {
    {3, 20, "Spring", "Autumn", "Spring Equinox"},
    {6, 21, "Summer", "Winter", "Summer Solstice"},
    {9, 22, "Autumn", "Spring", "Autumn Equinox"},
    {12, 21, "Winter", "Summer", "Winter Solstice"}};

struct SeasonEventUtc
{
    uint8_t markerIndex;
    time_t epoch;
};

double seasonEventJde0(int year, uint8_t markerIndex)
{
    const double y = (static_cast<double>(year) - 2000.0) / 1000.0;
    switch (markerIndex & 3)
    {
    case 0:
        return 2451623.80984 + 365242.37404 * y + 0.05169 * y * y - 0.00411 * y * y * y - 0.00057 * y * y * y * y;
    case 1:
        return 2451716.56767 + 365241.62603 * y + 0.00325 * y * y + 0.00888 * y * y * y - 0.00030 * y * y * y * y;
    case 2:
        return 2451810.21715 + 365242.01767 * y - 0.11575 * y * y + 0.00337 * y * y * y + 0.00078 * y * y * y * y;
    default:
        return 2451900.05952 + 365242.74049 * y - 0.06223 * y * y - 0.00823 * y * y * y + 0.00032 * y * y * y * y;
    }
}

double refinedSeasonEventJde(int year, uint8_t markerIndex)
{
    static const int16_t kSeasonTermA[] = {485, 203, 199, 182, 156, 136, 77, 74, 70, 58, 52, 50,
                                           45, 44, 29, 18, 17, 16, 14, 12, 12, 12, 9, 8};
    static const double kSeasonTermB[] = {324.96, 337.23, 342.08, 27.85, 73.14, 171.52, 222.54, 296.72,
                                          243.58, 119.81, 297.17, 21.02, 247.54, 325.15, 60.93, 155.12,
                                          288.79, 198.04, 199.76, 95.39, 287.11, 320.81, 227.73, 15.45};
    static const double kSeasonTermC[] = {1934.136, 32964.467, 20.186, 445267.112, 45036.886, 22518.443,
                                          65928.934, 3034.906, 9037.513, 33718.147, 150.678, 2281.226,
                                          29929.562, 31555.956, 4443.417, 67555.328, 4562.452, 62894.029,
                                          31436.921, 14577.848, 31931.756, 34777.259, 1222.114, 16859.074};

    const double jde0 = seasonEventJde0(year, markerIndex);
    const double T = (jde0 - 2451545.0) / 36525.0;
    const double W = (35999.373 * T - 2.47) * kDegToRad;
    const double deltaLambda = 1.0 + 0.0334 * cos(W) + 0.0007 * cos(2.0 * W);

    double S = 0.0;
    for (size_t i = 0; i < sizeof(kSeasonTermA) / sizeof(kSeasonTermA[0]); ++i)
        S += static_cast<double>(kSeasonTermA[i]) * cos((kSeasonTermB[i] + kSeasonTermC[i] * T) * kDegToRad);

    return jde0 + (0.00001 * S) / deltaLambda;
}

time_t unixTimeFromJulianDate(double jd)
{
    const double unixSeconds = (jd - 2440587.5) * 86400.0;
    return static_cast<time_t>(llround(unixSeconds));
}

SeasonEventUtc seasonEventUtc(int year, uint8_t markerIndex)
{
    SeasonEventUtc event{};
    event.markerIndex = markerIndex & 3;
    event.epoch = unixTimeFromJulianDate(refinedSeasonEventJde(year, event.markerIndex));
    return event;
}

bool seasonBoundsForUtc(const DateTime &utcNow, SeasonEventUtc &current, SeasonEventUtc &next)
{
    SeasonEventUtc events[12];
    size_t count = 0;
    for (int year = utcNow.year() - 1; year <= utcNow.year() + 1; ++year)
    {
        for (uint8_t markerIndex = 0; markerIndex < 4; ++markerIndex)
            events[count++] = seasonEventUtc(year, markerIndex);
    }

    const time_t nowEpoch = static_cast<time_t>(utcNow.unixtime());
    bool foundCurrent = false;
    bool foundNext = false;
    current = events[0];
    next = events[count - 1];

    for (size_t i = 0; i < count; ++i)
    {
        if (events[i].epoch <= nowEpoch)
        {
            current = events[i];
            foundCurrent = true;
            continue;
        }

        next = events[i];
        foundNext = true;
        break;
    }

    return foundCurrent && foundNext && next.epoch > current.epoch;
}

const SeasonalMarker &markerForDate(const DateTime &localNow)
{
    for (int i = static_cast<int>(sizeof(kSeasonMarkers) / sizeof(kSeasonMarkers[0])) - 1; i >= 0; --i)
    {
        const SeasonalMarker &marker = kSeasonMarkers[i];
        if (localNow.month() > marker.month ||
            (localNow.month() == marker.month && localNow.day() >= marker.day))
            return marker;
    }
    return kSeasonMarkers[sizeof(kSeasonMarkers) / sizeof(kSeasonMarkers[0]) - 1];
}

const SeasonalMarker &nextMarkerForDate(const DateTime &localNow, int &targetYear)
{
    for (size_t i = 0; i < sizeof(kSeasonMarkers) / sizeof(kSeasonMarkers[0]); ++i)
    {
        const SeasonalMarker &marker = kSeasonMarkers[i];
        if (localNow.month() < marker.month ||
            (localNow.month() == marker.month && localNow.day() < marker.day))
        {
            targetYear = localNow.year();
            return marker;
        }
    }

    targetYear = localNow.year() + 1;
    return kSeasonMarkers[0];
}

const char *seasonNameForMarker(const SeasonalMarker &marker, bool southernHemisphere)
{
    return southernHemisphere ? marker.seasonNameSouth : marker.seasonNameNorth;
}

void solarDeclinationAndEqTime(int doy, double localHour, double &declRad, double &eqTimeMin)
{
    const double gamma = (2.0 * kPi / 365.0) * (static_cast<double>(doy) - 1.0 + (localHour - 12.0) / 24.0);
    eqTimeMin = 229.18 * (0.000075 + 0.001868 * cos(gamma) - 0.032077 * sin(gamma) -
                          0.014615 * cos(2.0 * gamma) - 0.040849 * sin(2.0 * gamma));
    declRad = 0.006918 - 0.399912 * cos(gamma) + 0.070257 * sin(gamma) -
              0.006758 * cos(2.0 * gamma) + 0.000907 * sin(2.0 * gamma) -
              0.002697 * cos(3.0 * gamma) + 0.00148 * sin(3.0 * gamma);
}

bool computeSunTimes(const DateTime &localNow, double latDeg, double lonDeg, int &sunriseMinutes, int &sunsetMinutes)
{
    const int doy = dayOfYear(localNow.year(), localNow.month(), localNow.day());
    const double latRad = latDeg * kDegToRad;
    const double zenith = 90.8333 * kDegToRad;
    DateTime noonLocal(localNow.year(), localNow.month(), localNow.day(), 12, 0, 0);
    const int tzOffsetMinutes = timezoneOffsetForLocal(noonLocal);

    auto solveMinutesForHour = [&](double localHour, bool sunrise) -> double
    {
        double declRad = 0.0;
        double eqTimeMin = 0.0;
        solarDeclinationAndEqTime(doy, localHour, declRad, eqTimeMin);

        const double cosH = (cos(zenith) / (cos(latRad) * cos(declRad))) - tan(latRad) * tan(declRad);
        if (cosH < -1.0 || cosH > 1.0)
            return NAN;

        const double hourAngleDeg = acos(cosH) * kRadToDeg;
        const double solarNoonMin = 720.0 - 4.0 * lonDeg - eqTimeMin + static_cast<double>(tzOffsetMinutes);
        return sunrise ? (solarNoonMin - hourAngleDeg * 4.0)
                       : (solarNoonMin + hourAngleDeg * 4.0);
    };

    double sunriseEstimate = solveMinutesForHour(6.0, true);
    double sunsetEstimate = solveMinutesForHour(18.0, false);
    if (!isfinite(sunriseEstimate) || !isfinite(sunsetEstimate))
    {
        sunriseMinutes = -1;
        sunsetMinutes = -1;
        return false;
    }

    const double sunriseRefined = solveMinutesForHour(sunriseEstimate / 60.0, true);
    const double sunsetRefined = solveMinutesForHour(sunsetEstimate / 60.0, false);

    sunriseMinutes = static_cast<int>(round(isfinite(sunriseRefined) ? sunriseRefined : sunriseEstimate));
    sunsetMinutes = static_cast<int>(round(isfinite(sunsetRefined) ? sunsetRefined : sunsetEstimate));

    sunriseMinutes = constrain(sunriseMinutes, 0, 24 * 60 - 1);
    sunsetMinutes = constrain(sunsetMinutes, 0, 24 * 60 - 1);
    return true;
}

bool computeSunHorizontal(const DateTime &localNow, double latDeg, double lonDeg, float &azimuthDegOut, float &altitudeDegOut)
{
    const int doy = dayOfYear(localNow.year(), localNow.month(), localNow.day());
    const double localHour = static_cast<double>(localNow.hour()) +
                             static_cast<double>(localNow.minute()) / 60.0 +
                             static_cast<double>(localNow.second()) / 3600.0;

    double declRad = 0.0;
    double eqTimeMin = 0.0;
    solarDeclinationAndEqTime(doy, localHour, declRad, eqTimeMin);

    DateTime localCopy = localNow;
    const int tzOffsetMinutes = timezoneOffsetForLocal(localCopy);
    const double trueSolarTime = fmod(localHour * 60.0 + eqTimeMin + 4.0 * lonDeg - tzOffsetMinutes + 1440.0, 1440.0);
    double hourAngleDeg = trueSolarTime / 4.0 - 180.0;
    if (hourAngleDeg < -180.0)
        hourAngleDeg += 360.0;

    const double latRad = latDeg * kDegToRad;
    const double haRad = hourAngleDeg * kDegToRad;
    const double altitudeRad = asin(sin(latRad) * sin(declRad) + cos(latRad) * cos(declRad) * cos(haRad));
    double azimuthDeg = atan2(sin(haRad),
                              cos(haRad) * sin(latRad) - tan(declRad) * cos(latRad)) * kRadToDeg + 180.0;
    azimuthDeg = normalizeDegrees(azimuthDeg);
    azimuthDegOut = static_cast<float>(azimuthDeg);
    altitudeDegOut = static_cast<float>(altitudeRad * kRadToDeg);
    return isfinite(azimuthDegOut) && isfinite(altitudeDegOut);
}

bool computeMoonHorizontal(const DateTime &utcNow, double latDeg, double lonDeg, float &azimuthDegOut, float &altitudeDegOut)
{
    const double jd = julianDateFromUnix(static_cast<time_t>(utcNow.unixtime()));
    const double d = jd - 2451543.5;

    const double N = normalizeDegrees(125.1228 - 0.0529538083 * d);
    const double i = 5.1454;
    const double w = normalizeDegrees(318.0634 + 0.1643573223 * d);
    const double a = 60.2666;
    const double e = 0.054900;
    const double M = normalizeDegrees(115.3654 + 13.0649929509 * d);
    double eccentricAnomalyDeg = M + (e * sin(M * kDegToRad) * (1.0 + e * cos(M * kDegToRad))) * kRadToDeg;
    for (uint8_t iter = 0; iter < 5; ++iter)
    {
        const double eccentricAnomalyRad = eccentricAnomalyDeg * kDegToRad;
        eccentricAnomalyDeg -= (eccentricAnomalyDeg - e * sin(eccentricAnomalyRad) * kRadToDeg - M) /
                               (1.0 - e * cos(eccentricAnomalyRad));
    }

    const double eccentricAnomalyRad = eccentricAnomalyDeg * kDegToRad;
    const double xv = a * (cos(eccentricAnomalyRad) - e);
    const double yv = a * (sqrt(1.0 - e * e) * sin(eccentricAnomalyRad));
    const double vDeg = atan2(yv, xv) * kRadToDeg;
    double r = sqrt(xv * xv + yv * yv);

    const double NRad = N * kDegToRad;
    const double vwRad = (vDeg + w) * kDegToRad;
    const double iRad = i * kDegToRad;
    const double xh = r * (cos(NRad) * cos(vwRad) - sin(NRad) * sin(vwRad) * cos(iRad));
    const double yh = r * (sin(NRad) * cos(vwRad) + cos(NRad) * sin(vwRad) * cos(iRad));
    const double zh = r * sin(vwRad) * sin(iRad);

    double moonLonDeg = atan2(yh, xh) * kRadToDeg;
    double moonLatDeg = atan2(zh, sqrt(xh * xh + yh * yh)) * kRadToDeg;

    const double sunPerihelionDeg = 282.9404 + 4.70935E-5 * d;
    const double sunMeanAnomalyDeg = normalizeDegrees(356.0470 + 0.9856002585 * d);
    const double moonMeanLongitudeDeg = normalizeDegrees(M + w + N);
    const double sunMeanLongitudeDeg = normalizeDegrees(sunMeanAnomalyDeg + sunPerihelionDeg);
    const double elongationDeg = normalizeDegrees(moonMeanLongitudeDeg - sunMeanLongitudeDeg);
    const double argumentLatitudeDeg = normalizeDegrees(moonMeanLongitudeDeg - N);

    moonLonDeg += -1.274 * sin((M - 2.0 * elongationDeg) * kDegToRad) +
                  0.658 * sin((2.0 * elongationDeg) * kDegToRad) -
                  0.186 * sin(sunMeanAnomalyDeg * kDegToRad) -
                  0.059 * sin((2.0 * M - 2.0 * elongationDeg) * kDegToRad) -
                  0.057 * sin((M - 2.0 * elongationDeg + sunMeanAnomalyDeg) * kDegToRad) +
                  0.053 * sin((M + 2.0 * elongationDeg) * kDegToRad) +
                  0.046 * sin((2.0 * elongationDeg - sunMeanAnomalyDeg) * kDegToRad) +
                  0.041 * sin((M - sunMeanAnomalyDeg) * kDegToRad) -
                  0.035 * sin(elongationDeg * kDegToRad) -
                  0.031 * sin((M + sunMeanAnomalyDeg) * kDegToRad) -
                  0.015 * sin((2.0 * argumentLatitudeDeg - 2.0 * elongationDeg) * kDegToRad) +
                  0.011 * sin((M - 4.0 * elongationDeg) * kDegToRad);

    moonLatDeg += -0.173 * sin((argumentLatitudeDeg - 2.0 * elongationDeg) * kDegToRad) -
                  0.055 * sin((M - argumentLatitudeDeg - 2.0 * elongationDeg) * kDegToRad) -
                  0.046 * sin((M + argumentLatitudeDeg - 2.0 * elongationDeg) * kDegToRad) +
                  0.033 * sin((argumentLatitudeDeg + 2.0 * elongationDeg) * kDegToRad) +
                  0.017 * sin((2.0 * M + argumentLatitudeDeg) * kDegToRad);

    r += -0.58 * cos((M - 2.0 * elongationDeg) * kDegToRad) -
         0.46 * cos((2.0 * elongationDeg) * kDegToRad);

    const double moonLonRad = moonLonDeg * kDegToRad;
    const double moonLatRad = moonLatDeg * kDegToRad;
    const double ecl = (23.4393 - 3.563E-7 * d) * kDegToRad;
    const double xg = r * cos(moonLonRad) * cos(moonLatRad);
    const double yg = r * sin(moonLonRad) * cos(moonLatRad);
    const double zg = r * sin(moonLatRad);
    const double xe = xg;
    const double ye = yg * cos(ecl) - zg * sin(ecl);
    const double ze = yg * sin(ecl) + zg * cos(ecl);

    const double raDeg = normalizeDegrees(atan2(ye, xe) * kRadToDeg);
    const double decRad = atan2(ze, sqrt(xe * xe + ye * ye));

    const double T = (jd - 2451545.0) / 36525.0;
    double gmstDeg = 280.46061837 + 360.98564736629 * (jd - 2451545.0) +
                     0.000387933 * T * T - (T * T * T) / 38710000.0;
    gmstDeg = normalizeDegrees(gmstDeg);
    const double lstDeg = normalizeDegrees(gmstDeg + lonDeg);
    const double haDeg = normalizeSignedDegrees(lstDeg - raDeg);

    const double latRad = latDeg * kDegToRad;
    const double haRad = haDeg * kDegToRad;
    const double altitudeRad = asin(sin(latRad) * sin(decRad) + cos(latRad) * cos(decRad) * cos(haRad));
    double azimuthDeg = atan2(sin(haRad),
                              cos(haRad) * sin(latRad) - tan(decRad) * cos(latRad)) * kRadToDeg + 180.0;
    azimuthDeg = normalizeDegrees(azimuthDeg);
    azimuthDegOut = static_cast<float>(azimuthDeg);
    altitudeDegOut = static_cast<float>(altitudeRad * kRadToDeg);
    return isfinite(azimuthDegOut) && isfinite(altitudeDegOut);
}

bool computeMoonAltitudeForLocal(const DateTime &localTime, double latDeg, double lonDeg, float &altitudeDegOut)
{
    const DateTime utcTime = localToUtc(localTime, timezoneOffsetForLocal(localTime));
    float azimuthDeg = NAN;
    return computeMoonHorizontal(utcTime, latDeg, lonDeg, azimuthDeg, altitudeDegOut);
}

bool refineMoonCrossing(const DateTime &startLocal, const DateTime &endLocal, double latDeg, double lonDeg, int &minutesOut)
{
    DateTime low = startLocal;
    DateTime high = endLocal;
    float lowAlt = NAN;
    float highAlt = NAN;
    if (!computeMoonAltitudeForLocal(low, latDeg, lonDeg, lowAlt) ||
        !computeMoonAltitudeForLocal(high, latDeg, lonDeg, highAlt))
        return false;

    if ((lowAlt > 0.0f) == (highAlt > 0.0f))
        return false;

    for (uint8_t iter = 0; iter < 16; ++iter)
    {
        const uint32_t midEpoch = low.unixtime() + ((high.unixtime() - low.unixtime()) / 2U);
        const DateTime mid(midEpoch);
        float midAlt = NAN;
        if (!computeMoonAltitudeForLocal(mid, latDeg, lonDeg, midAlt))
            return false;

        if ((midAlt > 0.0f) == (lowAlt > 0.0f))
        {
            low = mid;
            lowAlt = midAlt;
        }
        else
        {
            high = mid;
            highAlt = midAlt;
        }
    }

    minutesOut = high.hour() * 60 + high.minute();
    return true;
}

bool computeMoonTimes(const DateTime &localNow, double latDeg, double lonDeg, int &moonriseMinutes, int &moonsetMinutes)
{
    moonriseMinutes = -1;
    moonsetMinutes = -1;

    const DateTime dayStart(localNow.year(), localNow.month(), localNow.day(), 0, 0, 0);
    float prevAlt = NAN;
    if (!computeMoonAltitudeForLocal(dayStart, latDeg, lonDeg, prevAlt))
        return false;

    bool foundCrossing = false;
    for (int hour = 1; hour <= 24; ++hour)
    {
        const DateTime sample = dayStart + TimeSpan(hour * 3600L);
        float sampleAlt = NAN;
        if (!computeMoonAltitudeForLocal(sample, latDeg, lonDeg, sampleAlt))
            return false;

        const bool prevAbove = prevAlt > 0.0f;
        const bool sampleAbove = sampleAlt > 0.0f;
        if (prevAbove != sampleAbove)
        {
            int crossingMinutes = -1;
            if (!refineMoonCrossing(sample - TimeSpan(3600), sample, latDeg, lonDeg, crossingMinutes))
                return false;

            if (!prevAbove && sampleAbove)
                moonriseMinutes = crossingMinutes;
            else if (prevAbove && !sampleAbove)
                moonsetMinutes = crossingMinutes;

            foundCrossing = true;
        }

        prevAlt = sampleAlt;
    }

    return foundCrossing;
}

float computeMoonPhaseFraction(const DateTime &utcNow)
{
    const double jd = julianDateFromUnix(static_cast<time_t>(utcNow.unixtime()));
    const double d = jd - 2451543.5;

    const double N = normalizeDegrees(125.1228 - 0.0529538083 * d);
    const double i = 5.1454;
    const double w = normalizeDegrees(318.0634 + 0.1643573223 * d);
    const double a = 60.2666;
    const double e = 0.054900;
    const double M = normalizeDegrees(115.3654 + 13.0649929509 * d);

    const double MRad = M * kDegToRad;
    const double ERad = MRad + e * sin(MRad) * (1.0 + e * cos(MRad));
    const double xv = a * (cos(ERad) - e);
    const double yv = a * (sqrt(1.0 - e * e) * sin(ERad));
    const double v = atan2(yv, xv);

    const double NRad = N * kDegToRad;
    const double wRad = w * kDegToRad;
    const double iRad = i * kDegToRad;
    const double vw = v + wRad;

    const double xh = cos(NRad) * cos(vw) - sin(NRad) * sin(vw) * cos(iRad);
    const double yh = sin(NRad) * cos(vw) + cos(NRad) * sin(vw) * cos(iRad);
    const double moonLonDeg = normalizeDegrees(atan2(yh, xh) * kRadToDeg);
    const double sunLonDeg = computeSunEclipticLongitude(jd);
    return static_cast<float>(normalizeDegrees(moonLonDeg - sunLonDeg) / 360.0);
}

MoonPhase phaseFromFraction(float phase)
{
    const float wrapped = phase - floorf(phase);
    const int bucket = static_cast<int>(floorf(wrapped * 8.0f + 0.5f)) & 7;
    return static_cast<MoonPhase>(bucket);
}

const char *kPhaseLabels[] = {
    "New Moon",
    "Waxing Crescent",
    "First Quarter",
    "Waxing Gibbous",
    "Full Moon",
    "Waning Gibbous",
    "Last Quarter",
    "Waning Crescent"};

void clearFactPage(SkyFactPage &page, SkyFactType type)
{
    page = SkyFactPage{};
    page.type = type;
}

void setFactLines(SkyFactPage &page, const char *title, const char *line1, const char *line2 = nullptr, const char *line3 = nullptr)
{
    if (title)
        snprintf(page.title, sizeof(page.title), "%s", title);
    if (line1 && line1[0] != '\0')
    {
        snprintf(page.line1, sizeof(page.line1), "%s", line1);
        page.lineCount = 1;
    }
    if (line2 && line2[0] != '\0')
    {
        snprintf(page.line2, sizeof(page.line2), "%s", line2);
        page.lineCount = 2;
    }
    if (line3 && line3[0] != '\0')
    {
        snprintf(page.line3, sizeof(page.line3), "%s", line3);
        page.lineCount = 3;
    }
    page.valid = page.lineCount > 0;
}

void appendFactPage(const SkyFactPage &page)
{
    if (!page.valid || s_skyFactCount >= kMaxSkyFactPages)
        return;
    s_skyFactPages[s_skyFactCount++] = page;
}

void formatHoursMinutes(int totalMinutes, char *out, size_t outSize)
{
    const int hours = totalMinutes / 60;
    const int minutes = abs(totalMinutes % 60);
    snprintf(out, outSize, "%dh %02dm", hours, minutes);
}

void formatMinutesSeconds(int totalSeconds, char *out, size_t outSize)
{
    const int absSeconds = abs(totalSeconds);
    const int minutes = absSeconds / 60;
    const int seconds = absSeconds % 60;
    if (minutes > 0)
        snprintf(out, outSize, "%dm %02ds", minutes, seconds);
    else
        snprintf(out, outSize, "%ds", seconds);
}

void formatSignedMinutesSeconds(int totalSeconds, char *out, size_t outSize)
{
    if (totalSeconds > 0)
    {
        char value[16];
        formatMinutesSeconds(totalSeconds, value, sizeof(value));
        snprintf(out, outSize, "+%s", value);
    }
    else if (totalSeconds < 0)
    {
        char value[16];
        formatMinutesSeconds(totalSeconds, value, sizeof(value));
        snprintf(out, outSize, "-%s", value);
    }
    else
    {
        snprintf(out, outSize, "0s");
    }
}

void formatRelativeMinutes(int totalMinutes, char *out, size_t outSize)
{
    const int hours = totalMinutes / 60;
    const int minutes = abs(totalMinutes % 60);
    if (hours > 0)
        snprintf(out, outSize, "%dh %02dm", hours, minutes);
    else
        snprintf(out, outSize, "%dm", minutes);
}

void formatClockMinutesCompact(int minutes, char *out, size_t outSize)
{
    if (minutes < 0)
    {
        snprintf(out, outSize, "--");
        return;
    }

    int hour = (minutes / 60) % 24;
    const int minute = minutes % 60;
    if (units.clock24h)
    {
        snprintf(out, outSize, "%02d:%02d", hour, minute);
        return;
    }

    const bool isPm = hour >= 12;
    hour %= 12;
    if (hour == 0)
        hour = 12;
    snprintf(out, outSize, "%d:%02d%c", hour, minute, isPm ? 'P' : 'A');
}

const char *compassLabelForDegrees(float degrees)
{
    if (!isfinite(degrees))
        return "--";
    static const char *dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    const int idx = static_cast<int>(floorf(fmodf(degrees + 22.5f + 360.0f, 360.0f) / 45.0f)) & 7;
    return dirs[idx];
}

void appendSummaryPhrase(char *dest, size_t destSize, const char *phrase)
{
    if (!dest || destSize == 0 || !phrase || phrase[0] == '\0')
        return;

    const size_t currentLen = strlen(dest);
    if (currentLen > 0)
        snprintf(dest + currentLen, destSize - currentLen, " * ");

    const size_t updatedLen = strlen(dest);
    if (updatedLen < destSize - 1)
        snprintf(dest + updatedLen, destSize - updatedLen, "%s", phrase);
}

const char *shortMoonPhaseLabel(MoonPhase phase)
{
    switch (phase)
    {
    case MoonPhase::NewMoon:
        return "New";
    case MoonPhase::WaxingCrescent:
        return "Wax Cres";
    case MoonPhase::FirstQuarter:
        return "1st Qtr";
    case MoonPhase::WaxingGibbous:
        return "Wax Gib";
    case MoonPhase::FullMoon:
        return "Full";
    case MoonPhase::WaningGibbous:
        return "Wan Gib";
    case MoonPhase::LastQuarter:
        return "Last Qtr";
    default:
        return "Wan Cres";
    }
}

void buildSeasonFact(const DateTime &localNow, const DateTime &utcNow, bool southernHemisphere)
{
    SkyFactPage page;
    clearFactPage(page, SkyFactType::Season);

    SeasonEventUtc currentEvent{};
    SeasonEventUtc nextEvent{};
    if (!seasonBoundsForUtc(utcNow, currentEvent, nextEvent))
        return;

    const SeasonalMarker &currentMarker = kSeasonMarkers[currentEvent.markerIndex & 3];
    const SeasonalMarker &nextMarker = kSeasonMarkers[nextEvent.markerIndex & 3];
    const DateTime dayStart(localNow.year(), localNow.month(), localNow.day(), 0, 0, 0);
    const DateTime nextUtc(static_cast<uint32_t>(nextEvent.epoch));
    const int nextOffsetMinutes = timezoneOffsetForUtc(nextUtc);
    const DateTime nextLocal = utcToLocal(nextUtc, nextOffsetMinutes);
    const DateTime nextLocalDay(nextLocal.year(), nextLocal.month(), nextLocal.day(), 0, 0, 0);
    const int daysUntil = daysBetweenDates(dayStart, nextLocalDay);
    char line2[24];
    snprintf(line2, sizeof(line2), "%s in %dd", seasonNameForMarker(nextMarker, southernHemisphere), daysUntil);
    setFactLines(page, "SEASON", seasonNameForMarker(currentMarker, southernHemisphere), line2);

    page.meterTotal = 255;
    const time_t nowEpoch = static_cast<time_t>(utcNow.unixtime());
    const time_t seasonDuration = nextEvent.epoch - currentEvent.epoch;
    const time_t seasonElapsed = nowEpoch - currentEvent.epoch;
    if (seasonDuration > 0)
    {
        const double progress = constrain(static_cast<double>(seasonElapsed) / static_cast<double>(seasonDuration), 0.0, 1.0);
        page.meterFill = static_cast<uint8_t>(lround(progress * static_cast<double>(page.meterTotal)));
    }
    appendFactPage(page);
}

void buildEquinoxFact(const DateTime &localNow)
{
    SkyFactPage page;
    clearFactPage(page, SkyFactType::EquinoxSolstice);

    int nextYear = localNow.year();
    const SeasonalMarker &nextMarker = nextMarkerForDate(localNow, nextYear);
    const DateTime dayStart(localNow.year(), localNow.month(), localNow.day(), 0, 0, 0);
    const DateTime nextDate(nextYear, nextMarker.month, nextMarker.day, 0, 0, 0);
    const int daysUntil = daysBetweenDates(dayStart, nextDate);

    const char *eventShort = strstr(nextMarker.eventName, "Equinox") ? "Equinox" : "Solstice";
    char line2[24];
    snprintf(line2, sizeof(line2), "in %dd", daysUntil);
    setFactLines(page, "NEXT SKY", eventShort, line2);
    appendFactPage(page);
}

void buildDaylightFact(const DateTime &localNow, double lat, double lon, bool hasSunTimes)
{
    if (!hasSunTimes || s_data.sunriseMinutes < 0 || s_data.sunsetMinutes < 0)
        return;

    SkyFactPage page;
    clearFactPage(page, SkyFactType::Daylight);

    const int daylightMinutes = s_data.sunsetMinutes - s_data.sunriseMinutes;
    char line1[24];
    char line2[24];
    formatHoursMinutes(daylightMinutes, line1, sizeof(line1));

    int yRise = -1;
    int ySet = -1;
    const DateTime yesterday = addDays(localNow, -1);
    if (computeSunTimes(yesterday, lat, lon, yRise, ySet) && yRise >= 0 && ySet >= 0)
    {
        const int deltaSeconds = (daylightMinutes - (ySet - yRise)) * 60;
        formatSignedMinutesSeconds(deltaSeconds, line2, sizeof(line2));
        page.trend = (deltaSeconds > 0) ? 1 : (deltaSeconds < 0 ? -1 : 0);
    }
    else
    {
        line2[0] = '\0';
        page.trend = 0;
    }

    setFactLines(page, "DAY LIGHT", line1, line2);
    appendFactPage(page);
}

void buildSunCountdownFact()
{
    if (!s_data.hasSunTimes || s_data.sunriseMinutes < 0 || s_data.sunsetMinutes < 0 || s_data.localMinutes < 0)
        return;

    SkyFactPage page;
    clearFactPage(page, SkyFactType::SunCountdown);

    char line2[24];
    if (s_data.localMinutes < s_data.sunriseMinutes)
    {
        formatRelativeMinutes(s_data.sunriseMinutes - s_data.localMinutes, line2, sizeof(line2));
        setFactLines(page, "SUN", "Rise in", line2);
        page.trend = 1;
    }
    else if ((s_data.localMinutes - s_data.sunriseMinutes) <= 90)
    {
        formatRelativeMinutes(s_data.localMinutes - s_data.sunriseMinutes, line2, sizeof(line2));
        char line1[24];
        snprintf(line1, sizeof(line1), "Rose");
        char line2Ago[24];
        snprintf(line2Ago, sizeof(line2Ago), "%s ago", line2);
        setFactLines(page, "SUN", line1, line2Ago);
        page.trend = 1;
    }
    else if (s_data.localMinutes < s_data.sunsetMinutes)
    {
        formatRelativeMinutes(s_data.sunsetMinutes - s_data.localMinutes, line2, sizeof(line2));
        setFactLines(page, "SUN", "Set in", line2);
        page.trend = -1;
    }
    else
    {
        formatRelativeMinutes(s_data.localMinutes - s_data.sunsetMinutes, line2, sizeof(line2));
        char line2Ago[24];
        snprintf(line2Ago, sizeof(line2Ago), "%s ago", line2);
        setFactLines(page, "SUN", "Set", line2Ago);
        page.trend = -1;
    }
    appendFactPage(page);
}

void buildMoonFact()
{
    SkyFactPage page;
    clearFactPage(page, SkyFactType::Moon);

    const float phase = s_data.moonPhaseFraction;
    const int daysToFull = static_cast<int>(roundf((((phase <= 0.5f) ? (0.5f - phase) : (1.5f - phase)) * static_cast<float>(kMoonSynodicDays))));
    const int daysToNew = static_cast<int>(roundf((((phase <= 0.01f) ? 0.0f : (1.0f - phase)) * static_cast<float>(kMoonSynodicDays))));
    char line2[24];
    if (daysToFull <= daysToNew && daysToFull <= 7)
        snprintf(line2, sizeof(line2), "Full in %dd", daysToFull);
    else if (daysToNew < daysToFull && daysToNew <= 7)
        snprintf(line2, sizeof(line2), "New in %dd", daysToNew);
    else
        snprintf(line2, sizeof(line2), "Lit %u%%", static_cast<unsigned>(s_data.moonIlluminationPct));

    setFactLines(page, "MOON", shortMoonPhaseLabel(s_data.moonPhase), line2);
    appendFactPage(page);
}

void buildSummaryFact(const DateTime &localNow, const DateTime &utcNow, bool southernHemisphere)
{
    SkyFactPage page;
    clearFactPage(page, SkyFactType::Summary);

    appendSummaryPhrase(page.marquee, sizeof(page.marquee), "NOW");

    SeasonEventUtc currentEvent{};
    SeasonEventUtc nextEvent{};
    const bool hasSeasonBounds = seasonBoundsForUtc(utcNow, currentEvent, nextEvent);

    char phrase[48];
    phrase[0] = '\0';

    if (hasSeasonBounds)
    {
        const char *seasonName = seasonNameForMarker(kSeasonMarkers[currentEvent.markerIndex & 3], southernHemisphere);
        snprintf(phrase, sizeof(phrase), "%s is here", seasonName);
        appendSummaryPhrase(page.marquee, sizeof(page.marquee), phrase);

        const DateTime dayStart(localNow.year(), localNow.month(), localNow.day(), 0, 0, 0);
        const DateTime nextUtc(static_cast<uint32_t>(nextEvent.epoch));
        const int nextOffsetMinutes = timezoneOffsetForUtc(nextUtc);
        const DateTime nextLocal = utcToLocal(nextUtc, nextOffsetMinutes);
        const DateTime nextLocalDay(nextLocal.year(), nextLocal.month(), nextLocal.day(), 0, 0, 0);
        const int daysUntilSeason = daysBetweenDates(dayStart, nextLocalDay);
        const char *nextSeason = seasonNameForMarker(kSeasonMarkers[nextEvent.markerIndex & 3], southernHemisphere);
        snprintf(phrase, sizeof(phrase), "%s in %dd", nextSeason, daysUntilSeason);
        appendSummaryPhrase(page.marquee, sizeof(page.marquee), phrase);
    }

    if (s_data.hasSunTimes && s_data.sunriseMinutes >= 0 && s_data.sunsetMinutes >= 0)
    {
        const int daylightMinutes = s_data.sunsetMinutes - s_data.sunriseMinutes;
        char daylightText[20];
        formatHoursMinutes(daylightMinutes, daylightText, sizeof(daylightText));
        snprintf(phrase, sizeof(phrase), "Daylight %s", daylightText);
        appendSummaryPhrase(page.marquee, sizeof(page.marquee), phrase);

        char sunPhrase[24];
        if (s_data.localMinutes < s_data.sunriseMinutes)
        {
            char relative[16];
            formatRelativeMinutes(s_data.sunriseMinutes - s_data.localMinutes, relative, sizeof(relative));
            snprintf(sunPhrase, sizeof(sunPhrase), "Sunrise in %s", relative);
        }
        else if (s_data.localMinutes < s_data.sunsetMinutes)
        {
            char relative[16];
            formatRelativeMinutes(s_data.sunsetMinutes - s_data.localMinutes, relative, sizeof(relative));
            snprintf(sunPhrase, sizeof(sunPhrase), "Sunset in %s", relative);
        }
        else
        {
            char relative[16];
            formatRelativeMinutes(s_data.localMinutes - s_data.sunsetMinutes, relative, sizeof(relative));
            snprintf(sunPhrase, sizeof(sunPhrase), "Sun set %s ago", relative);
        }
        appendSummaryPhrase(page.marquee, sizeof(page.marquee), sunPhrase);

        char riseText[12];
        char setText[12];
        formatClockMinutesCompact(s_data.sunriseMinutes, riseText, sizeof(riseText));
        formatClockMinutesCompact(s_data.sunsetMinutes, setText, sizeof(setText));
        snprintf(phrase, sizeof(phrase), "Sun %s-%s", riseText, setText);
        appendSummaryPhrase(page.marquee, sizeof(page.marquee), phrase);
    }

    if (s_data.hasSunAzimuth && s_data.hasSunAltitude)
    {
        snprintf(phrase, sizeof(phrase), "Sun %s %+d\xC2\xB0",
                 compassLabelForDegrees(s_data.sunAzimuthDeg),
                 static_cast<int>(roundf(s_data.sunAltitudeDeg)));
        appendSummaryPhrase(page.marquee, sizeof(page.marquee), phrase);
    }

    appendSummaryPhrase(page.marquee, sizeof(page.marquee), moonPhaseLabel(s_data.moonPhase));

    const float phase = s_data.moonPhaseFraction;
    const int daysToFull = static_cast<int>(roundf((((phase <= 0.5f) ? (0.5f - phase) : (1.5f - phase)) * static_cast<float>(kMoonSynodicDays))));
    const int daysToNew = static_cast<int>(roundf((((phase <= 0.01f) ? 0.0f : (1.0f - phase)) * static_cast<float>(kMoonSynodicDays))));
    if (daysToFull <= daysToNew && daysToFull <= 7)
        snprintf(phrase, sizeof(phrase), "Full Moon in %dd", daysToFull);
    else if (daysToNew < daysToFull && daysToNew <= 10)
        snprintf(phrase, sizeof(phrase), "New Moon in %dd", daysToNew);
    else
        snprintf(phrase, sizeof(phrase), "Moon lit %u%%", static_cast<unsigned>(s_data.moonIlluminationPct));
    appendSummaryPhrase(page.marquee, sizeof(page.marquee), phrase);

    if (s_data.hasMoonAltitude && s_data.hasMoonAzimuth)
    {
        snprintf(phrase, sizeof(phrase), "Moon %s %+d\xC2\xB0",
                 compassLabelForDegrees(s_data.moonAzimuthDeg),
                 static_cast<int>(roundf(s_data.moonAltitudeDeg)));
        appendSummaryPhrase(page.marquee, sizeof(page.marquee), phrase);

        snprintf(phrase, sizeof(phrase), "Moon %s",
                 (s_data.moonAltitudeDeg > 0.0f) ? "above horizon" : "below horizon");
        appendSummaryPhrase(page.marquee, sizeof(page.marquee), phrase);
    }

    if (s_data.hasMoonTimes && (s_data.moonriseMinutes >= 0 || s_data.moonsetMinutes >= 0))
    {
        char riseText[12];
        char setText[12];
        formatClockMinutesCompact(s_data.moonriseMinutes, riseText, sizeof(riseText));
        formatClockMinutesCompact(s_data.moonsetMinutes, setText, sizeof(setText));
        snprintf(phrase, sizeof(phrase), "Moonrise %s", riseText);
        appendSummaryPhrase(page.marquee, sizeof(page.marquee), phrase);
        snprintf(phrase, sizeof(phrase), "Moonset %s", setText);
        appendSummaryPhrase(page.marquee, sizeof(page.marquee), phrase);

        if (s_data.localMinutes >= 0)
        {
            if (s_data.moonAltitudeDeg <= 0.0f && s_data.moonriseMinutes >= 0)
            {
                int delta = s_data.moonriseMinutes - s_data.localMinutes;
                if (delta < 0 && s_data.moonsetMinutes >= 0 && s_data.moonriseMinutes > s_data.moonsetMinutes)
                    delta += 24 * 60;
                if (delta >= 0)
                {
                    char relative[16];
                    formatRelativeMinutes(delta, relative, sizeof(relative));
                    snprintf(phrase, sizeof(phrase), "Moonrise in %s", relative);
                    appendSummaryPhrase(page.marquee, sizeof(page.marquee), phrase);
                }
            }
            else if (s_data.moonAltitudeDeg > 0.0f && s_data.moonsetMinutes >= 0)
            {
                int delta = s_data.moonsetMinutes - s_data.localMinutes;
                if (delta < 0 && s_data.moonriseMinutes >= 0 && s_data.moonriseMinutes > s_data.moonsetMinutes)
                    delta += 24 * 60;
                if (delta >= 0)
                {
                    char relative[16];
                    formatRelativeMinutes(delta, relative, sizeof(relative));
                    snprintf(phrase, sizeof(phrase), "Moonset in %s", relative);
                    appendSummaryPhrase(page.marquee, sizeof(page.marquee), phrase);
                }
            }
        }
    }

    const int doy = dayOfYear(localNow.year(), localNow.month(), localNow.day());
    const int totalDays = daysInYear(localNow.year());
    const int daysLeft = totalDays - doy;
    snprintf(phrase, sizeof(phrase), "Day %d of %d", doy, totalDays);
    appendSummaryPhrase(page.marquee, sizeof(page.marquee), phrase);
    snprintf(phrase, sizeof(phrase), "%d days left", daysLeft);
    appendSummaryPhrase(page.marquee, sizeof(page.marquee), phrase);

    snprintf(page.title, sizeof(page.title), "%s", "SKY BRIEF");
    page.valid = page.marquee[0] != '\0';
    page.lineCount = page.valid ? 1 : 0;
    s_summaryPage = page;
}

void buildYearFact(const DateTime &localNow)
{
    SkyFactPage page;
    clearFactPage(page, SkyFactType::YearProgress);

    const int doy = dayOfYear(localNow.year(), localNow.month(), localNow.day());
    const int totalDays = daysInYear(localNow.year());
    const int daysLeft = totalDays - doy;

    char line1[24];
    char line2[24];
    snprintf(line1, sizeof(line1), "%d / %d", doy, totalDays);
    snprintf(line2, sizeof(line2), "%dd left", daysLeft);
    setFactLines(page, "YEAR", line1, line2);
    appendFactPage(page);
}
} // namespace

void updateAstronomyData(bool force)
{
    DateTime localNow;
    DateTime utcNow;
    if (!currentTimes(localNow, utcNow))
        return;

    double lat = NAN;
    double lon = NAN;
    const bool hasLocation = resolveCoordinates(lat, lon);
    const int dateKey = localNow.year() * 10000 + localNow.month() * 100 + localNow.day();
    const int minuteBucket = localNow.hour() * 60 + localNow.minute();

    if (!force &&
        s_data.hasLocation == hasLocation &&
        s_data.localDateKey == dateKey &&
        s_data.minuteBucket == minuteBucket &&
        (!hasLocation || (fabs(s_data.latitude - lat) < 0.001 && fabs(s_data.longitude - lon) < 0.001)))
    {
        return;
    }

    s_data = AstronomyData{};
    s_data.hasLocation = hasLocation;
    s_data.latitude = lat;
    s_data.longitude = lon;
    s_data.localDateKey = dateKey;
    s_data.minuteBucket = minuteBucket;
    s_data.localMinutes = localNow.hour() * 60 + localNow.minute();
    s_data.lastRefreshMs = millis();

    s_data.moonPhaseFraction = computeMoonPhaseFraction(utcNow);
    s_data.moonPhase = phaseFromFraction(s_data.moonPhaseFraction);
    s_data.moonIlluminationPct = static_cast<uint8_t>(roundf((1.0f - cosf(2.0f * static_cast<float>(kPi) * s_data.moonPhaseFraction)) * 50.0f));

    if (!hasLocation)
        return;

    s_data.hasSunTimes = computeSunTimes(localNow, lat, lon, s_data.sunriseMinutes, s_data.sunsetMinutes);
    s_data.hasSunAzimuth = computeSunHorizontal(localNow, lat, lon, s_data.sunAzimuthDeg, s_data.sunAltitudeDeg);
    s_data.hasSunAltitude = s_data.hasSunAzimuth;
    s_data.hasMoonTimes = computeMoonTimes(localNow, lat, lon, s_data.moonriseMinutes, s_data.moonsetMinutes);
    s_data.hasMoonAzimuth = computeMoonHorizontal(utcNow, lat, lon, s_data.moonAzimuthDeg, s_data.moonAltitudeDeg);
    s_data.hasMoonAltitude = s_data.hasMoonAzimuth;
}

const AstronomyData &astronomyData()
{
    return s_data;
}

const char *moonPhaseLabel(MoonPhase phase)
{
    return kPhaseLabels[static_cast<uint8_t>(phase) & 7];
}

void updateSkyFacts(bool force)
{
    updateAstronomyData(force);

    DateTime localNow;
    DateTime utcNow;
    if (!currentTimes(localNow, utcNow))
    {
        s_skyFactCount = 0;
        return;
    }

    const bool sameLocation = (!s_data.hasLocation && !isfinite(s_skyFactsLat) && !isfinite(s_skyFactsLon)) ||
                              (s_data.hasLocation &&
                               fabs(s_skyFactsLat - s_data.latitude) < 0.001 &&
                               fabs(s_skyFactsLon - s_data.longitude) < 0.001);
    if (!force &&
        s_skyFactsDateKey == s_data.localDateKey &&
        s_skyFactsMinute == s_data.localMinutes &&
        sameLocation)
    {
        return;
    }

    s_skyFactCount = 0;
    for (size_t i = 0; i < kMaxSkyFactPages; ++i)
        s_skyFactPages[i] = SkyFactPage{};
    s_summaryPage = SkyFactPage{};
    const bool southernHemisphere = s_data.hasLocation && s_data.latitude < 0.0;
    buildSeasonFact(localNow, utcNow, southernHemisphere);
    buildEquinoxFact(localNow);
    if (s_data.hasLocation)
        buildDaylightFact(localNow, s_data.latitude, s_data.longitude, s_data.hasSunTimes);
    buildSunCountdownFact();
    buildSummaryFact(localNow, utcNow, southernHemisphere);
    buildYearFact(localNow);

    s_skyFactsDateKey = s_data.localDateKey;
    s_skyFactsMinute = s_data.localMinutes;
    s_skyFactsLat = s_data.latitude;
    s_skyFactsLon = s_data.longitude;
}

size_t skyFactCount()
{
    return s_skyFactCount;
}

const SkyFactPage &skyFactPage(size_t index)
{
    static SkyFactPage empty;
    if (index >= s_skyFactCount)
        return empty;
    return s_skyFactPages[index];
}

const SkyFactPage &skySummaryPage()
{
    return s_summaryPage;
}

} // namespace wxv::astronomy
