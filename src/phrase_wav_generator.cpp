#include "phrase_wav_generator.h"

PhraseWavGenerator::PhraseWavGenerator()
    : channels_(0),
      sampleRate_(0),
      bitsPerSample_(0),
      availBytes_(0),
      buffSize_(128),
      buff_(nullptr),
      buffPtr_(0),
      buffLen_(0)
{
    running = false;
    file = nullptr;
    output = nullptr;
}

PhraseWavGenerator::~PhraseWavGenerator()
{
    free(buff_);
    buff_ = nullptr;
}

void PhraseWavGenerator::SetBufferSize(int sz)
{
    buffSize_ = sz;
}

bool PhraseWavGenerator::stop()
{
    if (!running)
    {
        return true;
    }
    running = false;
    if (file)
    {
        file->close();
    }
    return true;
}

bool PhraseWavGenerator::isRunning()
{
    return running;
}

bool PhraseWavGenerator::readU32(uint32_t *dest)
{
    return file->read(reinterpret_cast<uint8_t *>(dest), 4);
}

bool PhraseWavGenerator::readU16(uint16_t *dest)
{
    return file->read(reinterpret_cast<uint8_t *>(dest), 2);
}

bool PhraseWavGenerator::readU8(uint8_t *dest)
{
    return file->read(dest, 1);
}

bool PhraseWavGenerator::getBufferedData(int bytes, void *dest)
{
    if (!running)
    {
        return false;
    }

    uint8_t *p = reinterpret_cast<uint8_t *>(dest);
    while (bytes--)
    {
        if (buffPtr_ >= buffLen_)
        {
            buffPtr_ = 0;
            const uint32_t toRead = (availBytes_ > buffSize_) ? buffSize_ : availBytes_;
            buffLen_ = file->read(buff_, toRead);
            availBytes_ -= buffLen_;
        }
        if (buffPtr_ >= buffLen_)
        {
            return false;
        }
        *(p++) = buff_[buffPtr_++];
    }
    return true;
}

bool PhraseWavGenerator::loop()
{
    if (!running)
    {
        goto done;
    }

    if (!output->ConsumeSample(lastSample))
    {
        goto done;
    }

    do
    {
        if (bitsPerSample_ == 8)
        {
            uint8_t u8s = 0;
            if (!getBufferedData(1, &u8s))
            {
                running = false;
                break;
            }
            lastSample[AudioOutput::LEFTCHANNEL] = (static_cast<int16_t>(u8s) - 128) << 8;
            if (channels_ == 2)
            {
                if (!getBufferedData(1, &u8s))
                {
                    running = false;
                    break;
                }
                lastSample[AudioOutput::RIGHTCHANNEL] = (static_cast<int16_t>(u8s) - 128) << 8;
            }
            else
            {
                lastSample[AudioOutput::RIGHTCHANNEL] = lastSample[AudioOutput::LEFTCHANNEL];
            }
        }
        else if (bitsPerSample_ == 16)
        {
            if (!getBufferedData(2, &lastSample[AudioOutput::LEFTCHANNEL]))
            {
                running = false;
                break;
            }
            if (channels_ == 2)
            {
                if (!getBufferedData(2, &lastSample[AudioOutput::RIGHTCHANNEL]))
                {
                    running = false;
                    break;
                }
            }
            else
            {
                lastSample[AudioOutput::RIGHTCHANNEL] = lastSample[AudioOutput::LEFTCHANNEL];
            }
        }
    } while (running && output->ConsumeSample(lastSample));

done:
    if (file)
    {
        file->loop();
    }
    if (output)
    {
        output->loop();
    }
    return running;
}

bool PhraseWavGenerator::readWavInfo()
{
    uint32_t u32 = 0;
    uint16_t u16 = 0;
    int toSkip = 0;

    if (!readU32(&u32) || u32 != 0x46464952)
        return false;
    if (!readU32(&u32))
        return false;
    if (!readU32(&u32) || u32 != 0x45564157)
        return false;

    while (true)
    {
        if (!readU32(&u32))
            return false;
        if (u32 == 0x20746d66)
            break;
    }

    if (!readU32(&u32))
        return false;
    if (u32 == 16)
        toSkip = 0;
    else if (u32 == 18)
        toSkip = 2;
    else if (u32 == 40)
        toSkip = 24;
    else
        return false;

    if (!readU16(&u16) || u16 != 1)
        return false;
    if (!readU16(&channels_) || channels_ < 1 || channels_ > 2)
        return false;
    if (!readU32(&sampleRate_) || sampleRate_ < 1)
        return false;
    if (!readU32(&u32))
        return false;
    if (!readU16(&u16))
        return false;
    if (!readU16(&bitsPerSample_) || (bitsPerSample_ != 8 && bitsPerSample_ != 16))
        return false;

    while (toSkip-- > 0)
    {
        uint8_t ign = 0;
        if (!readU8(&ign))
            return false;
    }

    do
    {
        if (!readU32(&u32))
            return false;
        if (u32 == 0x61746164)
            break;
        if (!readU32(&u32))
            return false;
        while (u32--)
        {
            uint8_t ign = 0;
            if (!readU8(&ign))
                return false;
        }
    } while (true);

    if (!readU32(&availBytes_))
        return false;

    output->SetRate(sampleRate_);
    output->SetChannels(channels_);
    return true;
}

bool PhraseWavGenerator::begin(AudioFileSource *source, AudioOutput *out)
{
    if (!source || !out)
    {
        return false;
    }

    free(buff_);
    buff_ = reinterpret_cast<uint8_t *>(malloc(buffSize_));
    if (!buff_)
    {
        return false;
    }

    file = source;
    output = out;
    buffPtr_ = 0;
    buffLen_ = 0;

    if (!readWavInfo())
    {
        free(buff_);
        buff_ = nullptr;
        file = nullptr;
        output = nullptr;
        return false;
    }

    running = true;
    return true;
}
