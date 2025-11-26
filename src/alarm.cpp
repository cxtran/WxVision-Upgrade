#include "alarm.h"
#include "settings.h"
#include "buzzer.h"

static bool s_alarmActive = false;
static bool s_alarmFlashVisible = true;
static unsigned long s_lastFlashToggleMs = 0;
static unsigned long s_lastBeepMs = 0;
static uint32_t s_lastTriggerMinuteKey = 0;
static uint32_t s_silencedMinuteKey = 0;
static int s_activeSlot = -1;
static int s_melodyIndex = 0;
static unsigned long s_melodyNoteEndMs = 0;

static constexpr unsigned long kAlarmFlashIntervalMs = 400;
static constexpr unsigned long kAlarmBeepIntervalMs = 1000;
static constexpr int kAlarmBeepFreq = 3000;
static constexpr int kAlarmBeepMs = 120;

struct MelodyNote { int freq; int durMs; };
static const MelodyNote kAlarmMelody[] = {
    {262, 160}, {330, 160}, {392, 160}, {523, 200},
    {0, 120},
    {523, 160}, {392, 160}, {330, 160}, {262, 220}
};
static const int kAlarmMelodyLen = sizeof(kAlarmMelody) / sizeof(kAlarmMelody[0]);

static bool doesAlarmApplyToday(int slot, int dayOfWeek)
{
    if (!alarmEnabled[slot])
        return false;

    switch (alarmRepeatMode[slot])
    {
    case ALARM_REPEAT_NONE:
        return alarmOneShotPending[slot];
    case ALARM_REPEAT_DAILY:
        return true;
    case ALARM_REPEAT_WEEKLY:
        return dayOfWeek == alarmWeeklyDay[slot];
    case ALARM_REPEAT_WEEKDAY:
        return dayOfWeek >= 1 && dayOfWeek <= 5;
    case ALARM_REPEAT_WEEKEND:
        return dayOfWeek == 0 || dayOfWeek == 6;
    default:
        return false;
    }
}

void refreshAlarmArming()
{
    for (int i = 0; i < 3; ++i)
    {
        if (alarmRepeatMode[i] == ALARM_REPEAT_NONE)
        {
            alarmOneShotPending[i] = alarmEnabled[i];
        }
        else
        {
            alarmOneShotPending[i] = false;
        }
    }
}

static void resetRuntimeAlarm()
{
    s_alarmActive = false;
    s_alarmFlashVisible = true;
    s_lastFlashToggleMs = millis();
    s_lastBeepMs = 0;
    s_lastTriggerMinuteKey = 0;
    s_silencedMinuteKey = 0;
    s_melodyIndex = 0;
    s_melodyNoteEndMs = 0;
    stopAlarmBuzzer();
}

void initAlarmModule()
{
    refreshAlarmArming();
    resetRuntimeAlarm();
}

void notifyAlarmSettingsChanged()
{
    refreshAlarmArming();
    resetRuntimeAlarm();
}

void tickAlarmState(const DateTime &now)
{
    uint32_t currentMinuteKey = now.unixtime() / 60;
    // If silenced for this minute, keep alarm off entirely
    if (s_silencedMinuteKey == currentMinuteKey)
    {
        s_alarmActive = false;
        s_alarmFlashVisible = true;
        s_lastFlashToggleMs = millis();
        s_lastBeepMs = millis();
        s_melodyIndex = 0;
        s_melodyNoteEndMs = 0;
        stopAlarmBuzzer();
        return;
    }

    bool wasActive = s_alarmActive;
    if (s_silencedMinuteKey && currentMinuteKey != s_silencedMinuteKey)
    {
        s_silencedMinuteKey = 0;
    }
    s_activeSlot = -1;
    bool shouldBeActive = false;
    int triggeredSlot = -1;

    for (int i = 0; i < 3; ++i)
    {
        if (!alarmEnabled[i])
            continue;
        bool dayMatch = doesAlarmApplyToday(i, now.dayOfTheWeek());
        if (!dayMatch)
            continue;
        bool timeMatch = (now.hour() == alarmHour[i]) && (now.minute() == alarmMinute[i]);
        if (!timeMatch)
            continue;

        if (s_silencedMinuteKey == currentMinuteKey)
        {
            continue;
        }

        shouldBeActive = true;
        triggeredSlot = i;
        break;
    }

    if (shouldBeActive && s_lastTriggerMinuteKey != currentMinuteKey)
    {
        s_lastTriggerMinuteKey = currentMinuteKey;
        s_activeSlot = triggeredSlot;
        s_alarmFlashVisible = false;
        s_lastFlashToggleMs = millis();

        if (triggeredSlot >= 0 && alarmRepeatMode[triggeredSlot] == ALARM_REPEAT_NONE)
        {
            alarmOneShotPending[triggeredSlot] = false;
            alarmEnabled[triggeredSlot] = false;
            saveAlarmSettings();
        }
    }
    else if (!shouldBeActive && s_lastTriggerMinuteKey == currentMinuteKey)
    {
        shouldBeActive = true;
    }

    s_alarmActive = shouldBeActive;

    unsigned long nowMs = millis();
    if (s_alarmActive)
    {
        if (nowMs - s_lastFlashToggleMs >= kAlarmFlashIntervalMs)
        {
            s_alarmFlashVisible = !s_alarmFlashVisible;
            s_lastFlashToggleMs = nowMs;
        }
        if (alarmSoundMode == 0)
        {
            if (nowMs - s_lastBeepMs >= kAlarmBeepIntervalMs)
            {
                playBuzzerTone(kAlarmBeepFreq, kAlarmBeepMs);
                s_lastBeepMs = nowMs;
            }
        }
        else // Melody mode
        {
            if (nowMs >= s_melodyNoteEndMs)
            {
                const MelodyNote &note = kAlarmMelody[s_melodyIndex];
                if (note.freq > 0)
                {
                    playBuzzerTone(note.freq, note.durMs);
                }
                s_melodyNoteEndMs = nowMs + (unsigned long)note.durMs + 20;
                s_melodyIndex = (s_melodyIndex + 1) % kAlarmMelodyLen;
            }
        }
    }
    else
    {
        s_alarmFlashVisible = true;
        s_lastFlashToggleMs = nowMs;
        if (wasActive)
        {
            stopAlarmBuzzer();
        }
    }
}

bool isAlarmCurrentlyActive()
{
    return s_alarmActive;
}

bool isAlarmFlashVisible()
{
    return s_alarmFlashVisible;
}

bool isAnyAlarmEnabled()
{
    for (int i = 0; i < 3; ++i)
    {
        if (alarmEnabled[i])
            return true;
    }
    return false;
}

void cancelActiveAlarm()
{
    DateTime nowUtc = rtc.now();
    int offsetMinutes = timezoneIsCustom() ? tzStandardOffset : timezoneOffsetForUtc(nowUtc);
    DateTime localNow = utcToLocal(nowUtc, offsetMinutes);
    uint32_t currentMinuteKey = localNow.unixtime() / 60;

    s_silencedMinuteKey = currentMinuteKey;
    s_lastTriggerMinuteKey = currentMinuteKey;
    s_alarmActive = false;
    s_alarmFlashVisible = true;
    s_lastFlashToggleMs = millis();
    s_melodyIndex = 0;
    s_melodyNoteEndMs = 0;
    stopAlarmBuzzer();
}

void stopAlarmBuzzer()
{
    stopBuzzer();
}
