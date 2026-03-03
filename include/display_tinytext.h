#pragma once

#include <Arduino.h>
#include <stdint.h>

int drawTinyVietnameseText(int x, int yTop, const String &text, uint16_t color);
uint16_t tinyVietnameseTextWidth(const String &text);
