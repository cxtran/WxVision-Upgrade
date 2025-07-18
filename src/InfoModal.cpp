#include "InfoModal.h"
#include "menu.h" // for drawMenu()


InfoModal::InfoModal(const String& title)
    : modalTitle(title) {}

void InfoModal::setLines(const String _lines[], int count) {
    lineCount = (count > MAX_LINES) ? MAX_LINES : count;
    for (int i = 0; i < lineCount; ++i) lines[i] = _lines[i];
}

void InfoModal::show() {
    selIndex = 0;
    scrollY = 0;
    scrollOffset = 0;
    atClose = false;
    lastScrollTime = millis();
    firstScroll = true;
    lastSelIndex = -1;
    active = true;
    draw();
}

void InfoModal::hide() {
    active = false;
}

bool InfoModal::isActive() const { return active; }

void InfoModal::handleIR(uint32_t code) {
    if (!active) return;

    if (atClose) {
        if (code == 0xFFFF48B7) { // OK
            hide();
            drawMenu();
            return;
        } else if (code == 0xFFFF906F) { // DOWN -> go to first data line
            atClose = false;
            selIndex = 0;
            scrollY = 0;
        } else if (code == 0xFFFF30CF) { // UP on X -> wrap to last line
            atClose = false;
            selIndex = lineCount - 1;
            if (selIndex >= scrollY + INFOROWS)
                scrollY = selIndex - (INFOROWS - 1);
        }
    } else {
        if (code == 0xFFFF30CF) { // UP
            if (selIndex == 0) {
                atClose = true; // Move to X when pressing UP on first data line
            } else {
                selIndex--;
                if (selIndex < scrollY) scrollY--;
            }
        } else if (code == 0xFFFF906F) { // DOWN
            if (selIndex < lineCount - 1) {
                selIndex++;
                if (selIndex >= scrollY + INFOROWS) scrollY++;
            }
        }
    }
    draw();
}

void InfoModal::drawHeader() {
    const int headerHeight = CHARH;
    dma_display->fillRect(0, 0, SCREEN_WIDTH, headerHeight, INFOMODAL_HEADERBG);

    dma_display->setTextColor(INFOMODAL_GREEN);
    dma_display->setCursor(1, 0);
    dma_display->print(modalTitle);

    // --- "x" CLOSE BUTTON, upper right ---
    int xWidth = 7;
    int xX = SCREEN_WIDTH - xWidth;
    int xY = -1; if (xY < 0) xY = 0;

    uint16_t xBgColor = atClose ? INFOMODAL_SELXBG : INFOMODAL_UNSELXBG;
    uint16_t xFgColor = INFOMODAL_XCOLOR;

    dma_display->fillRect(xX, 0, xWidth, headerHeight, xBgColor);
    dma_display->setTextColor(xFgColor);
    dma_display->setCursor(xX + 1, xY - 1);
    dma_display->print("x");

    dma_display->drawFastHLine(0, headerHeight - 1, SCREEN_WIDTH, INFOMODAL_ULINE);
}

void InfoModal::draw() {
    dma_display->setFont(&Font5x7Uts);
    dma_display->fillScreen(0);

    drawHeader();

    for (int i = 0; i < MAXROWS - 1; ++i) { // -1 for header
        int idx = scrollY + i;
        if (idx >= lineCount) break;
        String s = lines[idx];
        bool isSelected = (selIndex == idx) && !atClose;

        if (isSelected) {
            if (selIndex != lastSelIndex) {
                scrollOffset = 0;
                firstScroll = true;
                lastSelIndex = selIndex;
            }
            int textW = getTextWidth(s.c_str());
            if (textW > SCREEN_WIDTH) {
                if (millis() - lastScrollTime > SCROLLSPEED) {
                    lastScrollTime = millis();
                    scrollOffset++;
                    if (scrollOffset > (textW - SCREEN_WIDTH)) {
                        firstScroll = false;
                    }
                    if (!firstScroll && scrollOffset > textW) {
                        scrollOffset = -SCREEN_WIDTH;
                    }
                }
            } else {
                scrollOffset = 0;
                firstScroll = true;
            }

            dma_display->setTextColor(INFOMODAL_SEL);
            int cursorX = -scrollOffset;
            dma_display->setCursor(cursorX, (i + 1) * CHARH);
            dma_display->print(s);
        } else {
            String sub = s.substring(0, MAXCOLS);
            dma_display->setTextColor(INFOMODAL_UNSEL);
            dma_display->setCursor(0, (i + 1) * CHARH);
            dma_display->print(sub);
        }
    }
}

void InfoModal::tick() {
    if (!active) return;
    draw(); // Redraw to update scrolling
}   