#pragma once
#include "display.h"
#include "ir_codes.h"
#include <SensirionI2CScd4x.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>

extern bool newAirQualityData;
extern bool newAHT20_BMP280Data;

void setupDHTSensor();

void readDHTSensor();

void setupIRSensor();

void readIRSensor();

void setupBrightnessSensor();

//void readBrightnessSensor();

float readBrightnessSensor();

void setDisplayBrightnessFromLux(float lux);

uint32_t getIRCodeNonBlocking();

void setupSCD40(); 
void readSCD40();
void showAirQualityScreen();
void showTempHumBaroScreen();
void setupAHT20();
void setupBMP280();
void readAHT20();
void readBMP280();

  