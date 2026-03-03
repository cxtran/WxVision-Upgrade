#include <Arduino.h>
#include "display.h"
#include "display_worldtime.h"
#include "worldtime.h"
#include "ScrollLine.h"
#include "settings.h"
#include "display_marquee_config.h"
#include "ui_theme.h"
namespace
{
    bool s_clockWorldHeaderEnabled = false;
    String s_clockWorldHeaderText;
    ScrollLine s_clockWorldHeaderScroll(PANEL_RES_X, kWorldHeaderMarqueeDefaultSpeedMs);
    bool s_clockWorldHeaderNeedsRedraw = false;
    unsigned long s_clockWorldHeaderLastStepMs = 0;
    bool worldHeaderNeedsScroll()
    {
        return (static_cast<int>(s_clockWorldHeaderText.length()) * 6) > PANEL_RES_X;
    }
    void drawClockWorldHeaderLine(bool forceDraw = false)
    {
        if (!s_clockWorldHeaderEnabled)
            return;
        const bool needsScroll = worldHeaderNeedsScroll();
        const unsigned long nowMs = millis();
        const unsigned long stepMs = normalizeWorldHeaderMarqueeSpeedMs(scrollSpeed);
        bool shouldDraw = forceDraw || s_clockWorldHeaderNeedsRedraw;
        if (needsScroll && (nowMs - s_clockWorldHeaderLastStepMs >= stepMs))
        {
            s_clockWorldHeaderScroll.update();
            s_clockWorldHeaderLastStepMs = nowMs;
            shouldDraw = true;
        }
        if (!shouldDraw)
            return;
        uint16_t lineColor = (theme == 1) ? ui_theme::worldHeaderNight() : ui_theme::worldHeaderDay();
        s_clockWorldHeaderScroll.setScrollSpeed(normalizeWorldHeaderMarqueeSpeedMs(scrollSpeed));
        s_clockWorldHeaderScroll.setScrollStepPx(kWorldHeaderMarqueeStepPx);
        uint16_t textColors[] = {lineColor};
        uint16_t bgColors[] = {myBLACK};
        s_clockWorldHeaderScroll.setLineColors(textColors, bgColors, 1);
        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);
        s_clockWorldHeaderScroll.draw(0, 0, lineColor);
        s_clockWorldHeaderNeedsRedraw = false;
    }
}
void worldTimeHeaderSync(bool worldView)
{
    if (worldView)
    {
        String worldHeader = worldTimeBuildCurrentHeaderText();
        if (!s_clockWorldHeaderEnabled)
        {
            s_clockWorldHeaderEnabled = true;
            s_clockWorldHeaderNeedsRedraw = true;
            s_clockWorldHeaderLastStepMs = millis();
        }
        if (worldHeader != s_clockWorldHeaderText)
        {
            s_clockWorldHeaderText = worldHeader;
            String lines[] = {s_clockWorldHeaderText};
            s_clockWorldHeaderScroll.setLines(lines, 1, true);
            s_clockWorldHeaderScroll.setScrollSpeed(normalizeWorldHeaderMarqueeSpeedMs(scrollSpeed));
            s_clockWorldHeaderScroll.setScrollStepPx(kWorldHeaderMarqueeStepPx);
            s_clockWorldHeaderNeedsRedraw = true;
            s_clockWorldHeaderLastStepMs = millis();
        }
        drawClockWorldHeaderLine(true);
        return;
    }
    s_clockWorldHeaderEnabled = false;
    s_clockWorldHeaderNeedsRedraw = false;
}
void tickClockWorldTimeMarquee()
{
    if (!s_clockWorldHeaderEnabled)
        return;
    drawClockWorldHeaderLine();
}
