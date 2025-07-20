#include "InfoModal.h"
#include "display.h"
#include "menu.h"
#include "keyboard.h"
#include <cstring>

// --- Info Modal Colors ---
#define INFOMODAL_GREEN    dma_display->color565(0,255,80)
#define INFOMODAL_HEADERBG dma_display->color565(0,20,60)
#define INFOMODAL_UNSELXBG dma_display->color565(110,80,133)
#define INFOMODAL_SELXBG   dma_display->color565(255,0,0)
#define INFOMODAL_XCOLOR   dma_display->color565(255,255,255)
#define INFOMODAL_ULINE    dma_display->color565(180,180,255)
#define INFOMODAL_SEL      dma_display->color565(255,255,64)
#define INFOMODAL_UNSEL    dma_display->color565(0,255,255)
#define INFOMODAL_BTN_BG   dma_display->color565(20,60,120)
#define INFOMODAL_BTN_SELBG dma_display->color565(255,130,0)

// --- Static trampoline for keyboard -> InfoModal::setTextValue ---
static InfoModal* s_modalForText = nullptr;
static int s_textIdxForText = -1;

static void keyboardTextDoneShim(const char* result) {
    if (s_modalForText) {
        s_modalForText->handleTextDone(s_textIdxForText, result);
        s_modalForText = nullptr;
    }
    s_textIdxForText = -1;
}

InfoModal::InfoModal(const String& title)
    : modalTitle(title) {
    memset(intRefs, 0, sizeof(intRefs));
    memset(chooserRefs, 0, sizeof(chooserRefs));
    memset(chooserOptions, 0, sizeof(chooserOptions));
    memset(chooserOptionCounts, 0, sizeof(chooserOptionCounts));
    memset(chooserFieldIndices, -1, sizeof(chooserFieldIndices));
    memset(numberFieldIndices, -1, sizeof(numberFieldIndices));
    memset(textRefs, 0, sizeof(textRefs));
    memset(textSizes, 0, sizeof(textSizes));
    memset(textFieldIndices, -1, sizeof(textFieldIndices));
    memset(btnLabels, 0, sizeof(btnLabels));
    btnCount = 0;
    btnSel = 0;
    inButtonBar = false;
}

void InfoModal::setLines(const String _lines[], const InfoFieldType _types[], int count) {
    lineCount = (count > MAX_LINES) ? MAX_LINES : count;
    for (int i = 0; i < lineCount; ++i) {
        lines[i] = _lines[i];
        fieldTypes[i] = _types[i];
        intRefs[i] = nullptr;
        chooserRefs[i] = nullptr;
        chooserOptions[i] = nullptr;
        chooserOptionCounts[i] = 0;
        textRefs[i] = nullptr;
        textSizes[i] = 0;
    }
    int chooserPos = 0, numberPos = 0, textPos = 0;
    for (int i = 0; i < lineCount; ++i) {
        if (fieldTypes[i] == InfoChooser) chooserFieldIndices[i] = chooserPos++;
        else chooserFieldIndices[i] = -1;
        if (fieldTypes[i] == InfoNumber) numberFieldIndices[i] = numberPos++;
        else numberFieldIndices[i] = -1;
        if (fieldTypes[i] == InfoText) textFieldIndices[i] = textPos++;
        else textFieldIndices[i] = -1;
    }
}

void InfoModal::setValueRefs(
    int* intRefs[], int refCount,
    int* chooserRefs[], int chooserCount,
    const char* const* chooserOptions[], const int chooserOptionCounts[],
    char* textRefs[], int textRefCount, int textSizes[]
) {
    for (int i = 0; i < refCount; ++i)
        this->intRefs[i] = intRefs[i];
    for (int i = refCount; i < MAX_LINES; ++i)
        this->intRefs[i] = nullptr;

    for (int i = 0; i < chooserCount; ++i) {
        this->chooserRefs[i] = chooserRefs[i];
        this->chooserOptions[i] = chooserOptions[i];
        this->chooserOptionCounts[i] = chooserOptionCounts[i];
    }
    for (int i = chooserCount; i < MAX_LINES; ++i) {
        this->chooserRefs[i] = nullptr;
        this->chooserOptions[i] = nullptr;
        this->chooserOptionCounts[i] = 0;
    }
    for (int i = 0; i < textRefCount && textRefs; ++i) {
        this->textRefs[i] = textRefs[i];
        this->textSizes[i] = textSizes ? textSizes[i] : 32;
    }
    for (int i = textRefCount; i < MAX_LINES; ++i) {
        this->textRefs[i] = nullptr;
        this->textSizes[i] = 0;
    }
}

void InfoModal::setButtons(const String btns[], int count) {
    btnCount = count > 0 ? ((count > MAXROWS) ? MAXROWS : count) : 0;
    for (int i = 0; i < btnCount; ++i)
        btnLabels[i] = btns[i];
    btnSel = 0;
    inButtonBar = false;
}

void InfoModal::setCallback(const std::function<void(bool, int)>& cb) { callback = cb; }

void InfoModal::show() {
    selIndex = 0;
    scrollY = 0;
    scrollOffset = 0;
    atClose = false;
    lastScrollTime = millis();
    firstScroll = true;
    lastSelIndex = -1;
    inEdit = false;
    editIndex = -1;
    btnSel = 0;
    inButtonBar = false;
    active = true;
    draw();
}
void InfoModal::hide() { active = false; }
bool InfoModal::isActive() const { return active; }

void InfoModal::drawHeader() {
    const int headerHeight = CHARH;
    dma_display->fillRect(0, 0, SCREEN_WIDTH, headerHeight, INFOMODAL_HEADERBG);

    dma_display->setTextColor(INFOMODAL_GREEN);
    dma_display->setCursor(1, 0);
    dma_display->print(modalTitle);

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

String InfoModal::getChooserLabel(int idx) {
    int cidx = chooserFieldIndices[idx];
    if (cidx >= 0 && chooserRefs[cidx] && chooserOptions[cidx] && chooserOptionCounts[cidx] > 0) {
        int v = *(chooserRefs[cidx]);
        if (v >= 0 && v < chooserOptionCounts[cidx])
            return chooserOptions[cidx][v];
    }
    return "?";
}

void InfoModal::draw() {
    dma_display->setFont(&Font5x7Uts);
    dma_display->fillScreen(0);
    drawHeader();

    // --- Data lines ---
    int visibleRows = (btnCount > 0) ? DATA_ROWS : DATA_ROWS_FULL;
    int dataCount = lineCount;
    int scrollLimit = (dataCount > visibleRows) ? (dataCount - visibleRows) : 0;

    // Clamp scrollY
    if (selIndex < scrollY) scrollY = selIndex;
    if (selIndex >= scrollY + visibleRows) scrollY = selIndex - visibleRows + 1;
    if (scrollY > scrollLimit) scrollY = scrollLimit;

    for (int i = 0; i < visibleRows; ++i) {
        int idx = scrollY + i;
        if (idx >= dataCount) break;

        bool isSelected = (selIndex == idx) && !atClose && (!inButtonBar);
        String s = lines[idx];

        // For choosers, numbers, and text, show live value
        if (fieldTypes[idx] == InfoNumber && intRefs[idx]) {
            s += ": " + String(*(intRefs[idx]));
        }
        else if (fieldTypes[idx] == InfoChooser && chooserFieldIndices[idx] >= 0 && chooserRefs[chooserFieldIndices[idx]]) {
            s += ": " + getChooserLabel(idx);
        }
        else if (fieldTypes[idx] == InfoText && textFieldIndices[idx] >= 0 && textRefs[textFieldIndices[idx]]) {
            s += ": ";
            s += textRefs[textFieldIndices[idx]];
        }

        // --- Selected row: HORIZONTAL SCROLL for any field type ---
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
                    if (scrollOffset > (textW - SCREEN_WIDTH)) firstScroll = false;
                    if (!firstScroll && scrollOffset > textW) scrollOffset = -SCREEN_WIDTH;
                }
            } else {
                scrollOffset = 0;
                firstScroll = true;
            }

            dma_display->setTextColor(INFOMODAL_SEL);
            int cursorX = -scrollOffset;
            dma_display->setCursor(cursorX, (i + 1) * CHARH);
            if ((fieldTypes[idx] == InfoNumber || fieldTypes[idx] == InfoChooser || fieldTypes[idx] == InfoText) && inEdit && editIndex == idx)
                dma_display->print(s + " <");
            else
                dma_display->print(s);
        } else {
            String sub = s.substring(0, MAXCOLS);
            dma_display->setTextColor(INFOMODAL_UNSEL);
            dma_display->setCursor(0, (i + 1) * CHARH);
            dma_display->print(sub);
        }
    }

    // --- Button bar (always last row) ---
    if (btnCount > 0) {
        int btnY = (MAXROWS - 1) * CHARH;
        int totalBtnW = 0;
        int btnWidths[MAXROWS];
        int pad = 3;

        for (int i = 0; i < btnCount; ++i) {
            btnWidths[i] = btnLabels[i].length() * 6 + 2 * pad;
            totalBtnW += btnWidths[i] + pad;
        }
        int btnX = (SCREEN_WIDTH - totalBtnW + pad) / 2;
        for (int i = 0; i < btnCount; ++i) {
            bool selected = (inButtonBar && btnSel == i);
            uint16_t btnBg = selected ? INFOMODAL_BTN_SELBG : INFOMODAL_BTN_BG;
            uint16_t btnFg = selected ? INFOMODAL_HEADERBG : INFOMODAL_XCOLOR;
            dma_display->fillRect(btnX, btnY, btnWidths[i], CHARH, btnBg);
            dma_display->setTextColor(btnFg);
            dma_display->setCursor(btnX + pad, btnY);
            dma_display->print(btnLabels[i]);
            btnX += btnWidths[i] + pad;
        }
    }
}

void InfoModal::handleIR(uint32_t code) {
    if (!active) return;

    Serial.printf("IR: %08lX | inButtonBar=%d, btnCount=%d, selIndex=%d\n", code, inButtonBar, btnCount, selIndex);

    // --- If in edit mode for number, chooser, or text ---
    if (inEdit && editIndex >= 0 && editIndex < lineCount) {
        if (fieldTypes[editIndex] == InfoNumber) {
            int nidx = numberFieldIndices[editIndex];
            if (nidx >= 0 && intRefs[nidx]) {
                int* ptr = intRefs[nidx];

                if (code == 0xFFFF30CF) { // UP
                    (*ptr)++;
                } else if (code == 0xFFFF906F) { // DOWN
                    (*ptr)--;
                } else if (code == 0xFFFF48B7) { // OK
                    inEdit = false;
                    editIndex = -1;
                    draw();
                    return;
                }

                switch (editIndex) {
                    case 0: if (*ptr < 2000) *ptr = 2000; if (*ptr > 2099) *ptr = 2099; break; // Year
                    case 1: if (*ptr < 1) *ptr = 1; if (*ptr > 12) *ptr = 12; break;           // Month
                    case 2: if (*ptr < 1) *ptr = 1; if (*ptr > 31) *ptr = 31; break;           // Day
                    case 3: if (*ptr < 0) *ptr = 0; if (*ptr > 23) *ptr = 23; break;           // Hour
                    case 4: if (*ptr < 0) *ptr = 0; if (*ptr > 59) *ptr = 59; break;           // Minute
                    case 5: if (*ptr < 0) *ptr = 0; if (*ptr > 59) *ptr = 59; break;           // Second
                    case 6: if (*ptr < -720) *ptr = -720; if (*ptr > 840) *ptr = 840; break;   // TimeZone
                }
                draw();
                return;
            }
        } else if (fieldTypes[editIndex] == InfoChooser) {
            int cidx = chooserFieldIndices[editIndex];
            if (cidx >= 0 && chooserRefs[cidx] && chooserOptionCounts[cidx]) {
                int& idx = *(chooserRefs[cidx]);
                int count = chooserOptionCounts[cidx];
                if (code == 0xFFFF30CF) { // UP
                    idx = (idx + count - 1) % count;
                    draw();
                    return;
                } else if (code == 0xFFFF906F) { // DOWN
                    idx = (idx + 1) % count;
                    draw();
                    return;
                } else if (code == 0xFFFF48B7) { // OK
                    inEdit = false;
                    editIndex = -1;
                    draw();
                    return;
                }
            }
        } else if (fieldTypes[editIndex] == InfoText) {
            // Only allow OK to exit edit mode (edit done by keyboard modal)
            if (code == 0xFFFF48B7) {
                inEdit = false;
                editIndex = -1;
                draw();
            }
            return;
        }
        return;
    }

    // --- Modal navigation ---
    if (atClose) {
        if (code == 0xFFFF48B7) { // OK
            if (callback) callback(false, -1);
            hide();
            drawMenu();
            return;
        } else if (code == 0xFFFF906F) { // DOWN -> go to first data line
            atClose = false;
            selIndex = 0;
            scrollY = 0;
        } else if (code == 0xFFFF30CF) { // UP on X -> wrap to last data line or button bar
            atClose = false;
            if (btnCount > 0 && inButtonBar) {
                inButtonBar = false;
                selIndex = lineCount - 1;
            } else if (btnCount > 0) {
                selIndex = lineCount - 1;
            } else {
                selIndex = (lineCount > 0) ? (lineCount - 1) : 0;
            }
            scrollY = (lineCount > 0) ? (lineCount - 1) : 0;
        }
    } else if (btnCount > 0 && inButtonBar) {
        // In button bar mode
        if (code == 0xFFFF30CF) { // UP
            inButtonBar = false;
            selIndex = lineCount - 1;
            draw();
            return;
        } else if (code == 0xFFFF906F) { // DOWN (optional: stay)
            // stay in button bar
        } else if (code == 0xFFFF48B7) { // OK
            if (callback) {
                bool accepted = (btnSel == 0); // Convention: 0 = Save, 1 = Cancel, etc
                callback(accepted, btnSel);
            }
            if (btnSel == 1) { // Cancel
                hide();
                drawMenu();
            }
            // Other buttons: stay open unless callback closes modal.
            return;
        } else if (code == 0xFFFF50AF) { // LEFT
    Serial.printf("LEFT: inButtonBar=%d btnSel(before)=%d btnCount=%d\n", inButtonBar, btnSel, btnCount);
    if (btnCount > 0) {
        btnSel = (btnSel - 1 + btnCount) % btnCount;
        Serial.printf("LEFT: btnSel(after)=%d\n", btnSel);
        draw();
        return;
    }
} else if (code == 0xFFFFE01F) { // RIGHT
    Serial.printf("RIGHT: inButtonBar=%d btnSel(before)=%d btnCount=%d\n", inButtonBar, btnSel, btnCount);
    if (btnCount > 0) {
        btnSel = (btnSel + 1) % btnCount;
        Serial.printf("RIGHT: btnSel(after)=%d\n", btnSel);
        draw();
        return;
    }
}

    } else {
        // Not in button bar
        if (code == 0xFFFF30CF) { // UP
            if (selIndex == 0) {
                atClose = true;
            } else {
                selIndex--;
            }
        } else if (code == 0xFFFF906F) { // DOWN
            if (selIndex < lineCount - 1) {
                selIndex++;
            } else if (btnCount > 0 && !inButtonBar) {
                inButtonBar = true;
                btnSel = 0;
            }
        } else if (code == 0xFFFF48B7) { // OK
            if (fieldTypes[selIndex] == InfoNumber || fieldTypes[selIndex] == InfoChooser) {
                inEdit = true;
                editIndex = selIndex;
                draw();
                return;
            } else if (fieldTypes[selIndex] == InfoText) {
                int textIdx = textFieldIndices[selIndex];
                if (textIdx >= 0 && textRefs[textIdx]) {
                    inEdit = true;
                    editIndex = selIndex;
                    s_modalForText = this;
                    s_textIdxForText = textIdx;
                    startKeyboardEntry(
                        textRefs[textIdx],
                        keyboardTextDoneShim,
                        lines[selIndex].c_str()
                    );
                }
                return;
            }
        }
    }
    draw();
}

int InfoModal::getSelIndex() const {
    return selIndex;
}

void InfoModal::tick() {
    if (!active) return;
    draw();
}

// Optional direct set (not required for normal usage)
void InfoModal::setTextValue(int idx, const char* value) {
    if (idx < 0 || idx >= MAX_LINES || !textRefs[idx]) return;
    int maxLen = textSizes[idx] ? textSizes[idx] : 32;
    strncpy(textRefs[idx], value, maxLen - 1);
    textRefs[idx][maxLen - 1] = 0;
}

void InfoModal::handleTextDone(int idx, const char* result) {
    if (idx < 0 || idx >= MAX_LINES || !textRefs[idx] || textSizes[idx] <= 0)
        return;
    if (result) {
        int maxLen = textSizes[idx];
        strncpy(textRefs[idx], result, maxLen - 1);
        textRefs[idx][maxLen - 1] = 0;
    }
    inEdit = false;
    editIndex = -1;
    draw();
}
