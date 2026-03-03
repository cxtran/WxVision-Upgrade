#include <Arduino.h>

#include "menu.h"
#include "display.h"

static void showSplashUntilButton()
{
    // Do not reuse startup splash visuals at runtime.
    // Keep this as a short non-blocking-style pause preview instead.
    getIRCodeNonBlocking(); // clear any pending input
    const unsigned long startMs = millis();
    while ((millis() - startMs) < 1000UL)
    {
        IRCodes::WxKey key = getIRCodeNonBlocking();
        if (key != IRCodes::WxKey::Unknown)
            break;
        delay(20);
    }
    delay(120);
}

struct WeatherScenePreviewOption
{
    const char *label;
    WeatherSceneKind kind;
};

static const WeatherScenePreviewOption WEATHER_SCENE_PREVIEW_OPTIONS[] = {
    {"Sunny (Day)", WeatherSceneKind::Sunny},
    {"Sunny (Night)", WeatherSceneKind::SunnyNight},
    {"Partly Cloudy (Day)", WeatherSceneKind::PartlyCloudy},
    {"Partly Cloudy (Night)", WeatherSceneKind::PartlyCloudyNight},
    {"Cloudy (Day)", WeatherSceneKind::Cloudy},
    {"Cloudy (Night)", WeatherSceneKind::CloudyNight},
    {"Overcast (Day)", WeatherSceneKind::Overcast},
    {"Overcast (Night)", WeatherSceneKind::OvercastNight},
    {"Fog (Day)", WeatherSceneKind::Fog},
    {"Fog (Night)", WeatherSceneKind::FogNight},
    {"Windy (Day)", WeatherSceneKind::Windy},
    {"Windy (Night)", WeatherSceneKind::WindyNight},
    {"Rain (Day)", WeatherSceneKind::Rain},
    {"Rain (Night)", WeatherSceneKind::RainNight},
    {"Thunderstorm (Day)", WeatherSceneKind::Thunderstorm},
    {"Thunderstorm (Night)", WeatherSceneKind::ThunderstormNight},
    {"Snow (Day)", WeatherSceneKind::Snow},
    {"Snow (Night)", WeatherSceneKind::SnowNight},
    {"Clear Night", WeatherSceneKind::ClearNight}};

static constexpr int WEATHER_SCENE_PREVIEW_COUNT =
    sizeof(WEATHER_SCENE_PREVIEW_OPTIONS) / sizeof(WEATHER_SCENE_PREVIEW_OPTIONS[0]);

static bool weatherScenePreviewActive = false;
static int weatherScenePreviewIndex = 0;

static int wrapPreviewIndex(int idx)
{
    if (WEATHER_SCENE_PREVIEW_COUNT == 0)
        return 0;
    int mod = idx % WEATHER_SCENE_PREVIEW_COUNT;
    if (mod < 0)
        mod += WEATHER_SCENE_PREVIEW_COUNT;
    return mod;
}

static void renderWeatherScenePreview()
{
    if (WEATHER_SCENE_PREVIEW_COUNT == 0)
        return;

    weatherScenePreviewIndex = wrapPreviewIndex(weatherScenePreviewIndex);
    const WeatherScenePreviewOption &opt = WEATHER_SCENE_PREVIEW_OPTIONS[weatherScenePreviewIndex];

    dma_display->fillScreen(0);
    drawWeatherConditionScene(opt.kind);
}

static void startWeatherScenePreview(int index)
{
    weatherScenePreviewIndex = wrapPreviewIndex(index);
    weatherScenePreviewActive = true;
    menuActive = false;
    renderWeatherScenePreview();
}

static void cycleWeatherScenePreview(int delta)
{
    if (WEATHER_SCENE_PREVIEW_COUNT == 0)
        return;
    weatherScenePreviewIndex = wrapPreviewIndex(weatherScenePreviewIndex + delta);
    renderWeatherScenePreview();
}

void showScenePreviewModal()
{
    weatherScenePreviewActive = false;

    if (currentMenuLevel != MENU_NONE && currentMenuLevel != MENU_SCENE_PREVIEW)
    {
        pushMenu(currentMenuLevel);
    }

    currentMenuLevel = MENU_SCENE_PREVIEW;
    menuActive = true;

    constexpr int menuItemCount = WEATHER_SCENE_PREVIEW_COUNT + 1;
    String labels[menuItemCount];
    InfoFieldType types[menuItemCount];
    for (int i = 0; i < WEATHER_SCENE_PREVIEW_COUNT; ++i)
    {
        labels[i] = WEATHER_SCENE_PREVIEW_OPTIONS[i].label;
        types[i] = InfoButton;
    }
    labels[WEATHER_SCENE_PREVIEW_COUNT] = "Splash Screen";
    types[WEATHER_SCENE_PREVIEW_COUNT] = InfoButton;

    scenePreviewModal.setLines(labels, types, menuItemCount);
    scenePreviewModal.setCallback([](bool accepted, int btnIdx) {
        if (!accepted)
        {
            scenePreviewModal.hide();
            showSystemModal();
            return;
        }

        int action = (btnIdx >= 0) ? btnIdx : scenePreviewModal.getSelIndex();
        if (action < 0)
            action = 0;

        scenePreviewModal.hide();

        if (action >= WEATHER_SCENE_PREVIEW_COUNT)
        {
            showSplashUntilButton();
            pendingModalFn = showScenePreviewModal;
            pendingModalTime = millis() + 200;
            return;
        }

        startWeatherScenePreview(action);
    });
    scenePreviewModal.resetState();
    scenePreviewModal.show();
}

bool isWeatherScenePreviewActive()
{
    return weatherScenePreviewActive;
}

void handleWeatherScenePreviewIR(IRCodes::WxKey key)
{
    if (!weatherScenePreviewActive)
        return;

    switch (key)
    {
    case IRCodes::WxKey::Left:
    case IRCodes::WxKey::Up:
        cycleWeatherScenePreview(-1);
        break;
    case IRCodes::WxKey::Right:
    case IRCodes::WxKey::Down:
        cycleWeatherScenePreview(+1);
        break;
    case IRCodes::WxKey::Ok:
    case IRCodes::WxKey::Cancel:
    case IRCodes::WxKey::Menu:
        weatherScenePreviewActive = false;
        showScenePreviewModal();
        break;
    default:
        break;
    }
}
