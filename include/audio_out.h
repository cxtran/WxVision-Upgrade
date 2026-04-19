#pragma once

#include <Arduino.h>
#include <driver/i2s_std.h>

class AudioOut
{
public:
    AudioOut();

    bool begin();
    bool setSampleRate(uint32_t sampleRate);
    void stop();
    void shutdown();
    void holdQuietPins();

    void playTone(uint16_t frequency, uint16_t durationMs, uint16_t amplitude = 3000);
    void beep();
    void writePcmMono(const int16_t *samples, size_t count);

private:
    i2s_chan_handle_t txHandle_;
    bool initialized_;
    bool channelEnabled_;
    uint32_t sampleRate_;

    static constexpr uint32_t kDefaultSampleRate = 22050;
    static constexpr int kBitsPerSample = 16;
    static constexpr size_t kBufferSamples = 256;

    bool ensureStarted_();
    void writeSamples_(const int16_t *samples, size_t count);
};
