#pragma once

struct LunarDate
{
    int day;
    int month;
    int year;
    bool leap;
};

long jdFromDate(int dd, int mm, int yy);
LunarDate convertSolar2Lunar(int dd, int mm, int yy, int timeZoneMinutes);
