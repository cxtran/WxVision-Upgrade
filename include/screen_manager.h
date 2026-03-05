#pragma once
#include "display.h"

void applyDataSourcePolicies(bool wifiConnected);
void transitionToScreen(ScreenMode target);
void rotateScreen(int direction);
void handleAutoRotate(unsigned long now);
bool isSectionHeadingActive();
void stepSectionHeading(int direction, unsigned long now);
void skipSectionHeading(unsigned long now);
bool serviceSectionHeading(unsigned long now);
void requestSectionHeadingRerender();
void playScreenRevealEffect(ScreenMode mode);
void noteScreenRotation(unsigned long now);
void ensureCurrentScreenAllowed();
