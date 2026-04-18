#pragma once

#include <Arduino.h>
#include <vector>
#include "psram_utils.h"

class Adafruit_GFX;

// Simple vertical scrolling helper: scrolls lines upward at pixel-level granularity
class RollingUpScreen
{
public:
    using StringCache = std::vector<String, wxv::memory::PsramAllocator<String>>;
    using U16Cache = std::vector<uint16_t, wxv::memory::PsramAllocator<uint16_t>>;
    using IntCache = std::vector<int, wxv::memory::PsramAllocator<int>>;
    using PtrCache = std::vector<const uint8_t *, wxv::memory::PsramAllocator<const uint8_t *>>;
    using ByteCache = std::vector<uint8_t, wxv::memory::PsramAllocator<uint8_t>>;
    using TimeCache = std::vector<unsigned long, wxv::memory::PsramAllocator<unsigned long>>;

    RollingUpScreen(int width, int defaultHeight, int lineHeight = 8);

    template <typename TStringVector>
    void setLines(const TStringVector &lines, bool resetPosition = true)
    {
        _lines.assign(lines.begin(), lines.end());
        _lineMarqueeOffsets.assign(_lines.size(), 0);
        _lineMarqueeLastTick.assign(_lines.size(), millis());
        _lineMarqueeDone.assign(_lines.size(), 0);
        if (resetPosition)
        {
            reset();
        }
    }

    void setScrollSpeed(unsigned int ms);
    template <typename TU16Vector>
    void setLineColors(const TU16Vector &colors)
    {
        _lineColors.assign(colors.begin(), colors.end());
    }
    // Optional per-line X offsets (pixels). Missing entries default to 0.
    template <typename TIntVector>
    void setLineOffsets(const TIntVector &offsets)
    {
        _lineOffsets.assign(offsets.begin(), offsets.end());
    }
    // Optional per-line icon X offsets (pixels). Missing entries fall back to line offsets.
    template <typename TIntVector>
    void setLineIconOffsets(const TIntVector &iconOffsets)
    {
        _lineIconOffsets.assign(iconOffsets.begin(), iconOffsets.end());
    }
    // Optional per-line Y offsets (pixels). Missing entries default to 0.
    template <typename TIntVector>
    void setLineYOffsets(const TIntVector &yOffsets)
    {
        _lineYOffsets.assign(yOffsets.begin(), yOffsets.end());
    }
    // Optional per-line icons and colors (16x16 bitmaps). Missing entries are ignored.
    template <typename TPtrVector, typename TU16Vector>
    void setLineIcons(const TPtrVector &icons, const TU16Vector &iconColors)
    {
        _lineIcons.assign(icons.begin(), icons.end());
        _iconColors.assign(iconColors.begin(), iconColors.end());
    }
    // Optional per-line marquee flags. Non-zero enables horizontal marquee for that line when overflowed.
    template <typename TByteVector>
    void setLineMarqueeFlags(const TByteVector &flags)
    {
        _lineMarqueeFlags.assign(flags.begin(), flags.end());
        if (_lineMarqueeOffsets.size() != _lines.size())
        {
            _lineMarqueeOffsets.assign(_lines.size(), 0);
            _lineMarqueeLastTick.assign(_lines.size(), millis());
            _lineMarqueeDone.assign(_lines.size(), 0);
        }
    }
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
    StringCache _lines;
    int _entryY;
    int _exitY;
    U16Cache _lineColors;
    IntCache _lineOffsets;
    IntCache _lineIconOffsets;
    IntCache _lineYOffsets;
    PtrCache _lineIcons;
    U16Cache _iconColors;
    ByteCache _lineMarqueeFlags;
    IntCache _lineMarqueeOffsets;
    TimeCache _lineMarqueeLastTick;
    ByteCache _lineMarqueeDone;
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
