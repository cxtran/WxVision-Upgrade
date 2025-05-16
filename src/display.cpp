#include "display.h"

MatrixPanel_I2S_DMA *dma_display = nullptr;

uint16_t myRED, myGREEN, myBLUE, myWHITE, myBLACK;

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
  dma_display->setBrightness8(3);
  dma_display->setTextSize(1);
  dma_display->setTextWrap(false);
  dma_display->setFont(&Font5x7Uts);

  myRED    = dma_display->color565(255, 0, 0);
  myGREEN  = dma_display->color565(0, 255, 0);
  myBLUE   = dma_display->color565(0, 0, 255);
  myWHITE  = dma_display->color565(255, 255, 255);
  myBLACK  = dma_display->color565(0, 0, 0);
}
