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
    // Optional per-line X offsets (pixels). Missing entries default to 0.
    void setLineOffsets(const std::vector<int> &offsets);
    // Optional per-line Y offsets (pixels). Missing entries default to 0.
    void setLineYOffsets(const std::vector<int> &yOffsets);
    // Optional per-line icons and colors (16x16 bitmaps). Missing entries are ignored.
    void setLineIcons(const std::vector<const uint8_t *> &icons, const std::vector<uint16_t> &iconColors);
    void setPaused(bool paused);
    // Optionally treat groups of pixels as a block for pause logic (e.g., 3 lines * 8px = 24px block)
    void setBlockSizePx(int px);
    // Stop/resume helpers for user input
    void onDownPress();  // single press steps view downward by 1px and pauses
    void onUpPress();    // resume scrolling up immediately
    void setAutoResumeMs(unsigned int ms); // default 5000 ms
    bool isPaused() const { return _paused; }
    // Optional gap (ms) to pause between cycles (default 1000 ms)
    void setGapHoldMs(unsigned int ms);
    // Optional pause (ms) when the leading edge reaches the exit line (header).
    void setExitHoldMs(unsigned int ms);
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
    std::vector<int> _lineOffsets;
    std::vector<int> _lineYOffsets;
    std::vector<const uint8_t *> _lineIcons;
    std::vector<uint16_t> _iconColors;
    bool _paused;
    unsigned int _gapHoldMs;
    unsigned long _gapHoldUntil;
    unsigned long _resumeAt;
    unsigned int _autoResumeMs;
    unsigned int _exitHoldMs;
    int _blockSizePx;
};
