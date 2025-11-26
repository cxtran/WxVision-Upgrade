#include <Arduino.h>
#include <DHT.h>
#include <IRrecv.h>
#include <pins.h>
#include "sensors.h"
#include "display.h"
#include "alarm.h"
#include "menu.h"
#include "ir_codes.h"
#include <Wire.h>
#include "units.h"
#include "system.h"
#include "buzzer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

// Brightness Sensor
#define BRIGHTNESS_PIN 36    // GPIO 36 (ADC1_CH0)
#define FIXED_RESISTOR 10000 // 10kΩ resistor (Ohms)

// Light sensor gain (percentage)

// SCD40
uint16_t SCD40_co2 = 0;
float SCD40_temp = NAN, SCD40_hum = NAN;

// aht20 and bmp280
float aht20_temp = NAN, aht20_hum = NAN;
float bmp280_temp = NAN, bmp280_pressure = NAN;

// Infrared Receiver
const uint16_t kRecvPin = IR_RECEIVE_PIN; // GPIO 34

IRrecv irrecv(kRecvPin);
decode_results results;

SensirionI2cScd4x scd4x;
Adafruit_AHTX0 aht20;
Adafruit_BMP280 bmp280;
static float s_lastCalibratedLux = NAN;

static float computeCalibratedLux(float rawLux)
{
  float gain = lightGain / 100.0f;
  float calibratedLux = rawLux * gain;

  const float minLux = 1.0f;
  const float maxLux = 700.0f;
  if (calibratedLux < minLux)
    calibratedLux = minLux;
  if (calibratedLux > maxLux)
    calibratedLux = maxLux;

  return calibratedLux;
}

namespace
{
  constexpr uint8_t kVirtualIrQueueSize = 8;
  volatile uint32_t s_virtualIrQueue[kVirtualIrQueueSize] = {};
  volatile uint8_t s_virtualIrHead = 0;
  volatile uint8_t s_virtualIrTail = 0;
  portMUX_TYPE s_virtualIrMux = portMUX_INITIALIZER_UNLOCKED;

  bool popVirtualIR(uint32_t &out)
  {
    bool has = false;
    portENTER_CRITICAL(&s_virtualIrMux);
    if (s_virtualIrHead != s_virtualIrTail)
    {
      out = s_virtualIrQueue[s_virtualIrHead];
      s_virtualIrHead = (s_virtualIrHead + 1) % kVirtualIrQueueSize;
      has = true;
    }
    portEXIT_CRITICAL(&s_virtualIrMux);
    return has;
  }
}

void setupIRSensor()
{
  irrecv.enableIRIn(); // Start the receiver
  Serial.println("VS1838B IR receiver test. Press buttons on your remote...");
}

// Forward declaration for use in readIRSensor
static inline bool isAlarmCancelCode(uint32_t code);

void readIRSensor()
{
  if (irrecv.decode(&results))
  {
    if (isAlarmCurrentlyActive() && isAlarmCancelCode(results.value))
    {
        cancelActiveAlarm();
        irrecv.resume();
        return; // Swallow input while alarm is sounding
    }
    handleIR(results.value);
    irrecv.resume(); // Receive the next value
    PRINT_IR_CODE(results.value);
  }
}

void setupBrightnessSensor()
{
  analogReadResolution(12); // ESP32 default is 12 bits (0-4095)
  Serial.println("GL5528 Brightness Sensor Test");
}

float readBrightnessSensor()
{
  int adcValue = analogRead(BRIGHTNESS_PIN); // 0-4095
  float voltage = adcValue * 3.3 / 4095.0;   // ESP32 ADC ref = 3.3V
  float ldrResistance = (3.3 - voltage) * FIXED_RESISTOR / voltage;
  float ldr_kOhm = ldrResistance / 1000.0;
  float lux = 500 * pow(ldr_kOhm, -1.4);

  Serial.print("ADC: ");
  Serial.print(adcValue);
  Serial.print("  V: ");
  Serial.print(voltage, 2);
  Serial.print("  R_LDR: ");
  Serial.print(ldrResistance, 0);
  Serial.print(" Ω");
  Serial.print("  Lux: ");
  Serial.println(lux, 1);

  return lux;
}

void setDisplayBrightnessFromLux(float lux)
{
  if (isScreenOff()) {
    return;
  }
  float calibratedLux = computeCalibratedLux(lux);
  s_lastCalibratedLux = calibratedLux;

  const float minLux = 1.0f;
  const float maxLux = 700.0f;
  const int minBrightness = 3;
  const int maxBrightness = 255;
  float scale = (log10(calibratedLux) - log10(minLux)) / (log10(maxLux) - log10(minLux));
  float sensitivity = 1.6;
  scale = pow(scale, sensitivity);
  if (scale < 0.0)
    scale = 0.0;
  if (scale > 1.0)
    scale = 1.0;

  int brightness = (int)(minBrightness + scale * (maxBrightness - minBrightness));
  if (brightness < minBrightness)
    brightness = minBrightness;
  if (brightness > maxBrightness)
    brightness = maxBrightness;

  setPanelBrightness(brightness);

  Serial.printf("LogAutoBrightness: Raw Lux=%.1f Calibrated=%.1f (Gain=%d%%, Sens=%.2f) -> Brightness=%d\n",
                lux, calibratedLux, lightGain, sensitivity, brightness);
}

float getCalibratedLux(float rawLux)
{
  float calibrated = computeCalibratedLux(rawLux);
  // Keep track of the most recent calibrated value even when auto-brightness is off
  s_lastCalibratedLux = calibrated;
  return calibrated;
}

float getLastCalibratedLux()
{
  return s_lastCalibratedLux;
}

static uint32_t s_lastIrCode = 0;
static unsigned long s_lastIrTimestamp = 0;
// While alarm is firing, any IR key cancels/snoozes it.
static inline bool isAlarmCancelCode(uint32_t /*code*/) { return true; }

uint32_t getIRCodeNonBlocking()
{
  uint32_t queuedCode = 0;
  if (popVirtualIR(queuedCode))
  {
    PRINT_IR_CODE(queuedCode);
    if (!menuActive && (queuedCode == IR_UP || queuedCode == IR_DOWN || queuedCode == IR_LEFT || queuedCode == IR_RIGHT || queuedCode == IR_OK || queuedCode == IR_CANCEL))
    {
      playBuzzerTone(1200, 80);
    }
    if (isAlarmCurrentlyActive() && isAlarmCancelCode(queuedCode))
    {
        cancelActiveAlarm();
        s_lastIrCode = 0;
        return 0; // Swallow input while alarm is snoozed
    }
    if (handleGlobalIRCode(queuedCode))
      return 0;
    s_lastIrCode = queuedCode;
    s_lastIrTimestamp = millis();
    return queuedCode;
  }

  static decode_results results;
  if (irrecv.decode(&results))
  {
    uint32_t code = results.value;
    if (results.repeat)
    {
      code = s_lastIrCode;
    }
    else
    {
      s_lastIrCode = code;
    }
    s_lastIrTimestamp = millis();
    irrecv.resume();
    if (code != 0)
    {
      PRINT_IR_CODE(code);
      if (!menuActive && (code == IR_UP || code == IR_DOWN || code == IR_LEFT || code == IR_RIGHT || code == IR_OK || code == IR_CANCEL))
      {
          playBuzzerTone(1200, 80);
      }
      if (isAlarmCurrentlyActive() && isAlarmCancelCode(code))
      {
          cancelActiveAlarm();
          s_lastIrCode = 0;
          return 0; // Consume the input; don't perform any action beyond snooze
      }
      if (handleGlobalIRCode(code))
        return 0;
    }
    return code;
  }

  // Clear last code if we've been idle for a while to avoid stale repeats.
  if (s_lastIrCode != 0 && millis() - s_lastIrTimestamp > 500)
  {
    s_lastIrCode = 0;
  }

  return 0;
}

uint32_t getIRCodeDebounced(uint16_t debounceMs)
{
  static uint32_t lastCode = 0;
  static unsigned long lastTime = 0;

  uint32_t code = getIRCodeNonBlocking();
  if (code == 0)
  {
    return 0;
  }

  unsigned long now = millis();
  if (code == lastCode && (now - lastTime) < debounceMs)
  {
    // Ignore rapid repeats of the same key for navigation-style input.
    return 0;
  }

  lastCode = code;
  lastTime = now;
  return code;
}

bool enqueueVirtualIRCode(uint32_t code)
{
  bool ok = false;
  portENTER_CRITICAL(&s_virtualIrMux);
  uint8_t nextTail = (s_virtualIrTail + 1) % kVirtualIrQueueSize;
  if (nextTail != s_virtualIrHead)
  {
    s_virtualIrQueue[s_virtualIrTail] = code;
    s_virtualIrTail = nextTail;
    ok = true;
  }
  portEXIT_CRITICAL(&s_virtualIrMux);
  return ok;
}

void setupSensors()
{
  Wire.begin();

  // --- SCD40 ---
  scd4x.begin(Wire, 0x62);
  scd4x.startPeriodicMeasurement();
  Serial.println("SCD40 initialized");

  // --- AHT20 ---
  if (aht20.begin())
  {
    Serial.println("AHT20 found");
  }
  else
  {
    Serial.println("Could not find AHT20!");
  }

  // --- BMP280 (try both addresses) ---
  if (bmp280.begin(0x76))
  {
    Serial.println("BMP280 found at 0x76");
  }
  else if (bmp280.begin(0x77))
  {
    Serial.println("BMP280 found at 0x77");
  }
  else
  {
    Serial.println("Could not find BMP280!");
  }
}

void readSCD40()
{
  bool isDataReady = false;
  scd4x.getDataReadyStatus(isDataReady);
  if (isDataReady)
  {
    scd4x.readMeasurement(SCD40_co2, SCD40_temp, SCD40_hum);
  }
  else
  {
    Serial.println("SCD40 data not ready");
  }
}

void readAHT20()
{
  sensors_event_t humidity, temp;
  aht20.getEvent(&humidity, &temp);
  aht20_temp = temp.temperature;
  aht20_hum = humidity.relative_humidity;
}

void readBMP280()
{
  bmp280_temp = bmp280.readTemperature();
  bmp280_pressure = bmp280.readPressure() / 100.0F;
}

bool newAirQualityData = false;
bool newAHT20_BMP280Data = false;

