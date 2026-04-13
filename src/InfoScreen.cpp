#include "InfoScreen.h"
#include "InfoModal.h"
#include "display.h"
#include "utils.h"
#include "RollingUpScreen.h"
#include "noaa.h"
#include "settings.h"
#include "ui_theme.h"
#include "screen_manager.h"

extern int scrollSpeed;
extern int verticalScrollSpeed;
extern int theme;

static RollingUpScreen s_hourlyRoll(InfoScreen::SCREEN_WIDTH, InfoScreen::SCREEN_HEIGHT - InfoScreen::CHARH, InfoScreen::CHARH);
static RollingUpScreen s_dailyRoll(InfoScreen::SCREEN_WIDTH, InfoScreen::SCREEN_HEIGHT - InfoScreen::CHARH, InfoScreen::CHARH);
static RollingUpScreen s_liveRoll(InfoScreen::SCREEN_WIDTH, InfoScreen::SCREEN_HEIGHT - InfoScreen::CHARH, InfoScreen::CHARH);
static RollingUpScreen s_lightningRoll(InfoScreen::SCREEN_WIDTH, InfoScreen::SCREEN_HEIGHT - InfoScreen::CHARH, InfoScreen::CHARH);
static RollingUpScreen s_currentRoll(InfoScreen::SCREEN_WIDTH, InfoScreen::SCREEN_HEIGHT - InfoScreen::CHARH, InfoScreen::CHARH);
static std::vector<String> s_hourlyHeaders;
static std::vector<String> s_dailyHeaders;

namespace
{
bool hourlyLineNeedsMarquee(const String &text)
{
    if (text.length() == 0)
        return false;
    return text.length() > 10;
}

uint16_t hourlyAccentColor(const String &label, uint16_t fallback)
{
    String lower = label;
    lower.toLowerCase();

    if (lower.indexOf("rain") >= 0 || lower.indexOf("shower") >= 0 || lower.indexOf("storm") >= 0)
        return (theme == 1) ? dma_display->color565(150, 190, 230) : dma_display->color565(105, 220, 255);
    if (lower.indexOf("wind") >= 0 || lower.indexOf("gust") >= 0 || lower.indexOf("breez") >= 0)
        return (theme == 1) ? dma_display->color565(220, 200, 130) : dma_display->color565(255, 210, 90);
    if (lower.indexOf("hot") >= 0)
        return (theme == 1) ? dma_display->color565(235, 175, 120) : dma_display->color565(255, 155, 90);
    if (lower.indexOf("cold") >= 0 || lower.indexOf("cool") >= 0)
        return (theme == 1) ? dma_display->color565(160, 205, 235) : dma_display->color565(120, 210, 255);
    return fallback;
}

uint16_t softenIconPlate(uint16_t color)
{
    if (color == 0)
        return 0;
    uint8_t r = ((color >> 11) & 0x1F) * 255 / 31;
    uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
    uint8_t b = (color & 0x1F) * 255 / 31;
    r = static_cast<uint8_t>(max(8, r / 5));
    g = static_cast<uint8_t>(max(8, g / 5));
    b = static_cast<uint8_t>(max(8, b / 5));
    return dma_display->color565(r, g, b);
}

uint16_t softenIconBorder(uint16_t color)
{
    if (color == 0)
        return 0;
    uint8_t r = ((color >> 11) & 0x1F) * 255 / 31;
    uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
    uint8_t b = (color & 0x1F) * 255 / 31;
    r = static_cast<uint8_t>(max(12, (r * 2) / 5));
    g = static_cast<uint8_t>(max(12, (g * 2) / 5));
    b = static_cast<uint8_t>(max(12, (b * 2) / 5));
    return dma_display->color565(r, g, b);
}
} // namespace


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
        std::vector<uint16_t> iconBgColors;
        std::vector<uint16_t> iconBorderColors;
        std::vector<uint8_t> marqueeFlags;
        s_hourlyHeaders.clear();
        s_hourlyHeaders.reserve(_lineCount);
        const size_t rowCount = static_cast<size_t>(_lineCount) * 3u;
        vec.reserve(rowCount);
        colors.reserve(rowCount);
        offsets.reserve(rowCount);
        yOffsets.reserve(rowCount);
        icons.reserve(rowCount);
        iconColors.reserve(rowCount);
        iconBgColors.reserve(rowCount);
        iconBorderColors.reserve(rowCount);
        marqueeFlags.reserve(rowCount);
        for (int i = 0; i < _lineCount; ++i) {
            // Split on '\n' to allow fixed block entries
            const bool monoTheme = (theme == 1);
            const uint16_t labelColor = monoTheme ? ui_theme::infoLabelMono() : ui_theme::infoLabelDay();
            const uint16_t defaultValueColor = monoTheme ? ui_theme::infoValueMono() : ui_theme::infoValueDay();
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
            const uint16_t valueColor = hourlyAccentColor(line3.length() ? line3 : line2, defaultValueColor);
            const uint16_t detailColor = hourlyAccentColor(line3, defaultValueColor);

            const uint8_t *iconPtr = nullptr;
            uint16_t iconClr = 0;
            // Parse icon hint from line1 only
            int hint = line1.lastIndexOf("[icon=");
            if (hint >= 0) {
                int end = line1.indexOf(']', hint);
                if (end > hint) {
                    String code = line1.substring(hint + 6, end);
                    line1.remove(hint);
                    line1.trim();
                    code.trim();
                    if (iconEnabled && code.length()) {
                        iconPtr = getWeatherIconFromCondition(code);
                        iconClr = getIconColorFromCondition(code);
                    }
                }
            }
            s_hourlyHeaders.push_back(line1.length() ? line1 : String("Hour"));

            // Keep hourly blocks exactly 24px tall (3 rows * 8px) so each page fits the screen.
            const int blockYOffset = 0;

            // Line 1: keep only icon row; hour text is moved to header title.
            vec.push_back(" ");
            colors.push_back(labelColor);
            offsets.push_back(1);
            yOffsets.push_back(blockYOffset);
            icons.push_back(iconEnabled ? iconPtr : nullptr);
            iconColors.push_back(iconEnabled ? iconClr : 0);
            iconBgColors.push_back(iconEnabled && iconPtr != nullptr ? softenIconPlate(iconClr) : 0);
            iconBorderColors.push_back(iconEnabled && iconPtr != nullptr ? softenIconBorder(iconClr) : 0);
            marqueeFlags.push_back(0);

            // Line 2: data line aligned to the right of icon
            vec.push_back(String("[big] ") + (line2.length() ? line2 : String("--")));
            colors.push_back(valueColor);
            offsets.push_back(valueIndentPx + iconPad);
            yOffsets.push_back(blockYOffset);
            icons.push_back(nullptr);
            iconColors.push_back(0);
            iconBgColors.push_back(0);
            iconBorderColors.push_back(0);
            marqueeFlags.push_back(0);

            // Line 3: condition line (optional) aligned to the right of icon; marquee-enabled like 10-day page.
            vec.push_back(line3.length() ? line3 : String(""));
            colors.push_back(detailColor);
            offsets.push_back(0);
            yOffsets.push_back(blockYOffset + 1); // move last line down 1px
            icons.push_back(nullptr);
            iconColors.push_back(0);
            iconBgColors.push_back(0);
            iconBorderColors.push_back(0);
            marqueeFlags.push_back(hourlyLineNeedsMarquee(line3) ? 1 : 0);
        }
        s_hourlyRoll.setLines(vec, resetPosition);
        s_hourlyRoll.setLineColors(colors);
        s_hourlyRoll.setLineOffsets(offsets);
        s_hourlyRoll.setLineYOffsets(yOffsets);
        s_hourlyRoll.setLineIcons(icons, iconColors);
        s_hourlyRoll.setLineIconBgColors(iconBgColors);
        s_hourlyRoll.setLineIconBorderColors(iconBorderColors);
        s_hourlyRoll.setLineMarqueeFlags(marqueeFlags);
        s_hourlyRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_hourlyRoll.setEntryExit(InfoScreen::SCREEN_HEIGHT, InfoScreen::CHARH); // enter from bottom, exit just under title
        s_hourlyRoll.setGapHoldMs(0);
        s_hourlyRoll.setExitHoldMs(constrain(forecastPauseMs, 0, 10000));
        s_hourlyRoll.setBlockSizePx(InfoScreen::SCREEN_HEIGHT - InfoScreen::CHARH); // one full hourly page per body
    }
    else if (_screenMode == SCREEN_UDP_FORECAST) {
        std::vector<String> vec;
        std::vector<uint16_t> colors;
        std::vector<int> offsets;
        std::vector<int> iconOffsets;
        std::vector<int> yOffsets;
        std::vector<const uint8_t *> icons;
        std::vector<uint16_t> iconColors;
        std::vector<uint8_t> marqueeFlags;
        const size_t rowCount = static_cast<size_t>(_lineCount) * 3u;
        vec.reserve(rowCount);
        colors.reserve(rowCount);
        offsets.reserve(rowCount);
        iconOffsets.reserve(rowCount);
        yOffsets.reserve(rowCount);
        icons.reserve(rowCount);
        iconColors.reserve(rowCount);
        marqueeFlags.reserve(rowCount);
        s_dailyHeaders.clear();
        s_dailyHeaders.reserve(_lineCount);
        const bool iconEnabled = (forecastIconSize == 16);
        const bool monoTheme = (theme == 1);
        const uint16_t highColor = monoTheme ? dma_display->color565(220, 220, 120) : dma_display->color565(255, 190, 110);
        const uint16_t lowColor  = monoTheme ? dma_display->color565(150, 185, 230) : dma_display->color565(120, 200, 255);
        const uint16_t detailColor = monoTheme ? ui_theme::infoValueMono() : ui_theme::infoValueDay();
        const int iconPad = iconEnabled ? 18 : 0;
        for (int i = 0; i < _lineCount; ++i) {
            String raw = _lines[i];
            int firstNl = raw.indexOf('\n');
            int secondNl = (firstNl >= 0) ? raw.indexOf('\n', firstNl + 1) : -1;
            int thirdNl = (secondNl >= 0) ? raw.indexOf('\n', secondNl + 1) : -1;
            String dateLine = (firstNl >= 0) ? raw.substring(0, firstNl) : raw;
            String hiLine = (firstNl >= 0) ? (secondNl >= 0 ? raw.substring(firstNl + 1, secondNl) : raw.substring(firstNl + 1)) : "";
            String lowLine = (secondNl >= 0) ? (thirdNl >= 0 ? raw.substring(secondNl + 1, thirdNl) : raw.substring(secondNl + 1)) : "";
            String detailLine = (thirdNl >= 0) ? raw.substring(thirdNl + 1) : "";
            dateLine.trim();
            hiLine.trim();
            lowLine.trim();
            detailLine.trim();
            if (!dateLine.length())
                dateLine = "Forecast";
            s_dailyHeaders.push_back(dateLine);

            // Parse trailing "[icon=...]" hint on hiLine and remove it from visible text.
            const uint8_t *iconPtr = nullptr;
            uint16_t iconClr = 0;
            int hint = hiLine.lastIndexOf("[icon=");
            if (hint >= 0) {
                int end = hiLine.indexOf(']', hint);
                if (end > hint) {
                      String code = hiLine.substring(hint + 6, end);
                      hiLine.remove(hint);
                      hiLine.trim();
                      code.trim();
                      if (iconEnabled)
                      {
                          iconPtr = code.length() ? getWeatherIconFromCondition(code) : nullptr;
                          iconClr = code.length() ? getIconColorFromCondition(code) : 0;
                      }
                  }
                }

            auto measureForecastValueWidth = [&](const String &rawLine) -> int {
                String visible = rawLine;
                int arrowW = 0;
                if (visible.startsWith("[up]"))
                {
                    visible.remove(0, 4);
                    visible.trim();
                    arrowW = 8;
                }
                else if (visible.startsWith("[down]"))
                {
                    visible.remove(0, 6);
                    visible.trim();
                    arrowW = 8;
                }
                int16_t tx1 = 0;
                int16_t ty1 = 0;
                uint16_t tw = 0;
                uint16_t th = 0;
                dma_display->getTextBounds(visible.c_str(), 0, 0, &tx1, &ty1, &tw, &th);
                int drawW = static_cast<int>(tw) + arrowW;
                int degPos = visible.lastIndexOf('\xB0');
                if (degPos >= 0)
                {
                    int unitStart = degPos + 1;
                    while (unitStart < visible.length() && visible[unitStart] == ' ')
                        unitStart++;
                    if (unitStart < visible.length())
                        drawW -= 6; // matches unit kerning shift in RollingUpScreen
                }
                if (drawW < 1)
                    drawW = 1;
                return drawW;
            };

            const int hiDrawW = measureForecastValueWidth(hiLine);
            const int lowDrawW = measureForecastValueWidth(lowLine);
            const int laneW = InfoScreen::SCREEN_WIDTH - iconPad;
            int hiX = iconPad;
            int lowX = iconPad;
            if (laneW > hiDrawW)
            {
                hiX = iconPad + (laneW - hiDrawW) / 2;
            }
            if (laneW > lowDrawW)
            {
                lowX = iconPad + (laneW - lowDrawW) / 2;
            }

            vec.push_back(hiLine.length() ? hiLine : String("[up] --\xB0 -"));
            colors.push_back(highColor);
            offsets.push_back(hiX);
            iconOffsets.push_back(1);
            yOffsets.push_back(i);
            icons.push_back(iconEnabled ? iconPtr : nullptr);
            iconColors.push_back(iconEnabled ? iconClr : 0);
            marqueeFlags.push_back(0);

            vec.push_back(lowLine.length() ? lowLine : String("[down] --\xB0 -"));
            colors.push_back(lowColor);
            offsets.push_back(lowX);
            iconOffsets.push_back(0);
            yOffsets.push_back(i + 1);
            icons.push_back(nullptr);
            iconColors.push_back(0);
            marqueeFlags.push_back(0);

            vec.push_back(detailLine.length() ? detailLine : String("No details"));
            colors.push_back(detailColor);
            offsets.push_back(0);
            iconOffsets.push_back(0);
            yOffsets.push_back(i + 1);
            icons.push_back(nullptr);
            iconColors.push_back(0);
            marqueeFlags.push_back(1);
        }
        if (vec.empty())
        {
            vec.push_back("No forecast data");
            colors.push_back(detailColor);
            offsets.push_back(0);
            iconOffsets.push_back(0);
            yOffsets.push_back(0);
            icons.push_back(nullptr);
            iconColors.push_back(0);
            marqueeFlags.push_back(0);
            s_dailyHeaders.push_back("Forecast");
        }
        s_dailyRoll.setLines(vec, resetPosition);
        s_dailyRoll.setLineColors(colors);
        s_dailyRoll.setLineOffsets(offsets);
        s_dailyRoll.setLineIconOffsets(iconOffsets);
        s_dailyRoll.setLineYOffsets(yOffsets);
        s_dailyRoll.setLineIcons(icons, iconColors);
        s_dailyRoll.setLineMarqueeFlags(marqueeFlags);
        s_dailyRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_dailyRoll.setEntryExit(InfoScreen::SCREEN_HEIGHT, InfoScreen::CHARH);
        s_dailyRoll.setGapHoldMs(0);
        s_dailyRoll.setExitHoldMs(constrain(forecastPauseMs, 0, 10000));
        s_dailyRoll.setBlockSizePx(max(iconEnabled ? 16 : 0, InfoScreen::CHARH * 3 + 1));
    }
    else if (_screenMode == SCREEN_UDP_DATA) {
        // Tempest outdoor conditions: match Current WX behavior
        std::vector<String> vec;
        std::vector<uint16_t> colors;
        std::vector<int> offsets;
        std::vector<uint8_t> marqueeFlags;
        const size_t rowCount = static_cast<size_t>(_lineCount) * 2u;
        vec.reserve(rowCount);
        colors.reserve(rowCount);
        offsets.reserve(rowCount);
        marqueeFlags.reserve(rowCount);
        const bool monoTheme = (theme == 1);
        const uint16_t labelColor = monoTheme ? ui_theme::infoLabelMono() : ui_theme::infoLabelDay();
        const uint16_t valueColor = monoTheme ? ui_theme::infoValueMono() : ui_theme::infoValueDay();
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
                marqueeFlags.push_back(0);
            }
            vec.push_back(value);
            colors.push_back(valueColor);
            offsets.push_back(valueIndentPx);
            marqueeFlags.push_back(value.length() ? 1 : 0);
        }
        if (vec.empty()) {
            vec.push_back("No live data");
            colors.push_back(valueColor);
            offsets.push_back(0);
            marqueeFlags.push_back(0);
        }
        s_liveRoll.setLines(vec, resetPosition);
        s_liveRoll.setLineColors(colors);
        s_liveRoll.setLineOffsets(offsets);
        s_liveRoll.setLineMarqueeFlags(marqueeFlags);
        s_liveRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_liveRoll.setEntryExit(InfoScreen::SCREEN_HEIGHT, InfoScreen::CHARH);
        s_liveRoll.setExitHoldMs(4000);
        s_liveRoll.setBlockSizePx(InfoScreen::CHARH * 2);
    }
    else if (_screenMode == SCREEN_LIGHTNING) {
        std::vector<String> vec;
        std::vector<uint16_t> colors;
        std::vector<int> offsets;
        std::vector<uint8_t> marqueeFlags;
        const size_t rowCount = static_cast<size_t>(_lineCount) * 2u;
        vec.reserve(rowCount);
        colors.reserve(rowCount);
        offsets.reserve(rowCount);
        marqueeFlags.reserve(rowCount);
        const bool monoTheme = (theme == 1);
        const uint16_t labelColor = monoTheme ? ui_theme::infoLabelMono() : ui_theme::infoLabelDay();
        const uint16_t valueColor = monoTheme ? ui_theme::infoValueMono() : ui_theme::infoValueDay();
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
                marqueeFlags.push_back(0);
            }
            vec.push_back(value);
            colors.push_back(valueColor);
            offsets.push_back(valueIndentPx);
            marqueeFlags.push_back(value.length() ? 1 : 0);
        }
        if (vec.empty()) {
            vec.push_back("No lightning");
            colors.push_back(valueColor);
            offsets.push_back(0);
            marqueeFlags.push_back(0);
        }
        s_lightningRoll.setLines(vec, resetPosition);
        s_lightningRoll.setLineColors(colors);
        s_lightningRoll.setLineOffsets(offsets);
        s_lightningRoll.setLineMarqueeFlags(marqueeFlags);
        s_lightningRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_lightningRoll.setEntryExit(InfoScreen::SCREEN_HEIGHT, InfoScreen::CHARH);
        s_lightningRoll.setExitHoldMs(3500);
        s_lightningRoll.setBlockSizePx(InfoScreen::CHARH * 2);
    }
    else if (_screenMode == SCREEN_CURRENT) {
        // Current conditions as vertical scroll-up
        std::vector<String> vec;
        std::vector<uint16_t> colors;
        std::vector<int> offsets;
        std::vector<uint8_t> marqueeFlags;
        const size_t rowCount = static_cast<size_t>(_lineCount) * 2u;
        vec.reserve(rowCount);
        colors.reserve(rowCount);
        offsets.reserve(rowCount);
        marqueeFlags.reserve(rowCount);
        const bool monoTheme = (theme == 1);
        const uint16_t labelColor = monoTheme ? ui_theme::infoLabelMono() : ui_theme::infoLabelDay();
        const uint16_t valueColor = monoTheme ? ui_theme::infoValueMono() : ui_theme::infoValueDay();
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
                marqueeFlags.push_back(0);
            }
            // Keep Current WX blocks to exactly label+value (2 lines) so
            // category hold points align consistently under the header.
            // Use marquee for long values instead of truncating.
            vec.push_back(value);
            colors.push_back(valueColor);
            offsets.push_back(valueIndentPx);
            marqueeFlags.push_back(value.length() ? 1 : 0);
        }
        if (vec.empty()) {
            vec.push_back("No current data");
            colors.push_back(valueColor);
            offsets.push_back(0);
            marqueeFlags.push_back(0);
        }
        s_currentRoll.setLines(vec, resetPosition);
        s_currentRoll.setLineColors(colors);
        s_currentRoll.setLineOffsets(offsets);
        s_currentRoll.setLineMarqueeFlags(marqueeFlags);
        s_currentRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_currentRoll.setEntryExit(InfoScreen::SCREEN_HEIGHT, InfoScreen::CHARH);
        // Pause each category block below the title before advancing.
        s_currentRoll.setExitHoldMs(4000);
        s_currentRoll.setBlockSizePx(InfoScreen::CHARH * 2);
    }
    else if (_screenMode == SCREEN_NOAA_ALERT) {
        // NOAA uses the dedicated paged renderer in display_noaa.cpp.
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
    uint16_t headerBg = monoTheme ? ui_theme::monoHeaderBg() : INFOSCREEN_HEADERBG;
    uint16_t headerFg = monoTheme ? ui_theme::monoHeaderFg() : INFOSCREEN_HEADERFG;
    if (isNoaa && noaaHasActiveAlert())
    {
        uint16_t alertColor = noaaActiveColor();
        if (alertColor != 0)
            headerFg = alertColor;
    }
    const uint16_t defaultLineColor = monoTheme ? ui_theme::monoHeaderFg() : ui_theme::infoDefaultLineDay();
    const bool isVerticalScrollScreen =
        (_screenMode == SCREEN_HOURLY) ||
        (_screenMode == SCREEN_UDP_DATA) ||
        (_screenMode == SCREEN_LIGHTNING) ||
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
        String t = _title;
        if (_screenMode == SCREEN_HOURLY && !s_hourlyHeaders.empty())
        {
            int hourIdx = s_hourlyRoll.currentEnteringBlockIndex();
            if (hourIdx >= 0 && hourIdx < static_cast<int>(s_hourlyHeaders.size()))
                t = s_hourlyHeaders[hourIdx];
        }
        else if (_screenMode == SCREEN_UDP_FORECAST && !s_dailyHeaders.empty())
        {
            int dayIdx = s_dailyRoll.currentBlockIndex();
            if (dayIdx >= 0 && dayIdx < static_cast<int>(s_dailyHeaders.size()))
                t = s_dailyHeaders[dayIdx];
        }
        if (t.length() > 12) t = t.substring(0, 12);
        dma_display->print(t);
        const uint16_t underlineColor = monoTheme ? ui_theme::monoHeaderBg() : ui_theme::infoUnderlineDay();
        dma_display->drawFastHLine(0, headerHeight - 1, SCREEN_WIDTH, underlineColor);
    };

    // Special path: WeatherFlow hourly screen uses vertical scroll-up display
    if (_screenMode == SCREEN_HOURLY) {
        int bodyH = InfoScreen::SCREEN_HEIGHT - headerHeight;
        s_hourlyRoll.draw(*dma_display, 0, headerHeight, bodyH, defaultLineColor);
        // Redraw header after scrolling so the title never gets contaminated by partial lines
        drawHeader();
        return;
    }
    // Live Weather screen: vertical scroll-up body
    if (_screenMode == SCREEN_UDP_DATA) {
        int bodyH = InfoScreen::SCREEN_HEIGHT - headerHeight;
        s_liveRoll.draw(*dma_display, 0, headerHeight, bodyH, defaultLineColor);
        drawHeader();
        return;
    }
    if (_screenMode == SCREEN_LIGHTNING) {
        int bodyH = InfoScreen::SCREEN_HEIGHT - headerHeight;
        s_lightningRoll.draw(*dma_display, 0, headerHeight, bodyH, defaultLineColor);
        drawHeader();
        return;
    }
    if (_screenMode == SCREEN_CURRENT) {
        int bodyH = InfoScreen::SCREEN_HEIGHT - headerHeight;
        s_currentRoll.draw(*dma_display, 0, headerHeight, bodyH, defaultLineColor);
        drawHeader();
        return;
    }
    if (_screenMode == SCREEN_NOAA_ALERT) {
        drawNoaaAlertsScreen();
        return;
    }
    // Forecast path: same vertical roll-up animation style as hourly forecast
    if (_screenMode == SCREEN_UDP_FORECAST) {
        int bodyH = InfoScreen::SCREEN_HEIGHT - headerHeight;
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
                valueColor = monoTheme ? ui_theme::infoValueHighlightMono() : ui_theme::infoValueHighlightDay();
            }
        }
        else
        {
            valueColor = baseColor;
        }

        const uint16_t defaultLabelColor = monoTheme ? ui_theme::infoLabelDimMono()
                                                     : dma_display->color565(130, 150, 200);
        uint16_t labelColor;
        if (highlightLine)
        {
            labelColor = monoTheme ? ui_theme::infoLabelHighlightMono() : ui_theme::infoLabelHighlightDay();
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
        static unsigned long s_lastHourlyDrawMs = 0;
        const unsigned long nowMs = millis();
        if (nowMs - s_lastHourlyDrawMs < 33UL) {
            return;
        }
        s_lastHourlyDrawMs = nowMs;
        s_hourlyRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_hourlyRoll.update();
        draw();
        return;
    }
    if (_screenMode == SCREEN_UDP_DATA) {
        static unsigned long s_lastLiveDrawMs = 0;
        const unsigned long nowMs = millis();
        if (nowMs - s_lastLiveDrawMs < 33UL) {
            return;
        }
        s_lastLiveDrawMs = nowMs;
        s_liveRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_liveRoll.update();
        draw();
        return;
    }
    if (_screenMode == SCREEN_LIGHTNING) {
        static unsigned long s_lastLightningDrawMs = 0;
        const unsigned long nowMs = millis();
        if (nowMs - s_lastLightningDrawMs < 33UL) {
            return;
        }
        s_lastLightningDrawMs = nowMs;
        s_lightningRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_lightningRoll.update();
        draw();
        return;
    }
    if (_screenMode == SCREEN_CURRENT) {
        static unsigned long s_lastCurrentDrawMs = 0;
        const unsigned long nowMs = millis();
        if (nowMs - s_lastCurrentDrawMs < 33UL) {
            return;
        }
        s_lastCurrentDrawMs = nowMs;
        s_currentRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_currentRoll.update();
        draw();
        return;
    }
    if (_screenMode == SCREEN_NOAA_ALERT) {
        tickNoaaAlertsScreen();
        return;
    }
    if (_screenMode == SCREEN_UDP_FORECAST) {
        // Throttle redraws on this screen to reduce HUB75 visible flicker.
        static unsigned long s_lastForecastDrawMs = 0;
        unsigned long nowMs = millis();
        if (nowMs - s_lastForecastDrawMs < 33UL) {
            return;
        }
        s_lastForecastDrawMs = nowMs;
        s_dailyRoll.setScrollSpeed((verticalScrollSpeed > 0) ? (unsigned)verticalScrollSpeed : 60u);
        s_dailyRoll.update();
        draw();
        return;
    }
    if (_screenMode == SCREEN_ENV_INDEX) {
        // Air Quality uses the standard renderer (full body redraw), so throttle
        // frame rate to reduce visible HUB75 flicker.
        static unsigned long s_lastEnvDrawMs = 0;
        const unsigned long nowMs = millis();
        if (nowMs - s_lastEnvDrawMs < 33UL) {
            return;
        }
        s_lastEnvDrawMs = nowMs;
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
        if (_screenMode == SCREEN_LIGHTNING) return &s_lightningRoll;
        if (_screenMode == SCREEN_CURRENT) return &s_currentRoll;
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
        transitionToScreen(next);
        return;
    }
    draw();
}

