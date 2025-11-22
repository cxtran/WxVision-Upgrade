#include "alarm.h"
#include "settings.h"
#include "buzzer.h"

static bool s_alarmActive = false;
static bool s_alarmFlashVisible = true;
static unsigned long s_lastFlashToggleMs = 0;
static uint32_t s_lastTriggerMinuteKey = 0;
static uint32_t s_silencedMinuteKey = 0;
static int s_activeSlot = -1;

static constexpr unsigned long kAlarmFlashIntervalMs = 400;

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
    s_lastTriggerMinuteKey = 0;
    s_silencedMinuteKey = 0;
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
    }
    else
    {
        s_alarmFlashVisible = true;
        s_lastFlashToggleMs = nowMs;
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
    stopAlarmBuzzer();
}

void stopAlarmBuzzer()
{
    stopBuzzer();
}
