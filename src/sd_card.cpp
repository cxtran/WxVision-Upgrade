#include "sd_card.h"

#include "pins.h"

namespace wxv::storage
{
    namespace
    {
        SPIClass sdSpi(FSPI);
        bool g_mounted = false;
        uint8_t g_cardType = CARD_NONE;
        String g_lastStatus = "Not initialized";
        unsigned long g_lastBeginAttemptMs = 0;
        constexpr unsigned long kBeginRetryCooldownMs = 1500UL;

        const char *cardTypeToString(uint8_t type)
        {
            switch (type)
            {
            case CARD_MMC:
                return "MMC";
            case CARD_SD:
                return "SDSC";
            case CARD_SDHC:
                return "SDHC/SDXC";
            default:
                return "NONE";
            }
        }

        void resetBusState()
        {
            SD.end();
            sdSpi.end();
            g_mounted = false;
            g_cardType = CARD_NONE;
        }

        void setStatus(const String &status)
        {
            g_lastStatus = status;
            Serial.printf("[SD] %s\n", g_lastStatus.c_str());
        }
    }

    bool begin()
    {
        if (g_mounted)
        {
            setStatus("Already mounted");
            return true;
        }

        const unsigned long now = millis();
        if (g_lastBeginAttemptMs != 0 && (now - g_lastBeginAttemptMs) < kBeginRetryCooldownMs)
        {
            setStatus("Retry blocked, wait 1.5s");
            return false;
        }
        g_lastBeginAttemptMs = now;

        resetBusState();

        pinMode(SD_CS_PIN, OUTPUT);
        digitalWrite(SD_CS_PIN, HIGH);
        pinMode(SD_SCK_PIN, OUTPUT);
        pinMode(SD_MOSI_PIN, OUTPUT);
        pinMode(SD_MISO_PIN, INPUT_PULLUP);
        delay(20);

        sdSpi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

        constexpr uint32_t kSdFreqHz = 1000000;
        if (!SD.begin(SD_CS_PIN, sdSpi, kSdFreqHz))
        {
            setStatus(String("Mount failed at ") + String(static_cast<unsigned long>(kSdFreqHz)) + " Hz");
            resetBusState();
            return false;
        }

        g_cardType = SD.cardType();
        if (g_cardType == CARD_NONE)
        {
            setStatus("No card attached");
            resetBusState();
            return false;
        }

        g_mounted = true;
        setStatus(String("Mounted: ") + cardTypeToString(g_cardType));
        Serial.printf("[SD] Size: %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));
        Serial.printf("[SD] Total: %llu MB\n", SD.totalBytes() / (1024ULL * 1024ULL));
        Serial.printf("[SD] Used: %llu MB\n", SD.usedBytes() / (1024ULL * 1024ULL));

        return true;
    }

    void end()
    {
        setStatus("Unmounted");
        resetBusState();
    }

    bool isMounted()
    {
        return g_mounted;
    }

    const char *lastStatus()
    {
        return g_lastStatus.c_str();
    }

    uint8_t cardType()
    {
        return g_cardType;
    }

    uint64_t cardSizeMB()
    {
        return g_mounted ? SD.cardSize() / (1024ULL * 1024ULL) : 0;
    }

    uint64_t totalMB()
    {
        return g_mounted ? SD.totalBytes() / (1024ULL * 1024ULL) : 0;
    }

    uint64_t usedMB()
    {
        return g_mounted ? SD.usedBytes() / (1024ULL * 1024ULL) : 0;
    }

    bool exists(const char *path)
    {
        return g_mounted && path != nullptr && SD.exists(path);
    }

    bool mkdir(const char *path)
    {
        if (!g_mounted || path == nullptr)
        {
            return false;
        }

        if (SD.exists(path))
        {
            return true;
        }

        return SD.mkdir(path);
    }

    bool rmdir(const char *path)
    {
        return g_mounted && path != nullptr && SD.rmdir(path);
    }

    bool remove(const char *path)
    {
        return g_mounted && path != nullptr && SD.remove(path);
    }

    bool writeText(const char *path, const String &content, bool append)
    {
        if (!g_mounted || path == nullptr)
        {
            return false;
        }

        File file = SD.open(path, append ? FILE_APPEND : FILE_WRITE);
        if (!file)
        {
            Serial.printf("[SD] Failed to open for write: %s\n", path);
            return false;
        }

        size_t written = file.print(content);
        file.close();
        return written == content.length();
    }

    bool readText(const char *path, String &out)
    {
        out = "";
        if (!g_mounted || path == nullptr)
        {
            return false;
        }

        File file = SD.open(path, FILE_READ);
        if (!file)
        {
            Serial.printf("[SD] Failed to open for read: %s\n", path);
            return false;
        }

        while (file.available())
        {
            out += static_cast<char>(file.read());
        }

        file.close();
        return true;
    }

    void listDir(const char *dirname, uint8_t levels)
    {
        if (!g_mounted)
        {
            Serial.println("[SD] Not mounted");
            return;
        }

        File root = SD.open(dirname);
        if (!root || !root.isDirectory())
        {
            Serial.printf("[SD] Failed to open dir: %s\n", dirname);
            return;
        }

        File file = root.openNextFile();
        while (file)
        {
            if (file.isDirectory())
            {
                Serial.printf("  DIR : %s\n", file.name());
                if (levels > 0)
                {
                    listDir(file.path(), levels - 1);
                }
            }
            else
            {
                Serial.printf("  FILE: %s  SIZE: %llu\n", file.name(), file.size());
            }

            file = root.openNextFile();
        }

        root.close();
    }

    const char *cardTypeName()
    {
        return cardTypeToString(g_cardType);
    }
}
