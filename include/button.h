#pragma once
// Button Reset
const unsigned long resetHoldTime = 5000; // 5 seconds hold for reset
bool resetLongPressHandled = false;
unsigned long buttonDownMillis = 0;
bool buttonWasDown = false;

void setupButtons();
void getButton();
void triggerPhysicalReset();