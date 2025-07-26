#pragma once
#include "display.h"
#include "ir_codes.h"

void setupDHTSensor();

void readDHTSensor();

void setupIRSensor();

void readIRSensor();

void setupBrightnessSensor();

//void readBrightnessSensor();

float readBrightnessSensor();

void setDisplayBrightnessFromLux(float lux);

