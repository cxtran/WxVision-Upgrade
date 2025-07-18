#pragma once
#include <Arduino.h>

#include <Preferences.h>



extern int units;            // 0 = F+mph, 1 = C+m/s
extern int dayFormat ;        // 0 = MM/DD/YYYY, 1 = DD/MM/YYYY
extern int forecastSrc ;      // 0 = OpenWeather, 1 = WeatherFlow
extern int autoRotate ;       // 1=on
extern int manualScreen;     // 0=Main,1=Weather,2=Forecast,3=Calib

extern String wifiSSID ;
extern String wifiPass ;

// --- Display ---
extern int theme ;            // 0 = Color, 1 = Monochrome
extern int brightness ;      // 1 - 100
extern int scrollSpeed ;      // 1-5
extern String customMsg ;

// --- Weather ---
extern String owmCity ;
extern String owmApiKey ;
extern int owmCountryIndex;
extern String owmCountryCustom;

extern String wfToken ;
extern String wfStationId ;

// --- Calibration ---
extern int tempOffset ;   // degrees
extern int tempOffset ;   // degrees
extern int lightGain ;  // %
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
