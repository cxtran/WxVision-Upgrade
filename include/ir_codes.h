// ir_codes.h
// All IR button codes for your remote
#pragma once
#include <Arduino.h>

// === IR Remote Button Codes (HEX values) ===
const uint32_t IR_UP      = 0xFFFF30CF;   // Up / CH+
const uint32_t IR_DOWN    = 0xFFFF906F;   // Down / CH-
const uint32_t IR_LEFT    = 0xFFFF50AF;   // Left
const uint32_t IR_RIGHT   = 0xFFFFE01F;   // Right
const uint32_t IR_OK      = 0xFFFF48B7;   // OK / Enter
const uint32_t IR_CANCEL  = 0xFFFF08F7;   // Power/Menu (Exit)
const uint32_t IR_MENU    = IR_CANCEL;    // Alias for clarity
const uint32_t IR_SCREEN    = 0xFFFFF00F;  // Screen On/Off
const uint32_t IR_THEME     = 0xFFFFB04F;  // Theme Toggle

// Add other buttons as needed:
const uint32_t IR_0       = 0xFFFF6897;
const uint32_t IR_1       = 0xFFFFA857;
// ... add more if needed

// Optionally: a macro for easier debug printing
#define PRINT_IR_CODE(code) Serial.printf("IR Code: 0x%08X\n", code)
