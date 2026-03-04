#pragma once

#include <stdint.h>
#include <vector>
#include "datalogger.h"

namespace wxv::ml
{
struct OutlookPrediction
{
    bool available = false;
    const char *label = "N/A";
    uint8_t confidencePct = 0;
};

// Predict weather outlook from recent sensor log history.
// Returns available=false until a trained model is generated and enabled.
OutlookPrediction predictOutlookFromLog(const std::vector<SensorSample> &log);
} // namespace wxv::ml
