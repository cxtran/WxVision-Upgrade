#pragma once

#include <Arduino.h>

// Maximum buffer length for keyboard entry (including null terminator)
#define KEYBOARD_BUFFER_LEN 64

// --- API functions ---

/**
 * Start a keyboard entry session.
 * @param initialValue   Optional initial text (can be "")
 * @param onDoneCallback Callback called when user completes entry (OK or Cancel).
 *        If canceled, receives nullptr. If OK, receives keyboard buffer.
 * @param title          Optional title for the input prompt (default: "Enter Text:")
 */
void startKeyboardEntry(const char* initialValue, void (*onDoneCallback)(const char* result), const char* title = nullptr);

/**
 * Call this from your IR input handler to send key events to the keyboard.
 * @param code IR code
 */
void handleKeyboardIR(uint32_t code);

/**
 * Call this from your main loop or a timer every ~500ms to blink the text cursor.
 */
void keyboardBlinkTick();

/**
 * Manually triggers a redraw of the keyboard.
 */
void drawKeyboard();

// --- Exposed state variables ---

extern char keyboardBuffer[KEYBOARD_BUFFER_LEN];
extern bool inKeyboardMode;
extern bool kbEditLineActive;
extern int kbCursorRow, kbCursorCol;
extern int kbRowScroll;
extern int editLen;
extern int kbEditCursor;
extern volatile bool blinkState;

