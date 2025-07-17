#include "display.h"
#include "icons.h"
#include "settings.h"


MatrixPanel_I2S_DMA *dma_display = nullptr;

uint16_t myRED, myGREEN, myBLUE, myWHITE, myBLACK, myYELLOW, myCYAN;

void setupDisplay() {
  HUB75_I2S_CFG::i2s_pins _pins = {
    R1_PIN, G1_PIN, B1_PIN,
    R2_PIN, G2_PIN, B2_PIN,
    A_PIN, B_PIN, C_PIN, D_PIN, E_PIN,
    LAT_PIN, OE_PIN, CLK_PIN
  };

  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN, _pins);
  mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_15M;
  mxconfig.min_refresh_rate = 120;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(brightness);
  dma_display->setTextSize(1);
  dma_display->setTextWrap(false);
  dma_display->setFont(&Font5x7Uts);

  myRED    = dma_display->color565(255, 0, 0);
  myGREEN  = dma_display->color565(0, 255, 0);
  myBLUE   = dma_display->color565(0, 0, 255);
  myWHITE  = dma_display->color565(255, 255, 255);
  myBLACK  = dma_display->color565(0, 0, 0);
  myYELLOW = dma_display->color565(255, 255, 0);
  myCYAN = dma_display->color565(0, 255, 255);
}

int getTextWidth(const char* text) {
  int16_t x1, y1;
  uint16_t w, h;
  dma_display->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return w;
}

const uint8_t* getWeatherIconFromCode(String code) {
  Serial.printf("Code: %s", code);
  if (code.startsWith("01n")) return icon_clear_night;
  if (code.startsWith("02n")) return icon_cloud_night;
  if (code.startsWith("01d")) return icon_clear;
  if (code.startsWith("02d")) return icon_cloudy;
  if (code.startsWith("03") || code.startsWith("04")) return icon_cloudy;
  if (code.startsWith("09") || code.startsWith("10")) return icon_rain;
  if (code.startsWith("11")) return icon_thunder;
  if (code.startsWith("13")) return icon_snow;
  if (code.startsWith("50")) return icon_fog;
  return icon_clear; // fallback
}

const uint8_t* getWeatherIconFromCondition(String condition) {
  condition.toLowerCase();
  if (condition.indexOf("clear") >= 0) return icon_clear;
  if (condition.indexOf("cloud") >= 0) return icon_cloudy;
  if (condition.indexOf("rain") >= 0) return icon_rain;
  if (condition.indexOf("storm") >= 0) return icon_thunder;
  if (condition.indexOf("snow") >= 0) return icon_snow;
  if (condition.indexOf("fog") >= 0 || condition.indexOf("mist") >= 0) return icon_fog;
  return icon_clear;
}

const uint16_t getIconColorFromCondition(String condition){
  if (condition.indexOf("clear") >= 0) return dma_display->color565(255, 255, 0); // (yellow)
  if (condition.indexOf("cloud") >= 0) return dma_display->color565(180, 180, 180);
  if (condition.indexOf("rain") >= 0) return dma_display->color565(0, 200, 255);
  if (condition.indexOf("storm") >= 0) return dma_display->color565(255, 255, 0);
  if (condition.indexOf("snow") >= 0) return dma_display->color565(220, 255, 255);
  if (condition.indexOf("fog") >= 0 || condition.indexOf("mist") >= 0) return dma_display->color565(180, 180, 180);
  return dma_display->color565(255, 255, 0);
}

const uint16_t getDayNightColorFromCode( String code){
  if (code.indexOf("d") >= 0) return dma_display->color565(255, 170, 51); // day color
  else return myBLUE; // night color
}

