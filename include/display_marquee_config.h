#pragma once

static constexpr const char *kConditionMarqueeSeparator = " \xC2\xA6 ";
static constexpr unsigned int kConditionMarqueeDefaultSpeedMs = 60U;
static constexpr int kConditionMarqueeStepPx = 1;
static constexpr unsigned int kWorldHeaderMarqueeDefaultSpeedMs = 40U;
static constexpr unsigned int kWorldHeaderMarqueeMinSpeedMs = 20U;
static constexpr int kWorldHeaderMarqueeStepPx = 1;

inline unsigned int normalizeConditionMarqueeSpeedMs(int configuredMs)
{
    return (configuredMs > 0) ? static_cast<unsigned int>(configuredMs) : kConditionMarqueeDefaultSpeedMs;
}

inline unsigned int normalizeWorldHeaderMarqueeSpeedMs(int configuredMs)
{
    unsigned int speed = (configuredMs > 0) ? static_cast<unsigned int>(configuredMs) : kWorldHeaderMarqueeDefaultSpeedMs;
    if (speed < kWorldHeaderMarqueeMinSpeedMs)
        speed = kWorldHeaderMarqueeMinSpeedMs;
    return speed;
}
