#include <Arduino.h>
#include <DHT.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>
#include <pins.h>
#include "sensors.h"
#include "menu.h"

// Brightness Sensor
#define BRIGHTNESS_PIN 36    // GPIO 36 (ADC1_CH0)
#define FIXED_RESISTOR 10000 // 10kΩ resistor (Ohms)





// Infrared Receiver
const uint16_t kRecvPin = IR_RECEIVE_PIN; // GPIO 34

IRrecv irrecv(kRecvPin);
decode_results results;

void setupIRSensor()
{
  irrecv.enableIRIn(); // Start the receiver
  Serial.println("VS1838B IR receiver test. Press buttons on your remote...");
}

void readIRSensor()
{
  if (irrecv.decode(&results))
  {

    handleIR(results.value);
    irrecv.resume(); // Receive the next value
  }
}

// DHT Sensor
DHT dht(DHTPIN, DHTTYPE);

void setupDHTSensor()
{
  dht.begin();
}

void readDHTSensor()
{
  float humidity = dht.readHumidity();
  float tempC = dht.readTemperature();     // Celsius
  float tempF = dht.readTemperature(true); // Fahrenheit

  if (isnan(humidity) || isnan(tempC) || isnan(tempF))
  {
    Serial.println("Failed to read from DHT11 sensor!");
  }
  else
  {
    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.print(" %\t");
    Serial.print("Temperature: ");
    Serial.print(tempC);
    Serial.print(" °C / ");
    Serial.print(tempF);
    Serial.println(" °F");
  }
  // delay(2000); // Wait 2 seconds between readings (DHT11 is slow)
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

  // Calculate LDR resistance using voltage divider formula
  float ldrResistance = (3.3 - voltage) * FIXED_RESISTOR / voltage;

  // Estimate lux from resistance (GL5528 typical curve)
  // lux = 500 * (R_LDR in kΩ) ^ -1.4
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


void setDisplayBrightnessFromLux(float lux) {
    float gain = lightGain / 100.0; // Convert to float gain
    float calibratedLux = lux * gain;

    const float minLux = 1.0;
    const float maxLux = 700.0;  // Lower maxLux for higher sensitivity

    if (calibratedLux < minLux) calibratedLux = minLux;
    if (calibratedLux > maxLux) calibratedLux = maxLux;

    const int minBrightness = 3;
    const int maxBrightness = 255;

    float scale = (log10(calibratedLux) - log10(minLux)) / (log10(maxLux) - log10(minLux));
    // Increase sensitivity by making the curve steeper:
    float sensitivity = 1.6; // Try 1.3-2.0 for your environment
    scale = pow(scale, sensitivity);

    if (scale < 0.0) scale = 0.0;
    if (scale > 1.0) scale = 1.0;

    int brightness = (int)(minBrightness + scale * (maxBrightness - minBrightness));

    if (brightness < minBrightness) brightness = minBrightness;
    if (brightness > maxBrightness) brightness = maxBrightness;

    dma_display->setBrightness8(brightness);

    Serial.printf("LogAutoBrightness: Raw Lux=%.1f Calibrated=%.1f (Gain=%d%%, Sens=%.2f) -> Brightness=%d\n",
                  lux, calibratedLux, lightGain, sensitivity, brightness);
}


// --- IR receiver helper ---
uint32_t getIRCodeNonBlocking() {
    static decode_results results;
    if (irrecv.decode(&results)) {
        uint32_t code = results.value;
        irrecv.resume();
        return code;
    }
    return 0;
}
