#ifndef DISPLAY_H
#define DISPLAY_H

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "Font5x7Uts.h"
#include "pins.h"

extern MatrixPanel_I2S_DMA *dma_display;

extern uint16_t myRED, myGREEN, myBLUE, myWHITE, myBLACK;

void setupDisplay();

#endif
