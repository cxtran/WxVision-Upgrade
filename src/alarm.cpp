#include "alarm.h"
#include "settings.h"
#include "buzzer.h"
#include "web.h"

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

struct MelodyNote
{
    int8_t midi;         // Piano note (MIDI), or NOTE_REST
    uint16_t durMs;      // total note duration
    uint16_t attackMs;   // ADSR: attack
    uint16_t decayMs;    // ADSR: decay
    uint8_t sustainPct;  // ADSR: sustain (0-100)
    uint16_t releaseMs;  // ADSR: release
};

static const MelodyNote kAlarmMelody[] = {
    {NOTE_C4, 160, 10, 30, 60, 40}, {NOTE_E4, 160, 10, 30, 60, 40}, {NOTE_G4, 160, 10, 30, 60, 40}, {NOTE_C5, 200, 12, 35, 55, 45},
    {NOTE_REST, 120, 0, 0, 0, 0},
    {NOTE_C5, 160, 10, 30, 60, 40}, {NOTE_G4, 160, 10, 30, 60, 40}, {NOTE_E4, 160, 10, 30, 60, 40}, {NOTE_C4, 220, 14, 45, 50, 60}
};
static const int kAlarmMelodyLen = sizeof(kAlarmMelody) / sizeof(kAlarmMelody[0]);

// Fur Elise (recognizable opening phrase)
// Piano notes (MIDI), durations in ms
static const MelodyNote kMelodyFurElise[] = {
    // e d# e d# e b d c a
    {NOTE_E5, 180, 8, 28, 60, 40}, {NOTE_DS5, 180, 8, 28, 60, 40}, {NOTE_E5, 180, 8, 28, 60, 40}, {NOTE_DS5, 180, 8, 28, 60, 40},
    {NOTE_E5, 180, 8, 28, 60, 40}, {NOTE_B4, 180, 10, 30, 55, 45}, {NOTE_D5, 180, 10, 30, 55, 45}, {NOTE_C5, 180, 10, 30, 55, 45},
    {NOTE_A4, 260, 14, 40, 50, 70}, {NOTE_REST, 100, 0, 0, 0, 0}, // brief breath
    // c a-arpeggio and lead-in
    {NOTE_C4, 180, 10, 30, 60, 40}, {NOTE_E4, 180, 10, 30, 60, 40}, {NOTE_A4, 180, 10, 30, 60, 40}, {NOTE_B4, 240, 12, 35, 55, 55},
    {NOTE_REST, 80, 0, 0, 0, 0},
    {NOTE_E4, 180, 10, 30, 60, 40}, {NOTE_GS4, 180, 10, 30, 60, 40}, {NOTE_B4, 180, 10, 30, 60, 40}, {NOTE_C5, 240, 12, 35, 55, 55},
    {NOTE_REST, 80, 0, 0, 0, 0},
    // repeat the opener once more
    {NOTE_E4, 180, 10, 30, 60, 40}, {NOTE_E5, 180, 8, 28, 60, 40}, {NOTE_DS5, 180, 8, 28, 60, 40}, {NOTE_E5, 180, 8, 28, 60, 40},
    {NOTE_DS5, 180, 8, 28, 60, 40}, {NOTE_E5, 180, 8, 28, 60, 40}, {NOTE_B4, 180, 10, 30, 55, 45}, {NOTE_D5, 180, 10, 30, 55, 45},
    {NOTE_C5, 180, 10, 30, 55, 45}, {NOTE_A4, 320, 18, 55, 45, 90}
};
static const int kMelodyFurEliseLen = sizeof(kMelodyFurElise) / sizeof(kMelodyFurElise[0]);

// Swan Lake (main theme fragment)
static const MelodyNote kMelodySwanLake[] = {
    {NOTE_B4, 200, 10, 35, 55, 55},  {NOTE_CS5, 200, 10, 35, 55, 55},  {NOTE_D5, 240, 12, 40, 55, 65},
    {NOTE_FS5, 240, 12, 40, 55, 65}, {NOTE_A5, 240, 12, 40, 55, 65},  {NOTE_G5, 220, 12, 40, 55, 60},
    {NOTE_FS5, 200, 10, 35, 55, 55}, {NOTE_E5, 200, 10, 35, 55, 55},  {NOTE_FS5, 220, 12, 40, 55, 60},
    {NOTE_D5, 240, 12, 40, 55, 65},  {NOTE_B4, 260, 14, 45, 50, 70},  {NOTE_REST, 120, 0, 0, 0, 0},
    {NOTE_B4, 200, 10, 35, 55, 55},  {NOTE_CS5, 200, 10, 35, 55, 55},  {NOTE_D5, 240, 12, 40, 55, 65},
    {NOTE_CS5, 200, 10, 35, 55, 55}, {NOTE_B4, 200, 10, 35, 55, 55},  {NOTE_A4, 200, 10, 35, 55, 55},
    {NOTE_FS4, 180, 10, 30, 60, 55}, {NOTE_G4, 200, 10, 35, 55, 55},   {NOTE_FS4, 200, 10, 35, 55, 55},
    {NOTE_E4, 200, 10, 35, 55, 55},  {NOTE_FS4, 240, 12, 40, 55, 65},  {NOTE_REST, 140, 0, 0, 0, 0}
};
static const int kMelodySwanLakeLen = sizeof(kMelodySwanLake) / sizeof(kMelodySwanLake[0]);

// Turkish March (Mozart) - bright opening motif
static const MelodyNote kMelodyTurkishMarch[] = {
    {NOTE_E5, 150, 6, 20, 70, 35}, {NOTE_F5, 150, 6, 20, 70, 35}, {NOTE_G5, 150, 6, 20, 70, 35}, {NOTE_F5, 150, 6, 20, 70, 35},
    {NOTE_E5, 150, 6, 20, 70, 35}, {NOTE_DS5, 150, 6, 20, 70, 35}, {NOTE_E5, 180, 8, 25, 65, 45}, {NOTE_B4, 120, 6, 18, 75, 30},
    {NOTE_C5, 150, 6, 20, 70, 35}, {NOTE_D5, 150, 6, 20, 70, 35}, {NOTE_E5, 170, 8, 25, 65, 45}, {NOTE_D5, 150, 6, 20, 70, 35},
    {NOTE_C5, 150, 6, 20, 70, 35}, {NOTE_B4, 170, 8, 25, 65, 45}, {NOTE_C5, 190, 8, 25, 65, 55}, {NOTE_REST, 110, 0, 0, 0, 0},
    {NOTE_C5, 150, 6, 20, 70, 35}, {NOTE_D5, 150, 6, 20, 70, 35}, {NOTE_E5, 180, 8, 25, 65, 45}, {NOTE_F5, 180, 8, 25, 65, 45},
    {NOTE_G5, 200, 10, 30, 60, 55}, {NOTE_F5, 170, 8, 25, 65, 45}, {NOTE_E5, 170, 8, 25, 65, 45}, {NOTE_DS5, 190, 8, 25, 65, 55},
    {NOTE_E5, 240, 12, 35, 55, 70}
};
static const int kMelodyTurkishMarchLen = sizeof(kMelodyTurkishMarch) / sizeof(kMelodyTurkishMarch[0]);

// Moonlight Sonata (Beethoven) - arpeggiated intro
static const MelodyNote kMelodyMoonlight[] = {
    {NOTE_CS4, 240, 25, 60, 45, 80}, {NOTE_E4, 240, 25, 60, 45, 80}, {NOTE_GS4, 240, 25, 60, 45, 80}, {NOTE_C5, 320, 30, 70, 40, 100}, {NOTE_REST, 140, 0, 0, 0, 0},
    {NOTE_CS4, 240, 25, 60, 45, 80}, {NOTE_E4, 240, 25, 60, 45, 80}, {NOTE_GS4, 240, 25, 60, 45, 80}, {NOTE_B4, 320, 30, 70, 40, 100}, {NOTE_REST, 140, 0, 0, 0, 0},
    {NOTE_B3, 240, 25, 60, 45, 80},  {NOTE_DS4, 240, 25, 60, 45, 80}, {NOTE_G4, 240, 25, 60, 45, 80},  {NOTE_B4, 320, 30, 70, 40, 100}, {NOTE_REST, 140, 0, 0, 0, 0},
    {NOTE_A3, 240, 25, 60, 45, 80},  {NOTE_CS4, 240, 25, 60, 45, 80}, {NOTE_F4, 240, 25, 60, 45, 80},  {NOTE_AS4, 320, 30, 70, 40, 120}, {NOTE_REST, 200, 0, 0, 0, 0}
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
            broadcastAppSettingsUpdate("alarms");
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
                const ADSR env{note.attackMs, note.decayMs, note.sustainPct, note.releaseMs};
                playBuzzerPianoNoteADSR(note.midi, note.durMs, env);
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
