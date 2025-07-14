#pragma once
#include <Arduino.h>

extern int units;
extern int dayFormat;
extern int forecastSrc;
extern int autoRotate;

extern int theme;
extern int brightness;
extern int scrollSpeed;

extern int tempOffset;
extern int humOffset;
extern int lightGain;

// Functions
void loadSettings();
void saveDeviceSettings();
void saveDisplaySettings();
void saveCalibrationSettings();
void saveSystemSettings();
void saveAllSettings();
void saveWeatherSettings();

void toggleUnits(int dir);
void toggleDayFormat(int dir);
void toggleForecastSrc(int dir);
void toggleAutoRotate(int dir);

void toggleTheme(int dir);
void adjustBrightness(int dir);
void adjustScrollSpeed(int dir);


void adjustTempOffset(int dir);
void adjustHumOffset(int dir);
void adjustLightGain(int dir);
