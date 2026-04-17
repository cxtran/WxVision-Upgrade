#pragma once

#include <Arduino.h>
#include <vector>

namespace wxv::audio
{
    size_t listSdMp3Files(std::vector<String> &paths, size_t maxCount = 8);
    bool testSdFileRead(const String &path, size_t &bytesRead, String &status, size_t probeBytes = 4096);
    bool findFirstSdMp3(String &path);
    bool startSdMp3(const String &path, int volumePercent = 50);
    void tickSdMp3();
    void stopSdMp3();
    bool isSdMp3Active();
    bool setSdMp3VolumePercent(int volumePercent);
    int sdMp3VolumePercent();
    bool playSdMp3(const String &path);
    bool playFirstSdMp3(String *playedPath = nullptr);
    const char *lastMp3Status();
    const char *lastMp3Path();
    unsigned int lastMp3FrameCount();
}
