#pragma once
#include <Arduino.h>
#include "display.h"
#include "ir_codes.h"

#define INFOSCREEN_MAX_LINES     10
#define INFOSCREEN_VISIBLE_ROWS  3

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

    // Horizontal scroll state for selected line
    int scrollOffset;
    unsigned long lastScrollTime;
    bool firstScroll;
    bool scrollPaused;
    unsigned long scrollPauseTime;

    void draw();
    int getTextWidth(const char* str);
    void resetHScroll();
};
