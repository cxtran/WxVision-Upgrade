#include "datalogger.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

namespace {
constexpr size_t kMaxSamples = 288;        // 24h at 5-minute intervals
constexpr const char *kLogPath = "/sensor_log.bin";
std::vector<SensorSample> s_log;

void trimToWindow() {
    while (s_log.size() > kMaxSamples) {
        s_log.erase(s_log.begin());
    }
}

void loadLog() {
    if (!SPIFFS.exists(kLogPath)) return;
    File f = SPIFFS.open(kLogPath, "r");
    if (!f) return;
    size_t count = f.size() / sizeof(SensorSample);
    s_log.clear();
    s_log.reserve(kMaxSamples);
    SensorSample s;
    for (size_t i = 0; i < count && f.readBytes(reinterpret_cast<char*>(&s), sizeof(SensorSample)) == sizeof(SensorSample); ++i) {
        s_log.push_back(s);
    }
    f.close();
    trimToWindow();
}

void saveLog() {
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
    }
}

const std::vector<SensorSample>& getSensorLog() {
    return s_log;
}
