#include "keyboard.h"
#include "display.h"  // Your dma_display instance

enum KbdMode { MODE_UPPER, MODE_LOWER, MODE_SYM };
KbdMode kbdMode = MODE_UPPER;

// Last row has only the "real" characters; "SP" is added in code after last character!
const char* keyboardGridUpper[] = {
    "ABCDEFGH", // 8
    "IJKLMNOP", // 8
    "QRSTUVWX", // 8
    "YZ"        // Only two letters, SP appears after 'Z'
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
    "[]{};:,." // 8 chars, but you could make it shorter
};

const int gridRows = 4;
const int gridCols = 8; // for navigation and drawing
const int keyboardVisibleRows = 1;

char keyboardBuffer[64] = ""; // Max 63 chars + null
bool inKeyboardMode = false;
bool kbEditLineActive = false;
int kbCursorRow = 0, kbCursorCol = 0;  // kbCursorRow == gridRows means button row
int kbRowScroll = 0;
int editLen = 0;
int kbEditCursor = 0;
volatile bool blinkState = true;  // Toggle in your loop/timer for cursor blink

static void (*keyboardDoneCallback)(const char*) = nullptr;

// --- Helpers ---
const char** getActiveGrid() {
    switch (kbdMode) {
        case MODE_LOWER: return keyboardGridLower;
        case MODE_SYM:   return keyboardGridSym;
        default:         return keyboardGridUpper;
    }
}
const char* getModeButtonLabel() {
    switch (kbdMode) {
        case MODE_UPPER: return "abc";
        case MODE_LOWER: return "#?!";
        case MODE_SYM:   return "ABC";
        default:         return "abc";
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

void startKeyboardEntry(const char* initialValue, void (*onDoneCallback)(const char* result)) {
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
    drawKeyboard();
}

// --- IR Handler ---
void handleKeyboardIR(uint32_t code) {
    const char** keyboardGrid = getActiveGrid();
    if (!inKeyboardMode) return;

    int lastRowLen = strlen(keyboardGrid[gridRows-1]);
    int lastRowSPCol = lastRowLen; // SP is at col = lastRowLen, only on last row

    if (kbEditLineActive) {
        if (code == 0xFFFF50AF) { // LEFT
            if (kbEditCursor > 0) kbEditCursor--;
        } else if (code == 0xFFFFE01F) { // RIGHT
            if (kbEditCursor < editLen) kbEditCursor++;
        }
        else if (code == 0xFFFF906F) { // DOWN
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
                // Adjust col for last row SP case
                if (kbCursorRow == gridRows-1) {
                    if (kbCursorCol > lastRowSPCol) kbCursorCol = lastRowSPCol;
                } else if (kbCursorCol >= gridCols) {
                    kbCursorCol = gridCols-1;
                }
                // Scroll if needed
                if (kbCursorRow < kbRowScroll) kbRowScroll = kbCursorRow;
            }
        } else if (code == 0xFFFF906F) { // DOWN
            if (kbCursorRow < gridRows) {
                kbCursorRow++;
                // If entering button row, limit to three columns
                if (kbCursorRow == gridRows && kbCursorCol > 2) kbCursorCol = 2;
                // If wrapping, reset to first row
                if (kbCursorRow > gridRows) {
                    kbCursorRow = 0;
                    kbRowScroll = 0;
                }
                // Scroll if needed
                if (kbCursorRow >= kbRowScroll + keyboardVisibleRows && kbCursorRow < gridRows) {
                    kbRowScroll = kbCursorRow - (keyboardVisibleRows-1);
                }
            }
        } else if (code == 0xFFFF50AF) { // LEFT
            if (kbCursorRow == gridRows) {
                kbCursorCol = (kbCursorCol == 0) ? 2 : kbCursorCol-1;
            } else if (kbCursorRow == gridRows-1) {
                // Special: wrap between last char and SP key
                if (kbCursorCol == 0)
                    kbCursorCol = lastRowSPCol;
                else
                    kbCursorCol--;
            } else {
                kbCursorCol = (kbCursorCol == 0) ? gridCols-1 : kbCursorCol-1;
            }
        } else if (code == 0xFFFFE01F) { // RIGHT
            if (kbCursorRow == gridRows) {
                kbCursorCol = (kbCursorCol == 2) ? 0 : kbCursorCol+1;
            } else if (kbCursorRow == gridRows-1) {
                // Only allow to move to SP after last char
                if (kbCursorCol == lastRowSPCol)
                    kbCursorCol = 0;
                else
                    kbCursorCol++;
                if (kbCursorCol > lastRowSPCol) kbCursorCol = 0; // prevent OOB
            } else {
                kbCursorCol = (kbCursorCol+1) % gridCols;
            }
        } else if (code == 0xFFFF48B7) { // OK
            if (kbCursorRow < gridRows) {
                if (kbCursorRow == gridRows-1 && kbCursorCol == lastRowSPCol) {
                    // Insert space (SP key)
                    if (editLen < (int)sizeof(keyboardBuffer)-1) {
                        for (int i = editLen; i > kbEditCursor; --i)
                            keyboardBuffer[i] = keyboardBuffer[i-1];
                        keyboardBuffer[kbEditCursor] = ' ';
                        editLen++;
                        kbEditCursor++;
                        keyboardBuffer[editLen] = '\0';
                    }
                } else {
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
                // 0=BS, 1=OK, 2=MOD
                if (kbCursorCol == 0) { // BS
                    if (kbEditCursor > 0 && editLen > 0) {
                        for (int i = kbEditCursor-1; i < editLen-1; ++i)
                            keyboardBuffer[i] = keyboardBuffer[i+1];
                        editLen--;
                        kbEditCursor--;
                        keyboardBuffer[editLen] = '\0';
                    }
                } else if (kbCursorCol == 1) { // OK
                    inKeyboardMode = false;
                    if (keyboardDoneCallback) keyboardDoneCallback(keyboardBuffer);
                    return;
                } else if (kbCursorCol == 2) { // MOD
                    switchKeyboardMode();
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

    // --- Title (yellow) ---
    dma_display->setTextColor(dma_display->color565(255,255,0));
    dma_display->setCursor(0, 0);
    dma_display->print("Enter Text:");

    // --- Edit Buffer, line 1 ---
    int bufferY = 8;
    int charWidth = 8;
    int visibleChars = 8;  // 64px / 8px per char
    int start = (kbEditCursor > visibleChars-1) ? (kbEditCursor - (visibleChars-1)) : 0;

    for (int i = 0; i < visibleChars; ++i) {
        int idx = start + i;
        int x = i * charWidth;
        char c = (idx < editLen) ? keyboardBuffer[idx] : ' ';
        bool isCursor = (idx == kbEditCursor);
        if (isCursor && blinkState) {
            dma_display->fillRect(x, bufferY, charWidth, 8, dma_display->color565(255,255,255));
            dma_display->setTextColor(dma_display->color565(0,0,180));
        } else {
            dma_display->setTextColor(dma_display->color565(64,128,255));
        }
        dma_display->setCursor(x, bufferY);
        dma_display->print(c);
    }

    // --- Draw Keyboard Row (line 2) ---
    int gridY = 16;
    int row = kbRowScroll;
    int lastRowLen = strlen(keyboardGrid[gridRows-1]);
    int lastRowSPCol = lastRowLen;
    for (int col = 0; col < ((row == gridRows-1) ? lastRowLen+1 : gridCols); ++col) {
        int x = col * charWidth;
        bool isSel = (!kbEditLineActive && row == kbCursorRow && col == kbCursorCol && kbCursorRow < gridRows);
        if (isSel) {
            dma_display->fillRect(x, gridY, charWidth, 8, dma_display->color565(0,128,255));
            dma_display->setTextColor(dma_display->color565(255,255,255));
        } else {
            dma_display->setTextColor(dma_display->color565(180,180,180));
        }
        dma_display->setCursor(x, gridY);
        if (row == gridRows-1 && col == lastRowSPCol) {
            dma_display->print("SP");
        } else {
            dma_display->print(keyboardGrid[row][col]);
        }
    }

    // --- Draw BS, OK, MOD buttons (line 3) ---
    int btnY = 24;
    const int btnW = 21, btnH = 8;
    for (int i = 0; i < 3; ++i) {
        int x = i * btnW;
        bool highlight = (!kbEditLineActive && kbCursorRow == gridRows && kbCursorCol == i);
        uint16_t fill = highlight ? (i==2 ? dma_display->color565(180,255,0) : dma_display->color565(255,180,0)) : dma_display->color565(40,40,40);
        dma_display->fillRect(x, btnY, btnW-1, btnH, fill);
        dma_display->setTextColor(highlight ? dma_display->color565(0,0,0) : dma_display->color565(255,255,255));
        dma_display->setCursor(x+2, btnY);
        if (i==0) dma_display->print("BS");
        else if (i==1) dma_display->print("OK");
        else          dma_display->print(getModeButtonLabel());
    }
}
