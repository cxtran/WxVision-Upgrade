#pragma once

#include <Arduino.h>

enum class RenderSlot : uint8_t
{
    ClockMain,
    ClockPulse,
    ClockMarquee,
    WorldClockMain,
    AstronomyMain,
    AstronomyTick,
    SkyBriefMain,
    SkyBriefTick,
    LunarLuckMain,
    LunarLuckMarquee,
    ConditionMain,
    ConditionMarquee,
    TempHistoryMain,
    TempHistoryMarquee,
    HumHistoryMain,
    HumHistoryMarquee,
    Co2HistoryMain,
    Co2HistoryMarquee,
    PredictMain,
    PredictMarquee,
    NoaaMain,
    NoaaTick,
    BaroHistoryMain,
    BaroHistoryMarquee,
    WindMain,
    OwmMain,
    OwmScroll,
    Count
};

bool renderDue(RenderSlot slot, unsigned long now, unsigned long intervalMs, bool force = false);
void markRendered(RenderSlot slot, unsigned long now);
void noteFrameDraw(unsigned long now);
void noteFullClear();

constexpr unsigned long kRenderMarqueeMs = 40UL;     // 25 FPS
constexpr unsigned long kRenderSkySummaryMs = 8UL;   // higher cadence for smoother summary marquee motion
constexpr unsigned long kRenderWorldClockMs = 40UL;  // 25 FPS for long-run stability
constexpr unsigned long kRenderChartMs = 15000UL;    // preserve existing behavior
constexpr unsigned long kRenderConditionMs = 5000UL; // preserve existing behavior
constexpr unsigned long kRenderWindMs = 40UL;        // 25 FPS
constexpr unsigned long kRenderOwmMainMs = 1000UL;   // preserve existing behavior
