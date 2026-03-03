#pragma once
#include <Arduino.h>
void drawSunIcon(int x, int y, uint16_t color);
void drawHouseIcon(int x, int y, uint16_t color);
void drawHumidityIcon(int x, int y, uint16_t color);
void drawWiFiIcon(int x, int y, uint16_t dimColor, uint16_t activeColor, int rssi);
void drawAlarmIcon(int x, int y, uint16_t color);
