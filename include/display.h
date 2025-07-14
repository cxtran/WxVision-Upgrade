#pragma once

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "Font5x7Uts.h"
#include "pins.h"

// === Panel Config ===
#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 1

extern MatrixPanel_I2S_DMA *dma_display;

extern uint16_t myRED, myGREEN, myBLUE, myWHITE, myBLACK, myYELLOW, myCYAN;

void setupDisplay();
int getTextWidth(const char* text);
const uint8_t* getWeatherIconFromCode(String code);
const uint8_t* getWFIconFromCondition(String condition) ;
const uint16_t getIconColorFromCondition(String condition);
const uint16_t  getDayNightColorFromCode( String code);

