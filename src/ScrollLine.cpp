#include "ScrollLine.h"
#include "display.h"  // your dma_display pointer

extern int theme;

ScrollLine::ScrollLine(int screenWidth, unsigned int scrollSpeedMs)
    : _screenWidth(screenWidth), _scrollSpeedMs(scrollSpeedMs),
      _startPauseMs(0), _scrollStepPx(1), _continuousWrap(true),
      _lineCount(0), _selIndex(0), _isTitleMode(false),
      _titleTextWidth(0), _titleScrollOffset(0), _titleLastScrollTime(0),
      _titleTextColor(0xFFFF), _titleBgColor(0x0000), _cycleCompleted(false), _enteredFromRight(false)
{
    reset();
    for (int i = 0; i < MAX_LINES; ++i) {
        _textColors[i] = 0xFFFF;  // White text default
        _bgColors[i] = 0x0000;    // Black bg default
    }
    _titleScrollDirection = 1;
}

void ScrollLine::setLines(const String lines[], int n, bool resetPosition) {
    if (_isTitleMode) return;

    _lineCount = (n > MAX_LINES) ? MAX_LINES : n;
    for (int i = 0; i < _lineCount; ++i) _lines[i] = lines[i];
    if (resetPosition) reset();
}

void ScrollLine::setTitleText(const String& text) {
    _isTitleMode = true;
    _titleText = text;
    _titleTextWidth = getTextWidth(text.c_str());
    reset();
}

void ScrollLine::setTitleMode(bool enable) {
    _isTitleMode = enable;
    if (enable) _lineCount = 0;
    reset();
}

void ScrollLine::setScrollSpeed(unsigned int ms) {
    _scrollSpeedMs = ms;
}

void ScrollLine::setScrollStepPx(int px) {
    _scrollStepPx = (px < 1) ? 1 : px;
}

void ScrollLine::setStartPauseMs(unsigned int ms) {
    _startPauseMs = ms;
}

void ScrollLine::setContinuousWrap(bool enabled) {
    _continuousWrap = enabled;
}

void ScrollLine::reset() {
    _cycleCompleted = false;
    _enteredFromRight = false;
    if (_isTitleMode) {
        _titleScrollOffset = 0;
        _titleScrollDirection = 1;
        _titleLastScrollTime = millis();
    } else {
        for (int i = 0; i < MAX_LINES; ++i) {
            _scrollOffsets[i] = 0;
            _scrollDirections[i] = 1;
            _lastScrollTimes[i] = millis();
        }
        _selIndex = 0;
    }
}

void ScrollLine::update() {
    unsigned long now = millis();

    if (_isTitleMode) {
        if (_titleTextWidth <= _screenWidth) return;

        if (now - _titleLastScrollTime >= _scrollSpeedMs) {
            if (_titleScrollOffset == 0 && (now - _titleLastScrollTime) < _startPauseMs) {
                return;
            }
            int prevOffset = _titleScrollOffset;
            _titleScrollOffset += _scrollStepPx;
            int entryThreshold = _titleTextWidth + 1 - _screenWidth;
            if (entryThreshold < 0) entryThreshold = 0;
            if (prevOffset < entryThreshold && _titleScrollOffset >= entryThreshold) {
                _enteredFromRight = true;
            }
            if (_titleScrollOffset > _titleTextWidth) {
                _titleScrollOffset = 0;  // reset to start smoothly
                _cycleCompleted = true;
            }
            _titleLastScrollTime = now;
        }
    } else {
        if (_lineCount == 0) return;
        if (_selIndex >= _lineCount) _selIndex = 0;

        for (int i = 0; i < _lineCount; ++i) {
            int lineW = getTextWidth(_lines[i].c_str());
            int& scrollOffset = _scrollOffsets[i];
            unsigned long& lastScrollTime = _lastScrollTimes[i];

            if (i == _selIndex && lineW > _screenWidth) {
                if (now - lastScrollTime >= _scrollSpeedMs) {
                    if (scrollOffset == 0 && (now - lastScrollTime) < _startPauseMs) {
                        continue;
                    }
                    int prevOffset = scrollOffset;
                    scrollOffset += _scrollStepPx;
                    int entryThreshold = lineW + 1 - _screenWidth;
                    if (entryThreshold < 0) entryThreshold = 0;
                    if (prevOffset < entryThreshold && scrollOffset >= entryThreshold) {
                        _enteredFromRight = true;
                    }
                    if (scrollOffset > lineW) {
                        scrollOffset = 0;  // loop continuous marquee
                        _cycleCompleted = true;
                    }
                    lastScrollTime = now;
                }
            } else {
                scrollOffset = 0;
            }
        }
    }
}

void ScrollLine::setLineColors(uint16_t textColors[], uint16_t bgColors[], int n) {
    int count = (n > _lineCount) ? _lineCount : n;
    for (int i = 0; i < count; ++i) {
        _textColors[i] = textColors[i];
        _bgColors[i] = bgColors[i];
    }
}

void ScrollLine::setTitleColors(uint16_t textColor, uint16_t bgColor) {
    _titleTextColor = textColor;
    _titleBgColor = bgColor;
}

void ScrollLine::draw(int x, int y, uint16_t defaultColor) {
    const bool monoTheme = (theme == 1);
    if (_isTitleMode) {
        // Draw background behind title
        uint16_t titleBg = monoTheme ? dma_display->color565(20,20,40) : _titleBgColor;
        dma_display->fillRect(x, y, _screenWidth, 8, titleBg);

        // Draw bottom border line
        const uint16_t borderLineColor = monoTheme ? dma_display->color565(40,40,90) : dma_display->color565(100, 100, 100);
        dma_display->drawFastHLine(x, y + 7, _screenWidth, borderLineColor);

        uint16_t titleText = monoTheme ? dma_display->color565(60,60,120) : _titleTextColor;
        dma_display->setTextColor(titleText);

            if (_titleTextWidth <= _screenWidth) {
                dma_display->setCursor(x, y);
                dma_display->print(_titleText);
            } else {
                // Draw first instance of text offset left by _titleScrollOffset
                int drawX = x - _titleScrollOffset;
                dma_display->setCursor(drawX, y);
                dma_display->print(_titleText);

                if (_continuousWrap) {
                    // Draw second instance right after first to fill gap
                    dma_display->setCursor(drawX + _titleTextWidth + 1, y);
                    dma_display->print(_titleText);
                }
            }
    } else {
        // Your existing multi-line drawing code unchanged (or apply similar marquee if desired)
        int lineHeight = 8;
        for (int i = 0; i < _lineCount; ++i) {
            int drawY = y + i * lineHeight;
            String line = _lines[i];
            int lineW = getTextWidth(line.c_str());
            int scrollOffset = _scrollOffsets[i];
            int cursorX = x - scrollOffset;  // note adjusted for marquee

            // Draw background behind text line
            dma_display->fillRect(x, drawY, _screenWidth, lineHeight, _bgColors[i]);

            uint16_t textColor = _textColors[i];
            if (textColor == 0) textColor = defaultColor;
            if (monoTheme) textColor = dma_display->color565(60,60,120);

            dma_display->setTextColor(textColor);

            if (lineW <= _screenWidth) {
                dma_display->setCursor(x, drawY);
                dma_display->print(line);
            } else {
                // Draw first instance
                dma_display->setCursor(cursorX, drawY);
                dma_display->print(line);

                if (_continuousWrap) {
                    // Draw second instance to fill gap
                    dma_display->setCursor(cursorX + lineW + 1, drawY);
                    dma_display->print(line);
                }
            }
        }
    }
}

bool ScrollLine::isActive() const {
    return _isTitleMode || (_lineCount > 0);
}

bool ScrollLine::selectedLineNeedsScroll() const {
    if (_isTitleMode) {
        return _titleTextWidth > _screenWidth;
    }
    if (_lineCount == 0) {
        return false;
    }
    int idx = _selIndex;
    if (idx < 0 || idx >= _lineCount) {
        idx = 0;
    }
    return getTextWidth(_lines[idx].c_str()) > _screenWidth;
}

bool ScrollLine::consumeCycleCompleted() {
    bool done = _cycleCompleted;
    _cycleCompleted = false;
    return done;
}

bool ScrollLine::consumeEnteredFromRight() {
    bool entered = _enteredFromRight;
    _enteredFromRight = false;
    return entered;
}

int ScrollLine::getTextWidth(const char* text) const {
    int width = 0;
    for (const char* c = text; *c != '\0'; c++) {
        width += 6; // fixed-width font assumption; replace if you have better API
    }
    return width;
}

void ScrollLine::setTitleScrollDirection(int dir) {
    if (dir == 1 || dir == -1) _titleScrollDirection = dir;
}

void ScrollLine::setLineScrollDirection(int lineIndex, int dir) {
    if (lineIndex >= 0 && lineIndex < MAX_LINES && (dir == 1 || dir == -1))
        _scrollDirections[lineIndex] = dir;
}

void ScrollLine::setBounceEnabled(bool enabled) {
    _bounceEnabled = enabled;
}
