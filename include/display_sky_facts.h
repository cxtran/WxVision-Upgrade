#pragma once

#include "astronomy.h"

void drawSkyFactsScreen();
void tickSkyFactsScreen();
void drawSkyBriefScreen();
void tickSkyBriefScreen();
void drawSkyFactSubpage(const wxv::astronomy::SkyFactPage &page);
void handleSkyFactsDownPress();
void handleSkyFactsUpPress();
void handleSkyFactsSelectPress();
void handleSkyBriefDownPress();
void handleSkyBriefUpPress();
void resetSkyFactsScreenState();
void resetSkyBriefScreenState();
