#pragma once

#include "AudioFileSource.h"
#include "AudioGenerator.h"

class PhraseWavGenerator : public AudioGenerator
{
public:
    PhraseWavGenerator();
    ~PhraseWavGenerator() override;

    bool begin(AudioFileSource *source, AudioOutput *output) override;
    bool loop() override;
    bool stop() override;
    bool isRunning() override;
    void SetBufferSize(int sz);

private:
    bool readU32(uint32_t *dest);
    bool readU16(uint16_t *dest);
    bool readU8(uint8_t *dest);
    bool getBufferedData(int bytes, void *dest);
    bool readWavInfo();

    uint16_t channels_;
    uint32_t sampleRate_;
    uint16_t bitsPerSample_;
    uint32_t availBytes_;
    uint32_t buffSize_;
    uint8_t *buff_;
    uint16_t buffPtr_;
    uint16_t buffLen_;
};
