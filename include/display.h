#pragma once
#include <Arduino_JSON.h>
#ifdef typeof
#undef typeof
#endif
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "Font5x7Uts.h"
#include "pins.h"
#include "utils.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include "time.h"

// === Panel Config ===
#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 1

extern MatrixPanel_I2S_DMA *dma_display;
extern uint8_t currentPanelBrightness;
void setPanelBrightness(uint8_t value);
extern bool rtcReady;

enum ScreenMode {
    SCREEN_OWM = 0,
    SCREEN_UDP_FORECAST,
    SCREEN_UDP_DATA,
    SCREEN_WIND_DIR, 
    SCREEN_ENV_INDEX,
    SCREEN_CONDITION_SCENE,
    SCREEN_CURRENT,
    SCREEN_HOURLY,
    SCREEN_CLOCK,
    SCREEN_LUNAR_VI,
    SCREEN_COUNT
};

enum class WeatherSceneKind {
    Sunny,
    Cloudy,
    Rain,
    Thunderstorm,
    Snow,
    ClearNight,
    Unknown
};

extern ScreenMode currentScreen;
//extern const int SCREEN_COUNT;
extern const int NUM_INFOSCREENS;
extern const ScreenMode InfoScreenModes[];

extern uint16_t myRED, myGREEN, myBLUE, myWHITE, myBLACK, myYELLOW, myCYAN;
extern bool useImperial;

void setupDisplay();
int getTextWidth(const char* text);
const uint8_t* getWeatherIconFromCode(String code);
const uint8_t* getWFIconFromCondition(String condition) ;
const uint16_t getIconColorFromCondition(String condition);
const uint16_t  getDayNightColorFromCode( String code);

void syncTimeFromNTP1();
void getTimeFromRTC();
extern byte t_minute;
extern byte t_second;
void scrollWeatherDetails();



void drawOWMScreen();
void drawClockScreen(); 
void drawWeatherScreen();
void drawUdpDataScreen();
void drawSettingsScreen();
void displayWeatherData();
void fetchWeatherFromOWM();
void createScrollingText();
void drawLunarScreenVi();
// display.h
void requestScrollRebuild();     // mark that the marquee text must be rebuilt
void notifyUnitsMaybeChanged();  // compare unit signature; request rebuild if changed
void serviceScrollRebuild();     // if a rebuild is pending, rebuild now (cheap no-op otherwise)
void applyUnitPreferences();     // sync derived display flags from UnitPrefs

bool screenIsAllowed(ScreenMode mode);
ScreenMode nextAllowedScreen(ScreenMode start, int direction);
ScreenMode enforceAllowedScreen(ScreenMode desired);
ScreenMode homeScreenForDataSource();

void drawConditionSceneScreen();
void drawWeatherConditionScene(WeatherSceneKind kind);
void drawWeatherConditionScene(const String &condition);
void tickConditionSceneMarquee();
void tickLunarMarquee();

// Splash screen helpers
void splashBegin(uint16_t minimumMs);
void splashUpdate(const char* status, uint8_t step, uint8_t total);
void splashEnd();
bool isSplashActive();
