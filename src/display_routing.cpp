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
    case SCREEN_UDP_FORECAST:
    case SCREEN_WIND_DIR:
    case SCREEN_HOURLY:
        return isDataSourceForecastModel();
    case SCREEN_CURRENT:
        return isDataSourceForecastModel() || isDataSourceOwm();
    case SCREEN_CONDITION_SCENE:
        return !isDataSourceNone();
    case SCREEN_NOAA_ALERT:
        return noaaAlertsEnabled;
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
