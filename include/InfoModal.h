#pragma once
#include <Arduino.h>
#include "display.h"

// --- Info Modal Colors ---
#define INFOMODAL_GREEN   dma_display->color565(0,255,80)
#define INFOMODAL_HEADERBG dma_display->color565(0,20,60)
#define INFOMODAL_UNSELXBG dma_display->color565(40,40,180)
#define INFOMODAL_SELXBG   dma_display->color565(255,0,0)
#define INFOMODAL_XCOLOR   dma_display->color565(255,255,255)
#define INFOMODAL_ULINE    dma_display->color565(180,180,255)
#define INFOMODAL_SEL      dma_display->color565(255,255,64)
#define INFOMODAL_UNSEL    dma_display->color565(0,255,255)

class InfoModal {
public:
    static const int MAX_LINES = 8;
    static const int MAXCOLS = 12;
    static const int CHARH = 8;
    static const int MAXROWS = 4;
    static const int INFOROWS = MAXROWS - 1;
    static const int SCREEN_WIDTH = 64;
    static const int SCROLLSPEED = 50;
    static const int ENDPAUSE = 300;

    InfoModal(const String& title = "Info");

    void setLines(const String lines[], int count);
    void show();
    void hide();
    void handleIR(uint32_t code);
    bool isActive() const;
    void tick();
private:
    void draw();
    void drawHeader();

    String modalTitle;
    String lines[MAX_LINES];
    int lineCount = 0;

    bool active = false;
    int selIndex = 0;
    int scrollY = 0;
    int scrollOffset = 0;
    bool atClose = false;
    unsigned long lastScrollTime = 0;
    bool firstScroll = true;
    int lastSelIndex = -1;
};
