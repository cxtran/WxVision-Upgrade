#pragma once

#include <Arduino.h>

namespace wxv::announce
{
    bool playAudioPath(const char *path);
    bool playClip(const char *path);
    bool playFirstExistingClip(const char *primaryPath, const char *fallbackPath = nullptr);
    bool playClipSequence(const char *firstPath, const char *secondPath = nullptr);
    bool speakNumber(int value);
    bool speakTime12(int hour24, int minute);
    bool speakTemperature(int value, bool fahrenheit);
    bool speakHumidity(int value);
    bool speakPressureHpa(int value);
    bool speakWindSpeed(int value);
    bool speakRainChance(int value);
    bool speakAlertEvent(const String &eventKey);
    bool speakAlertSeverity(const String &severityKey);
    bool playChime(const String &chimeKey);
    bool playAlarm(const String &alarmKey);
    bool playUiSound(const char *uiKey);
    bool playUiSound(const String &uiKey);

    bool speakTime();
    bool speakTimeWithChime(const String &chimeKey);
    bool speakDate();
    bool speakIndoorTemperature();
    bool speakOutdoorTemperature();
    bool speakIndoorHumidity();
    bool speakOutdoorWeather();
    bool speakWindSpeed();
    bool speakWindDirection();
    bool speakPressure();
    bool speakRain();
    bool speakAlerts();
    bool speakInternalAlerts();
    bool speakWeatherSummary();

    void tick();
    void stop();
    bool isActive();
    bool refreshOutputVolume();
    const char *lastStatus();
}
