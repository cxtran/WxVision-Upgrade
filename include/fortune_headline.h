#pragma once

#include <Arduino.h>
#include <pgmspace.h>

// Shared lunar day detail used by deterministic advice + contextual headline.
struct LunarDayDetail
{
    String stem;
    String branch;
    String stemBranch;
    String element;
    int branchIndex;
};

String buildContextHeadline(
    int score,
    int lunarDay, int lunarMonth, int lunarYear, bool lunarLeap,
    const LunarDayDetail &dayInfo);

// Debug helper to verify deterministic phrase picks on a known sample date.
void debugContextHeadlineSample(const LunarDayDetail &dayInfo);

// Flag helpers are implemented in display.cpp and reused by headline builder.
bool isTamNuongDay(int lunarDay);
bool isNguyetKyDay(int lunarDay);
void zodiacCompatByBranch(int branchIndex, String &hop1, String &hop2, String &ky);
