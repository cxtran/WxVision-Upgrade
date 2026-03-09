#include "screen_manager.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include "display.h"
#include "graph.h"
#include "menu.h"
#include "settings.h"
#include "sensors.h"
#include "tempest.h"
#include "worldtime.h"
#include "env_quality.h"
#include "WorldClockScreen.h"
#include "system.h"
#include "weather_provider.h"
#include "noaa.h"

extern ScreenMode currentScreen;
extern InfoScreen udpScreen;
extern InfoScreen forecastScreen;
extern InfoScreen envQualityScreen;
extern InfoScreen currentCondScreen;
extern InfoScreen hourlyScreen;
extern bool inKeyboardMode;
extern bool wifiSelecting;
extern bool udpListening;
extern int localPort;
extern WiFiUDP udp;

void hideAllInfoScreens();

namespace
{
unsigned long s_lastAutoRotateMillis = 0;
bool s_sectionHeadingActive = false;
bool s_sectionHeadingRendered = false;
unsigned long s_sectionHeadingStartMs = 0;
uint16_t s_sectionHeadingDurationMs = 4000;
ScreenMode s_sectionHeadingTarget = SCREEN_CLOCK;
const char *s_sectionHeadingTitle = nullptr;
const char *s_sectionHeadingSubtitle = nullptr;

bool isRotationBlocked()
{
    return inKeyboardMode ||
           menuActive ||
           wifiSelecting ||
           sysInfoModal.isActive() ||
           wifiInfoModal.isActive() ||
           dateModal.isActive() ||
           mainMenuModal.isActive() ||
           deviceModal.isActive() ||
           wifiSettingsModal.isActive() ||
           displayModal.isActive() ||
           weatherModal.isActive() ||
           tempestModal.isActive() ||
           calibrationModal.isActive() ||
           systemModal.isActive() ||
           locationModal.isActive() ||
           worldTimeModal.isActive() ||
           manageTzModal.isActive() ||
           scenePreviewModal.isActive() ||
           isWeatherScenePreviewActive();
}

bool headingForScreen(ScreenMode mode, const char *&title, const char *&subtitle)
{
    title = nullptr;
    subtitle = nullptr;
    switch (mode)
    {
    case SCREEN_UDP_DATA:
        title = "Outdoor Conditions";
        subtitle = "Tempest";
        return true;
    case SCREEN_UDP_FORECAST:
        title = "10-Day Forecast";
        subtitle = nullptr;
        return true;
    case SCREEN_HOURLY:
        title = "24-Hour Forecast";
        subtitle = nullptr;
        return true;
    case SCREEN_CURRENT:
        title = "Outdoor Conditions";
        subtitle = nullptr;
        return true;
    case SCREEN_WIND_DIR:
        title = "Wind & Gusts";
        subtitle = nullptr;
        return true;
    case SCREEN_ENV_INDEX:
        title = "Indoor Air Quality";
        subtitle = nullptr;
        return true;
    case SCREEN_TEMP_HISTORY:
    case SCREEN_HUM_HISTORY:
    case SCREEN_CO2_HISTORY:
    case SCREEN_BARO_HISTORY:
        title = "Indoor Charts";
        subtitle = "Last 24-HR";
        return true;
    case SCREEN_PREDICT:
        title = "Next 24-HR";
        subtitle = "Prediction";
        return true;
    case SCREEN_NOAA_ALERT:
        title = "NOAA Alerts";
        subtitle = nullptr;
        return true;
    case SCREEN_CONDITION_SCENE:
        title = "Weather Scene";
        subtitle = nullptr;
        return true;
    case SCREEN_LUNAR_LUCK:
        title = "Lunar Calendar";
        subtitle = "Luck";
        return true;
    case SCREEN_OWM:
        return false;
    default:
        return false;
    }
}

void enterScreen(ScreenMode mode)
{
    currentScreen = mode;
    switch (currentScreen)
    {
    case SCREEN_CLOCK:
        drawClockScreen();
        break;
    case SCREEN_WORLD_CLOCK:
        drawWorldClockScreen();
        break;
    case SCREEN_UDP_DATA:
        showUdpScreen();
        break;
    case SCREEN_UDP_FORECAST:
        showForecastScreen();
        break;
    case SCREEN_WIND_DIR:
        showWindDirectionScreen();
        break;
    case SCREEN_ENV_INDEX:
        showEnvironmentalQualityScreen();
        break;
    case SCREEN_TEMP_HISTORY:
    case SCREEN_HUM_HISTORY:
    case SCREEN_CO2_HISTORY:
    case SCREEN_BARO_HISTORY:
        set24HourSectionPageForScreen(currentScreen);
        currentScreen = SCREEN_TEMP_HISTORY;
        draw24HourSectionScreen();
        break;
    case SCREEN_PREDICT:
        resetPredictionRenderState();
        drawPredictionScreen();
        break;
    case SCREEN_LUNAR_LUCK:
        drawLunarLuckScreen();
        break;
    case SCREEN_CONDITION_SCENE:
        drawConditionSceneScreen();
        break;
    case SCREEN_CURRENT:
        showCurrentConditionsScreen();
        break;
    case SCREEN_HOURLY:
        showHourlyForecastScreen();
        break;
    case SCREEN_NOAA_ALERT:
        refreshNoaaAlertsForScreenEntry();
        drawNoaaAlertsScreen();
        break;
    case SCREEN_OWM:
        drawOWMScreen();
        break;
    default:
        break;
    }
}

void beginScreenTransition(ScreenMode next, unsigned long now)
{
    hideAllInfoScreens();

    const char *title = nullptr;
    const char *subtitle = nullptr;
    const bool useHeading = (!isScreenOff()) && headingForScreen(next, title, subtitle);
    if (useHeading)
    {
        s_sectionHeadingActive = true;
        s_sectionHeadingRendered = false;
        s_sectionHeadingStartMs = now;
        s_sectionHeadingDurationMs = 4000;
        s_sectionHeadingTarget = next;
        s_sectionHeadingTitle = title;
        s_sectionHeadingSubtitle = subtitle;
        showSectionHeading(s_sectionHeadingTitle, s_sectionHeadingSubtitle, s_sectionHeadingDurationMs);
        s_sectionHeadingRendered = true;
        return;
    }

    enterScreen(next);
    playScreenRevealEffect(currentScreen);
    noteScreenRotation(now);
}

void renderScreenContents(ScreenMode mode)
{
    if (!screenIsAllowed(mode))
        return;

    switch (mode)
    {
    case SCREEN_OWM:
        drawOWMScreen();
        break;
    case SCREEN_UDP_DATA:
        udpScreen.tick();
        break;
    case SCREEN_UDP_FORECAST:
        forecastScreen.tick();
        break;
    case SCREEN_WIND_DIR:
        showWindDirectionScreen();
        break;
    case SCREEN_ENV_INDEX:
        envQualityScreen.tick();
        break;
    case SCREEN_TEMP_HISTORY:
    case SCREEN_HUM_HISTORY:
    case SCREEN_CO2_HISTORY:
    case SCREEN_BARO_HISTORY:
        set24HourSectionPageForScreen(mode);
        draw24HourSectionScreen();
        break;
    case SCREEN_PREDICT:
        drawPredictionScreen();
        break;
    case SCREEN_NOAA_ALERT:
        drawNoaaAlertsScreen();
        break;
    case SCREEN_CONDITION_SCENE:
        drawConditionSceneScreen();
        break;
    case SCREEN_CURRENT:
        currentCondScreen.tick();
        break;
    case SCREEN_HOURLY:
        hourlyScreen.tick();
        break;
    case SCREEN_CLOCK:
        drawClockScreen();
        break;
    case SCREEN_WORLD_CLOCK:
        drawWorldClockScreen();
        break;
    case SCREEN_LUNAR_LUCK:
        drawLunarLuckScreen();
        break;
    default:
        break;
    }
}
} // namespace

void playScreenRevealEffect(ScreenMode mode)
{
    if (isScreenOff())
        return;
    if (!screenIsAllowed(mode))
        return;

    uint8_t original = currentPanelBrightness;
    if (original == 0)
    {
        original = map(brightness, 1, 100, 3, 255);
        if (original == 0)
            original = 30;
    }

    setPanelBrightness(original);
    renderScreenContents(mode);
}

void noteScreenRotation(unsigned long now)
{
    s_lastAutoRotateMillis = now;
}

void ensureCurrentScreenAllowed()
{
    if (s_sectionHeadingActive)
        s_sectionHeadingActive = false;
    ScreenMode allowed = enforceAllowedScreen(currentScreen);
    if (allowed != currentScreen)
    {
        hideAllInfoScreens();
        currentScreen = allowed;
        playScreenRevealEffect(currentScreen);
        noteScreenRotation(millis());
    }
}

void applyDataSourcePolicies(bool wifiConnected)
{
    static int lastSource = -1;
    static bool lastWifiConnected = false;
    static bool pendingCloudBootstrapFetch = true;
    int previousSource = lastSource;
    const bool wantsUdpMulticast = wxv::provider::sourceUsesUdpMulticast(dataSource);

    if (wantsUdpMulticast)
    {
        if (!udpListening && wifiConnected)
        {
            if (udp.begin(localPort))
            {
                udpListening = true;
                Serial.printf("Listening for Tempest on UDP port %d\n", localPort);
            }
            else
            {
                udpListening = false;
                Serial.printf("Failed to bind UDP port %d\n", localPort);
            }
        }
    }
    else if (udpListening)
    {
        udp.stop();
        udpListening = false;
    }

    if (previousSource != dataSource)
    {
        bool prevWasForecastModel = wxv::provider::sourceIsForecastModel(previousSource);
        bool nowIsForecastModel = wxv::provider::sourceIsForecastModel(dataSource);
        if (prevWasForecastModel || nowIsForecastModel)
        {
            resetForecastModelData();
        }

        if (wifiConnected)
            wxv::provider::fetchActiveProviderData();
        pendingCloudBootstrapFetch = true;
        ensureCurrentScreenAllowed();

        if (udpScreen.isActive() || currentScreen == SCREEN_UDP_DATA)
            showUdpScreen();
        if (forecastScreen.isActive() || currentScreen == SCREEN_UDP_FORECAST)
            showForecastScreen();
        if (currentCondScreen.isActive() || currentScreen == SCREEN_CURRENT)
            showCurrentConditionsScreen();
        if (hourlyScreen.isActive() || currentScreen == SCREEN_HOURLY)
            showHourlyForecastScreen();

        // Force OWM-screen marquee regeneration when provider changes so
        // line 4 reflects the new active source immediately.
        requestScrollRebuild();
        if (currentScreen == SCREEN_OWM)
        {
            serviceScrollRebuild();
        }

        lastSource = dataSource;
    }

    // If source was selected while offline, or Wi-Fi just reconnected, force one
    // bootstrap fetch for cloud-based providers so first visible values appear.
    const bool wifiJustConnected = (wifiConnected && !lastWifiConnected);
    if (wifiConnected && (pendingCloudBootstrapFetch || wifiJustConnected))
    {
        const auto caps = wxv::provider::activeProvider().capabilities();
        if (caps.usesCloudFetch)
        {
            wxv::provider::fetchActiveProviderData();
            pendingCloudBootstrapFetch = false;
        }
    }

    lastWifiConnected = wifiConnected;
}

void rotateScreen(int direction)
{
    currentScreen = enforceAllowedScreen(currentScreen);

    ScreenMode next = nextAllowedScreen(currentScreen, direction);
    if (!screenIsAllowed(next))
        next = enforceAllowedScreen(next);

    if (next == currentScreen)
    {
        playScreenRevealEffect(currentScreen);
        noteScreenRotation(millis());
        return;
    }

    beginScreenTransition(next, millis());
}

void transitionToScreen(ScreenMode target)
{
    ScreenMode next = enforceAllowedScreen(target);
    if (!screenIsAllowed(next))
        next = enforceAllowedScreen(next);

    if (next == currentScreen)
    {
        playScreenRevealEffect(currentScreen);
        noteScreenRotation(millis());
        return;
    }

    beginScreenTransition(next, millis());
}

void handleAutoRotate(unsigned long now)
{
    if (s_sectionHeadingActive)
        return;

    if (autoRotate == 0)
    {
        noteScreenRotation(now);
        return;
    }
    if (isRotationBlocked())
    {
        noteScreenRotation(now);
        return;
    }
    unsigned long intervalMs = (autoRotateInterval > 0)
                                   ? static_cast<unsigned long>(autoRotateInterval) * 1000UL
                                   : 15000UL;
    if (now - s_lastAutoRotateMillis >= intervalMs)
    {
        rotateScreen(+1);
    }
}

bool serviceSectionHeading(unsigned long now)
{
    if (!s_sectionHeadingActive)
        return false;

    if (!s_sectionHeadingRendered)
    {
        showSectionHeading(s_sectionHeadingTitle, s_sectionHeadingSubtitle, s_sectionHeadingDurationMs);
        s_sectionHeadingRendered = true;
    }

    if (now - s_sectionHeadingStartMs >= static_cast<unsigned long>(s_sectionHeadingDurationMs))
    {
        s_sectionHeadingActive = false;
        enterScreen(s_sectionHeadingTarget);
        playScreenRevealEffect(currentScreen);
        noteScreenRotation(now);
    }
    return true;
}

void requestSectionHeadingRerender()
{
    if (!s_sectionHeadingActive)
        return;
    s_sectionHeadingRendered = false;
}

bool isSectionHeadingActive()
{
    return s_sectionHeadingActive;
}

void stepSectionHeading(int direction, unsigned long now)
{
    if (!s_sectionHeadingActive)
        return;

    if (direction == 0)
        direction = 1;

    ScreenMode base = s_sectionHeadingTarget;
    if (!screenIsAllowed(base))
        base = enforceAllowedScreen(base);

    ScreenMode next = nextAllowedScreen(base, (direction > 0) ? 1 : -1);
    if (!screenIsAllowed(next))
        next = enforceAllowedScreen(next);

    if (next == base)
    {
        // Keep heading visible but restart timer so interaction feels immediate.
        s_sectionHeadingStartMs = now;
        s_sectionHeadingRendered = false;
        return;
    }

    const char *title = nullptr;
    const char *subtitle = nullptr;
    const bool useHeading = (!isScreenOff()) && headingForScreen(next, title, subtitle);
    if (useHeading)
    {
        s_sectionHeadingTarget = next;
        s_sectionHeadingTitle = title;
        s_sectionHeadingSubtitle = subtitle;
        s_sectionHeadingStartMs = now;
        s_sectionHeadingRendered = false;
        s_sectionHeadingDurationMs = 4000;
        showSectionHeading(s_sectionHeadingTitle, s_sectionHeadingSubtitle, s_sectionHeadingDurationMs);
        s_sectionHeadingRendered = true;
        return;
    }

    s_sectionHeadingActive = false;
    enterScreen(next);
    playScreenRevealEffect(currentScreen);
    noteScreenRotation(now);
}

void skipSectionHeading(unsigned long now)
{
    if (!s_sectionHeadingActive)
        return;
    s_sectionHeadingActive = false;
    enterScreen(s_sectionHeadingTarget);
    playScreenRevealEffect(currentScreen);
    noteScreenRotation(now);
}
