#include <Arduino.h>
#include <DHT.h>
#include <IRrecv.h>
#include <pins.h>
#include "sensors.h"
#include "menu.h"
#include <Wire.h>
#include "InfoScreen.h"

// Brightness Sensor
#define BRIGHTNESS_PIN 36    // GPIO 36 (ADC1_CH0)
#define FIXED_RESISTOR 10000 // 10kΩ resistor (Ohms)

extern InfoScreen airQualityScreen;
extern InfoScreen tempHumBaroScreen;

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

/*

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
*/

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

void setDisplayBrightnessFromLux(float lux)
{
  float gain = lightGain / 100.0; // Convert to float gain
  float calibratedLux = lux * gain;

  const float minLux = 1.0;
  const float maxLux = 700.0; // Lower maxLux for higher sensitivity

  if (calibratedLux < minLux)
    calibratedLux = minLux;
  if (calibratedLux > maxLux)
    calibratedLux = maxLux;

  const int minBrightness = 3;
  const int maxBrightness = 255;

  float scale = (log10(calibratedLux) - log10(minLux)) / (log10(maxLux) - log10(minLux));
  // Increase sensitivity by making the curve steeper:
  float sensitivity = 1.6; // Try 1.3-2.0 for your environment
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

  dma_display->setBrightness8(brightness);

  Serial.printf("LogAutoBrightness: Raw Lux=%.1f Calibrated=%.1f (Gain=%d%%, Sens=%.2f) -> Brightness=%d\n",
                lux, calibratedLux, lightGain, sensitivity, brightness);
}

// --- IR receiver helper ---
uint32_t getIRCodeNonBlocking()
{
  static decode_results results;
  if (irrecv.decode(&results))
  {
    uint32_t code = results.value;
    irrecv.resume();
    return code;
  }
  return 0;
}

void setupSCD40()
{
  Wire.begin(); // Uses GPIO21 (SDA), GPIO22 (SCL) by default
  scd4x.begin(Wire, 0x62);
  scd4x.startPeriodicMeasurement();
  Serial.println("SCD40 initialized");
}

void setupAHT20()
{
  if (aht20.begin())
  {
    Serial.println("AHT20 found");
  }
  else
  {
    Serial.println("Could not find AHT20!");
  }
}

void setupBMP280()
{
  if (bmp280.begin(0x77))
  { // Change to 0x77 if your BMP280 uses that address
    Serial.println("BMP280 found");
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
    //           Serial.printf("CO2: %u ppm, T: %.2f C, RH: %.2f %%\n", co2, temperature, humidity);
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

void showAirQualityScreen()
{
  String lines[3];
  lines[0] = "Temp: " + String(SCD40_temp, 2) + "C";
  lines[1] = "Hum:  " + String(SCD40_hum, 2) + "%";
  lines[2] = "CO2: " + String(SCD40_co2) + "ppm";
  airQualityScreen.setLines(lines, 3);
  airQualityScreen.show([](){ currentScreen = ScreenMode::SCREEN_OWM; });
}
void showTempHumBaroScreen(){
  String lines[3];
  lines[0] = "Temp: " + String(aht20_temp, 2) + "C";
  lines[1] = "Hum:  " + String(aht20_hum, 2) + "%";
  lines[2] = "Baro: " + String(bmp280_pressure) + "hPa";
  tempHumBaroScreen.setLines(lines, 3);
  tempHumBaroScreen.show([](){ currentScreen = ScreenMode::SCREEN_OWM; });

}
