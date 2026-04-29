#pragma once

#include <Arduino.h>
#include <stddef.h>

namespace wxv::audio
{
    constexpr size_t kChimeCatalogCount = 7;

    size_t chimeCount();
    const char *chimeKeyAt(size_t index);
    const char *chimeLabelAt(size_t index);
    const char *chimeHourlyLabelAt(size_t index);
    int clampChimeIndex(int index);
}
