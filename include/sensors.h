#pragma once
#include "display.h"
#include "ir_codes.h"
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <SensirionI2CScd4x.h>
#include <Adafruit_BMP280.h>

extern uint16_t SCD40_co2;
extern float SCD40_temp;
extern float SCD40_hum;
extern float bmp280_temp;
extern float bmp280_pressure;
extern bool scd40Ready;
extern bool bmp280Ready;
extern unsigned long scd40InitMs;
extern unsigned long scd40LastSuccessMs;
extern int16_t scd40LastError;
extern uint32_t scd40ReadFailures;

extern bool newAirQualityData;
extern bool newAHT20_BMP280Data;
extern IRrecv irrecv;
extern decode_results results;
extern SensirionI2cScd4x scd4x;
extern Adafruit_BMP280 bmp280;

void setupIRSensor();

void readIRSensor();

void setupBrightnessSensor();

//void readBrightnessSensor();

float readBrightnessSensor();
float getCalibratedLux(float rawLux);
float getLastCalibratedLux();
float getLastRawLux();

void setDisplayBrightnessFromLux(float lux);

IRCodes::WxKey getIRCodeNonBlocking();
// Debounced IR read: suppresses rapid repeats of the same code.
// Used for general navigation so that only value adjustments (which call
// getIRCodeNonBlocking directly) remain highly responsive.
IRCodes::WxKey getIRCodeDebounced(uint16_t debounceMs = 200);
bool enqueueVirtualIRKey(IRCodes::WxKey key);
bool enqueueVirtualIRCode(uint32_t code);
IRCodes::WxKey mapLegacyCodeToKey(uint32_t legacy);
bool startUniversalRemoteLearning();
bool clearUniversalRemoteLearning();


void setupSensors();
// void setupSCD40();
void readSCD40();
// void setupBMP280();
void readBMP280();

bool scd40DataIsFresh(unsigned long staleAfterMs = 90000UL);
bool scd40IsWarmingUp(unsigned long warmupMs = 90000UL);

  
