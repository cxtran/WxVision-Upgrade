#include "WorldClockScreen.h"

#include "display.h"
#include "worldtime.h"
#include "datetimesettings.h"
#include "settings.h"
#include "units.h"
#include "alarm.h"
#include "ui_theme.h"

namespace
{
enum class BannerPhase : uint8_t
{
    SplitReveal = 0,
    Hold,
    SweepOut,
    ScrollWeather
};

constexpr unsigned long SPLIT_MS = 620UL;
constexpr unsigned long HOLD_MS = 650UL;
constexpr unsigned long SWEEP_OUT_MS = 180UL;
constexpr unsigned long SHORT_WEATHER_HOLD_MS = 1700UL;
constexpr unsigned long SHORT_CITY_HOLD_MS = 1200UL;
constexpr unsigned long WEATHER_LEADIN_MS = 180UL;
constexpr unsigned long SCROLL_STEP_MS = 40UL;
constexpr unsigned long GAP_MS = 60UL;
constexpr unsigned long MANUAL_PAUSE_MS = 1200UL;

BannerPhase s_phase = BannerPhase::SplitReveal;
bool s_initialized = false;
size_t s_cityIndex = 0;
size_t s_knownSelectionCount = 0;
unsigned long s_phaseStartMs = 0;
unsigned long s_lastScrollStepMs = 0;
unsigned long s_pauseUntilMs = 0;
bool s_rotationPaused = false;
int s_scrollX = PANEL_RES_X;
int s_cityTextWidth = 0;
int s_weatherTextWidth = 0;
int s_cityScrollX = 0;
bool s_weatherLeadIn = false;
unsigned long s_weatherHoldUntilMs = 0;
unsigned long s_cityHoldUntilMs = 0;
char s_cityText[40];
char s_weatherText[96];

float easeInOutCubic(float t)
{
    if (t < 0.5f)
        return 4.0f * t * t * t;
    float u = -2.0f * t + 2.0f;
    return 1.0f - (u * u * u) / 2.0f;
}

uint16_t scaleColor565(uint16_t color, float intensity)
{
    if (intensity < 0.0f)
        intensity = 0.0f;
    if (intensity > 1.0f)
        intensity = 1.0f;

    uint8_t r = static_cast<uint8_t>((color >> 11) & 0x1F);
    uint8_t g = static_cast<uint8_t>((color >> 5) & 0x3F);
    uint8_t b = static_cast<uint8_t>(color & 0x1F);

    r = static_cast<uint8_t>(r * intensity);
    g = static_cast<uint8_t>(g * intensity);
    b = static_cast<uint8_t>(b * intensity);

    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

uint16_t activeAccentColor()
{
    return (theme == 1) ? dma_display->color565(120, 120, 180)
                        : dma_display->color565(180, 220, 255);
}

uint16_t inactiveDimColor()
{
    return (theme == 1) ? dma_display->color565(18, 18, 30)
                        : dma_display->color565(35, 45, 60);
}

uint16_t cityNameColor()
{
    return (theme == 1) ? dma_display->color565(170, 170, 255)
                        : dma_display->color565(235, 248, 255);
}

uint16_t weatherTextColor()
{
    return (theme == 1) ? dma_display->color565(150, 150, 140)
                        : dma_display->color565(205, 198, 182);
}

int textWidth(const char *text)
{
    int16_t x1, y1;
    uint16_t w, h;
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    dma_display->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return static_cast<int>(w);
}

void setBannerPayloadForCurrentCity()
{
    s_cityText[0] = '\0';
    s_weatherText[0] = '\0';

    const size_t count = worldTimeDisplayCount();
    if (count == 0)
    {
        strncpy(s_cityText, "No Cities", sizeof(s_cityText) - 1);
        strncpy(s_weatherText, "Updating...", sizeof(s_weatherText) - 1);
        s_cityText[sizeof(s_cityText) - 1] = '\0';
        s_weatherText[sizeof(s_weatherText) - 1] = '\0';
        s_cityTextWidth = textWidth(s_cityText);
        s_weatherTextWidth = textWidth(s_weatherText);
        return;
    }

    if (s_cityIndex >= count)
        s_cityIndex = 0;

    String city = worldTimeDisplayCityLabel(s_cityIndex);
    if (city.length() > 0)
    {
        city.toCharArray(s_cityText, sizeof(s_cityText));
        s_cityText[sizeof(s_cityText) - 1] = '\0';
    }
    else
    {
        strncpy(s_cityText, "City", sizeof(s_cityText) - 1);
        s_cityText[sizeof(s_cityText) - 1] = '\0';
    }

    WorldWeather weather;
    if (worldTimeGetDisplayWeather(s_cityIndex, weather))
    {
        char tempBuf[16];
        if (isnan(weather.temperature))
        {
            strncpy(tempBuf, "--", sizeof(tempBuf) - 1);
            tempBuf[sizeof(tempBuf) - 1] = '\0';
        }
        else
        {
            String temp = fmtTemp(weather.temperature, 0);
            temp.toCharArray(tempBuf, sizeof(tempBuf));
        }

        const char *condition = weather.condition.length() ? weather.condition.c_str() : "Weather";
        snprintf(s_weatherText, sizeof(s_weatherText), "%s %s", condition, tempBuf);
    }
    else
    {
        strncpy(s_weatherText, "Updating...", sizeof(s_weatherText) - 1);
        s_weatherText[sizeof(s_weatherText) - 1] = '\0';
    }

    s_cityTextWidth = textWidth(s_cityText);
    s_weatherTextWidth = textWidth(s_weatherText);
}

void startSplitReveal(unsigned long nowMs)
{
    s_phase = BannerPhase::SplitReveal;
    s_phaseStartMs = nowMs;
}

void stepToCity(int delta, unsigned long nowMs, bool manualPause)
{
    const size_t count = worldTimeDisplayCount();
    if (count > 0)
    {
        int next = static_cast<int>(s_cityIndex);
        if (delta > 0)
            next += 1;
        else if (delta < 0)
            next -= 1;
        if (next < 0)
            next = static_cast<int>(count) - 1;
        if (next >= static_cast<int>(count))
            next = 0;
        s_cityIndex = static_cast<size_t>(next);
    }
    else
    {
        s_cityIndex = 0;
    }

    setBannerPayloadForCurrentCity();
    startSplitReveal(nowMs);
    s_lastScrollStepMs = nowMs;
    s_cityScrollX = 0;
    s_scrollX = PANEL_RES_X;
    s_cityHoldUntilMs = nowMs + SHORT_CITY_HOLD_MS;
    if (manualPause)
        s_pauseUntilMs = nowMs + MANUAL_PAUSE_MS;
}

void updateBannerState(unsigned long nowMs)
{
    const size_t count = worldTimeDisplayCount();
    if (!s_initialized)
    {
        s_initialized = true;
        s_knownSelectionCount = count;
        stepToCity(0, nowMs, false);
        return;
    }

    if (count != s_knownSelectionCount)
    {
        s_knownSelectionCount = count;
        if (s_cityIndex >= count)
            s_cityIndex = 0;
        stepToCity(0, nowMs, false);
        return;
    }

    if (s_rotationPaused)
        return;

    if (nowMs < s_pauseUntilMs)
        return;

    switch (s_phase)
    {
    case BannerPhase::SplitReveal:
        if (nowMs - s_phaseStartMs >= SPLIT_MS)
        {
            s_phase = BannerPhase::Hold;
            s_phaseStartMs = nowMs;
            s_cityScrollX = 0;
            s_cityHoldUntilMs = nowMs + SHORT_CITY_HOLD_MS;
            s_lastScrollStepMs = nowMs;
        }
        break;
    case BannerPhase::Hold:
        if (!worldTimeAutoCycleEnabled() && s_cityTextWidth > PANEL_RES_X)
        {
            if (nowMs < s_cityHoldUntilMs)
                break;
            if (nowMs - s_lastScrollStepMs >= SCROLL_STEP_MS)
            {
                unsigned long elapsed = nowMs - s_lastScrollStepMs;
                int steps = static_cast<int>(elapsed / SCROLL_STEP_MS);
                if (steps < 1)
                    steps = 1;
                s_cityScrollX -= steps;
                s_lastScrollStepMs += static_cast<unsigned long>(steps) * SCROLL_STEP_MS;
            }
            if (s_cityScrollX + s_cityTextWidth < 0)
            {
                s_phase = BannerPhase::SweepOut;
                s_phaseStartMs = nowMs;
            }
        }
        else
        {
            if (nowMs - s_phaseStartMs >= HOLD_MS)
            {
                s_phase = BannerPhase::SweepOut;
                s_phaseStartMs = nowMs;
            }
        }
        break;
    case BannerPhase::SweepOut:
        if (nowMs - s_phaseStartMs >= SWEEP_OUT_MS)
        {
            s_phase = BannerPhase::ScrollWeather;
            s_phaseStartMs = nowMs;
            s_scrollX = 0;
            s_weatherLeadIn = (s_weatherTextWidth > PANEL_RES_X);
            s_weatherHoldUntilMs = nowMs + SHORT_WEATHER_HOLD_MS;
            s_lastScrollStepMs = nowMs;
        }
        break;
    case BannerPhase::ScrollWeather:
        if (s_weatherTextWidth <= PANEL_RES_X)
        {
            if (nowMs - s_phaseStartMs >= SHORT_WEATHER_HOLD_MS)
            {
                if (worldTimeAutoCycleEnabled())
                {
                    stepToCity(+1, nowMs, false);
                    s_pauseUntilMs = nowMs + GAP_MS;
                }
                else
                {
                    startSplitReveal(nowMs);
                    s_pauseUntilMs = nowMs + GAP_MS;
                }
            }
            break;
        }

        if (s_weatherLeadIn)
        {
            if (nowMs - s_phaseStartMs >= WEATHER_LEADIN_MS)
            {
                s_weatherLeadIn = false;
                s_weatherHoldUntilMs = nowMs + SHORT_WEATHER_HOLD_MS;
                s_lastScrollStepMs = nowMs;
                s_scrollX = 0;
            }
            break;
        }
        if (nowMs < s_weatherHoldUntilMs)
        {
            break;
        }
        if (s_lastScrollStepMs < s_weatherHoldUntilMs)
        {
            // Prevent a large first delta after hold; start smooth from current x.
            s_lastScrollStepMs = nowMs;
        }

        if (nowMs - s_lastScrollStepMs >= SCROLL_STEP_MS)
        {
            unsigned long elapsed = nowMs - s_lastScrollStepMs;
            int steps = static_cast<int>(elapsed / SCROLL_STEP_MS);
            if (steps < 1)
                steps = 1;
            s_scrollX -= steps;
            s_lastScrollStepMs += static_cast<unsigned long>(steps) * SCROLL_STEP_MS;
        }
        if (s_scrollX + s_weatherTextWidth < 0)
        {
            if (worldTimeAutoCycleEnabled())
            {
                stepToCity(+1, nowMs, false);
                s_pauseUntilMs = nowMs + GAP_MS;
            }
            else
            {
                startSplitReveal(nowMs);
                s_pauseUntilMs = nowMs + GAP_MS;
            }
        }
        break;
    }
}

void drawBannerTopLine(unsigned long nowMs)
{
    dma_display->fillRect(0, 0, PANEL_RES_X, 8, myBLACK);
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    const uint16_t dividerColor = ui_theme::isNightTheme() ? ui_theme::monoUnderline()
                                                           : ui_theme::infoUnderlineDay();

    if (s_phase == BannerPhase::ScrollWeather)
    {
        uint16_t weatherColor = weatherTextColor();
        if (s_weatherTextWidth <= PANEL_RES_X)
        {
            dma_display->setTextColor(weatherColor);
            dma_display->setCursor(0, 0);
            dma_display->print(s_weatherText);
        }
        else
        {
            if (s_weatherLeadIn)
            {
                unsigned long elapsed = nowMs - s_phaseStartMs;
                if (elapsed > WEATHER_LEADIN_MS)
                    elapsed = WEATHER_LEADIN_MS;
                float t = static_cast<float>(elapsed) / static_cast<float>(WEATHER_LEADIN_MS);
                weatherColor = scaleColor565(weatherColor, easeInOutCubic(t));
            }
            dma_display->setTextColor(weatherColor);
            dma_display->setCursor(s_scrollX, 0);
            dma_display->print(s_weatherText);
        }
        dma_display->drawFastHLine(0, 7, PANEL_RES_X, dividerColor);
        return;
    }

    float cityIntensity = 1.0f;
    if (s_phase == BannerPhase::SplitReveal)
    {
        unsigned long elapsed = nowMs - s_phaseStartMs;
        if (elapsed > SPLIT_MS)
            elapsed = SPLIT_MS;
        float t = static_cast<float>(elapsed) / static_cast<float>(SPLIT_MS);
        cityIntensity = easeInOutCubic(t);
    }
    else if (s_phase == BannerPhase::SweepOut)
    {
        unsigned long elapsed = nowMs - s_phaseStartMs;
        if (elapsed > SWEEP_OUT_MS)
            elapsed = SWEEP_OUT_MS;
        float t = static_cast<float>(elapsed) / static_cast<float>(SWEEP_OUT_MS);
        cityIntensity = 1.0f - easeInOutCubic(t);
    }

    dma_display->setTextColor(scaleColor565(cityNameColor(), cityIntensity));
    int cityX = 0;
    if (!worldTimeAutoCycleEnabled() && s_phase == BannerPhase::Hold && s_cityTextWidth > PANEL_RES_X)
    {
        cityX = s_cityScrollX;
    }
    dma_display->setCursor(cityX, 0);
    dma_display->print(s_cityText);
    dma_display->drawFastHLine(0, 7, PANEL_RES_X, dividerColor);
}

void drawSegmentedPageBar()
{
    // Page indicator intentionally disabled.
}

DateTime resolveWorldCityNow()
{
    DateTime out(2000, 1, 1, 0, 0, 0);
    if (worldTimeGetDisplayDateTime(s_cityIndex, out))
        return out;
    if (getLocalDateTime(out))
        return out;
    return out;
}
} // namespace

void resetWorldClockScreenState()
{
    s_initialized = false;
    s_cityIndex = 0;
    s_knownSelectionCount = 0;
    s_phase = BannerPhase::SplitReveal;
    s_phaseStartMs = 0;
    s_lastScrollStepMs = 0;
    s_pauseUntilMs = 0;
    s_rotationPaused = false;
    s_scrollX = PANEL_RES_X;
    s_cityScrollX = 0;
    s_cityTextWidth = 0;
    s_weatherTextWidth = 0;
    s_weatherLeadIn = false;
    s_weatherHoldUntilMs = 0;
    s_cityHoldUntilMs = 0;
    s_cityText[0] = '\0';
    s_weatherText[0] = '\0';
}

bool worldClockHandleStep(int delta)
{
    if (delta == 0)
        return false;
    if (worldTimeDisplayCount() == 0)
        return false;
    stepToCity(delta, millis(), true);
    return true;
}

void handleWorldClockSelectPress()
{
    s_rotationPaused = !s_rotationPaused;
    s_pauseUntilMs = 0;
}

void drawWorldClockScreen()
{
    const unsigned long nowMs = millis();
    updateBannerState(nowMs);

    dma_display->fillScreen(0);
    drawBannerTopLine(nowMs);

    DateTime now = resolveWorldCityNow();
    bool alarmActive = isAlarmCurrentlyActive();
    drawClockTimeLine(now, alarmActive);
    drawClockDateLine(now);
    drawClockPulseDot(now.second());
}
