
#include <Arduino.h>
#include <SPI.h>
#include "display.h"
#include "pins.h"
#include "settings.h"
#include "menu.h"
#include "InfoModal.h"
#include "ir_codes.h"
#include "sensors.h" // for enqueueVirtualIRKey
#include "audio_announcer.h"
#include "alarm.h"
#include "notifications.h"
#include "keyboard.h"
#include "screen_manager.h"

// Explicit forward declarations to satisfy compilation order
extern bool isAlarmCurrentlyActive();
extern void cancelActiveAlarm();
extern InfoModal dateModal;

namespace
{
bool shouldRotateScreenDirectlyFromButton()
{
  return !menuActive &&
         !wifiSelecting &&
         !inKeyboardMode &&
         !isSectionHeadingActive() &&
         !isTemporaryAlertActive() &&
         currentScreen != SCREEN_NOAA_ALERT;
}

void dispatchHorizontalButton(IRCodes::WxKey key, int direction)
{
  if (shouldRotateScreenDirectlyFromButton())
  {
    rotateScreen(direction);
    return;
  }
  enqueueVirtualIRKey(key);
}
}

void setupButtons() {
#if WXV_ENABLE_5WAY_BUTTONS
if (BTN_UP >= 0) pinMode(BTN_UP, INPUT_PULLUP);
if (BTN_DN >= 0) pinMode(BTN_DN, INPUT_PULLUP);
if (BTN_LEFT >= 0) pinMode(BTN_LEFT, INPUT_PULLUP);
if (BTN_RIGHT >= 0) pinMode(BTN_RIGHT, INPUT_PULLUP);
if (BTN_SEL >= 0) pinMode(BTN_SEL, INPUT_PULLUP);
#endif
}
void getButton(){
#if !WXV_ENABLE_5WAY_BUTTONS
  return;
#else
  static bool lastUp = HIGH, lastDn = HIGH, lastCtr = HIGH, lastLeft = HIGH, lastRight = HIGH;
  static unsigned long warmupUntilMs = 0;
  static unsigned long lastUpEdgeMs = 0;
  static unsigned long lastDnEdgeMs = 0;
  static unsigned long lastLeftEdgeMs = 0;
  static unsigned long lastRightEdgeMs = 0;
  static unsigned long lastCtrEdgeMs = 0;
  static unsigned long leftHoldStartMs = 0;
  static unsigned long rightHoldStartMs = 0;
  static unsigned long lastLeftRepeatMs = 0;
  static unsigned long lastRightRepeatMs = 0;
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
  if (now < warmupUntilMs) {
    // Track the stable baseline during warmup so no synthetic edge is generated later.
    lastUp = up;
    lastDn = dn;
    lastLeft = left;
    lastRight = right;
    lastCtr = ctr;
    leftHoldStartMs = 0;
    rightHoldStartMs = 0;
    lastLeftRepeatMs = 0;
    lastRightRepeatMs = 0;
    return;
  }

  // Detect button press (HIGH to LOW transition)
  if (lastUp == HIGH && up == LOW && (now - lastUpEdgeMs) >= debounceMs) {
    lastUpEdgeMs = now;
    if (isAlarmCurrentlyActive()) cancelActiveAlarm();
    enqueueVirtualIRKey(IRCodes::WxKey::Up);
  
  }
  if (lastDn == HIGH && dn == LOW && (now - lastDnEdgeMs) >= debounceMs) {
    lastDnEdgeMs = now;
    if (isAlarmCurrentlyActive()) cancelActiveAlarm();
    enqueueVirtualIRKey(IRCodes::WxKey::Down);
  }
  if (lastLeft == HIGH && left == LOW && (now - lastLeftEdgeMs) >= debounceMs) {
    lastLeftEdgeMs = now;
    leftHoldStartMs = now;
    lastLeftRepeatMs = now;
    if (isAlarmCurrentlyActive()) cancelActiveAlarm();
    dispatchHorizontalButton(IRCodes::WxKey::Left, -1);
  }
  if (lastRight == HIGH && right == LOW && (now - lastRightEdgeMs) >= debounceMs) {
    lastRightEdgeMs = now;
    rightHoldStartMs = now;
    lastRightRepeatMs = now;
    if (isAlarmCurrentlyActive()) cancelActiveAlarm();
    dispatchHorizontalButton(IRCodes::WxKey::Right, 1);
  }
  if (lastCtr == HIGH && ctr == LOW && (now - lastCtrEdgeMs) >= debounceMs) {
    lastCtrEdgeMs = now;
    if (isAlarmCurrentlyActive()) cancelActiveAlarm();
    if (!menuActive)
    {
      wxv::announce::playUiSound("select");
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

  auto repeatIntervalForHold = [](unsigned long heldMs) -> unsigned long {
    if (heldMs >= 1500UL) return 55UL;
    if (heldMs >= 800UL) return 90UL;
    return 140UL;
  };

  bool allowDateModalHoldRepeat = dateModal.isActive();
  if (allowDateModalHoldRepeat && left == LOW && lastLeft == LOW)
  {
    if (leftHoldStartMs == 0)
      leftHoldStartMs = now;
    if ((now - leftHoldStartMs) >= 350UL)
    {
      unsigned long interval = repeatIntervalForHold(now - leftHoldStartMs);
      if ((now - lastLeftRepeatMs) >= interval)
      {
        lastLeftRepeatMs = now;
        enqueueVirtualIRKey(IRCodes::WxKey::Left);
      }
    }
  }
  if (allowDateModalHoldRepeat && right == LOW && lastRight == LOW)
  {
    if (rightHoldStartMs == 0)
      rightHoldStartMs = now;
    if ((now - rightHoldStartMs) >= 350UL)
    {
      unsigned long interval = repeatIntervalForHold(now - rightHoldStartMs);
      if ((now - lastRightRepeatMs) >= interval)
      {
        lastRightRepeatMs = now;
        enqueueVirtualIRKey(IRCodes::WxKey::Right);
      }
    }
  }

  if (left == HIGH) {
    leftHoldStartMs = 0;
    lastLeftRepeatMs = 0;
  }
  if (right == HIGH) {
    rightHoldStartMs = 0;
    lastRightRepeatMs = 0;
  }

  lastUp = up;
  lastDn = dn;
  lastLeft = left;
  lastRight = right;
  lastCtr = ctr;
#endif
}

void triggerPhysicalReset() {
    // Optional: Show a message first
    wxv::notify::showNotification(wxv::notify::NotifyId::Resetting, dma_display->color565(255, 0, 0));
    delay(250);

    ESP.restart(); // Built-in ESP32 software reset
}
