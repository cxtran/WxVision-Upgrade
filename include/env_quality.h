#pragma once

enum class EnvBand : int {
    Unknown = -1,
    Critical = 0,
    Poor = 1,
    Moderate = 2,
    Good = 3
};

void showEnvironmentalQualityScreen();
