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
    // Optional per-line icon X offsets (pixels). Missing entries fall back to line offsets.
    void setLineIconOffsets(const std::vector<int> &iconOffsets);
    // Optional per-line Y offsets (pixels). Missing entries default to 0.
    void setLineYOffsets(const std::vector<int> &yOffsets);
    // Optional per-line icons and colors (16x16 bitmaps). Missing entries are ignored.
    void setLineIcons(const std::vector<const uint8_t *> &icons, const std::vector<uint16_t> &iconColors);
    // Optional per-line marquee flags. Non-zero enables horizontal marquee for that line when overflowed.
    void setLineMarqueeFlags(const std::vector<uint8_t> &flags);
    void setPaused(bool paused);
    // Optionally treat groups of pixels as a block for pause logic (e.g., 3 lines * 8px = 24px block)
    void setBlockSizePx(int px);
    // Stop/resume helpers for user input
    void onDownPress();  // gradually slow scrolling until it stops
    void onUpPress();    // resume scrolling up at default speed
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
    int currentBlockIndex() const;

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
    std::vector<int> _lineIconOffsets;
    std::vector<int> _lineYOffsets;
    std::vector<const uint8_t *> _lineIcons;
    std::vector<uint16_t> _iconColors;
    std::vector<uint8_t> _lineMarqueeFlags;
    std::vector<int> _lineMarqueeOffsets;
    std::vector<unsigned long> _lineMarqueeLastTick;
    std::vector<uint8_t> _lineMarqueeDone;
    unsigned long _marqueeStartAfterMs;
    int _holdBlockIndex;
    bool _holdWaitForMarquee;
    bool _paused;
    unsigned int _gapHoldMs;
    unsigned long _gapHoldUntil;
    unsigned long _resumeAt;
    unsigned int _autoResumeMs;
    unsigned int _exitHoldMs;
    int _blockSizePx;
    bool _slowdownActive;
    unsigned int _dynamicScrollSpeedMs;
    unsigned int _slowdownStepMs;
    unsigned int _slowdownStopMs;
    unsigned long _slowdownStartMs;
    unsigned long _slowdownDurationMs;
    unsigned int _slowdownStartSpeedMs;
    unsigned int _slowdownEndSpeedMs;
    int _slowdownPresses;
    int _slowdownPressesToStop;
};
