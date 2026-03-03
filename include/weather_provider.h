#pragma once

#include <Arduino.h>
#include "tempest.h"

namespace wxv::provider
{
enum class WeatherProviderId : uint8_t
{
    None = 0,
    Owm,
    WeatherFlow,
    OpenMeteo
};

struct WeatherProviderCapabilities
{
    bool hasCurrent = false;
    bool hasForecast = false;
    bool usesUdpMulticast = false;
    bool usesCloudFetch = false;
};

struct CurrentWeatherSnapshot
{
    float tempC = NAN;
    float feelsLikeC = NAN;
    int humidityPct = -1;
    float pressureHpa = NAN;
    float windSpeedMps = NAN;
    String condition;
    String icon;
    uint32_t updatedMs = 0;
};

struct WeatherSnapshot
{
    WeatherProviderId provider = WeatherProviderId::None;
    CurrentWeatherSnapshot current;
    bool hasCurrent = false;

    ForecastDay daily[MAX_FORECAST_DAYS];
    int dailyCount = 0;
    ForecastHour hourly[MAX_FORECAST_HOURS];
    int hourlyCount = 0;
    uint32_t updatedMs = 0;
};

class IWeatherProvider
{
public:
    virtual ~IWeatherProvider() = default;
    virtual WeatherProviderId id() const = 0;
    virtual const char *name() const = 0;
    virtual WeatherProviderCapabilities capabilities() const = 0;
    virtual bool fetch() = 0;
    virtual bool snapshot(WeatherSnapshot &out) const = 0;
};

WeatherProviderId providerIdFromDataSource(int dataSource);
const char *providerNameFromDataSource(int dataSource);
bool sourceUsesUdpMulticast(int dataSource);
bool sourceIsForecastModel(int dataSource);

IWeatherProvider &providerForDataSource(int dataSource);
IWeatherProvider &activeProvider();
bool fetchActiveProviderData();
bool fetchProviderData(int dataSource);
bool readActiveProviderSnapshot(WeatherSnapshot &out);
bool readProviderSnapshot(int dataSource, WeatherSnapshot &out);

} // namespace wxv::provider
