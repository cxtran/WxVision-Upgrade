#include "RollingUpScreen.h"
#include <Adafruit_GFX.h>
#include "Font5x7Uts.h"
#include "fonts/verdanab8pt7b.h"
#include "settings.h" // for verticalScrollSpeed default
#include "ui_theme.h"

extern int getTextWidth(const char *text);

extern int theme;

template <typename TIntVector>
static int maxExtraOffset(const TIntVector &offsets)
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
      _slowdownPressesToStop(3),
      _marqueeStartAfterMs(0),
      _holdBlockIndex(-1),
      _holdWaitForMarquee(false)
{
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void RollingUpScreen::setScrollSpeed(unsigned int ms)
{
    _scrollSpeedMs = ms;
    if (!_slowdownActive && !_paused && _slowdownPresses == 0)
    {
        _dynamicScrollSpeedMs = ms;
    }
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
    _lineMarqueeOffsets.assign(_lines.size(), 0);
    _lineMarqueeLastTick.assign(_lines.size(), millis());
    _lineMarqueeDone.assign(_lines.size(), 0);
    _marqueeStartAfterMs = millis() + 2000UL;
    _holdBlockIndex = -1;
    _holdWaitForMarquee = false;
}

int RollingUpScreen::currentBlockIndex() const
{
    if (_lines.empty())
        return 0;
    int blockPx = (_blockSizePx > 0) ? _blockSizePx : _lineHeight;
    if (blockPx < 1)
        blockPx = 1;
    int totalHeight = static_cast<int>(_lines.size()) * _lineHeight + maxExtraOffset(_lineYOffsets);
    int blockCount = (totalHeight + blockPx - 1) / blockPx;
    if (blockCount < 1)
        blockCount = 1;

    int effectiveEntry = (_entryY >= 0) ? _entryY : (_defaultHeight > 0 ? _defaultHeight : totalHeight);
    int effectiveExit = (_exitY >= 0) ? _exitY : 0;
    int targetBase = abs(effectiveEntry - effectiveExit);
    int rel = _offsetPx - targetBase;
    if (rel < 0)
        rel = 0;
    int idx = (rel / blockPx) % blockCount;
    if (idx < 0)
        idx = 0;
    return idx;
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

    auto marqueeLineForBlock = [&](int blockIdx) -> int {
        if (blockIdx < 0 || _lines.empty() || _lineMarqueeFlags.empty())
            return -1;
        int linesPerBlock = (_blockSizePx > 0 && _lineHeight > 0) ? (_blockSizePx / _lineHeight) : 0;
        if (linesPerBlock < 1)
            linesPerBlock = 3;
        int first = blockIdx * linesPerBlock;
        if (first >= static_cast<int>(_lines.size()))
            return -1;
        int last = min(static_cast<int>(_lines.size()) - 1, first + linesPerBlock - 1);
        for (int i = last; i >= first; --i)
        {
            if (i < static_cast<int>(_lineMarqueeFlags.size()) && _lineMarqueeFlags[i] != 0)
                return i;
        }
        return -1;
    };

    // Auto-resume after timeout, but keep holding until marquee cycle completes for this block.
    if (_resumeAt != 0 && now >= _resumeAt)
    {
        bool ready = true;
        if (_holdWaitForMarquee)
        {
            int mIdx = marqueeLineForBlock(_holdBlockIndex);
            if (mIdx >= 0 && mIdx < static_cast<int>(_lineMarqueeDone.size()) && _lineMarqueeDone[mIdx] == 0)
                ready = false;
            else
                _holdWaitForMarquee = false;
        }
        if (ready)
        {
            _paused = false;
            _resumeAt = 0;
            _gapHoldUntil = 0;
            _holdBlockIndex = -1;
            _lastTick = now;
        }
    }

    auto updateMarqueeState = [&](unsigned long tsNow) {
        if (_lineMarqueeOffsets.size() != _lines.size())
        {
            _lineMarqueeOffsets.assign(_lines.size(), 0);
            _lineMarqueeLastTick.assign(_lines.size(), tsNow);
            _lineMarqueeDone.assign(_lines.size(), 0);
        }
        for (size_t idx = 0; idx < _lines.size(); ++idx)
        {
            if (idx >= _lineMarqueeFlags.size() || _lineMarqueeFlags[idx] == 0)
                continue;

            String visible = _lines[idx];
            bool arrowLine = false;
            if (visible.startsWith("[up]"))
            {
                visible.remove(0, 4);
                visible.trim();
                arrowLine = true;
            }
            else if (visible.startsWith("[down]"))
            {
                visible.remove(0, 6);
                visible.trim();
                arrowLine = true;
            }

            int baseOffset = (idx < _lineOffsets.size()) ? _lineOffsets[idx] : 0;
            int iconX = (idx < _lineIconOffsets.size()) ? _lineIconOffsets[idx] : baseOffset;
            int textX = baseOffset;
            if (idx < _lineIcons.size() && _lineIcons[idx] != nullptr)
                textX = max(textX, iconX + 18);
            if (arrowLine)
                textX += 8;

            int lineW = getTextWidth(visible.c_str());
            if (arrowLine)
            {
                int degPos = visible.lastIndexOf('\xB0');
                if (degPos >= 0)
                {
                    int unitStart = degPos + 1;
                    while (unitStart < visible.length() && visible[unitStart] == ' ')
                        unitStart++;
                    if (unitStart < visible.length())
                        lineW -= 6; // unit kerning shift applied in draw()
                }
            }
            if (lineW < 1)
                lineW = 1;
            int availW = _width - textX;
            if (availW < 1)
                availW = 1;
            if (lineW <= availW)
            {
                _lineMarqueeOffsets[idx] = 0;
                _lineMarqueeDone[idx] = 1;
                continue;
            }

            const bool blockIsHoldingAtHeader = (_paused && _resumeAt != 0);
            const bool delayElapsed = (tsNow >= _marqueeStartAfterMs);
            if (!blockIsHoldingAtHeader || !delayElapsed || _lineMarqueeDone[idx] != 0)
            {
                _lineMarqueeOffsets[idx] = 0;
                continue;
            }

            const unsigned long stepMs = static_cast<unsigned long>((scrollSpeed > 0) ? scrollSpeed : 60);
            if (tsNow - _lineMarqueeLastTick[idx] >= stepMs)
            {
                _lineMarqueeOffsets[idx] += 1;
                _lineMarqueeLastTick[idx] = tsNow;
                const int wrap = lineW + 10;
                if (_lineMarqueeOffsets[idx] > wrap)
                {
                    _lineMarqueeOffsets[idx] = 0;
                    _lineMarqueeDone[idx] = 1; // single cycle
                }
            }
        }
    };

    if (_paused)
    {
        updateMarqueeState(now);
        return;
    }

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
                int blockCount = (_blockSizePx > 0) ? ((totalHeight + _blockSizePx - 1) / _blockSizePx) : 0;
                if (blockCount < 1)
                    blockCount = 1;
                _holdBlockIndex = (nextBlockIdx >= 0) ? (nextBlockIdx % blockCount) : 0;
                if (_holdBlockIndex < 0)
                    _holdBlockIndex = 0;
                _holdWaitForMarquee = false;
                int mIdx = marqueeLineForBlock(_holdBlockIndex);
                if (mIdx >= 0 && mIdx < static_cast<int>(_lineMarqueeDone.size()))
                {
                    _lineMarqueeOffsets[mIdx] = 0;
                    _lineMarqueeLastTick[mIdx] = now;
                    _lineMarqueeDone[mIdx] = 0; // require a fresh cycle for this held date
                    _holdWaitForMarquee = true;
                }
                _paused = true;
                _resumeAt = now + _exitHoldMs;
                // Start marquee delay when the page actually stops at the header.
                _marqueeStartAfterMs = now + 2000UL;
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

    updateMarqueeState(now);
}

void RollingUpScreen::draw(Adafruit_GFX &display, int x, int y, int height, uint16_t color)
{
    if (_lines.empty())
        return;

    const bool mono = (theme == 1);
    // Use a consistent mono accent when in mono theme; otherwise honor caller color
    const uint16_t themeColor = mono ? ui_theme::applyGraphicColor(color) : color;

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
            String line = _lines[idx];
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
            int iconX = (idx < _lineIconOffsets.size()) ? _lineIconOffsets[idx] : baseOffset;
            int arrowDir = 0;
            bool bigTempLine = false;
            if (line.startsWith("[up]"))
            {
                line.remove(0, 4);
                line.trim();
                arrowDir = 1;
            }
            else if (line.startsWith("[down]"))
            {
                line.remove(0, 6);
                line.trim();
                arrowDir = -1;
            }
            if (line.startsWith("[big]"))
            {
                line.remove(0, 5);
                line.trim();
                bigTempLine = true;
            }

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

            if (arrowDir != 0)
            {
                const int ax = textX;
                const int ay = localY - 1;
                if (arrowDir > 0)
                {
                    canvas.drawLine(ax + 3, ay + 1, ax + 1, ay + 3, useColor);
                    canvas.drawLine(ax + 3, ay + 1, ax + 5, ay + 3, useColor);
                    canvas.drawLine(ax + 3, ay + 1, ax + 3, ay + 7, useColor);
                }
                else
                {
                    canvas.drawLine(ax + 3, ay + 7, ax + 1, ay + 5, useColor);
                    canvas.drawLine(ax + 3, ay + 7, ax + 5, ay + 5, useColor);
                    canvas.drawLine(ax + 3, ay + 1, ax + 3, ay + 7, useColor);
                }
                textX += 8;
            }

            canvas.setTextColor(useColor, 0);
            int16_t tx1 = 0;
            int16_t ty1 = 0;
            uint16_t tw = 0;
            uint16_t th = 0;
            canvas.getTextBounds(line.c_str(), 0, 0, &tx1, &ty1, &tw, &th);
            int lineW = static_cast<int>(tw);
            int availW = _width - textX;
            if (availW < 1)
                availW = 1;

            bool useMarquee = false;
            if (idx < _lineMarqueeFlags.size() && _lineMarqueeFlags[idx] != 0 && lineW > availW)
            {
                const bool blockIsHoldingAtHeader = (_paused && _resumeAt != 0);
                const bool delayElapsed = (millis() >= _marqueeStartAfterMs);
                const bool cycleDone = (idx < _lineMarqueeDone.size()) ? (_lineMarqueeDone[idx] != 0) : false;
                useMarquee = blockIsHoldingAtHeader && delayElapsed && !cycleDone;
            }

            bool shiftUnitLeft = false;
            String mainPart = line;
            String unitPart;
            if (!useMarquee && arrowDir != 0)
            {
                int degPos = line.lastIndexOf('\xB0');
                if (degPos >= 0)
                {
                    int unitStart = degPos + 1;
                    while (unitStart < line.length() && line[unitStart] == ' ')
                        unitStart++;
                    if (unitStart < line.length())
                    {
                        mainPart = line.substring(0, unitStart);
                        unitPart = line.substring(unitStart);
                        shiftUnitLeft = (unitPart.length() > 0);
                    }
                }
            }

            // Optional label/value split coloring (e.g. "Press: 1013.2 hPa").
            // Only apply to single-colon lines with numeric-ish values to avoid breaking multi-column labels.
            int colonPos = line.indexOf(':');
            if (!useMarquee && !mono && colonPos > 0 && line.indexOf(':', colonPos + 1) < 0)
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

            if (shiftUnitLeft)
            {
                canvas.setCursor(textX, localY);
                canvas.print(mainPart);
                            int unitX = canvas.getCursorX() - 5;
                if (unitX < textX)
                    unitX = textX;
                canvas.setCursor(unitX, localY);
                canvas.print(unitPart);
                yy += _lineHeight;
                continue;
            }

            if (useMarquee)
            {
                int scrollX = (idx < _lineMarqueeOffsets.size()) ? _lineMarqueeOffsets[idx] : 0;
                int drawX = textX - scrollX;
                canvas.setCursor(drawX, localY);
                canvas.print(line);
            }
            else
            {
                if (bigTempLine)
                {
                    const int bigShiftX = 4;
                    char valueText[16] = {0};
                    char unitChar = '\0';
                    size_t vIdx = 0;
                    for (int ci = 0; ci < line.length() && vIdx < sizeof(valueText) - 1; ++ci)
                    {
                        char c = line.charAt(ci);
                        if ((c >= '0' && c <= '9') || c == '-' || c == '+')
                        {
                            valueText[vIdx++] = c;
                        }
                        else if (c == 'C' || c == 'F')
                        {
                            unitChar = c;
                        }
                    }
                    valueText[vIdx] = '\0';
                    if (vIdx == 0)
                    {
                        strncpy(valueText, "--", sizeof(valueText) - 1);
                        valueText[sizeof(valueText) - 1] = '\0';
                    }

                    uint16_t bigColor = useColor;
                    int tempVal = atoi(valueText);
                    bool hasTemp = (vIdx > 0 && valueText[0] != '-');
                    if (hasTemp)
                    {
                        const bool isF = (unitChar == 'F');
                        if ((isF && tempVal <= 45) || (!isF && tempVal <= 7))
                            bigColor = rgb565(120, 200, 255); // cold
                        else if ((isF && tempVal >= 85) || (!isF && tempVal >= 30))
                            bigColor = rgb565(255, 130, 90);  // hot
                        else
                            bigColor = rgb565(255, 225, 120);  // mild
                    }

                    canvas.setFont(&verdanab8pt7b);
                    canvas.setTextSize(1);
                    int valueBaseY = localY + 5; // move temperature down 2px
                    canvas.setTextColor(bigColor, 0);
                    canvas.setCursor(textX + bigShiftX, valueBaseY);
                    canvas.print(valueText);

                    if (unitChar == 'C' || unitChar == 'F')
                    {
                        int16_t vX1 = 0;
                        int16_t vY1 = 0;
                        uint16_t vW = 0;
                        uint16_t vH = 0;
                        canvas.getTextBounds(valueText, 0, 0, &vX1, &vY1, &vW, &vH);
                        char unitText[4] = {'\xB0', unitChar, '\0'};
                        canvas.setFont(&Font5x7Uts);
                        canvas.setTextSize(1);
                        canvas.setTextColor(bigColor, 0);
                        canvas.setCursor(textX + bigShiftX + static_cast<int>(vW) + 3, localY - 6);
                        canvas.print(unitText);
                    }

                    canvas.setFont(&Font5x7Uts);
                    canvas.setTextSize(1);
                }
                else
                {
                    canvas.setCursor(textX, localY);
                    canvas.print(line);
                }
            }
            yy += _lineHeight;
        }
    };

    drawPass(startY);
    // Second pass follows after totalHeight + gap to keep continuity with minimal blank time
    drawPass(startY + direction * (totalHeight + gap));

    ui_theme::applyGraphicThemeToBuffer(canvas.getBuffer(), static_cast<size_t>(_width) * static_cast<size_t>(h));

    // Blit the body canvas onto the display; header remains untouched.
    display.drawRGBBitmap(x, y, canvas.getBuffer(), _width, h);
}
