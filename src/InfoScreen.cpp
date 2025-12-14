#include "InfoScreen.h"
#include "InfoModal.h"
#include "display.h"
#include "utils.h"
#include "RollingUpScreen.h"
#include "noaa.h"
#include "settings.h"

extern const int NUM_INFOSCREENS;
extern const ScreenMode InfoScreenModes[];
extern ScreenMode currentScreen;
extern int scrollSpeed;
extern int verticalScrollSpeed;
extern int theme;

static RollingUpScreen s_hourlyRoll(InfoScreen::SCREEN_WIDTH, InfoScreen::SCREEN_HEIGHT - InfoScreen::CHARH, InfoScreen::CHARH);
static RollingUpScreen s_dailyRoll(InfoScreen::SCREEN_WIDTH, InfoScreen::SCREEN_HEIGHT - InfoScreen::CHARH, InfoScreen::CHARH);
static RollingUpScreen s_liveRoll(InfoScreen::SCREEN_WIDTH, InfoScreen::SCREEN_HEIGHT - InfoScreen::CHARH, InfoScreen::CHARH);
static RollingUpScreen s_currentRoll(InfoScreen::SCREEN_WIDTH, InfoScreen::SCREEN_HEIGHT - InfoScreen::CHARH, InfoScreen::CHARH);
static RollingUpScreen s_noaaRoll(InfoScreen::SCREEN_WIDTH, InfoScreen::SCREEN_HEIGHT - InfoScreen::CHARH, InfoScreen::CHARH);


static uint16_t brightenColor(uint16_t color, uint8_t boost = 50)
{
    uint8_t r = ((color >> 11) & 0x1F) * 255 / 31;
    uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
    uint8_t b = (color & 0x1F) * 255 / 31;

    auto clamp = [](uint8_t v, uint8_t increase) -> uint8_t {
        uint16_t temp = static_cast<uint16_t>(v) + increase;
        if (temp > 255)
            temp = 255;
        return static_cast<uint8_t>(temp);
    };

    r = clamp(r, boost);
    g = clamp(g, boost);
    b = clamp(b, boost);
    return dma_display->color565(r, g, b);
}

// Simple word-wrap to fit within the small 64px screen width
static std::vector<String> wrapToWidth(const String &text, int maxWidthPx)
{
    std::vector<String> out;
    String current;
    int currentW = 0;
    int spaceW = getTextWidth(" ");
    int idx = 0;
    while (idx < text.length())
    {
        // read next word
        int start = idx;
        while (idx < text.length() && !isspace(text[idx])) idx++;
        String word = text.substring(start, idx);
        int wW = getTextWidth(word.c_str());
        if (!current.isEmpty() && currentW + spaceW + wW > maxWidthPx)
        {
            out.push_back(current);
            current = word;
            currentW = wW;
        }
        else
        {
            if (!current.isEmpty())
            {
                current += " ";
                currentW += spaceW;
            }
            current += word;
            currentW += wW;
        }
        while (idx < text.length() && isspace(text[idx]))
        {
            idx++;
        }
    }
    if (!current.isEmpty())
        out.push_back(current);
    if (out.empty())
        out.push_back("");
    return out;
}

static String truncateToWidth(const String &text, int maxWidthPx)
{
    if (maxWidthPx < 1)
        return "";
    if (getTextWidth(text.c_str()) <= maxWidthPx)
        return text;

    String out = text;
    // Keep at least 1 char
    while (out.length() > 1 && getTextWidth(out.c_str()) > maxWidthPx)
    {
        out.remove(out.length() - 1);
    }
    return out;
}

InfoScreen::InfoScreen(const String& title, ScreenMode mode)
    : _title(title), _screenMode(mode), _lineCount(0), _active(false),
      _onExit(nullptr), scrollY(0), selIndex(0), lastSelIndex(-1),
      firstScroll(true), scrollPaused(false), scrollPauseTime(0),
      _highlightEnabled(true), _lineOverlay(nullptr)
{
    for (int i = 0; i < INFOSCREEN_MAX_LINES; ++i)
    {
        _lineColors[i] = 0;
        _lineColorUsed[i] = false;
    }
    resetHScroll();
}

void InfoScreen::setTitle(const String& title) { _title = title; }

void InfoScreen::setLines(const String lines[], int n, bool resetPosition, const uint16_t colors[]) {
    _lineCount = (n > INFOSCREEN_MAX_LINES) ? INFOSCREEN_MAX_LINES : n;
    for (int i = 0; i < _lineCount; ++i) {
        _lines[i] = lines[i];
        if (colors)
        {
            _lineColors[i] = colors[i];
            _lineColorUsed[i] = true;
        }
        else
        {
            _lineColors[i] = 0;
            _lineColorUsed[i] = false;
        }
    }
    for (int i = _lineCount; i < INFOSCREEN_MAX_LINES; ++i)
    {
        _lineColors[i] = 0;
        _lineColorUsed[i] = false;
    }

    if (resetPosition) {
        scrollY = 0;
        selIndex = 0;
        lastSelIndex = -1;
        resetHScroll();
    }

    // For the WeatherFlow hourly screen, also feed the vertical scroller with wrapped lines
    if (_screenMode == SCREEN_HOURLY) {
        std::vector<String> vec;
        std::vector<uint16_t> colors;
        std::vector<int> offsets;
        std::vector<int> yOffsets;
        std::vector<const uint8_t *> icons;
        std::vector<uint16_t> iconColors;
        vec.reserve(_lineCount * 2);
        for (int i = 0; i < _lineCount; ++i) {
            // Split on '\n' to allow fixed block entries
            const bool monoTheme = (theme == 1);
            const uint16_t labelColor = monoTheme ? dma_display->color565(220, 220, 120)
                                                  : dma_display->color565(255, 240, 140);
            const uint16_t valueColor = monoTheme ? dma_display->color565(120, 160, 255)
                                                  : dma_display->color565(120, 200, 255);
            const bool iconEnabled = (forecastIconSize == 16);
            const int iconPad = iconEnabled ? 18 : 0;
            const int valueIndentPx = 4;

            // Split into up to 3 lines (time, data, condition)
            String raw = _lines[i];
            int firstNl = raw.indexOf('\n');
            int secondNl = (firstNl >= 0) ? raw.indexOf('\n', firstNl + 1) : -1;
            String line1 = (firstNl >= 0) ? raw.substring(0, firstNl) : raw;
            String line2 = (firstNl >= 0) ? (secondNl >= 0 ? raw.substring(firstNl + 1, secondNl) : raw.substring(firstNl + 1)) : "";
            String line3 = (secondNl >= 0) ? raw.substring(secondNl + 1) : "";
            line1.trim();
            line2.trim();
            line3.trim();

            const uint8_t *iconPtr = nullptr;
            uint16_t iconClr = 0;
            // Parse icon hint from line1 only
            int hint = line1.lastIndexOf("[icon=");
            if (hint >= 0) {
                int end = line1.indexOf(']', hint);
                if (end > hint) {
                    String code = line1.substring(hint + 6, end);
                    line1.remove(hint);
                    code.trim();
                    if (iconEnabled && code.length()) {
                        iconPtr = getWeatherIconFromCondition(code);
                        iconClr = getIconColorFromCondition(code);
                    }
                }
            }

            // Add 1px visual gap between blocks by cumulatively offsetting each hour's block.
            const int blockYOffset = i; // per-hour entry index

            // Line 1: timestamp with icon anchored at left margin
            if (line1.length()) {
                vec.push_back(truncateToWidth(line1, InfoScreen::SCREEN_WIDTH - iconPad));
                colors.push_back(labelColor);
                offsets.push_back(0);
                yOffsets.push_back(blockYOffset);
                icons.push_back(iconEnabled ? iconPtr : nullptr);
                iconColors.push_back(iconEnabled ? iconClr : 0);
            }
            // Line 2: data line aligned to the right of icon
            vec.push_back(truncateToWidth(line2, InfoScreen::SCREEN_WIDTH - valueIndentPx - iconPad));
            colors.push_back(valueColor);
            offsets.push_back(valueIndentPx + iconPad);
            yOffsets.push_back(blockYOffset);
            icons.push_back(nullptr);
            iconColors.push_back(0);

            // Line 3: condition line (optional) aligned to the right of icon
            vec.push_back(truncateToWidth(line3, InfoScreen::SCREEN_WIDTH));
            colors.push_back(valueColor);
            offsets.push_back(0);
            yOffsets.push_back(blockYOffset + 1); // +1px: last line down within block
            icons.push_back(nullptr);
            iconColors.push_back(0);
        }
        s_hourlyRoll.setLines(vec, resetPosition);
        s_hourlyRoll.setLineColors(colors);
        s_hourlyRoll.setLineOffsets(offsets);
        s_hourlyRoll.setLineYOffsets(yOffsets);
        s_hourlyRoll.setLineIcons(icons, iconColors);
        s_hourlyRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_hourlyRoll.setEntryExit(InfoScreen::SCREEN_HEIGHT, InfoScreen::CHARH); // enter from bottom, exit just under title
        s_hourlyRoll.setGapHoldMs(0);
        s_hourlyRoll.setExitHoldMs(constrain(forecastPauseMs, 0, 10000));
        s_hourlyRoll.setBlockSizePx(max(forecastIconSize == 16 ? 16 : 0, InfoScreen::CHARH * 3 + 1));
    }
    else if (_screenMode == SCREEN_UDP_FORECAST) {
        // Convert daily forecast list into a vertical scroll-up body similar to the hourly screen
        std::vector<String> vec;
        std::vector<uint16_t> colors;
           std::vector<int> offsets;
           std::vector<int> yOffsets;
           std::vector<const uint8_t *> icons;
           std::vector<uint16_t> iconColors;
           vec.reserve(_lineCount * 4);
           const int linesPerDay = constrain(forecastLinesPerDay, 2, 3);
           const bool iconEnabled = (forecastIconSize == 16);
          const bool monoTheme = (theme == 1);
        const uint16_t labelColor = monoTheme ? dma_display->color565(220, 220, 120)
                                              : dma_display->color565(255, 240, 140);
        const uint16_t valueColor = monoTheme ? dma_display->color565(120, 160, 255)
                                              : dma_display->color565(120, 200, 255);
          const int iconPad = iconEnabled ? 18 : 0; // space for 16px icon + padding
        for (int i = 0; i < _lineCount; ++i) {
            String raw = _lines[i];
            int colon = raw.indexOf(':');
            String label = (colon >= 0) ? raw.substring(0, colon + 1) : raw;
            String value = (colon >= 0) ? raw.substring(colon + 1) : "";
            label.trim();
            value.trim();
            const uint8_t *iconPtr = nullptr;
            uint16_t iconColor = 0;
            // Parse trailing "[icon=...]" hint if present to pick the proper bitmap/color
            int hint = value.lastIndexOf("[icon=");
            if (hint >= 0) {
                int end = value.indexOf(']', hint);
                if (end > hint) {
                      String code = value.substring(hint + 6, end);
                      value.remove(hint); // drop hint from visible text
                      code.trim();
                      if (iconEnabled)
                      {
                          iconPtr = code.length() ? getWeatherIconFromCondition(code) : nullptr;
                          iconColor = code.length() ? getIconColorFromCondition(code) : 0;
                      }
                  }
              }
            // Extract temps (before double space) and condition text (after)
            String temps = value;
            String condText;
            int split = value.indexOf("  ");
            if (split >= 0) {
                temps = value.substring(0, split);
                condText = value.substring(split + 2);
              }
              temps.trim();
              condText.trim();
               if (linesPerDay == 2 && condText.length())
               {
                   temps += "  " + condText;
                   condText = "";
               }

               // Add 1px visual gap between blocks by cumulatively offsetting each day block.
               const int blockYOffset = i;

            // Line 1: day label (e.g., 12/13:) with icon anchored at left
            if (label.length()) {
                   vec.push_back(label);
                   colors.push_back(labelColor);
                   offsets.push_back(0);      // icon at x=0, text will shift to iconPad in renderer
                   yOffsets.push_back(blockYOffset);
                   icons.push_back(iconEnabled ? iconPtr : nullptr);
                   iconColors.push_back(iconEnabled ? iconColor : 0);
               }
            // Line 2: temps (aligned with text to the right of the icon)
            vec.push_back(temps.length() ? temps : String("--/--"));
            colors.push_back(valueColor);
            offsets.push_back(iconPad); // keep temps aligned to the right of icon
            yOffsets.push_back(blockYOffset + ((linesPerDay == 2) ? 1 : 0)); // last line down 1px in 2-line mode
            icons.push_back(nullptr);
            iconColors.push_back(0);

               // Line 3: condition text (rain chance, etc.), no icon
               if (linesPerDay >= 3)
               {
                   vec.push_back(condText.length() ? condText : String(""));
                   colors.push_back(valueColor);
                   offsets.push_back(0);
                   yOffsets.push_back(blockYOffset + 1); // last line down 1px in 3-line mode
                   icons.push_back(nullptr);
                   iconColors.push_back(0);
               }
           }
        if (vec.empty()) {
            vec.push_back("No forecast data");
            colors.push_back(valueColor);
            offsets.push_back(0);
            yOffsets.push_back(0);
            icons.push_back(nullptr);
            iconColors.push_back(0);
        }
        s_dailyRoll.setLines(vec, resetPosition);
        s_dailyRoll.setLineColors(colors);
        s_dailyRoll.setLineOffsets(offsets);
        s_dailyRoll.setLineYOffsets(yOffsets);
        s_dailyRoll.setLineIcons(icons, iconColors);
        s_dailyRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_dailyRoll.setGapHoldMs(0); // no blank gap; let the block roll continuously
           s_dailyRoll.setExitHoldMs(constrain(forecastPauseMs, 0, 10000));
        // Treat each day as a block; include icon height so the whole icon stays visible during pause
           int blockPx = max(iconEnabled ? 16 : 0, InfoScreen::CHARH * linesPerDay + 1);
        s_dailyRoll.setBlockSizePx(blockPx);
        s_dailyRoll.setEntryExit(InfoScreen::SCREEN_HEIGHT, InfoScreen::CHARH); // enter from bottom, exit under title
    }
    else if (_screenMode == SCREEN_UDP_DATA) {
        // Live Weather screen also uses vertical scroll-up like hourly
        std::vector<String> vec;
        std::vector<uint16_t> colors;
        std::vector<int> offsets;
        vec.reserve(_lineCount * 2);
        const bool monoTheme = (theme == 1);
        const uint16_t labelColor = monoTheme ? dma_display->color565(220, 220, 120)
                                              : dma_display->color565(255, 240, 140);
        const uint16_t valueColor = monoTheme ? dma_display->color565(120, 160, 255)
                                              : dma_display->color565(120, 200, 255);
        const int valueIndentPx = 4;
        for (int i = 0; i < _lineCount; ++i) {
            String raw = _lines[i];
            int colon = raw.indexOf(':');
            String label = (colon >= 0) ? raw.substring(0, colon + 1) : raw;
            String value = (colon >= 0) ? raw.substring(colon + 1) : "";
            label.trim();
            value.trim();
            if (label.length()) {
                vec.push_back(label);
                colors.push_back(labelColor);
                offsets.push_back(0);
            }
            if (value.length()) {
                auto wrapped = wrapToWidth(value, InfoScreen::SCREEN_WIDTH - valueIndentPx);
                for (const auto &w : wrapped) {
                    vec.push_back(w);
                    colors.push_back(valueColor);
                    offsets.push_back(valueIndentPx);
                }
            }
        }
        if (vec.empty()) {
            vec.push_back("No live data");
            colors.push_back(valueColor);
            offsets.push_back(0);
        }
        s_liveRoll.setLines(vec, resetPosition);
        s_liveRoll.setLineColors(colors);
        s_liveRoll.setLineOffsets(offsets);
        s_liveRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_liveRoll.setEntryExit(InfoScreen::SCREEN_HEIGHT, InfoScreen::CHARH);
    }
    else if (_screenMode == SCREEN_CURRENT) {
        // Current conditions as vertical scroll-up
        std::vector<String> vec;
        std::vector<uint16_t> colors;
        std::vector<int> offsets;
        vec.reserve(_lineCount * 2);
        const bool monoTheme = (theme == 1);
        const uint16_t labelColor = monoTheme ? dma_display->color565(220, 220, 120)
                                              : dma_display->color565(255, 240, 140);
        const uint16_t valueColor = monoTheme ? dma_display->color565(120, 160, 255)
                                              : dma_display->color565(120, 200, 255);
        const int valueIndentPx = 4;
        for (int i = 0; i < _lineCount; ++i) {
            String raw = _lines[i];
            int colon = raw.indexOf(':');
            String label = (colon >= 0) ? raw.substring(0, colon + 1) : raw;
            String value = (colon >= 0) ? raw.substring(colon + 1) : "";
            label.trim();
            value.trim();
            if (label.length()) {
                vec.push_back(label);
                colors.push_back(labelColor);
                offsets.push_back(0);
            }
            if (value.length()) {
                auto wrapped = wrapToWidth(value, InfoScreen::SCREEN_WIDTH - valueIndentPx);
                for (const auto &w : wrapped) {
                    vec.push_back(w);
                    colors.push_back(valueColor);
                    offsets.push_back(valueIndentPx);
                }
            }
        }
        if (vec.empty()) {
            vec.push_back("No current data");
            colors.push_back(valueColor);
            offsets.push_back(0);
        }
        s_currentRoll.setLines(vec, resetPosition);
        s_currentRoll.setLineColors(colors);
        s_currentRoll.setLineOffsets(offsets);
        s_currentRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_currentRoll.setEntryExit(InfoScreen::SCREEN_HEIGHT, InfoScreen::CHARH);
    }
    else if (_screenMode == SCREEN_NOAA_ALERT) {
        // NOAA alert: show all fields with vertical roll-up
        std::vector<String> vec;
        std::vector<uint16_t> colors;
        std::vector<int> offsets;
        vec.reserve(_lineCount * 2);
        const bool monoTheme = (theme == 1);
        const uint16_t labelColor = monoTheme ? dma_display->color565(220, 220, 120)
                                              : dma_display->color565(255, 240, 140);
        const uint16_t valueColor = monoTheme ? dma_display->color565(120, 160, 255)
                                              : dma_display->color565(120, 200, 255);
        const int valueIndentPx = 4;
        auto pushValueWrapped = [&](const String &text) {
            auto wrapped = wrapToWidth(text, InfoScreen::SCREEN_WIDTH - valueIndentPx);
            for (const auto &w : wrapped) {
                vec.push_back(w);
                colors.push_back(valueColor);
                offsets.push_back(valueIndentPx);
            }
        };
        for (int i = 0; i < _lineCount; ++i) {
            String raw = _lines[i];
            int colon = raw.indexOf(':');
            String label = (colon >= 0) ? raw.substring(0, colon + 1) : raw;
            String value = (colon >= 0) ? raw.substring(colon + 1) : "";
            label.trim();
            value.trim();
            if (label.length()) {
                vec.push_back(label);
                colors.push_back(labelColor);
                offsets.push_back(0);
            }
            if (value.length()) {
                pushValueWrapped(value);
            }
        }
        if (vec.empty()) {
            vec.push_back("No alert data");
            colors.push_back(valueColor);
            offsets.push_back(0);
        }
        s_noaaRoll.setLines(vec, resetPosition);
        s_noaaRoll.setLineColors(colors);
        s_noaaRoll.setLineOffsets(offsets);
        s_noaaRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_noaaRoll.setEntryExit(InfoScreen::SCREEN_HEIGHT, InfoScreen::CHARH);
    }
}

void InfoScreen::setHighlightEnabled(bool enabled)
{
    _highlightEnabled = enabled;
}

void InfoScreen::setLineOverlay(LineOverlayFn fn)
{
    _lineOverlay = fn;
}

void InfoScreen::setSelectedLine(int index)
{
    if (_lineCount <= 0)
        return;

    if (index < 0)
        index = 0;
    if (index >= _lineCount)
        index = _lineCount - 1;

    int currentSelectedGlobal = -1;
    if (selIndex >= 0 && selIndex < INFOSCREEN_VISIBLE_ROWS)
    {
        int visibleLines = min(_lineCount - scrollY, INFOSCREEN_VISIBLE_ROWS);
        if (selIndex < visibleLines && visibleLines > 0)
        {
            currentSelectedGlobal = scrollY + selIndex;
        }
    }

    if (currentSelectedGlobal == index)
    {
        return;
    }

    // Ensure selected line is within the current view window
    if (index < scrollY)
    {
        scrollY = index;
    }
    else
    {
        int maxVisibleIndex = scrollY + INFOSCREEN_VISIBLE_ROWS - 1;
        if (index > maxVisibleIndex)
        {
            scrollY = index - (INFOSCREEN_VISIBLE_ROWS - 1);
            if (scrollY < 0)
                scrollY = 0;
        }
    }

    selIndex = index - scrollY;
    if (selIndex < 0)
        selIndex = 0;
    if (selIndex >= INFOSCREEN_VISIBLE_ROWS)
        selIndex = INFOSCREEN_VISIBLE_ROWS - 1;

    lastSelIndex = -1; // force redraw/scroll reset on next tick
}


void InfoScreen::show(void (*onExit)()) {
    _active = true; _onExit = onExit;
    scrollY = 0; selIndex = 0; lastSelIndex = -1;
    resetHScroll();
}
void InfoScreen::hide() { _active = false; if (_onExit) _onExit(); resetHScroll(); }
bool InfoScreen::isActive() const { return _active; }

void InfoScreen::resetHScroll() {
    for (int i = 0; i < INFOSCREEN_VISIBLE_ROWS; ++i) {
        scrollOffsets[i] = 0;
        lastScrollTimes[i] = millis();
    }
    firstScroll = true;
    scrollPaused = false;
    scrollPauseTime = 0;
}

void InfoScreen::draw() {
    const bool monoTheme = (theme == 1);
    const bool isNoaa = (_screenMode == SCREEN_NOAA_ALERT);
    uint16_t headerBg = monoTheme ? dma_display->color565(20,20,40) : INFOSCREEN_HEADERBG;
    uint16_t headerFg = monoTheme ? dma_display->color565(60,60,120) : INFOSCREEN_HEADERFG;
    if (isNoaa && noaaHasActiveAlert())
    {
        uint16_t alertColor = noaaActiveColor();
        if (alertColor != 0)
            headerFg = alertColor;
    }
    const uint16_t defaultLineColor = monoTheme ? dma_display->color565(60,60,120)
                                                : dma_display->color565(230,230,230);
    const bool isVerticalScrollScreen =
        (_screenMode == SCREEN_HOURLY) ||
        (_screenMode == SCREEN_UDP_DATA) ||
        (_screenMode == SCREEN_UDP_FORECAST) ||
        (_screenMode == SCREEN_CURRENT) ||
        (_screenMode == SCREEN_NOAA_ALERT);

    // Avoid full-screen clears on vertical scrollers to reduce flicker; other screens still clear.
    if (!isVerticalScrollScreen) {
        dma_display->fillScreen(0);
    }

    // Header renderer so we can repaint after the scroller to mask any overshoot
    const int headerHeight = CHARH;
    auto drawHeader = [&]() {
        dma_display->fillRect(0, 0, SCREEN_WIDTH, headerHeight, headerBg);
        dma_display->setTextColor(headerFg, headerBg);
        dma_display->setCursor(1, 0);
        String t = _title; if (t.length() > 12) t = t.substring(0, 12);
        dma_display->print(t);
        const uint16_t underlineColor = monoTheme ? dma_display->color565(18, 18, 40)
                                                  : dma_display->color565(12, 40, 80);
        dma_display->drawFastHLine(0, headerHeight - 1, SCREEN_WIDTH, underlineColor);
    };

    // Special path: WeatherFlow hourly screen uses vertical scroll-up display
    if (_screenMode == SCREEN_HOURLY) {
        int bodyH = InfoScreen::SCREEN_HEIGHT - headerHeight;
        // Blank the header band to ensure any stray pixels are cleared before redraw
        dma_display->fillRect(0, headerHeight, InfoScreen::SCREEN_WIDTH, bodyH, 0);
        // Clear the separator row so any stray pixels under the title are zeroed
        dma_display->fillRect(0, headerHeight - 1, InfoScreen::SCREEN_WIDTH, 1, 0);
        s_hourlyRoll.draw(*dma_display, 0, headerHeight, bodyH, defaultLineColor);
        // Redraw header after scrolling so the title never gets contaminated by partial lines
        drawHeader();
        return;
    }
    // Live Weather screen: vertical scroll-up body
    if (_screenMode == SCREEN_UDP_DATA) {
        int bodyH = InfoScreen::SCREEN_HEIGHT - headerHeight;
        dma_display->fillRect(0, headerHeight, InfoScreen::SCREEN_WIDTH, bodyH, 0);
        dma_display->fillRect(0, headerHeight - 1, InfoScreen::SCREEN_WIDTH, 1, 0);
        s_liveRoll.draw(*dma_display, 0, headerHeight, bodyH, defaultLineColor);
        drawHeader();
        return;
    }
    if (_screenMode == SCREEN_CURRENT) {
        int bodyH = InfoScreen::SCREEN_HEIGHT - headerHeight;
        dma_display->fillRect(0, headerHeight, InfoScreen::SCREEN_WIDTH, bodyH, 0);
        dma_display->fillRect(0, headerHeight - 1, InfoScreen::SCREEN_WIDTH, 1, 0);
        s_currentRoll.draw(*dma_display, 0, headerHeight, bodyH, defaultLineColor);
        drawHeader();
        return;
    }
    if (_screenMode == SCREEN_NOAA_ALERT) {
        int bodyH = InfoScreen::SCREEN_HEIGHT - headerHeight;
        dma_display->fillRect(0, headerHeight, InfoScreen::SCREEN_WIDTH, bodyH, 0);
        dma_display->fillRect(0, headerHeight - 1, InfoScreen::SCREEN_WIDTH, 1, 0);
        s_noaaRoll.draw(*dma_display, 0, headerHeight, bodyH, defaultLineColor);
        drawHeader();
        return;
    }
    // Special path: 7-day forecast uses the same vertical scroll-up treatment
    if (_screenMode == SCREEN_UDP_FORECAST) {
        int bodyH = InfoScreen::SCREEN_HEIGHT - headerHeight;
        dma_display->fillRect(0, headerHeight, InfoScreen::SCREEN_WIDTH, bodyH, 0);
        dma_display->fillRect(0, headerHeight - 1, InfoScreen::SCREEN_WIDTH, 1, 0);
        s_dailyRoll.draw(*dma_display, 0, headerHeight, bodyH, defaultLineColor);
        drawHeader();
        return;
    }

    // Standard screens: draw header once before rendering lines
    drawHeader();

    // Lines
    int y = headerHeight;
    int pageLines = min(_lineCount - scrollY, INFOSCREEN_VISIBLE_ROWS);

    for (int i = 0; i < pageLines; ++i) {
        int idx = scrollY + i;
        String line = _lines[idx];

        int lineW = getTextWidth(line.c_str());
        bool isSelected = (i == selIndex);
        bool highlightLine = _highlightEnabled && isSelected;
        uint16_t baseColor = _lineColorUsed[idx] ? _lineColors[idx] : defaultLineColor;
        uint16_t valueColor;
        if (highlightLine)
        {
            if (_lineColorUsed[idx])
            {
                valueColor = brightenColor(baseColor);
            }
            else
            {
                valueColor = monoTheme ? dma_display->color565(120, 120, 200)
                                       : dma_display->color565(255, 255, 0);
            }
        }
        else
        {
            valueColor = baseColor;
        }

        const uint16_t defaultLabelColor = monoTheme ? dma_display->color565(70, 70, 110)
                                                     : dma_display->color565(130, 150, 200);
        uint16_t labelColor;
        if (highlightLine)
        {
            labelColor = monoTheme ? dma_display->color565(140, 140, 220)
                                   : dma_display->color565(255, 255, 180);
        }
        else
        {
            labelColor = defaultLabelColor;
        }

        int yPos = headerHeight + i * CHARH;

        int colonPos = line.indexOf(':');
        if (colonPos >= 0)
        {
            String labelPart = line.substring(0, colonPos + 1);
            String valuePart = line.substring(colonPos + 1);
            int labelWidth = getTextWidth(labelPart.c_str());
            int valueWidth = getTextWidth(valuePart.c_str());
            int totalWidth = labelWidth + valueWidth;

            if (totalWidth <= SCREEN_WIDTH)
            {
                dma_display->setTextColor(labelColor);
                dma_display->setCursor(0, yPos);
                dma_display->print(labelPart);
                dma_display->setTextColor(valueColor);
                dma_display->setCursor(labelWidth, yPos);
                dma_display->print(valuePart);
            }
            else if (highlightLine)
            {
                int cursorX = SCREEN_WIDTH - scrollOffsets[i];
                if (cursorX + totalWidth > 0 && cursorX < SCREEN_WIDTH)
                {
                    // Draw label segment
                    dma_display->setTextColor(labelColor);
                    dma_display->setCursor(cursorX, yPos);
                    dma_display->print(labelPart);
                    // Draw value segment
                    dma_display->setTextColor(valueColor);
                    dma_display->setCursor(cursorX + labelWidth, yPos);
                    dma_display->print(valuePart);
                }
            }
            else
            {
                dma_display->setTextColor(labelColor);
                dma_display->setCursor(0, yPos);
                dma_display->print(labelPart);
                dma_display->setTextColor(valueColor);
                dma_display->setCursor(labelWidth, yPos);
                dma_display->print(valuePart);
            }
        }
        else
        {
            if (lineW <= SCREEN_WIDTH) {
                dma_display->setTextColor(valueColor);
                dma_display->setCursor(0, yPos);
                dma_display->print(line);
            } else if (highlightLine) {
                int cursorX = SCREEN_WIDTH - scrollOffsets[i];
                if (cursorX + lineW > 0 && cursorX < SCREEN_WIDTH) {
                    dma_display->setTextColor(valueColor);
                    dma_display->setCursor(cursorX, yPos);
                    dma_display->print(line);
                }
            } else {
                // Too long & not selected: print entire line, let display handle overflow
                dma_display->setTextColor(valueColor);
                dma_display->setCursor(0, yPos);
                dma_display->print(line);
            }
        }
        if (_lineOverlay)
        {
            _lineOverlay(idx, yPos, highlightLine);
        }
    }
}

void InfoScreen::tick() {
    if (!_active) return;

    if (_screenMode == SCREEN_HOURLY) {
        s_hourlyRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_hourlyRoll.update();
        draw();
        return;
    }
    if (_screenMode == SCREEN_UDP_DATA) {
        s_liveRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_liveRoll.update();
        draw();
        return;
    }
    if (_screenMode == SCREEN_CURRENT) {
        s_currentRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_currentRoll.update();
        draw();
        return;
    }
    if (_screenMode == SCREEN_NOAA_ALERT) {
        s_noaaRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_noaaRoll.update();
        draw();
        return;
    }
    if (_screenMode == SCREEN_UDP_FORECAST) {
        s_dailyRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_dailyRoll.update();
        draw();
        return;
    }

    unsigned long now = millis();
    int pageLines = min(_lineCount - scrollY, INFOSCREEN_VISIBLE_ROWS);

    if (selIndex >= pageLines) selIndex = 0;

    if (lastSelIndex != selIndex) {
        resetHScroll();
        lastSelIndex = selIndex;
    }

    // Only scroll the selected line if too long
    for (int i = 0; i < pageLines; ++i) {
        int idx = scrollY + i;
        String line = _lines[idx];
        int lineW = getTextWidth(line.c_str());

        int &scrollOffset = scrollOffsets[i];
        unsigned long &lastScrollTime = lastScrollTimes[i];

        if (i == selIndex && lineW > SCREEN_WIDTH) {
            int cursorX = SCREEN_WIDTH - scrollOffset;
            if (now - lastScrollTime > (unsigned)scrollSpeed) {
                scrollOffset++;
                lastScrollTime = now;
                if (cursorX + lineW < 0) {
                    scrollOffset = 0;
                }
            }
        } else {
            scrollOffset = 0;
        }
    }
    draw();
}

void InfoScreen::handleIR(uint32_t code) {
    if (!_active) return;

    auto verticalScroller = [&]() -> RollingUpScreen* {
        if (_screenMode == SCREEN_HOURLY) return &s_hourlyRoll;
        if (_screenMode == SCREEN_UDP_FORECAST) return &s_dailyRoll;
        if (_screenMode == SCREEN_UDP_DATA) return &s_liveRoll;
        if (_screenMode == SCREEN_CURRENT) return &s_currentRoll;
        if (_screenMode == SCREEN_NOAA_ALERT) return &s_noaaRoll;
        return nullptr;
    };

    if (RollingUpScreen* scroller = verticalScroller()) {
        if (code == IR_DOWN) { scroller->onDownPress(); return; }
        if (code == IR_UP)   { scroller->onUpPress();   return; }
    }

    int pageLines = min(_lineCount - scrollY, INFOSCREEN_VISIBLE_ROWS);

    if (code == IR_UP) {
        if (selIndex > 0) {
            selIndex--;
        } else if (scrollY > 0) {
            scrollY--;
        } else {
            scrollY = max(0, _lineCount - INFOSCREEN_VISIBLE_ROWS);
            selIndex = min(INFOSCREEN_VISIBLE_ROWS - 1, _lineCount - 1);
        }
        resetHScroll(); draw(); return;
    }
    if (code == IR_DOWN) {
        if (selIndex < pageLines - 1 && (scrollY + selIndex) < (_lineCount - 1)) {
            selIndex++;
        } else if ((scrollY + INFOSCREEN_VISIBLE_ROWS) < _lineCount) {
            scrollY++;
        } else {
            scrollY = 0; selIndex = 0;
        }
        resetHScroll(); draw(); return;
    }
    if (code == IR_LEFT || code == IR_RIGHT) {
        int direction = (code == IR_LEFT) ? -1 : 1;
        ScreenMode next = nextAllowedScreen(_screenMode, direction);
        next = enforceAllowedScreen(next);
        hide();
        currentScreen = next;
        return;
    }
    draw();
}
