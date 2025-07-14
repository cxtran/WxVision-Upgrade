#pragma once

// RGB Matrix Panel Pin Mapping (P5)
#define R1_PIN 19
#define G1_PIN 13
#define B1_PIN 18
#define R2_PIN 5
#define G2_PIN 12
#define B2_PIN 17
#define A_PIN 16
#define B_PIN 14
#define C_PIN 4
#define D_PIN 27
#define E_PIN -1
#define LAT_PIN 26
#define OE_PIN 15
#define CLK_PIN 2

#define A_PIN 16
#define B_PIN 14
#define C_PIN 4
#define D_PIN 27
#define E_PIN -1  // -1 for panels that don't use E

#define LAT_PIN 26
#define OE_PIN  15
#define CLK_PIN 2

// Panel dimensions and chaining
#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 1

// Buttons
#define BTN_UP 33
#define BTN_DN 32
#define BTN_SEL 25

// Infrared Receiver
#define IR_RECEIVE_PIN 34 // GPIO 34    

// DHT Sensor
#define DHTPIN 23
#define DHTTYPE DHT11

// Brghtness Sensor
#define BRIGHTNESS_PIN 36  // GPIO 36 (ADC1_CH0)

// Buzzer
#define BUZZER_PIN 35   // Update if you use another GPIO!