#pragma once

#include <stdint.h>

namespace ui_theme
{
struct Layout
{
    static constexpr int kTitleBarY = 0;
    static constexpr int kTitleBarH = 8;
    static constexpr int kTitleTextX = 1;
    static constexpr int kBodyY = 9;
    static constexpr int kBodyLineH = 8;
    static constexpr int kBodyVisibleLines = 3;
    static constexpr int kBodyVisibleH = 23;
    static constexpr int kWrapCharsTiny = 11;
};

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b);

uint16_t monoHeaderBg();
uint16_t monoHeaderFg();
uint16_t monoUnderline();
uint16_t monoBodyText();

uint16_t wifiHeaderBg();
uint16_t wifiHeaderFg();
uint16_t wifiTextNormal();
uint16_t wifiTextSelected();
uint16_t wifiErrorText();

uint16_t noaaHeaderBg(int theme);
uint16_t noaaHeaderFgFallback(int theme);
uint16_t noaaTitleArea();
uint16_t noaaTitleWhat();
uint16_t noaaTitleDoThis();
uint16_t noaaTitleInfo();
uint16_t noaaSeverityExtreme();
uint16_t noaaSeveritySevere();
uint16_t noaaSeverityModerate();
uint16_t noaaSeverityMinor();
uint16_t noaaSeverityUnknown();

uint16_t infoModalHeaderFg();
uint16_t infoModalHeaderBg();
uint16_t infoModalUnselXBg();
uint16_t infoModalSelXBg();
uint16_t infoModalXColor();
uint16_t infoModalUnderline();
uint16_t infoModalSel();
uint16_t infoModalUnsel();
uint16_t infoModalBtnBg();
uint16_t infoModalBtnSelBg();
uint16_t infoModalEdit();

uint16_t infoScreenHeaderFg();
uint16_t infoScreenHeaderBg();
uint16_t infoLabelMono();
uint16_t infoValueMono();
uint16_t infoLabelDimMono();
uint16_t infoLabelDay();
uint16_t infoValueDay();
uint16_t infoDefaultLineDay();
uint16_t infoUnderlineDay();
uint16_t infoValueHighlightMono();
uint16_t infoValueHighlightDay();
uint16_t infoLabelHighlightMono();
uint16_t infoLabelHighlightDay();

uint16_t keyboardTitleBg();
uint16_t keyboardTitleFg();
uint16_t keyboardSeparator();
uint16_t keyboardKeyRowBg();
uint16_t keyboardBufferActive();
uint16_t keyboardBufferInactive();
uint16_t keyboardCursor();
uint16_t keyboardSelectedKeyBg();
uint16_t keyboardSelectedKeyFg();
uint16_t keyboardKeyFg();
uint16_t keyboardBtnCancelBg();
uint16_t keyboardBtnCancelBgSel();
uint16_t keyboardBtnCancelBorder();
uint16_t keyboardBtnCancelFg();
uint16_t keyboardBtnBg();
uint16_t keyboardBtnSelBg();
uint16_t keyboardBtnModeSelBg();
uint16_t keyboardBtnBorder();
uint16_t keyboardBtnFg();
uint16_t keyboardBtnSelFg();

uint16_t worldHeaderNight();
uint16_t worldHeaderDay();
uint16_t noaaLinePrimary();
uint16_t noaaLineSecondary();
uint16_t noaaLineWhat();
uint16_t noaaLineInfo();
} // namespace ui_theme
