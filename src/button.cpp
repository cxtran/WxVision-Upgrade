
#include <Arduino.h>
#include <SPI.h>
#include "display.h"
#include "pins.h"
#include "settings.h"
#include "menu.h"

void setupButtons() {
   
// Serial.begin(115200);
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DN, INPUT_PULLUP);
    pinMode(BTN_SEL, INPUT_PULLUP);

    Serial.println("=== 3-Way Switch Test ===");
    Serial.println("Press UP, DOWN, or SELECT...");
}
  
bool lastUp = HIGH;
bool lastDn = HIGH;
bool lastCtr = HIGH;

void getButton(){
  static bool lastUp = HIGH, lastDn = HIGH, lastCtr = HIGH;

  bool up  = digitalRead(BTN_UP);
  bool dn  = digitalRead(BTN_DN);
  bool ctr = digitalRead(BTN_SEL);

  // Detect button press (HIGH to LOW transition)
  if (lastUp == HIGH && up == LOW) {
    Serial.println("UP button pressed");
    handleUp();
  
  }
  if (lastDn == HIGH && dn == LOW) {
    Serial.println("DOWN button pressed");
    handleDown();
  }
  if (lastCtr == HIGH && ctr == LOW) {
    Serial.println("CENTER button pressed");
    handleSelect();
  }

  lastUp = up;
  lastDn = dn;
  lastCtr = ctr;
}