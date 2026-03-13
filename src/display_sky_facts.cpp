#include "display_sky_facts.h"

#include <math.h>
#include <string.h>

#include "astronomy.h"
#include "display.h"
#include "render_scheduler.h"
#include "settings.h"
#include "ui_theme.h"

namespace
{
constexpr unsigned long kSkyFactsPageAutoMs = 4200UL;
constexpr int kSummaryRepeatGapPx = 4;
constexpr int kSummaryEntryInsetPx = 6;
constexpr unsigned long kSummaryStartDelayMs = 2000UL;

uint8_t s_skyFactsPageIndex = 0;
unsigned long s_skyFactsLastSwitchMs = 0;
bool s_skyFactsRotationPaused = false;
int s_skyFactsLastTheme = -1;
int s_skyFactsLastDateKey = -1;
int s_skyFactsLastMinute = -1;
size_t s_skyFactsLastCount = 0;
int s_summaryMarqueeOffset = 0;
int s_summaryMarqueeWidth = 0;
unsigned long s_summaryMarqueeLastStepMs = 0;
unsigned long s_summaryMarqueeStartAfterMs = 0;
unsigned long s_summaryStepMs = 0;
char s_summaryLastText[192] = "";

uint16_t titleBg()
{
    return ui_theme::isNightTheme() ? ui_theme::monoHeaderBg() : ui_theme::infoScreenHeaderBg();
}

uint16_t titleFg()
{
    return ui_theme::isNightTheme() ? ui_theme::monoHeaderFg() : ui_theme::infoScreenHeaderFg();
}

uint16_t bodyColor()
{
    return ui_theme::isNightTheme() ? ui_theme::monoBodyText() : ui_theme::infoValueDay();
}

uint16_t pageAccentColor(wxv::astronomy::SkyFactType type)
{
    switch (type)
    {
    case wxv::astronomy::SkyFactType::Season:
        return ui_theme::applyGraphicColor(dma_display->color565(110, 220, 120));
    case wxv::astronomy::SkyFactType::EquinoxSolstice:
        return ui_theme::applyGraphicColor(dma_display->color565(110, 220, 255));
    case wxv::astronomy::SkyFactType::Daylight:
        return ui_theme::applyGraphicColor(dma_display->color565(255, 225, 110));
    case wxv::astronomy::SkyFactType::SunCountdown:
        return ui_theme::applyGraphicColor(dma_display->color565(255, 190, 90));
    case wxv::astronomy::SkyFactType::Moon:
        return ui_theme::applyGraphicColor(dma_display->color565(214, 226, 250));
    case wxv::astronomy::SkyFactType::Summary:
    case wxv::astronomy::SkyFactType::YearProgress:
    default:
        return ui_theme::applyGraphicColor(dma_display->color565(170, 220, 235));
    }
}

uint16_t trendColor(int8_t trend)
{
    if (trend > 0)
        return ui_theme::applyGraphicColor(dma_display->color565(90, 220, 110));
    if (trend < 0)
        return ui_theme::applyGraphicColor(dma_display->color565(255, 120, 90));
    return bodyColor();
}

void drawHeader(const char *title)
{
    dma_display->fillScreen(0);
    dma_display->fillRect(0, 0, PANEL_RES_X, 8, titleBg());
    dma_display->drawFastHLine(0, 7, PANEL_RES_X, ui_theme::isNightTheme() ? ui_theme::monoUnderline() : ui_theme::infoUnderlineDay());
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    dma_display->setTextColor(titleFg());

    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    int x = (PANEL_RES_X - static_cast<int>(w)) / 2;
    if (x < 0)
        x = 0;
    dma_display->setCursor(x, 0);
    dma_display->print(title);
}

void drawCenteredLine(int y, const char *text, uint16_t color)
{
    if (!text || text[0] == '\0')
        return;

    dma_display->setTextColor(color);
    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    int x = (PANEL_RES_X - static_cast<int>(w)) / 2;
    if (x < 0)
        x = 0;
    dma_display->setCursor(x, y);
    dma_display->print(text);
}

void syncSummaryMarqueeState(const wxv::astronomy::SkyFactPage &page, bool forceReset)
{
    const bool textChanged = strncmp(s_summaryLastText, page.marquee, sizeof(s_summaryLastText)) != 0;
    if (forceReset || textChanged)
    {
        const bool hadExistingText = s_summaryLastText[0] != '\0';
        const int previousCycle = PANEL_RES_X + s_summaryMarqueeWidth + kSummaryRepeatGapPx + kSummaryEntryInsetPx;
        snprintf(s_summaryLastText, sizeof(s_summaryLastText), "%s", page.marquee);
        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);
        int16_t x1, y1;
        uint16_t w, h;
        dma_display->getTextBounds(page.marquee, 0, 0, &x1, &y1, &w, &h);
        s_summaryMarqueeWidth = static_cast<int>(w);
        const int nextCycle = PANEL_RES_X + s_summaryMarqueeWidth + kSummaryRepeatGapPx + kSummaryEntryInsetPx;
        if (forceReset || previousCycle <= 0 || nextCycle <= 0)
            s_summaryMarqueeOffset = 0;
        else
            s_summaryMarqueeOffset %= nextCycle;
        const unsigned long nowMs = millis();
        if (forceReset || !hadExistingText)
        {
            s_summaryMarqueeLastStepMs = nowMs;
            s_summaryMarqueeStartAfterMs = nowMs + kSummaryStartDelayMs;
        }
        else
        {
            // Preserve current motion when only the content changes.
            if (s_summaryMarqueeLastStepMs == 0)
                s_summaryMarqueeLastStepMs = nowMs;
            if (s_summaryMarqueeStartAfterMs < nowMs)
                s_summaryMarqueeStartAfterMs = nowMs;
        }
    }
}

void drawSummaryPage(const wxv::astronomy::SkyFactPage &page)
{
    drawHeader(page.title[0] ? page.title : "SKY BRIEF");
    dma_display->fillRect(0, 12, PANEL_RES_X, 20, myBLACK);

    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    dma_display->setTextColor(bodyColor());
    if (page.marquee[0] != '\0')
    {
        const int textX = PANEL_RES_X - kSummaryEntryInsetPx - s_summaryMarqueeOffset;
        dma_display->setCursor(textX, 20);
        dma_display->print(page.marquee);
        dma_display->setCursor(textX + s_summaryMarqueeWidth + kSummaryRepeatGapPx + kSummaryEntryInsetPx, 20);
        dma_display->print(page.marquee);
    }
}

void resetSummaryMarquee()
{
    s_summaryMarqueeOffset = 0;
    s_summaryMarqueeWidth = 0;
    s_summaryMarqueeLastStepMs = 0;
    s_summaryMarqueeStartAfterMs = 0;
    s_summaryLastText[0] = '\0';
}

unsigned long defaultSummaryStepMs()
{
    return static_cast<unsigned long>(constrain(scrollSpeed, 40, 120));
}

unsigned long effectiveSummaryStepMs()
{
    return (s_summaryStepMs > 0) ? s_summaryStepMs : defaultSummaryStepMs();
}

int summaryCycleWidth()
{
    return PANEL_RES_X + s_summaryMarqueeWidth + kSummaryRepeatGapPx + kSummaryEntryInsetPx;
}

bool speedUpSummaryStep()
{
    const int current = static_cast<int>(effectiveSummaryStepMs());
    const int next = constrain((current * 7) / 8, static_cast<int>(kRenderSkySummaryMs), 180);
    if (next == current)
        return false;
    s_summaryStepMs = static_cast<unsigned long>(next);
    return true;
}

bool slowDownSummaryStep()
{
    const int current = static_cast<int>(effectiveSummaryStepMs());
    const int next = constrain(((current * 9) + 7) / 8, static_cast<int>(kRenderSkySummaryMs), 180);
    if (next == current)
        return false;
    s_summaryStepMs = static_cast<unsigned long>(next);
    return true;
}

void drawSkyFactPageImpl(const wxv::astronomy::SkyFactPage &page)
{
    if (page.type == wxv::astronomy::SkyFactType::Summary)
    {
        syncSummaryMarqueeState(page, false);
        drawSummaryPage(page);
        return;
    }

    drawHeader(page.title[0] ? page.title : "SKY");
    if (!page.valid)
    {
        drawCenteredLine(16, "No sky facts", bodyColor());
        return;
    }

    const uint16_t accent = pageAccentColor(page.type);
    const uint16_t normal = bodyColor();

    auto drawArrow = [&](int x, int y, int8_t trend, uint16_t color)
    {
        if (trend > 0)
        {
            dma_display->drawLine(x + 3, y + 1, x + 1, y + 3, color);
            dma_display->drawLine(x + 3, y + 1, x + 5, y + 3, color);
            dma_display->drawLine(x + 3, y + 1, x + 3, y + 7, color);
        }
        else if (trend < 0)
        {
            dma_display->drawLine(x + 3, y + 7, x + 1, y + 5, color);
            dma_display->drawLine(x + 3, y + 7, x + 5, y + 5, color);
            dma_display->drawLine(x + 3, y + 1, x + 3, y + 7, color);
        }
    };

    auto drawSeasonMeter = [&](int x, int y, uint8_t fill, uint8_t total)
    {
        if (total == 0)
            return;
        const int barW = 58;
        const int barH = 4;
        const uint16_t remainingColor = ui_theme::isNightTheme()
                                            ? ui_theme::applyGraphicColor(dma_display->color565(48, 56, 68))
                                            : ui_theme::applyGraphicColor(dma_display->color565(52, 72, 92));
        const uint16_t borderColor = ui_theme::applyGraphicColor(dma_display->color565(95, 120, 145));
        const uint16_t elapsedColor = accent;
        const int innerW = barW - 2;
        const int fillW = constrain((innerW * static_cast<int>(fill) + static_cast<int>(total) / 2) / static_cast<int>(total), 0, innerW);

        dma_display->drawRect(x, y, barW, barH, borderColor);
        if (innerW <= 0 || barH <= 2)
            return;

        dma_display->fillRect(x + 1, y + 1, innerW, barH - 2, remainingColor);
        if (fillW > 0)
            dma_display->fillRect(x + 1, y + 1, fillW, barH - 2, elapsedColor);
    };

    auto drawMoonIcon = [&](int cx, int cy)
    {
        const wxv::astronomy::AstronomyData &astro = wxv::astronomy::astronomyData();
        const uint16_t dark = ui_theme::applyGraphicColor(dma_display->color565(30, 36, 48));
        const uint16_t lit = accent;
        const uint16_t rim = ui_theme::applyGraphicColor(dma_display->color565(245, 235, 180));
        const int r = 4;
        const float wrapped = astro.moonPhaseFraction - floorf(astro.moonPhaseFraction);
        const float phaseAngle = wrapped * 2.0f * 3.14159265f;
        const float k = cosf(phaseAngle);
        const bool waxing = wrapped <= 0.5f;

        for (int dy = -r; dy <= r; ++dy)
        {
            for (int dx = -r; dx <= r; ++dx)
            {
                const float xn = static_cast<float>(dx) / static_cast<float>(r);
                const float yn = static_cast<float>(dy) / static_cast<float>(r);
                if (xn * xn + yn * yn > 1.0f)
                    continue;
                const float limb = sqrtf(fmaxf(0.0f, 1.0f - yn * yn));
                const bool on = waxing ? (xn >= k * limb) : (xn <= -k * limb);
                dma_display->drawPixel(cx + dx, cy + dy, on ? lit : dark);
            }
        }
        dma_display->drawCircle(cx, cy, r, rim);
    };

    switch (page.type)
    {
    case wxv::astronomy::SkyFactType::Season:
        drawCenteredLine(10, page.line1, accent);
        drawSeasonMeter(3, 18, page.meterFill, page.meterTotal);
        drawCenteredLine(23, page.line2, normal);
        break;
    case wxv::astronomy::SkyFactType::Daylight:
    {
        const char *trendText = page.line2;
        if (trendText[0] == '+' || trendText[0] == '-')
            ++trendText;
        drawCenteredLine(12, page.line1, accent);
        drawCenteredLine(21, trendText, trendColor(page.trend));
        if (trendText[0] != '\0')
            drawArrow(8, 20, page.trend, trendColor(page.trend));
        break;
    }
    case wxv::astronomy::SkyFactType::SunCountdown:
        drawCenteredLine(12, page.line1, accent);
        drawCenteredLine(21, page.line2, normal);
        drawArrow(8, 11, page.trend, accent);
        break;
    case wxv::astronomy::SkyFactType::Moon:
        drawMoonIcon(12, 18);
        dma_display->setTextColor(accent);
        dma_display->setCursor(21, 12);
        dma_display->print(page.line1);
        dma_display->setTextColor(normal);
        dma_display->setCursor(16, 22);
        dma_display->print(page.line2);
        break;
    default:
        switch (page.lineCount)
        {
        case 1:
            drawCenteredLine(16, page.line1, accent);
            break;
        case 2:
            drawCenteredLine(12, page.line1, accent);
            drawCenteredLine(21, page.line2, normal);
            break;
        default:
            drawCenteredLine(10, page.line1, accent);
            drawCenteredLine(17, page.line2, normal);
            drawCenteredLine(24, page.line3, normal);
            break;
        }
        break;
    }
}
} // namespace

void drawSkyBriefScreen()
{
    wxv::astronomy::updateSkyFacts();
    const wxv::astronomy::AstronomyData &astro = wxv::astronomy::astronomyData();
    s_skyFactsLastTheme = theme;
    s_skyFactsLastDateKey = astro.localDateKey;
    s_skyFactsLastMinute = astro.localMinutes;
    s_skyFactsLastCount = 1;

    const wxv::astronomy::SkyFactPage &page = wxv::astronomy::skySummaryPage();
    syncSummaryMarqueeState(page, false);
    drawSummaryPage(page);
}

void tickSkyBriefScreen()
{
    wxv::astronomy::updateSkyFacts();
    const wxv::astronomy::AstronomyData &astro = wxv::astronomy::astronomyData();
    const unsigned long nowMs = millis();
    const wxv::astronomy::SkyFactPage &page = wxv::astronomy::skySummaryPage();

    if (theme != s_skyFactsLastTheme ||
        astro.localDateKey != s_skyFactsLastDateKey ||
        astro.localMinutes != s_skyFactsLastMinute)
    {
        drawSkyBriefScreen();
        return;
    }

    if (page.marquee[0] == '\0')
        return;

    const unsigned long summaryStepMs = effectiveSummaryStepMs();
    if (nowMs >= s_summaryMarqueeStartAfterMs &&
        nowMs - s_summaryMarqueeLastStepMs >= summaryStepMs)
    {
        const unsigned long elapsed = nowMs - s_summaryMarqueeLastStepMs;
        const unsigned long steps = elapsed / summaryStepMs;
        s_summaryMarqueeLastStepMs += steps * summaryStepMs;
        s_summaryMarqueeOffset += static_cast<int>(steps);
        const int cycle = summaryCycleWidth();
        if (cycle > 0)
            s_summaryMarqueeOffset %= cycle;
        drawSummaryPage(page);
    }
}

void drawSkyFactsScreen()
{
    wxv::astronomy::updateSkyFacts();
    const size_t count = wxv::astronomy::skyFactCount();
    const wxv::astronomy::AstronomyData &astro = wxv::astronomy::astronomyData();
    s_skyFactsLastTheme = theme;
    s_skyFactsLastDateKey = astro.localDateKey;
    s_skyFactsLastMinute = astro.localMinutes;
    s_skyFactsLastCount = count;

    if (count == 0)
    {
        wxv::astronomy::SkyFactPage empty;
        snprintf(empty.title, sizeof(empty.title), "%s", "SKY");
        snprintf(empty.line1, sizeof(empty.line1), "%s", "Facts unavailable");
        empty.valid = true;
        empty.lineCount = 1;
        drawSkyFactPageImpl(empty);
        return;
    }

    if (s_skyFactsPageIndex >= count)
        s_skyFactsPageIndex = 0;
    drawSkyFactPageImpl(wxv::astronomy::skyFactPage(s_skyFactsPageIndex));
}

void tickSkyFactsScreen()
{
    wxv::astronomy::updateSkyFacts();
    const wxv::astronomy::AstronomyData &astro = wxv::astronomy::astronomyData();
    const size_t count = wxv::astronomy::skyFactCount();
    const unsigned long nowMs = millis();

    if (theme != s_skyFactsLastTheme ||
        astro.localDateKey != s_skyFactsLastDateKey ||
        astro.localMinutes != s_skyFactsLastMinute ||
        count != s_skyFactsLastCount)
    {
        drawSkyFactsScreen();
        return;
    }

    if (s_skyFactsLastSwitchMs == 0)
        s_skyFactsLastSwitchMs = nowMs;

    if (s_skyFactsRotationPaused || count <= 1)
    {
        const wxv::astronomy::SkyFactPage &page = wxv::astronomy::skyFactPage(s_skyFactsPageIndex);
        if (page.type == wxv::astronomy::SkyFactType::Summary &&
            page.marquee[0] != '\0' &&
            s_summaryMarqueeWidth > PANEL_RES_X &&
            nowMs >= s_summaryMarqueeStartAfterMs &&
            nowMs - s_summaryMarqueeLastStepMs >= effectiveSummaryStepMs())
        {
            const unsigned long summaryStepMs = effectiveSummaryStepMs();
            const unsigned long elapsed = nowMs - s_summaryMarqueeLastStepMs;
            const unsigned long steps = elapsed / summaryStepMs;
            s_summaryMarqueeLastStepMs += steps * summaryStepMs;
            s_summaryMarqueeOffset += static_cast<int>(steps);
            const int cycle = summaryCycleWidth();
            if (cycle > 0)
                s_summaryMarqueeOffset %= cycle;
            drawSummaryPage(page);
        }
        return;
    }

    if (nowMs - s_skyFactsLastSwitchMs >= kSkyFactsPageAutoMs)
    {
        s_skyFactsPageIndex = static_cast<uint8_t>((s_skyFactsPageIndex + 1u) % count);
        s_skyFactsLastSwitchMs = nowMs;
        resetSummaryMarquee();
        drawSkyFactsScreen();
        return;
    }

    const wxv::astronomy::SkyFactPage &page = wxv::astronomy::skyFactPage(s_skyFactsPageIndex);
    if (page.type == wxv::astronomy::SkyFactType::Summary &&
        page.marquee[0] != '\0' &&
        s_summaryMarqueeWidth > PANEL_RES_X &&
        nowMs >= s_summaryMarqueeStartAfterMs &&
        nowMs - s_summaryMarqueeLastStepMs >= effectiveSummaryStepMs())
    {
        const unsigned long summaryStepMs = effectiveSummaryStepMs();
        const unsigned long elapsed = nowMs - s_summaryMarqueeLastStepMs;
        const unsigned long steps = elapsed / summaryStepMs;
        s_summaryMarqueeLastStepMs += steps * summaryStepMs;
        s_summaryMarqueeOffset += static_cast<int>(steps);
        const int cycle = summaryCycleWidth();
        if (cycle > 0)
            s_summaryMarqueeOffset %= cycle;
        drawSummaryPage(page);
    }
}

void handleSkyFactsDownPress()
{
    const size_t count = wxv::astronomy::skyFactCount();
    if (count <= 1)
        return;
    s_skyFactsPageIndex = static_cast<uint8_t>((s_skyFactsPageIndex + 1u) % count);
    s_skyFactsLastSwitchMs = millis();
    resetSummaryMarquee();
    drawSkyFactsScreen();
}

void handleSkyFactsUpPress()
{
    const size_t count = wxv::astronomy::skyFactCount();
    if (count <= 1)
        return;
    int next = static_cast<int>(s_skyFactsPageIndex) - 1;
    if (next < 0)
        next = static_cast<int>(count) - 1;
    s_skyFactsPageIndex = static_cast<uint8_t>(next);
    s_skyFactsLastSwitchMs = millis();
    resetSummaryMarquee();
    drawSkyFactsScreen();
}

void handleSkyFactsSelectPress()
{
    s_skyFactsRotationPaused = !s_skyFactsRotationPaused;
    s_skyFactsLastSwitchMs = millis();
    drawSkyFactsScreen();
}

void resetSkyFactsScreenState()
{
    s_skyFactsPageIndex = 0;
    s_skyFactsLastSwitchMs = millis();
    s_skyFactsRotationPaused = false;
    s_skyFactsLastTheme = -1;
    s_skyFactsLastDateKey = -1;
    s_skyFactsLastMinute = -1;
    s_skyFactsLastCount = 0;
    resetSummaryMarquee();
}

void resetSkyBriefScreenState()
{
    resetSummaryMarquee();
}

void handleSkyBriefDownPress()
{
    if (!slowDownSummaryStep())
        return;
    s_summaryMarqueeLastStepMs = millis();
    s_summaryMarqueeStartAfterMs = s_summaryMarqueeLastStepMs;
    drawSkyBriefScreen();
}

void handleSkyBriefUpPress()
{
    if (!speedUpSummaryStep())
        return;
    s_summaryMarqueeLastStepMs = millis();
    s_summaryMarqueeStartAfterMs = s_summaryMarqueeLastStepMs;
    drawSkyBriefScreen();
}

void drawSkyFactSubpage(const wxv::astronomy::SkyFactPage &page)
{
    drawSkyFactPageImpl(page);
}
