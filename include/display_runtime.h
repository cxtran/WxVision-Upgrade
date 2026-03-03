#pragma once

#include <Arduino.h>
#include "tempest.h"

// Runtime weather/time strings produced by display/weather pipeline.
extern String str_Weather_Conditions;
extern String str_Weather_Conditions_Des;
extern String str_Temp;
extern String str_Humd;
extern String str_Feels_like;
extern String str_Pressure;
extern String str_Wind_Speed;

// Clock/runtime fields shared across display modules.
extern byte t_hour;
extern byte t_minute;
extern byte t_second;
extern byte d_day;
extern byte d_month;
extern int d_year;
extern char chr_t_hour[3];
extern char chr_t_minute[3];
extern char chr_t_second[3];
extern bool reset_Time_and_Date_Display;

// Shared formatting helper implemented in display.cpp.
String formatOutdoorTemperature();
