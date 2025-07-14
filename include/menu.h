#pragma once
#include    <Arduino.h>

void handleButtonInput();
void handleIR(uint32_t code);

#pragma once
#include <Arduino.h>

// Menu identifiers
enum MenuLevel {
  MENU_MAIN,
  MENU_DEVICE,
  MENU_DISPLAY,
  MENU_WEATHER,
  MENU_CALIBRATION,
  MENU_SYSTEM,
  MENU_MANUAL_SCREEN,
};

enum MenuItem {
  // Main
  MAIN_DEVICE_SETTINGS,
  MAIN_DISPLAY_SETTINGS,
  MAIN_WEATHER_SETTINGS,
  MAIN_CALIBRATION,
  MAIN_SYSTEM_ACTIONS,
  MAIN_SAVE_EXIT,

  // Device
  DEVICE_UNITS,
  DEVICE_DAY_FORMAT,
  DEVICE_FORECAST_SOURCE,
  DEVICE_AUTO_ROTATE,
  DEVICE_MANUAL_SCREEN,
  DEVICE_EXIT,

  // Display
  DISPLAY_THEME,
  DISPLAY_BRIGHTNESS,
  DISPLAY_SCROLL_SPEED,
  DISPLAY_CUSTOM_MSG,
  DISPLAY_EXIT,

  // Weather
  WEATHER_OWM_CITY,
  WEATHER_OWM_KEY,
  WEATHER_WF_TOKEN,
  WEATHER_WF_STATION,
  WEATHER_EXIT,

  // Calibration
  CALIB_TEMP_OFFSET,
  CALIB_HUM_OFFSET,
  CALIB_LIGHT_GAIN,
  CALIB_EXIT,

  // System
  SYS_OTA,
  SYS_RESET_POWER,
  SYS_QUICK_RESTORE,
  SYS_FACTORY_RESET,
  SYS_EXIT
};

extern MenuLevel currentMenuLevel;
extern int currentMenuIndex;

void updateMenu();
void drawMenu();
void handleUp();
void handleDown();
void handleSelect();
void handleLeft();
void handleRight();
