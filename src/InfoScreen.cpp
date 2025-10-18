#include "InfoScreen.h"
#include "InfoModal.h"
#include "display.h"
#include "utils.h"

extern const int NUM_INFOSCREENS;
extern const ScreenMode InfoScreenModes[];
extern ScreenMode currentScreen;
extern int scrollSpeed;
extern int theme;


static uint16_t brightenColor(uint16_t color, uint8_t boost = 50)
{
    uint8_t r = ((color >> 11) & 0x1F) * 255 / 31;
    uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
    uint8_t b = (color & 0x1F) * 255 / 31;

    auto clamp = [](uint8_t v, uint8_t increase) -> uint8_t {
        uint16_t temp = static_cast<uint16_t>(v) + increase;
        if (temp > 255)
            temp = 255;
        return static_cast<uint8_t>(temp);
    };

    r = clamp(r, boost);
    g = clamp(g, boost);
    b = clamp(b, boost);
    return dma_display->color565(r, g, b);
}

InfoScreen::InfoScreen(const String& title, ScreenMode mode)
    : _title(title), _screenMode(mode), _lineCount(0), _active(false),
      _onExit(nullptr), scrollY(0), selIndex(0), lastSelIndex(-1),
      firstScroll(true), scrollPaused(false), scrollPauseTime(0),
      _highlightEnabled(true), _lineOverlay(nullptr)
{
    for (int i = 0; i < INFOSCREEN_MAX_LINES; ++i)
    {
        _lineColors[i] = 0;
        _lineColorUsed[i] = false;
    }
    resetHScroll();
}

void InfoScreen::setTitle(const String& title) { _title = title; }

void InfoScreen::setLines(const String lines[], int n, bool resetPosition, const uint16_t colors[]) {
    _lineCount = (n > INFOSCREEN_MAX_LINES) ? INFOSCREEN_MAX_LINES : n;
    for (int i = 0; i < _lineCount; ++i) {
        _lines[i] = lines[i];
        if (colors)
        {
            _lineColors[i] = colors[i];
            _lineColorUsed[i] = true;
        }
        else
        {
            _lineColors[i] = 0;
            _lineColorUsed[i] = false;
        }
    }
    for (int i = _lineCount; i < INFOSCREEN_MAX_LINES; ++i)
    {
        _lineColors[i] = 0;
        _lineColorUsed[i] = false;
    }

    if (resetPosition) {
        scrollY = 0;
        selIndex = 0;
        lastSelIndex = -1;
        resetHScroll();
    }
}

void InfoScreen::setHighlightEnabled(bool enabled)
{
    _highlightEnabled = enabled;
}

void InfoScreen::setLineOverlay(LineOverlayFn fn)
{
    _lineOverlay = fn;
}

void InfoScreen::setSelectedLine(int index)
{
    if (_lineCount <= 0)
        return;

    if (index < 0)
        index = 0;
    if (index >= _lineCount)
        index = _lineCount - 1;

    int currentSelectedGlobal = -1;
    if (selIndex >= 0 && selIndex < INFOSCREEN_VISIBLE_ROWS)
    {
        int visibleLines = min(_lineCount - scrollY, INFOSCREEN_VISIBLE_ROWS);
        if (selIndex < visibleLines && visibleLines > 0)
        {
            currentSelectedGlobal = scrollY + selIndex;
        }
    }

    if (currentSelectedGlobal == index)
    {
        return;
    }

    // Ensure selected line is within the current view window
    if (index < scrollY)
    {
        scrollY = index;
    }
    else
    {
        int maxVisibleIndex = scrollY + INFOSCREEN_VISIBLE_ROWS - 1;
        if (index > maxVisibleIndex)
        {
            scrollY = index - (INFOSCREEN_VISIBLE_ROWS - 1);
            if (scrollY < 0)
                scrollY = 0;
        }
    }

    selIndex = index - scrollY;
    if (selIndex < 0)
        selIndex = 0;
    if (selIndex >= INFOSCREEN_VISIBLE_ROWS)
        selIndex = INFOSCREEN_VISIBLE_ROWS - 1;

    lastSelIndex = -1; // force redraw/scroll reset on next tick
}


void InfoScreen::show(void (*onExit)()) {
    _active = true; _onExit = onExit;
    scrollY = 0; selIndex = 0; lastSelIndex = -1;
    resetHScroll();
}
void InfoScreen::hide() { _active = false; if (_onExit) _onExit(); resetHScroll(); }
bool InfoScreen::isActive() const { return _active; }

void InfoScreen::resetHScroll() {
    for (int i = 0; i < INFOSCREEN_VISIBLE_ROWS; ++i) {
        scrollOffsets[i] = 0;
        lastScrollTimes[i] = millis();
    }
    firstScroll = true;
    scrollPaused = false;
    scrollPauseTime = 0;
}

void InfoScreen::draw() {
    dma_display->fillScreen(0);

    const bool monoTheme = (theme == 1);
    const uint16_t headerBg = monoTheme ? dma_display->color565(20,20,40) : INFOSCREEN_HEADERBG;
    const uint16_t headerFg = monoTheme ? dma_display->color565(60,60,120) : INFOSCREEN_HEADERFG;
    const uint16_t defaultLineColor = monoTheme ? dma_display->color565(60,60,120)
                                                : dma_display->color565(230,230,230);

    // Header
    const int headerHeight = CHARH;
    dma_display->fillRect(0, 0, SCREEN_WIDTH, headerHeight, headerBg);
    dma_display->setTextColor(headerFg);
    dma_display->setCursor(1, 0);
    String t = _title; if (t.length() > 12) t = t.substring(0, 12);
    dma_display->print(t);
    const uint16_t underlineColor = monoTheme ? dma_display->color565(18, 18, 40)
                                              : dma_display->color565(12, 40, 80);
    dma_display->drawFastHLine(0, headerHeight - 1, SCREEN_WIDTH, underlineColor);

    // Lines
    int y = headerHeight;
    int pageLines = min(_lineCount - scrollY, INFOSCREEN_VISIBLE_ROWS);

    for (int i = 0; i < pageLines; ++i) {
        int idx = scrollY + i;
        String line = _lines[idx];

        int lineW = getTextWidth(line.c_str());
        bool isSelected = (i == selIndex);
        bool highlightLine = _highlightEnabled && isSelected;
        uint16_t baseColor = _lineColorUsed[idx] ? _lineColors[idx] : defaultLineColor;
        uint16_t valueColor;
        if (highlightLine)
        {
            if (_lineColorUsed[idx])
            {
                valueColor = brightenColor(baseColor);
            }
            else
            {
                valueColor = monoTheme ? dma_display->color565(120, 120, 200)
                                       : dma_display->color565(255, 255, 0);
            }
        }
        else
        {
            valueColor = baseColor;
        }

        const uint16_t defaultLabelColor = monoTheme ? dma_display->color565(70, 70, 110)
                                                     : dma_display->color565(130, 150, 200);
        uint16_t labelColor;
        if (highlightLine)
        {
            labelColor = monoTheme ? dma_display->color565(140, 140, 220)
                                   : dma_display->color565(255, 255, 180);
        }
        else
        {
            labelColor = defaultLabelColor;
        }

        int yPos = headerHeight + i * CHARH;

        int colonPos = line.indexOf(':');
        if (colonPos >= 0)
        {
            String labelPart = line.substring(0, colonPos + 1);
            String valuePart = line.substring(colonPos + 1);
            int labelWidth = getTextWidth(labelPart.c_str());
            int valueWidth = getTextWidth(valuePart.c_str());
            int totalWidth = labelWidth + valueWidth;

            if (totalWidth <= SCREEN_WIDTH)
            {
                dma_display->setTextColor(labelColor);
                dma_display->setCursor(0, yPos);
                dma_display->print(labelPart);
                dma_display->setTextColor(valueColor);
                dma_display->setCursor(labelWidth, yPos);
                dma_display->print(valuePart);
            }
            else if (highlightLine)
            {
                int cursorX = SCREEN_WIDTH - scrollOffsets[i];
                if (cursorX + totalWidth > 0 && cursorX < SCREEN_WIDTH)
                {
                    // Draw label segment
                    dma_display->setTextColor(labelColor);
                    dma_display->setCursor(cursorX, yPos);
                    dma_display->print(labelPart);
                    // Draw value segment
                    dma_display->setTextColor(valueColor);
                    dma_display->setCursor(cursorX + labelWidth, yPos);
                    dma_display->print(valuePart);
                }
            }
            else
            {
                dma_display->setTextColor(labelColor);
                dma_display->setCursor(0, yPos);
                dma_display->print(labelPart);
                dma_display->setTextColor(valueColor);
                dma_display->setCursor(labelWidth, yPos);
                dma_display->print(valuePart);
            }
        }
        else
        {
            if (lineW <= SCREEN_WIDTH) {
                dma_display->setTextColor(valueColor);
                dma_display->setCursor(0, yPos);
                dma_display->print(line);
            } else if (highlightLine) {
                int cursorX = SCREEN_WIDTH - scrollOffsets[i];
                if (cursorX + lineW > 0 && cursorX < SCREEN_WIDTH) {
                    dma_display->setTextColor(valueColor);
                    dma_display->setCursor(cursorX, yPos);
                    dma_display->print(line);
                }
            } else {
                // Too long & not selected: print entire line, let display handle overflow
                dma_display->setTextColor(valueColor);
                dma_display->setCursor(0, yPos);
                dma_display->print(line);
            }
        }
        if (_lineOverlay)
        {
            _lineOverlay(idx, yPos, highlightLine);
        }
    }
}

void InfoScreen::tick() {
    if (!_active) return;

    unsigned long now = millis();
    int pageLines = min(_lineCount - scrollY, INFOSCREEN_VISIBLE_ROWS);

    if (selIndex >= pageLines) selIndex = 0;

    if (lastSelIndex != selIndex) {
        resetHScroll();
        lastSelIndex = selIndex;
    }

    // Only scroll the selected line if too long
    for (int i = 0; i < pageLines; ++i) {
        int idx = scrollY + i;
        String line = _lines[idx];
        int lineW = getTextWidth(line.c_str());

        int &scrollOffset = scrollOffsets[i];
        unsigned long &lastScrollTime = lastScrollTimes[i];

        if (i == selIndex && lineW > SCREEN_WIDTH) {
            int cursorX = SCREEN_WIDTH - scrollOffset;
            if (now - lastScrollTime > (unsigned)scrollSpeed) {
                scrollOffset++;
                lastScrollTime = now;
                if (cursorX + lineW < 0) {
                    scrollOffset = 0;
                }
            }
        } else {
            scrollOffset = 0;
        }
    }
    draw();
}

void InfoScreen::handleIR(uint32_t code) {
    if (!_active) return;

    int pageLines = min(_lineCount - scrollY, INFOSCREEN_VISIBLE_ROWS);

    if (code == IR_UP) {
        if (selIndex > 0) {
            selIndex--;
        } else if (scrollY > 0) {
            scrollY--;
        } else {
            scrollY = max(0, _lineCount - INFOSCREEN_VISIBLE_ROWS);
            selIndex = min(INFOSCREEN_VISIBLE_ROWS - 1, _lineCount - 1);
        }
        resetHScroll(); draw(); return;
    }
    if (code == IR_DOWN) {
        if (selIndex < pageLines - 1 && (scrollY + selIndex) < (_lineCount - 1)) {
            selIndex++;
        } else if ((scrollY + INFOSCREEN_VISIBLE_ROWS) < _lineCount) {
            scrollY++;
        } else {
            scrollY = 0; selIndex = 0;
        }
        resetHScroll(); draw(); return;
    }
    if (code == IR_LEFT || code == IR_RIGHT) {
        int direction = (code == IR_LEFT) ? -1 : 1;
        ScreenMode next = nextAllowedScreen(_screenMode, direction);
        next = enforceAllowedScreen(next);
        hide();
        currentScreen = next;
        return;
    }
    draw();
}
