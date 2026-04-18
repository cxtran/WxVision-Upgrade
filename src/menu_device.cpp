#include <Arduino.h>

#include "menu.h"
#include "settings.h"

namespace
{
const char *dataSourceMenuLabel(int source)
{
    switch (source)
    {    
    case DATA_SOURCE_OWM:
        return "OpenWeather";
    case DATA_SOURCE_WEATHERFLOW:
        return "WeatherFlow";
    case DATA_SOURCE_NONE:
        return "None";
    case DATA_SOURCE_OPEN_METEO:
        return "Open-Meteo";
    default:
        return "OpenWeather";   
    }
}
}

void showDataSourceSelectionModal()
{
    currentMenuLevel = MENU_DEVICE;
    menuActive = true;

    String labels[] = {"OpenWeather", "WeatherFlow", "None", "Open-Meteo"};
    InfoFieldType types[] = {InfoButton, InfoButton, InfoButton, InfoButton};

    dataSourceModal.setLines(labels, types, 4);
    dataSourceModal.setValueRefs(nullptr, 0, nullptr, 0, nullptr, nullptr, nullptr, 0, nullptr);
    dataSourceModal.setShowForwardArrow(true);

    int selectedIndex = dataSource;
    if (selectedIndex < DATA_SOURCE_OWM || selectedIndex > DATA_SOURCE_OPEN_METEO)
        selectedIndex = DATA_SOURCE_OWM;
    dataSourceModal.setSelIndex(selectedIndex);

    dataSourceModal.setCallback([](bool accepted, int) {
        if (!accepted)
        {
            dataSourceModal.hide();
            showDeviceSettingsModal();
            return;
        }

        const int selectedSource = dataSourceModal.getSelIndex();
        setDataSource(selectedSource);
        saveDeviceSettings();
        reset_Time_and_Date_Display = true;

        dataSourceModal.hide();
        showDeviceSettingsModal();
    });

    dataSourceModal.show();
}

void showDeviceSettingsModal()
{
    // Rebuilding the device modal from within the device flow should not
    // push MENU_DEVICE again, or the header back action will reopen itself.
    if (currentMenuLevel != MENU_NONE && currentMenuLevel != MENU_DEVICE)
        pushMenu(currentMenuLevel);

    currentMenuLevel = MENU_DEVICE;
    menuActive = true;

    String dataSourceLabel = "Data Source: ";
    dataSourceLabel += dataSourceMenuLabel(dataSource);
    static int debugMemoryLogsChoice = 0;
    debugMemoryLogsChoice = debugMemoryLogs ? 1 : 0;

    String labels[] = {dataSourceLabel, "Manual Screen", "Debug Memory Logs"};
    InfoFieldType types[] = {InfoButton, InfoChooser, InfoChooser};

    int *chooserRefs[] = {&manualScreen, &debugMemoryLogsChoice};
    static const char *manualOpt[] = {"Off", "On"};
    static const char *toggleOpts[] = {"Off", "On"};
    const char *const *chooserOpts[] = {manualOpt, toggleOpts};
    int chooserCounts[] = {2, 2};

    deviceModal.setLines(labels, types, 3);
    deviceModal.setValueRefs(
        nullptr, 0,
        chooserRefs, 2,
        chooserOpts, chooserCounts,
        nullptr, 0, nullptr);

    deviceModal.setShowForwardArrow(true);
    deviceModal.setButtons(nullptr, 0);

    deviceModal.setCallback([](bool accepted, int) {
        const int selectedLine = deviceModal.getSelIndex();
        if (!accepted)
        {
            saveDeviceSettings();
            return;
        }

        if (selectedLine == 0)
        {
            deviceModal.hide();
            pendingModalFn = showDataSourceSelectionModal;
            pendingModalTime = millis() + 10;
            return;
        }

        debugMemoryLogs = (debugMemoryLogsChoice > 0);
        saveDeviceSettings();
    });

    deviceModal.show();
}
