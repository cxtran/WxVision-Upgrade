// windmeter_font.h
#pragma once
#include <Arduino.h>
// 4x5 bitmap font for N, E, S, W
const uint8_t FONT_N[5] = {
    0b1001,  // #  .  .  #
    0b1101,  // #  #  .  #
    0b1011,  // #  .  #  #
    0b1001,  // #  .  .  #
    0b1001   // #  .  .  #
};

const uint8_t FONT_E[5] = {
    0b1111,  // #  #  #  #
    0b1000,  // #  .  .  .
    0b1110,  // #  #  #  .
    0b1000,  // #  .  .  .
    0b1111   // #  #  #  #
};

const uint8_t FONT_S[5] = {
    0b0111,  // .  #  #  #
    0b1000,  // #  .  .  .
    0b0110,  // .  #  #  .
    0b0001,  // .  .  .  #
    0b1110   // #  #  #  .
};

const uint8_t FONT_W[5] = {
    0b1001,  // #  .  .  #
    0b1001,  // #  .  .  #
    0b1001,  // #  .  .  #
    0b1111,  // #  #  #  #
    0b1001   // #  .  .  #
};

