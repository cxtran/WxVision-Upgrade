#pragma once
#include <Arduino.h>

extern char keyboardBuffer[64]; // 32 chars max + null
extern bool inKeyboardMode;

// Begin keyboard entry (with optional initial value, callback on completion)
void startKeyboardEntry(const char* initialValue, void (*onDoneCallback)(const char* result));

// IR handler
void handleKeyboardIR(uint32_t code);

// Draw/redraw
void drawKeyboard();
