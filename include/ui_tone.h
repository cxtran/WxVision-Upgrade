#pragma once

#include <Arduino.h>

enum UiTone
{
    UI_TONE_UP,
    UI_TONE_DOWN,
    UI_TONE_LEFT,
    UI_TONE_RIGHT,
    UI_TONE_SELECT,
    UI_TONE_BACK,
    UI_TONE_ERROR,
    UI_TONE_TOGGLE_ON,
    UI_TONE_TOGGLE_OFF,
    UI_TONE_VOLUME_UP,
    UI_TONE_VOLUME_DOWN
};

void playUiTone(UiTone tone);
void setUiToneEnabled(bool enabled);
void setUiToneVolume(float volume);

