#include "display.h"
#include "settings.h"

bool screenIsAllowed(ScreenMode mode)
{
    switch (mode)
    {
    case SCREEN_OWM:
        return isDataSourceOwm() || isDataSourceForecastModel();
    case SCREEN_UDP_DATA:
        return isDataSourceWeatherFlow();
    case SCREEN_LIGHTNING:
        return isDataSourceWeatherFlow();
    case SCREEN_UDP_FORECAST:
        return isDataSourceForecastModel() || isDataSourceWeatherFlow() || isDataSourceOwm();
    case SCREEN_WIND_DIR:
        return isDataSourceWeatherFlow();
    case SCREEN_HOURLY:
        return isDataSourceForecastModel() || isDataSourceWeatherFlow() || isDataSourceOwm();
    case SCREEN_CURRENT:
        return isDataSourceForecastModel() || isDataSourceOwm() || isDataSourceWeatherFlow();
    case SCREEN_CONDITION_SCENE:
        return !isDataSourceNone();
    case SCREEN_ASTRONOMY:
        return WXV_ENABLE_ASTRONOMY;
    case SCREEN_SKY_BRIEF:
        return WXV_ENABLE_SKY_BRIEF;
    case SCREEN_PREDICT:
        return WXV_ENABLE_NEXT24H_PREDICTION;
    case SCREEN_NOAA_ALERT:
        return noaaAlertsEnabled;
    case SCREEN_LUNAR_LUCK:
        return WXV_ENABLE_LUNAR_CALENDAR && WXV_ENABLE_LUNAR_LUCK;
    default:
        return true;
    }
}

ScreenMode nextAllowedScreen(ScreenMode start, int direction)
{
    if (direction == 0)
        direction = 1;

    int startIdx = -1;
    for (int i = 0; i < NUM_INFOSCREENS; ++i)
    {
        if (InfoScreenModes[i] == start)
        {
            startIdx = i;
            break;
        }
    }
    if (startIdx < 0)
        startIdx = 0;

    int idx = startIdx;
    for (int steps = 0; steps < NUM_INFOSCREENS; ++steps)
    {
        idx = (idx + direction + NUM_INFOSCREENS) % NUM_INFOSCREENS;
        ScreenMode candidate = InfoScreenModes[idx];
        if (screenIsAllowed(candidate))
            return candidate;
    }
    return SCREEN_CLOCK;
}

ScreenMode enforceAllowedScreen(ScreenMode desired)
{
    if (screenIsAllowed(desired))
        return desired;

    ScreenMode candidate = nextAllowedScreen(desired, +1);
    if (screenIsAllowed(candidate))
        return candidate;

    candidate = nextAllowedScreen(desired, -1);
    if (screenIsAllowed(candidate))
        return candidate;

    return SCREEN_CLOCK;
}

ScreenMode homeScreenForDataSource()
{
    if (isDataSourceOwm())
        return SCREEN_OWM;
    return SCREEN_CLOCK;
}
