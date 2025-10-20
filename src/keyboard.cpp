#include "keyboard.h"
#include "display.h"  // Your dma_display instance

enum KbdMode { MODE_UPPER, MODE_LOWER, MODE_SYM };
KbdMode kbdMode = MODE_UPPER;

const char* keyboardGridUpper[] = {
    "ABCDEFGH",
    "IJKLMNOP",
    "QRSTUVWX",
    "YZ"
};
const char* keyboardGridLower[] = {
    "abcdefgh",
    "ijklmnop",
    "qrstuvwx",
    "yz"
};
const char* keyboardGridSym[] = {
    "01234567",
    "89!@#$%^", 
    "&*()-_=+",
    "[]{};:,."
};

const int gridRows = 4;
const int gridCols = 8;
const int keyboardVisibleRows = 1;
enum KeyboardButtonIndex { BTN_BS = 0, BTN_SPACE = 1, BTN_MODE = 2, BTN_OK = 3, BTN_CANCEL = 4, BTN_COUNT = 5 };

char keyboardBuffer[64] = ""; // Max 63 chars + null
bool inKeyboardMode = false;
bool kbEditLineActive = false;
int kbCursorRow = 0, kbCursorCol = 0;
int kbRowScroll = 0;
int editLen = 0;
int kbEditCursor = 0;
volatile bool blinkState = true;

static void (*keyboardDoneCallback)(const char*) = nullptr;
// static const char* keyboardTitle = nullptr; // For customizable title

// --- Fixed title handling ---
static char keyboardTitleBuf[64] = "Enter Text:";  // Safe persistent buffer
static const char* keyboardTitle = keyboardTitleBuf;



const char** getActiveGrid() {
    switch (kbdMode) {
        case MODE_LOWER: return keyboardGridLower;
        case MODE_SYM:   return keyboardGridSym;
        default:         return keyboardGridUpper;
    }
}
const char* getModeButtonLabel() {
    switch (kbdMode) {
        case MODE_UPPER: return "ab";
        case MODE_LOWER: return "12";
        case MODE_SYM:   return "AB";
        default:         return "ab";
    }
}
void switchKeyboardMode() {
    if (kbdMode == MODE_UPPER)
        kbdMode = MODE_LOWER;
    else if (kbdMode == MODE_LOWER)
        kbdMode = MODE_SYM;
    else
        kbdMode = MODE_UPPER;
    drawKeyboard();
}

void tickKeyboard() {
    if (!inKeyboardMode) return;

    drawKeyboard();  // Redraws with cursor blink
    readIRSensor();  // Ensures IR goes to keyboard
}


// --- Updated to copy title safely ---
void startKeyboardEntry(const char* initialValue, void (*onDoneCallback)(const char* result), const char* title) {
    inKeyboardMode = true;
    kbEditLineActive = false;
    kbdMode = MODE_UPPER;
    strncpy(keyboardBuffer, initialValue ? initialValue : "", sizeof(keyboardBuffer)-1);
    keyboardBuffer[sizeof(keyboardBuffer)-1] = '\0';
    editLen = strlen(keyboardBuffer);
    kbCursorRow = 0;
    kbCursorCol = 0;
    kbRowScroll = 0;
    kbEditCursor = editLen;
    keyboardDoneCallback = onDoneCallback;

    // Copy custom title safely into persistent buffer
    if (title && strlen(title) > 0) {
        strncpy(keyboardTitleBuf, title, sizeof(keyboardTitleBuf) - 1);
        keyboardTitleBuf[sizeof(keyboardTitleBuf) - 1] = '\0';
    } else {
        strncpy(keyboardTitleBuf, "Enter Text:", sizeof(keyboardTitleBuf) - 1);
        keyboardTitleBuf[sizeof(keyboardTitleBuf) - 1] = '\0';
    }

    keyboardTitle = keyboardTitleBuf;
    drawKeyboard();
}

// --- IR Handler ---
void handleKeyboardIR(uint32_t code) {
    const char** keyboardGrid = getActiveGrid();
    if (!inKeyboardMode) return;

    if (kbEditLineActive) {
        if (code == 0xFFFF50AF) { // LEFT
            if (kbEditCursor > 0) kbEditCursor--;
        } else if (code == 0xFFFFE01F) { // RIGHT
            if (kbEditCursor < editLen) kbEditCursor++;
        } else if (code == 0xFFFF48B7) { // OK deletes char at cursor
            if (kbEditCursor < editLen) {
                for (int i = kbEditCursor; i < editLen-1; ++i)
                    keyboardBuffer[i] = keyboardBuffer[i+1];
                editLen--;
                keyboardBuffer[editLen] = '\0';
            }
        } else if (code == 0xFFFF906F) { // DOWN
            kbEditLineActive = false;
            kbCursorRow = kbRowScroll;
        } else if (code == 0xFFFF08F7) { // CANCEL
            inKeyboardMode = false;
            if (keyboardDoneCallback) keyboardDoneCallback(nullptr);
            return;
        }
    } else {
        if (code == 0xFFFF30CF) { // UP
            if (kbCursorRow == 0 && kbRowScroll == 0) {
                kbEditLineActive = true;
            } else if (kbCursorRow > 0) {
                kbCursorRow--;
                int rowLen = strlen(keyboardGrid[kbCursorRow]);
                if (rowLen == 0)
                    kbCursorCol = 0;
                else if (kbCursorCol >= rowLen)
                    kbCursorCol = rowLen - 1;
                if (kbCursorRow < kbRowScroll) kbRowScroll = kbCursorRow;
            }
        } else if (code == 0xFFFF906F) { // DOWN
            if (kbCursorRow < gridRows) {
                kbCursorRow++;
                if (kbCursorRow == gridRows) {
                    if (kbCursorCol >= BTN_COUNT) kbCursorCol = BTN_COUNT - 1;
                } else {
                    int rowLen = strlen(keyboardGrid[kbCursorRow]);
                    if (rowLen == 0)
                        kbCursorCol = 0;
                    else if (kbCursorCol >= rowLen)
                        kbCursorCol = rowLen - 1;
                }
                if (kbCursorRow >= kbRowScroll + keyboardVisibleRows && kbCursorRow < gridRows) {
                    kbRowScroll = kbCursorRow - (keyboardVisibleRows-1);
                }
            }
        } else if (code == 0xFFFF50AF) { // LEFT
            if (kbCursorRow == gridRows) {
                kbCursorCol = (kbCursorCol == 0) ? (BTN_COUNT - 1) : kbCursorCol - 1;
            } else {
                int rowLen = strlen(keyboardGrid[kbCursorRow]);
                if (rowLen == 0)
                    kbCursorCol = 0;
                else if (kbCursorCol == 0 || kbCursorCol >= rowLen)
                    kbCursorCol = rowLen - 1;
                else
                    kbCursorCol--;
            }
        } else if (code == 0xFFFFE01F) { // RIGHT
            if (kbCursorRow == gridRows) {
                kbCursorCol = (kbCursorCol + 1) % BTN_COUNT;
            } else {
                int rowLen = strlen(keyboardGrid[kbCursorRow]);
                if (rowLen == 0)
                    kbCursorCol = 0;
                else
                    kbCursorCol = (kbCursorCol + 1) % rowLen;
            }
        } else if (code == 0xFFFF48B7) { // OK
            if (kbCursorRow < gridRows) {
                int rowLen = strlen(keyboardGrid[kbCursorRow]);
                if (rowLen > 0 && kbCursorCol < rowLen) {
                    char ch = keyboardGrid[kbCursorRow][kbCursorCol];
                    if (ch && ch != ' ') {
                        if (editLen < (int)sizeof(keyboardBuffer)-1) {
                            for (int i = editLen; i > kbEditCursor; --i)
                                keyboardBuffer[i] = keyboardBuffer[i-1];
                            keyboardBuffer[kbEditCursor] = ch;
                            editLen++;
                            kbEditCursor++;
                            keyboardBuffer[editLen] = '\0';
                        }
                    }
                }
            } else if (kbCursorRow == gridRows) {
                // Bottom button row: Back, Space, Mode, OK, Cancel
                if (kbCursorCol == BTN_BS) { // Back
                    if (kbEditCursor > 0 && editLen > 0) {
                        for (int i = kbEditCursor-1; i < editLen-1; ++i)
                            keyboardBuffer[i] = keyboardBuffer[i+1];
                        editLen--;
                        kbEditCursor--;
                        keyboardBuffer[editLen] = '\0';
                    }
                } else if (kbCursorCol == BTN_SPACE) { // SPACE
                    if (editLen < (int)sizeof(keyboardBuffer)-1) {
                        for (int i = editLen; i > kbEditCursor; --i)
                            keyboardBuffer[i] = keyboardBuffer[i-1];
                        keyboardBuffer[kbEditCursor] = ' ';
                        editLen++;
                        kbEditCursor++;
                        keyboardBuffer[editLen] = '\0';
                    }
                } else if (kbCursorCol == BTN_MODE) { // MODE
                    switchKeyboardMode();
                    kbCursorRow = 0;
                    kbCursorCol = 0;
                    kbRowScroll = 0;
                    return;
                } else if (kbCursorCol == BTN_OK) { // OK
                    inKeyboardMode = false;
                    if (keyboardDoneCallback) keyboardDoneCallback(keyboardBuffer);
                    return;
                } else if (kbCursorCol == BTN_CANCEL) { // CANCEL
                    inKeyboardMode = false;
                    if (keyboardDoneCallback) keyboardDoneCallback(nullptr);
                    return;
                }
            }
        } else if (code == 0xFFFF08F7) { // CANCEL
            inKeyboardMode = false;
            if (keyboardDoneCallback) keyboardDoneCallback(nullptr);
            return;
        }
    }
    drawKeyboard();
}

// Call this from your main loop/timer every ~500ms to blink cursor
void keyboardBlinkTick() {
    if (!inKeyboardMode) return;
    blinkState = !blinkState;
    drawKeyboard();
}

void drawKeyboard() {
    const char** keyboardGrid = getActiveGrid();
    dma_display->fillScreen(0);

    // --- Title Background Bar ---
    int titleHeight = 8;
    dma_display->fillRect(0, 0, dma_display->width(), titleHeight, dma_display->color565(0, 0, 120));
    dma_display->drawFastHLine(0, titleHeight - 1, dma_display->width(), dma_display->color565(255, 255, 255));

    // --- Title Text ---
    // Use safe buffer instead of pointer
    const char* title = keyboardTitleBuf;
    int textLen = strlen(title);
    int textX = (dma_display->width() - textLen * 6) / 2;
    if (textX < 0) textX = 0;


    dma_display->setTextColor(dma_display->color565(255, 255, 255));
    dma_display->setCursor(textX, 0);  // vertically centered in 8px bar
    dma_display->print(title);

    // --- Edit Buffer (Line 1) ---
    int bufferY = titleHeight;  // Start immediately after title bar
    int charWidth = 8;
    int visibleChars = 8;
    int start = (kbEditCursor > visibleChars - 1) ? (kbEditCursor - (visibleChars - 1)) : 0;

    uint16_t bufferColor = kbEditLineActive
        ? dma_display->color565(255, 220, 80)
        : dma_display->color565(64, 128, 255);

    for (int i = 0; i < visibleChars; ++i) {
        int idx = start + i;
        int x = i * charWidth;
        char c = (idx < editLen) ? keyboardBuffer[idx] : ' ';
        bool isCursor = (idx == kbEditCursor);

        if (isCursor) {
            dma_display->setTextColor(blinkState ? dma_display->color565(255, 255, 64) : bufferColor);
        } else {
            dma_display->setTextColor(bufferColor);
        }

        dma_display->setCursor(x, bufferY);
        dma_display->print(isCursor && blinkState ? '_' : c);
    }

    // --- Draw Keyboard Row (Line 2) ---
    int gridY = bufferY + 8;
    int row = kbRowScroll;
    int rowLen = strlen(keyboardGrid[row]);

    for (int col = 0; col < rowLen; ++col) {
        int x = col * charWidth;
        bool isSel = (!kbEditLineActive && row == kbCursorRow && col == kbCursorCol && kbCursorRow < gridRows);

        if (isSel) {
            dma_display->fillRect(x - 1, gridY - 1, 7, 9, dma_display->color565(0, 128, 255));
            dma_display->setTextColor(dma_display->color565(255, 255, 255));
        } else {
            dma_display->setTextColor(dma_display->color565(180, 180, 180));
        }
        dma_display->setCursor(x, gridY);
        dma_display->print(keyboardGrid[row][col]);
    }

    // --- Draw Back, Space, Mode, OK, Cancel buttons (Line 3) ---
    int btnY = gridY + 8;
    int screenW = 64;
    const int btnCount = BTN_COUNT;
    int btnWidths[BTN_COUNT] = {11, 10, 15, 13, 11};
    const char* btnLabels[BTN_COUNT] = {nullptr, nullptr, getModeButtonLabel(), "OK", nullptr};

    int xpos = 0;
    for (int i = 0; i < btnCount; ++i) {
        bool highlight = (!kbEditLineActive && kbCursorRow == gridRows && kbCursorCol == i);

        uint16_t fill, border, textColor;
        if (i == BTN_CANCEL) {
            fill = highlight ? dma_display->color565(220, 40, 40) : dma_display->color565(120, 0, 0);
            border = dma_display->color565(255, 64, 64);
            textColor = dma_display->color565(255, 255, 255);
        } else {
            fill = highlight
                ? (i == BTN_MODE ? dma_display->color565(180, 255, 0) : dma_display->color565(255, 180, 0))
                : dma_display->color565(30, 40, 60);
            border = dma_display->color565(255, 255, 0);
            textColor = highlight ? dma_display->color565(0, 0, 0) : dma_display->color565(255, 255, 255);
        }

        const char* label = btnLabels[i];
        int rawWidth = btnWidths[i];
        int drawX = xpos;
        int drawWidth = rawWidth;

        if (i == BTN_OK) {
            drawWidth = rawWidth + 2;
        } else if (i == BTN_CANCEL) {
            drawX += 2;
            drawWidth = max(3, rawWidth - 2);
        }

        dma_display->fillRect(drawX, btnY, drawWidth, 9, fill);
        dma_display->drawRect(drawX, btnY, drawWidth, 9, border);

        if (i == BTN_BS) {
            uint16_t symbol = textColor;
            int midY = btnY + 4;
            int right = drawX + drawWidth - 3;
            int left = drawX;
            dma_display->drawLine(right , midY, left + 2, midY, symbol);
            dma_display->drawLine(left + 2, midY, left + 4, midY - 2, symbol);
            dma_display->drawLine(left + 3, midY, left + 4, midY + 2, symbol);
        } else if (i == BTN_SPACE) {
            uint16_t symbol = textColor;
            int left = drawX + 2;
            int right = drawX + drawWidth - 3;
            int top = btnY + 3;
            int bottom = btnY + 6;
            dma_display->drawFastHLine(left, bottom, right - left + 1, symbol);
            dma_display->drawLine(left, top + 1, left, bottom, symbol);
            dma_display->drawLine(right, top + 1, right, bottom, symbol);
        } else if (i == BTN_CANCEL) {
            uint16_t symbol = textColor;
            int left = xpos + 4;
            int right = xpos + rawWidth - 3;
            int top = btnY + 2;
            int bottom = btnY + 6;
            dma_display->drawLine(left, top, right, bottom, symbol);
            dma_display->drawLine(left, bottom, right, top, symbol);
        } else if (label && *label) {
            int labelLen = strlen(label);
            int tx = drawX + (drawWidth - 8 * labelLen) / 2;
            if (tx < drawX + 1) tx = drawX + 1;
            if (i == BTN_MODE) tx += 1;
            else if (i == BTN_OK) tx += 1;
            dma_display->setTextColor(textColor);
            dma_display->setCursor(tx, btnY + 1);
            dma_display->print(label);
        }

        xpos += rawWidth + 1;
    }
}




