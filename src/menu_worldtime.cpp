#include <Arduino.h>

#include "menu.h"
#include "worldtime.h"
#include "datetimesettings.h"

static void showManageTzModal();
static void queueWorldTimeModal(void (*fn)());
static int manageTzPage = 0;
static int manageTzRestoreSel = -1;

static void queueWorldTimeModal(void (*fn)())
{
    pendingModalFn = fn;
    pendingModalTime = millis() + 10;
}

void showWorldTimeModal()
{
    if (currentMenuLevel != MENU_NONE)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_WORLDTIME;
    menuActive = true;

    loadWorldTimeSettings();
    static int worldAutoCycleTemp = 0;
    worldAutoCycleTemp = worldTimeAutoCycleEnabled() ? 1 : 0;

    String lines[2];
    InfoFieldType types[2];

    lines[0] = "Manage TZ";
    types[0] = InfoLabel;
    lines[1] = "Auto Cycle";
    types[1] = InfoChooser;

    int *chooserRefs[] = {&worldAutoCycleTemp};
    static const char *autoCycleOpts[] = {"Off", "On"};
    const char *const *chooserOpts[] = {autoCycleOpts};
    int chooserCounts[] = {2};

    worldTimeModal.setLines(lines, types, 2);
    worldTimeModal.setValueRefs(nullptr, 0, chooserRefs, 1, chooserOpts, chooserCounts, nullptr, 0, nullptr);
    worldTimeModal.setShowForwardArrow(true);
    worldTimeModal.setForwardArrowOnlyIndex(0);
    worldTimeModal.setKeepOpenOnSelect(false);
    worldTimeModal.setCallback([](bool accepted, int) {
        if (!accepted)
        {
            worldTimeModal.hide();
            currentMenuLevel = MENU_MAIN;
            showMainMenuModal();
            return;
        }

        int sel = worldTimeModal.getSelIndex();
        if (sel == 0)
        {
            queueWorldTimeModal(showManageTzModal);
            return;
        }
        if (sel == 1)
        {
            worldTimeSetAutoCycleEnabled(worldAutoCycleTemp > 0);
            saveWorldTimeSettings();
            queueWorldTimeModal(showWorldTimeModal);
            return;
        }

        queueWorldTimeModal(showWorldTimeModal);
    });
    worldTimeModal.show();
}

static void showManageTzModal()
{
    currentMenuLevel = MENU_WORLDTIME;
    menuActive = true;
    loadWorldTimeSettings();

    const int rowsPerPage = InfoModal::MAX_LINES - 1; // line 1 is "Page X/Y"
    const int tzCount = static_cast<int>(timezoneCount());
    const int customCount = static_cast<int>(worldTimeCustomCityCount());
    const int totalEntries = tzCount + customCount;
    int totalPages = (totalEntries + rowsPerPage - 1) / rowsPerPage;
    if (totalPages < 1)
        totalPages = 1;
    manageTzPage = constrain(manageTzPage, 0, totalPages - 1);

    int start = manageTzPage * rowsPerPage;
    int end = start + rowsPerPage;
    if (end > totalEntries)
        end = totalEntries;

    String lines[InfoModal::MAX_LINES];
    InfoFieldType types[InfoModal::MAX_LINES];
    int lineCount = 0;

    lines[lineCount] = "Page " + String(manageTzPage + 1) + "/" + String(totalPages);
    types[lineCount++] = InfoLabel;

    for (int i = start; i < end && lineCount < InfoModal::MAX_LINES; ++i)
    {
        bool selected = false;
        String label;
        if (i < tzCount)
        {
            selected = worldTimeIsSelected(i);
            label = String(timezoneLabelAt(static_cast<size_t>(i)));
        }
        else
        {
            WorldTimeCustomCity city;
            int customIdx = i - tzCount;
            if (worldTimeGetCustomCity(static_cast<size_t>(customIdx), city))
            {
                selected = city.enabled;
                label = String("Custom: ") + city.name;
            }
            else
            {
                selected = false;
                label = "Custom";
            }
        }
        lines[lineCount] = String(selected ? "[x] " : "[ ] ") + label;
        types[lineCount++] = InfoLabel;
    }

    manageTzModal.setLines(lines, types, lineCount);
    manageTzModal.setShowForwardArrow(false);
    manageTzModal.setForwardArrowOnlyIndex(-1);
    manageTzModal.setKeepOpenOnSelect(true);
    manageTzModal.setCallback([](bool accepted, int) {
        if (!accepted)
        {
            queueWorldTimeModal(showWorldTimeModal);
            return;
        }

        int sel = manageTzModal.getSelIndex();
        if (sel == 0)
        {
            const int rowsPerPage = InfoModal::MAX_LINES - 1;
            const int tzCountNow = static_cast<int>(timezoneCount());
            const int customCountNow = static_cast<int>(worldTimeCustomCityCount());
            const int totalEntriesNow = tzCountNow + customCountNow;
            int totalPagesNow = (totalEntriesNow + rowsPerPage - 1) / rowsPerPage;
            if (totalPagesNow < 1)
                totalPagesNow = 1;

            manageTzPage = (manageTzPage + 1) % totalPagesNow;
            int startNow = manageTzPage * rowsPerPage;
            int endNow = startNow + rowsPerPage;
            if (endNow > totalEntriesNow)
                endNow = totalEntriesNow;

            String refreshLines[InfoModal::MAX_LINES];
            InfoFieldType refreshTypes[InfoModal::MAX_LINES];
            int refreshCount = 0;

            refreshLines[refreshCount] = "Page " + String(manageTzPage + 1) + "/" + String(totalPagesNow);
            refreshTypes[refreshCount++] = InfoLabel;

            for (int i = startNow; i < endNow && refreshCount < InfoModal::MAX_LINES; ++i)
            {
                bool selectedNow = false;
                String labelNow;
                if (i < tzCountNow)
                {
                    selectedNow = worldTimeIsSelected(i);
                    labelNow = String(timezoneLabelAt(static_cast<size_t>(i)));
                }
                else
                {
                    WorldTimeCustomCity cityNow;
                    int customIdxNow = i - tzCountNow;
                    if (worldTimeGetCustomCity(static_cast<size_t>(customIdxNow), cityNow))
                    {
                        selectedNow = cityNow.enabled;
                        labelNow = String("Custom: ") + cityNow.name;
                    }
                    else
                    {
                        selectedNow = false;
                        labelNow = "Custom";
                    }
                }
                refreshLines[refreshCount] = String(selectedNow ? "[x] " : "[ ] ") + labelNow;
                refreshTypes[refreshCount++] = InfoLabel;
            }

            manageTzModal.setLines(refreshLines, refreshTypes, refreshCount);
            manageTzModal.setShowForwardArrow(false);
            manageTzModal.setForwardArrowOnlyIndex(-1);
            manageTzModal.setKeepOpenOnSelect(true);
            manageTzModal.setSelIndex(0);
            manageTzModal.redraw();
            return;
        }

        const int rowsPerPage = InfoModal::MAX_LINES - 1;
        int start = manageTzPage * rowsPerPage;
        int entryIndex = start + (sel - 1);
        const int tzCountNow = static_cast<int>(timezoneCount());
        bool changed = false;
        if (entryIndex >= 0 && entryIndex < tzCountNow)
        {
            changed = worldTimeToggleTimezone(entryIndex);
        }
        else
        {
            int customIdx = entryIndex - tzCountNow;
            WorldTimeCustomCity city;
            if (worldTimeGetCustomCity(static_cast<size_t>(customIdx), city))
            {
                changed = worldTimeSetCustomCityEnabled(static_cast<size_t>(customIdx), !city.enabled);
            }
        }
        if (changed)
        {
            saveWorldTimeSettings();

            const int customCountNow = static_cast<int>(worldTimeCustomCityCount());
            const int totalEntriesNow = tzCountNow + customCountNow;
            int totalPagesNow = (totalEntriesNow + rowsPerPage - 1) / rowsPerPage;
            if (totalPagesNow < 1)
                totalPagesNow = 1;
            manageTzPage = constrain(manageTzPage, 0, totalPagesNow - 1);

            int startNow = manageTzPage * rowsPerPage;
            int endNow = startNow + rowsPerPage;
            if (endNow > totalEntriesNow)
                endNow = totalEntriesNow;

            String refreshLines[InfoModal::MAX_LINES];
            InfoFieldType refreshTypes[InfoModal::MAX_LINES];
            int refreshCount = 0;

            refreshLines[refreshCount] = "Page " + String(manageTzPage + 1) + "/" + String(totalPagesNow);
            refreshTypes[refreshCount++] = InfoLabel;

            for (int i = startNow; i < endNow && refreshCount < InfoModal::MAX_LINES; ++i)
            {
                bool selectedNow = false;
                String labelNow;
                if (i < tzCountNow)
                {
                    selectedNow = worldTimeIsSelected(i);
                    labelNow = String(timezoneLabelAt(static_cast<size_t>(i)));
                }
                else
                {
                    WorldTimeCustomCity cityNow;
                    int customIdxNow = i - tzCountNow;
                    if (worldTimeGetCustomCity(static_cast<size_t>(customIdxNow), cityNow))
                    {
                        selectedNow = cityNow.enabled;
                        labelNow = String("Custom: ") + cityNow.name;
                    }
                    else
                    {
                        selectedNow = false;
                        labelNow = "Custom";
                    }
                }
                refreshLines[refreshCount] = String(selectedNow ? "[x] " : "[ ] ") + labelNow;
                refreshTypes[refreshCount++] = InfoLabel;
            }

            manageTzModal.setLines(refreshLines, refreshTypes, refreshCount);
            manageTzModal.setShowForwardArrow(false);
            manageTzModal.setForwardArrowOnlyIndex(-1);
            manageTzModal.setKeepOpenOnSelect(true);
            manageTzModal.setSelIndex(sel);
            manageTzModal.redraw();
            return;
        }
        manageTzModal.redraw();
    });
    manageTzModal.show();
    if (manageTzRestoreSel >= 0)
    {
        manageTzModal.setSelIndex(manageTzRestoreSel);
        manageTzModal.redraw();
        manageTzRestoreSel = -1;
    }
}
