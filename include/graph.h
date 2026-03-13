#pragma once
#include "display.h"
void updateGraphData();
void drawTemperatureHistoryScreen();
void drawHumidityHistoryScreen();
void drawCo2HistoryScreen();
void drawBaroHistoryScreen();
void drawPredictionScreen();
void resetPredictionRenderState();
void tickTemperatureHistoryMarquee();
void tickHumidityHistoryMarquee();
void tickCo2HistoryMarquee();
void tickBaroHistoryMarquee();
void tickPredictionScreen();
void handlePredictionDownPress();
void handlePredictionUpPress();
void handlePredictionSelectPress();
bool is24HourSectionScreen(ScreenMode mode);
void set24HourSectionPageForScreen(ScreenMode mode);
void draw24HourSectionScreen();
void tick24HourSection();
void handle24HourSectionDownPress();
void handle24HourSectionUpPress();
void handle24HourSectionSelectPress();
