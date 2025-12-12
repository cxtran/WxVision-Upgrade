#pragma once
#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>

struct SensorSample {
    uint32_t ts;   // epoch seconds
    float temp;    // deg C
    float hum;     // %
    float press;   // hPa
    float lux;     // raw lux
    float co2;     // ppm
};

// Initialize and load existing log from flash
void initSensorLog();
// Append a new sample; full history is persisted to flash, RAM keeps a bounded tail
void appendSensorSample(const SensorSample &s);
// Serialize log to JSON array (oldest->newest)
void sensorLogToJson(JsonDocument &doc);
// Serialize with simple downsampling to cap payload size
void sensorLogToJsonDownsample(JsonDocument &doc, size_t maxSamples);
// Access the current in-memory log (oldest->newest)
const std::vector<SensorSample>& getSensorLog();
