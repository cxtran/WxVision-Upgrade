#include <Arduino.h>
#include <SPI.h>
#include "display.h"

// put function declarations here:
int myFunction(int, int);

void setup() {
  Serial.begin(115200);
  delay(1000);

  setupDisplay();                 // Initialize RGB matrix
  dma_display->clearScreen();     // Clear everything
  dma_display->setTextColor(myGREEN);
  dma_display->setCursor(0, 0);   // Top-left corner
  dma_display->print("VisionWX Ready!");
}

void loop() {
  // put your main code here, to run repeatedly:
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}