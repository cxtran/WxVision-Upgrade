#include <Arduino.h>    
#include "pins.h"
#include "buzzer.h"    
#define BUZZER_CHANNEL 0

void setupBuzzer() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW); // Ensure buzzer is off initially
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL); // Attach once!
    ledcWriteTone(BUZZER_CHANNEL, 0);          // Make sure off
}   

void playBuzzerTone(int frequency, int duration) {
    if (frequency <= 0 || duration <= 0) return; // Invalid parameters

    ledcWriteTone(BUZZER_CHANNEL, frequency);  // Start tone
    delay(duration);                           // Wait
    ledcWriteTone(BUZZER_CHANNEL, 0);          // Stop tone
}

void stopBuzzer() {
    ledcWriteTone(BUZZER_CHANNEL, 0);
}
