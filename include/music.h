#pragma once

#include <Arduino.h>

// Piano-note (MIDI) helpers.
// MIDI note numbers: C4=60, A4=69 (440Hz).
//
// Use NOTE_REST (-1) for silence.

constexpr int8_t NOTE_REST = -1;

// Piano notes used by built-in melodies (MIDI note numbers).
// Octave numbering matches MIDI convention: C4 = middle C (60).
constexpr int8_t NOTE_A3  = 57;
constexpr int8_t NOTE_AS3 = 58;
constexpr int8_t NOTE_B3  = 59;
constexpr int8_t NOTE_C4  = 60;
constexpr int8_t NOTE_CS4 = 61;
constexpr int8_t NOTE_D4  = 62;
constexpr int8_t NOTE_DS4 = 63;
constexpr int8_t NOTE_E4  = 64;
constexpr int8_t NOTE_F4  = 65;
constexpr int8_t NOTE_FS4 = 66;
constexpr int8_t NOTE_G4  = 67;
constexpr int8_t NOTE_GS4 = 68;
constexpr int8_t NOTE_A4  = 69;
constexpr int8_t NOTE_AS4 = 70;
constexpr int8_t NOTE_B4  = 71;
constexpr int8_t NOTE_C5  = 72;
constexpr int8_t NOTE_CS5 = 73;
constexpr int8_t NOTE_D5  = 74;
constexpr int8_t NOTE_DS5 = 75;
constexpr int8_t NOTE_E5  = 76;
constexpr int8_t NOTE_F5  = 77;
constexpr int8_t NOTE_FS5 = 78;
constexpr int8_t NOTE_G5  = 79;
constexpr int8_t NOTE_A5  = 81;

// ADSR envelope for buzzer playback (times in ms, sustain in percent).
struct ADSR
{
    uint16_t attackMs;
    uint16_t decayMs;
    uint8_t sustainPct; // 0-100
    uint16_t releaseMs;
};

// Convert a MIDI note number to frequency in Hz (rounded).
int midiNoteToFrequencyHz(int8_t midiNote);
