#include <Arduino.h>
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
#include "ir_learn.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

// Brightness Sensor
#define BRIGHTNESS_PIN 36    // GPIO 36 (ADC1_CH0)
#define FIXED_RESISTOR 10000 // 10kΩ resistor (Ohms)

// Light sensor gain (percentage)

// SCD40
uint16_t SCD40_co2 = 0;
float SCD40_temp = NAN, SCD40_hum = NAN;
bool scd40Ready = false;

// aht20 and bmp280
float aht20_temp = NAN, aht20_hum = NAN;
float bmp280_temp = NAN, bmp280_pressure = NAN;
bool aht20Ready = false;
bool bmp280Ready = false;

// Infrared Receiver
const uint16_t kRecvPin = IR_RECEIVE_PIN; // GPIO 34

IRrecv irrecv(kRecvPin);
decode_results results;

SensirionI2cScd4x scd4x;
Adafruit_AHTX0 aht20;
Adafruit_BMP280 bmp280;
static float s_lastCalibratedLux = NAN;
static float s_lastRawLux = NAN;

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
  constexpr uint8_t kVirtualIrQueueSize = 24;
  volatile IRCodes::WxKey s_virtualIrQueue[kVirtualIrQueueSize] = {};
  volatile uint8_t s_virtualIrHead = 0;
  volatile uint8_t s_virtualIrTail = 0;
  portMUX_TYPE s_virtualIrMux = portMUX_INITIALIZER_UNLOCKED;

  bool popVirtualIR(IRCodes::WxKey &out)
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
  Serial.println(F("VS1838B IR receiver test. Press buttons on your remote..."));
}

// Forward declaration for use in readIRSensor
static inline bool isAlarmCancelCode(IRCodes::WxKey key);
static inline bool isNavKey(IRCodes::WxKey key);

void readIRSensor()
{
  if (irrecv.decode(&results))
  {
    if (wxv::irlearn::isActive())
    {
      wxv::irlearn::onDecodedFrame(results);
      irrecv.resume();
      return;
    }
    IRCodes::WxKey key = IRCodes::WxKey::Unknown;
    if (!IRCodes::matchAnyProfile(results, key))
    {
      irrecv.resume();
      return;
    }
    if (isAlarmCurrentlyActive() && isAlarmCancelCode(key))
    {
        cancelActiveAlarm();
        irrecv.resume();
        return; // Swallow input while alarm is sounding
    }
    handleIRKey(key);
    irrecv.resume(); // Receive the next value
  }
}

void setupBrightnessSensor()
{
  analogReadResolution(12); // ESP32 default is 12 bits (0-4095)
  Serial.println(F("GL5528 Brightness Sensor Test"));
}

float readBrightnessSensor()
{
  int adcValue = analogRead(BRIGHTNESS_PIN); // 0-4095
  float voltage = adcValue * 3.3 / 4095.0;   // ESP32 ADC ref = 3.3V
  float ldrResistance = (3.3 - voltage) * FIXED_RESISTOR / voltage;
  float ldr_kOhm = ldrResistance / 1000.0;
  float lux = 500 * pow(ldr_kOhm, -1.4);
  s_lastRawLux = lux;


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

float getLastRawLux()
{
  return s_lastRawLux;
}

static IRCodes::WxKey s_lastIrKey = IRCodes::WxKey::Unknown;
static unsigned long s_lastIrTimestamp = 0;
static bool s_lastIrWasVirtual = false;
static IRCodes::WxKey s_lastPhysicalDeliveredKey = IRCodes::WxKey::Unknown;
static unsigned long s_lastPhysicalDeliveredAt = 0;
static const unsigned long IR_PHYSICAL_REPEAT_BLOCK_MS = 180UL;
// While alarm is firing, any IR key cancels/snoozes it.
static inline bool isAlarmCancelCode(IRCodes::WxKey /*key*/) { return true; }

static inline bool isNavKey(IRCodes::WxKey key)
{
  return key == IRCodes::WxKey::Up || key == IRCodes::WxKey::Down ||
         key == IRCodes::WxKey::Left || key == IRCodes::WxKey::Right ||
         key == IRCodes::WxKey::Ok || key == IRCodes::WxKey::Cancel;
}

bool startUniversalRemoteLearning()
{
  return wxv::irlearn::start();
}

bool clearUniversalRemoteLearning()
{
  return wxv::irlearn::clearLearnedRemote();
}

IRCodes::WxKey getIRCodeNonBlocking()
{
  IRCodes::WxKey queuedKey = IRCodes::WxKey::Unknown;
  if (popVirtualIR(queuedKey))
  {
    if (wxv::irlearn::isActive())
    {
      if (queuedKey == IRCodes::WxKey::Cancel || queuedKey == IRCodes::WxKey::Menu)
      {
        wxv::irlearn::cancel();
      }
      return IRCodes::WxKey::Unknown;
    }
    Serial.printf("Virtual IR Key: %s\n", IRCodes::keyName(queuedKey));
    if (!menuActive && isNavKey(queuedKey))
    {
      playBuzzerTone(1200, 80);
    }
    if (isAlarmCurrentlyActive() && isAlarmCancelCode(queuedKey))
    {
        cancelActiveAlarm();
        s_lastIrKey = IRCodes::WxKey::Unknown;
        return IRCodes::WxKey::Unknown; // Swallow input while alarm is snoozed
    }
    if (handleGlobalIRKey(queuedKey))
      return IRCodes::WxKey::Unknown;
    s_lastIrKey = queuedKey;
    s_lastIrTimestamp = millis();
    s_lastIrWasVirtual = true;
    return queuedKey;
  }

  static decode_results results;
  if (irrecv.decode(&results))
  {
    if (isAlarmCurrentlyActive())
    {
      cancelActiveAlarm();
      irrecv.resume();
      return IRCodes::WxKey::Unknown;
    }

    if (wxv::irlearn::isActive())
    {
      wxv::irlearn::onDecodedFrame(results);
      irrecv.resume();
      return IRCodes::WxKey::Unknown;
    }

    IRCodes::WxKey key = IRCodes::WxKey::Unknown;
    const bool isRepeatFrame = results.repeat;
    if (isRepeatFrame)
    {
      key = s_lastIrKey;
    }
    else
    {
      if (IRCodes::matchAnyProfile(results, key))
      {
        s_lastIrKey = key;
      }
    }
    s_lastIrTimestamp = millis();
    s_lastIrWasVirtual = false;
    irrecv.resume();

    if (key != IRCodes::WxKey::Unknown)
    {
      unsigned long now = millis();
      bool blockRepeat = false;

      // Global physical-IR repeat filter:
      // suppress short-interval duplicate frames (including protocol repeat frames).
      if (key == s_lastPhysicalDeliveredKey &&
          (now - s_lastPhysicalDeliveredAt) < IR_PHYSICAL_REPEAT_BLOCK_MS)
      {
        blockRepeat = true;
      }
      else if (isRepeatFrame &&
               (now - s_lastPhysicalDeliveredAt) < IR_PHYSICAL_REPEAT_BLOCK_MS)
      {
        blockRepeat = true;
      }

      if (blockRepeat)
      {
        return IRCodes::WxKey::Unknown;
      }

      s_lastPhysicalDeliveredKey = key;
      s_lastPhysicalDeliveredAt = now;
      if (!menuActive && isNavKey(key))
      {
          playBuzzerTone(1200, 80);
      }
      if (isAlarmCurrentlyActive() && isAlarmCancelCode(key))
      {
          cancelActiveAlarm();
          s_lastIrKey = IRCodes::WxKey::Unknown;
          return IRCodes::WxKey::Unknown; // Consume the input; don't perform any action beyond snooze
      }
      if (handleGlobalIRKey(key))
        return IRCodes::WxKey::Unknown;
    }
    return key;
  }

  // Clear last code if we've been idle for a while to avoid stale repeats.
  if (s_lastIrKey != IRCodes::WxKey::Unknown && millis() - s_lastIrTimestamp > 500)
  {
    s_lastIrKey = IRCodes::WxKey::Unknown;
    s_lastPhysicalDeliveredKey = IRCodes::WxKey::Unknown;
  }

  return IRCodes::WxKey::Unknown;
}

IRCodes::WxKey getIRCodeDebounced(uint16_t debounceMs)
{
  static IRCodes::WxKey lastKey = IRCodes::WxKey::Unknown;
  static unsigned long lastTime = 0;

  IRCodes::WxKey key = getIRCodeNonBlocking();
  if (key == IRCodes::WxKey::Unknown)
  {
    return IRCodes::WxKey::Unknown;
  }

  // Virtual IR is injected by WebUI/buttons and should be handled immediately.
  if (s_lastIrWasVirtual)
  {
    return key;
  }

  unsigned long now = millis();
  if (key == lastKey && (now - lastTime) < debounceMs)
  {
    // Ignore rapid repeats of the same key for navigation-style input.
    return IRCodes::WxKey::Unknown;
  }

  lastKey = key;
  lastTime = now;
  return key;
}

bool enqueueVirtualIRKey(IRCodes::WxKey key)
{
  bool ok = false;
  portENTER_CRITICAL(&s_virtualIrMux);
  uint8_t nextTail = (s_virtualIrTail + 1) % kVirtualIrQueueSize;
  if (nextTail != s_virtualIrHead)
  {
    s_virtualIrQueue[s_virtualIrTail] = key;
    s_virtualIrTail = nextTail;
    ok = true;
  }
  portEXIT_CRITICAL(&s_virtualIrMux);
  return ok;
}

IRCodes::WxKey mapLegacyCodeToKey(uint32_t legacy)
{
  return IRCodes::mapLegacyCodeToKey(legacy);
}

bool enqueueVirtualIRCode(uint32_t code)
{
  IRCodes::WxKey key = mapLegacyCodeToKey(code);
  if (key == IRCodes::WxKey::Unknown)
  {
    return false;
  }
  return enqueueVirtualIRKey(key);
}

void setupSensors()
{
  Wire.begin();
  IRCodes::loadLearnedProfile();

  // --- SCD40 ---
  scd4x.begin(Wire, 0x62);
  scd4x.startPeriodicMeasurement();
  scd40Ready = true;
  Serial.println(F("SCD40 initialized"));

  // --- AHT20 ---
  if (aht20.begin())
  {
    aht20Ready = true;
    Serial.println(F("AHT20 found"));
  }
  else
  {
    aht20Ready = false;
    Serial.println(F("Could not find AHT20!"));
  }

  // --- BMP280 (try both addresses) ---
  if (bmp280.begin(0x76))
  {
    bmp280Ready = true;
    Serial.println(F("BMP280 found at 0x76"));
  }
  else if (bmp280.begin(0x77))
  {
    bmp280Ready = true;
    Serial.println(F("BMP280 found at 0x77"));
  }
  else
  {
    bmp280Ready = false;
    Serial.println(F("Could not find BMP280!"));
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
    Serial.println(F("SCD40 data not ready"));
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


