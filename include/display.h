#pragma once
#include <Arduino_JSON.h>
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

enum ScreenMode {
    SCREEN_OWM = 0,
    SCREEN_UDP_FORECAST,
    SCREEN_UDP_DATA,
    // Add more screens as needed
    SCREEN_COUNT
};

extern ScreenMode currentScreen;
//extern const int SCREEN_COUNT;
extern const int NUM_INFOSCREENS;
extern const ScreenMode InfoScreenModes[];

extern uint16_t myRED, myGREEN, myBLUE, myWHITE, myBLACK, myYELLOW, myCYAN;

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

