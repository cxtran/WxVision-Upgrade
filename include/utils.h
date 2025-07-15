#pragma once
#include <Arduino.h>

int customRoundString(const char *str);

//int getTextWidth(const char* text);

bool needsScroll(const char* text) ;


void drawScrollingText(const char* text, int y, uint16_t color, int selectedIndex);