#pragma once
#include <Arduino.h>
#include "pins.h"
void setupBuzzer();
void playBuzzerTone(int frequency, int duration);
void tone(uint8_t _pin, unsigned int frequency, unsigned long duration);
void noTone(uint8_t _pin);
void stopBuzzer(); // Stops any ongoing tone
