#pragma once

void startOTA();
void resetPowerUsage();
void quickRestore();
void factoryReset();

// ---- Screen On/Off RAM-only feature ----
void setScreenOff(bool off);
bool isScreenOff();
void toggleScreenPower();
