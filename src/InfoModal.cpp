#include "InfoModal.h"
#include "display.h"
#include "menu.h"
#include "settings.h"
#include "alarm.h"
#include "keyboard.h"
#include "buzzer.h"
#include "datetimesettings.h"
#include <RTClib.h>
#include <cstring>
#include <vector>
#include <ctype.h>
#include <math.h>
#include "system.h"
#include "default_values.h"
#include "ui_theme.h"
#include "noaa.h"
#include <limits.h>

extern InfoModal alarmModal;
extern InfoModal setupPromptModal;
extern void handleAlarmSlotChangedInModal();

extern bool autoBrightness;
extern int scrollLevel;
extern void saveDisplaySettings();
extern MenuStack menuStack;
extern int theme;
extern bool rtcReady;
extern RTC_DS3231 rtc;
extern bool reset_Time_and_Date_Display;
extern int dtTimezoneIndex;
extern int dtManualOffset;
extern int dtAutoDst;
extern int dtYear, dtMonth, dtDay, dtHour, dtMinute, dtSecond;
extern void getTimeFromRTC();
extern void setSystemTimeFromDateTime(const DateTime &dt);

// --- Static trampoline for keyboard -> InfoModal::setTextValue ---
static InfoModal *s_modalForText = nullptr;
static int s_textIdxForText = -1;


namespace {
struct ModalPalette {
    uint16_t headerBg;
    uint16_t headerFg;
    uint16_t underline;
    uint16_t closeBg;
    uint16_t closeSelBg;
    uint16_t closeFg;
    uint16_t lineUnselected;
    uint16_t lineSelected;
    uint16_t lineEditing;
    uint16_t buttonBg;
    uint16_t buttonSelBg;
    uint16_t buttonFg;
    uint16_t buttonSelFg;
};

ModalPalette makePalette() {
    ModalPalette p{};
    if (theme == 1) {
        p.headerBg = ui_theme::monoHeaderBg();
        p.headerFg = ui_theme::monoHeaderFg();
        p.underline = ui_theme::monoUnderline();
        p.closeBg = ui_theme::monoHeaderBg();
        p.closeSelBg = ui_theme::rgb(90, 90, 150);
        p.closeFg = ui_theme::rgb(180, 180, 220);
        p.lineUnselected = ui_theme::rgb(40, 40, 90);
        p.lineSelected = ui_theme::rgb(90, 90, 150);
        p.lineEditing = ui_theme::rgb(140, 140, 200);
        p.buttonBg = ui_theme::rgb(25, 25, 60);
        p.buttonSelBg = ui_theme::rgb(90, 90, 150);
        p.buttonFg = ui_theme::monoHeaderFg();
        p.buttonSelFg = ui_theme::monoHeaderBg();
    } else {
        p.headerBg = INFOMODAL_HEADERBG;
        p.headerFg = INFOMODAL_GREEN;
        p.underline = INFOMODAL_ULINE;
        p.closeBg = INFOMODAL_UNSELXBG;
        p.closeSelBg = INFOMODAL_SELXBG;
        p.closeFg = INFOMODAL_XCOLOR;
        p.lineUnselected = INFOMODAL_UNSEL;
        p.lineSelected = INFOMODAL_SEL;
        p.lineEditing = INFOMODAL_EDIT;
        p.buttonBg = INFOMODAL_BTN_BG;
        p.buttonSelBg = INFOMODAL_BTN_SELBG;
        p.buttonFg = INFOMODAL_XCOLOR;
        p.buttonSelFg = INFOMODAL_HEADERBG;
    }
    return p;
}

constexpr int kChooserArrowWidth = 6;

ModalPalette currentPalette;

static void drawForwardArrow(int x, int y, uint16_t color)
{
    // Mirror of drawBackArrow: triangle tip on the right
    dma_display->drawLine(x + 4, y + 3, x + 2, y + 1, color);
    dma_display->drawLine(x + 4, y + 3, x + 2, y + 5, color);
    dma_display->drawLine(x, y + 3, x + 4, y + 3, color);
}
}


static void keyboardTextDoneShim(const char *result)
{
    if (s_modalForText)
    {
        s_modalForText->handleTextDone(s_textIdxForText, result);
        s_modalForText = nullptr;
    }
    s_textIdxForText = -1;
}

InfoModal::InfoModal(const String &title)
    : modalTitle(title)
{
    memset(intRefs, 0, sizeof(intRefs));
    memset(numberFieldConfigs, 0, sizeof(numberFieldConfigs));
    memset(chooserRefs, 0, sizeof(chooserRefs));
    memset(chooserOptions, 0, sizeof(chooserOptions));
    memset(chooserOptionCounts, 0, sizeof(chooserOptionCounts));
    memset(chooserFieldIndices, -1, sizeof(chooserFieldIndices));
    memset(numberFieldIndices, -1, sizeof(numberFieldIndices));
    memset(textRefs, 0, sizeof(textRefs));
    memset(textSizes, 0, sizeof(textSizes));
    memset(textFieldIndices, -1, sizeof(textFieldIndices));
    memset(btnLabels, 0, sizeof(btnLabels));
    btnCount = 0;
    btnSel = 0;
    inButtonBar = false;
    clearNumberFieldConfigs();
}

void InfoModal::setLines(const String _lines[], const InfoFieldType _types[], int count)
{
    lineCount = (count > MAX_LINES) ? MAX_LINES : count;

    int chooserPos = 0;
    int numberPos = 0;
    int textPos = 0;

    for (int i = 0; i < lineCount; ++i)
    {
        lines[i] = _lines[i];
        fieldTypes[i] = _types[i];

        // Reset
        intRefs[i] = nullptr;
        numberFieldConfigs[i] = NumberFieldConfig{};
        chooserRefs[i] = nullptr;
        chooserOptions[i] = nullptr;
        chooserOptionCounts[i] = 0;
        textRefs[i] = nullptr;
        textSizes[i] = 0;

        // Field index mapping (fixes crash!)
        if (fieldTypes[i] == InfoChooser)
        {
            chooserFieldIndices[i] = chooserPos++;
            numberFieldIndices[i] = -1;
            textFieldIndices[i] = -1;
        }
        else if (fieldTypes[i] == InfoNumber)
        {
            numberFieldIndices[i] = numberPos++;
            chooserFieldIndices[i] = -1;
            textFieldIndices[i] = -1;
        }
        else if (fieldTypes[i] == InfoText)
        {
            textFieldIndices[i] = textPos++;
            chooserFieldIndices[i] = -1;
            numberFieldIndices[i] = -1;
        }
        else
        {
            // Label or Button
            chooserFieldIndices[i] = -1;
            numberFieldIndices[i] = -1;
            textFieldIndices[i] = -1;
        }
    }
}

void InfoModal::setValueRefs(
    int *intRefs[], int refCount,
    int *chooserRefs[], int chooserCount,
    const char *const *chooserOptions[], const int chooserOptionCounts[],
    char *textRefs[], int textRefCount, int textSizes[])
{
    //   Serial.println("[InfoModal] setValueRefs()");

    // --- Number references ---
    int numIndex = 0;
    for (int i = 0; i < lineCount && numIndex < refCount; ++i)
    {
        if (fieldTypes[i] == InfoNumber)
        {
            this->intRefs[numIndex] = intRefs[numIndex];
            numberFieldIndices[i] = numIndex;
            //         Serial.printf("  [Number] Line %d -> intRefs[%d] = %p (val = %d)\n", i, numIndex, intRefs[numIndex], *intRefs[numIndex]);
            ++numIndex;
        }
        else
        {
            numberFieldIndices[i] = -1;
        }
    }
    for (int i = numIndex; i < MAX_LINES; ++i)
    {
        this->intRefs[i] = nullptr;
    }

    // --- Chooser references and options ---
    int chooserIndex = 0;
    for (int i = 0; i < lineCount && chooserIndex < chooserCount; ++i)
    {
        if (fieldTypes[i] == InfoChooser)
        {
            this->chooserRefs[chooserIndex] = chooserRefs[chooserIndex];
            this->chooserOptions[chooserIndex] = chooserOptions[chooserIndex];
            this->chooserOptionCounts[chooserIndex] = chooserOptionCounts[chooserIndex];
            chooserFieldIndices[i] = chooserIndex;
            //         Serial.printf("  [Chooser] Line %d -> chooserRefs[%d] = %p (val = %d)\n", i, chooserIndex, chooserRefs[chooserIndex], *chooserRefs[chooserIndex]);
            ++chooserIndex;
        }
        else
        {
            chooserFieldIndices[i] = -1;
        }
    }
    for (int i = chooserIndex; i < MAX_LINES; ++i)
    {
        this->chooserRefs[i] = nullptr;
        this->chooserOptions[i] = nullptr;
        this->chooserOptionCounts[i] = 0;
    }

    // --- Text references ---
    int textIndex = 0;
    for (int i = 0; i < lineCount && textIndex < textRefCount; ++i)
    {
        if (fieldTypes[i] == InfoText)
        {
            this->textRefs[textIndex] = textRefs[textIndex];
            this->textSizes[textIndex] = (textSizes && textSizes[textIndex] > 0) ? textSizes[textIndex] : 64;
            textFieldIndices[i] = textIndex;
            Serial.printf("  [Text] Line %d -> textRefs[%d] = \"%s\"\n", i, textIndex, textRefs[textIndex]);
            ++textIndex;
        }
        else
        {
            textFieldIndices[i] = -1;
        }
    }
    for (int i = textIndex; i < MAX_LINES; ++i)
    {
        this->textRefs[i] = nullptr;
        this->textSizes[i] = 0;
    }

    textFieldCount = textIndex;
    //  Serial.printf("[InfoModal] Setup complete: %d number, %d chooser, %d text fields\n", numIndex, chooserIndex, textFieldCount);
}

void InfoModal::setButtons(const String btns[], int count)
{
    btnCount = count > 0 ? ((count > MAXROWS) ? MAXROWS : count) : 0;
    for (int i = 0; i < btnCount; ++i)
        btnLabels[i] = btns[i];
    btnSel = 0;
    inButtonBar = false;
}

void InfoModal::setCallback(const std::function<void(bool, int)> &cb) { callback = cb; }

void InfoModal::clearNumberFieldConfigs()
{
    for (int i = 0; i < MAX_LINES; ++i)
    {
        numberFieldConfigs[i] = NumberFieldConfig{};
    }
}

void InfoModal::setNumberFieldConfig(int lineIndex, const NumberFieldConfig &config)
{
    if (lineIndex < 0 || lineIndex >= MAX_LINES)
        return;
    numberFieldConfigs[lineIndex] = config;
}

void InfoModal::setShowNumberArrows(bool enable)
{
    showNumberArrows = enable;
}

void InfoModal::setShowChooserArrows(bool enable)
{
    showChooserArrows = enable;
}

void InfoModal::setShowForwardArrow(bool enable)
{
    showForwardArrow = enable;
}

void InfoModal::setForwardArrowOnlyIndex(int idx)
{
    forwardArrowOnlyIndex = idx;
}

void InfoModal::setKeepOpenOnSelect(bool enable)
{
    keepOpenOnSelect = enable;
}

void InfoModal::show()
{
    selIndex = 0;
    scrollY = 0;
    scrollOffset = 0;
    atClose = false;
    lastScrollTime = millis();
    firstScroll = true;
    lastSelIndex = -1;
    inEdit = false;
    editIndex = -1;
    btnSel = 0;
    inButtonBar = (this == &setupPromptModal && btnCount > 0);
    resetHeldAdjustState();
    active = true;
    draw();
}

void InfoModal::redraw()
{
    if (!active) return;
    draw();
}

void InfoModal::hide()
{
    active = false;
    resetHeldAdjustState();
}

bool InfoModal::isActive() const { return active; }

void InfoModal::drawHeader()
{
    const ModalPalette &palette = currentPalette;
    const int headerHeight = CHARH;
    dma_display->fillRect(0, 0, SCREEN_WIDTH, headerHeight, palette.headerBg);

    dma_display->setTextColor(palette.headerFg);
    dma_display->setCursor(1, 0);
    dma_display->print(modalTitle);

    int xWidth = 7;
    int xX = SCREEN_WIDTH - xWidth;
    int xY = -1;
    if (xY < 0)
        xY = 0;

    uint16_t xBgColor = atClose ? palette.closeSelBg : palette.closeBg;
    uint16_t xFgColor = palette.closeFg;

    dma_display->fillRect(xX, 0, xWidth, headerHeight, xBgColor);
    dma_display->setTextColor(xFgColor);

    /*
    dma_display->setCursor(xX + 1, xY );
    dma_display->drawLine(xX + 1, xY + 3, xX + 3, xY + 1, xFgColor);
    dma_display->drawLine(xX + 1, xY + 3, xX + 3, xY + 5, xFgColor);
    dma_display->drawLine(xX + 1, xY + 3, xX + 5, xY + 3, xFgColor);
    */
    drawBackArrow(xX, xY, xFgColor);


   // dma_display->print("<");

    dma_display->drawFastHLine(0, headerHeight - 1, SCREEN_WIDTH, palette.underline);
}

String InfoModal::getChooserLabel(int idx)
{
    int cidx = chooserFieldIndices[idx];
    if (cidx < 0 || !chooserRefs[cidx] || !chooserOptions[cidx])
        return "?";

    int v = *(chooserRefs[cidx]);
    if (v < 0 || v >= chooserOptionCounts[cidx])
        return "?";

    return chooserOptions[cidx][v];
}


static String formatUtcOffsetMinutes(int minutes)
{
    char buf[16];
    char sign = (minutes >= 0) ? '+' : '-';
    int absMinutes = abs(minutes);
    int hours = absMinutes / 60;
    int mins = absMinutes % 60;
    snprintf(buf, sizeof(buf), "UTC%c%02d:%02d", sign, hours, mins);
    return String(buf);
}

static String formatScheduleTime(int minutes)
{
    int normalized = normalizeThemeScheduleMinutes(minutes);
    int hours = normalized / 60;
    int mins = normalized % 60;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", hours, mins);
    return String(buf);
}

void InfoModal::draw()
{
    if (!active)
        return;

    currentPalette = makePalette();
    const ModalPalette &palette = currentPalette;

    dma_display->setFont(&Font5x7Uts);
    dma_display->fillScreen(0);
    drawHeader();

    int visibleRows = (btnCount > 0) ? DATA_ROWS : DATA_ROWS_FULL;
    int dataCount = lineCount;
    int scrollLimit = (dataCount > visibleRows) ? (dataCount - visibleRows) : 0;

    // Adjust scrollY based on selIndex, but if atClose (X selected), no line is selected
    if (!atClose)
    {
        if (selIndex < scrollY)
            scrollY = selIndex;
        if (selIndex >= scrollY + visibleRows)
            scrollY = selIndex - visibleRows + 1;
    }
    if (scrollY > scrollLimit)
        scrollY = scrollLimit;

    for (int i = 0; i < visibleRows; ++i)
    {
        int idx = scrollY + i;
        if (idx >= dataCount)
            break;

        // Determine if this line is selected
        // When atClose==true, no line is selected, so isSelected=false
        bool isSelected = (!atClose) && (selIndex == idx) && (!inButtonBar);
        bool isEditing = (inEdit && editIndex == idx);

        dma_display->setTextColor(isEditing ? palette.lineEditing : palette.lineSelected);
        String s = lines[idx];

        if (fieldTypes[idx] == InfoNumber)
        {
            int nidx = numberFieldIndices[idx];
            if (nidx >= 0 && nidx < MAX_LINES && intRefs[nidx])
            {
                int val = *(intRefs[nidx]);
                if (lines[idx] == "TimeZone")
                {
                    s += ": " + formatUtcOffsetMinutes(val);
                }
                else if (lines[idx] == "Day Theme Start" || lines[idx] == "Night Theme Start")
                {
                    s += ": " + formatScheduleTime(val);
                }
                else
                {
                    if (lines[idx].startsWith("Temp Offset") || lines[idx].startsWith("Temp Alert"))
                    {
                        s += ": " + String(static_cast<float>(val) / 10.0f, 1);
                    }
                    else
                    {
                        s += ": " + String(val);
                    }
                }
                //       Serial.printf("intRefs[%d] = %d\n", nidx, val);
            }
            else
            {
                s += ": ?";
                //      Serial.printf("Invalid numberRef (nidx=%d)\n", nidx);
            }
        }
        else if (fieldTypes[idx] == InfoChooser)
        {
            int cidx = chooserFieldIndices[idx];
            if (cidx >= 0 && cidx < MAX_LINES &&
                chooserRefs[cidx] && chooserOptions[cidx] &&
                chooserOptionCounts[cidx] > 0)
            {
                s += ": " + getChooserLabel(idx);
                //        Serial.printf("chooserRefs[%d] = %d -> \"%s\"\n", cidx, *chooserRefs[cidx], getChooserLabel(idx).c_str());
            }
            else
            {
                s += ": ?";
                //          Serial.printf("Invalid chooserRef (cidx=%d)\n", cidx);
            }
        }
        else if (fieldTypes[idx] == InfoText)
        {
            int tidx = textFieldIndices[idx];
            if (tidx >= 0 && tidx < MAX_LINES && textRefs[tidx])
            {
                s += ": ";
                s += textRefs[tidx];
                //         Serial.printf("textRefs[%d] = \"%s\"\n", tidx, textRefs[tidx]);
            }
            else
            {
                s += ": ?";
                //              Serial.printf("Invalid textRef (tidx=%d)\n", tidx);
            }
        }
        else
        {
            //          Serial.println("Unsupported field type");
        }

        int drawLineIndex = i + 1; // Lines start below header (row 0 is header)
        bool scheduleNumber = (fieldTypes[idx] == InfoNumber) &&
                              (lines[idx] == "Day Theme Start" || lines[idx] == "Night Theme Start");
        bool brightnessLine = (fieldTypes[idx] == InfoNumber) && (lines[idx] == "Brightness");
        bool arrowLine = (showChooserArrows && fieldTypes[idx] == InfoChooser) ||
                         (showNumberArrows && fieldTypes[idx] == InfoNumber) || scheduleNumber || brightnessLine;
        bool forwardAllowedOnLine = (forwardArrowOnlyIndex < 0) || (forwardArrowOnlyIndex == idx);
        bool forwardIndicator = showForwardArrow && forwardAllowedOnLine && !arrowLine &&
                                (fieldTypes[idx] == InfoLabel || fieldTypes[idx] == InfoButton);
        const int arrowBoxW = kChooserArrowWidth + 1;
        int reservedWidth = 0;
        if (arrowLine)
            reservedWidth = arrowBoxW * 2;
        else if (forwardIndicator)
            reservedWidth = arrowBoxW;
        int availableWidth = SCREEN_WIDTH - reservedWidth;
        if (availableWidth <= 0)
            availableWidth = SCREEN_WIDTH;

        int rowY = drawLineIndex * CHARH;
        // Keep initial setup prompt text visually centered above the Yes/No buttons.
        if (this == &setupPromptModal)
        {
            rowY -= 4;
            // Font glyphs extend above baseline; keep baseline low enough to avoid header overlap.
            if (rowY < (CHARH + 6))
                rowY = CHARH + 6;
        }
        bool extendUp = (drawLineIndex > 1);
        int rowHighlightY = extendUp ? rowY - 1 : rowY;
        int rowHighlightH = extendUp ? (CHARH + 1) : CHARH;
        if (rowHighlightY < 0)
            rowHighlightY = 0;
        // Never paint selection/background into the header band.
        if (rowHighlightY < CHARH)
        {
            int overlap = CHARH - rowHighlightY;
            rowHighlightY = CHARH;
            rowHighlightH -= overlap;
            if (rowHighlightH < 0)
                rowHighlightH = 0;
        }
        int displayHeight = dma_display->height();
        if (rowHighlightY + rowHighlightH > displayHeight)
            rowHighlightH = displayHeight - rowHighlightY;
        // Determine if the next visible row will extend into this one so we can avoid double painting
        bool nextRowExtendsUp = false;
        if (i + 1 < visibleRows)
        {
            int nextIdx = idx + 1;
            if (nextIdx < dataCount)
            {
                bool nextSelected = (!atClose) && (selIndex == nextIdx) && (!inButtonBar);
                int nextDrawLineIndex = (i + 1) + 1;
                nextRowExtendsUp = nextSelected && (nextDrawLineIndex > 1);
            }
        }

        bool isAlarmAmPmLine = (this == &alarmModal && lines[idx] == "AM/PM");

        if (isSelected)
        {
            // Fill the current row so the active line is visually highlighted
            uint16_t rowBg = isEditing ? palette.buttonSelBg : palette.buttonBg;
            dma_display->fillRect(0, rowHighlightY, SCREEN_WIDTH, rowHighlightH, rowBg);

            if (selIndex != lastSelIndex)
            {
                scrollOffset = 0;
                firstScroll = true;
                lastSelIndex = selIndex;
                scrollPaused = false;
                scrollPauseTime = 0;
            }
            int textW = getTextWidth(s.c_str());
            bool needsScroll = isAlarmAmPmLine || textW > availableWidth;
            if (needsScroll && !scrollPaused)
            {
                if (millis() - lastScrollTime > scrollSpeed)
                {
                    lastScrollTime = millis();
                    scrollOffset++;
                    if (textW > SCREEN_WIDTH)
                    {
                        if (scrollOffset > (textW - SCREEN_WIDTH))
                            firstScroll = false;
                        if (!firstScroll && scrollOffset > textW)
                            scrollOffset = -SCREEN_WIDTH;
                    }
                    else if (scrollOffset > availableWidth)
                    {
                        scrollOffset = 0;
                        firstScroll = true;
                    }
                }
            }
            else if (scrollPaused)
            {
                if (scrollPauseTime && (millis() - scrollPauseTime > 1000)) // Pause time when edit
                {
                    scrollPaused = false;
                    scrollPauseTime = 0;
                }
            }
            else
            {
                scrollOffset = 0;
                firstScroll = true;
            }

            dma_display->setTextColor(isEditing ? palette.lineEditing : palette.lineSelected);
            int cursorX = (arrowLine ? arrowBoxW : 0) - scrollOffset;
            dma_display->setCursor(cursorX, rowY);
            dma_display->print(s + (isEditing ? " <" : ""));

            if (arrowLine)
            {
                uint16_t arrowBg = palette.closeSelBg;
                uint16_t arrowFg = palette.closeFg;
                dma_display->fillRect(0, rowHighlightY, arrowBoxW, rowHighlightH, arrowBg);
                dma_display->fillRect(SCREEN_WIDTH - arrowBoxW, rowHighlightY, arrowBoxW, rowHighlightH, arrowBg);
                int arrowY = rowHighlightY + (rowHighlightH / 2) - 3;
                if (arrowY < rowHighlightY)
                    arrowY = rowHighlightY;
                drawBackArrow(0, arrowY, arrowFg);
                drawForwardArrow(SCREEN_WIDTH - kChooserArrowWidth + 1, arrowY, arrowFg);
            }
            else if (forwardIndicator)
            {
                uint16_t arrowBg = palette.closeSelBg;
                uint16_t arrowFg = palette.lineEditing; // use highlight color that fits current theme
                int arrowX = SCREEN_WIDTH - arrowBoxW;
                dma_display->fillRect(arrowX, rowHighlightY, arrowBoxW, rowHighlightH, arrowBg);
                int arrowY = rowHighlightY + (rowHighlightH / 2) - 3;
                if (arrowY < rowHighlightY)
                    arrowY = rowHighlightY;
                drawForwardArrow(arrowX + 1, arrowY, arrowFg);
            }
        }
        else
        {
            // Ensure previously highlighted rows are cleared when not selected
            int clearY = rowY;
            int clearH = CHARH;
            if (nextRowExtendsUp && clearH > 0)
            {
                // Leave bottom pixel for the next row's extended highlight to avoid flicker
                --clearH;
            }
            dma_display->fillRect(0, clearY, SCREEN_WIDTH, clearH, 0);

            String sub = s.substring(0, MAXCOLS);
            dma_display->setTextColor(palette.lineUnselected);
            dma_display->setCursor(0, rowY);
            dma_display->print(sub);
        }
    }

    // --- Button bar ---
// --- Button bar (fixed alignment to prevent first-letter clipping) ---
if (btnCount > 0)
{
    int btnY = (MAXROWS - 1) * CHARH;
    int totalBtnW = 0;
    int btnWidths[MAXROWS];
    const int padX = 3;   // inner padding
    const int padY = 1;   // vertical offset for better centering

    // Measure total width of buttons (including spacing)
    for (int i = 0; i < btnCount; ++i)
    {
        btnWidths[i] = getTextWidth(btnLabels[i].c_str()) + padX * 2;
        totalBtnW += btnWidths[i] + padX;
    }

    // Center horizontally
    int btnX = (SCREEN_WIDTH - totalBtnW + padX) / 2;

    for (int i = 0; i < btnCount; ++i)
    {
        bool selected = (inButtonBar && btnSel == i);
        uint16_t btnBg = selected ? currentPalette.buttonSelBg : currentPalette.buttonBg;
        uint16_t btnFg = selected ? currentPalette.buttonSelFg : currentPalette.buttonFg;

        // Draw button background
        dma_display->fillRect(btnX, btnY, btnWidths[i], CHARH, btnBg);

        // --- FIX: add small left margin to avoid cutting first letter ---
        int textX = btnX + padX;
        int textY = btnY + padY;

        dma_display->setTextColor(btnFg);
        dma_display->setCursor(textX, textY);
        dma_display->print(btnLabels[i]);

        btnX += btnWidths[i] + padX;
    }
}

}

int InfoModal::getSelIndex() const
{
    return atClose ? -1 : selIndex;
}

void InfoModal::setSelIndex(int idx)
{
    if (lineCount <= 0)
        return;
    if (idx < 0)
        idx = 0;
    if (idx >= lineCount)
        idx = lineCount - 1;
    selIndex = idx;
    atClose = false;
    if (scrollY > selIndex)
        scrollY = selIndex;
    int visibleRows = (btnCount > 0) ? DATA_ROWS : DATA_ROWS_FULL;
    if (selIndex >= scrollY + visibleRows)
        scrollY = selIndex - visibleRows + 1;
    resetHeldAdjustState();
}

void InfoModal::tick()
{
    if (!active)
        return;

    // Event-driven by default (no per-frame redraw), but allow low-rate redraw
    // when selected text requires horizontal marquee scrolling.
    if (atClose || inButtonBar || lineCount <= 0 || selIndex < 0 || selIndex >= lineCount)
        return;

    int idx = selIndex;
    String s = lines[idx];

    if (fieldTypes[idx] == InfoNumber)
    {
        int nidx = numberFieldIndices[idx];
        if (nidx >= 0 && nidx < MAX_LINES && intRefs[nidx])
        {
            int val = *(intRefs[nidx]);
            if (lines[idx] == "TimeZone")
                s += ": " + formatUtcOffsetMinutes(val);
            else if (lines[idx] == "Day Theme Start" || lines[idx] == "Night Theme Start")
                s += ": " + formatScheduleTime(val);
            else if (lines[idx].startsWith("Temp Offset"))
                s += ": " + String(static_cast<float>(val) / 10.0f, 1);
            else
                s += ": " + String(val);
        }
        else
        {
            s += ": ?";
        }
    }
    else if (fieldTypes[idx] == InfoChooser)
    {
        int cidx = chooserFieldIndices[idx];
        if (cidx >= 0 && cidx < MAX_LINES &&
            chooserRefs[cidx] && chooserOptions[cidx] &&
            chooserOptionCounts[cidx] > 0)
            s += ": " + getChooserLabel(idx);
        else
            s += ": ?";
    }
    else if (fieldTypes[idx] == InfoText)
    {
        int tidx = textFieldIndices[idx];
        if (tidx >= 0 && tidx < MAX_LINES && textRefs[tidx])
        {
            s += ": ";
            s += textRefs[tidx];
        }
        else
        {
            s += ": ?";
        }
    }

    bool scheduleNumber = (fieldTypes[idx] == InfoNumber) &&
                          (lines[idx] == "Day Theme Start" || lines[idx] == "Night Theme Start");
    bool brightnessLine = (fieldTypes[idx] == InfoNumber) && (lines[idx] == "Brightness");
    bool arrowLine = (showChooserArrows && fieldTypes[idx] == InfoChooser) ||
                     (showNumberArrows && fieldTypes[idx] == InfoNumber) || scheduleNumber || brightnessLine;
    bool forwardAllowedOnLine = (forwardArrowOnlyIndex < 0) || (forwardArrowOnlyIndex == idx);
    bool forwardIndicator = showForwardArrow && forwardAllowedOnLine && !arrowLine &&
                            (fieldTypes[idx] == InfoLabel || fieldTypes[idx] == InfoButton);
    int reservedWidth = 0;
    if (arrowLine)
        reservedWidth = (kChooserArrowWidth + 1) * 2;
    else if (forwardIndicator)
        reservedWidth = (kChooserArrowWidth + 1);
    int availableWidth = SCREEN_WIDTH - reservedWidth;
    if (availableWidth <= 0)
        availableWidth = SCREEN_WIDTH;

    bool isAlarmAmPmLine = (this == &alarmModal && lines[idx] == "AM/PM");
    int textW = getTextWidth(s.c_str());
    bool needsScroll = isAlarmAmPmLine || (textW > availableWidth);

    if (!needsScroll)
        return;

    unsigned long now = millis();
    if (scrollPaused)
    {
        if (scrollPauseTime && (now - scrollPauseTime > 1000UL))
            draw();
        return;
    }

    if (now - lastScrollTime >= static_cast<unsigned long>(scrollSpeed))
        draw();
}

// Optional direct set (not required for normal usage)
void InfoModal::setTextValue(int idx, const char *value)
{
    if (idx < 0 || idx >= MAX_LINES || !textRefs[idx])
        return;
    int maxLen = textSizes[idx] ? textSizes[idx] : 32;
    strncpy(textRefs[idx], value, maxLen - 1);
    textRefs[idx][maxLen - 1] = 0;
}

void InfoModal::handleTextDone(int idx, const char *result)
{
    if (idx < 0 || idx >= MAX_LINES || !textRefs[idx] || textSizes[idx] <= 0)
        return;
    if (result)
    {
        int maxLen = textSizes[idx];
        strncpy(textRefs[idx], result, maxLen - 1);
        textRefs[idx][maxLen - 1] = 0;
    }
    inEdit = false;
    editIndex = -1;
    draw();
}

void InfoModal::setTextRefs(char *textRefsIn[], int count)
{
    textFieldCount = 0;
    for (int i = 0; i < lineCount && textFieldCount < count && i < MAX_LINES; i++)
    {
        if (fieldTypes[i] == InfoText)
        {
            textRefs[textFieldCount] = textRefsIn[textFieldCount];
            textFieldIndices[i] = textFieldCount;
            textSizes[textFieldCount] = 64; // Default size
            textFieldCount++;
        }
    }
}

void InfoModal::resetState()
{
    selIndex = 0;
    scrollY = 0;
    inButtonBar = false;
    atClose = false;
    inEdit = false;
    editIndex = -1;
    resetHeldAdjustState();
}

void InfoModal::resetHeldAdjustState()
{
    heldAdjustKey = 0;
    heldAdjustStartMs = 0;
    heldAdjustLastMs = 0;
    heldAdjustRepeatCount = 0;
}

int InfoModal::adjustedStepForHold(const NumberFieldConfig &config, int direction) const
{
    if (config.step <= 0)
        return 1;

    const uint32_t adjustKey = (direction < 0) ? IR_LEFT : IR_RIGHT;
    unsigned long now = millis();
    bool continuingHold = (heldAdjustKey == adjustKey) &&
                          (heldAdjustLastMs > 0) &&
                          ((now - heldAdjustLastMs) <= 450UL);

    if (!continuingHold)
    {
        return config.step;
    }

    unsigned long heldMs = (heldAdjustStartMs > 0) ? (now - heldAdjustStartMs) : 0;
    int multiplier = 1;
    if (config.accelerateOnHold)
    {
        if (heldAdjustRepeatCount >= 8 || heldMs >= 1200UL)
            multiplier = 5;
        else if (heldAdjustRepeatCount >= 4 || heldMs >= 600UL)
            multiplier = 2;
    }
    return config.step * multiplier;
}

bool InfoModal::applyConfiguredNumberEdit(int lineIndex, int direction)
{
    if (lineIndex < 0 || lineIndex >= MAX_LINES || direction == 0)
        return false;

    int nidx = numberFieldIndices[lineIndex];
    if (nidx < 0 || nidx >= MAX_LINES || !intRefs[nidx])
        return false;

    NumberFieldConfig config = numberFieldConfigs[lineIndex];
    if (config.step <= 0)
        config.step = 1;

    int *ptr = intRefs[nidx];
    int step = adjustedStepForHold(config, direction);
    int candidate = *ptr + (direction < 0 ? -step : step);

    if (config.useDateDayRange)
    {
        int maxDay = daysInMonthForYearMonth(dtYear, dtMonth);
        if (config.wrap)
        {
            if (candidate < 1)
                candidate = maxDay;
            else if (candidate > maxDay)
                candidate = 1;
        }
        else
        {
            candidate = constrain(candidate, 1, maxDay);
        }
    }
    else if (config.hasBounds)
    {
        if (config.wrap)
        {
            if (candidate < config.minValue)
                candidate = config.maxValue;
            else if (candidate > config.maxValue)
                candidate = config.minValue;
        }
        else
        {
            candidate = constrain(candidate, config.minValue, config.maxValue);
        }
    }

    *ptr = candidate;

    if (this == &dateModal)
    {
        if (lines[lineIndex] == "Year")
        {
            dtDay = constrain(dtDay, 1, daysInMonthForYearMonth(dtYear, dtMonth));
        }
        else if (lines[lineIndex] == "Month")
        {
            dtDay = constrain(dtDay, 1, daysInMonthForYearMonth(dtYear, dtMonth));
        }
    }

    unsigned long now = millis();
    const uint32_t adjustKey = (direction < 0) ? IR_LEFT : IR_RIGHT;
    bool continuingHold = (heldAdjustKey == adjustKey) &&
                          (heldAdjustLastMs > 0) &&
                          ((now - heldAdjustLastMs) <= 450UL);
    if (!continuingHold)
    {
        heldAdjustKey = adjustKey;
        heldAdjustStartMs = now;
        heldAdjustRepeatCount = 0;
    }
    else
    {
        ++heldAdjustRepeatCount;
    }
    heldAdjustLastMs = now;

    return true;
}

void InfoModal::handleIR(uint32_t code)
{
    if (!active)
        return;
    auto beep = [&](int freq, int ms = 80) { playBuzzerTone(freq, ms); };
    //   Serial.printf("IR: %08lX | inButtonBar=%d, btnCount=%d, selIndex=%d\n", code, inButtonBar, btnCount, selIndex);

    if (inEdit && fieldTypes[editIndex] == InfoText)
    {
        if (code == IR_CANCEL)
        {
            inEdit = false;
            editIndex = -1;
            beep(900);
            draw();
        }
        return;
    }

    if (atClose)
    {
        selIndex = -1; // CLEAR selection when X is selected
        if (code == IR_OK || code == IR_CANCEL)
        {
            if (callback)
                callback(false, -1);
            beep(900);

            hide(); // Always hide THIS modal first!

            // --- Stack-aware navigation ---
            if (!menuStack.empty())
            {
                MenuLevel prev = menuStack.back();
                menuStack.pop_back();
                while (prev == MENU_DISPLAY && !menuStack.empty())
                {
                    prev = menuStack.back();
                    menuStack.pop_back();
                }
                if (prev == MENU_DISPLAY && menuStack.empty())
                {
                    prev = MENU_MAIN;
                }
                currentMenuLevel = prev;

                switch (prev)
                {
                case MENU_MAIN:
                    showMainMenuModal();
                    break;
                case MENU_DEVICE:
                    showDeviceSettingsModal();
                    break;
                case MENU_WIFISETTINGS:
                    showWiFiSettingsModal();
                    break;
                case MENU_DISPLAY:
                    showDisplaySettingsModal();
                    break;
                case MENU_ALARM:
                    showAlarmSettingsModal();
                    break;
                case MENU_MQTT:
                    showMqttSettingsModal();
                    break;
                case MENU_NOAA:
                    showNoaaSettingsModal();
                    break;
                case MENU_WEATHER:
                    showWeatherSettingsModal();
                    break;
                case MENU_CALIBRATION:
                    showCalibrationModal();
                    break;
                case MENU_SYSTEM:
                    showSystemModal();
                    break;
                case MENU_SYSLOCATION:
                    showDeviceLocationModal();
                    break;
                case MENU_INITIAL_SETUP:
                    currentMenuLevel = MENU_NONE;
                    menuActive = false;
                    break;
                default:
                    currentMenuLevel = MENU_MAIN;
                    showMainMenuModal();
                    break;
                }
            }
            else
            {
                // --- PATCH: If this is Main Menu, exit menu system and go to info/home screen ---
                if (this == &mainMenuModal)
                {
                    exitToHomeScreen();
                    return;
                }
                // Otherwise, show main menu as root
            currentMenuLevel = MENU_MAIN;
            menuActive = true;
            showMainMenuModal();
        }
        beep(1200);
        return;
    }
        if (code == IR_DOWN || code == IR_UP)
        {
            atClose = false;
            // Restore selection to first or last line
            selIndex = (code == IR_DOWN) ? 0 : (lineCount > 0 ? lineCount - 1 : 0);
            scrollY = selIndex;
            draw();
        }
        return;
    }

    if (btnCount > 0 && inButtonBar)
    {
        if (this == &setupPromptModal)
        {
            if (code == IR_LEFT || code == IR_UP)
            {
                btnSel = (btnSel - 1 + btnCount) % btnCount;
                beep(900);
            }
            else if (code == IR_RIGHT || code == IR_DOWN)
            {
                btnSel = (btnSel + 1) % btnCount;
                beep(1800);
            }
            else if (code == IR_OK)
            {
                if (callback)
                    callback(btnSel == 0, btnSel);
                if (btnSel == 1)
                {
                    hide();
                    drawMenu();
                }
                beep(2200);
            }
            if (active)
                draw();
            return;
        }

        if (code == IR_UP)
        {
            inButtonBar = false;
            selIndex = lineCount - 1;
            beep(1500);
        }
        else if (code == IR_LEFT)
        {
            btnSel = (btnSel - 1 + btnCount) % btnCount;
            beep(900);
        }
        else if (code == IR_RIGHT)
        {
            btnSel = (btnSel + 1) % btnCount;
            beep(1800);
        }
        else if (code == IR_OK)
        {
            if (callback)
                callback(btnSel == 0, btnSel);
            if (btnSel == 1)
            {
                hide();
                drawMenu();
            }
            beep(2200);
        }
        draw();
        return;
    }

    if (code == IR_UP)
    {
        resetHeldAdjustState();
        scrollPaused = false;
        scrollPauseTime = 0;
        if (atClose)
        { // --- PATCH: wrap around from X to last line
            atClose = false;
            selIndex = lineCount - 1;
        }
        else if (selIndex == 0)
        {
            atClose = true;
        }
        else
        {
            selIndex--;
        }
        beep(1500);
        draw();
        return;
    }
    else if (code == IR_DOWN)
    {
        resetHeldAdjustState();
        scrollPaused = false;
        scrollPauseTime = 0;
        if (atClose)
        { // --- PATCH: wrap around from X to first line
            atClose = false;
            selIndex = 0;
        }
        else if (selIndex < lineCount - 1)
        {
            selIndex++;
        }
        else
        {
            if (btnCount > 0)
            {
                inButtonBar = true;
                btnSel = 0;
            }
            else
            {
                atClose = true; // --- PATCH: move from last line to X
            }
        }
        beep(1200);
        draw();
        return;
    }

    if (code == IR_LEFT || code == IR_RIGHT)
    {
        scrollPaused = true;        // --- PATCH
        scrollPauseTime = millis(); // --- PATCH

        InfoFieldType type = fieldTypes[selIndex];
        int direction = (code == IR_LEFT) ? -1 : 1;

        if (type == InfoNumber)
        {
            bool handledByConfig = applyConfiguredNumberEdit(selIndex, direction);
            int nidx = numberFieldIndices[selIndex];
            if ((handledByConfig || (nidx >= 0 && intRefs[nidx])))
            {
                int *ptr = (nidx >= 0 && nidx < MAX_LINES) ? intRefs[nidx] : nullptr;
                const String &label = lines[selIndex];
                if (!handledByConfig && ptr)
                {
                    int step = 1;
                    if (label == "Day Theme Start" || label == "Night Theme Start")
                    {
                        step = 5;
                    }

                    if (code == IR_LEFT)
                        (*ptr) -= step;
                    else
                        (*ptr) += step;
                }

                // --- Date/time field constraints ---

                if (!ptr)
                {
                    return;
                }

                if (!handledByConfig && this == &dateModal)
                {
                    if (label == "Year")
                        *ptr = constrain(*ptr, 2020, 2099);
                    else if (label == "Month")
                        *ptr = constrain(*ptr, 1, 12);
                    else if (label == "Day")
                        *ptr = constrain(*ptr, 1, daysInMonthForYearMonth(dtYear, dtMonth));
                    else if (label == "Hour")
                        *ptr = constrain(*ptr, 0, 23);
                    else if (label == "Minute")
                        *ptr = constrain(*ptr, 0, 59);
                    else if (label == "Second")
                        *ptr = constrain(*ptr, 0, 59);
                    else if (label == "Manual Offset (min)")
                        *ptr = constrain(*ptr, -720, 840);
                }
                else if (this == &alarmModal)
                {
                    if (label.startsWith("Hour"))
                    {
                        if (units.clock24h)
                            *ptr = constrain(*ptr, 0, 23);
                        else
                            *ptr = constrain(*ptr, 1, 12);
                    }
                    else if (label.startsWith("Minute"))
                        *ptr = constrain(*ptr, 0, 59);
                }
                else if (label == "Day Theme Start" || label == "Night Theme Start")
                {
                    *ptr = normalizeThemeScheduleMinutes(*ptr);
                }
                else if (label.startsWith("Sound Volume"))
                {
                    *ptr = constrain(*ptr, 0, 100);
                    buzzerVolume = *ptr;
                    if (buzzerVolume > 0)
                    {
                        playBuzzerTone((buzzerToneSet == 0) ? 2000 : 1200, 80);
                    }
                    saveDeviceSettings();
                }
                else if (label.startsWith("Light Threshold"))
                {
                    *ptr = constrain(*ptr, 1, 5000);
                    autoThemeLightThreshold = *ptr;
                    if (autoThemeAmbient)
                    {
                        float lux = readBrightnessSensor();
                        tickAutoThemeAmbient(lux, false, true); // live preview without persisting
                    }
                }

                // Also handle Brightness live preview if on that field (by label!)
                if (lines[selIndex] == "Brightness")
                {
                    *ptr = constrain(*ptr, 1, 100);
                    if (!autoBrightness)
                    {
                        int hw = map(*ptr, 1, 100, 3, 255);
                        if (!isScreenOff())
                        {
                            setPanelBrightness(hw);
                        }
                        //           Serial.printf("[Live] Brightness: %d => HW %d\n", *ptr, hw);
                    }
                    else
                    {
                        //           Serial.println("[Live] Brightness ignored (Auto ON)");
                    }
                    saveDisplaySettings();
                }

                if (this == &calibrationModal || this == &noaaModal)
                {
                    // Clamp according to which field label this is
                    if (lines[selIndex].startsWith("Temp Offset"))
                    {
                        int limit = (units.temp == TempUnit::F) ? 180 : 100;
                        *ptr = constrain(*ptr, -limit, limit);
                        float displayVal = static_cast<float>(*ptr) / 10.0f;
                        float newOffsetC = static_cast<float>(tempOffsetToC(displayVal));
                        tempOffset = constrain(newOffsetC, wxv::defaults::kTempOffsetMinC, wxv::defaults::kTempOffsetMaxC);
                        float normalizedDisplay = static_cast<float>(dispTempOffset(tempOffset));
                        *ptr = static_cast<int>(lroundf(normalizedDisplay * 10.0f));
                    }
                    else if (lines[selIndex].startsWith("Hum Offset")) *ptr = constrain(*ptr, wxv::defaults::kHumOffsetMin, wxv::defaults::kHumOffsetMax);
                    else if (lines[selIndex].startsWith("Light Gain")) *ptr = constrain(*ptr, wxv::defaults::kLightGainMinPercent, wxv::defaults::kLightGainMaxPercent);
                    else if (lines[selIndex].startsWith("CO2 Alert") || lines[selIndex].startsWith("CO2 Threshold")) *ptr = constrain(*ptr, 400, 5000);
                    else if (lines[selIndex].startsWith("Temp Alert") || lines[selIndex].startsWith("Temp Threshold"))
                    {
                        int minTenths = (units.temp == TempUnit::F) ? 500 : 100;
                        int maxTenths = (units.temp == TempUnit::F) ? 1220 : 500;
                        *ptr = constrain(*ptr, minTenths, maxTenths);
                        float displayVal = static_cast<float>(*ptr) / 10.0f;
                        envAlertTempThresholdC = (units.temp == TempUnit::F)
                                                     ? static_cast<float>((displayVal - 32.0f) * 5.0f / 9.0f)
                                                     : displayVal;
                        envAlertTempThresholdC = constrain(envAlertTempThresholdC, 10.0f, 50.0f);
                        float normalizedDisplay = static_cast<float>(dispTemp(envAlertTempThresholdC));
                        *ptr = static_cast<int>(lroundf(normalizedDisplay * 10.0f));
                    }
                    else if (lines[selIndex].startsWith("Hum Low Alert") || lines[selIndex].startsWith("Hum Low Threshold")) *ptr = constrain(*ptr, 0, 100);
                    else if (lines[selIndex].startsWith("Hum High Alert") || lines[selIndex].startsWith("Hum High Threshold")) *ptr = constrain(*ptr, 0, 100);

                    if (lines[selIndex].startsWith("CO2 Alert") || lines[selIndex].startsWith("CO2 Threshold"))
                        envAlertCo2Threshold = *ptr;
                    else if (lines[selIndex].startsWith("Hum Low Alert") || lines[selIndex].startsWith("Hum Low Threshold"))
                        envAlertHumidityLowThreshold = *ptr;
                    else if (lines[selIndex].startsWith("Hum High Alert") || lines[selIndex].startsWith("Hum High Threshold"))
                        envAlertHumidityHighThreshold = *ptr;

                    envAlertCo2Threshold = constrain(envAlertCo2Threshold, 400, 5000);
                    envAlertHumidityLowThreshold = constrain(envAlertHumidityLowThreshold, 0, 100);
                    envAlertHumidityHighThreshold = constrain(envAlertHumidityHighThreshold, 0, 100);
                    if (envAlertHumidityLowThreshold > envAlertHumidityHighThreshold)
                    {
                        if (lines[selIndex].startsWith("Hum Low Alert") || lines[selIndex].startsWith("Hum Low Threshold"))
                            envAlertHumidityHighThreshold = envAlertHumidityLowThreshold;
                        else if (lines[selIndex].startsWith("Hum High Alert") || lines[selIndex].startsWith("Hum High Threshold"))
                            envAlertHumidityLowThreshold = envAlertHumidityHighThreshold;
                    }

                    // Save to NVS right away
                    saveCalibrationSettings();

                    // Apply brightness instantly in case lightGain affects auto-bright logic
                    float lux = readBrightnessSensor();
                    setDisplayBrightnessFromLux(lux);

                    // Hint the climate screen to refresh next tick
                    newAHT20_BMP280Data = true;

                    Serial.printf("[Alert/Calibration autosave] temp=%.1f hum=%d gain=%d co2=%d tempAlert=%.1f humLow=%d humHigh=%d\n",
                                tempOffset, humOffset, lightGain,
                                envAlertCo2Threshold, envAlertTempThresholdC,
                                envAlertHumidityLowThreshold, envAlertHumidityHighThreshold);
                }

                beep(code == IR_LEFT ? 900 : 1800);
                draw();
            }
        }

        else if (type == InfoChooser)
        {
            resetHeldAdjustState();
            int cidx = chooserFieldIndices[selIndex];
            if (cidx >= 0 && chooserRefs[cidx])
            {
                int &val = *(chooserRefs[cidx]);
                int count = chooserOptionCounts[cidx];
                if (count > 0)
                {
                    val = (val + (code == IR_LEFT ? count - 1 : 1)) % count;

                    // Special case: Alarm modal "Select Alarm" should refresh without restarting scroll
                    if (this == &alarmModal && lines[selIndex] == "Select Alarm")
                    {
                        int savedOffset = scrollOffset;
                        bool savedFirst = firstScroll;
                        int savedLastSel = lastSelIndex;
                        unsigned long savedLastScroll = lastScrollTime;
                        handleAlarmSlotChangedInModal();
                        // Restore scroll state so marquee doesn't restart
                        scrollOffset = savedOffset;
                        firstScroll = savedFirst;
                        lastSelIndex = savedLastSel;
                        lastScrollTime = savedLastScroll;
                        beep(code == IR_LEFT ? 900 : 1800);
                        return;
                    }

                    if (lines[selIndex].equalsIgnoreCase("Auto Brightness"))
                    { // Auto Brightness toggle
                        autoBrightness = (val > 0);
                        //         Serial.printf("[Live] AutoBrightness: %s\n", autoBrightness ? "ON" : "OFF");

                        if (autoBrightness)
                        {
                            float lux = readBrightnessSensor();
                            setDisplayBrightnessFromLux(lux);
                            //           Serial.printf("[Live] Auto ON ??? Brightness from Lux: %.1f\n", lux);
                        }
                        else
                        {
                            int bIdx = numberFieldIndices[2];
                            if (bIdx >= 0 && intRefs[bIdx])
                            {
                                int b = constrain(*intRefs[bIdx], 1, 100);
                                int hw = map(b, 1, 100, 3, 255);
                                if (!isScreenOff())
                                {
                                    setPanelBrightness(hw);
                                }
                                //              Serial.printf("[Live] Auto OFF ??? Brightness: %d => HW %d\n", b, hw);
                            }
                        }
                        saveDisplaySettings();
                    }

                    if (lines[selIndex].equalsIgnoreCase("Scroll Speed"))
                    { // Scroll Speed chooser
                        scrollLevel = constrain(val, 0, 9);
                        scrollSpeed = scrollDelays[scrollLevel];
                        //          Serial.printf("[Live] ScrollSpeed set to %d ms (Level %d)\n", scrollSpeed, scrollLevel);
                        saveDisplaySettings();
                    }

                    if (lines[selIndex] == "Auto Rotate")
                    { // Auto Rotate toggle
                        setAutoRotateEnabled(val > 0, true);
                    }

                    if (lines[selIndex] == "Timezone")
                    {
                        int tzCountInt = static_cast<int>(timezoneCount());
                        if (tzCountInt > 31)
                            tzCountInt = 31;
                        dtTimezoneIndex = constrain(dtTimezoneIndex, 0, tzCountInt);
                        bool useCustomTz = (dtTimezoneIndex == tzCountInt);
                        if (useCustomTz)
                        {
                            setCustomTimezoneOffset(dtManualOffset);
                            dtAutoDst = 0;
                        }
                        else
                        {
                            selectTimezoneByIndex(dtTimezoneIndex);
                            dtManualOffset = tzStandardOffset;
                            dtAutoDst = tzAutoDst ? 1 : 0;
                        }
                        saveDateTimeSettings();
                        DateTime utcNow;
                        if (rtcReady)
                            utcNow = rtc.now();
                        else
                        {
                            DateTime localNow;
                            if (getLocalDateTime(localNow))
                                utcNow = localToUtc(localNow);
                            else
                                utcNow = DateTime(2000, 1, 1, 0, 0, 0);
                        }
                        updateTimezoneOffsetWithUtc(utcNow);
                        // Keep system clock in sync with UTC so timezone offset is applied immediately
                        setSystemTimeFromDateTime(utcNow);
                        getTimeFromRTC();
                        reset_Time_and_Date_Display = true;
                    }

                    if (lines[selIndex] == "Auto DST")
                    {
                        setTimezoneAutoDst(dtAutoDst != 0);
                        saveDateTimeSettings();
                        DateTime utcNow;
                        if (rtcReady)
                            utcNow = rtc.now();
                        else
                        {
                            DateTime localNow;
                            if (getLocalDateTime(localNow))
                                utcNow = localToUtc(localNow);
                            else
                                utcNow = DateTime(2000, 1, 1, 0, 0, 0);
                        }
                        updateTimezoneOffsetWithUtc(utcNow);
                        setSystemTimeFromDateTime(utcNow);
                        getTimeFromRTC();
                        reset_Time_and_Date_Display = true;
                    }

                    if (lines[selIndex] == "Rotate Interval")
                    { // Rotate Interval chooser
                        const char *label = nullptr;
                        if (chooserOptions[cidx] && val >= 0 && val < chooserOptionCounts[cidx])
                        {
                            label = chooserOptions[cidx][val];
                        }
                        int seconds = 0;
                        if (label)
                        {
                            while (*label && !isdigit(static_cast<unsigned char>(*label)))
                                ++label;
                            while (*label && isdigit(static_cast<unsigned char>(*label)))
                            {
                                seconds = seconds * 10 + (*label - '0');
                                ++label;
                            }
                        }
                        if (seconds <= 0)
                        {
                            seconds = autoRotateInterval;
                        }
                        setAutoRotateInterval(seconds, true);
                    }

                    // Rebuild Display modal when Theme Mode changes to show relevant fields
                    if (this == &displayModal && lines[selIndex] == "Theme Mode")
                    {
                        // Cache current selection so the rebuilt modal keeps the new mode
                        preserveDisplayModeTemp = true;
                        cachedDisplayModeTemp = val;
                        autoThemeModeTemp = val;
                        displayModal.hide();
                        pendingModalFn = showDisplaySettingsModal;
                        pendingModalTime = millis() + 10;
                        return;
                    }
                    if (this == &noaaModal &&
                        (lines[selIndex] == "NOAA Alerts" ||
                         lines[selIndex] == "CO2 Alert" ||
                         lines[selIndex] == "Temp Alert" ||
                         lines[selIndex] == "Humidity Alert"))
                    {
                        noaaAlertsEnabled = (lines[selIndex] == "NOAA Alerts") ? (val > 0) : noaaAlertsEnabled;
                        envAlertCo2Enabled = (lines[selIndex] == "CO2 Alert") ? (val > 0) : envAlertCo2Enabled;
                        envAlertTempEnabled = (lines[selIndex] == "Temp Alert") ? (val > 0) : envAlertTempEnabled;
                        envAlertHumidityEnabled = (lines[selIndex] == "Humidity Alert") ? (val > 0) : envAlertHumidityEnabled;
                        saveNoaaSettings();
                        saveCalibrationSettings();
                        if (lines[selIndex] == "NOAA Alerts")
                            notifyNoaaSettingsChanged();
                        requestNoaaSettingsModalRefresh();
                        beep(code == IR_LEFT ? 900 : 1800);
                        return;
                    }
                    if (lines[selIndex].startsWith("Sound Profile"))
                    {
                        val = constrain(val, 0, 6);
                        buzzerToneSet = val;
                        int preview = 0;
                        switch (val) {
                            case 0: preview = 2200; break; // Bright
                            case 1: preview = 1200; break; // Soft
                            case 2: preview = 5000; break; // Click
                            case 3: preview = 1800; break; // Chime
                            case 4: preview = 2000; break; // Pulse
                            case 5: preview = 900;  break; // Warm
                            case 6: preview = 1600; break; // Melody
                        }
                        playBuzzerTone(preview, 70);
                        saveDeviceSettings();
                    }

                    beep(code == IR_LEFT ? 900 : 1800);
                    draw();
                }
            }
        }
        return;
    }

    resetHeldAdjustState();

    if (code == IR_OK)
    {
        if (fieldTypes[selIndex] == InfoText)
        {
            int textIdx = textFieldIndices[selIndex];
            if (textIdx >= 0 && textRefs[textIdx])
            {
                inEdit = true;
                editIndex = selIndex;
                s_modalForText = this;
                s_textIdxForText = textIdx;
                startKeyboardEntry(
                    textRefs[textIdx],
                    keyboardTextDoneShim,
                    lines[selIndex].c_str());
                beep(2200);
            }
        }
        else if (fieldTypes[selIndex] == InfoChooser)
        {
            // Left/Right already handle value change; disable OK for chooser lines
            // So do nothing here on OK for chooser.
        }
        else if (fieldTypes[selIndex] == InfoNumber)
        {
            const String &label = lines[selIndex];
            if (label == "Day Theme Start" || label == "Night Theme Start")
            {
                // Ignore OK press on scheduled theme rows to prevent accidental exit
                return;
            }
        }
        else if (btnCount == 0 && callback)
        {
            // Only call callback and hide if there are no buttons and field is not Text or Chooser
            callback(true, selIndex);
            if (!keepOpenOnSelect)
            {
                hide();
                drawMenu();
            }
            else
            {
                draw();
            }
        }
        beep(2200);
        return;
    }

    if (code == IR_CANCEL)
    {
        if (inEdit)
        {
            inEdit = false;
            editIndex = -1;
            draw();
        }
        else if (callback)
        {
            callback(false, -1);

            hide(); // Always hide THIS modal first!

            // --- Stack-aware navigation; collapse duplicate Display modal rebuilds ---
            if (!menuStack.empty())
            {
                MenuLevel prev = menuStack.back();
                menuStack.pop_back();
                while (prev == MENU_DISPLAY && !menuStack.empty())
                {
                    prev = menuStack.back();
                    menuStack.pop_back();
                }
                if (prev == MENU_DISPLAY && menuStack.empty())
                {
                    prev = MENU_MAIN;
                }
                currentMenuLevel = prev;

                switch (prev)
                {
                case MENU_MAIN:
                    showMainMenuModal();
                    break;
                case MENU_DEVICE:
                    showDeviceSettingsModal();
                    break;
                case MENU_WIFISETTINGS:
                    showWiFiSettingsModal();
                    break;
                case MENU_DISPLAY:
                    showDisplaySettingsModal();
                    break;
                case MENU_ALARM:
                    showAlarmSettingsModal();
                    break;
                case MENU_MQTT:
                    showMqttSettingsModal();
                    break;
                case MENU_NOAA:
                    showNoaaSettingsModal();
                    break;
                case MENU_WEATHER:
                    showWeatherSettingsModal();
                    break;
                case MENU_CALIBRATION:
                    showCalibrationModal();
                    break;
                case MENU_SYSTEM:
                    showSystemModal();
                    break;
                case MENU_SYSLOCATION:
                    showDeviceLocationModal();
                    break;
                case MENU_INITIAL_SETUP:
                    currentMenuLevel = MENU_NONE;
                    menuActive = false;
                    break;
                default:
                    currentMenuLevel = MENU_MAIN;
                    showMainMenuModal();
                    break;
                }
            }
            else
            {
                // --- PATCH: If this is Main Menu, exit menu system and go to info/home screen ---
                if (this == &mainMenuModal)
                {
                    exitToHomeScreen();
                    return;
                }
                // Otherwise, show main menu as root
                currentMenuLevel = MENU_MAIN;
                menuActive = true;
                showMainMenuModal();
            }
            return;
        }
    }
}


