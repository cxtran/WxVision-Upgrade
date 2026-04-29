#include "chime_catalog.h"

namespace wxv::audio
{
    namespace
    {
        struct ChimeCatalogEntry
        {
            const char *key;
            const char *label;
            const char *hourlyLabel;
        };

        constexpr ChimeCatalogEntry kChimeCatalog[kChimeCatalogCount] = {
            {"cucook", "Cucook", "Cucook + Time"},
            {"alarm_soft", "Alarm Soft", "Alarm Soft + Time"},
            {"alarm_urgent", "Alarm Urgent", "Alarm Urgent + Time"},
            {"wakeup_birds", "Wakeup Birds", "Wakeup Birds + Time"},
            {"alarm_classic", "Alarm Classic", "Alarm Classic + Time"},
            {"alarm_digital", "Alarm Digital", "Alarm Digital + Time"},
            {"alarm_gentle_chime", "Gentle Chime", "Gentle Chime + Time"}};
    }

    size_t chimeCount()
    {
        return kChimeCatalogCount;
    }

    const char *chimeKeyAt(size_t index)
    {
        if (index >= kChimeCatalogCount)
            return "";
        return kChimeCatalog[index].key;
    }

    const char *chimeLabelAt(size_t index)
    {
        if (index >= kChimeCatalogCount)
            return "";
        return kChimeCatalog[index].label;
    }

    const char *chimeHourlyLabelAt(size_t index)
    {
        if (index >= kChimeCatalogCount)
            return "";
        return kChimeCatalog[index].hourlyLabel;
    }

    int clampChimeIndex(int index)
    {
        if (index < 0)
            return 0;
        const int maxIndex = static_cast<int>(kChimeCatalogCount) - 1;
        if (index > maxIndex)
            return maxIndex;
        return index;
    }
}
