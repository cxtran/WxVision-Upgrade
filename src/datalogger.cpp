#include "datalogger.h"
#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <math.h>

namespace {
constexpr size_t kRamMaxSamples = 288;     // 24h @ 5-minute sampling; full history stays on flash
constexpr const char *kLogPath = "/sensor_log.bin";
std::vector<SensorSample> s_log;
bool s_spiffsReady = false;

struct LegacySampleV1 {
    uint32_t ts;
    float temp;
    float hum;
    float press;
    float lux;
};

bool ensureSpiffsMounted() {
    if (s_spiffsReady) {
        return true;
    }
    s_spiffsReady = SPIFFS.begin(true);
    if (!s_spiffsReady) {
        Serial.println("[Datalogger] SPIFFS mount failed; log will not persist.");
    }
    return s_spiffsReady;
}

void loadLog() {
    if (!ensureSpiffsMounted()) return;
    if (!SPIFFS.exists(kLogPath)) return;
    File f = SPIFFS.open(kLogPath, "r");
    if (!f) return;
    size_t fileSize = f.size();
    size_t count = 0;
    bool legacyV1 = false;
    if (sizeof(SensorSample) > 0 && fileSize % sizeof(SensorSample) == 0) {
        count = fileSize / sizeof(SensorSample);
    } else if (sizeof(LegacySampleV1) > 0 && fileSize % sizeof(LegacySampleV1) == 0) {
        count = fileSize / sizeof(LegacySampleV1);
        legacyV1 = true;
    } else {
        Serial.printf("[Datalogger] Unrecognized log size (%u bytes); skipping load.\n", static_cast<unsigned>(fileSize));
        f.close();
        return;
    }
    // Only keep the most recent kRamMaxSamples in memory to save RAM
    size_t startIdx = 0;
    if (count > kRamMaxSamples)
        startIdx = count - kRamMaxSamples;
    size_t startOffset = startIdx * (legacyV1 ? sizeof(LegacySampleV1) : sizeof(SensorSample));
    if (startOffset > 0) f.seek(startOffset, SeekSet);

    s_log.clear();
    if (s_log.capacity() < kRamMaxSamples) {
        s_log.reserve(kRamMaxSamples);
    }
    if (!legacyV1) {
        SensorSample s;
        for (size_t i = startIdx; i < count && f.readBytes(reinterpret_cast<char*>(&s), sizeof(SensorSample)) == sizeof(SensorSample); ++i) {
            s_log.push_back(s);
        }
    } else {
        LegacySampleV1 s;
        for (size_t i = startIdx; i < count && f.readBytes(reinterpret_cast<char*>(&s), sizeof(LegacySampleV1)) == sizeof(LegacySampleV1); ++i) {
            SensorSample converted{s.ts, s.temp, s.hum, s.press, s.lux, NAN};
            s_log.push_back(converted);
        }
    }
    f.close();
}

void saveLog() {
    if (!ensureSpiffsMounted()) return;
    File f = SPIFFS.open(kLogPath, "a");
    if (!f) return;
    if (!s_log.empty()) {
        const SensorSample &s = s_log.back();
        f.write(reinterpret_cast<const uint8_t*>(&s), sizeof(SensorSample));
    }
    f.close();
}
} // namespace

void initSensorLog() {
    // Reserve once up front while heap is least fragmented.
    if (s_log.capacity() < kRamMaxSamples) {
        s_log.reserve(kRamMaxSamples);
    }
    loadLog();
}

void appendSensorSample(const SensorSample &s) {
    if (s_log.size() < kRamMaxSamples) {
        // With upfront reserve, this path should not reallocate at runtime.
        s_log.push_back(s);
    } else if (!s_log.empty()) {
        // Avoid vector growth/reallocation by overwriting the oldest entry.
        for (size_t i = 1; i < s_log.size(); ++i) {
            s_log[i - 1] = s_log[i];
        }
        s_log[s_log.size() - 1] = s;
    }
    saveLog();
}

void sensorLogToJsonDownsample(JsonDocument &doc, size_t maxSamples) {
    JsonArray arr = doc.to<JsonArray>();
    if (maxSamples == 0 || s_log.empty()) return;
    size_t stride = (s_log.size() + maxSamples - 1) / maxSamples; // ceil
    if (stride < 1) stride = 1;
    for (size_t i = 0; i < s_log.size(); i += stride) {
        const auto &s = s_log[i];
        JsonObject o = arr.add<JsonObject>();
        o["ts"] = s.ts;
        o["temp"] = s.temp;
        o["hum"] = s.hum;
        o["press"] = s.press;
        o["lux"] = s.lux;
        if (!isnan(s.co2) && s.co2 > 0.0f) {
            o["co2"] = s.co2;
        } else {
            o["co2"] = nullptr;
        }
    }
    // Ensure the newest sample is present even if stride skipped it
    if (arr.size() == 0 || arr[arr.size() - 1]["ts"].as<uint32_t>() != s_log.back().ts) {
        const auto &s = s_log.back();
        JsonObject o = arr.add<JsonObject>();
        o["ts"] = s.ts;
        o["temp"] = s.temp;
        o["hum"] = s.hum;
        o["press"] = s.press;
        o["lux"] = s.lux;
        if (!isnan(s.co2) && s.co2 > 0.0f) {
            o["co2"] = s.co2;
        } else {
            o["co2"] = nullptr;
        }
    }
}

const std::vector<SensorSample>& getSensorLog() {
    return s_log;
}
