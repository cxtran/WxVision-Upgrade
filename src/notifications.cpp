#include "notifications.h"

#include "display.h"

namespace wxv {
namespace notify {

namespace {
constexpr size_t kMaxChars = kNotifyMaxChars;

template <size_t N>
constexpr bool fits(const char (&txt)[N]) {
  return (N - 1U) <= kMaxChars;
}

static_assert(fits("WIFI SCAN"), "notify text too long");
static_assert(fits("WIFI"), "notify text too long");
static_assert(fits("CONNECTING"), "notify text too long");
static_assert(fits("WIFI CONN"), "notify text too long");
static_assert(fits("WIFI OK"), "notify text too long");
static_assert(fits("WIFI FAIL"), "notify text too long");
static_assert(fits("NO NET"), "notify text too long");
static_assert(fits("AUTH FAIL"), "notify text too long");
static_assert(fits("TIMEOUT"), "notify text too long");
static_assert(fits("NTP SYNC"), "notify text too long");
static_assert(fits("NTP OK"), "notify text too long");
static_assert(fits("NTP FAIL"), "notify text too long");
static_assert(fits("SAVE OK"), "notify text too long");
static_assert(fits("SAVE FAIL"), "notify text too long");
static_assert(fits("WX UPDATE"), "notify text too long");
static_assert(fits("WX FAIL"), "notify text too long");
static_assert(fits("SNSR FAIL"), "notify text too long");
static_assert(fits("SYS ERR"), "notify text too long");
static_assert(fits("OTA UPDATE"), "notify text too long");
static_assert(fits("UPGRADE"), "notify text too long");
static_assert(fits("RESTORE"), "notify text too long");
static_assert(fits("FACT RESET"), "notify text too long");
static_assert(fits("RESETTING"), "notify text too long");
}  // namespace

NotificationText textFor(NotifyId id) {
  switch (id) {
    case NotifyId::WifiConnecting: return {"WIFI", "CONNECTING", true};
    case NotifyId::WifiConnected: return {"WIFI CONN", nullptr, false};
    case NotifyId::WifiFail: return {"WIFI FAIL", nullptr, false};
    case NotifyId::WifiNoNet: return {"WIFI", "NO NET", false};
    case NotifyId::WifiAuthFail: return {"WIFI", "AUTH FAIL", false};
    case NotifyId::NtpSync: return {"NTP SYNC", nullptr, true};
    case NotifyId::NtpOk: return {"NTP OK", nullptr, false};
    case NotifyId::NtpFail: return {"NTP FAIL", nullptr, false};
    case NotifyId::SaveOk: return {"SAVE OK", nullptr, false};
    case NotifyId::SaveFail: return {"SAVE FAIL", nullptr, false};
    case NotifyId::WeatherUpdate: return {"WX UPDATE", nullptr, true};
    case NotifyId::WeatherFail: return {"WX FAIL", nullptr, false};
    case NotifyId::SensorFail: return {"SNSR FAIL", nullptr, false};
    case NotifyId::SystemError: return {"SYS ERR", nullptr, false};
    case NotifyId::WifiScan: return {"WIFI SCAN", nullptr, true};
    case NotifyId::WifiTimeout: return {"TIMEOUT", nullptr, false};
    case NotifyId::OtaUpdate: return {"OTA UPDATE", nullptr, false};
    case NotifyId::Upgrading: return {"UPGRADE", nullptr, true};
    case NotifyId::Restoring: return {"RESTORE", nullptr, true};
    case NotifyId::FactoryReset: return {"FACT RESET", nullptr, false};
    case NotifyId::Resetting: return {"RESETTING", nullptr, true};
    default: return {"SYS ERR", nullptr, false};
  }
}

String clampNotifyLine(const String& line) {
  String out = line;
  out.trim();
  out.toUpperCase();
  out.replace(".", "");
  out.replace("!", "");
  out.replace(":", "");
  out.replace("-", " ");
  out.replace("_", " ");
  if (out.length() > kNotifyMaxChars) {
    out = out.substring(0, kNotifyMaxChars);
  }
  return out;
}

static String appendWaitingDots(const String& line) {
  // Keep final line <= 11 chars including "..."
  String base = clampNotifyLine(line);
  const String dots = "...";
  int maxBaseLen = static_cast<int>(kNotifyMaxChars) - static_cast<int>(dots.length());
  if (maxBaseLen < 0) {
    maxBaseLen = 0;
  }
  if (base.length() > static_cast<unsigned>(maxBaseLen)) {
    base = base.substring(0, maxBaseLen);
  }
  base += dots;
  return clampNotifyLine(base);
}

void showNotification(NotifyId id, uint16_t line1Color, uint16_t line2Color, const String& line2Override) {
  if (dma_display == nullptr) {
    return;
  }

  dma_display->setFont(&Font5x7Uts);

  NotificationText msg = textFor(id);
  String l1 = clampNotifyLine(String(msg.line1));
  String l2 = line2Override.length() > 0 ? clampNotifyLine(line2Override)
                                          : (msg.line2 ? clampNotifyLine(String(msg.line2)) : String(""));
  bool twoLine = (l2.length() > 0);
  if (msg.isWaiting) {
    if (twoLine) {
      l2 = appendWaitingDots(l2);
    } else {
      l1 = appendWaitingDots(l1);
    }
  }

  dma_display->fillScreen(0);
  dma_display->setTextColor(line1Color);
  if (!twoLine) {
    // Single-line: center horizontally.
    int textW = getTextWidth(l1.c_str());
    int x = (64 - textW) / 2;
    if (x < 0) x = 0;
    dma_display->setCursor(x, 12);
    dma_display->print(l1);
  } else {
    // Two-line professional staggered layout:
    // line1 left aligned at X=1; line2 right aligned to end at X=63.
    dma_display->setCursor(1, 8);
    dma_display->print(l1);

    int line2W = getTextWidth(l2.c_str());
    int x2 = 63 - line2W;
    if (x2 < 0) x2 = 0;
    dma_display->setTextColor(line2Color);
    dma_display->setCursor(x2, 18);
    dma_display->print(l2);
  }
}

void showNotification(NotifyId id, uint16_t line1Color, const String& line2Override) {
  showNotification(id, line1Color, line1Color, line2Override);
}

void showNotification(NotifyId id) {
  showNotification(id, myWHITE, myWHITE, "");
}

}  // namespace notify
}  // namespace wxv
