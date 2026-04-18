#include "mp3_player.h"

#include <FS.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <memory>
#include <vector>

#include "AudioFileSourceID3.h"
#include "AudioFileSourceBuffer.h"
#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include "buzzer.h"
#include "display.h"
#include "pins.h"
#include "sd_card.h"

namespace wxv::audio
{
    namespace
    {
        String g_lastMp3Status = "idle";
        String g_lastMp3Path;
        unsigned int g_lastMp3FrameCount = 0;
        int g_volumePercent = 50;
        constexpr size_t kMp3StreamBufferBytes = 16384;

        std::unique_ptr<AudioFileSourceSD> g_file;
        std::unique_ptr<AudioFileSourceBuffer> g_bufferedFile;
        std::unique_ptr<AudioFileSourceID3> g_id3;
        std::unique_ptr<AudioOutputI2S> g_out;
        std::unique_ptr<AudioGeneratorMP3> g_mp3;
        std::unique_ptr<uint8_t, decltype(&heap_caps_free)> g_streamBuffer{nullptr, &heap_caps_free};
        bool g_active = false;

        float volumePercentToGain(int volumePercent)
        {
            const int clamped = constrain(volumePercent, 0, 100);
            const float normalized = static_cast<float>(clamped) / 100.0f;
            return 0.10f + (1.20f * normalized * normalized);
        }

        void cleanupPlayback(bool reenableSpeaker)
        {
            if (g_mp3)
            {
                g_mp3->stop();
            }
            if (g_out)
            {
                g_out->stop();
            }
            if (g_id3)
            {
            g_id3->close();
        }
        if (g_bufferedFile)
        {
            g_bufferedFile->close();
        }
        if (g_file)
        {
            g_file->close();
            }

        g_mp3.reset();
        g_out.reset();
        g_id3.reset();
        g_bufferedFile.reset();
        g_file.reset();
        g_streamBuffer.reset();
            g_active = false;

            if (reenableSpeaker)
            {
                setupBuzzer();
            }
        }

        uint8_t *allocateStreamBuffer(size_t bytes, bool &usedPsram)
        {
            usedPsram = false;

            if (psramFound())
            {
                uint8_t *ptr = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                if (ptr != nullptr)
                {
                    usedPsram = true;
                    return ptr;
                }
            }

            return static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
        }

        bool hasMp3Extension(const char *name)
        {
            if (name == nullptr)
            {
                return false;
            }

            const char *ext = strrchr(name, '.');
            return ext != nullptr && strcasecmp(ext, ".mp3") == 0;
        }

        bool collectMp3InDir(File &dir, Mp3PathList &paths, size_t maxCount, uint8_t depth)
        {
            if (!dir || !dir.isDirectory())
            {
                return false;
            }

            File entry = dir.openNextFile();
            while (entry)
            {
                if (entry.isDirectory())
                {
                    if (depth > 0 && collectMp3InDir(entry, paths, maxCount, depth - 1))
                    {
                        entry.close();
                        return true;
                    }
                }
                else if (hasMp3Extension(entry.name()))
                {
                    const char *entryPath = entry.path();
                    if (entryPath == nullptr || entryPath[0] == '\0')
                    {
                        entryPath = entry.name();
                    }

                    if (entryPath != nullptr && entryPath[0] != '\0')
                    {
                        paths.push_back(String(entryPath));
                    }

                    if (paths.size() >= maxCount)
                    {
                        entry.close();
                        return true;
                    }
                }

                entry.close();
                entry = dir.openNextFile();
            }

            return false;
        }
    } // namespace

    size_t listSdMp3Files(Mp3PathList &paths, size_t maxCount)
    {
        paths.clear();

        if (maxCount == 0)
        {
            return 0;
        }

        if (!wxv::storage::isMounted() && !wxv::storage::begin())
        {
            return 0;
        }

        File root = SD.open("/");
        if (!root)
        {
            return 0;
        }

        collectMp3InDir(root, paths, maxCount, 8);
        root.close();
        return paths.size();
    }

    bool testSdFileRead(const String &path, size_t &bytesRead, String &status, size_t probeBytes)
    {
        bytesRead = 0;
        status = "not started";

        if (path.isEmpty())
        {
            status = "empty path";
            return false;
        }

        if (!wxv::storage::isMounted() && !wxv::storage::begin())
        {
            status = "sd mount failed";
            return false;
        }

        File file = SD.open(path, FILE_READ);
        if (!file)
        {
            status = "open failed";
            return false;
        }

        uint8_t buffer[256];
        size_t remaining = probeBytes;

        while (remaining > 0)
        {
            const size_t chunk = min(remaining, sizeof(buffer));
            const size_t n = file.read(buffer, chunk);
            if (n == 0)
            {
                break;
            }
            bytesRead += n;
            remaining -= n;
        }

        file.close();

        if (bytesRead == 0)
        {
            status = "read 0 bytes";
            return false;
        }

        status = (bytesRead >= probeBytes) ? "probe ok" : "short read";
        return true;
    }

    bool findFirstSdMp3(String &path)
    {
        path = "";
        Mp3PathList paths;
        if (listSdMp3Files(paths, 1) == 0)
        {
            return false;
        }

        path = paths.front();
        return true;
    }

    bool startSdMp3(const String &path, int volumePercent)
    {
        stopSdMp3();

        g_lastMp3Status = "start";
        g_lastMp3Path = path;
        g_lastMp3FrameCount = 0;
        g_volumePercent = constrain(volumePercent, 0, 100);

        if (path.isEmpty())
        {
            g_lastMp3Status = "empty path";
            showSectionHeading("NO SD MP3", nullptr, 1200);
            return false;
        }

        if (!wxv::storage::isMounted() && !wxv::storage::begin())
        {
            g_lastMp3Status = "sd mount failed";
            showSectionHeading("SD MOUNT ERR", nullptr, 1200);
            return false;
        }

        File probe = SD.open(path, FILE_READ);
        if (!probe)
        {
            g_lastMp3Status = "open failed";
            showSectionHeading("MP3 OPEN ERR", nullptr, 1200);
            return false;
        }
        probe.close();

        releaseSpeaker();

        g_file.reset(new (std::nothrow) AudioFileSourceSD(path.c_str()));
        g_out.reset(new (std::nothrow) AudioOutputI2S());
        g_mp3.reset(new (std::nothrow) AudioGeneratorMP3());

        if (!g_file || !g_out || !g_mp3 || !g_file->isOpen())
        {
            g_lastMp3Status = "alloc/open failed";
            cleanupPlayback(true);
            showSectionHeading("MP3 INIT ERR", nullptr, 1200);
            return false;
        }

        bool usedPsram = false;
        g_streamBuffer.reset(allocateStreamBuffer(kMp3StreamBufferBytes, usedPsram));
        if (!g_streamBuffer)
        {
            g_lastMp3Status = "buffer alloc failed";
            cleanupPlayback(true);
            showSectionHeading("MP3 BUF ERR", nullptr, 1200);
            return false;
        }

        g_bufferedFile.reset(new (std::nothrow) AudioFileSourceBuffer(g_file.get(), g_streamBuffer.get(), kMp3StreamBufferBytes));
        if (!g_bufferedFile)
        {
            g_lastMp3Status = "buffer alloc failed";
            cleanupPlayback(true);
            showSectionHeading("MP3 INIT ERR", nullptr, 1200);
            return false;
        }

        g_id3.reset(new (std::nothrow) AudioFileSourceID3(g_bufferedFile.get()));
        if (!g_id3 || !g_bufferedFile->isOpen())
        {
            g_lastMp3Status = "id3 alloc failed";
            cleanupPlayback(true);
            showSectionHeading("MP3 INIT ERR", nullptr, 1200);
            return false;
        }

        g_out->SetPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);
        g_out->SetOutputModeMono(false);
        g_out->SetGain(volumePercentToGain(g_volumePercent));

        if (!g_mp3->begin(g_id3.get(), g_out.get()))
        {
            g_lastMp3Status = "begin failed";
            cleanupPlayback(true);
            showSectionHeading("MP3 BEGIN ERR", nullptr, 1200);
            return false;
        }

        g_lastMp3Status = usedPsram ? "playing psram" : "playing heap";
        g_active = true;
        return true;
    }

    void tickSdMp3()
    {
        if (!g_active || !g_mp3)
        {
            return;
        }

        if (!g_mp3->isRunning())
        {
            g_lastMp3Status = "done";
            cleanupPlayback(true);
            return;
        }

        if (!g_mp3->loop())
        {
            g_lastMp3Status = (g_lastMp3FrameCount == 0) ? "decode err" : "done";
            cleanupPlayback(true);
            return;
        }

        ++g_lastMp3FrameCount;
    }

    void stopSdMp3()
    {
        if (!g_active && !g_mp3 && !g_out && !g_id3 && !g_file)
        {
            return;
        }

        g_lastMp3Status = "stopped";
        cleanupPlayback(true);
    }

    bool isSdMp3Active()
    {
        return g_active;
    }

    bool setSdMp3VolumePercent(int volumePercent)
    {
        g_volumePercent = constrain(volumePercent, 0, 100);
        if (g_out)
        {
            return g_out->SetGain(volumePercentToGain(g_volumePercent));
        }
        return true;
    }

    int sdMp3VolumePercent()
    {
        return g_volumePercent;
    }

    bool playSdMp3(const String &path)
    {
        if (!startSdMp3(path, g_volumePercent))
        {
            return false;
        }

        showSectionHeading("PLAYING MP3", path.c_str(), 1000);
        while (isSdMp3Active())
        {
            tickSdMp3();
            delay(0);
        }

        if (g_lastMp3FrameCount == 0)
        {
            showSectionHeading("MP3 DECODE ERR", nullptr, 1200);
            return false;
        }

        showSectionHeading("MP3 DONE", path.c_str(), 1000);
        return true;
    }

    bool playFirstSdMp3(String *playedPath)
    {
        String path;
        if (!findFirstSdMp3(path))
        {
            g_lastMp3Status = "no mp3 found";
            showSectionHeading("NO SD MP3", nullptr, 1200);
            return false;
        }

        if (playedPath != nullptr)
        {
            *playedPath = path;
        }

        return playSdMp3(path);
    }

    const char *lastMp3Status()
    {
        return g_lastMp3Status.c_str();
    }

    const char *lastMp3Path()
    {
        return g_lastMp3Path.c_str();
    }

    unsigned int lastMp3FrameCount()
    {
        return g_lastMp3FrameCount;
    }
}
