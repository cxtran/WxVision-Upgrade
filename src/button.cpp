
#include <Arduino.h>
#include <SPI.h>
#include "display.h"
#include "pins.h"
#include "settings.h"
#include "menu.h"
#include "ir_codes.h"
#include "sensors.h" // for enqueueVirtualIRCode
#include "buzzer.h"

void setupButtons() {
   
// Serial.begin(115200);
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DN, INPUT_PULLUP);
    pinMode(BTN_LEFT, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(BTN_SEL, INPUT_PULLUP);

    Serial.println("=== 5-Way Switch Test ===");
    Serial.println("Press UP, DOWN, LEFT, RIGHT, or SELECT...");
}
void getButton(){
  static bool lastUp = HIGH, lastDn = HIGH, lastCtr = HIGH, lastLeft = HIGH, lastRight = HIGH;

  bool up  = digitalRead(BTN_UP);
  bool dn  = digitalRead(BTN_DN);
  bool left  = digitalRead(BTN_LEFT);
  bool right = digitalRead(BTN_RIGHT);
  bool ctr = digitalRead(BTN_SEL);
  const int toneMs = 150;

  // Detect button press (HIGH to LOW transition)
  if (lastUp == HIGH && up == LOW) {
    Serial.println("UP button pressed");
    playBuzzerTone(1500, toneMs);
    enqueueVirtualIRCode(IR_UP);
  
  }
  if (lastDn == HIGH && dn == LOW) {
    Serial.println("DOWN button pressed");
    playBuzzerTone(1200, toneMs);
    enqueueVirtualIRCode(IR_DOWN);
  }
  if (lastLeft == HIGH && left == LOW) {
    Serial.println("LEFT button pressed");
    playBuzzerTone(900, toneMs);
    enqueueVirtualIRCode(IR_LEFT);
  }
  if (lastRight == HIGH && right == LOW) {
    Serial.println("RIGHT button pressed");
    playBuzzerTone(1800, toneMs);
    enqueueVirtualIRCode(IR_RIGHT);
  }
  if (lastCtr == HIGH && ctr == LOW) {
    Serial.println("CENTER button pressed");
    playBuzzerTone(2200, toneMs);
    if (!menuActive)
    {
      menuActive = true;
      currentMenuLevel = MENU_MAIN;
      currentMenuIndex = 0;
      menuScroll = 0;
      showMainMenuModal();
    }
    else
    {
      enqueueVirtualIRCode(IR_OK);
    }
  }

  lastUp = up;
  lastDn = dn;
  lastLeft = left;
  lastRight = right;
  lastCtr = ctr;
}

void triggerPhysicalReset() {
    // Optional: Show a message first
    dma_display->fillScreen(0);
    dma_display->setTextColor(dma_display->color565(255,0,0));
    dma_display->setCursor(0, 8);
    dma_display->print("Resetting...");
    delay(250);

    ESP.restart(); // Built-in ESP32 software reset
}
