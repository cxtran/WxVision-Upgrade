#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

namespace wxv::storage
{
    bool begin();
    void end();
    bool isMounted();
    const char *lastStatus();

    uint8_t cardType();
    uint64_t cardSizeMB();
    uint64_t totalMB();
    uint64_t usedMB();

    bool exists(const char *path);
    bool mkdir(const char *path);
    bool rmdir(const char *path);
    bool remove(const char *path);

    bool writeText(const char *path, const String &content, bool append = false);
    bool readText(const char *path, String &out);

    void listDir(const char *dirname = "/", uint8_t levels = 1);

    const char *cardTypeName();
}
