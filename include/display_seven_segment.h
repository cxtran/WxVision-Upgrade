#pragma once

#include <stdint.h>

namespace wxv {
namespace seg7 {

struct Metrics
{
    int digitWidth;
    int digitHeight;
    int colonWidth;
    int spacing;
};

Metrics metricsForThickness(int thickness);
void drawSegment(int x, int y, bool horizontal, int length, uint16_t color, int thickness);
void drawDigit(int x, int y, char digit, uint16_t color, int thickness, const Metrics &metrics);
void drawColon(int x, int y, uint16_t color, int thickness, const Metrics &metrics);
int measureTime(int hour24, int minute, bool use24h, int thickness, const Metrics *metrics = nullptr);
int drawTime(int x, int y, int hour24, int minute, bool use24h, uint16_t color, int thickness,
             const Metrics *metrics = nullptr, bool colonVisible = true);

} // namespace seg7
} // namespace wxv
