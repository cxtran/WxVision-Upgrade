#pragma once

#include <Arduino.h>

#include "audio_out.h"
#include "music.h"
#include "pins.h"

void setupBuzzer();
bool ensureSpeakerReady();
AudioOut &speakerAudioOut();
void releaseSpeaker();
void playBuzzerTone(int frequency, int duration);
void playBuzzerToneADSR(int frequency, int durationMs, const ADSR &env);
void playBuzzerPianoNoteADSR(int8_t midiNote, int durationMs, const ADSR &env);
void tone(uint8_t _pin, unsigned int frequency, unsigned long duration);
void noTone(uint8_t _pin);
void stopBuzzer();
