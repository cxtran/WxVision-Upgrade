#pragma once
#include <Arduino.h>

// -------- Enums --------
enum class TempUnit    : uint8_t { C, F };
enum class WindUnit    : uint8_t { MPS, MPH, KTS, KPH };
enum class PressUnit   : uint8_t { HPA, INHG };
enum class PrecipUnit  : uint8_t { MM, INCH };
enum class DistanceUnit : uint8_t { M, KM, FEET, MILE };

// -------- Prefs Struct --------
struct UnitPrefs {
  TempUnit   temp   = TempUnit::C;
  WindUnit   wind   = WindUnit::MPS;
  PressUnit  press  = PressUnit::HPA;
  PrecipUnit precip = PrecipUnit::MM;
  DistanceUnit distance = DistanceUnit::KM;
  bool       clock24h = true;   // 24h/12h display flag
};

// Global instance (make sure no other header declares `extern int units` anymore)
extern UnitPrefs units;

// -------- Conversions (SI -> selected) --------
inline double c_to_f(double c)      { return c * 9.0/5.0 + 32.0; }
inline double mps_to_mph(double v)  { return v * 2.23693629; }
inline double mps_to_kts(double v)  { return v * 1.94384449; }
inline double mps_to_kph(double v)  { return v * 3.6; }
inline double hpa_to_inhg(double p) { return p * 0.0295299831; }
inline double mm_to_in(double r)    { return r * 0.0393700787; }
inline double km_to_miles(double km){ return km * 0.621371192; }
inline double meters_to_feet(double m){ return m * 3.280839895; }

inline double dispDistanceMeters(double meters) {
  switch (units.distance) {
    case DistanceUnit::KM:   return meters / 1000.0;
    case DistanceUnit::FEET: return meters_to_feet(meters);
    case DistanceUnit::MILE: return km_to_miles(meters / 1000.0);
    default:                 return meters;
  }
}

inline double dispDistanceKm(double km) {
  return dispDistanceMeters(km * 1000.0);
}

// Numeric “display” helpers (return the value in the currently-selected units)
inline double dispTemp(double c) {
  return (units.temp == TempUnit::F) ? c_to_f(c) : c;
}

inline double dispTempOffset(double cOffset) {
  return (units.temp == TempUnit::F) ? (cOffset * 9.0 / 5.0) : cOffset;
}

inline double tempOffsetToC(double displayOffset) {
  return (units.temp == TempUnit::F) ? (displayOffset * 5.0 / 9.0) : displayOffset;
}
inline double dispWind(double mps) {
  switch (units.wind) {
    case WindUnit::MPH: return mps_to_mph(mps);
    case WindUnit::KTS: return mps_to_kts(mps);
    case WindUnit::KPH: return mps_to_kph(mps);
    default:            return mps;
  }
}
inline double dispPress(double hpa) {
  return (units.press == PressUnit::INHG) ? hpa_to_inhg(hpa) : hpa;
}
inline double dispPrecip(double mm) {
  return (units.precip == PrecipUnit::INCH) ? mm_to_in(mm) : mm;
}

// Suffix helpers (store in flash)
inline const __FlashStringHelper* tempSuffix()   { return (units.temp==TempUnit::F)     ? F("°F")   : F("°C"); }
inline const __FlashStringHelper* windSuffix()   {
  switch (units.wind) {
    case WindUnit::MPH: return F(" mph");
    case WindUnit::KTS: return F(" kt");
    case WindUnit::KPH: return F(" km/h");
    default:            return F(" m/s");
  }
}
inline const __FlashStringHelper* pressSuffix()  { return (units.press==PressUnit::INHG) ? F(" inHg") : F(" hPa"); }
inline const __FlashStringHelper* precipSuffix() { return (units.precip==PrecipUnit::INCH)? F("\"")    : F(" mm"); }
inline const __FlashStringHelper* distanceSuffix() {
  switch (units.distance) {
    case DistanceUnit::KM:   return F(" km");
    case DistanceUnit::FEET: return F(" feet");
    case DistanceUnit::MILE: return F(" mile");
    default:                 return F(" m");
  }
}

// Pretty formatters (dp is cast to unsigned int to avoid ambiguity with String ctors)
String fmtTemp  (double c,   uint8_t dp = 1);
String fmtWind  (double mps, uint8_t dp = 1);
String fmtPress (double hpa, uint8_t dp = 1);
String fmtPrecip(double mm,  uint8_t dp = 2);
String fmtDistanceMeters(double meters, uint8_t dp = 1);
String fmtDistanceKm(double km, uint8_t dp = 1);

// Load/save from NVS (ESP32 Preferences)
void loadUnits();
void saveUnits();

// Simple toggles (hook to buttons/menus)
void toggleTempUnit();
void cycleWindUnit();
void togglePressUnit();
void togglePrecipUnit();
void cycleDistanceUnit();

// Quick presets
void setMetric();   // C, m/s, hPa, mm
void setImperial(); // F, mph, inHg, inch

// --- Rebuild triggers for scrolling text ---
static bool needScrollRebuild = true;

// Snapshot of unit settings; force a first-time rebuild with an impossible value
static uint16_t lastUnitSig = 0xFFFF;

// Pack all unit choices into a small signature so we can detect any change.
static inline uint16_t unitSignature() {
  // temp (2 bits), wind (2), press (2), precip (2), distance (2), clock24h (1)
  return  ((uint16_t)units.temp   & 0x3)
        | (((uint16_t)units.wind   & 0x3) << 2)
        | (((uint16_t)units.press  & 0x3) << 4)
        | (((uint16_t)units.precip & 0x3) << 6)
        | (((uint16_t)units.distance & 0x3) << 8)
        | ((units.clock24h ? 1u : 0u)      << 10);
}
