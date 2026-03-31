#include "display_lunar_math.h"

#include <math.h>

#if WXV_ENABLE_LUNAR_CALENDAR

static long getNewMoonDay(int k, int timeZoneHours)
{
    double T = k / 1236.85;
    double T2 = T * T;
    double T3 = T2 * T;
    double dr = M_PI / 180.0;
    double Jd1 = 2415020.75933 + 29.53058868 * k
                 + 0.0001178 * T2
                 - 0.000000155 * T3;
    Jd1 += 0.00033 * sin((166.56 + 132.87 * T - 0.009173 * T2) * dr);

    double M = 359.2242 + 29.10535608 * k - 0.0000333 * T2 - 0.00000347 * T3;
    double Mpr = 306.0253 + 385.81691806 * k + 0.0107306 * T2 + 0.00001236 * T3;
    double F = 21.2964 + 390.67050646 * k - 0.0016528 * T2 - 0.00000239 * T3;

    double C1 = (0.1734 - 0.000393 * T) * sin(M * dr)
                + 0.0021 * sin(2 * M * dr)
                - 0.4068 * sin(Mpr * dr)
                + 0.0161 * sin(2 * Mpr * dr)
                - 0.0004 * sin(3 * Mpr * dr)
                + 0.0104 * sin(2 * F * dr)
                - 0.0051 * sin((M + Mpr) * dr)
                - 0.0074 * sin((M - Mpr) * dr)
                + 0.0004 * sin((2 * F + M) * dr)
                - 0.0004 * sin((2 * F - M) * dr)
                - 0.0006 * sin((2 * F + Mpr) * dr)
                + 0.0010 * sin((2 * F - Mpr) * dr)
                + 0.0005 * sin((2 * Mpr + M) * dr);

    double deltaT;
    if (T < -11)
    {
        deltaT = 0.001 + 0.000839 * T + 0.0002261 * T2
                 - 0.00000845 * T3 - 0.000000081 * T * T3;
    }
    else
    {
        deltaT = -0.000278 + 0.000265 * T + 0.000262 * T2;
    }

    double JdNew = Jd1 + C1 - deltaT;
    return (long)floor(JdNew + 0.5 + timeZoneHours / 24.0 + 1e-9);
}

static double getSunLongitude(double jdn)
{
    double T = (jdn - 2451545.0) / 36525.0;
    double T2 = T * T;
    double dr = M_PI / 180.0;
    double M = 357.52910 + 35999.05030 * T - 0.0001559 * T2 - 0.00000048 * T * T2;
    double L0 = 280.46645 + 36000.76983 * T + 0.0003032 * T2;

    double DL = (1.914600 - 0.004817 * T - 0.000014 * T2) * sin(dr * M)
                + (0.019993 - 0.000101 * T) * sin(2 * dr * M)
                + 0.000290 * sin(3 * dr * M);

    double L = L0 + DL;
    L *= dr;
    L -= 2 * M_PI * floor(L / (2 * M_PI));
    return L;
}

long jdFromDate(int dd, int mm, int yy)
{
    int a = (14 - mm) / 12;
    int y = yy + 4800 - a;
    int m = mm + 12 * a - 3;
    long jd = dd + (153 * m + 2) / 5 + 365L * y + y / 4 - y / 100 + y / 400 - 32045;
    return jd;
}

static long getLunarMonth11(int yy, int timeZoneHours)
{
    long off = jdFromDate(31, 12, yy) - 2415021;
    int k = (int)(off / 29.530588853);
    long nm = getNewMoonDay(k, timeZoneHours);
    double sunLong = getSunLongitude((double)nm);
    if (sunLong > 3 * M_PI / 2)
    {
        nm = getNewMoonDay(k - 1, timeZoneHours);
    }
    return nm;
}

static int getLeapMonthOffset(double a11, int timeZoneHours)
{
    int k = (int)((a11 - 2415021.076998695) / 29.530588853 + 0.5);
    int last = 0;
    int i = 1;
    double arc = getSunLongitude((double)getNewMoonDay(k + i, timeZoneHours));
    double lastArc;
    do
    {
        last = i;
        lastArc = arc;
        i++;
        arc = getSunLongitude((double)getNewMoonDay(k + i, timeZoneHours));
    } while (arc != lastArc && i < 14);
    return last - 1;
}

LunarDate convertSolar2Lunar(int dd, int mm, int yy, int timeZoneMinutes)
{
    int timeZoneHours = timeZoneMinutes / 60;
    long dayNumber = jdFromDate(dd, mm, yy);
    long k = (long)((dayNumber - 2415021.076998695) / 29.530588853);
    long monthStart = getNewMoonDay((int)(k + 1), timeZoneHours);
    if (monthStart > dayNumber)
    {
        monthStart = getNewMoonDay((int)k, timeZoneHours);
    }

    long a11 = getLunarMonth11(yy, timeZoneHours);
    long b11 = a11;
    int lunarYear;
    if (a11 >= monthStart)
    {
        lunarYear = yy;
        a11 = getLunarMonth11(yy - 1, timeZoneHours);
    }
    else
    {
        lunarYear = yy + 1;
        b11 = getLunarMonth11(yy + 1, timeZoneHours);
    }

    int lunarDay = (int)(dayNumber - monthStart + 1);
    int diff = (int)((monthStart - a11) / 29);
    int lunarMonth = diff + 11;
    bool lunarLeap = false;

    if (b11 - a11 > 365)
    {
        int leapMonthDiff = getLeapMonthOffset(a11, timeZoneHours);
        if (diff >= leapMonthDiff)
        {
            lunarMonth = diff + 10;
            if (diff == leapMonthDiff)
            {
                lunarLeap = true;
            }
        }
    }

    if (lunarMonth > 12)
    {
        lunarMonth -= 12;
    }
    if (lunarMonth >= 11 && diff < 4)
    {
        lunarYear--;
    }

    LunarDate ld;
    ld.day = lunarDay;
    ld.month = lunarMonth;
    ld.year = lunarYear;
    ld.leap = lunarLeap;
    return ld;
}

#else

long jdFromDate(int dd, int mm, int yy)
{
    (void)dd;
    (void)mm;
    (void)yy;
    return 0;
}

LunarDate convertSolar2Lunar(int dd, int mm, int yy, int timeZoneMinutes)
{
    (void)dd;
    (void)mm;
    (void)yy;
    (void)timeZoneMinutes;

    LunarDate ld{};
    return ld;
}

#endif
