#include "ui_theme.h"

#include "display.h"
#include "settings.h"

namespace ui_theme
{
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return dma_display ? dma_display->color565(r, g, b) : 0;
}

bool isNightTheme()
{
    return theme == 1;
}

static uint16_t compose565(uint8_t r, uint8_t g, uint8_t b)
{
    if (dma_display)
        return dma_display->color565(r, g, b);
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t applyGraphicColor(uint16_t color)
{
    if (!isNightTheme() || color == 0)
        return color;

    const uint8_t r = static_cast<uint8_t>(((color >> 11) & 0x1F) * 255 / 31);
    const uint8_t g = static_cast<uint8_t>(((color >> 5) & 0x3F) * 255 / 63);
    const uint8_t b = static_cast<uint8_t>((color & 0x1F) * 255 / 31);
    const int luma = (r * 30 + g * 59 + b * 11) / 100;

    // Keep one neutral matrix tone while preserving relative contrast between bright/dark art.
    constexpr uint8_t kToneR = 172;
    constexpr uint8_t kToneG = 186;
    constexpr uint8_t kToneB = 214;
    const float level = 0.16f + (static_cast<float>(luma) / 255.0f) * 0.48f;

    const uint8_t nr = static_cast<uint8_t>(kToneR * level + 0.5f);
    const uint8_t ng = static_cast<uint8_t>(kToneG * level + 0.5f);
    const uint8_t nb = static_cast<uint8_t>(kToneB * level + 0.5f);
    return compose565(nr, ng, nb);
}

void applyGraphicThemeToBuffer(uint16_t *buffer, size_t count)
{
    if (!isNightTheme() || buffer == nullptr)
        return;

    for (size_t i = 0; i < count; ++i)
        buffer[i] = applyGraphicColor(buffer[i]);
}

void drawBitmapThemed(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t color)
{
    if (!dma_display || bitmap == nullptr)
        return;
    dma_display->drawBitmap(x, y, bitmap, w, h, applyGraphicColor(color));
}

uint16_t monoHeaderBg() { return rgb(20, 20, 40); }
uint16_t monoHeaderFg() { return rgb(60, 60, 120); }
uint16_t monoUnderline() { return rgb(30, 30, 70); }
uint16_t monoBodyText() { return rgb(120, 160, 255); }

uint16_t wifiHeaderBg() { return rgb(0, 40, 80); }
uint16_t wifiHeaderFg() { return rgb(0, 255, 255); }
uint16_t wifiTextNormal() { return rgb(255, 255, 255); }
uint16_t wifiTextSelected() { return rgb(255, 255, 0); }
uint16_t wifiErrorText() { return rgb(255, 80, 80); }

uint16_t noaaHeaderBg(int theme)
{
    return (theme == 1) ? rgb(56, 10, 10) : rgb(92, 12, 12);
}

uint16_t noaaHeaderFgFallback(int theme)
{
    return (theme == 1) ? rgb(255, 196, 120) : rgb(255, 222, 140);
}

uint16_t noaaTitleArea() { return rgb(255, 210, 120); }
uint16_t noaaTitleWhat() { return rgb(255, 170, 90); }
uint16_t noaaTitleDoThis() { return rgb(255, 235, 120); }
uint16_t noaaTitleInfo() { return rgb(255, 190, 105); }

uint16_t noaaSeverityExtreme() { return rgb(255, 70, 70); }
uint16_t noaaSeveritySevere() { return rgb(255, 155, 40); }
uint16_t noaaSeverityModerate() { return rgb(255, 220, 95); }
uint16_t noaaSeverityMinor() { return rgb(120, 220, 255); }
uint16_t noaaSeverityUnknown() { return rgb(190, 190, 205); }

uint16_t infoModalHeaderFg() { return rgb(255, 255, 255); }
uint16_t infoModalHeaderBg() { return rgb(0, 0, 120); }
uint16_t infoModalUnselXBg() { return rgb(110, 80, 133); }
uint16_t infoModalSelXBg() { return rgb(255, 0, 0); }
uint16_t infoModalXColor() { return rgb(255, 255, 255); }
uint16_t infoModalUnderline() { return rgb(255, 255, 255); }
uint16_t infoModalSel() { return rgb(255, 255, 64); }
uint16_t infoModalUnsel() { return rgb(0, 255, 255); }
uint16_t infoModalBtnBg() { return rgb(20, 60, 120); }
uint16_t infoModalBtnSelBg() { return rgb(255, 130, 0); }
uint16_t infoModalEdit() { return rgb(255, 255, 0); }

uint16_t infoScreenHeaderFg() { return rgb(156, 255, 91); }
uint16_t infoScreenHeaderBg() { return rgb(0, 20, 60); }
uint16_t infoLabelMono() { return rgb(220, 220, 120); }
uint16_t infoValueMono() { return rgb(120, 160, 255); }
uint16_t infoLabelDimMono() { return rgb(70, 70, 110); }
uint16_t infoLabelDay() { return rgb(255, 240, 140); }
uint16_t infoValueDay() { return rgb(120, 200, 255); }
uint16_t infoDefaultLineDay() { return rgb(230, 230, 230); }
uint16_t infoUnderlineDay() { return rgb(12, 40, 80); }
uint16_t infoValueHighlightMono() { return rgb(120, 120, 200); }
uint16_t infoValueHighlightDay() { return rgb(255, 255, 0); }
uint16_t infoLabelHighlightMono() { return rgb(140, 140, 220); }
uint16_t infoLabelHighlightDay() { return rgb(255, 255, 180); }

uint16_t keyboardTitleBg() { return rgb(0, 0, 120); }
uint16_t keyboardTitleFg() { return rgb(255, 255, 255); }
uint16_t keyboardSeparator() { return rgb(20, 30, 80); }
uint16_t keyboardKeyRowBg() { return rgb(18, 18, 40); }
uint16_t keyboardBufferActive() { return rgb(255, 220, 80); }
uint16_t keyboardBufferInactive() { return rgb(64, 128, 255); }
uint16_t keyboardCursor() { return rgb(255, 255, 64); }
uint16_t keyboardSelectedKeyBg() { return rgb(0, 128, 255); }
uint16_t keyboardSelectedKeyFg() { return rgb(255, 255, 255); }
uint16_t keyboardKeyFg() { return rgb(180, 180, 180); }
uint16_t keyboardBtnCancelBg() { return rgb(120, 0, 0); }
uint16_t keyboardBtnCancelBgSel() { return rgb(220, 40, 40); }
uint16_t keyboardBtnCancelBorder() { return rgb(255, 64, 64); }
uint16_t keyboardBtnCancelFg() { return rgb(255, 255, 255); }
uint16_t keyboardBtnBg() { return rgb(30, 40, 60); }
uint16_t keyboardBtnSelBg() { return rgb(255, 180, 0); }
uint16_t keyboardBtnModeSelBg() { return rgb(180, 255, 0); }
uint16_t keyboardBtnBorder() { return rgb(255, 255, 0); }
uint16_t keyboardBtnFg() { return rgb(255, 255, 255); }
uint16_t keyboardBtnSelFg() { return rgb(0, 0, 0); }

uint16_t worldHeaderNight() { return rgb(120, 120, 180); }
uint16_t worldHeaderDay() { return rgb(180, 220, 255); }
uint16_t noaaLinePrimary() { return rgb(235, 245, 255); }
uint16_t noaaLineSecondary() { return rgb(120, 220, 255); }
uint16_t noaaLineWhat() { return rgb(255, 225, 140); }
uint16_t noaaLineInfo() { return rgb(175, 235, 255); }
} // namespace ui_theme
