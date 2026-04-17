#pragma once

// ======================================================
// WxVision ESP32-S3 Pin Map
// Joystick removed
// DHT removed
// 5-button switch layout added
// I2C sensors used instead
// ======================================================


// ------------------------------
// RGB Matrix Panel Pin Mapping
// ------------------------------
#define R1_PIN 10
#define G1_PIN 11
#define B1_PIN 12
#define R2_PIN 13
#define G2_PIN 7// 14
#define B2_PIN 15

#define A_PIN 16
#define B_PIN 17
#define C_PIN 18
#define D_PIN 3
#define E_PIN -1   // -1 for 64x32 panel

#define LAT_PIN 42
#define OE_PIN 41
#define CLK_PIN 6

// Panel dimensions and chaining
#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 1


// ------------------------------
// 5-Way Switch Buttons
// ------------------------------
#define BTN_UP 1
#define BTN_DN 2
#define BTN_LEFT 19
#define BTN_RIGHT 20
#define BTN_SEL 44





#define SD_SCK_PIN   5
#define SD_MOSI_PIN  21
#define SD_MISO_PIN  14// 7
#define SD_CS_PIN    40//0

// ------------------------------
// Infrared Receiver
// ------------------------------
#define IR_RECEIVE_PIN 48 //40


// ------------------------------
// I2C Bus

// ------------------------------
#define I2C_SDA 8
#define I2C_SCL 9


// ------------------------------
// Brightness Sensor
// ------------------------------
#define BRIGHTNESS_PIN 4


// ------------------------------
// Buzzer
// ------------------------------
// #define BUZZER_PIN 5


// ------------------------------
// I2S Audio - MAX98357A
// ------------------------------
#define I2S_BCLK_PIN   47
#define I2S_LRC_PIN    38
#define I2S_DOUT_PIN   39




// ------------------------------
// Optional / informational
// ------------------------------
#define MATRIX_BRIGHTNESS_DEFAULT 10


// ------------------------------
// Notes
// ------------------------------
// Removed:
//   - Joystick X/Y/SW
//   - DHTPIN / DHTTYPE
//
// Reserved / avoid on ESP32-S3 DevKitC-1:
//   GPIO19 = USB D-
//   GPIO20 = USB D+
//   GPIO45 = strap / avoid
//   GPIO46 = not exposed / avoid