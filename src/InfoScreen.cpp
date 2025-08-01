#include "InfoScreen.h"
#include "InfoModal.h"   // For CHARH, SCREEN_WIDTH, INFOMODAL_* colors
#include "display.h"     // For dma_display, SCREEN_WIDTH

extern const int NUM_INFOSCREENS;
extern const ScreenMode InfoScreenModes[];
extern ScreenMode currentScreen;
extern int scrollSpeed; // ms per pixel, for horizontal scroll

int InfoScreen::getTextWidth(const char* str) {
    return strlen(str) * 6;  // 6px per char, adjust for your font if needed
}

InfoScreen::InfoScreen(const String& title, ScreenMode mode)
    : _title(title), _screenMode(mode), _lineCount(0), _active(false),
      _onExit(nullptr), scrollY(0), selIndex(0), lastSelIndex(-1),
      scrollOffset(0), lastScrollTime(0), firstScroll(true),
      scrollPaused(false), scrollPauseTime(0)
{
    resetHScroll();
}

void InfoScreen::setTitle(const String& title) {
    _title = title;
}

void InfoScreen::setLines(const String lines[], int n) {
    _lineCount = (n > INFOSCREEN_MAX_LINES) ? INFOSCREEN_MAX_LINES : n;
    for (int i = 0; i < _lineCount; ++i) _lines[i] = lines[i];
    scrollY = 0;
    selIndex = 0;
    lastSelIndex = -1;
    resetHScroll();
}

void InfoScreen::show(void (*onExit)()) {
    _active = true;
    _onExit = onExit;
    scrollY = 0;
    selIndex = 0;
    lastSelIndex = -1;
    resetHScroll();
}

void InfoScreen::hide() {
    _active = false;
    if (_onExit) _onExit();
    resetHScroll();
}

bool InfoScreen::isActive() const {
    return _active;
}

void InfoScreen::resetHScroll() {
    scrollOffset = 0;
    lastScrollTime = millis();
    firstScroll = true;
    scrollPaused = false;
    scrollPauseTime = 0;
}

void InfoScreen::draw() {
    dma_display->fillScreen(0);

    // --- Header ---
    const int headerHeight = CHARH;
    dma_display->fillRect(0, 0, SCREEN_WIDTH, headerHeight, INFOMODAL_HEADERBG);
    dma_display->setTextColor(INFOMODAL_GREEN);
    dma_display->setCursor(1, 0);
    String t = _title;
    if (t.length() > 12) t = t.substring(0, 12);
    dma_display->print(t);
    dma_display->drawFastHLine(0, headerHeight - 1, SCREEN_WIDTH, INFOMODAL_ULINE);

    // --- Lines ---
    int y = headerHeight;
    int pageLines = min(_lineCount - scrollY, INFOSCREEN_VISIBLE_ROWS);

    for (int i = 0; i < pageLines; ++i) {
        int idx = scrollY + i;
        String line = _lines[idx];
        if (line.length() > 40) line = line.substring(0, 40);

        bool isSelected = (i == selIndex);

        dma_display->setTextColor(isSelected ? dma_display->color565(255,255,0) : dma_display->color565(255,255,255));
        if (isSelected) {
            int textW = getTextWidth(line.c_str());
            if (textW > SCREEN_WIDTH) {
                // --- True LED marquee: start off right, scroll left, restart ---
                int cursorX = SCREEN_WIDTH - scrollOffset;
                dma_display->setCursor(cursorX, y);
                dma_display->print(line);
            } else {
                dma_display->setCursor(0, y);
                dma_display->print(line);
            }
        } else {
            dma_display->setCursor(0, y);
            dma_display->print(line);
        }
        y += CHARH;
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

    if (selIndex < pageLines && pageLines > 0) {
        int idx = scrollY + selIndex;
        String line = _lines[idx];
        int textW = getTextWidth(line.c_str());

        if (textW > SCREEN_WIDTH) {
            // --- True marquee: scroll from off-screen right to off-screen left ---
            const int gap = 16; // px between scrolls for readability; set 0 for none
            int scrollRange = textW + SCREEN_WIDTH + gap;
            if (now - lastScrollTime > (unsigned)scrollSpeed) {
                scrollOffset++;
                lastScrollTime = now;
                if (scrollOffset >= scrollRange)
                    scrollOffset = 0;
            }
        } else {
            scrollOffset = 0;
        }
    }

    draw();
}

void InfoScreen::handleIR(uint32_t code) {
    if (!_active) return;

    if (code == IR_UP) {
        if (selIndex == 0) {
            if (scrollY > 0) scrollY--;
            else scrollY = max(0, _lineCount - INFOSCREEN_VISIBLE_ROWS);
        } else {
            selIndex--;
        }
        resetHScroll();
        draw();
        return;
    }
    if (code == IR_DOWN) {
        int pageLines = min(_lineCount - scrollY, INFOSCREEN_VISIBLE_ROWS);
        if (selIndex >= pageLines - 1) {
            if (scrollY + INFOSCREEN_VISIBLE_ROWS < _lineCount) scrollY++;
            else scrollY = 0;
            selIndex = 0;
        } else {
            selIndex++;
        }
        resetHScroll();
        draw();
        return;
    }
    if (code == IR_LEFT || code == IR_RIGHT) {
        int idx = -1;
        for (int i = 0; i < NUM_INFOSCREENS; ++i) {
            if (InfoScreenModes[i] == _screenMode) { idx = i; break; }
        }
        if (idx < 0) return;
        int nextIdx = (code == IR_LEFT)
            ? (idx - 1 + NUM_INFOSCREENS) % NUM_INFOSCREENS
            : (idx + 1) % NUM_INFOSCREENS;
        ScreenMode next = InfoScreenModes[nextIdx];
        hide();
        currentScreen = next;
        return;
    }
    draw();
}
