#pragma once
#include <Arduino.h>
#include "display.h"
uint16_t scaleColor565(uint16_t color, float intensity);
WeatherSceneKind resolveWeatherSceneKind(const String &condition);
uint16_t weatherSceneAccentColor(WeatherSceneKind kind);
bool weatherSceneIsNight(WeatherSceneKind kind);
uint16_t weatherSceneTempBgColor(WeatherSceneKind kind);
uint16_t weatherSceneAdaptiveTempTextColor(WeatherSceneKind kind, uint16_t accent, bool secondary);
String formatConditionLabel(const String &condition);
