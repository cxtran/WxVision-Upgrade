#include "alarm.h"
#include "settings.h"

static bool s_alarmActive = false;
static bool s_alarmFlashVisible = true;
static unsigned long s_lastFlashToggleMs = 0;
static uint32_t s_lastTriggerMinuteKey = 0;

static constexpr unsigned long kAlarmFlashIntervalMs = 400;

static bool doesAlarmApplyToday(int dayOfWeek)
{
    if (!alarmEnabled)
        return false;

    switch (alarmRepeatMode)
    {
    case ALARM_REPEAT_NONE:
        return alarmOneShotPending;
    case ALARM_REPEAT_DAILY:
        return true;
    case ALARM_REPEAT_WEEKLY:
        return dayOfWeek == alarmWeeklyDay;
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
    if (alarmRepeatMode == ALARM_REPEAT_NONE)
    {
        if (alarmEnabled)
        {
            alarmOneShotPending = true;
        }
        else
        {
            alarmOneShotPending = false;
        }
    }
    else
    {
        alarmOneShotPending = false;
    }
}

static void resetRuntimeAlarm()
{
    s_alarmActive = false;
    s_alarmFlashVisible = true;
    s_lastFlashToggleMs = millis();
    s_lastTriggerMinuteKey = 0;
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
    bool dayMatch = doesAlarmApplyToday(now.dayOfTheWeek());
    bool timeMatch = dayMatch && now.hour() == alarmHour && now.minute() == alarmMinute;

    bool shouldBeActive = false;
    if (timeMatch)
    {
        if (currentMinuteKey != s_lastTriggerMinuteKey)
        {
            s_lastTriggerMinuteKey = currentMinuteKey;
            shouldBeActive = true;
            s_alarmFlashVisible = false;
            s_lastFlashToggleMs = millis();

            if (alarmRepeatMode == ALARM_REPEAT_NONE)
            {
                alarmOneShotPending = false;
                alarmEnabled = false;
                saveAlarmSettings();
            }
        }
        else
        {
            shouldBeActive = true;
        }
    }
    else if (s_lastTriggerMinuteKey != 0 && currentMinuteKey == s_lastTriggerMinuteKey)
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
