#include <Arduino.h>
#include <math.h>
#include "pins.h"
#include "buzzer.h"
#include "settings.h"
#include "music.h"
#include "audio_out.h"
#include "audio_announcer.h"
#include "mp3_player.h"

static bool buzzerReady = false;
static AudioOut speaker;

int midiNoteToFrequencyHz(int8_t midiNote)
{
    if (midiNote < 0) return 0;
    float expv = (static_cast<float>(midiNote) - 69.0f) / 12.0f;
    float f = 440.0f * powf(2.0f, expv);
    int hz = static_cast<int>(lroundf(f));
    return max(0, hz);
}

static uint16_t volumeToAmplitude_()
{
    return static_cast<uint16_t>(
        map(constrain(buzzerVolume, 0, 100), 0, 100, 300, 2500)
    );
}

void setupBuzzer()
{
    if (wxv::audio::isSdMp3Active() || wxv::announce::isActive())
        return;

    speaker.holdQuietPins();
}

bool ensureSpeakerReady()
{
    if (wxv::audio::isSdMp3Active() || wxv::announce::isActive())
    {
        return false;
    }

    if (!buzzerReady)
    {
        buzzerReady = speaker.begin();
        if (buzzerReady)
        {
            Serial.println("Speaker wrapper initialized");
        }
        else
        {
            Serial.println("Speaker wrapper init failed");
        }
    }

    return buzzerReady;
}

AudioOut &speakerAudioOut()
{
    return speaker;
}

void releaseSpeaker()
{
    speaker.stop();
    speaker.shutdown();
    buzzerReady = false;
}

static void playBuzzerToneADSRInternal(int frequency, int durationMs, uint16_t peakAmp, const ADSR &env)
{
    if (frequency <= 0 || durationMs <= 0 || peakAmp == 0) return;

    constexpr int kStepMs = 8;

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

    uint16_t sustainAmp = static_cast<uint16_t>(
        (peakAmp * constrain(static_cast<int>(env.sustainPct), 0, 100)) / 100
    );

    auto rampSegment = [&](uint16_t fromAmp, uint16_t toAmp, int ms)
    {
        if (ms <= 0) return;
        int steps = max(1, ms / kStepMs);
        int stepDur = max(1, ms / steps);

        for (int i = 1; i <= steps; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(steps);
            uint16_t amp = static_cast<uint16_t>(fromAmp + (toAmp - fromAmp) * t);
            speaker.playTone(frequency, stepDur, amp);
        }
    };

    rampSegment(0, peakAmp, attackMs);
    rampSegment(peakAmp, sustainAmp, decayMs);

    if (sustainMs > 0)
    {
        speaker.playTone(frequency, sustainMs, sustainAmp);
    }

    rampSegment(sustainAmp, 0, releaseMs);
}

void playBuzzerToneADSR(int frequency, int durationMs, const ADSR &env)
{
    if (frequency <= 0 || durationMs <= 0) return;
    if (wxv::audio::isSdMp3Active() || wxv::announce::isActive()) return;
    if (!ensureSpeakerReady()) return;
    if (buzzerVolume <= 0) return;

    uint16_t amp = volumeToAmplitude_();
    playBuzzerToneADSRInternal(frequency, durationMs, amp, env);
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

void playBuzzerTone(int frequency, int duration)
{
    if (frequency <= 0 || duration <= 0) return;
    if (wxv::audio::isSdMp3Active() || wxv::announce::isActive()) return;
    if (!ensureSpeakerReady()) return;
    if (buzzerVolume <= 0) return;

    int freq = frequency;
    int dur = duration;
    uint16_t amp = static_cast<uint16_t>(max(100, static_cast<int>(volumeToAmplitude_() * 0.70f)));

    speaker.playTone(freq, dur, amp);
}

void stopBuzzer()
{
    if (wxv::audio::isSdMp3Active() || wxv::announce::isActive()) return;
    if (!buzzerReady) return;
    speaker.stop();
}

void tone(uint8_t _pin, unsigned int frequency, unsigned long duration)
{
    (void)_pin;
    playBuzzerTone(static_cast<int>(frequency), static_cast<int>(duration));
}

void noTone(uint8_t _pin)
{
    (void)_pin;
    stopBuzzer();
}
