#include <Arduino.h>

#include "menu.h"
#include "settings.h"

void showDeviceSettingsModal()
{
    if (currentMenuLevel != MENU_NONE)
        pushMenu(currentMenuLevel);

    currentMenuLevel = MENU_DEVICE;
    menuActive = true;

    // Settings without Wi-Fi setup
    String labels[] = {"Day Format", "Data Source", "Manual Screen"};
    InfoFieldType types[] = {InfoChooser, InfoChooser, InfoChooser};

    int *chooserRefs[] = {&dayFormat, &dataSource, &manualScreen};
    static const char *dayFmtOpt[] = {"MM/DD", "DD/MM"};
    static const char *dataSourceOpt[] = {"OWM", "WeatherFlow", "None", "Open-Meteo"};
    static const char *manualOpt[] = {"Off", "On"};
    const char *const *chooserOpts[] = {dayFmtOpt, dataSourceOpt, manualOpt};
    int chooserCounts[] = {2, 4, 2};

    // Configure modal (no text fields, no Wi-Fi choosers)
    deviceModal.setLines(labels, types, 3);
    deviceModal.setValueRefs(
        nullptr, 0,                 // no numeric fields
        chooserRefs, 3,             // 3 chooser fields
        chooserOpts, chooserCounts, // their options/counts
        nullptr, 0, nullptr         // no text fields
    );

    deviceModal.setButtons(nullptr, 0);

    deviceModal.setCallback([](bool, int) {
        // Device modal uses chooser-only fields; persist on close because chooser edits
        // do not route through an "accepted=true" callback path.
        setDataSource(dataSource);
        saveDeviceSettings();
        deviceModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    });

    deviceModal.show();
}
