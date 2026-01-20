#include "RollingUpScreen.h"
#include <Adafruit_GFX.h>
#include "Font5x7Uts.h"
#include "settings.h" // for verticalScrollSpeed default

extern int theme;

static int maxExtraOffset(const std::vector<int> &offsets)
{
    int m = 0;
    for (int v : offsets)
        if (v > m)
            m = v;
    return m;
}

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
      _autoResumeMs(5000),
      _exitHoldMs(0),
      _blockSizePx(0),
      _slowdownActive(false),
      _dynamicScrollSpeedMs(_scrollSpeedMs),
      _slowdownStepMs(10),
      _slowdownStopMs(800),
      _slowdownStartMs(0),
      _slowdownDurationMs(1200),
      _slowdownStartSpeedMs(_scrollSpeedMs),
      _slowdownEndSpeedMs(800),
      _slowdownPresses(0),
      _slowdownPressesToStop(3)
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
    if (!_slowdownActive && !_paused && _slowdownPresses == 0)
    {
        _dynamicScrollSpeedMs = ms;
    }
}

void RollingUpScreen::setLineColors(const std::vector<uint16_t> &colors)
{
    _lineColors = colors;
}

void RollingUpScreen::setLineOffsets(const std::vector<int> &offsets)
{
    _lineOffsets = offsets;
}

void RollingUpScreen::setLineYOffsets(const std::vector<int> &yOffsets)
{
    _lineYOffsets = yOffsets;
}

void RollingUpScreen::setLineIcons(const std::vector<const uint8_t *> &icons, const std::vector<uint16_t> &iconColors)
{
    _lineIcons = icons;
    _iconColors = iconColors;
}

void RollingUpScreen::setPaused(bool paused)
{
    _paused = paused;
    if (_paused)
        _slowdownActive = false;
}

void RollingUpScreen::onDownPress()
{
    if (_lines.empty())
        return;
    if (_paused)
        return;

    unsigned int baseSpeed = _scrollSpeedMs;
    if (baseSpeed == 0)
        baseSpeed = 1;

    if (_slowdownPresses == 0)
        _slowdownPressesToStop = 3;
    _slowdownPresses++;
    if (_slowdownPresses >= _slowdownPressesToStop)
    {
        _slowdownActive = false;
        _paused = true;
        _dynamicScrollSpeedMs = max(600u, baseSpeed * 4u);
        _resumeAt = 0;
        _gapHoldUntil = 0;
        _lastTick = millis();
        return;
    }

    unsigned int maxSlow = max(250u, baseSpeed * 4u);
    unsigned int step = max(30u, baseSpeed);
    unsigned int nextTarget = baseSpeed + step * _slowdownPresses;
    if (nextTarget > maxSlow)
        nextTarget = maxSlow;

    _slowdownStopMs = maxSlow;
    _slowdownDurationMs = max(250u, baseSpeed * 2u);
    _slowdownStartSpeedMs = _dynamicScrollSpeedMs;
    _slowdownEndSpeedMs = nextTarget;
    _slowdownActive = true;
    _slowdownStartMs = millis();
    _resumeAt = 0;
    _gapHoldUntil = 0;
}

void RollingUpScreen::onUpPress()
{
    _paused = false;
    _slowdownActive = false;
    unsigned int baseSpeed = (verticalScrollSpeed > 0) ? static_cast<unsigned int>(verticalScrollSpeed) : 60u;
    _scrollSpeedMs = baseSpeed;
    _dynamicScrollSpeedMs = baseSpeed;
    _slowdownStartMs = 0;
    _slowdownPresses = 0;
    _resumeAt = 0;
    _gapHoldUntil = 0;
    _lastTick = millis();
}

void RollingUpScreen::setAutoResumeMs(unsigned int ms)
{
    _autoResumeMs = ms;
}

void RollingUpScreen::setGapHoldMs(unsigned int ms)
{
    _gapHoldMs = ms;
}

void RollingUpScreen::setExitHoldMs(unsigned int ms)
{
    _exitHoldMs = ms;
}

void RollingUpScreen::setBlockSizePx(int px)
{
    _blockSizePx = px;
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
    _slowdownActive = false;
    _dynamicScrollSpeedMs = _scrollSpeedMs;
    _slowdownStartMs = 0;
    _slowdownPresses = 0;
}

void RollingUpScreen::update()
{
    if (_lines.empty())
        return;

    // Keep speed in sync with global vertical setting each tick
    unsigned int baseSpeed = (verticalScrollSpeed > 0) ? static_cast<unsigned int>(verticalScrollSpeed) : 60u;
    if (!_slowdownActive)
    {
        _scrollSpeedMs = baseSpeed;
        if (!_paused && _slowdownPresses == 0)
            _dynamicScrollSpeedMs = baseSpeed;
    }

    unsigned long now = millis();

    // Auto-resume after timeout
    if (_resumeAt != 0 && now >= _resumeAt)
    {
        _paused = false;
        _resumeAt = 0;
        _gapHoldUntil = 0;
        _lastTick = now;
    }

    if (_paused)
        return;

    if (_gapHoldUntil != 0 && now < _gapHoldUntil)
        return;
    if (_gapHoldUntil != 0 && now >= _gapHoldUntil)
        _gapHoldUntil = 0;

    unsigned int effectiveSpeed = (_slowdownActive || _slowdownPresses > 0)
                                      ? _dynamicScrollSpeedMs
                                      : _scrollSpeedMs;
    if (now - _lastTick >= effectiveSpeed)
    {
        int totalHeight = static_cast<int>(_lines.size()) * _lineHeight + maxExtraOffset(_lineYOffsets);
        int travelHeight = (_entryY >= 0 && _exitY >= 0)
                               ? abs(_entryY - _exitY)
                               : (_defaultHeight > 0 ? _defaultHeight : totalHeight);
        // Ensure travel covers at least the visible window
        if (_defaultHeight > 0)
            travelHeight = max(travelHeight, _defaultHeight);
        int gap = (_defaultHeight > 0) ? _defaultHeight : 0; // ensure clearance so last block fully exits
        int cycle = totalHeight + gap;
        if (cycle <= 0) cycle = 1;
        int next = _offsetPx + 1;

        // Optional pause when the head reaches the exit line
        if (_exitHoldMs > 0 && !_paused)
        {
            int effectiveEntry = (_entryY >= 0) ? _entryY : (_defaultHeight > 0 ? _defaultHeight : totalHeight);
            int effectiveExit = (_exitY >= 0) ? _exitY : 0;
            int direction = (effectiveEntry >= effectiveExit) ? 1 : -1;
            int phase = _offsetPx % cycle;
            int nextPhase = (phase + 1) % cycle;
            // head is the top of the visible block (leading edge). We pause when it reaches exit.
            int headY = effectiveEntry - (phase * direction);
            int nextHeadY = effectiveEntry - (nextPhase * direction);
            bool crossed = false;
            int nextBlockIdx = -1;

            if (_blockSizePx > 0)
            {
                int targetBase = effectiveEntry - effectiveExit;
                int currRel = _offsetPx - targetBase;
                int nextRel = next - targetBase;
                if (nextRel >= 0)
                {
                    int currBlock = (currRel >= 0) ? (currRel / _blockSizePx) : -1;
                    int nextBlock = nextRel / _blockSizePx;
                    crossed = (currBlock != nextBlock);
                    nextBlockIdx = nextBlock;
                }
            }
            else
            {
                crossed = (direction > 0) ? (headY > effectiveExit && nextHeadY <= effectiveExit)
                                          : (headY < effectiveExit && nextHeadY >= effectiveExit);
            }
            if (crossed)
            {
                // Set the offset right at the exit line for the current block
                int alignedPhase = (direction > 0)
                                       ? (effectiveEntry - effectiveExit)
                                       : (effectiveExit - effectiveEntry);
                if (_blockSizePx > 0 && nextBlockIdx >= 0)
                {
                    alignedPhase += nextBlockIdx * _blockSizePx;
                }
                // Keep absolute offset progression to avoid losing cycle context
                _offsetPx = (_offsetPx - phase) + alignedPhase;
                _paused = true;
                _resumeAt = now + _exitHoldMs;
                _lastTick = now;
                return;
            }
        }

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

        if (_slowdownActive)
        {
            if (_slowdownStartMs == 0)
                _slowdownStartMs = now;
            unsigned long elapsed = now - _slowdownStartMs;
            if (elapsed >= _slowdownDurationMs)
                elapsed = _slowdownDurationMs;
            float t = (_slowdownDurationMs > 0) ? (static_cast<float>(elapsed) / _slowdownDurationMs) : 1.0f;
            float easeOut = 1.0f - (1.0f - t) * (1.0f - t);
            float speed = _slowdownStartSpeedMs +
                          ((_slowdownEndSpeedMs - _slowdownStartSpeedMs) * easeOut);
            _dynamicScrollSpeedMs = static_cast<unsigned int>(lroundf(speed));
            if (elapsed >= _slowdownDurationMs || _dynamicScrollSpeedMs >= _slowdownEndSpeedMs)
            {
                _slowdownActive = false;
                if (_slowdownPresses >= _slowdownPressesToStop)
                {
                    _paused = true;
                    _resumeAt = 0;
                }
                else
                {
                    _dynamicScrollSpeedMs = _slowdownEndSpeedMs;
                }
            }
        }
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

    int totalHeight = static_cast<int>(_lines.size()) * _lineHeight + maxExtraOffset(_lineYOffsets);
    // Enter from bottom of body; exit at absolute Y=8 unless caller overrides
    int effectiveEntry = (_entryY >= 0) ? _entryY : (y + h);
    int effectiveExit = (_exitY >= 0) ? _exitY : 8;
    int travel = abs(effectiveEntry - effectiveExit);
    if (travel <= 0)
        travel = (h > 0 ? h : totalHeight);
    if (h > 0)
        travel = max(travel, h);
    int gap = (_defaultHeight > 0) ? _defaultHeight : 0; // match update() gap
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
            if (idx < _lineYOffsets.size())
                localY += _lineYOffsets[idx];
            if (localY >= h) break;                  // past bottom
            int visualH = _lineHeight;
            if (idx < _lineIcons.size() && _lineIcons[idx] != nullptr)
            {
                visualH = max(visualH, 16);
            }
            if (localY + visualH <= 0) {         // fully above visible band
                yy += _lineHeight;
                continue;
            }
            uint16_t useColor = (idx < _lineColors.size()) ? _lineColors[idx] : themeColor;
            if (mono) {
                useColor = themeColor;
            }
            int baseOffset = (idx < _lineOffsets.size()) ? _lineOffsets[idx] : 0;
            int textX = baseOffset;
            int iconX = baseOffset;

            // Optional 16x16 icon
            if (idx < _lineIcons.size() && _lineIcons[idx] != nullptr)
            {
                int iconY = localY;
                if (iconY + 16 > 0 && iconY < h) // simple visibility check
                {
                    uint16_t iconColor = (idx < _iconColors.size() && _iconColors[idx] != 0) ? _iconColors[idx] : useColor;
                    int drawX = iconX;
                    if (drawX < 0) drawX = 0;
                    if (drawX > _width - 16) drawX = _width - 16;
                    // Do not clamp Y: Adafruit_GFX will clip per-pixel, matching text roll behavior.
                    canvas.drawBitmap(drawX, iconY, _lineIcons[idx], 16, 16, iconColor);
                }
                textX = max(textX, iconX + 18); // leave room for icon + padding
            }

            canvas.setTextColor(useColor, 0);
            canvas.setCursor(textX, localY);

            // Optional label/value split coloring (e.g. "Press: 1013.2 hPa").
            // Only apply to single-colon lines with numeric-ish values to avoid breaking multi-column labels.
            int colonPos = line.indexOf(':');
            if (!mono && colonPos > 0 && line.indexOf(':', colonPos + 1) < 0)
            {
                String labelPart = line.substring(0, colonPos + 1);
                String valuePart = line.substring(colonPos + 1);
                String valueTrim = valuePart;
                valueTrim.trim();
                if (valueTrim.length())
                {
                    char c0 = valueTrim[0];
                    bool numericish = (c0 == '-') || (c0 == '.') || (c0 >= '0' && c0 <= '9');
                    if (numericish)
                    {
                        // Match InfoScreen labelColor (approx dma_display->color565(255, 240, 140))
                        const uint16_t labelColor = 0xFF71;
                        canvas.setTextColor(labelColor, 0);
                        canvas.print(labelPart);
                        int16_t afterLabelX = canvas.getCursorX();
                        canvas.setCursor(afterLabelX, localY);
                        canvas.setTextColor(useColor, 0);
                        canvas.print(valuePart);
                        yy += _lineHeight;
                        continue;
                    }
                }
            }

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
