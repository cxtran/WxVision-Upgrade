#include "InfoScreen.h"
#include "InfoModal.h"
#include "display.h"

extern const int NUM_INFOSCREENS;
extern const ScreenMode InfoScreenModes[];
extern ScreenMode currentScreen;
extern int scrollSpeed;
extern int theme;


InfoScreen::InfoScreen(const String& title, ScreenMode mode)
    : _title(title), _screenMode(mode), _lineCount(0), _active(false),
      _onExit(nullptr), scrollY(0), selIndex(0), lastSelIndex(-1),
      firstScroll(true), scrollPaused(false), scrollPauseTime(0)
{
    resetHScroll();
}

void InfoScreen::setTitle(const String& title) { _title = title; }

void InfoScreen::setLines(const String lines[], int n, bool resetPosition) {
    _lineCount = (n > INFOSCREEN_MAX_LINES) ? INFOSCREEN_MAX_LINES : n;
    for (int i = 0; i < _lineCount; ++i) _lines[i] = lines[i];

    if (resetPosition) {
        scrollY = 0;
        selIndex = 0;
        lastSelIndex = -1;
        resetHScroll();
    }
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
    const uint16_t lineColor = monoTheme ? dma_display->color565(40,40,90) : dma_display->color565(255,255,255);
    const uint16_t selectedLineColor = monoTheme ? dma_display->color565(90,90,150) : dma_display->color565(255,255,0);

    // Header
    const int headerHeight = CHARH;
    dma_display->fillRect(0, 0, SCREEN_WIDTH, headerHeight, headerBg);
    dma_display->setTextColor(headerFg);
    dma_display->setCursor(1, 0);
    String t = _title; if (t.length() > 12) t = t.substring(0, 12);
    dma_display->print(t);
    const uint16_t underlineColor = monoTheme ? dma_display->color565(30,30,70) : INFOMODAL_ULINE;
    dma_display->drawFastHLine(0, headerHeight - 1, SCREEN_WIDTH, underlineColor);

    // Lines
    int y = headerHeight;
    int pageLines = min(_lineCount - scrollY, INFOSCREEN_VISIBLE_ROWS);

    for (int i = 0; i < pageLines; ++i) {
        int idx = scrollY + i;
        String line = _lines[idx];
        if (line.length() > 40) line = line.substring(0, 40);

        int lineW = getTextWidth(line.c_str());
        bool isSelected = (i == selIndex);
        uint16_t color = isSelected ? selectedLineColor : lineColor;

        int yPos = headerHeight + i * CHARH;

        if (lineW <= SCREEN_WIDTH) {
            dma_display->setTextColor(color);
            dma_display->setCursor(0, yPos);
            dma_display->print(line);
        } else if (isSelected) {
            int cursorX = SCREEN_WIDTH - scrollOffsets[i];
            if (cursorX + lineW > 0 && cursorX < SCREEN_WIDTH) {
                dma_display->setTextColor(color);
                dma_display->setCursor(cursorX, yPos);
                dma_display->print(line);
            }
        } else {
            // Too long & not selected: print entire line, let display handle overflow
            dma_display->setTextColor(color);
            dma_display->setCursor(0, yPos);
            dma_display->print(line);
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
