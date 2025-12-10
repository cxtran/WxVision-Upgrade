#pragma once

#include <Arduino.h>
#include <vector>

class Adafruit_GFX;

// Simple vertical scrolling helper: scrolls lines upward at pixel-level granularity
class RollingUpScreen
{
public:
    RollingUpScreen(int width, int defaultHeight, int lineHeight = 8);

    void setLines(const std::vector<String> &lines, bool resetPosition = true);
    void setScrollSpeed(unsigned int ms);
    void setLineColors(const std::vector<uint16_t> &colors);
    void setPaused(bool paused);
    bool isPaused() const { return _paused; }
    // Optional gap (ms) to pause between cycles (default 1000 ms)
    void setGapHoldMs(unsigned int ms);
    // Configure where the text enters and exits (absolute Y coordinates).
    // If not set, entry defaults to (body top + height) and exit to body top passed to draw().
    void setEntryExit(int entryY, int exitY);
    void reset();

    void update();
    void draw(Adafruit_GFX &display, int x, int y, int height, uint16_t color);

private:
    int _width;
    int _defaultHeight;
    int _lineHeight;
    unsigned int _scrollSpeedMs;
    unsigned long _lastTick;
    int _offsetPx;
    std::vector<String> _lines;
    int _entryY;
    int _exitY;
    std::vector<uint16_t> _lineColors;
    bool _paused;
    unsigned int _gapHoldMs;
    unsigned long _gapHoldUntil;
};
