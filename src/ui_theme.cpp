#include "ui_theme.h"

#include "display.h"

namespace ui_theme
{
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return dma_display ? dma_display->color565(r, g, b) : 0;
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
    return (theme == 1) ? rgb(18, 18, 38) : rgb(18, 52, 96);
}

uint16_t noaaHeaderFgFallback(int theme)
{
    return (theme == 1) ? rgb(190, 190, 230) : rgb(230, 245, 255);
}

uint16_t noaaTitleArea() { return rgb(150, 220, 255); }
uint16_t noaaTitleWhat() { return rgb(255, 215, 120); }
uint16_t noaaTitleDoThis() { return rgb(120, 245, 170); }
uint16_t noaaTitleInfo() { return rgb(130, 220, 255); }

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
