#include "datalogger.h"
#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <math.h>

namespace {
constexpr size_t kMaxSamples = 288;        // 24h at 5-minute intervals
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

void trimToWindow() {
    while (s_log.size() > kMaxSamples) {
        s_log.erase(s_log.begin());
    }
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
    s_log.clear();
    s_log.reserve(kMaxSamples);
    if (!legacyV1) {
        SensorSample s;
        for (size_t i = 0; i < count && f.readBytes(reinterpret_cast<char*>(&s), sizeof(SensorSample)) == sizeof(SensorSample); ++i) {
            s_log.push_back(s);
        }
    } else {
        LegacySampleV1 s;
        for (size_t i = 0; i < count && f.readBytes(reinterpret_cast<char*>(&s), sizeof(LegacySampleV1)) == sizeof(LegacySampleV1); ++i) {
            SensorSample converted{s.ts, s.temp, s.hum, s.press, s.lux, NAN};
            s_log.push_back(converted);
        }
    }
    f.close();
    trimToWindow();
}

void saveLog() {
    if (!ensureSpiffsMounted()) return;
    File f = SPIFFS.open(kLogPath, "w");
    if (!f) return;
    for (const auto &s : s_log) {
        f.write(reinterpret_cast<const uint8_t*>(&s), sizeof(SensorSample));
    }
    f.close();
}
} // namespace

void initSensorLog() {
    loadLog();
}

void appendSensorSample(const SensorSample &s) {
    s_log.push_back(s);
    trimToWindow();
    saveLog();
}

void sensorLogToJson(JsonDocument &doc) {
    JsonArray arr = doc.to<JsonArray>();
    for (const auto &s : s_log) {
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
