#pragma once
#include <Arduino.h>
#include <functional>

enum InfoFieldType { InfoLabel, InfoNumber, InfoChooser, InfoText, InfoButton };

class InfoModal {
public:
    static const int MAX_LINES = 16;
    static const int MAXCOLS = 12;
    static const int CHARH = 8;
    static const int MAXROWS = 4;
    static const int DATA_ROWS = 2;    // Only 2 data rows if button bar shown
    static const int DATA_ROWS_FULL = 3; // 3 data rows if NO button bar
    static const int SCREEN_WIDTH = 64;
    static const int SCROLLSPEED = 50;

    InfoModal(const String& title = "Info");

    void setLines(const String lines[], const InfoFieldType types[], int count);
    void setValueRefs(
        int* intRefs[], int intRefCount,
        int* chooserRefs[], int chooserCount,
        const char* const* chooserOptions[], const int chooserOptionCounts[],
        char* textRefs[] = nullptr, int textRefCount = 0, int textSizes[] = nullptr
    );

    void setButtons(const String btns[], int btnCount);
    void setCallback(const std::function<void(bool, int)>& cb);
    void show();
    void hide();
    bool isActive() const;
    void tick();
    void handleIR(uint32_t code);
    int getSelIndex() const;

    // Optional: direct set (not required for normal usage)
    void setTextValue(int idx, const char* value);

    void handleTextDone(int idx, const char* result);

    // Public for friend test code or minimal integration, not needed for normal use
    bool inButtonBar;
    int btnSel;

private:
    void draw();
    void drawHeader();
    String getChooserLabel(int idx);

    String modalTitle;
    String lines[MAX_LINES];
    InfoFieldType fieldTypes[MAX_LINES];
    int lineCount = 0;

    // InfoNumber
    int* intRefs[MAX_LINES];

    // InfoChooser
    int* chooserRefs[MAX_LINES];
    const char* const* chooserOptions[MAX_LINES];
    int chooserOptionCounts[MAX_LINES];

    // Field index maps
    int chooserFieldIndices[MAX_LINES];
    int numberFieldIndices[MAX_LINES];

    // InfoText
    char* textRefs[MAX_LINES];
    int   textSizes[MAX_LINES];
    int   textFieldIndices[MAX_LINES];

    // Button bar
    String btnLabels[MAXROWS];
    int btnCount = 0;
    bool active = false;

    int selIndex = 0;
    int scrollY = 0;
    int scrollOffset = 0;
    bool atClose = false;
    unsigned long lastScrollTime = 0;
    bool firstScroll = true;
    int lastSelIndex = -1;
    bool inEdit = false;
    int editIndex = -1;

    std::function<void(bool, int)> callback;
};
