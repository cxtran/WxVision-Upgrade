#pragma once
#include <Arduino.h>
#include "display.h"
#include "ir_codes.h"
#include "ui_theme.h"

#define INFOSCREEN_MAX_LINES     32
#define INFOSCREEN_VISIBLE_ROWS  3
#define INFOSCREEN_HEADERFG ui_theme::infoScreenHeaderFg()
#define INFOSCREEN_HEADERBG ui_theme::infoScreenHeaderBg()

class InfoScreen {
public:
    InfoScreen(const String& title = "", ScreenMode mode = SCREEN_OWM);

    void setTitle(const String& title);
//    void setLines(const String lines[], int n);
    void show(void (*onExit)() = nullptr);
    void hide();
    bool isActive() const;
    void handleIR(uint32_t code);
    void tick();
    ScreenMode getMode() const { return _screenMode; }
    static const int MAX_LINES = 16;
    static const int MAXCOLS = 12;
    static const int CHARH = 8;
    static const int MAXROWS = 4;
    static const int DATA_ROWS_FULL = 3; // 3 data rows if NO button bar
    static const int SCREEN_WIDTH = 64;
    static const int SCREEN_HEIGHT = 32;
    static const int CHARW = 6; // 5x7 font width + 1 spacing for wrapping
    int getScrollY() const { return scrollY; }
    void setScrollY(int val) { if (val < 0) val = 0;   scrollY = val; }

    int getSelIndex() const { return selIndex; }
    void setLines(const String lines[], int n, bool resetPosition = false, const uint16_t colors[] = nullptr);
    void setHighlightEnabled(bool enabled);
    using LineOverlayFn = void (*)(int lineIndex, int y, bool selected);
    void setLineOverlay(LineOverlayFn fn);
    void setSelectedLine(int index);

private:
    String _title;
    String _lines[INFOSCREEN_MAX_LINES];
    uint16_t _lineColors[INFOSCREEN_MAX_LINES];
    bool _lineColorUsed[INFOSCREEN_MAX_LINES];
    bool _highlightEnabled;
    LineOverlayFn _lineOverlay;
    int _lineCount;
    bool _active;
    void (*_onExit)();
    ScreenMode _screenMode;

    int scrollY;          // vertical scroll start index for visible window
    int selIndex;         // selected line index (relative to visible page)
    int lastSelIndex;     // last selected line index (for detecting changes)

    // Independent horizontal scroll state for each visible line
    int scrollOffsets[INFOSCREEN_VISIBLE_ROWS];
    unsigned long lastScrollTimes[INFOSCREEN_VISIBLE_ROWS];

    // (Legacy modal state fields, keep for compatibility if referenced elsewhere)
    bool firstScroll;
    bool scrollPaused;
    unsigned long scrollPauseTime;

    void draw();
    void resetHScroll();
};
