#pragma once
#include <Arduino.h>
#include "display.h"
#include "ir_codes.h"

#define INFOSCREEN_MAX_LINES     10
#define INFOSCREEN_VISIBLE_ROWS  3
#define INFOSCREEN_HEADERFG dma_display->color565(156, 255, 91)
#define INFOSCREEN_HEADERBG dma_display->color565(0,20,60)

class InfoScreen {
public:
    InfoScreen(const String& title = "", ScreenMode mode = SCREEN_OWM);

    void setTitle(const String& title);
    void setLines(const String lines[], int n);
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

private:
    String _title;
    String _lines[INFOSCREEN_MAX_LINES];
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
