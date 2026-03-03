#include <Arduino.h>

#include "menu.h"
#include "alarm.h"
#include "settings.h"

static int alarmEnabledTemp = 0;
static int alarmHourTemp = 0;
static int alarmMinuteTemp = 0;
static int alarmRepeatTemp = 0;
static int alarmWeeklyDayTemp = 0;
static int alarmAmPmTemp = 0;

static void refreshAlarmTemps()
{
    alarmSlotSelection = constrain(alarmSlotSelection, 0, 2);
    alarmSlotShown = alarmSlotSelection;
    alarmEnabledTemp = alarmEnabled[alarmSlotSelection] ? 1 : 0;
    alarmAmPmTemp = 0;
    alarmHourTemp = alarmHour[alarmSlotSelection];
    alarmMinuteTemp = alarmMinute[alarmSlotSelection];
    alarmRepeatTemp = static_cast<int>(alarmRepeatMode[alarmSlotSelection]);
    alarmWeeklyDayTemp = alarmWeeklyDay[alarmSlotSelection];
    bool use12h = !units.clock24h;
    if (use12h)
    {
        alarmAmPmTemp = (alarmHour[alarmSlotSelection] >= 12) ? 1 : 0;
        int hour12 = alarmHour[alarmSlotSelection] % 12;
        if (hour12 == 0)
            hour12 = 12;
        alarmHourTemp = hour12;
    }
}

// Exposed for InfoModal to refresh alarm fields without resetting scroll
void handleAlarmSlotChangedInModal()
{
    refreshAlarmTemps();
    alarmModal.redraw();
}

void showAlarmSettingsModal()
{
    if (currentMenuLevel != MENU_NONE)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_ALARM;
    menuActive = true;

    refreshAlarmTemps();

    String labels[8];
    InfoFieldType types[8];
    int lineCount = 0;

    int *numberRefs[2];
    int numberCount = 0;
    int *chooserRefs[6];
    const char *const *chooserOpts[6];
    int chooserCounts[6];
    int chooserCount = 0;

    auto addNumberLine = [&](const String &label, int *ref) {
        labels[lineCount] = label;
        types[lineCount++] = InfoNumber;
        numberRefs[numberCount++] = ref;
    };
    auto addChooserLine = [&](const String &label, int *ref, const char *const *opts, int count) {
        labels[lineCount] = label;
        types[lineCount++] = InfoChooser;
        chooserRefs[chooserCount] = ref;
        chooserOpts[chooserCount] = opts;
        chooserCounts[chooserCount] = count;
        ++chooserCount;
    };

    static const char *enableOpts[] = {"Off", "On"};
    static const char *alarmSlotOpts[] = {"1", "2", "3"};
    static const char *repeatOpts[] = {"No Repeat", "Daily", "Weekly", "Weekdays", "Weekend"};
    static const char *dowOpts[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *ampmOpts[] = {"AM", "PM"};
    static const char *alarmSoundOpts[] = {"Tone", "Fur Elise", "Swan Lake", "Turkey March", "Moon Light Sonata"};

    addChooserLine("Select Alarm", &alarmSlotSelection, alarmSlotOpts, 3);
    addChooserLine("Alarm Enabled", &alarmEnabledTemp, enableOpts, 2);
    bool use12h = !units.clock24h;
    if (use12h)
    {
        addChooserLine("AM/PM", &alarmAmPmTemp, ampmOpts, 2);
    }
    addNumberLine(use12h ? "Hour (1-12)" : "Hour (0-23)", &alarmHourTemp);
    addNumberLine("Minute (0-59)", &alarmMinuteTemp);
    addChooserLine("Repeat Mode", &alarmRepeatTemp, repeatOpts, 5);
    addChooserLine("Weekly Day", &alarmWeeklyDayTemp, dowOpts, 7);
    addChooserLine("Alarm Sound", &alarmSoundMode, alarmSoundOpts, 5);

    alarmModal.setLines(labels, types, lineCount);
    alarmModal.setValueRefs(numberRefs, numberCount, chooserRefs, chooserCount, chooserOpts, chooserCounts, nullptr, 0, nullptr);
    alarmModal.setShowNumberArrows(true);
    alarmModal.setShowChooserArrows(true);

    alarmModal.setCallback([](bool /*accepted*/, int) {
        alarmSlotSelection = constrain(alarmSlotSelection, 0, 2);
        if (alarmSlotSelection != alarmSlotShown)
        {
            // Update temp values in-place without hiding/reopening so scroll/position stay intact
            refreshAlarmTemps();
            // Redraw modal in-place without resetting scroll/offset
            alarmModal.redraw();
            return;
        }
        int slot = alarmSlotSelection;
        alarmHourTemp = constrain(alarmHourTemp, 0, 23);
        alarmMinuteTemp = constrain(alarmMinuteTemp, 0, 59);
        alarmWeeklyDayTemp = constrain(alarmWeeklyDayTemp, 0, 6);
        alarmEnabled[slot] = (alarmEnabledTemp > 0);
        int repeatIdx = constrain(alarmRepeatTemp, static_cast<int>(ALARM_REPEAT_NONE), static_cast<int>(ALARM_REPEAT_WEEKEND));
        alarmRepeatMode[slot] = static_cast<AlarmRepeatMode>(repeatIdx);
        bool use12hInner = !units.clock24h;
        if (use12hInner)
        {
            alarmHourTemp = constrain(alarmHourTemp, 1, 12);
            int hourCore = alarmHourTemp % 12;
            if (alarmAmPmTemp > 0)
            {
                hourCore += 12;
            }
            else if (hourCore == 0)
            {
                hourCore = 0;
            }
            alarmHour[slot] = hourCore;
        }
        else
        {
            alarmHour[slot] = constrain(alarmHourTemp, 0, 23);
        }
        alarmMinute[slot] = constrain(alarmMinuteTemp, 0, 59);
        alarmWeeklyDay[slot] = constrain(alarmWeeklyDayTemp, 0, 6);
        alarmSoundMode = constrain(alarmSoundMode, 0, 4);

        refreshAlarmArming();
        saveAlarmSettings();
        notifyAlarmSettingsChanged();

        Serial.printf("[Alarm] Saved enabled=%d time=%02d:%02d repeat=%d weekday=%d\n",
                      alarmEnabled[slot], alarmHour[slot], alarmMinute[slot], alarmRepeatMode[slot], alarmWeeklyDay[slot]);

        alarmModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    });

    alarmModal.show();
}
