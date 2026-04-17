#include "audio_out.h"

#include <math.h>

#include "pins.h"

namespace
{
constexpr uint32_t kDmaDescCount = 8;
constexpr uint32_t kDmaFrameCount = 256;
}

AudioOut::AudioOut()
    : txHandle_(nullptr), initialized_(false), channelEnabled_(false), sampleRate_(kDefaultSampleRate)
{
}

bool AudioOut::begin()
{
    if (initialized_)
    {
        return true;
    }

    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chanCfg.dma_desc_num = kDmaDescCount;
    chanCfg.dma_frame_num = kDmaFrameCount;

    esp_err_t err = i2s_new_channel(&chanCfg, &txHandle_, nullptr);
    if (err != ESP_OK)
    {
        Serial.printf("AudioOut: i2s_new_channel failed: %d\n", err);
        txHandle_ = nullptr;
        return false;
    }

    i2s_std_config_t stdCfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate_),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = static_cast<gpio_num_t>(I2S_BCLK_PIN),
            .ws = static_cast<gpio_num_t>(I2S_LRC_PIN),
            .dout = static_cast<gpio_num_t>(I2S_DOUT_PIN),
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(txHandle_, &stdCfg);
    if (err != ESP_OK)
    {
        Serial.printf("AudioOut: i2s_channel_init_std_mode failed: %d\n", err);
        i2s_del_channel(txHandle_);
        txHandle_ = nullptr;
        return false;
    }

    initialized_ = true;
    channelEnabled_ = false;
    return ensureStarted_();
}

bool AudioOut::ensureStarted_()
{
    if (!initialized_ || txHandle_ == nullptr)
    {
        return false;
    }

    if (channelEnabled_)
    {
        return true;
    }

    const esp_err_t err = i2s_channel_enable(txHandle_);
    if (err != ESP_OK)
    {
        Serial.printf("AudioOut: i2s_channel_enable failed: %d\n", err);
        return false;
    }

    channelEnabled_ = true;
    return true;
}

bool AudioOut::setSampleRate(uint32_t sampleRate)
{
    if (sampleRate == 0)
    {
        return false;
    }

    sampleRate_ = sampleRate;

    if (!initialized_ || txHandle_ == nullptr)
    {
        return true;
    }

    i2s_std_clk_config_t clkCfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate_);
    esp_err_t err = ESP_OK;
    if (channelEnabled_)
    {
        err = i2s_channel_disable(txHandle_);
        if (err != ESP_OK)
        {
            Serial.printf("AudioOut: i2s_channel_disable failed: %d\n", err);
            return false;
        }
        channelEnabled_ = false;
    }

    err = i2s_channel_reconfig_std_clock(txHandle_, &clkCfg);
    if (err != ESP_OK)
    {
        Serial.printf("AudioOut: i2s_channel_reconfig_std_clock failed: %d\n", err);
        return false;
    }

    return ensureStarted_();
}

void AudioOut::stop()
{
    if (!initialized_ || txHandle_ == nullptr)
    {
        return;
    }

    if (!channelEnabled_)
    {
        return;
    }

    const esp_err_t err = i2s_channel_disable(txHandle_);
    if (err != ESP_OK)
    {
        Serial.printf("AudioOut: i2s_channel_disable failed: %d\n", err);
        return;
    }

    channelEnabled_ = false;
}

void AudioOut::shutdown()
{
    if (!initialized_ || txHandle_ == nullptr)
    {
        return;
    }

    if (channelEnabled_)
    {
        i2s_channel_disable(txHandle_);
        channelEnabled_ = false;
    }
    i2s_del_channel(txHandle_);
    txHandle_ = nullptr;
    initialized_ = false;
}

void AudioOut::writeSamples_(const int16_t *samples, size_t count)
{
    if ((!initialized_ && !begin()) || samples == nullptr || count == 0)
    {
        return;
    }

    if (!ensureStarted_())
    {
        return;
    }

    int16_t stereoBuffer[kBufferSamples * 2];
    size_t writtenSamples = 0;

    while (writtenSamples < count)
    {
        const size_t chunk = min<size_t>(kBufferSamples, count - writtenSamples);
        for (size_t i = 0; i < chunk; ++i)
        {
            const int16_t sample = samples[writtenSamples + i];
            stereoBuffer[i * 2] = sample;
            stereoBuffer[i * 2 + 1] = sample;
        }

        size_t bytesWritten = 0;
        const size_t byteCount = chunk * 2 * sizeof(int16_t);
        const esp_err_t err = i2s_channel_write(txHandle_, stereoBuffer, byteCount, &bytesWritten, portMAX_DELAY);
        if (err != ESP_OK)
        {
            Serial.printf("AudioOut: i2s_channel_write failed: %d\n", err);
            return;
        }

        writtenSamples += bytesWritten / (2 * sizeof(int16_t));
    }
}

void AudioOut::playTone(uint16_t frequency, uint16_t durationMs, uint16_t amplitude)
{
    if (frequency == 0 || durationMs == 0)
    {
        return;
    }

    if (!initialized_ && !begin())
    {
        return;
    }

    if (amplitude > 32000)
    {
        amplitude = 32000;
    }

    const uint32_t totalSamples = (sampleRate_ * durationMs) / 1000U;
    int16_t buffer[kBufferSamples];
    uint32_t generated = 0;

    while (generated < totalSamples)
    {
        const size_t chunk = min<size_t>(kBufferSamples, totalSamples - generated);
        for (size_t i = 0; i < chunk; ++i)
        {
            const float t = static_cast<float>(generated + i) / static_cast<float>(sampleRate_);
            const float s = sinf(2.0f * PI * static_cast<float>(frequency) * t);
            buffer[i] = static_cast<int16_t>(s * amplitude);
        }

        writeSamples_(buffer, chunk);
        generated += chunk;
    }

    stop();
}

void AudioOut::writePcmMono(const int16_t *samples, size_t count)
{
    writeSamples_(samples, count);
}

void AudioOut::beep()
{
    playTone(1000, 80, 600);
}

void AudioOut::playSuccessTone()
{
    playTone(1200, 100, 2800);
    delay(40);
    playTone(1800, 140, 2800);
}

void AudioOut::playErrorTone()
{
    playTone(500, 180, 3000);
    delay(60);
    playTone(350, 220, 3000);
}

const int C4 = 262;
const int E4 = 330;
const int GS4 = 415;

void AudioOut::playFurElise()
{
    const int amp = 1500;
    const int E5 = 659;
    const int DS5 = 622;
    const int B4 = 494;
    const int D5 = 587;
    const int C5 = 523;
    const int A4 = 440;
    const int REST = 0;

    int melody[] = {
        E5, DS5, E5, DS5, E5, B4, D5, C5, A4,
        REST,
        C4, E4, A4, B4,
        REST,
        E4, GS4, B4, C5,
        REST,
        E4, E5, DS5, E5, DS5, E5, B4, D5, C5, A4};

    int duration[] = {
        150, 150, 150, 150, 150, 150, 150, 150, 300,
        150,
        150, 150, 150, 300,
        150,
        150, 150, 150, 300,
        150,
        150, 150, 150, 150, 150, 150, 150, 150, 300};

    const int notes = sizeof(melody) / sizeof(melody[0]);
    for (int i = 0; i < notes; ++i)
    {
        if (melody[i] > 0)
        {
            playTone(melody[i], duration[i], amp);
        }
        delay(duration[i] * 0.3);
    }
}
