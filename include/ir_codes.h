#pragma once

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <Preferences.h>

namespace IRCodes {

enum class WxKey : uint8_t {
  Up,
  Down,
  Left,
  Right,
  Ok,
  Cancel,
  Menu,
  Screen,
  Theme,
  C0,
  C1,
  Unknown
};

struct IrCode {
  decode_type_t proto;
  uint64_t value;
  uint16_t bits;
};

struct RemoteProfile {
  const char *name;
  IrCode up;
  IrCode down;
  IrCode left;
  IrCode right;
  IrCode ok;
  IrCode cancel;
  IrCode menu;
  IrCode screen;
  IrCode theme;
  IrCode c0;
  IrCode c1;
};

bool loadLearnedProfile();
void clearLearnedProfile();
bool isLearnedProfileValid();
const char *learnedProfileName();
bool beginLearn(WxKey targetKey, uint32_t timeoutMs);
bool processLearnFrame(const decode_results &res);
bool isLearning();
WxKey learningTarget();
bool cancelLearnIfTimedOut();

constexpr IrCode kDefaultUp{NEC, 0xFFFF30CFULL, 32};
constexpr IrCode kDefaultDown{NEC, 0xFFFF906FULL, 32};
constexpr IrCode kDefaultLeft{NEC, 0xFFFF50AFULL, 32};
constexpr IrCode kDefaultRight{NEC, 0xFFFFE01FULL, 32};
constexpr IrCode kDefaultOk{NEC, 0xFFFF48B7ULL, 32};
constexpr IrCode kDefaultCancel{NEC, 0xFFFF08F7ULL, 32};
constexpr IrCode kDefaultMenu{kDefaultCancel};
constexpr IrCode kDefaultScreen{NEC, 0xFFFFF00FULL, 32};
constexpr IrCode kDefaultTheme{NEC, 0xFFFFB04FULL, 32};
constexpr IrCode kDefaultC0{NEC, 0xFFFF6897ULL, 32};
constexpr IrCode kDefaultC1{NEC, 0xFFFFA857ULL, 32};

constexpr RemoteProfile kRemote_Default{
    "Default (Your Remote)",
    kDefaultUp,
    kDefaultDown,
    kDefaultLeft,
    kDefaultRight,
    kDefaultOk,
    kDefaultCancel,
    kDefaultMenu,
    kDefaultScreen,
    kDefaultTheme,
    kDefaultC0,
    kDefaultC1,
};

constexpr RemoteProfile kRemote_SamsungTV{
    "Samsung TV",
    {SAMSUNG, 0xE0E006F9ULL, 32}, // Up
    {SAMSUNG, 0xE0E08679ULL, 32}, // Down
    {SAMSUNG, 0xE0E0A659ULL, 32}, // Left
    {SAMSUNG, 0xE0E046B9ULL, 32}, // Right
    {SAMSUNG, 0xE0E016E9ULL, 32}, // OK
    {SAMSUNG, 0xE0E01AE5ULL, 32}, // Cancel (Return)
    {SAMSUNG, 0xE0E01AE5ULL, 32}, // Menu alias -> Return
    {SAMSUNG, 0xE0E040BFULL, 32}, // Screen (Power)
    {SAMSUNG, 0xE0E0D02FULL, 32}, // Theme (Tools/Menu)
    {SAMSUNG, 0xE0E08877ULL, 32}, // C0
    {SAMSUNG, 0xE0E020DFULL, 32}, // C1
};

static constexpr const RemoteProfile *kProfiles[] = {
    &kRemote_Default,
    &kRemote_SamsungTV,
};

inline size_t &activeProfileIndexRef() {
  static size_t idx = 0;
  return idx;
}

inline bool &activeIsLearnedRef() {
  static bool learnedActive = false;
  return learnedActive;
}

inline bool &learnedLoadedRef() {
  static bool loaded = false;
  return loaded;
}

inline bool &learningActiveRef() {
  static bool active = false;
  return active;
}

inline WxKey &learningTargetRef() {
  static WxKey key = WxKey::Unknown;
  return key;
}

inline uint32_t &learningStartedAtMsRef() {
  static uint32_t ms = 0;
  return ms;
}

inline uint32_t &learningTimeoutMsRef() {
  static uint32_t ms = 0;
  return ms;
}

inline const char *learnedProfileName() {
  return "Learned (Universal)";
}

inline const IrCode &emptyCode() {
  static const IrCode code = {UNKNOWN, 0, 0};
  return code;
}

inline RemoteProfile &learnedProfileRef() {
  static RemoteProfile profile = {
      learnedProfileName(),
      emptyCode(),
      emptyCode(),
      emptyCode(),
      emptyCode(),
      emptyCode(),
      emptyCode(),
      emptyCode(),
      emptyCode(),
      emptyCode(),
      emptyCode(),
      emptyCode(),
  };
  profile.name = learnedProfileName();
  return profile;
}

inline bool *learnedValidRef() {
  static bool valid[10] = {false, false, false, false, false, false, false, false, false, false};
  return valid;
}

constexpr size_t profileCount() {
  return sizeof(kProfiles) / sizeof(kProfiles[0]);
}

inline int learnedSlotForKey(WxKey key) {
  switch (key) {
  case WxKey::Up:
    return 0;
  case WxKey::Down:
    return 1;
  case WxKey::Left:
    return 2;
  case WxKey::Right:
    return 3;
  case WxKey::Ok:
    return 4;
  case WxKey::Cancel:
  case WxKey::Menu:
    return 5;
  case WxKey::Screen:
    return 6;
  case WxKey::Theme:
    return 7;
  case WxKey::C0:
    return 8;
  case WxKey::C1:
    return 9;
  default:
    return -1;
  }
}

inline const char *learnedKeyStoragePrefix(WxKey key) {
  switch (key) {
  case WxKey::Up:
    return "up";
  case WxKey::Down:
    return "dn";
  case WxKey::Left:
    return "lt";
  case WxKey::Right:
    return "rt";
  case WxKey::Ok:
    return "ok";
  case WxKey::Cancel:
  case WxKey::Menu:
    return "cn";
  case WxKey::Screen:
    return "sc";
  case WxKey::Theme:
    return "th";
  case WxKey::C0:
    return "c0";
  case WxKey::C1:
    return "c1";
  default:
    return "uk";
  }
}

inline IrCode *learnedCodeRefByKey(WxKey key) {
  RemoteProfile &p = learnedProfileRef();
  switch (key) {
  case WxKey::Up:
    return &p.up;
  case WxKey::Down:
    return &p.down;
  case WxKey::Left:
    return &p.left;
  case WxKey::Right:
    return &p.right;
  case WxKey::Ok:
    return &p.ok;
  case WxKey::Cancel:
  case WxKey::Menu:
    return &p.cancel;
  case WxKey::Screen:
    return &p.screen;
  case WxKey::Theme:
    return &p.theme;
  case WxKey::C0:
    return &p.c0;
  case WxKey::C1:
    return &p.c1;
  default:
    return nullptr;
  }
}

inline bool sameCode(const IrCode &expected, decode_type_t p, uint64_t v, uint16_t b) {
  return (expected.proto == p) && (expected.value == v) && (expected.bits == b);
}

inline bool isLearnedProfileValid() {
  if (!learnedLoadedRef()) {
    // Lazy-load on first use.
    loadLearnedProfile();
  }
  bool *valid = learnedValidRef();
  return valid[0] && valid[1] && valid[2] && valid[3] && valid[4] && valid[5];
}

inline bool setActiveProfile(size_t idx) {
  if (idx >= profileCount()) {
    return false;
  }
  activeProfileIndexRef() = idx;
  activeIsLearnedRef() = false;
  return true;
}

inline size_t activeProfileIndex() {
  return activeProfileIndexRef();
}

inline const char *activeProfileName() {
  if (activeIsLearnedRef() && isLearnedProfileValid()) {
    return learnedProfileName();
  }
  return kProfiles[activeProfileIndexRef()]->name;
}

inline const char *keyName(WxKey key) {
  switch (key) {
  case WxKey::Up:
    return "Up";
  case WxKey::Down:
    return "Down";
  case WxKey::Left:
    return "Left";
  case WxKey::Right:
    return "Right";
  case WxKey::Ok:
    return "Ok";
  case WxKey::Cancel:
    return "Cancel";
  case WxKey::Menu:
    return "Menu";
  case WxKey::Screen:
    return "Screen";
  case WxKey::Theme:
    return "Theme";
  case WxKey::C0:
    return "C0";
  case WxKey::C1:
    return "C1";
  default:
    return "Unknown";
  }
}

inline void printCode(decode_type_t proto, uint64_t value, uint16_t bits, bool repeat = false) {
  Serial.printf("IR proto=%d bits=%u value=0x%llX repeat=%d\n",
                static_cast<int>(proto),
                static_cast<unsigned>(bits),
                static_cast<unsigned long long>(value),
                repeat ? 1 : 0);
}

inline bool matchProfile(const RemoteProfile &profile,
                         decode_type_t p,
                         uint64_t v,
                         uint16_t b,
                         bool /*repeat*/,
                         WxKey &outKey) {
  if (sameCode(profile.up, p, v, b)) {
    outKey = WxKey::Up;
    return true;
  }
  if (sameCode(profile.down, p, v, b)) {
    outKey = WxKey::Down;
    return true;
  }
  if (sameCode(profile.left, p, v, b)) {
    outKey = WxKey::Left;
    return true;
  }
  if (sameCode(profile.right, p, v, b)) {
    outKey = WxKey::Right;
    return true;
  }
  if (sameCode(profile.ok, p, v, b)) {
    outKey = WxKey::Ok;
    return true;
  }
  if (sameCode(profile.cancel, p, v, b)) {
    outKey = WxKey::Cancel;
    return true;
  }
  if (sameCode(profile.menu, p, v, b)) {
    outKey = WxKey::Menu;
    return true;
  }
  if (sameCode(profile.screen, p, v, b)) {
    outKey = WxKey::Screen;
    return true;
  }
  if (sameCode(profile.theme, p, v, b)) {
    outKey = WxKey::Theme;
    return true;
  }
  if (sameCode(profile.c0, p, v, b)) {
    outKey = WxKey::C0;
    return true;
  }
  if (sameCode(profile.c1, p, v, b)) {
    outKey = WxKey::C1;
    return true;
  }
  outKey = WxKey::Unknown;
  return false;
}

inline bool loadLearnedProfile() {
  Preferences prefs;
  RemoteProfile &learned = learnedProfileRef();
  bool *valid = learnedValidRef();

  learned.up = emptyCode();
  learned.down = emptyCode();
  learned.left = emptyCode();
  learned.right = emptyCode();
  learned.ok = emptyCode();
  learned.cancel = emptyCode();
  learned.menu = emptyCode();
  learned.screen = emptyCode();
  learned.theme = emptyCode();
  learned.c0 = emptyCode();
  learned.c1 = emptyCode();

  for (int i = 0; i < 10; ++i)
    valid[i] = false;

  if (!prefs.begin("wx_ir", true)) {
    learnedLoadedRef() = true;
    return false;
  }

  const uint16_t ver = prefs.getUShort("ver", 0);
  const bool storedValid = prefs.getBool("valid", false);
  if (ver != 1) {
    prefs.end();
    learnedLoadedRef() = true;
    return false;
  }

  const WxKey keys[] = {
      WxKey::Up, WxKey::Down, WxKey::Left, WxKey::Right, WxKey::Ok,
      WxKey::Cancel, WxKey::Screen, WxKey::Theme, WxKey::C0, WxKey::C1};

  for (size_t i = 0; i < (sizeof(keys) / sizeof(keys[0])); ++i) {
    const WxKey key = keys[i];
    const char *prefix = learnedKeyStoragePrefix(key);
    String pKey = String(prefix) + "_p";
    String bKey = String(prefix) + "_b";
    String hiKey = String(prefix) + "_v_hi";
    String loKey = String(prefix) + "_v_lo";

    const uint8_t proto = prefs.getUChar(pKey.c_str(), static_cast<uint8_t>(UNKNOWN));
    const uint16_t bits = prefs.getUShort(bKey.c_str(), 0);
    const uint32_t hi = prefs.getUInt(hiKey.c_str(), 0);
    const uint32_t lo = prefs.getUInt(loKey.c_str(), 0);
    const uint64_t value = (static_cast<uint64_t>(hi) << 32) | static_cast<uint64_t>(lo);

    if (proto == static_cast<uint8_t>(UNKNOWN) || bits == 0) {
      continue;
    }

    IrCode *slot = learnedCodeRefByKey(key);
    if (!slot) {
      continue;
    }
    slot->proto = static_cast<decode_type_t>(proto);
    slot->bits = bits;
    slot->value = value;
    valid[learnedSlotForKey(key)] = true;
  }

  learned.menu = learned.cancel;
  prefs.end();
  learnedLoadedRef() = true;
  if (storedValid) {
    return true;
  }
  return isLearnedProfileValid();
}

inline void clearLearnedProfile() {
  Preferences prefs;
  if (prefs.begin("wx_ir", false)) {
    prefs.clear();
    prefs.end();
  }
  learnedLoadedRef() = false;
  activeIsLearnedRef() = false;
  learningActiveRef() = false;
  learningTargetRef() = WxKey::Unknown;
  loadLearnedProfile();
}

inline bool isLearning() {
  return learningActiveRef();
}

inline WxKey learningTarget() {
  return learningTargetRef();
}

inline bool cancelLearnIfTimedOut() {
  if (!learningActiveRef()) {
    return false;
  }
  if (learningTimeoutMsRef() == 0) {
    return false;
  }
  const uint32_t now = millis();
  if ((now - learningStartedAtMsRef()) >= learningTimeoutMsRef()) {
    learningActiveRef() = false;
    learningTargetRef() = WxKey::Unknown;
    return true;
  }
  return false;
}

inline bool beginLearn(WxKey targetKey, uint32_t timeoutMs) {
  if (!learnedLoadedRef()) {
    loadLearnedProfile();
  }
  const int slot = learnedSlotForKey(targetKey);
  if (slot < 0) {
    return false;
  }
  learningTargetRef() = targetKey;
  learningTimeoutMsRef() = timeoutMs;
  learningStartedAtMsRef() = millis();
  learningActiveRef() = true;
  return true;
}

inline bool processLearnFrame(const decode_results &res) {
  if (!learningActiveRef()) {
    return false;
  }
  if (cancelLearnIfTimedOut()) {
    return false;
  }
  if (res.repeat) {
    return false;
  }
  if (res.decode_type == UNKNOWN || res.bits == 0) {
    return false;
  }

  const WxKey target = learningTargetRef();
  IrCode *slot = learnedCodeRefByKey(target);
  const int validitySlot = learnedSlotForKey(target);
  if (!slot || validitySlot < 0) {
    learningActiveRef() = false;
    learningTargetRef() = WxKey::Unknown;
    return false;
  }

  slot->proto = res.decode_type;
  slot->bits = res.bits;
  slot->value = res.value;

  Preferences prefs;
  if (prefs.begin("wx_ir", false)) {
    prefs.putUShort("ver", 1);
    const char *prefix = learnedKeyStoragePrefix(target);
    String pKey = String(prefix) + "_p";
    String bKey = String(prefix) + "_b";
    String hiKey = String(prefix) + "_v_hi";
    String loKey = String(prefix) + "_v_lo";

    prefs.putUChar(pKey.c_str(), static_cast<uint8_t>(res.decode_type));
    prefs.putUShort(bKey.c_str(), res.bits);
    prefs.putUInt(hiKey.c_str(), static_cast<uint32_t>(res.value >> 32));
    prefs.putUInt(loKey.c_str(), static_cast<uint32_t>(res.value & 0xFFFFFFFFULL));
    prefs.end();
  }

  learnedValidRef()[validitySlot] = true;
  learnedProfileRef().menu = learnedProfileRef().cancel;
  if (prefs.begin("wx_ir", false)) {
    prefs.putBool("valid", isLearnedProfileValid());
    prefs.end();
  }
  learningActiveRef() = false;
  learningTargetRef() = WxKey::Unknown;
  learnedLoadedRef() = true;
  return true;
}

inline bool match(decode_type_t p, uint64_t v, uint16_t b, bool repeat, WxKey &outKey) {
  if (!learnedLoadedRef()) {
    loadLearnedProfile();
  }

  if (activeIsLearnedRef() && isLearnedProfileValid()) {
    if (matchProfile(learnedProfileRef(), p, v, b, repeat, outKey)) {
      return true;
    }
  } else {
    if (matchProfile(*kProfiles[activeProfileIndexRef()], p, v, b, repeat, outKey)) {
      return true;
    }
  }

  outKey = WxKey::Unknown;
  return false;
}

inline bool matchAnyProfile(decode_type_t p, uint64_t v, uint16_t b, bool repeat, WxKey &outKey) {
  if (!learnedLoadedRef()) {
    loadLearnedProfile();
  }

  if (match(p, v, b, repeat, outKey)) {
    return true;
  }

  for (size_t i = 0; i < profileCount(); ++i) {
    if (!activeIsLearnedRef() && i == activeProfileIndexRef()) {
      continue;
    }
    if (matchProfile(*kProfiles[i], p, v, b, repeat, outKey)) {
      activeProfileIndexRef() = i;
      activeIsLearnedRef() = false;
      return true;
    }
  }

  if (isLearnedProfileValid() && matchProfile(learnedProfileRef(), p, v, b, repeat, outKey)) {
    activeIsLearnedRef() = true;
    return true;
  }

  outKey = WxKey::Unknown;
  return false;
}

inline bool match(const decode_results &r, WxKey &outKey) {
  return match(r.decode_type, r.value, r.bits, r.repeat, outKey);
}

inline bool matchAnyProfile(const decode_results &r, WxKey &outKey) {
  return matchAnyProfile(r.decode_type, r.value, r.bits, r.repeat, outKey);
}

inline uint32_t legacyCodeForKey(WxKey key) {
  switch (key) {
  case WxKey::Up:
    return static_cast<uint32_t>(kDefaultUp.value);
  case WxKey::Down:
    return static_cast<uint32_t>(kDefaultDown.value);
  case WxKey::Left:
    return static_cast<uint32_t>(kDefaultLeft.value);
  case WxKey::Right:
    return static_cast<uint32_t>(kDefaultRight.value);
  case WxKey::Ok:
    return static_cast<uint32_t>(kDefaultOk.value);
  case WxKey::Cancel:
  case WxKey::Menu:
    return static_cast<uint32_t>(kDefaultCancel.value);
  case WxKey::Screen:
    return static_cast<uint32_t>(kDefaultScreen.value);
  case WxKey::Theme:
    return static_cast<uint32_t>(kDefaultTheme.value);
  case WxKey::C0:
    return static_cast<uint32_t>(kDefaultC0.value);
  case WxKey::C1:
    return static_cast<uint32_t>(kDefaultC1.value);
  default:
    return 0;
  }
}

inline WxKey mapLegacyCodeToKey(uint32_t legacy) {
  if (legacy == static_cast<uint32_t>(kDefaultUp.value))
    return WxKey::Up;
  if (legacy == static_cast<uint32_t>(kDefaultDown.value))
    return WxKey::Down;
  if (legacy == static_cast<uint32_t>(kDefaultLeft.value))
    return WxKey::Left;
  if (legacy == static_cast<uint32_t>(kDefaultRight.value))
    return WxKey::Right;
  if (legacy == static_cast<uint32_t>(kDefaultOk.value))
    return WxKey::Ok;
  if (legacy == static_cast<uint32_t>(kDefaultCancel.value))
    return WxKey::Cancel;
  if (legacy == static_cast<uint32_t>(kDefaultScreen.value))
    return WxKey::Screen;
  if (legacy == static_cast<uint32_t>(kDefaultTheme.value))
    return WxKey::Theme;
  if (legacy == static_cast<uint32_t>(kDefaultC0.value))
    return WxKey::C0;
  if (legacy == static_cast<uint32_t>(kDefaultC1.value))
    return WxKey::C1;
  return WxKey::Unknown;
}

} // namespace IRCodes

#define PRINT_IR_CODE(code) IRCodes::printCode(NEC, static_cast<uint64_t>(code), 32, false)

static constexpr uint32_t IR_UP = static_cast<uint32_t>(IRCodes::kDefaultUp.value);
static constexpr uint32_t IR_DOWN = static_cast<uint32_t>(IRCodes::kDefaultDown.value);
static constexpr uint32_t IR_LEFT = static_cast<uint32_t>(IRCodes::kDefaultLeft.value);
static constexpr uint32_t IR_RIGHT = static_cast<uint32_t>(IRCodes::kDefaultRight.value);
static constexpr uint32_t IR_OK = static_cast<uint32_t>(IRCodes::kDefaultOk.value);
static constexpr uint32_t IR_CANCEL = static_cast<uint32_t>(IRCodes::kDefaultCancel.value);
static constexpr uint32_t IR_MENU = static_cast<uint32_t>(IRCodes::kDefaultMenu.value);
static constexpr uint32_t IR_SCREEN = static_cast<uint32_t>(IRCodes::kDefaultScreen.value);
static constexpr uint32_t IR_THEME = static_cast<uint32_t>(IRCodes::kDefaultTheme.value);
static constexpr uint32_t IR_0 = static_cast<uint32_t>(IRCodes::kDefaultC0.value);
static constexpr uint32_t IR_1 = static_cast<uint32_t>(IRCodes::kDefaultC1.value);
