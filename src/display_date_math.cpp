#include "display_date_math.h"

#include <stdint.h>

bool isLeapYearGregorian(int year)
{
    if ((year % 400) == 0)
        return true;
    if ((year % 100) == 0)
        return false;
    return (year % 4) == 0;
}

int daysInMonthGregorian(int year, int month)
{
    static const uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 30;
    if (month == 2 && isLeapYearGregorian(year))
        return 29;
    return kDays[month - 1];
}

void addDaysToDate(int &year, int &month, int &day, int deltaDays)
{
    while (deltaDays > 0)
    {
        int dim = daysInMonthGregorian(year, month);
        if (day < dim)
        {
            ++day;
            --deltaDays;
            continue;
        }
        day = 1;
        ++month;
        if (month > 12)
        {
            month = 1;
            ++year;
        }
        --deltaDays;
    }
    while (deltaDays < 0)
    {
        if (day > 1)
        {
            --day;
            ++deltaDays;
            continue;
        }
        --month;
        if (month < 1)
        {
            month = 12;
            --year;
        }
        day = daysInMonthGregorian(year, month);
        ++deltaDays;
    }
}
