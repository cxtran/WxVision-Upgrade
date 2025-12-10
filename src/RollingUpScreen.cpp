#include "RollingUpScreen.h"
#include <Adafruit_GFX.h>

RollingUpScreen::RollingUpScreen(int width, int defaultHeight, int lineHeight)
    : _width(width),
      _defaultHeight(defaultHeight),
      _lineHeight(lineHeight),
      _scrollSpeedMs(60),
      _lastTick(0),
      _offsetPx(0),
      _entryY(-1),
      _exitY(-1),
      _paused(false),
      _gapHoldMs(1000),
      _gapHoldUntil(0)
{
}

void RollingUpScreen::setLines(const std::vector<String> &lines, bool resetPosition)
{
    _lines = lines;
    if (resetPosition)
    {
        _offsetPx = 0;
        _lastTick = millis();
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

void RollingUpScreen::setPaused(bool paused)
{
    _paused = paused;
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
}

void RollingUpScreen::update()
{
    if (_lines.empty())
        return;
    if (_paused)
        return;

    unsigned long now = millis();
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

    int h = (height > 0) ? height : _defaultHeight;
    int totalHeight = static_cast<int>(_lines.size()) * _lineHeight;
    int effectiveEntry = (_entryY >= 0) ? _entryY : (y + h);
    int effectiveExit = (_exitY >= 0) ? _exitY : y;
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
            // Draw while the line intersects the visible body; allow partial lines for pixel-level motion
            if (yy >= y + h) break;                 // past bottom
            if (yy + _lineHeight <= y) {            // fully above visible band
                yy += _lineHeight;
                continue;
            }
            // Ensure the top of the line is inside the visible band so nothing renders under the title
            if (yy < y) {
                yy += _lineHeight;
                continue;
            }
            uint16_t useColor = (idx < _lineColors.size()) ? _lineColors[idx] : color;
            display.setTextColor(useColor);
            display.setCursor(x, yy);
            display.print(line);
            yy += _lineHeight;
        }
    };

    drawPass(startY);
    // Second pass follows after totalHeight + gap to keep continuity with minimal blank time
    drawPass(startY + direction * (totalHeight + gap));
}
