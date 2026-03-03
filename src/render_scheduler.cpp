#include "render_scheduler.h"

namespace
{
struct RenderSchedulerState
{
    unsigned long last[(size_t)RenderSlot::Count] = {};
};

RenderSchedulerState s_render;

#ifdef WXV_RENDER_STATS
struct RenderStats
{
    uint32_t frames = 0;
    uint32_t fullClears = 0;
    unsigned long lastReportMs = 0;
};

RenderStats s_renderStats;
#endif
} // namespace

bool renderDue(RenderSlot slot, unsigned long now, unsigned long intervalMs, bool force)
{
    unsigned long &last = s_render.last[(size_t)slot];
    if (force || intervalMs == 0 || (now - last) >= intervalMs)
    {
        last = now;
        return true;
    }
    return false;
}

void markRendered(RenderSlot slot, unsigned long now)
{
    s_render.last[(size_t)slot] = now;
}

void noteFrameDraw(unsigned long now)
{
#ifdef WXV_RENDER_STATS
    s_renderStats.frames++;
    if (now - s_renderStats.lastReportMs >= 10000UL)
    {
        Serial.printf("[Render] fps=%.2f fullClears=%lu\n",
                      s_renderStats.frames / 10.0f,
                      static_cast<unsigned long>(s_renderStats.fullClears));
        s_renderStats.frames = 0;
        s_renderStats.fullClears = 0;
        s_renderStats.lastReportMs = now;
    }
#else
    (void)now;
#endif
}

void noteFullClear()
{
#ifdef WXV_RENDER_STATS
    s_renderStats.fullClears++;
#endif
}
