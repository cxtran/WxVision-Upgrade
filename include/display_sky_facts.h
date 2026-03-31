#pragma once

#include "astronomy.h"

void drawSkyFactsScreen();
void tickSkyFactsScreen();
void drawSkyBriefScreen();
void tickSkyBriefScreen();
void drawSkyFactSubpage(const wxv::astronomy::SkyFactPage &page);
void handleSkyBriefDownPress();
void handleSkyBriefUpPress();
void resetSkyBriefScreenState();
