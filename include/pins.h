#ifndef PINS_H
#define PINS_H

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
#define E_PIN -1  // -1 for panels that don't use E

#define LAT_PIN 26
#define OE_PIN  15
#define CLK_PIN 2

// Panel dimensions and chaining
#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 1

#endif
