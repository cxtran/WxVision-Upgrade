#include "RollingUpScreen.h"
#include <Adafruit_GFX.h>
#include "Font5x7Uts.h"
#include "settings.h" // for verticalScrollSpeed default

extern int theme;

RollingUpScreen::RollingUpScreen(int width, int defaultHeight, int lineHeight)
    : _width(width),
      _defaultHeight(defaultHeight),
      _lineHeight(lineHeight),
      _scrollSpeedMs((verticalScrollSpeed > 0) ? static_cast<unsigned int>(verticalScrollSpeed) : 60u),
      _lastTick(0),
      _offsetPx(0),
      _entryY(-1),
      _exitY(-1),
      _paused(false),
      _gapHoldMs(1000),
      _gapHoldUntil(0),
      _resumeAt(0),
      _autoResumeMs(5000)
{
}

void RollingUpScreen::setLines(const std::vector<String> &lines, bool resetPosition)
{
    _lines = lines;
    if (resetPosition)
    {
        reset();
    }
}

void RollingUpScreen::setScrollSpeed(unsigned int ms)
{
    _scrollSpeedMs = ms;
}

void RollingUpScreen::setLineColors(const std::vector<uint16_t> &colors)
{
    _lineColors = colors;
}

void RollingUpScreen::setLineOffsets(const std::vector<int> &offsets)
{
    _lineOffsets = offsets;
}

void RollingUpScreen::setPaused(bool paused)
{
    _paused = paused;
}

void RollingUpScreen::onDownPress()
{
    unsigned long now = millis();
    // Step one pixel downward (reverse of normal motion) and pause
    int totalHeight = static_cast<int>(_lines.size()) * _lineHeight;
    int gap = 24;
    int cycle = totalHeight + gap;
    if (cycle <= 0) cycle = 1;
    if (_offsetPx == 0)
        _offsetPx = cycle - 1;
    else
        _offsetPx = (_offsetPx - 1) % cycle;
    _paused = true;
    _resumeAt = now + _autoResumeMs;
}

void RollingUpScreen::onUpPress()
{
    _paused = false;
    _resumeAt = 0;
}

void RollingUpScreen::setAutoResumeMs(unsigned int ms)
{
    _autoResumeMs = ms;
}

void RollingUpScreen::setGapHoldMs(unsigned int ms)
{
    _gapHoldMs = ms;
}

void RollingUpScreen::setEntryExit(int entryY, int exitY)
{
    _entryY = entryY;
    _exitY = exitY;
}

void RollingUpScreen::reset()
{
    _offsetPx = 0;
    _lastTick = millis();
    _resumeAt = 0;
    _paused = false;
}

void RollingUpScreen::update()
{
    if (_lines.empty())
        return;

    // Keep speed in sync with global vertical setting each tick
    _scrollSpeedMs = (verticalScrollSpeed > 0) ? static_cast<unsigned int>(verticalScrollSpeed) : 60u;

    unsigned long now = millis();

    // Auto-resume after timeout
    if (_resumeAt != 0 && now >= _resumeAt)
    {
        _paused = false;
        _resumeAt = 0;
    }

    if (_paused)
        return;

    if (_gapHoldUntil != 0 && now < _gapHoldUntil)
        return;
    if (_gapHoldUntil != 0 && now >= _gapHoldUntil)
        _gapHoldUntil = 0;

    if (now - _lastTick >= _scrollSpeedMs)
    {
        int totalHeight = static_cast<int>(_lines.size()) * _lineHeight;
        int travelHeight = (_entryY >= 0 && _exitY >= 0)
                               ? abs(_entryY - _exitY)
                               : (_defaultHeight > 0 ? _defaultHeight : totalHeight);
        // Ensure travel covers at least the visible window
        if (_defaultHeight > 0)
            travelHeight = max(travelHeight, _defaultHeight);
        int gap = 24; // fixed 24px gap between cycles
        // Single gap cycle to keep at least one copy visible while exiting/entering
        int cycle = totalHeight + gap;
        if (cycle <= 0) cycle = 1;
        int next = _offsetPx + 1;
        if (next >= cycle)
        {
            _offsetPx = 0;
            _gapHoldUntil = now + _gapHoldMs; // hold for configured gap (default 1s)
        }
        else
        {
            _offsetPx = next;
        }
        _lastTick = now;
    }
}

void RollingUpScreen::draw(Adafruit_GFX &display, int x, int y, int height, uint16_t color)
{
    if (_lines.empty())
        return;

    const bool mono = (theme == 1);
    // Use a consistent mono accent when in mono theme; otherwise honor caller color
    const uint16_t themeColor = mono ? 0x6B6D /* soft blue-grey */ : color;

    int h = (height > 0) ? height : _defaultHeight;
    if (h <= 0) return;

    // Render into an offscreen canvas clipped to the body height, then blit. Keeps header clean.
    GFXcanvas16 canvas(_width, h);
    canvas.fillScreen(0);
    canvas.setTextWrap(false);
    canvas.setFont(&Font5x7Uts);
    canvas.setTextSize(1);

    int totalHeight = static_cast<int>(_lines.size()) * _lineHeight;
    // Enter from bottom of body; exit at absolute Y=8 unless caller overrides
    int effectiveEntry = (_entryY >= 0) ? _entryY : (y + h);
    int effectiveExit = (_exitY >= 0) ? _exitY : 8;
    int travel = abs(effectiveEntry - effectiveExit);
    if (travel <= 0)
        travel = (h > 0 ? h : totalHeight);
    if (h > 0)
        travel = max(travel, h);
    int gap = 24; // match update() gap
    int cycle = totalHeight + gap;
    if (cycle <= 0) cycle = 1;
    int phase = (_offsetPx % cycle);
    int direction = (effectiveEntry >= effectiveExit) ? 1 : -1;
    int startY = effectiveEntry - (phase * direction);

    auto drawPass = [&](int baseY) {
        int yy = baseY;
        for (size_t idx = 0; idx < _lines.size(); ++idx)
        {
            const auto &line = _lines[idx];
            // Translate to canvas coordinates
            int localY = yy - y;
            if (localY >= h) break;                  // past bottom
            if (localY + _lineHeight <= 0) {         // fully above visible band
                yy += _lineHeight;
                continue;
            }
            uint16_t useColor = (idx < _lineColors.size()) ? _lineColors[idx] : themeColor;
            if (mono) {
                useColor = themeColor;
            }
            int xOffset = (idx < _lineOffsets.size()) ? _lineOffsets[idx] : 0;
            canvas.setTextColor(useColor, 0);
            canvas.setCursor(xOffset, localY);
            canvas.print(line);
            yy += _lineHeight;
        }
    };

    drawPass(startY);
    // Second pass follows after totalHeight + gap to keep continuity with minimal blank time
    drawPass(startY + direction * (totalHeight + gap));

    // Blit the body canvas onto the display; header remains untouched.
    display.drawRGBBitmap(x, y, canvas.getBuffer(), _width, h);
}
