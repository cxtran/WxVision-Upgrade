#include <Arduino.h>
#include <math.h>

#include "pins.h"
#include "buzzer.h"
#include "settings.h"
#include "music.h"
#include "audio_out.h"
#include "mp3_player.h"

static bool buzzerReady = false;
static AudioOut speaker;

namespace
{
constexpr uint32_t kUiSampleRate = 32000;
constexpr uint32_t kAlarmSampleRate = 32000;
constexpr size_t kSynthBufferSamples = 160;

enum class VoiceProfile : uint8_t
{
    Bright = 0,
    Soft = 1,
    Click = 2,
    Chime = 3,
    Pulse = 4,
    Warm = 5,
    Piano = 6,
    Alarm = 7,
};

float clampUnit_(float v)
{
    if (v < -1.0f)
    {
        return -1.0f;
    }
    if (v > 1.0f)
    {
        return 1.0f;
    }
    return v;
}

float applyEnvelope_(float tMs, float durationMs, const ADSR &env)
{
    const float attackMs = max(1.0f, static_cast<float>(env.attackMs));
    const float decayMs = max(0.0f, static_cast<float>(env.decayMs));
    const float releaseMs = max(1.0f, static_cast<float>(env.releaseMs));
    const float sustainLevel = constrain(static_cast<float>(env.sustainPct) / 100.0f, 0.0f, 1.0f);
    const float sustainStart = attackMs + decayMs;
    const float releaseStart = max(sustainStart, durationMs - releaseMs);

    if (tMs < attackMs)
    {
        const float x = tMs / attackMs;
        return x * x;
    }

    if (tMs < sustainStart)
    {
        const float x = (tMs - attackMs) / max(1.0f, decayMs);
        return 1.0f + (sustainLevel - 1.0f) * x;
    }

    if (tMs < releaseStart)
    {
        return sustainLevel;
    }

    const float x = (tMs - releaseStart) / max(1.0f, durationMs - releaseStart);
    return sustainLevel * (1.0f - x);
}

float synthVoiceSample_(VoiceProfile voice, float phase, float phase2, float phase3, float tSec, float env)
{
    const float s1 = sinf(phase);
    const float s2 = sinf(phase2);
    const float s3 = sinf(phase3);
    const float s4 = sinf(phase * 4.0f);
    const float slowDecay = 0.75f + 0.25f * expf(-3.8f * tSec);
    const float fastDecay = expf(-8.0f * tSec);

    float sample = 0.0f;
    switch (voice)
    {
    case VoiceProfile::Soft:
        sample = (0.86f * s1) + (0.10f * s2 * 0.7f) + (0.04f * s3 * 0.5f);
        break;
    case VoiceProfile::Click:
        sample = ((0.70f * s3) + (0.45f * s4) + (0.18f * s2)) * fastDecay;
        sample += 0.12f * s1 * slowDecay;
        break;
    case VoiceProfile::Chime:
        sample = ((0.66f * s1) + (0.28f * s2 * fastDecay) + (0.22f * sinf(phase * 2.71f) * fastDecay));
        sample += 0.12f * sinf(phase * 4.08f) * expf(-5.2f * tSec);
        break;
    case VoiceProfile::Pulse:
        sample = (0.72f * s1) + (0.20f * s2) + (0.08f * s3);
        sample *= (0.76f + 0.24f * sinf(2.0f * PI * 7.0f * tSec));
        break;
    case VoiceProfile::Warm:
        sample = (0.92f * s1) + (0.18f * s2 * 0.55f) + (0.08f * s3 * 0.35f);
        sample = tanhf(sample * 0.9f);
        break;
    case VoiceProfile::Piano:
        sample = (0.86f * s1 * slowDecay) +
                 (0.32f * sinf(phase * 2.01f) * expf(-5.8f * tSec)) +
                 (0.16f * sinf(phase * 3.08f) * expf(-8.5f * tSec)) +
                 (0.08f * sinf(phase * 4.21f) * expf(-11.0f * tSec));
        sample += 0.04f * s2 * env;
        break;
    case VoiceProfile::Alarm:
        sample = (0.78f * s1) + (0.24f * s2) + (0.12f * s3);
        sample += 0.06f * sinf(phase * 0.5f + phase2);
        break;
    case VoiceProfile::Bright:
    default:
        sample = (0.82f * s1) + (0.24f * s2) + (0.14f * s3) + (0.06f * s4 * fastDecay);
        break;
    }

    return clampUnit_(sample * env);
}

void renderVoiceTone_(int frequency, int durationMs, uint16_t peakAmp, const ADSR &env, VoiceProfile voice, uint32_t sampleRate)
{
    if (frequency <= 0 || durationMs <= 0 || peakAmp == 0)
    {
        return;
    }

    speaker.setSampleRate(sampleRate);
    const uint32_t totalSamples = (sampleRate * static_cast<uint32_t>(durationMs)) / 1000U;
    int16_t buffer[kSynthBufferSamples];
    uint32_t generated = 0;

    while (generated < totalSamples)
    {
        const size_t chunk = min<size_t>(kSynthBufferSamples, totalSamples - generated);
        for (size_t i = 0; i < chunk; ++i)
        {
            const uint32_t index = generated + static_cast<uint32_t>(i);
            const float tSec = static_cast<float>(index) / static_cast<float>(sampleRate);
            const float tMs = tSec * 1000.0f;
            const float envGain = applyEnvelope_(tMs, static_cast<float>(durationMs), env);

            float pitchMod = 1.0f;
            if (voice == VoiceProfile::Chime)
            {
                pitchMod += 0.0035f * sinf(2.0f * PI * 5.0f * tSec);
            }
            else if (voice == VoiceProfile::Pulse)
            {
                pitchMod += 0.0040f * sinf(2.0f * PI * 4.0f * tSec);
            }
            else if (voice == VoiceProfile::Alarm)
            {
                pitchMod += 0.0065f * sinf(2.0f * PI * 6.5f * tSec);
            }

            const float baseFreq = static_cast<float>(frequency) * pitchMod;
            const float phase = 2.0f * PI * baseFreq * tSec;
            const float phase2 = 2.0f * PI * baseFreq * 2.0f * tSec;
            const float phase3 = 2.0f * PI * baseFreq * 3.0f * tSec;
            const float sample = synthVoiceSample_(voice, phase, phase2, phase3, tSec, envGain);
            buffer[i] = static_cast<int16_t>(sample * static_cast<float>(peakAmp));
        }

        speaker.writePcmMono(buffer, chunk);
        generated += static_cast<uint32_t>(chunk);
    }

    speaker.stop();
}

VoiceProfile currentUiVoice_()
{
    switch (buzzerToneSet)
    {
    case 1:
        return VoiceProfile::Soft;
    case 2:
        return VoiceProfile::Click;
    case 3:
        return VoiceProfile::Chime;
    case 4:
        return VoiceProfile::Pulse;
    case 5:
        return VoiceProfile::Warm;
    case 6:
        return VoiceProfile::Piano;
    default:
        return VoiceProfile::Bright;
    }
}
} // namespace

int midiNoteToFrequencyHz(int8_t midiNote)
{
    if (midiNote < 0)
    {
        return 0;
    }
    float expv = (static_cast<float>(midiNote) - 69.0f) / 12.0f;
    float f = 440.0f * powf(2.0f, expv);
    return max(0, static_cast<int>(lroundf(f)));
}

static uint16_t volumeToAmplitude_()
{
    return static_cast<uint16_t>(map(constrain(buzzerVolume, 0, 100), 0, 100, 600, 5200));
}

void setupBuzzer()
{
    if (wxv::audio::isSdMp3Active())
    {
        return;
    }

    speaker.holdQuietPins();
}

bool ensureSpeakerReady()
{
    if (wxv::audio::isSdMp3Active())
    {
        return false;
    }

    if (!buzzerReady)
    {
        buzzerReady = speaker.begin();
        if (buzzerReady)
        {
            Serial.println("Speaker initialized");
        }
        else
        {
            Serial.println("Speaker init failed");
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
    renderVoiceTone_(frequency, durationMs, peakAmp, env, VoiceProfile::Piano, kAlarmSampleRate);
}

void playBuzzerToneADSR(int frequency, int durationMs, const ADSR &env)
{
    if (frequency <= 0 || durationMs <= 0 || wxv::audio::isSdMp3Active() || !ensureSpeakerReady())
    {
        return;
    }

    playBuzzerToneADSRInternal(frequency, durationMs, volumeToAmplitude_(), env);
}

void playBuzzerPianoNoteADSR(int8_t midiNote, int durationMs, const ADSR &env)
{
    if (durationMs <= 0)
    {
        return;
    }

    const int hz = midiNoteToFrequencyHz(midiNote);
    if (hz <= 0)
    {
        delay(durationMs);
        return;
    }

    playBuzzerToneADSR(hz, durationMs, env);
}

void playBuzzerTone(int frequency, int duration)
{
    if (frequency <= 0 || duration <= 0 || wxv::audio::isSdMp3Active() || !ensureSpeakerReady())
    {
        return;
    }

    int freq = frequency;
    int dur = duration;
    uint16_t amp = volumeToAmplitude_();
    ADSR env{8, 22, 62, 40};
    VoiceProfile voice = currentUiVoice_();

    switch (buzzerToneSet)
    {
    case 1:
        freq = max(200, (frequency * 70) / 100);
        dur = duration + 12;
        amp = static_cast<uint16_t>(max(180, static_cast<int>(amp * 0.52f)));
        env = ADSR{12, 28, 58, 55};
        break;
    case 2:
        freq = min(5200, max(2800, frequency * 2));
        dur = min(duration, 45);
        amp = static_cast<uint16_t>(max(220, static_cast<int>(amp * 0.38f)));
        env = ADSR{2, 8, 12, 16};
        break;
    case 3:
        freq = 1200 + (frequency / 3);
        dur = duration + 40;
        amp = static_cast<uint16_t>(max(240, static_cast<int>(amp * 0.62f)));
        env = ADSR{5, 40, 36, 85};
        break;
    case 4:
        amp = static_cast<uint16_t>(max(200, static_cast<int>(amp * 0.56f)));
        env = ADSR{6, 18, 72, 36};
        break;
    case 5:
        freq = max(180, (frequency * 85) / 100);
        dur = duration + 32;
        amp = static_cast<uint16_t>(max(180, static_cast<int>(amp * 0.54f)));
        env = ADSR{14, 34, 60, 62};
        break;
    case 6:
        amp = static_cast<uint16_t>(max(260, static_cast<int>(amp * 0.68f)));
        env = ADSR{6, 36, 42, 90};
        break;
    default:
        amp = static_cast<uint16_t>(max(220, static_cast<int>(amp * 0.66f)));
        env = ADSR{6, 20, 60, 32};
        break;
    }

    renderVoiceTone_(freq, dur, amp, env, voice, kUiSampleRate);

    if (buzzerToneSet == 4)
    {
        delay(30);
        renderVoiceTone_(freq, 42, static_cast<uint16_t>(amp * 0.8f), ADSR{2, 8, 28, 18}, voice, kUiSampleRate);
    }
    else if (buzzerToneSet == 6)
    {
        delay(10);
        renderVoiceTone_(freq + 170, max(26, dur / 2), static_cast<uint16_t>(amp * 0.55f), ADSR{4, 18, 34, 55}, voice, kUiSampleRate);
    }
}

void stopBuzzer()
{
    if (wxv::audio::isSdMp3Active() || !buzzerReady)
    {
        return;
    }

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
