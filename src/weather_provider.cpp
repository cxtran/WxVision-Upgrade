#include "weather_provider.h"

#include <cstdlib>
#include "settings.h"
#include "display.h"
#include "display_runtime.h"
#include "notifications.h"

namespace
{
using wxv::provider::CurrentWeatherSnapshot;
using wxv::provider::IWeatherProvider;
using wxv::provider::WeatherProviderCapabilities;
using wxv::provider::WeatherProviderId;
using wxv::provider::WeatherSnapshot;

static float toFloatOrNan(const String &value)
{
    if (value.length() == 0 || value == "--")
        return NAN;
    return static_cast<float>(atof(value.c_str()));
}

static int toIntOrNegOne(const String &value)
{
    if (value.length() == 0 || value == "--")
        return -1;
    return value.toInt();
}

class NoneProvider final : public IWeatherProvider
{
public:
    WeatherProviderId id() const override { return WeatherProviderId::None; }
    const char *name() const override { return "none"; }
    WeatherProviderCapabilities capabilities() const override { return {}; }
    bool fetch() override { return false; }
    bool snapshot(WeatherSnapshot &out) const override
    {
        out = WeatherSnapshot{};
        out.provider = id();
        return true;
    }
};

class OwmProvider final : public IWeatherProvider
{
public:
    WeatherProviderId id() const override { return WeatherProviderId::Owm; }
    const char *name() const override { return "openweather"; }
    WeatherProviderCapabilities capabilities() const override
    {
        WeatherProviderCapabilities caps;
        caps.hasCurrent = true;
        caps.hasForecast = true;
        caps.usesCloudFetch = true;
        return caps;
    }
    bool fetch() override
    {
        fetchWeatherFromOWM();
        return true;
    }
    bool snapshot(WeatherSnapshot &out) const override
    {
        out = WeatherSnapshot{};
        out.provider = id();
        CurrentWeatherSnapshot cur;
        cur.tempC = isnan(currentCond.temp) ? toFloatOrNan(str_Temp) : static_cast<float>(currentCond.temp);
        cur.feelsLikeC = isnan(currentCond.feelsLike) ? toFloatOrNan(str_Feels_like) : static_cast<float>(currentCond.feelsLike);
        cur.humidityPct = (currentCond.humidity >= 0) ? currentCond.humidity : toIntOrNegOne(str_Humd);
        cur.pressureHpa = isnan(currentCond.pressure) ? toFloatOrNan(str_Pressure) : static_cast<float>(currentCond.pressure);
        cur.windSpeedMps = isnan(currentCond.windAvg) ? toFloatOrNan(str_Wind_Speed) : static_cast<float>(currentCond.windAvg);
        cur.condition = (currentCond.cond.length() > 0) ? currentCond.cond : str_Weather_Conditions;
        cur.icon = currentCond.icon;
        cur.updatedMs = forecast.lastUpdate;
        out.current = cur;
        out.hasCurrent = (!isnan(cur.tempC) || cur.condition.length() > 0);

        out.dailyCount = forecast.numDays;
        if (out.dailyCount < 0)
            out.dailyCount = 0;
        if (out.dailyCount > MAX_FORECAST_DAYS)
            out.dailyCount = MAX_FORECAST_DAYS;
        for (int i = 0; i < out.dailyCount; ++i)
            out.daily[i] = forecast.days[i];

        out.hourlyCount = forecast.numHours;
        if (out.hourlyCount < 0)
            out.hourlyCount = 0;
        if (out.hourlyCount > MAX_FORECAST_HOURS)
            out.hourlyCount = MAX_FORECAST_HOURS;
        for (int i = 0; i < out.hourlyCount; ++i)
            out.hourly[i] = forecast.hours[i];

        out.updatedMs = cur.updatedMs;
        return true;
    }
};

class ForecastModelProviderBase : public IWeatherProvider
{
public:
    bool fetch() override
    {
        fetchForecastData();
        return true;
    }
    bool snapshot(WeatherSnapshot &out) const override
    {
        out = WeatherSnapshot{};
        out.provider = id();

        CurrentWeatherSnapshot cur;
        cur.tempC = isnan(currentCond.temp) ? static_cast<float>(tempest.temperature) : static_cast<float>(currentCond.temp);
        cur.feelsLikeC = isnan(currentCond.feelsLike) ? cur.tempC : static_cast<float>(currentCond.feelsLike);
        cur.humidityPct = (currentCond.humidity >= 0) ? currentCond.humidity : static_cast<int>(roundf(static_cast<float>(tempest.humidity)));
        cur.pressureHpa = isnan(currentCond.pressure) ? static_cast<float>(tempest.pressure) : static_cast<float>(currentCond.pressure);
        cur.windSpeedMps = isnan(currentCond.windAvg) ? static_cast<float>(tempest.windAvg) : static_cast<float>(currentCond.windAvg);
        cur.condition = (currentCond.cond.length() > 0) ? currentCond.cond : str_Weather_Conditions;
        cur.icon = currentCond.icon;
        cur.updatedMs = forecast.lastUpdate;
        out.current = cur;
        out.hasCurrent = (!isnan(cur.tempC) || cur.condition.length() > 0);

        out.dailyCount = forecast.numDays;
        if (out.dailyCount < 0)
            out.dailyCount = 0;
        if (out.dailyCount > MAX_FORECAST_DAYS)
            out.dailyCount = MAX_FORECAST_DAYS;
        for (int i = 0; i < out.dailyCount; ++i)
            out.daily[i] = forecast.days[i];

        out.hourlyCount = forecast.numHours;
        if (out.hourlyCount < 0)
            out.hourlyCount = 0;
        if (out.hourlyCount > MAX_FORECAST_HOURS)
            out.hourlyCount = MAX_FORECAST_HOURS;
        for (int i = 0; i < out.hourlyCount; ++i)
            out.hourly[i] = forecast.hours[i];

        out.updatedMs = forecast.lastUpdate;
        return true;
    }
};

class WeatherFlowProvider final : public ForecastModelProviderBase
{
public:
    WeatherProviderId id() const override { return WeatherProviderId::WeatherFlow; }
    const char *name() const override { return "weatherflow"; }
    WeatherProviderCapabilities capabilities() const override
    {
        WeatherProviderCapabilities caps;
        caps.hasCurrent = true;
        caps.hasForecast = true;
        caps.usesUdpMulticast = true;
        caps.usesCloudFetch = true;
        return caps;
    }
};

class OpenMeteoProvider final : public ForecastModelProviderBase
{
public:
    WeatherProviderId id() const override { return WeatherProviderId::OpenMeteo; }
    const char *name() const override { return "open-meteo"; }
    WeatherProviderCapabilities capabilities() const override
    {
        WeatherProviderCapabilities caps;
        caps.hasCurrent = true;
        caps.hasForecast = true;
        caps.usesUdpMulticast = false;
        caps.usesCloudFetch = true;
        return caps;
    }
};

NoneProvider g_noneProvider;
OwmProvider g_owmProvider;
WeatherFlowProvider g_weatherFlowProvider;
OpenMeteoProvider g_openMeteoProvider;

} // namespace

namespace wxv::provider
{
WeatherProviderId providerIdFromDataSource(int source)
{
    switch (source)
    {
    case DATA_SOURCE_OWM:
        return WeatherProviderId::Owm;
    case DATA_SOURCE_WEATHERFLOW:
        return WeatherProviderId::WeatherFlow;
    case DATA_SOURCE_OPEN_METEO:
        return WeatherProviderId::OpenMeteo;
    case DATA_SOURCE_NONE:
    default:
        return WeatherProviderId::None;
    }
}

const char *providerNameFromDataSource(int source)
{
    return providerForDataSource(source).name();
}

bool sourceUsesUdpMulticast(int source)
{
    return providerForDataSource(source).capabilities().usesUdpMulticast;
}

bool sourceIsForecastModel(int source)
{
    return providerForDataSource(source).capabilities().hasForecast;
}

IWeatherProvider &providerForDataSource(int source)
{
    switch (providerIdFromDataSource(source))
    {
    case WeatherProviderId::Owm:
        return g_owmProvider;
    case WeatherProviderId::WeatherFlow:
        return g_weatherFlowProvider;
    case WeatherProviderId::OpenMeteo:
        return g_openMeteoProvider;
    case WeatherProviderId::None:
    default:
        return g_noneProvider;
    }
}

IWeatherProvider &activeProvider()
{
    return providerForDataSource(dataSource);
}

bool fetchActiveProviderData()
{
    const auto caps = activeProvider().capabilities();
    if (caps.usesCloudFetch && dma_display != nullptr && !isSplashActive())
    {
        wxv::notify::showNotification(wxv::notify::NotifyId::Busy, myCYAN, myWHITE, "UPDATING");
    }
    return activeProvider().fetch();
}

bool fetchProviderData(int source)
{
    const auto caps = providerForDataSource(source).capabilities();
    if (caps.usesCloudFetch && dma_display != nullptr && !isSplashActive())
    {
        wxv::notify::showNotification(wxv::notify::NotifyId::Busy, myCYAN, myWHITE, "UPDATING");
    }
    return providerForDataSource(source).fetch();
}

bool readActiveProviderSnapshot(WeatherSnapshot &out)
{
    return activeProvider().snapshot(out);
}

bool readProviderSnapshot(int source, WeatherSnapshot &out)
{
    return providerForDataSource(source).snapshot(out);
}

} // namespace wxv::provider
