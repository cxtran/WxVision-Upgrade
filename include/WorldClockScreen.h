#pragma once

#include <Arduino.h>

void drawWorldClockScreen();
void resetWorldClockScreenState();
bool worldClockHandleStep(int delta);
void handleWorldClockSelectPress();
