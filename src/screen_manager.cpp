#include "screen_manager.h"

#include <cstring>
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
#include "display_runtime.h"
#include "display_astronomy.h"
#include "display_sky_facts.h"

extern ScreenMode currentScreen;
extern InfoScreen udpScreen;
extern InfoScreen lightningScreen;
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
struct TemporaryHeadingAlert
{
    char text[40];
    char subtitle[40];
    uint16_t durationMs;
    uint32_t signature;
};

unsigned long s_lastAutoRotateMillis = 0;
bool s_sectionHeadingActive = false;
bool s_sectionHeadingRendered = false;
unsigned long s_sectionHeadingStartMs = 0;
uint16_t s_sectionHeadingDurationMs = 4000;
ScreenMode s_sectionHeadingTarget = SCREEN_CLOCK;
const char *s_sectionHeadingTitle = nullptr;
const char *s_sectionHeadingSubtitle = nullptr;

static constexpr uint8_t kTemporaryAlertQueueCapacity = 4;
TemporaryHeadingAlert s_temporaryAlertQueue[kTemporaryAlertQueueCapacity] = {};
uint8_t s_temporaryAlertHead = 0;
uint8_t s_temporaryAlertCount = 0;
bool s_temporaryAlertActive = false;
bool s_temporaryAlertRendered = false;
unsigned long s_temporaryAlertStartMs = 0;
uint16_t s_temporaryAlertDurationMs = 0;
char s_temporaryAlertText[40] = {0};
char s_temporaryAlertSubtitle[40] = {0};
uint32_t s_temporaryAlertSignature = 0;

bool isRotationBlocked();
bool isTemporaryAlertBlocked();
bool isAlertDuplicate(const char *text, uint32_t signature)
{
    if (s_temporaryAlertActive)
    {
        if (signature != 0 && s_temporaryAlertSignature == signature)
            return true;
        if (signature == 0 && strncmp(s_temporaryAlertText, text, sizeof(s_temporaryAlertText)) == 0)
            return true;
    }

    for (uint8_t i = 0; i < s_temporaryAlertCount; ++i)
    {
        const uint8_t idx = static_cast<uint8_t>((s_temporaryAlertHead + i) % kTemporaryAlertQueueCapacity);
        const TemporaryHeadingAlert &queued = s_temporaryAlertQueue[idx];
        if (signature != 0 && queued.signature == signature)
            return true;
        if (signature == 0 && strncmp(queued.text, text, sizeof(queued.text)) == 0)
            return true;
    }
    return false;
}

void activateTemporaryAlert(const TemporaryHeadingAlert &alert, unsigned long now)
{
    s_temporaryAlertActive = true;
    s_temporaryAlertRendered = false;
    s_temporaryAlertStartMs = now;
    s_temporaryAlertDurationMs = alert.durationMs;
    s_temporaryAlertSignature = alert.signature;
    strncpy(s_temporaryAlertText, alert.text, sizeof(s_temporaryAlertText) - 1);
    s_temporaryAlertText[sizeof(s_temporaryAlertText) - 1] = '\0';
    strncpy(s_temporaryAlertSubtitle, alert.subtitle, sizeof(s_temporaryAlertSubtitle) - 1);
    s_temporaryAlertSubtitle[sizeof(s_temporaryAlertSubtitle) - 1] = '\0';

    if (!isScreenOff() && !isTemporaryAlertBlocked())
    {
        showSectionHeading(s_temporaryAlertText,
                           s_temporaryAlertSubtitle[0] ? s_temporaryAlertSubtitle : nullptr,
                           s_temporaryAlertDurationMs);
        s_temporaryAlertRendered = true;
    }
}

bool beginNextTemporaryAlert(unsigned long now)
{
    if (s_temporaryAlertActive || s_sectionHeadingActive || s_temporaryAlertCount == 0 || isTemporaryAlertBlocked())
        return false;

    // Alerts are displayed one at a time and deferred until section-heading
    // navigation finishes so normal rotation flow stays predictable.
    const TemporaryHeadingAlert alert = s_temporaryAlertQueue[s_temporaryAlertHead];
    s_temporaryAlertHead = static_cast<uint8_t>((s_temporaryAlertHead + 1) % kTemporaryAlertQueueCapacity);
    s_temporaryAlertCount--;
    activateTemporaryAlert(alert, now);
    return true;
}

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
           mqttModal.isActive() ||
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

bool isTemporaryAlertBlocked()
{
    return inKeyboardMode ||
           wifiSelecting ||
           setupPromptModal.isActive() ||
           sysInfoModal.isActive() ||
           wifiInfoModal.isActive() ||
           dateModal.isActive() ||
           mainMenuModal.isActive() ||
           deviceModal.isActive() ||
           wifiSettingsModal.isActive() ||
           displayModal.isActive() ||
           alarmModal.isActive() ||
           mqttModal.isActive() ||
           weatherModal.isActive() ||
           tempestModal.isActive() ||
           calibrationModal.isActive() ||
           noaaModal.isActive() ||
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
    case SCREEN_CLOCK:
        title = "Local Time";
        subtitle = "Clock";
        return true;
    case SCREEN_OWM:
        title = "Local Time";
        subtitle = "Weather";
        return true;
    case SCREEN_UDP_DATA:
        title = "Outdoor Conditions";
        subtitle = "Tempest";
        return true;
    case SCREEN_LIGHTNING:
        title = "Lightning";
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
    case SCREEN_WORLD_CLOCK:
        title = "World Time";
        subtitle = nullptr;
        return true;
    case SCREEN_ASTRONOMY:
        title = "Astronomy";
        subtitle = "Sun & Moon";
        return true;
    case SCREEN_SKY_BRIEF:
        title = "Sky Brief";
        subtitle = "Summary";
        return true;
#if WXV_ENABLE_LUNAR_CALENDAR && WXV_ENABLE_LUNAR_LUCK
    case SCREEN_LUNAR_LUCK:
        title = "Lunar Calendar";
        subtitle = "Luck";
        return true;
#endif
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
    case SCREEN_ASTRONOMY:
        resetAstronomyScreenState();
        drawAstronomyScreen();
        break;
    case SCREEN_SKY_BRIEF:
        resetSkyBriefScreenState();
        drawSkyBriefScreen();
        break;
    case SCREEN_UDP_DATA:
        showUdpScreen();
        break;
    case SCREEN_LIGHTNING:
        showLightningScreen();
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
#if WXV_ENABLE_LUNAR_CALENDAR && WXV_ENABLE_LUNAR_LUCK
    case SCREEN_LUNAR_LUCK:
        drawLunarLuckScreen();
        break;
#endif
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
    if (currentScreen == SCREEN_NOAA_ALERT && next != SCREEN_NOAA_ALERT)
        resetNoaaAlertsScreenPager();

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
    case SCREEN_LIGHTNING:
        lightningScreen.tick();
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
    case SCREEN_ASTRONOMY:
        drawAstronomyScreen();
        break;
    case SCREEN_SKY_BRIEF:
        drawSkyBriefScreen();
        break;
#if WXV_ENABLE_LUNAR_CALENDAR && WXV_ENABLE_LUNAR_LUCK
    case SCREEN_LUNAR_LUCK:
        drawLunarLuckScreen();
        break;
#endif
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
    static unsigned long earliestCloudBootstrapFetchMs = 0;
    bool deferBootstrapFetchUntilNextPass = false;
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
        const auto caps = wxv::provider::activeProvider().capabilities();
        if (prevWasForecastModel || nowIsForecastModel)
        {
            resetForecastModelData();
        }

        if (wifiConnected && caps.usesUdpMulticast)
        {
            // WeatherFlow switches are more memory- and timing-sensitive because
            // UDP, screen refresh, and a large cloud forecast fetch all start at once.
            // Give the source change a short settle window before the cloud fetch starts.
            pendingCloudBootstrapFetch = caps.usesCloudFetch;
            earliestCloudBootstrapFetchMs = millis() + 3000UL;
            deferBootstrapFetchUntilNextPass = pendingCloudBootstrapFetch;
        }
        else if (wifiConnected)
        {
            wxv::provider::fetchActiveProviderData();
            pendingCloudBootstrapFetch = false;
            earliestCloudBootstrapFetchMs = 0;
        }
        else
        {
            pendingCloudBootstrapFetch = caps.usesCloudFetch;
            earliestCloudBootstrapFetchMs = 0;
        }
        ensureCurrentScreenAllowed();

        if (udpScreen.isActive() || currentScreen == SCREEN_UDP_DATA)
            showUdpScreen();
        if (lightningScreen.isActive() || currentScreen == SCREEN_LIGHTNING)
            showLightningScreen();
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
        reset_Time_and_Date_Display = true;

        lastSource = dataSource;
    }

    // If source was selected while offline, or Wi-Fi just reconnected, force one
    // bootstrap fetch for cloud-based providers so first visible values appear.
    const bool wifiJustConnected = (wifiConnected && !lastWifiConnected);
    if (!deferBootstrapFetchUntilNextPass && wifiConnected && (pendingCloudBootstrapFetch || wifiJustConnected))
    {
        if (earliestCloudBootstrapFetchMs != 0 &&
            static_cast<int32_t>(millis() - earliestCloudBootstrapFetchMs) < 0)
        {
            lastWifiConnected = wifiConnected;
            return;
        }

        const auto caps = wxv::provider::activeProvider().capabilities();
        if (caps.usesCloudFetch)
        {
            wxv::provider::fetchActiveProviderData();
            pendingCloudBootstrapFetch = false;
            earliestCloudBootstrapFetchMs = 0;
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
    if (s_sectionHeadingActive || s_temporaryAlertActive)
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
    if (s_sectionHeadingActive)
    {
        s_sectionHeadingRendered = false;
    }
    if (s_temporaryAlertActive)
    {
        s_temporaryAlertRendered = false;
    }
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

void queueTemporaryAlertHeading(const char *text, uint16_t durationMs, uint32_t signature, const char *subtitle)
{
    if (!text || !text[0])
        return;
    if (durationMs == 0)
        durationMs = 2000;
    if (isAlertDuplicate(text, signature))
        return;

    TemporaryHeadingAlert alert = {};
    strncpy(alert.text, text, sizeof(alert.text) - 1);
    alert.text[sizeof(alert.text) - 1] = '\0';
    if (subtitle && subtitle[0])
    {
        strncpy(alert.subtitle, subtitle, sizeof(alert.subtitle) - 1);
        alert.subtitle[sizeof(alert.subtitle) - 1] = '\0';
    }
    alert.durationMs = durationMs;
    alert.signature = signature;

    if (!s_temporaryAlertActive &&
        !s_sectionHeadingActive &&
        s_temporaryAlertCount == 0 &&
        !isTemporaryAlertBlocked())
    {
        activateTemporaryAlert(alert, millis());
        return;
    }

    if (s_temporaryAlertCount >= kTemporaryAlertQueueCapacity)
    {
        const uint8_t tail = static_cast<uint8_t>((s_temporaryAlertHead + s_temporaryAlertCount - 1) % kTemporaryAlertQueueCapacity);
        s_temporaryAlertQueue[tail] = alert;
        return;
    }

    const uint8_t insertIdx = static_cast<uint8_t>((s_temporaryAlertHead + s_temporaryAlertCount) % kTemporaryAlertQueueCapacity);
    s_temporaryAlertQueue[insertIdx] = alert;
    s_temporaryAlertCount++;
}

bool isTemporaryAlertActive()
{
    return s_temporaryAlertActive;
}

void skipTemporaryAlertHeading(unsigned long now)
{
    if (!s_temporaryAlertActive)
        return;

    s_temporaryAlertActive = false;
    s_temporaryAlertRendered = false;
    s_temporaryAlertText[0] = '\0';
    s_temporaryAlertSubtitle[0] = '\0';
    s_temporaryAlertSignature = 0;
    playScreenRevealEffect(currentScreen);
    noteScreenRotation(now);
}

bool serviceTemporaryAlertHeading(unsigned long now)
{
    bool activatedThisPass = false;
    if (!s_temporaryAlertActive)
    {
        if (!beginNextTemporaryAlert(now))
            return false;
        activatedThisPass = true;
    }

    if (!s_temporaryAlertRendered)
    {
        showSectionHeading(s_temporaryAlertText, s_temporaryAlertSubtitle[0] ? s_temporaryAlertSubtitle : nullptr, s_temporaryAlertDurationMs);
        s_temporaryAlertRendered = true;
    }

    if (activatedThisPass)
        return true;

    if (now >= s_temporaryAlertStartMs &&
        (now - s_temporaryAlertStartMs) >= static_cast<unsigned long>(s_temporaryAlertDurationMs))
    {
        s_temporaryAlertActive = false;
        s_temporaryAlertRendered = false;
        s_temporaryAlertText[0] = '\0';
        s_temporaryAlertSubtitle[0] = '\0';
        s_temporaryAlertSignature = 0;
        playScreenRevealEffect(currentScreen);
        noteScreenRotation(now);
        beginNextTemporaryAlert(now);
    }
    return true;
}
