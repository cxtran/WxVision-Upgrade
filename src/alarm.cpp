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

// Fur Elise (recognizable opening phrase)
// Frequencies are in Hz, durations in ms
static const MelodyNote kMelodyFurElise[] = {
    // e d# e d# e b d c a
    {659, 180}, {622, 180}, {659, 180}, {622, 180}, {659, 180}, {494, 180}, {587, 180}, {523, 180},
    {440, 260}, {0, 100}, // brief breath
    // c a-arpeggio and lead-in
    {262, 180}, {330, 180}, {440, 180}, {494, 240}, {0, 80},
    {330, 180}, {415, 180}, {494, 180}, {523, 240}, {0, 80},
    // repeat the opener once more
    {330, 180}, {659, 180}, {622, 180}, {659, 180}, {622, 180}, {659, 180}, {494, 180}, {587, 180},
    {523, 180}, {440, 320}
};
static const int kMelodyFurEliseLen = sizeof(kMelodyFurElise) / sizeof(kMelodyFurElise[0]);

// Swan Lake (main theme fragment)
static const MelodyNote kMelodySwanLake[] = {
    {494, 200},  {554, 200},  {587, 240},  {740, 240},  {880, 240},  {784, 220},
    {740, 200},  {659, 200},  {740, 220},  {587, 240},  {494, 260},  {0, 120},
    {494, 200},  {554, 200},  {587, 240},  {554, 200},  {494, 200},  {440, 200},
    {370, 180},  {392, 200},  {370, 200},  {330, 200},  {370, 240},  {0, 140}
};
static const int kMelodySwanLakeLen = sizeof(kMelodySwanLake) / sizeof(kMelodySwanLake[0]);

// Turkish March (Mozart) - bright opening motif
static const MelodyNote kMelodyTurkishMarch[] = {
    {659, 150}, {698, 150}, {784, 150}, {698, 150}, {659, 150}, {622, 150}, {659, 180}, {494, 120},
    {523, 150}, {587, 150}, {659, 170}, {587, 150}, {523, 150}, {494, 170}, {523, 190}, {0, 110},
    {523, 150}, {587, 150}, {659, 180}, {698, 180}, {784, 200}, {698, 170}, {659, 170}, {622, 190},
    {659, 240}
};
static const int kMelodyTurkishMarchLen = sizeof(kMelodyTurkishMarch) / sizeof(kMelodyTurkishMarch[0]);

// Moonlight Sonata (Beethoven) - arpeggiated intro
static const MelodyNote kMelodyMoonlight[] = {
    {277, 240}, {330, 240}, {415, 240}, {523, 320}, {0, 140},
    {277, 240}, {330, 240}, {415, 240}, {494, 320}, {0, 140},
    {247, 240}, {311, 240}, {392, 240}, {494, 320}, {0, 140},
    {220, 240}, {277, 240}, {349, 240}, {466, 320}, {0, 200}
};
static const int kMelodyMoonlightLen = sizeof(kMelodyMoonlight) / sizeof(kMelodyMoonlight[0]);

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
            const MelodyNote *melody = kAlarmMelody;
            int melodyLen = kAlarmMelodyLen;
            if (alarmSoundMode == 1) { melody = kMelodyFurElise; melodyLen = kMelodyFurEliseLen; }
            else if (alarmSoundMode == 2) { melody = kMelodySwanLake; melodyLen = kMelodySwanLakeLen; }
            else if (alarmSoundMode == 3) { melody = kMelodyTurkishMarch; melodyLen = kMelodyTurkishMarchLen; }
            else if (alarmSoundMode == 4) { melody = kMelodyMoonlight; melodyLen = kMelodyMoonlightLen; }

            if (nowMs >= s_melodyNoteEndMs)
            {
                const MelodyNote &note = melody[s_melodyIndex];
                if (note.freq > 0)
                {
                    playBuzzerTone(note.freq, note.durMs);
                }
                s_melodyNoteEndMs = nowMs + (unsigned long)note.durMs + 20;
                s_melodyIndex = (s_melodyIndex + 1) % melodyLen;
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
