#include "units.h"
#include <Preferences.h>
#include "display.h"
#include "default_values.h"

UnitPrefs units;                 // the global
static Preferences u_prefs;

static const char* NS = "visionwx";  // same namespace as your other prefs
// keys are distinct so they don't collide with existing ones
static const char* K_TEMP  = "u_temp";
static const char* K_WIND  = "u_wind";
static const char* K_PRESS = "u_press";
static const char* K_PREC  = "u_prec";
static const char* K_DIST  = "u_dist";
static const char* K_24H   = "u_24h";

void loadUnits() {
  if (!u_prefs.begin(NS, /*readOnly*/ true)) return;
  units.temp    = static_cast<TempUnit>(   u_prefs.getUChar(K_TEMP,  static_cast<uint8_t>(wxv::defaults::kDefaults.tempUnit == wxv::defaults::TempUnit::F ? TempUnit::F : TempUnit::C)));
  units.wind    = static_cast<WindUnit>(   u_prefs.getUChar(K_WIND,  static_cast<uint8_t>(WindUnit::MPS)));
  units.press   = static_cast<PressUnit>(  u_prefs.getUChar(K_PRESS, static_cast<uint8_t>(PressUnit::HPA)));
  units.precip  = static_cast<PrecipUnit>( u_prefs.getUChar(K_PREC,  static_cast<uint8_t>(PrecipUnit::MM)));
  units.distance= static_cast<DistanceUnit>(u_prefs.getUChar(K_DIST, static_cast<uint8_t>(DistanceUnit::KM)));
  units.clock24h= u_prefs.getBool(K_24H, wxv::defaults::kDefaults.timeFormat == wxv::defaults::TimeFormat::H24);
  u_prefs.end();
}

void saveUnits() {
  if (!u_prefs.begin(NS, /*readOnly*/ false)) return;
  u_prefs.putUChar(K_TEMP,  static_cast<uint8_t>(units.temp));
  u_prefs.putUChar(K_WIND,  static_cast<uint8_t>(units.wind));
  u_prefs.putUChar(K_PRESS, static_cast<uint8_t>(units.press));
  u_prefs.putUChar(K_PREC,  static_cast<uint8_t>(units.precip));
  u_prefs.putUChar(K_DIST,  static_cast<uint8_t>(units.distance));
  u_prefs.putBool (K_24H,   units.clock24h);
  u_prefs.end();
  applyUnitPreferences();
}

// ---- formatters (cast dp to unsigned int so the right String ctor is chosen) ----
String fmtTemp(double c, uint8_t dp) {
  if (isnan(c)) return F("--");
  return String(dispTemp(c),  (unsigned int)dp) + tempSuffix();
}
String fmtWind(double mps, uint8_t dp) {
  if (isnan(mps)) return F("--");
  return String(dispWind(mps), (unsigned int)dp) + windSuffix();
}
String fmtPress(double hpa, uint8_t dp) {
  if (isnan(hpa)) return F("--");
  return String(dispPress(hpa), (unsigned int)dp) + pressSuffix();
}
String fmtPrecip(double mm, uint8_t dp) {
  if (isnan(mm)) return F("--");
  return String(dispPrecip(mm), (unsigned int)dp) + precipSuffix();
}
String fmtDistanceKm(double km, uint8_t dp) {
  if (isnan(km)) return F("--");
  return String(dispDistanceKm(km), (unsigned int)dp) + distanceSuffix();
}

// ---- toggles & presets ----
void toggleTempUnit() {
  units.temp = (units.temp == TempUnit::C) ? TempUnit::F : TempUnit::C;
  saveUnits();
}
void cycleWindUnit() {
  units.wind = (units.wind == WindUnit::MPS) ? WindUnit::MPH :
               (units.wind == WindUnit::MPH) ? WindUnit::KTS :
               (units.wind == WindUnit::KTS) ? WindUnit::KPH : WindUnit::MPS;
  saveUnits();
}
void togglePressUnit() {
  units.press = (units.press == PressUnit::HPA) ? PressUnit::INHG : PressUnit::HPA;
  saveUnits();
}
void togglePrecipUnit() {
  units.precip = (units.precip == PrecipUnit::MM) ? PrecipUnit::INCH : PrecipUnit::MM;
  saveUnits();
}

void setMetric() {
  units.temp   = TempUnit::C;
  units.wind   = WindUnit::MPS;
  units.press  = PressUnit::HPA;
  units.precip = PrecipUnit::MM;
  units.distance = DistanceUnit::KM;
  saveUnits();
}
void setImperial() {
  units.temp   = TempUnit::F;
  units.wind   = WindUnit::MPH;
  units.press  = PressUnit::INHG;
  units.precip = PrecipUnit::INCH;
  units.distance = DistanceUnit::MILE;
  saveUnits();
}
