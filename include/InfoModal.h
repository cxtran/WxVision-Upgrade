#pragma once
#include <Arduino.h>
#include <functional>
#include "ir_codes.h"
#include "sensors.h"
#include "settings.h"
// --- Info Modal Colors ---
#define INFOMODAL_GREEN    dma_display->color565(255,255,255)
#define INFOMODAL_HEADERBG dma_display->color565(0,0,120)

#define INFOMODAL_UNSELXBG dma_display->color565(110,80,133)
#define INFOMODAL_SELXBG   dma_display->color565(255,0,0)
#define INFOMODAL_XCOLOR   dma_display->color565(255,255,255)
//#define INFOMODAL_ULINE    dma_display->color565(180,180,255)
#define INFOMODAL_ULINE    dma_display->color565(255,255,255)
#define INFOMODAL_SEL      dma_display->color565(255,255,64)
#define INFOMODAL_UNSEL    dma_display->color565(0,255,255)
#define INFOMODAL_BTN_BG   dma_display->color565(20,60,120)
#define INFOMODAL_BTN_SELBG dma_display->color565(255,130,0)
#define INFOMODAL_EDIT    dma_display->color565(255, 255, 0)  // Yellow highlight for editing


enum InfoFieldType { InfoLabel, InfoNumber, InfoChooser, InfoText, InfoButton };

class InfoModal {
public:
    static const int MAX_LINES = 16;
    static const int MAXCOLS = 12;
    static const int CHARH = 8;
    static const int MAXROWS = 4;
    static const int DATA_ROWS = 2;      // Only 2 data rows if button bar shown
    static const int DATA_ROWS_FULL = 3; // 3 data rows if NO button bar
    static const int SCREEN_WIDTH = 64;

   // int scrollSpeed = 50;  // Default scroll speed, now writable



    InfoModal(const String& title = "Info");

    void setLines(const String lines[], const InfoFieldType types[], int count);

    void setValueRefs(
        int* intRefs[], int intRefCount,
        int* chooserRefs[], int chooserCount,
        const char* const* chooserOptions[], const int chooserOptionCounts[],
        char* textRefs[] = nullptr, int textRefCount = 0, int textSizes[] = nullptr
    );

    void setTextRefs(char* textRefsIn[], int count);  // ✅ fixed: uses char* not const char*
    void setButtons(const String btns[], int btnCount);
    void setCallback(const std::function<void(bool, int)>& cb);
    void setShowNumberArrows(bool enable);
    void setShowChooserArrows(bool enable);
    void setShowForwardArrow(bool enable);
    void show();
    void hide();
    bool isActive() const;
    void tick();
    void handleIR(uint32_t code);
    int getSelIndex() const;

    // Optional: direct text set (e.g. to preload buffer)
    void setTextValue(int idx, const char* value);
    void handleTextDone(int idx, const char* result);

    // Public for integration (but not normally needed)
    bool inButtonBar;
    int btnSel;
    bool showNumberArrows = false;
    bool showChooserArrows = true;
    bool showForwardArrow = false;

    void resetState();
private:
    void draw();
    void drawHeader();
    String getChooserLabel(int idx);

    String modalTitle;
    String lines[MAX_LINES];
    InfoFieldType fieldTypes[MAX_LINES];
    int lineCount = 0;

    // Number fields
    int* intRefs[MAX_LINES];

    // Chooser fields
    int* chooserRefs[MAX_LINES];
    const char* const* chooserOptions[MAX_LINES];
    int chooserOptionCounts[MAX_LINES];

    // Index maps
    int chooserFieldIndices[MAX_LINES];
    int numberFieldIndices[MAX_LINES];

    // Text fields
    char* textRefs[MAX_LINES];              // external char* buffers
    int textFieldIndices[MAX_LINES];        // maps modal line index to textRefs[]
    int textSizes[MAX_LINES];               // optional lengths per buffer
    int textFieldCount = 0;

    // Buttons
    String btnLabels[MAXROWS];
    int btnCount = 0;

    // State
    bool active = false;
    bool atClose = false;
    bool inEdit = false;
    int selIndex = 0;
    int scrollY = 0;
    int scrollOffset = 0;
    int lastSelIndex = -1;
    int editIndex = -1;

    unsigned long lastScrollTime = 0;
    bool firstScroll = true;

    std::function<void(bool, int)> callback;


        // --- PATCH: Scrolling pause state ---
    unsigned long scrollPauseTime = 0;   // --- PATCH
    bool scrollPaused = false;           // --- PATCH

};
