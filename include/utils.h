#pragma once
#include <Arduino.h>

int customRoundString(const char *str);

//int getTextWidth(const char* text);

bool needsScroll(const char* text) ;

enum class EnvBand : int;

void drawScrollingText(const char* text, int y, uint16_t color, int selectedIndex);

void drawBackArrow(int x, int y, uint16_t color);

void drawEnvBandIcon(int x, int y, EnvBand band, uint16_t bandColor, uint16_t backgroundColor, bool emphasize, bool swapFaceForeground = false);


