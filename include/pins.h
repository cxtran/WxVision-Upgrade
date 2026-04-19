#pragma once

// WxVision ESP32-S3 pin map

// RGB Matrix Panel Pin Mapping
#define R1_PIN 10
#define G1_PIN 11
#define B1_PIN 12
#define R2_PIN 13
#define G2_PIN 7
#define B2_PIN 15

#define A_PIN 16
#define B_PIN 17
#define C_PIN 18
#define D_PIN 3
#define E_PIN -1

#define LAT_PIN 42
#define OE_PIN 41
#define CLK_PIN 6

#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 1

// 5-way buttons
#define BTN_UP 1
#define BTN_DN 2
#define BTN_LEFT 19
#define BTN_RIGHT 20
#define BTN_SEL 44

// SD card
#define SD_SCK_PIN 5
#define SD_MOSI_PIN 21
#define SD_MISO_PIN 14
#define SD_CS_PIN 40

// Infrared receiver
#define IR_RECEIVE_PIN 48

// I2C
#define I2C_SDA 8
#define I2C_SCL 9

// Brightness sensor
#define BRIGHTNESS_PIN 4

// I2S audio output (MAX98357A)
#define I2S_BCLK_PIN 47
#define I2S_LRC_PIN 38
#define I2S_DOUT_PIN 39

#define MATRIX_BRIGHTNESS_DEFAULT 10
