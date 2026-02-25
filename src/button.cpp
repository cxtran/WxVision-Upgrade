
#include <Arduino.h>
#include <SPI.h>
#include "display.h"
#include "pins.h"
#include "settings.h"
#include "menu.h"
#include "ir_codes.h"
#include "sensors.h" // for enqueueVirtualIRKey
#include "buzzer.h"
#include "alarm.h"
#include "notifications.h"

// Explicit forward declarations to satisfy compilation order
extern bool isAlarmCurrentlyActive();
extern void cancelActiveAlarm();

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
  static unsigned long warmupUntilMs = 0;
  static unsigned long lastUpEdgeMs = 0;
  static unsigned long lastDnEdgeMs = 0;
  static unsigned long lastLeftEdgeMs = 0;
  static unsigned long lastRightEdgeMs = 0;
  static unsigned long lastCtrEdgeMs = 0;
  const unsigned long now = millis();
  const unsigned long debounceMs = 120;
  if (warmupUntilMs == 0) {
    // Ignore startup pin transients right after boot.
    warmupUntilMs = now + 1800;
  }

  bool up  = digitalRead(BTN_UP);
  bool dn  = digitalRead(BTN_DN);
  bool left  = digitalRead(BTN_LEFT);
  bool right = digitalRead(BTN_RIGHT);
  bool ctr = digitalRead(BTN_SEL);
  const int toneMs = 150;

  if (now < warmupUntilMs) {
    // Track the stable baseline during warmup so no synthetic edge is generated later.
    lastUp = up;
    lastDn = dn;
    lastLeft = left;
    lastRight = right;
    lastCtr = ctr;
    return;
  }

  // Detect button press (HIGH to LOW transition)
  if (lastUp == HIGH && up == LOW && (now - lastUpEdgeMs) >= debounceMs) {
    lastUpEdgeMs = now;
    Serial.println("UP button pressed");
    playBuzzerTone(1500, toneMs);
    if (isAlarmCurrentlyActive()) cancelActiveAlarm();
    if (currentScreen == SCREEN_LUNAR_LUCK) {
      adjustLunarLuckSpeed(+1);
    } else {
      enqueueVirtualIRKey(IRCodes::WxKey::Up);
    }
  
  }
  if (lastDn == HIGH && dn == LOW && (now - lastDnEdgeMs) >= debounceMs) {
    lastDnEdgeMs = now;
    Serial.println("DOWN button pressed");
    playBuzzerTone(1200, toneMs);
    if (isAlarmCurrentlyActive()) cancelActiveAlarm();
    if (currentScreen == SCREEN_LUNAR_LUCK) {
      adjustLunarLuckSpeed(-1);
    } else {
      enqueueVirtualIRKey(IRCodes::WxKey::Down);
    }
  }
  if (lastLeft == HIGH && left == LOW && (now - lastLeftEdgeMs) >= debounceMs) {
    lastLeftEdgeMs = now;
    Serial.println("LEFT button pressed");
    playBuzzerTone(900, toneMs);
    if (isAlarmCurrentlyActive()) cancelActiveAlarm();
    enqueueVirtualIRKey(IRCodes::WxKey::Left);
  }
  if (lastRight == HIGH && right == LOW && (now - lastRightEdgeMs) >= debounceMs) {
    lastRightEdgeMs = now;
    Serial.println("RIGHT button pressed");
    playBuzzerTone(1800, toneMs);
    if (isAlarmCurrentlyActive()) cancelActiveAlarm();
    enqueueVirtualIRKey(IRCodes::WxKey::Right);
  }
  if (lastCtr == HIGH && ctr == LOW && (now - lastCtrEdgeMs) >= debounceMs) {
    lastCtrEdgeMs = now;
    Serial.println("CENTER button pressed");
    playBuzzerTone(2200, toneMs);
    if (isAlarmCurrentlyActive()) cancelActiveAlarm();
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
      enqueueVirtualIRKey(IRCodes::WxKey::Ok);
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
    wxv::notify::showNotification(wxv::notify::NotifyId::Resetting, dma_display->color565(255, 0, 0));
    delay(250);

    ESP.restart(); // Built-in ESP32 software reset
}
