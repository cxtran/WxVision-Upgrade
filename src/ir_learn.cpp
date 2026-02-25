#include "ir_learn.h"

#include "display.h"
#include "ir_codes.h"

namespace wxv {
namespace irlearn {

namespace {
constexpr uint32_t kPerKeyTimeoutMs = 12000UL;
constexpr uint32_t kSampleGapMs = 250UL;
constexpr uint32_t kSavedBannerMs = 700UL;
constexpr uint32_t kExitBannerMs = 1500UL;
constexpr uint32_t kTimeoutBannerMs = 900UL;

enum class LearnState : uint8_t { Idle, Collecting, Timeout, Saved, Done, Cancelled };

const IRCodes::WxKey kLearnOrder[] = {
    IRCodes::WxKey::Up,
    IRCodes::WxKey::Down,
    IRCodes::WxKey::Left,
    IRCodes::WxKey::Right,
    IRCodes::WxKey::Ok,
    IRCodes::WxKey::Menu,
    IRCodes::WxKey::Screen,
    IRCodes::WxKey::Theme,
    IRCodes::WxKey::C0,
    IRCodes::WxKey::C1,
};

LearnState s_state = LearnState::Idle;
uint8_t s_stepIndex = 0;
bool s_hasCandidate = false;
IRCodes::IrCode s_candidate{UNKNOWN, 0, 0};
uint8_t s_candidateCount = 0;
uint32_t s_stepStartedAt = 0;
uint32_t s_lastAcceptedAt = 0;
uint32_t s_stateUntil = 0;
bool s_returnToSystemPending = false;

String normalizeLine(const String &in) {
  String out = in;
  out.trim();
  out.toUpperCase();
  out.replace('\n', ' ');
  while (out.indexOf("  ") >= 0) {
    out.replace("  ", " ");
  }
  return out;
}

String fitLineToWidth(const String &line, int maxWidthPx) {
  String out = normalizeLine(line);
  while (out.length() > 0 && getTextWidth(out.c_str()) > maxWidthPx) {
    out.remove(out.length() - 1);
  }
  return out;
}

void drawTwoLine(const String &line1, const String &line2) {
  if (!dma_display) {
    return;
  }
  dma_display->fillScreen(0);
  dma_display->setFont(&Font5x7Uts);
  dma_display->setTextColor(myWHITE);

  // Keep safe horizontal margins on a 64x32 panel to prevent edge clipping.
  String l1 = fitLineToWidth(line1, 60);
  String l2 = fitLineToWidth(line2, 60);

  dma_display->setCursor(1, 8);
  dma_display->print(l1);

  int w2 = getTextWidth(l2.c_str());
  int x2 = 63 - w2;
  if (x2 < 0) {
    x2 = 0;
  }
  dma_display->setCursor(x2, 18);
  dma_display->print(l2);
}

const char *stepLabel(IRCodes::WxKey key) {
  switch (key) {
  case IRCodes::WxKey::Up: return "UP";
  case IRCodes::WxKey::Down: return "DOWN";
  case IRCodes::WxKey::Left: return "LEFT";
  case IRCodes::WxKey::Right: return "RIGHT";
  case IRCodes::WxKey::Ok: return "OK";
  case IRCodes::WxKey::Cancel: return "MENU";
  case IRCodes::WxKey::Menu: return "MENU";
  case IRCodes::WxKey::Screen: return "SCREEN";
  case IRCodes::WxKey::Theme: return "THEME";
  case IRCodes::WxKey::C0: return "C0";
  case IRCodes::WxKey::C1: return "C1";
  default: return "KEY";
  }
}

String pressPrompt(IRCodes::WxKey key) {
  String line = "PRESS ";
  line += stepLabel(key);
  return line;
}

void showPrompt() {
  if (s_stepIndex >= (sizeof(kLearnOrder) / sizeof(kLearnOrder[0]))) {
    return;
  }
  drawTwoLine("LEARN REMOTE", pressPrompt(kLearnOrder[s_stepIndex]));
}

void resetCandidate() {
  s_hasCandidate = false;
  s_candidate = {UNKNOWN, 0, 0};
  s_candidateCount = 0;
  s_stepStartedAt = millis();
}

void beginStep() {
  resetCandidate();
  s_state = LearnState::Collecting;
  showPrompt();
}

bool isValidLearnFrame(const decode_results &res) {
  if (res.repeat) return false;
  if (res.decode_type == UNKNOWN) return false;
  if (res.decode_type == decode_type_t::UNUSED) return false;
  if (res.decode_type == decode_type_t::kLastDecodeType) return false;
  if (res.bits == 0) return false;
  if (res.value == 0) return false;
  return true;
}

bool saveCurrentStep(const decode_results &res) {
  IRCodes::WxKey target = kLearnOrder[s_stepIndex];
  if (!IRCodes::beginLearn(target, kPerKeyTimeoutMs)) {
    return false;
  }
  return IRCodes::processLearnFrame(res);
}

void finishAndReturn(const String &l1, const String &l2, LearnState state) {
  drawTwoLine(l1, l2);
  s_state = state;
  s_stateUntil = millis() + kExitBannerMs;
}

} // namespace

bool start() {
  if (s_state != LearnState::Idle) {
    return false;
  }
  IRCodes::loadLearnedProfile();
  s_stepIndex = 0;
  s_returnToSystemPending = false;
  beginStep();
  return true;
}

void cancel() {
  if (s_state == LearnState::Idle) {
    return;
  }
  finishAndReturn("CANCELLED", "LEARN STOP", LearnState::Cancelled);
}

bool clearLearnedRemote() {
  IRCodes::clearLearnedProfile();
  finishAndReturn("CLEARED", "LEARNED REM", LearnState::Done);
  return true;
}

bool isActive() {
  return s_state != LearnState::Idle;
}

void onDecodedFrame(const decode_results &res) {
  if (s_state != LearnState::Collecting) {
    return;
  }

  // Cancel exits learn mode immediately when a known Cancel/Menu key is pressed.
  IRCodes::WxKey matched = IRCodes::WxKey::Unknown;
  if (IRCodes::matchAnyProfile(res, matched) &&
      (matched == IRCodes::WxKey::Cancel || matched == IRCodes::WxKey::Menu)) {
    cancel();
    return;
  }

  if (!isValidLearnFrame(res)) {
    return;
  }

  uint32_t now = millis();
  if ((now - s_lastAcceptedAt) < kSampleGapMs) {
    return;
  }
  s_lastAcceptedAt = now;

  IRCodes::IrCode sample{res.decode_type, res.value, res.bits};

  if (!s_hasCandidate) {
    s_candidate = sample;
    s_candidateCount = 1;
    s_hasCandidate = true;
    String line2 = String(stepLabel(kLearnOrder[s_stepIndex])) + " 1/3";
    drawTwoLine("RECEIVED", line2);
    return;
  }

  if (s_candidate.proto == sample.proto && s_candidate.bits == sample.bits &&
      s_candidate.value == sample.value) {
    s_candidateCount++;
    String line2 = String(stepLabel(kLearnOrder[s_stepIndex])) + " " + String(s_candidateCount) + "/3";
    drawTwoLine("RECEIVED", line2);
  } else {
    s_candidate = sample;
    s_candidateCount = 1;
    drawTwoLine("FAILED", String("TRY ") + stepLabel(kLearnOrder[s_stepIndex]) + " AGAIN");
    return;
  }

  if (s_candidateCount < 3) {
    return;
  }

  if (!saveCurrentStep(res)) {
    drawTwoLine("FAILED", String("TRY ") + stepLabel(kLearnOrder[s_stepIndex]) + " AGAIN");
    resetCandidate();
    return;
  }

  drawTwoLine("SUCCESS", String(stepLabel(kLearnOrder[s_stepIndex])) + " SAVED");
  s_state = LearnState::Saved;
  s_stateUntil = millis() + kSavedBannerMs;
}

void tick() {
  if (s_state == LearnState::Idle) {
    return;
  }

  uint32_t now = millis();
  if (s_state == LearnState::Collecting) {
    if ((now - s_stepStartedAt) >= kPerKeyTimeoutMs) {
      drawTwoLine("TIMEOUT", pressPrompt(kLearnOrder[s_stepIndex]));
      s_state = LearnState::Timeout;
      s_stateUntil = now + kTimeoutBannerMs;
    }
    return;
  }

  if ((s_state == LearnState::Timeout || s_state == LearnState::Saved ||
       s_state == LearnState::Done || s_state == LearnState::Cancelled) &&
      now < s_stateUntil) {
    return;
  }

  if (s_state == LearnState::Timeout) {
    beginStep();
    return;
  }

  if (s_state == LearnState::Saved) {
    s_stepIndex++;
    if (s_stepIndex >= (sizeof(kLearnOrder) / sizeof(kLearnOrder[0]))) {
      finishAndReturn("DONE", "REMOTE LEARN", LearnState::Done);
      return;
    }
    beginStep();
    return;
  }

  if (s_state == LearnState::Done || s_state == LearnState::Cancelled) {
    s_state = LearnState::Idle;
    s_returnToSystemPending = true;
  }
}

bool consumeReturnToSystemMenuRequest() {
  bool pending = s_returnToSystemPending;
  s_returnToSystemPending = false;
  return pending;
}

} // namespace irlearn
} // namespace wxv
