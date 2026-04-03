#pragma once

#include <stdint.h>
#include <stddef.h>

namespace wxv {
namespace defaults {

// TempUnit:
// - C: Celsius
// - F: Fahrenheit
// Default: C
enum class TempUnit : uint8_t { C = 0, F = 1 };

// TimeFormat:
// - H12: 12-hour with AM/PM
// - H24: 24-hour
// Default: H24
enum class TimeFormat : uint8_t { H12 = 0, H24 = 1 };

// ColorMode:
// - Mono: reduced/mono look
// - Color: full color
// Default: Color
enum class ColorMode : uint8_t { Mono = 0, Color = 1 };

// Theme:
// - Day: day palette
// - Night: night palette
// - AutoSchedule: schedule-based auto mode
// - AutoAmbient: lux-based auto mode
// Default: Day
enum class Theme : uint8_t { Day = 0, Night = 1, AutoSchedule = 2, AutoAmbient = 3 };

// WiFiPolicy:
// - ManualOnly: connect only when user initiates
// - AutoReconnect: background reconnect policy
// Default: AutoReconnect
enum class WiFiPolicy : uint8_t { ManualOnly = 0, AutoReconnect = 1 };

// ForecastStrip:
// - Off: hide compact forecast strip
// - On: show compact forecast strip
// Default: On
enum class ForecastStrip : uint8_t { Off = 0, On = 1 };

// ScreenAutoRotate:
// - Off: keep current screen until changed manually
// - On: rotate screens automatically using configured interval
// Default: Off
enum class ScreenAutoRotate : uint8_t { Off = 0, On = 1 };

// ClockScreenMode:
// - Normal: local/system clock
// - WorldTime: world time view
// Default: Normal
enum class ClockScreenMode : uint8_t { Normal = 0, WorldTime = 1 };

// IconSet:
// - Mini5x7: compact icon mode
// - Standard: standard icon mode
// - None: no icon mode
// Default: Standard
enum class IconSet : uint8_t { Mini5x7 = 0, Standard = 1, None = 2 };

// MarqueeMode:
// - Off: disabled
// - Auto: enable only when text overflows
// - On: always enabled
// Default: Auto
enum class MarqueeMode : uint8_t { Off = 0, Auto = 1, On = 2 };

// Provider:
// - OpenWeatherMap
// - WeatherFlow
// - None
// Default: OpenWeatherMap
enum class Provider : uint8_t { OpenWeatherMap = 0, WeatherFlow = 1, None = 2 };

// SoundProfile:
// - Bright, Soft, Click, Chime, Pulse, Warm, Melody
// Default: Bright
enum class SoundProfile : uint8_t {
  Bright = 0,
  Soft = 1,
  Click = 2,
  Chime = 3,
  Pulse = 4,
  Warm = 5,
  Melody = 6
};

// DefaultConfig:
// Central compile-time defaults used when preferences are missing/reset.
struct DefaultConfig {
  // typed enums
  TempUnit tempUnit;
  TimeFormat timeFormat;
  ColorMode colorMode;
  Theme theme;
  WiFiPolicy wifiPolicy;
  ForecastStrip forecastStrip;
  ScreenAutoRotate screenAutoRotate;
  IconSet iconSet;
  MarqueeMode marqueeMode;
  Provider provider;

  // numeric defaults
  // brightness: range [1..100], unit: percent
  uint8_t brightness;
  bool autoBrightness;
  // screenRefreshMs: range [50..60000], unit: ms
  uint32_t screenRefreshMs;
  // wifi retry/backoff/scan intervals, unit: ms
  uint32_t wifiRetryInitialMs;
  uint32_t wifiRetryMaxMs;
  uint32_t wifiSsidNotFoundScanMs;
  uint32_t wifiConnectTimeoutMs;
  // ntp/weather intervals, unit: ms
  uint32_t ntpSyncIntervalMs;
  uint32_t weatherUpdateIntervalMs;

  // strings
  // allowed NTP hostnames
  const char* ntpServer1;
  const char* ntpServer2;

  // timezone default
  int defaultTimezoneIndex; // index into timezone list

  // existing WxVision menu-related defaults
  int dayFormat;              // 0..1
  int autoRotate;             // 0..1
  int autoRotateIntervalSec;  // range [5..300]
  int manualScreen;           // app-defined
  bool noaaAlertsEnabled;
  float noaaLatitude;
  float noaaLongitude;
  int themeIndex;             // 0 day, 1 night
  int autoThemeModeStorage;   // 0 off, 1 schedule, 2 ambient
  bool autoThemeScheduleLegacy;
  int autoThemeLuxThreshold;
  int dayThemeStartMinutes;
  int nightThemeStartMinutes;
  int scrollLevel;            // 0..9
  bool autoBrightnessEnabled;
  bool sceneClockEnabled;
  int returnToDefaultSec;    // 0 disables auto-return, otherwise timeout in seconds
  int splashDurationSec;      // 1..10
  int buzzerVolume;           // 0..100
  int buzzerToneSet;          // 0..6
  int alarmSoundMode;         // 0..4
  int forecastLinesPerDay;    // 2..3
  int forecastPauseMs;        // 0..10000
  int forecastIconSize;       // 0 or 16
  float tempOffsetC;
  int humOffset;
  int lightGainPercent;       // [1..300]
  bool envAlertCo2Enabled;
  bool envAlertTempEnabled;
  bool envAlertHumidityEnabled;
  int defaultNtpPreset;       // existing storage mapping
  int dateFormatStorage;      // existing storage mapping
  bool cloudEnabled;
  const char* cloudApiBaseUrl;
  const char* cloudRelayUrl;
  uint32_t cloudHeartbeatIntervalMs;
  uint32_t cloudReconnectInitialMs;
  uint32_t cloudReconnectMaxMs;
};

// Calibration defaults and allowed ranges:
// - TempOffsetC: unit Celsius
// - HumOffset: unit percent
// - LightGain: unit percent scaling
// Defaults/ranges match existing behavior.
enum class TempOffsetBoundTenthC : int16_t {
  Min = -100,   // -10.0 C
  Default = 0,  // 0.0 C
  Max = 100,    // 10.0 C
  Step = 1      // 0.1 C
};

enum class HumOffsetBound : int8_t {
  Min = -20,
  Default = 0,
  Max = 20,
  Step = 1
};

enum class LightGainBoundPercent : int16_t {
  Min = 1,
  Default = 12,
  Max = 300,
  Step = 5
};

enum class SoundVolumeBound : int16_t {
  Min = 0,
  Default = 8,
  Max = 100,
  Step = 5
};

constexpr int toInt(TempOffsetBoundTenthC v) { return static_cast<int>(v); }
constexpr int toInt(HumOffsetBound v) { return static_cast<int>(v); }
constexpr int toInt(LightGainBoundPercent v) { return static_cast<int>(v); }
constexpr int toInt(SoundVolumeBound v) { return static_cast<int>(v); }
constexpr float tempOffsetFromTenthC(TempOffsetBoundTenthC v) { return static_cast<float>(toInt(v)) / 10.0f; }

// Compatibility aliases used across existing code paths.
static const float kTempOffsetDefaultC = tempOffsetFromTenthC(TempOffsetBoundTenthC::Default);
static const float kTempOffsetMinC = tempOffsetFromTenthC(TempOffsetBoundTenthC::Min);
static const float kTempOffsetMaxC = tempOffsetFromTenthC(TempOffsetBoundTenthC::Max);
static const float kTempOffsetStepC = tempOffsetFromTenthC(TempOffsetBoundTenthC::Step);
static const int kHumOffsetDefault = toInt(HumOffsetBound::Default);
static const int kHumOffsetMin = toInt(HumOffsetBound::Min);
static const int kHumOffsetMax = toInt(HumOffsetBound::Max);
static const int kHumOffsetStep = toInt(HumOffsetBound::Step);
static const int kLightGainDefaultPercent = toInt(LightGainBoundPercent::Default);
static const int kLightGainMinPercent = toInt(LightGainBoundPercent::Min);
static const int kLightGainMaxPercent = toInt(LightGainBoundPercent::Max);
static const int kLightGainStepPercent = toInt(LightGainBoundPercent::Step);
static const int kSoundVolumeDefault = toInt(SoundVolumeBound::Default);
static const int kSoundVolumeMin = toInt(SoundVolumeBound::Min);
static const int kSoundVolumeMax = toInt(SoundVolumeBound::Max);
static const int kSoundVolumeStep = toInt(SoundVolumeBound::Step);
static const SoundProfile kSoundProfileDefault = SoundProfile::Melody;

static const int kScrollDelays[10] = {500, 300, 200, 150, 100, 75, 50, 30, 20, 10};

static const DefaultConfig kDefaults = {
    TempUnit::C,
    TimeFormat::H24,
    ColorMode::Color,
    Theme::Day,
    WiFiPolicy::AutoReconnect,
    ForecastStrip::On,
    ScreenAutoRotate::Off,
    IconSet::Standard,
    MarqueeMode::Auto,
    Provider::None,

    10,         // brightness
    true,       // autoBrightness
    1000,       // screenRefreshMs
    5000,       // wifiRetryInitialMs
    60000,      // wifiRetryMaxMs
    60000,      // wifiSsidNotFoundScanMs
    20000,      // wifiConnectTimeoutMs
    86400000,   // ntpSyncIntervalMs
    600000,     // weatherUpdateIntervalMs

    "time.google.com",
    "pool.ntp.org",
    10,         // UTC entry in current timezone list

    0,          // dayFormat
    0,          // autoRotate
    15,         // autoRotateIntervalSec
    0,          // manualScreen
    false,      // noaaAlertsEnabled
    0.0f,       // noaaLatitude
    0.0f,       // noaaLongitude
    0,          // themeIndex
    0,          // autoThemeModeStorage
    false,      // autoThemeScheduleLegacy
    20,         // autoThemeLuxThreshold
    6 * 60,     // dayThemeStartMinutes
    20 * 60,    // nightThemeStartMinutes
    7,          // scrollLevel
    true,       // autoBrightnessEnabled
    true,       // sceneClockEnabled
    0,          // returnToDefaultSec
    3,          // splashDurationSec
    kSoundVolumeDefault,        // buzzerVolume
    static_cast<int>(kSoundProfileDefault),          // buzzerToneSet
    0,          // alarmSoundMode
    3,          // forecastLinesPerDay
    3000,       // forecastPauseMs
    16,         // forecastIconSize
    kTempOffsetDefaultC,       // tempOffsetC
    kHumOffsetDefault,         // humOffset
    kLightGainDefaultPercent,  // lightGainPercent
    true,       // envAlertCo2Enabled
    true,       // envAlertTempEnabled
    true,       // envAlertHumidityEnabled
    1,          // defaultNtpPreset
    0,          // dateFormatStorage
    true,       // cloudEnabled
    "https://api.wxvisions.com",
    "wss://relay.wxvisions.com/ws/device",
    30000,      // cloudHeartbeatIntervalMs
    5000,       // cloudReconnectInitialMs
    60000       // cloudReconnectMaxMs
};

// Explicit storage mappings for compatibility with existing Preferences keys.
constexpr int toStorage(TimeFormat v) { return v == TimeFormat::H24 ? 1 : 0; }
constexpr TimeFormat fromStorageTimeFormat(int v, TimeFormat fallback) {
  return (v == 0) ? TimeFormat::H12 : (v == 1 ? TimeFormat::H24 : fallback);
}

constexpr int toStorage(Provider v) {
  return (v == Provider::OpenWeatherMap) ? 0 : (v == Provider::WeatherFlow ? 1 : 2);
}
constexpr Provider fromStorageProvider(int v, Provider fallback) {
  return (v == 0) ? Provider::OpenWeatherMap : (v == 1 ? Provider::WeatherFlow : (v == 2 ? Provider::None : fallback));
}

constexpr int toStorage(Theme v) {
  return (v == Theme::Night) ? 1 : 0; // existing theme key stores day/night only
}

constexpr int toStorageAutoThemeMode(Theme v) {
  return (v == Theme::AutoSchedule) ? 1 : ((v == Theme::AutoAmbient) ? 2 : 0);
}

constexpr int toInt(MarqueeMode v) { return static_cast<int>(v); }
constexpr int toInt(IconSet v) { return static_cast<int>(v); }
constexpr int toInt(ForecastStrip v) { return static_cast<int>(v); }
constexpr int toInt(WiFiPolicy v) { return static_cast<int>(v); }
constexpr int toStorage(SoundProfile v) { return static_cast<int>(v); }
constexpr SoundProfile fromStorageSoundProfile(int v, SoundProfile fallback) {
  return (v >= static_cast<int>(SoundProfile::Bright) && v <= static_cast<int>(SoundProfile::Melody))
             ? static_cast<SoundProfile>(v)
             : fallback;
}
constexpr int toStorage(ScreenAutoRotate v) { return v == ScreenAutoRotate::On ? 1 : 0; }
constexpr ScreenAutoRotate fromStorageScreenAutoRotate(int v, ScreenAutoRotate fallback) {
  return (v == 0) ? ScreenAutoRotate::Off : (v == 1 ? ScreenAutoRotate::On : fallback);
}

inline constexpr const char* toString(TempUnit v) { return (v == TempUnit::F) ? "F" : "C"; }
inline constexpr const char* toString(TimeFormat v) { return (v == TimeFormat::H24) ? "H24" : "H12"; }
inline constexpr const char* toString(Theme v) {
  return (v == Theme::Night) ? "Night" :
         (v == Theme::AutoSchedule) ? "AutoSchedule" :
         (v == Theme::AutoAmbient) ? "AutoAmbient" : "Day";
}
inline constexpr const char* toString(Provider v) {
  return (v == Provider::WeatherFlow) ? "WeatherFlow" :
         (v == Provider::None) ? "None" : "OpenWeatherMap";
}
inline constexpr const char* toString(SoundProfile v) {
  return (v == SoundProfile::Soft) ? "Soft" :
         (v == SoundProfile::Click) ? "Click" :
         (v == SoundProfile::Chime) ? "Chime" :
         (v == SoundProfile::Pulse) ? "Pulse" :
         (v == SoundProfile::Warm) ? "Warm" :
         (v == SoundProfile::Melody) ? "Melody" : "Bright";
}
inline constexpr const char* toString(ScreenAutoRotate v) { return (v == ScreenAutoRotate::On) ? "On" : "Off"; }

} // namespace defaults
} // namespace wxv
