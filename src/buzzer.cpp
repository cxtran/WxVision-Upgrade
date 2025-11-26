#include <Arduino.h>    
#include "pins.h"
#include "buzzer.h"    
#include "settings.h"
#define BUZZER_CHANNEL 0

void setupBuzzer() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW); // Ensure buzzer is off initially
    // Configure LEDC for tone generation
    ledcSetup(BUZZER_CHANNEL, 2000, 10);       // base freq/resolution
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL); // Attach once!
    ledcWriteTone(BUZZER_CHANNEL, 0);          // Make sure off
}

void playBuzzerTone(int frequency, int duration) {
    if (frequency <= 0 || duration <= 0) return; // Invalid parameters

    if (buzzerVolume <= 0) return;

    int duty = map(constrain(buzzerVolume, 0, 100), 0, 100, 0, 1023); // 10-bit resolution
    int freq = frequency;
    if (buzzerToneSet == 1) { // Soft profile
        freq = max(200, (frequency * 70) / 100);
    } else if (buzzerToneSet == 2) { // Click profile
        freq = 5000;
    }
    ledcWriteTone(BUZZER_CHANNEL, freq);  // Set frequency
    ledcWrite(BUZZER_CHANNEL, duty);           // Set duty (volume)
    delay(duration);                           // Wait
    ledcWrite(BUZZER_CHANNEL, 0);              // Stop tone
    ledcWriteTone(BUZZER_CHANNEL, 0);
}

void stopBuzzer() {
    ledcWriteTone(BUZZER_CHANNEL, 0);
}
