#pragma once
#include <Arduino.h>
#include "display.h"
#include "ir_codes.h"

// --- You can increase this for more lines per screen
#define INFOSCREEN_MAX_LINES  10


class InfoScreen {
public:
    // NEW: Pass the mode for rotation
    InfoScreen(const String& title = "", ScreenMode mode = SCREEN_OWM);

    void setTitle(const String& title);
    void setLines(const String lines[], int n);
    void show(void (*onExit)() = nullptr);
    void hide();
    bool isActive() const;
    void handleIR(uint32_t code);
    void tick();  // call periodically to redraw/refresh
    void setRefreshInterval(unsigned long ms);

    // Needed for generic rotation
    ScreenMode getMode() const { return _screenMode; }

private:
    String _title;
    String _lines[INFOSCREEN_MAX_LINES];
    int _lineCount;
    bool _active;
    void (*_onExit)();
    unsigned long _lastDraw;
    unsigned long _refreshInterval;
    ScreenMode _screenMode; // --- NEW: Each screen knows its mode for rotation
    void draw();
};
