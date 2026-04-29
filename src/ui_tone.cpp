#include "ui_tone.h"

#include "buzzer.h"
#include "settings.h"

namespace
{
bool uiToneEnabled = true;
float uiToneVolume = 0.45f;
unsigned long lastUiToneMs = 0;

uint16_t uiToneAmplitude()
{
    const int base = map(constrain(buzzerVolume, 0, 100), 0, 100, 220, 1800);
    return static_cast<uint16_t>(constrain(static_cast<int>(base * uiToneVolume), 0, 2200));
}

void playToneStep(int freq, int durationMs)
{
    if (freq <= 0 || durationMs <= 0)
    {
        return;
    }
    if (buzzerVolume <= 0)
    {
        return;
    }
    if (!ensureSpeakerReady())
    {
        return;
    }

    speakerAudioOut().playTone(static_cast<uint16_t>(freq),
                               static_cast<uint16_t>(durationMs),
                               uiToneAmplitude());
}

void playToneSequence(const int *freqs, const int *durations, int count)
{
    if (freqs == nullptr || durations == nullptr || count <= 0)
    {
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        playToneStep(freqs[i], durations[i]);
        if (i + 1 < count)
        {
            delay(10);
        }
    }
}
} // namespace

void setUiToneEnabled(bool enabled)
{
    uiToneEnabled = enabled;
}

void setUiToneVolume(float volume)
{
    uiToneVolume = constrain(volume, 0.0f, 1.0f);
}

void playUiTone(UiTone tone)
{
    if (!uiToneEnabled)
    {
        return;
    }

    const unsigned long now = millis();
    if ((now - lastUiToneMs) < 70UL)
    {
        return;
    }
    lastUiToneMs = now;

    switch (tone)
    {
    case UI_TONE_UP:
    {
        const int freqs[] = {1480, 2120, 1760};
        const int durations[] = {16, 22, 18};
        playToneSequence(freqs, durations, 3);
        break;
    }
    case UI_TONE_DOWN:
    {
        const int freqs[] = {2120, 1480, 1180};
        const int durations[] = {16, 22, 22};
        playToneSequence(freqs, durations, 3);
        break;
    }
    case UI_TONE_LEFT:
    {
        const int freqs[] = {1320, 980};
        const int durations[] = {18, 24};
        playToneSequence(freqs, durations, 2);
        break;
    }
    case UI_TONE_RIGHT:
    {
        const int freqs[] = {1660, 2380, 1960};
        const int durations[] = {16, 22, 18};
        playToneSequence(freqs, durations, 3);
        break;
    }
    case UI_TONE_SELECT:
    {
        const int freqs[] = {1840, 2620, 2100};
        const int durations[] = {18, 28, 24};
        playToneSequence(freqs, durations, 3);
        break;
    }
    case UI_TONE_BACK:
    {
        const int freqs[] = {1180, 860};
        const int durations[] = {18, 28};
        playToneSequence(freqs, durations, 2);
        break;
    }
    case UI_TONE_ERROR:
    {
        const int freqs[] = {980, 760, 620};
        const int durations[] = {18, 20, 28};
        playToneSequence(freqs, durations, 3);
        break;
    }
    case UI_TONE_TOGGLE_ON:
    {
        const int freqs[] = {1440, 2060, 1680};
        const int durations[] = {16, 24, 22};
        playToneSequence(freqs, durations, 3);
        break;
    }
    case UI_TONE_TOGGLE_OFF:
    {
        const int freqs[] = {2060, 1440, 1120};
        const int durations[] = {16, 22, 24};
        playToneSequence(freqs, durations, 3);
        break;
    }
    case UI_TONE_VOLUME_UP:
    {
        const int freqs[] = {1320, 1760, 2360};
        const int durations[] = {14, 16, 24};
        playToneSequence(freqs, durations, 3);
        break;
    }
    case UI_TONE_VOLUME_DOWN:
    {
        const int freqs[] = {2360, 1760, 1320};
        const int durations[] = {14, 16, 24};
        playToneSequence(freqs, durations, 3);
        break;
    }
    }
}
