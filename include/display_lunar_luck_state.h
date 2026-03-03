#pragma once

#include <Arduino.h>

struct LuckSection
{
    const char *title;
    char *content;
    uint16_t contentCap;
    uint16_t contentLen;
    bool marquee;
    int16_t contentWidthPx;
};

enum HeaderAnimState
{
    HDR_IDLE = 0,
    HDR_DOOR
};

extern LuckSection g_sections[10];
extern char g_goiyContent[800];
extern uint8_t g_sectionCount;
extern uint8_t currentSectionIndex;
extern HeaderAnimState hdrState;
extern int16_t hdrDoorPx;
extern bool hdrDrawNew;
extern bool hdrDelayActive;
extern bool hdrBrightnessPulsed;
extern uint8_t hdrBrightnessSaved;
extern char hdrOld[48];
extern char hdrNew[48];

int measureTextWidthPx(const char *s, uint16_t len);
void setSectionStartState();
