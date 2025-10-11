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
    "[]{};:,.",
    "&*()-_=+",  
    "89!@#$%^",    
    "01234567"
};

const int gridRows = 4;
const int gridCols = 8;
const int keyboardVisibleRows = 1;

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

// --- 🔧 Fixed title handling ---
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
        case MODE_UPPER: return "abc";
        case MODE_LOWER: return "123";
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

void tickKeyboard() {
    if (!inKeyboardMode) return;

    drawKeyboard();  // Redraws with cursor blink
    readIRSensor();  // ✅ Ensures IR goes to keyboard
}


// --- 🔧 Updated to copy title safely ---
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

    int lastRowLen = strlen(keyboardGrid[gridRows-1]);
    int lastRowSPCol = lastRowLen;

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
                if (kbCursorRow == gridRows-1) {
                    if (kbCursorCol > lastRowSPCol) kbCursorCol = lastRowSPCol;
                } else if (kbCursorCol >= gridCols) {
                    kbCursorCol = gridCols-1;
                }
                if (kbCursorRow < kbRowScroll) kbRowScroll = kbCursorRow;
            }
        } else if (code == 0xFFFF906F) { // DOWN
            if (kbCursorRow < gridRows) {
                kbCursorRow++;
                if (kbCursorRow == gridRows && kbCursorCol > 3) kbCursorCol = 3;
                if (kbCursorRow > gridRows) {
                    kbCursorRow = 0;
                    kbRowScroll = 0;
                }
                if (kbCursorRow >= kbRowScroll + keyboardVisibleRows && kbCursorRow < gridRows) {
                    kbRowScroll = kbCursorRow - (keyboardVisibleRows-1);
                }
            }
        } else if (code == 0xFFFF50AF) { // LEFT
            if (kbCursorRow == gridRows) {
                kbCursorCol = (kbCursorCol == 0) ? 3 : kbCursorCol-1;
            } else if (kbCursorRow == gridRows-1) {
                if (kbCursorCol == 0)
                    kbCursorCol = lastRowSPCol;
                else
                    kbCursorCol--;
            } else {
                kbCursorCol = (kbCursorCol == 0) ? gridCols-1 : kbCursorCol-1;
            }
        } else if (code == 0xFFFFE01F) { // RIGHT
            if (kbCursorRow == gridRows) {
                kbCursorCol = (kbCursorCol == 3) ? 0 : kbCursorCol+1;
            } else if (kbCursorRow == gridRows-1) {
                if (kbCursorCol == lastRowSPCol)
                    kbCursorCol = 0;
                else
                    kbCursorCol++;
                if (kbCursorCol > lastRowSPCol) kbCursorCol = 0;
            } else {
                kbCursorCol = (kbCursorCol+1) % gridCols;
            }
        } else if (code == 0xFFFF48B7) { // OK
            if (kbCursorRow < gridRows) {
                if (kbCursorRow == gridRows-1 && kbCursorCol == lastRowSPCol) {
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
                // 0=BS, 1=OK, 2=MOD, 3=X
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
                } else if (kbCursorCol == 3) { // CANCEL "X"
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
    // 🔧 Use safe buffer instead of pointer
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
    int lastRowLen = strlen(keyboardGrid[gridRows - 1]);
    int lastRowSPCol = lastRowLen;
    int colsThisRow = (row == gridRows - 1 && kbdMode != MODE_SYM) ? lastRowLen + 1 : strlen(keyboardGrid[row]);

    for (int col = 0; col < colsThisRow; ++col) {
        int x = col * charWidth;
        bool isSel = (!kbEditLineActive && row == kbCursorRow && col == kbCursorCol && kbCursorRow < gridRows);

if (row == gridRows - 1 && col == lastRowSPCol && kbdMode != MODE_SYM) {
    const char* spaceLabel = "Space";
    int spW = 28;
    int spH = 9;
    int spX = x - 1 + 1;  // original x-1 + shift right by 1
    int spY = gridY - 1;

    if (isSel) {
        dma_display->fillRect(spX, spY, spW, spH, dma_display->color565(0, 180, 180));
        dma_display->drawRect(spX, spY, spW, spH, dma_display->color565(255, 255, 255));
        dma_display->setTextColor(dma_display->color565(255, 255, 255));
    } else {
        dma_display->fillRect(spX, spY, spW, spH, dma_display->color565(20, 100, 100));
        dma_display->drawRect(spX, spY, spW, spH, dma_display->color565(100, 200, 200));
        dma_display->setTextColor(dma_display->color565(220, 255, 255));
    }

    // Shift label 2px from new left edge
    int textX = spX + 2;
    int textY = spY + 1;
    dma_display->setCursor(textX, textY);
    dma_display->print(spaceLabel);
}


        else if (col < (int)strlen(keyboardGrid[row])) {
            if (isSel) {
                dma_display->fillRect(x - 1, gridY - 1, 7, 9, dma_display->color565(0, 128, 255));
                dma_display->setTextColor(dma_display->color565(255, 255, 255));
            } else {
                dma_display->setTextColor(dma_display->color565(180, 180, 180));
            }
            dma_display->setCursor(x, gridY);
            dma_display->print(keyboardGrid[row][col]);
        }
    }

    // --- Draw BS, OK, MOD, X buttons (Line 3) ---
    int btnY = gridY + 8;
    int screenW = 64;
    int btnCount = 4;
    int totalSpacing = (btnCount - 1) * 2;
    int baseBtnW = (screenW - totalSpacing) / 4;
    int btnWidths[4] = {baseBtnW, baseBtnW, baseBtnW + 5, baseBtnW - 3};
    const char* btnLabels[4] = {"BS", "OK", getModeButtonLabel(), "X"};

    int xpos = 0;
    for (int i = 0; i < 4; ++i) {
        bool highlight = (!kbEditLineActive && kbCursorRow == gridRows && kbCursorCol == i);

        uint16_t fill, border, textColor;
        if (i == 3) {
            fill = highlight ? dma_display->color565(220, 40, 40) : dma_display->color565(120, 0, 0);
            border = dma_display->color565(255, 64, 64);
            textColor = dma_display->color565(255, 255, 255);
        } else {
            fill = highlight
                ? (i == 2 ? dma_display->color565(180, 255, 0) : dma_display->color565(255, 180, 0))
                : dma_display->color565(30, 40, 60);
            border = dma_display->color565(255, 255, 0);
            textColor = highlight ? dma_display->color565(0, 0, 0) : dma_display->color565(255, 255, 255);
        }

        dma_display->fillRect(xpos, btnY, btnWidths[i], 9, fill);
        dma_display->drawRect(xpos, btnY, btnWidths[i], 9, border);

        int labelLen = strlen(btnLabels[i]);
        int tx = xpos + (btnWidths[i] - 8 * labelLen) / 2;
        if (i == 3) tx += 2;
        dma_display->setTextColor(textColor);
        dma_display->setCursor(tx > xpos ? tx : xpos + 1, btnY + 1);
        dma_display->print(btnLabels[i]);

        xpos += btnWidths[i] + 2;
    }
}
