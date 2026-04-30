#pragma once

#include <Arduino.h>

#include "audio_out.h"
#include "music.h"
#include "pins.h"

void setupSpeaker();
bool ensureSpeakerReady();
AudioOut &speakerAudioOut();
void releaseSpeaker();
void playSpeakerTone(int frequency, int duration);
void playSpeakerToneADSR(int frequency, int durationMs, const ADSR &env);
void playSpeakerPianoNoteADSR(int8_t midiNote, int durationMs, const ADSR &env);
void tone(uint8_t _pin, unsigned int frequency, unsigned long duration);
void noTone(uint8_t _pin);
void stopSpeaker();
