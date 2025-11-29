#include <Arduino.h>    
#include "pins.h"
#include "buzzer.h"    
#include "settings.h"
// Use a dedicated LEDC channel unlikely to conflict with other peripherals
static const int BUZZER_CHANNEL = 7;
static bool buzzerReady = false;

void setupBuzzer() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW); // Ensure buzzer is off initially
    // Configure LEDC for tone generation
    ledcSetup(BUZZER_CHANNEL, 2000, 10);       // base freq/resolution
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL); // Attach once!
    ledcWriteTone(BUZZER_CHANNEL, 0);          // Make sure off
    ledcWrite(BUZZER_CHANNEL, 0);
    buzzerReady = true;
}

void playBuzzerTone(int frequency, int duration) {
    if (frequency <= 0 || duration <= 0) return; // Invalid parameters
    if (!buzzerReady) {
        setupBuzzer();
    }
    if (buzzerVolume <= 0) return;

    int duty = map(constrain(buzzerVolume, 0, 100), 0, 100, 0, 1023); // 10-bit resolution
    int freq = frequency;
    int dur = duration;
    switch (buzzerToneSet) {
        case 1: // Soft
            freq = max(200, (frequency * 70) / 100);
            break;
        case 2: // Click
            freq = 5000;
            dur = min(duration, 50);
            break;
        case 3: // Chime
            freq = 1500 + (frequency / 4);
            dur = duration + 20;
            break;
        case 4: // Pulse
            freq = frequency;
            dur = duration;
            break;
        default: // Bright
            break;
    }
    ledcWriteTone(BUZZER_CHANNEL, freq);  // Set frequency
    ledcWrite(BUZZER_CHANNEL, duty);           // Set duty (volume)
    delay(dur);                                // Wait
    if (buzzerToneSet == 4) { // Pulse: brief gap then quick second pulse
        ledcWrite(BUZZER_CHANNEL, 0);
        delay(30);
        ledcWrite(BUZZER_CHANNEL, duty);
        delay(40);
    }
    ledcWrite(BUZZER_CHANNEL, 0);              // Stop tone
    ledcWriteTone(BUZZER_CHANNEL, 0);
}

void stopBuzzer() {
    ledcWriteTone(BUZZER_CHANNEL, 0);
    ledcWrite(BUZZER_CHANNEL, 0);
}
