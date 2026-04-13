#pragma once

#include <Arduino.h>

enum class ForecastSummaryImportance : uint8_t
{
    Low = 0,
    Medium,
    High
};

struct ForecastSummaryMessage
{
    bool available = false;
    char title[16] = "FORECAST";
    char line1[24] = "";
    char line2[24] = "";
    ForecastSummaryImportance importance = ForecastSummaryImportance::Low;
    uint32_t validUntilEpoch = 0;
    uint16_t displayDurationMs = 4500;
    bool useTypewriter = false;
    char debugReason[96] = "";
    uint32_t signature = 0;
};

void forecastSummaryTick();
bool forecastSummaryHasMessage();
bool forecastSummaryScreenAllowed();
const ForecastSummaryMessage &currentForecastSummaryMessage();
void beginForecastSummaryDisplay();
void finishForecastSummaryDisplay();
bool forecastSummaryDisplayExpired();
bool forecastSummaryScreenActive();
bool forecastSummaryShouldAutoPresent();
void acknowledgeForecastSummaryAutoPresent();
void resetForecastSummaryState();
