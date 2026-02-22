#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace wxv {
namespace notify {

// Abbreviations:
// WIFI = wireless network, NTP = network time, WX = weather
// CFG = configuration, SYS = system, ERR = error
// All rendered lines are clamped to <= 11 chars for 64x32 HUB75 (5x7 font).
static const uint8_t kNotifyMaxChars = 11;

enum class NotifyId : uint8_t {
  WifiConnecting,
  WifiConnected,
  WifiFail,
  WifiNoNet,
  WifiAuthFail,
  NtpSync,
  NtpOk,
  NtpFail,
  SaveOk,
  SaveFail,
  WeatherUpdate,
  WeatherFail,
  SensorFail,
  SystemError,
  WifiScan,
  WifiTimeout,
  OtaUpdate,
  Upgrading,
  Restoring,
  FactoryReset,
  Resetting
};

struct NotificationText {
  const char* line1;
  const char* line2;  // nullptr means single-line
  bool isWaiting;     // auto-append "..." to active line when true
};

NotificationText textFor(NotifyId id);
String clampNotifyLine(const String& line);
void showNotification(NotifyId id, uint16_t line1Color, uint16_t line2Color, const String& line2Override = "");
void showNotification(NotifyId id, uint16_t line1Color, const String& line2Override = "");
void showNotification(NotifyId id);

}  // namespace notify
}  // namespace wxv
