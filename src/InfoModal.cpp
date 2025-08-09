#include "InfoModal.h"
#include "display.h"
#include "menu.h"
#include "keyboard.h"
#include <cstring>
#include <vector>

extern bool autoBrightness;
extern int scrollLevel;
extern void saveDisplaySettings();
extern std::vector<MenuLevel> menuStack;

// --- Static trampoline for keyboard -> InfoModal::setTextValue ---
static InfoModal *s_modalForText = nullptr;
static int s_textIdxForText = -1;

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
    inButtonBar = false;
    active = true;
    draw();
}

void InfoModal::hide() { active = false; }

bool InfoModal::isActive() const { return active; }

void InfoModal::drawHeader()
{
    const int headerHeight = CHARH;
    dma_display->fillRect(0, 0, SCREEN_WIDTH, headerHeight, INFOMODAL_HEADERBG);

    dma_display->setTextColor(INFOMODAL_GREEN);
    dma_display->setCursor(1, 0);
    dma_display->print(modalTitle);

    int xWidth = 7;
    int xX = SCREEN_WIDTH - xWidth;
    int xY = -1;
    if (xY < 0)
        xY = 0;

    uint16_t xBgColor = atClose ? INFOMODAL_SELXBG : INFOMODAL_UNSELXBG;
    uint16_t xFgColor = INFOMODAL_XCOLOR;

    dma_display->fillRect(xX, 0, xWidth, headerHeight, xBgColor);
    dma_display->setTextColor(xFgColor);
    dma_display->setCursor(xX + 1, xY - 1);
    dma_display->print("x");

    dma_display->drawFastHLine(0, headerHeight - 1, SCREEN_WIDTH, INFOMODAL_ULINE);
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

void InfoModal::draw()
{
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
        bool isEditing = isSelected;

        dma_display->setTextColor(isEditing ? INFOMODAL_EDIT : INFOMODAL_SEL);
        String s = lines[idx];

        if (fieldTypes[idx] == InfoNumber)
        {
            int nidx = numberFieldIndices[idx];
            if (nidx >= 0 && nidx < MAX_LINES && intRefs[nidx])
            {
                int val = *(intRefs[nidx]);
                s += ": " + String(val);
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

        if (isSelected)
        {
            if (selIndex != lastSelIndex)
            {
                scrollOffset = 0;
                firstScroll = true;
                lastSelIndex = selIndex;
                scrollPaused = false;
                scrollPauseTime = 0;
            }
            int textW = getTextWidth(s.c_str());
            if (textW > SCREEN_WIDTH)
            {
                if (!scrollPaused)
                {
                    if (millis() - lastScrollTime > scrollSpeed)
                    {
                        lastScrollTime = millis();
                        scrollOffset++;
                        if (scrollOffset > (textW - SCREEN_WIDTH))
                            firstScroll = false;
                        if (!firstScroll && scrollOffset > textW)
                            scrollOffset = -SCREEN_WIDTH;
                    }
                }
                else
                {
                    if (scrollPauseTime && (millis() - scrollPauseTime > 1000)) // Pause time when edit
                    {
                        scrollPaused = false;
                        scrollPauseTime = 0;
                    }
                }
            }
            else
            {
                scrollOffset = 0;
                firstScroll = true;
                scrollPaused = false;
                scrollPauseTime = 0;
            }

            dma_display->setTextColor(isEditing ? INFOMODAL_EDIT : INFOMODAL_SEL);
            int cursorX = -scrollOffset;
            dma_display->setCursor(cursorX, drawLineIndex * CHARH);
            dma_display->print(s + (isEditing ? " <" : ""));
        }
        else
        {
            String sub = s.substring(0, MAXCOLS);
            dma_display->setTextColor(INFOMODAL_UNSEL);
            dma_display->setCursor(0, drawLineIndex * CHARH);
            dma_display->print(sub);
        }
    }

    // --- Button bar ---
    if (btnCount > 0)
    {
        int btnY = (MAXROWS - 1) * CHARH;
        int totalBtnW = 0;
        int btnWidths[MAXROWS];
        int pad = 3;

        for (int i = 0; i < btnCount; ++i)
        {
            btnWidths[i] = btnLabels[i].length() * 6 + 2 * pad;
            totalBtnW += btnWidths[i] + pad;
        }

        int btnX = (SCREEN_WIDTH - totalBtnW + pad) / 2;
        for (int i = 0; i < btnCount; ++i)
        {
            bool selected = (inButtonBar && btnSel == i);
            uint16_t btnBg = selected ? INFOMODAL_BTN_SELBG : INFOMODAL_BTN_BG;
            uint16_t btnFg = selected ? INFOMODAL_HEADERBG : INFOMODAL_XCOLOR;
            dma_display->fillRect(btnX, btnY, btnWidths[i], CHARH, btnBg);
            dma_display->setTextColor(btnFg);
            dma_display->setCursor(btnX + pad, btnY);
            dma_display->print(btnLabels[i]);
            btnX += btnWidths[i] + pad;
        }
    }
}

int InfoModal::getSelIndex() const
{
    return atClose ? -1 : selIndex;
}

void InfoModal::tick()
{
    if (!active)
        return;
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
}

void InfoModal::handleIR(uint32_t code)
{
    if (!active)
        return;
    //   Serial.printf("IR: %08lX | inButtonBar=%d, btnCount=%d, selIndex=%d\n", code, inButtonBar, btnCount, selIndex);

    if (inEdit && fieldTypes[editIndex] == InfoText)
    {
        if (code == IR_CANCEL)
        {
            inEdit = false;
            editIndex = -1;
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

            hide(); // Always hide THIS modal first!

            // --- Stack-aware navigation ---
            if (!menuStack.empty())
            {
                MenuLevel prev = menuStack.back();
                menuStack.pop_back();
                currentMenuLevel = prev;

                switch (prev)
                {
                case MENU_MAIN:
                    showMainMenuModal();
                    break;
                case MENU_DEVICE:
                    showDeviceSettingsModal();
                    break;
                case MENU_DISPLAY:
                    showDisplaySettingsModal();
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
                    menuActive = false;
                    return;
                }
                // Otherwise, show main menu as root
                currentMenuLevel = MENU_MAIN;
                menuActive = true;
                showMainMenuModal();
            }
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
        if (code == IR_UP)
        {
            inButtonBar = false;
            selIndex = lineCount - 1;
        }
        else if (code == IR_LEFT)
        {
            btnSel = (btnSel - 1 + btnCount) % btnCount;
        }
        else if (code == IR_RIGHT)
        {
            btnSel = (btnSel + 1) % btnCount;
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
        }
        draw();
        return;
    }

    if (code == IR_UP)
    {
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
        draw();
        return;
    }
    else if (code == IR_DOWN)
    {
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
            atClose = true; // --- PATCH: move from last line to X
        }
        draw();
        return;
    }

    if (code == IR_LEFT || code == IR_RIGHT)
    {
        scrollPaused = true;        // --- PATCH
        scrollPauseTime = millis(); // --- PATCH

        InfoFieldType type = fieldTypes[selIndex];

        if (type == InfoNumber)
        {
            int nidx = numberFieldIndices[selIndex];
            if (nidx >= 0 && intRefs[nidx])
            {
                int *ptr = intRefs[nidx];
                if (code == IR_LEFT)
                    (*ptr)--;
                else
                    (*ptr)++;

                // --- Date/time field constraints ---

                if (this == &dateModal)
                {
                    switch (selIndex)
                    {
                    case 0:
                        *ptr = constrain(*ptr, 2000, 2099);
                        break; // Year
                    case 1:
                        *ptr = constrain(*ptr, 1, 12);
                        break; // Month
                    case 2:
                        *ptr = constrain(*ptr, 1, 31);
                        break; // Day
                    case 3:
                        *ptr = constrain(*ptr, 0, 23);
                        break; // Hour
                    case 4:
                        *ptr = constrain(*ptr, 0, 59);
                        break; // Minute
                    case 5:
                        *ptr = constrain(*ptr, 0, 59);
                        break; // Second
                    case 6:
                        *ptr = constrain(*ptr, -720, 840);
                        break; // TimeZone
                    }
                }

                // Also handle Brightness live preview if on that field (by label!)
                if (lines[selIndex] == "Brightness")
                {
                    *ptr = constrain(*ptr, 1, 100);
                    if (!autoBrightness)
                    {
                        int hw = map(*ptr, 1, 100, 3, 255);
                        dma_display->setBrightness8(hw);
                        //           Serial.printf("[Live] Brightness: %d => HW %d\n", *ptr, hw);
                    }
                    else
                    {
                        //           Serial.println("[Live] Brightness ignored (Auto ON)");
                    }
                    saveDisplaySettings();
                }

                if (this == &calibrationModal )
                {
                    // Clamp according to which field label this is
                    if (lines[selIndex].startsWith("Temp Offset"))   *ptr = constrain(*ptr, -10, 10);
                    else if (lines[selIndex].startsWith("Hum Offset")) *ptr = constrain(*ptr, -20, 20);
                    else if (lines[selIndex].startsWith("Light Gain")) *ptr = constrain(*ptr, 1, 150);

                    // Save to NVS right away
                    saveCalibrationSettings();

                    // Apply brightness instantly in case lightGain affects auto-bright logic
                    float lux = readBrightnessSensor();
                    setDisplayBrightnessFromLux(lux);

                    // Hint the climate screen to refresh next tick
                    newAHT20_BMP280Data = true;

                    Serial.printf("[Calibration autosave] temp=%d hum=%d gain=%d\n",
                                tempOffset, humOffset, lightGain);
                }

                draw();
            }
        }

        else if (type == InfoChooser)
        {
            int cidx = chooserFieldIndices[selIndex];
            if (cidx >= 0 && chooserRefs[cidx])
            {
                int &val = *(chooserRefs[cidx]);
                int count = chooserOptionCounts[cidx];
                if (count > 0)
                {
                    val = (val + (code == IR_LEFT ? count - 1 : 1)) % count;

                    if (selIndex == 1)
                    { // Auto Brightness toggle
                        autoBrightness = (val > 0);
                        //         Serial.printf("[Live] AutoBrightness: %s\n", autoBrightness ? "ON" : "OFF");

                        if (autoBrightness)
                        {
                            float lux = readBrightnessSensor();
                            setDisplayBrightnessFromLux(lux);
                            //           Serial.printf("[Live] Auto ON → Brightness from Lux: %.1f\n", lux);
                        }
                        else
                        {
                            int bIdx = numberFieldIndices[2];
                            if (bIdx >= 0 && intRefs[bIdx])
                            {
                                int b = constrain(*intRefs[bIdx], 1, 100);
                                int hw = map(b, 1, 100, 3, 255);
                                dma_display->setBrightness8(hw);
                                //              Serial.printf("[Live] Auto OFF → Brightness: %d => HW %d\n", b, hw);
                            }
                        }
                        saveDisplaySettings();
                    }

                    if (selIndex == 3)
                    { // Scroll Speed chooser
                        scrollLevel = constrain(val, 0, 9);
                        scrollSpeed = scrollDelays[scrollLevel];
                        //          Serial.printf("[Live] ScrollSpeed set to %d ms (Level %d)\n", scrollSpeed, scrollLevel);
                        saveDisplaySettings();
                    }

                    draw();
                }
            }
        }
        return;
    }

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
            }
        }
        else if (fieldTypes[selIndex] == InfoChooser)
        {
            // Left/Right already handle value change; disable OK for chooser lines
            // So do nothing here on OK for chooser.
        }
        else if (btnCount == 0 && callback)
        {
            // Only call callback and hide if there are no buttons and field is not Text or Chooser
            callback(true, selIndex);
            hide();
            drawMenu();
        }
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

            if (!menuStack.empty())
            {
                MenuLevel prev = menuStack.back();
                menuStack.pop_back();
                currentMenuLevel = prev;

                switch (prev)
                {
                case MENU_MAIN:
                    showMainMenuModal();
                    break;
                case MENU_DEVICE:
                    showDeviceSettingsModal();
                    break;
                case MENU_DISPLAY:
                    showDisplaySettingsModal();
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
                    menuActive = false;
                    dma_display->clearScreen();
                    delay(50);
                    fetchWeatherFromOWM();
                    displayClock();
                    displayDate();
                    displayWeatherData();
                    reset_Time_and_Date_Display = true;
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
