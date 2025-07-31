#include "InfoScreen.h"

extern const int NUM_INFOSCREENS;
extern const ScreenMode InfoScreenModes[];
extern ScreenMode currentScreen;

InfoScreen::InfoScreen(const String& title, ScreenMode mode)
    : _title(title),
      _screenMode(mode),  // FIX: Store mode!
      _lineCount(0),
      _active(false),
      _onExit(nullptr),
      _lastDraw(0),
      _refreshInterval(1000) {}

void InfoScreen::setTitle(const String& title) { _title = title; }

void InfoScreen::setLines(const String lines[], int n) {
    _lineCount = (n > INFOSCREEN_MAX_LINES) ? INFOSCREEN_MAX_LINES : n;
    for (int i = 0; i < _lineCount; ++i) _lines[i] = lines[i];
}

void InfoScreen::setRefreshInterval(unsigned long ms) { _refreshInterval = ms; }

void InfoScreen::show(void (*onExit)()) {
    _active = true;
    _onExit = onExit;
    _lastDraw = 0; // force immediate draw
}

void InfoScreen::hide() {
    _active = false;
    if (_onExit) _onExit();
}

bool InfoScreen::isActive() const { return _active; }

void InfoScreen::draw() {
    dma_display->fillScreen(0);
    dma_display->setFont(&Font5x7Uts);

    dma_display->setTextColor(dma_display->color565(0,255,255));
    dma_display->setCursor(0, 0);
    dma_display->print(_title);
    int y = 8;

    dma_display->setTextColor(dma_display->color565(255,255,255));
    for (int i = 0; i < _lineCount; ++i) {
        dma_display->setCursor(0, y);
        dma_display->print(_lines[i]);
        y += 8;
    }

    dma_display->setTextColor(dma_display->color565(255,180,0));
    dma_display->setCursor(0, 56);
    dma_display->print("[BACK]");
}

void InfoScreen::tick() {
    if (!_active) return;
    if (millis() - _lastDraw >= _refreshInterval) {
        _lastDraw = millis();
        draw();
    }
}

void InfoScreen::handleIR(uint32_t code) {
    if (!_active) return;

    // Find this screen's index in InfoScreenModes
    int idx = -1;
    for (int i = 0; i < NUM_INFOSCREENS; ++i) {
        if (InfoScreenModes[i] == _screenMode) { idx = i; break; }
    }
    if (idx < 0) return; // not found

    if (code == IR_CANCEL || code == IR_OK) {
        hide();
        currentScreen = SCREEN_OWM; // Always return home on exit
        return;
    }

    int nextIdx = -1;
    if (code == IR_LEFT)  nextIdx = (idx - 1 + NUM_INFOSCREENS) % NUM_INFOSCREENS;
    if (code == IR_RIGHT) nextIdx = (idx + 1) % NUM_INFOSCREENS;

    if (nextIdx >= 0 && nextIdx != idx) {
        hide();
        currentScreen = InfoScreenModes[nextIdx];
        // The next screen will be activated automatically in your main loop switch/case.
    }
}


