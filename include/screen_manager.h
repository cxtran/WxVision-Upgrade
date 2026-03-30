#pragma once
#include "display.h"

void applyDataSourcePolicies(bool wifiConnected);
void transitionToScreen(ScreenMode target);
void rotateScreen(int direction);
void handleAutoRotate(unsigned long now);
void handleReturnToDefault(unsigned long now);
bool isSectionHeadingActive();
void stepSectionHeading(int direction, unsigned long now);
void skipSectionHeading(unsigned long now);
bool serviceSectionHeading(unsigned long now);
void requestSectionHeadingRerender();
void queueTemporaryAlertHeading(const char *text, uint16_t durationMs, uint32_t signature = 0, const char *subtitle = nullptr);
bool isTemporaryAlertActive();
void skipTemporaryAlertHeading(unsigned long now);
bool serviceTemporaryAlertHeading(unsigned long now);
void playScreenRevealEffect(ScreenMode mode);
void noteScreenRotation(unsigned long now);
ScreenMode configuredDefaultScreen();
void ensureCurrentScreenAllowed();
void refreshVisibleScreen();
