#include <Arduino.h>    
#include "pins.h"
#include "buzzer.h"    
#include "settings.h"
#include "music.h"
#include <math.h>
// Use a dedicated LEDC channel unlikely to conflict with other peripherals
static const int BUZZER_CHANNEL = 7;
static bool buzzerReady = false;

int midiNoteToFrequencyHz(int8_t midiNote)
{
    if (midiNote < 0) return 0;
    // Equal temperament: f = 440 * 2^((n-69)/12)
    float expv = (static_cast<float>(midiNote) - 69.0f) / 12.0f;
    float f = 440.0f * powf(2.0f, expv);
    int hz = static_cast<int>(lroundf(f));
    return max(0, hz);
}

static void playBuzzerToneADSRInternal(int frequency, int durationMs, int maxDuty, const ADSR &env)
{
    if (frequency <= 0 || durationMs <= 0 || maxDuty <= 0) return;

    constexpr int kStepMs = 5;

    int attackMs = static_cast<int>(env.attackMs);
    int decayMs = static_cast<int>(env.decayMs);
    int releaseMs = static_cast<int>(env.releaseMs);

    int envTotal = attackMs + decayMs + releaseMs;
    int sustainMs = durationMs - envTotal;
    if (sustainMs < 0)
    {
        int third = max(1, durationMs / 3);
        attackMs = third;
        decayMs = third;
        releaseMs = max(1, durationMs - attackMs - decayMs);
        sustainMs = 0;
    }

    int sustainDuty = (maxDuty * constrain(static_cast<int>(env.sustainPct), 0, 100)) / 100;

    ledcWriteTone(BUZZER_CHANNEL, frequency);

    auto rampDuty = [&](int fromDuty, int toDuty, int ms) {
        if (ms <= 0) return;
        int steps = max(1, ms / kStepMs);
        int stepMs = ms / steps;
        int remaining = ms - (steps * stepMs);
        for (int i = 1; i <= steps; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(steps);
            int duty = fromDuty + static_cast<int>(lroundf((toDuty - fromDuty) * t));
            ledcWrite(BUZZER_CHANNEL, duty);
            delay(stepMs);
        }
        if (remaining > 0) delay(remaining);
    };

    rampDuty(0, maxDuty, attackMs);
    rampDuty(maxDuty, sustainDuty, decayMs);

    if (sustainMs > 0)
    {
        ledcWrite(BUZZER_CHANNEL, sustainDuty);
        delay(sustainMs);
    }

    rampDuty(sustainDuty, 0, releaseMs);
    ledcWrite(BUZZER_CHANNEL, 0);
    ledcWriteTone(BUZZER_CHANNEL, 0);
}

void playBuzzerToneADSR(int frequency, int durationMs, const ADSR &env)
{
    if (frequency <= 0 || durationMs <= 0) return;
    if (!buzzerReady) setupBuzzer();
    if (buzzerVolume <= 0) return;

    int duty = map(constrain(buzzerVolume, 0, 100), 0, 100, 0, 1023);
    playBuzzerToneADSRInternal(frequency, durationMs, duty, env);
}

void playBuzzerPianoNoteADSR(int8_t midiNote, int durationMs, const ADSR &env)
{
    if (durationMs <= 0) return;
    int hz = midiNoteToFrequencyHz(midiNote);
    if (hz <= 0)
    {
        delay(durationMs);
        return;
    }
    playBuzzerToneADSR(hz, durationMs, env);
}

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
        case 5: // Warm: lower pitch slightly and lengthen sustain for music
            freq = max(180, (frequency * 85) / 100);
            dur = duration + 30;
            break;
        case 6: // Melody: keep accurate pitch, shape with ADSR envelope
            freq = frequency;
            dur = duration;
            break;
        default: // Bright
            break;
    }

    if (buzzerToneSet == 6)
    {
        const ADSR env{18, 35, 55, 45};
        playBuzzerToneADSRInternal(freq, dur, duty, env);
        return;
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
