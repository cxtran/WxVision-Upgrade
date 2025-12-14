#include <Arduino.h>
#include <ctype.h>
#include "display.h"
#include "icons.h"
#include "settings.h"
#include "RTClib.h" // For RTC DateTime
#include "sensors.h"
// #include "fonts/FreeSans9pt7b.h"
// #include "fonts/tahomabd7pt7b.h"
// #include "fonts/tahomabd8pt7b.h"
#include "fonts/verdanab8pt7b.h"
#include "datetimesettings.h"
#include "units.h"
#include "env_quality.h"
#include "alarm.h"

#include "tempest.h"
#include "weather_countries.h"
#include "InfoModal.h"
#include <math.h>
#include "ScrollLine.h"

extern float aht20_temp;
extern float SCD40_temp;
extern float SCD40_hum;
extern float aht20_hum;
extern int scrollLevel;
extern int scrollSpeed;
extern void displayClock();
extern void displayDate();
extern void displayWeatherData();
extern int theme;
extern TempestData tempest;
extern CurrentConditions currentCond;
extern const ScreenMode InfoScreenModes[];
extern const int NUM_INFOSCREENS;
extern bool isDataSourceOwm();
extern bool isDataSourceWeatherFlow();
extern bool isDataSourceNone();
extern String str_Temp;
extern String str_Humd;
extern String str_Weather_Conditions_Des;
extern const uint8_t *getWeatherIconFromCondition(String condition);
extern int humOffset;
extern byte t_hour;
extern byte t_minute;
extern byte d_day;
extern byte d_month;
extern int d_year;
/*
const ScreenMode InfoScreenModes[] = {
    SCREEN_UDP_DATA,
    SCREEN_UDP_FORECAST,
    // ...add more InfoScreen-based modes here
};
*/
// const int NUM_INFOSCREENS = sizeof(InfoScreenModes) / sizeof(InfoScreenModes[0]);

MatrixPanel_I2S_DMA *dma_display = nullptr;
uint8_t currentPanelBrightness = 0;

uint16_t myRED, myGREEN, myBLUE, myWHITE, myBLACK, myYELLOW, myCYAN;

// ---------- Vietnamese lunar calendar helpers ----------
struct LunarDate
{
    int day;
    int month;
    int year;
    bool leap;
};

static long jdFromDate(int dd, int mm, int yy)
{
    int a = (14 - mm) / 12;
    int y = yy + 4800 - a;
    int m = mm + 12 * a - 3;
    long jd = dd + (153 * m + 2) / 5 + 365L * y + y / 4 - y / 100 + y / 400 - 32045;
    return jd;
}

static double jdToDate(long jd, int &dd, int &mm, int &yy)
{
    long a = jd + 32044;
    long b = (4 * a + 3) / 146097;
    long c = a - (146097 * b) / 4;
    long d = (4 * c + 3) / 1461;
    long e = c - (1461 * d) / 4;
    long m = (5 * e + 2) / 153;

    dd = (int)(e - (153 * m + 2) / 5 + 1);
    mm = (int)(m + 3 - 12 * (m / 10));
    yy = (int)(100 * b + d - 4800 + m / 10);
    return 0.0;
}

static double getNewMoonDay(int k, int timeZoneHours)
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
    return JdNew + 0.5 + timeZoneHours / 24.0;
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

static long getLunarMonth11(int yy, int timeZoneHours)
{
    long off = jdFromDate(31, 12, yy) - 2415021;
    int k = (int)(off / 29.530588853);
    double nm = getNewMoonDay(k, timeZoneHours);
    double sunLong = getSunLongitude(nm);
    if (sunLong > 3 * M_PI / 2)
    {
        nm = getNewMoonDay(k - 1, timeZoneHours);
    }
    return (long)nm;
}

static int getLeapMonthOffset(double a11, int timeZoneHours)
{
    int k = (int)((a11 - 2415021.076998695) / 29.530588853 + 0.5);
    int last = 0;
    int i = 1;
    double arc = getSunLongitude(getNewMoonDay(k + i, timeZoneHours));
    double lastArc;
    do
    {
        last = i;
        lastArc = arc;
        i++;
        arc = getSunLongitude(getNewMoonDay(k + i, timeZoneHours));
    } while (arc != lastArc && i < 14);
    return last - 1;
}

static LunarDate convertSolar2Lunar(int dd, int mm, int yy, int timeZoneMinutes)
{
    int timeZoneHours = timeZoneMinutes / 60;
    long dayNumber = jdFromDate(dd, mm, yy);
    long k = (long)((dayNumber - 2415021.076998695) / 29.530588853);
    double monthStart = getNewMoonDay((int)(k + 1), timeZoneHours);
    if (monthStart > dayNumber + 0.5)
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

    int lunarDay = (int)(dayNumber - (long)monthStart + 1);
    int diff = (int)(((long)monthStart - a11) / 29);
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

static void buildLunarYearNames(int lunarYear,
                                String &stemBranchVi,
                                String &zodiacVi,
                                String &yearEn,
                                String &animalVi,
                                String &animalEn)
{
    const char *stemsVi[10] = {"Giap", "At", "Binh", "Dinh", "Mau", "Ky", "Canh", "Tan", "Nham", "Quy"};
    const char *branchesVi[12] = {"Ty", "Suu", "Dan", "Mao", "Thin", "Ty", "Ngo", "Mui", "Than", "Dau", "Tuat", "Hoi"};
    const char *animalsVi[12] = {"Chuot", "Trau", "Ho", "Meo", "Rong", "Ran", "Ngua", "De", "Khi", "Ga", "Cho", "Heo"};
    const char *animalsEn[12] = {"Rat", "Ox", "Tiger", "Cat", "Dragon", "Snake", "Horse", "Goat", "Monkey", "Rooster", "Dog", "Pig"};

    const char *elementsEn[5] = {"Wood", "Fire", "Earth", "Metal", "Water"};

    int stemIndex = (lunarYear + 6) % 10;
    int branchIndex = (lunarYear + 8) % 12;

    stemBranchVi = String(stemsVi[stemIndex]) + " " + branchesVi[branchIndex];
    zodiacVi = String("Nam con ") + animalsVi[branchIndex];
    animalVi = String(animalsVi[branchIndex]);

    int elementIndex = stemIndex / 2;
    yearEn = String(elementsEn[elementIndex]) + " " + animalsEn[branchIndex];
    animalEn = String(animalsEn[branchIndex]);
}

static String buildOrdinal(int day)
{
    int mod10 = day % 10;
    int mod100 = day % 100;
    if (mod10 == 1 && mod100 != 11)
        return String("st");
    if (mod10 == 2 && mod100 != 12)
        return String("nd");
    if (mod10 == 3 && mod100 != 13)
        return String("rd");
    return String("th");
}

// Forward declaration so we reuse the global 12/24h formatter
static String formatConditionSceneTimeTag();

static String formatLunarHourTag()
{
    // Map local hour (0–23) to Vietnamese lunar hour name
    static const char *names[12] = {
        "Gio Ty",  "Gio Suu",  "Gio Dan",  "Gio Mao",
        "Gio Thin","Gio Ty",   "Gio Ngo",  "Gio Mui",
        "Gio Than","Gio Dau",  "Gio Tuat", "Gio Hoi"};

    int hour = t_hour;
    if (hour < 0 || hour > 23)
        hour = 0;

    int idx;
    if (hour == 23)
        idx = 0;
    else
        idx = (hour + 1) / 2;
    idx %= 12;

    return String(names[idx]);
}

static String formatLunarClockTag()
{
    // Use the same 12/24h formatting as the rest of the app
    return formatConditionSceneTimeTag();
}

static String formatSolarTermTag()
{
    int m = d_month;
    int d = d_day;

    switch (m)
    {
    case 1:
        return (d <= 15) ? String("Tieu Han") : String("Dai Han");
    case 2:
        return (d <= 15) ? String("Lap Xuan") : String("Vu Thuy");
    case 3:
        return (d <= 15) ? String("Kinh Thap") : String("Xuan Phan");
    case 4:
        return (d <= 15) ? String("Thanh Minh") : String("Coc Vu");
    case 5:
        return (d <= 15) ? String("Lap Ha") : String("Tieu Man");
    case 6:
        return (d <= 15) ? String("Mang Chung") : String("Ha Chi");
    case 7:
        return (d <= 15) ? String("Tieu Thu") : String("Dai Thu");
    case 8:
        return (d <= 15) ? String("Lap Thu") : String("Xu Thu");
    case 9:
        return (d <= 15) ? String("Bach Lo") : String("Thu Phan");
    case 10:
        return (d <= 15) ? String("Han Lo") : String("Suong Giang");
    case 11:
        return (d <= 15) ? String("Lap Dong") : String("Tieu Tuyet");
    case 12:
        return (d <= 15) ? String("Dai Tuyet") : String("Dong Chi");
    default:
        return String("");
    }
}

static String formatLunarDayName(int dd, int mm, int yy)
{
    long jd = jdFromDate(dd, mm, yy);

    static const char *stemsVi[10] = {"Giap", "At", "Binh", "Dinh", "Mau", "Ky", "Canh", "Tan", "Nham", "Quy"};
    static const char *branchesVi[12] = {"Ty", "Suu", "Dan", "Mao", "Thin", "Ty", "Ngo", "Mui", "Than", "Dau", "Tuat", "Hoi"};

    int stemIndex = (int)((jd + 9) % 10);
    int branchIndex = (int)((jd + 1) % 12);
    if (stemIndex < 0)
        stemIndex += 10;
    if (branchIndex < 0)
        branchIndex += 12;

    return String(stemsVi[stemIndex]) + " " + branchesVi[branchIndex];
}

// Lunar marquee state (merged screen)
static String lunarLines[3];
static uint16_t lunarWidths[3] = {0, 0, 0};
static int lunarOffsets[3] = {0, 0, 0};
static unsigned long lastLunarTick = 0;
static bool lunarInitialized = false;

static void renderLunarLines(const String lines[3], const uint16_t widths[3], const int offsets[3])
{
    dma_display->fillScreen(0);

    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);

    uint16_t headerBg, headerFg, underlineColor, bodyColor;
    uint16_t lineColors[3];
    if (theme == 1)
    {
        headerBg = dma_display->color565(20, 20, 40);
        headerFg = dma_display->color565(60, 60, 120);
        underlineColor = dma_display->color565(30, 30, 70);
        bodyColor = dma_display->color565(90, 90, 150);
        lineColors[0] = dma_display->color565(150, 150, 220); // day name highlight
        lineColors[1] = dma_display->color565(110, 110, 190); // solar term
        lineColors[2] = dma_display->color565(200, 200, 255); // marquee detail
    }
    else
    {
        headerBg = INFOMODAL_HEADERBG;
        headerFg = INFOMODAL_GREEN;
        underlineColor = INFOMODAL_ULINE;
        bodyColor = INFOMODAL_UNSEL;
        lineColors[0] = INFOMODAL_SEL;      // bright for day name
        lineColors[1] = bodyColor;          // standard for solar term
        lineColors[2] = INFOMODAL_EDIT;     // accent for marquee detail
    }

    // --- Header bar ---
    const int headerHeight = 8;
    dma_display->fillRect(0, 0, PANEL_RES_X, headerHeight, headerBg);

    dma_display->setTextColor(headerFg);
    const char *title = "Lunar Date";
    int16_t tx1, ty1;
    uint16_t tw, th;
    dma_display->getTextBounds(title, 0, 0, &tx1, &ty1, &tw, &th);
    int titleX = (PANEL_RES_X - (int)tw) / 2;
    if (titleX < 0)
        titleX = 0;
    dma_display->setCursor(titleX, 0);
    dma_display->print(title);

    // Header underline
    dma_display->drawFastHLine(0, headerHeight - 1, PANEL_RES_X, underlineColor);

    // --- Lines below title ---
    for (int i = 0; i < 3; ++i)
    {
        int y = headerHeight + 8 * (i + 1) - 8; // 8,16,24
        int w = widths[i];
        if (w <= 0)
            continue;

        int baseX;
        if (i < 2)
        {
            // Line 1 + 2: static centered
            baseX = (PANEL_RES_X - w) / 2;
            if (baseX < 0)
                baseX = 0;
        }
        else
        {
            // Line 3: marquee
            baseX = PANEL_RES_X - offsets[2];
        }

        dma_display->setTextColor(lineColors[i]);
        dma_display->setCursor(baseX, y);
        dma_display->print(lines[i]);
    }
}

static void buildLunarLinesMerged()
{
    getTimeFromRTC();
    int dd = d_day;
    int mm = d_month;
    int yy = d_year;

    int tzMinutes = tzStandardOffset;
    LunarDate ld = convertSolar2Lunar(dd, mm, yy, tzMinutes);

    String stemBranchVi, zodiacVi, yearEn, animalVi, animalEn;
    buildLunarYearNames(ld.year, stemBranchVi, zodiacVi, yearEn, animalVi, animalEn);

    // Header Line 1: date name (Can Chi for the day)
    lunarLines[0] = formatLunarDayName(dd, mm, yy);

    // Header Line 2: Lap Dong (solar term, updated by date)
    lunarLines[1] = formatSolarTermTag();

    // Detail pieces for combined marquee line
    // e.g. "Ngay 28/10 Nam At Ty"
    String viDetail = String("Ngay ") + ld.day + "/" + ld.month +
                      " Nam " + stemBranchVi;

    // English year line: "The year of Wood Snake"
    String enDetail = String("The year of ") + yearEn;

    // Hour detail: e.g. "Gio Hoi  / 11:24 PM"
    String lunarHour = formatLunarHourTag();
    String clockTag = formatLunarClockTag();
    String hourDetail = lunarHour + "  / " + clockTag;

    // Line 3: combined marquee
    // Ngay 28/10 Nam At Ty ¦ The year of Wood Snake ¦ Gio Hoi  / 11:24 PM
    lunarLines[2] = viDetail + " \xC2\xA6  " + enDetail + "  \xC2\xA6  " + hourDetail;

    for (int i = 0; i < 3; ++i)
    {
        lunarWidths[i] = getTextWidth(lunarLines[i].c_str());
        if (lunarWidths[i] <= 0)
            lunarWidths[i] = 1;
    }
    for (int i = 0; i < 3; ++i)
        lunarOffsets[i] = 0;
    lastLunarTick = millis();
    lunarInitialized = true;
}

static int envScoreForBand(EnvBand band)
{
    switch (band)
    {
    case EnvBand::Good:
        return 3;
    case EnvBand::Moderate:
        return 2;
    case EnvBand::Poor:
        return 1;
    case EnvBand::Critical:
        return 0;
    default:
        return -1;
    }
}

static EnvBand envBandFromIndex(float idx)
{
    if (idx < 0.0f)
        return EnvBand::Unknown;
    if (idx >= 75.0f)
        return EnvBand::Good;
    if (idx >= 50.0f)
        return EnvBand::Moderate;
    if (idx >= 25.0f)
        return EnvBand::Poor;
    return EnvBand::Critical;
}

static EnvBand envBandFromCo2(float co2)
{
    if (isnan(co2) || co2 <= 0.0f)
        return EnvBand::Unknown;
    if (co2 <= 800.0f)
        return EnvBand::Good;
    if (co2 <= 1200.0f)
        return EnvBand::Moderate;
    if (co2 <= 2000.0f)
        return EnvBand::Poor;
    return EnvBand::Critical;
}

static EnvBand envBandFromTemp(float tempC)
{
    if (isnan(tempC))
        return EnvBand::Unknown;
    if (tempC >= 20.0f && tempC <= 24.0f)
        return EnvBand::Good;
    if ((tempC >= 18.0f && tempC < 20.0f) || (tempC > 24.0f && tempC <= 26.0f))
        return EnvBand::Moderate;
    if ((tempC >= 16.0f && tempC < 18.0f) || (tempC > 26.0f && tempC <= 28.0f))
        return EnvBand::Poor;
    return EnvBand::Critical;
}

static EnvBand envBandFromHumidity(float humidity)
{
    if (isnan(humidity))
        return EnvBand::Unknown;
    if (humidity >= 35.0f && humidity <= 55.0f)
        return EnvBand::Good;
    if ((humidity >= 30.0f && humidity < 35.0f) || (humidity > 55.0f && humidity <= 60.0f))
        return EnvBand::Moderate;
    if ((humidity >= 25.0f && humidity < 30.0f) || (humidity > 60.0f && humidity <= 70.0f))
        return EnvBand::Poor;
    return EnvBand::Critical;
}

static EnvBand envBandFromPressure(float pressure)
{
    if (isnan(pressure) || pressure < 200.0f)
        return EnvBand::Unknown;
    if (pressure >= 995.0f && pressure <= 1025.0f)
        return EnvBand::Good;
    if ((pressure >= 985.0f && pressure < 995.0f) || (pressure > 1025.0f && pressure <= 1035.0f))
        return EnvBand::Moderate;
    if ((pressure >= 970.0f && pressure < 985.0f) || (pressure > 1035.0f && pressure <= 1045.0f))
        return EnvBand::Poor;
    return EnvBand::Critical;
}

static uint16_t envColorForBand(EnvBand band)
{
    const bool monoTheme = (theme == 1);
    if (monoTheme)
    {
        switch (band)
        {
        case EnvBand::Good:
            return dma_display->color565(120, 120, 220);
        case EnvBand::Moderate:
            return dma_display->color565(90, 90, 180);
        case EnvBand::Poor:
            return dma_display->color565(70, 70, 150);
        case EnvBand::Critical:
            return dma_display->color565(50, 50, 110);
        default:
            return dma_display->color565(80, 80, 140);
        }
    }

    switch (band)
    {
    case EnvBand::Good:
        return dma_display->color565(54, 196, 93);
    case EnvBand::Moderate:
        return dma_display->color565(241, 196, 15);
    case EnvBand::Poor:
        return dma_display->color565(230, 126, 34);
    case EnvBand::Critical:
        return dma_display->color565(231, 76, 60);
    default:
        return dma_display->color565(120, 120, 120);
    }
}

static uint16_t scaleColor565(uint16_t color, float intensity)
{
    if (intensity <= 0.0f)
        return 0;
    if (intensity >= 1.0f)
        return color;

    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;

    int newR = static_cast<int>(r * intensity + 0.5f);
    int newG = static_cast<int>(g * intensity + 0.5f);
    int newB = static_cast<int>(b * intensity + 0.5f);

    if (newR > 31)
        newR = 31;
    if (newG > 63)
        newG = 63;
    if (newB > 31)
        newB = 31;
    if (newR < 0)
        newR = 0;
    if (newG < 0)
        newG = 0;
    if (newB < 0)
        newB = 0;

    return static_cast<uint16_t>((newR << 11) | (newG << 5) | newB);
}

static uint16_t lerpColor565(uint16_t a, uint16_t b, float t)
{
    if (t <= 0.0f)
        return a;
    if (t >= 1.0f)
        return b;

    int ar = (a >> 11) & 0x1F;
    int ag = (a >> 5) & 0x3F;
    int ab = a & 0x1F;

    int br = (b >> 11) & 0x1F;
    int bg = (b >> 5) & 0x3F;
    int bb = b & 0x1F;

    int nr = static_cast<int>(ar + (br - ar) * t + 0.5f);
    int ng = static_cast<int>(ag + (bg - ag) * t + 0.5f);
    int nb = static_cast<int>(ab + (bb - ab) * t + 0.5f);

    nr = constrain(nr, 0, 31);
    ng = constrain(ng, 0, 63);
    nb = constrain(nb, 0, 31);

    return static_cast<uint16_t>((nr << 11) | (ng << 5) | nb);
}

static void drawVerticalGradient(uint16_t topColor, uint16_t bottomColor)
{
    for (int y = 0; y < PANEL_RES_Y; ++y)
    {
        float t = (PANEL_RES_Y <= 1) ? 0.0f : static_cast<float>(y) / (PANEL_RES_Y - 1);
        uint16_t color = lerpColor565(topColor, bottomColor, t);
        dma_display->drawFastHLine(0, y, PANEL_RES_X, color);
    }
}

static void drawGroundBand(uint16_t color, int height = 8)
{
    if (height <= 0)
        return;
    int y = PANEL_RES_Y - height;
    if (y < 0)
        y = 0;
    dma_display->fillRect(0, y, PANEL_RES_X, height, color);
}

static void drawSkyGradient(const uint16_t *colors, int count)
{
    if (count <= 0)
    {
        dma_display->fillScreen(0);
        return;
    }
    if (count == 1)
    {
        dma_display->fillScreen(colors[0]);
        return;
    }

    const int totalRows = PANEL_RES_Y;
    for (int y = 0; y < totalRows; ++y)
    {
        float globalT = (totalRows <= 1) ? 0.0f : static_cast<float>(y) / (totalRows - 1);
        float scaled = globalT * (count - 1);
        int idx = static_cast<int>(scaled);
        if (idx >= count - 1)
            idx = count - 2;
        float localT = scaled - idx;
        uint16_t rowColor = lerpColor565(colors[idx], colors[idx + 1], localT);
        dma_display->drawFastHLine(0, y, PANEL_RES_X, rowColor);
    }
}

static void drawGroundGradient(const uint16_t *colors, int count, int height)
{
    if (height <= 0 || colors == nullptr || count <= 0)
        return;
    if (height > PANEL_RES_Y)
        height = PANEL_RES_Y;

    int startY = PANEL_RES_Y - height;
    for (int y = 0; y < height; ++y)
    {
        float globalT = (height <= 1) ? 0.0f : static_cast<float>(y) / (height - 1);
        if (count == 1)
        {
            dma_display->drawFastHLine(0, startY + y, PANEL_RES_X, colors[0]);
            continue;
        }
        float scaled = globalT * (count - 1);
        int idx = static_cast<int>(scaled);
        if (idx >= count - 1)
            idx = count - 2;
        float localT = scaled - idx;
        uint16_t rowColor = lerpColor565(colors[idx], colors[idx + 1], localT);
        dma_display->drawFastHLine(0, startY + y, PANEL_RES_X, rowColor);
    }
}

static void drawSun(int centerX, int centerY, int radius, uint16_t baseColor)
{
    if (radius < 3)
        radius = 3;
    centerX = constrain(centerX, radius, PANEL_RES_X - 1 - radius);
    centerY = constrain(centerY, radius, PANEL_RES_Y - 1 - radius);

    dma_display->fillCircle(centerX, centerY, radius, baseColor);
    dma_display->drawCircle(centerX, centerY, radius + 1, scaleColor565(baseColor, 0.7f));
}

static void drawCloud(int x, int y, uint16_t color, int radius)
{
    if (radius < 2)
        radius = 2;

    dma_display->fillCircle(x, y, radius, color);
    dma_display->fillCircle(x - radius, y + 2, radius - 1, color);
    dma_display->fillCircle(x + radius, y + 2, radius - 1, color);
    dma_display->fillCircle(x - radius / 2, y + radius, radius - 2, color);
    dma_display->fillCircle(x + radius / 2, y + radius, radius - 2, color);

    int bodyWidth = radius * 2 + 6;
    dma_display->fillRect(x - bodyWidth / 2, y + radius - 1, bodyWidth, radius + 2, color);
}

static void drawSnowflake(int x, int y, uint16_t color)
{
    if (x < 0 || x >= PANEL_RES_X || y < 0 || y >= PANEL_RES_Y)
        return;

    dma_display->drawPixel(x, y, color);
    if (x > 0)
        dma_display->drawPixel(x - 1, y, color);
    if (x < PANEL_RES_X - 1)
        dma_display->drawPixel(x + 1, y, color);
    if (y > 0)
        dma_display->drawPixel(x, y - 1, color);
    if (y < PANEL_RES_Y - 1)
        dma_display->drawPixel(x, y + 1, color);
}

static void drawStar(int x, int y, uint16_t color)
{
    if (x < 1 || x >= PANEL_RES_X - 1 || y < 1 || y >= PANEL_RES_Y - 1)
        return;

    dma_display->drawPixel(x, y, color);
    dma_display->drawPixel(x - 1, y, color);
    dma_display->drawPixel(x + 1, y, color);
    dma_display->drawPixel(x, y - 1, color);
    dma_display->drawPixel(x, y + 1, color);
}

static void drawCompactCloud(int cx, int cy, uint16_t color)
{
    uint16_t shade = scaleColor565(color, 0.85f);
    dma_display->fillCircle(cx, cy, 4, color);
    dma_display->fillCircle(cx - 4, cy + 1, 3, shade);
    dma_display->fillCircle(cx + 4, cy + 1, 3, shade);
    dma_display->fillCircle(cx - 2, cy + 3, 2, color);
    dma_display->fillCircle(cx + 2, cy + 3, 2, color);
}

static void drawWeatherSceneSunny()
{
    uint16_t skyColors[] = {
        dma_display->color565(15, 105, 185),
        dma_display->color565(30, 140, 205),
        dma_display->color565(55, 175, 220),
        dma_display->color565(85, 205, 235)
    };
    drawSkyGradient(skyColors, sizeof(skyColors) / sizeof(skyColors[0]));

    uint16_t horizonGlow = dma_display->color565(255, 220, 120);
    uint16_t fieldBase = dma_display->color565(215, 160, 55);
    dma_display->fillRect(0, PANEL_RES_Y - 6, PANEL_RES_X, 6, fieldBase);
    dma_display->fillRect(0, PANEL_RES_Y - 7, PANEL_RES_X, 1, horizonGlow);
    for (int x = 1; x < PANEL_RES_X; x += 5)
    {
        int blades = (x % 10 == 0) ? 5 : 3;
        dma_display->drawFastVLine(x, PANEL_RES_Y - blades - 3, blades, horizonGlow);
    }
    dma_display->drawFastHLine(0, PANEL_RES_Y - 1, PANEL_RES_X, dma_display->color565(150, 90, 30));

    uint16_t sunColor = dma_display->color565(255, 240, 90);
    int sunX = 12;
    int sunY = 11;
    dma_display->fillCircle(sunX, sunY, 7, sunColor);
    dma_display->fillCircle(sunX, sunY, 4, dma_display->color565(255, 255, 120));
    dma_display->drawCircle(sunX, sunY, 7, scaleColor565(sunColor, 0.8f));

}

static void drawWeatherSceneCloudy()
{
    uint16_t skyColors[] = {
        dma_display->color565(20, 110, 190),
        dma_display->color565(35, 140, 205),
        dma_display->color565(55, 170, 215),
        dma_display->color565(85, 200, 225)
    };
    drawSkyGradient(skyColors, sizeof(skyColors) / sizeof(skyColors[0]));

    uint16_t horizonGlow = dma_display->color565(240, 210, 125);
    uint16_t fieldBase = dma_display->color565(205, 150, 60);
    dma_display->fillRect(0, PANEL_RES_Y - 6, PANEL_RES_X, 6, fieldBase);
    dma_display->fillRect(0, PANEL_RES_Y - 7, PANEL_RES_X, 1, horizonGlow);
    for (int x = 1; x < PANEL_RES_X; x += 5)
    {
        int blades = (x % 10 == 0) ? 5 : 3;
        dma_display->drawFastVLine(x, PANEL_RES_Y - blades - 3, blades, horizonGlow);
    }
    dma_display->drawFastHLine(0, PANEL_RES_Y - 1, PANEL_RES_X, dma_display->color565(150, 90, 30));

    uint16_t cloudLight = dma_display->color565(235, 240, 248);
    uint16_t cloudMid = dma_display->color565(210, 218, 235);
    uint16_t cloudDark = dma_display->color565(170, 180, 200);

    drawCompactCloud(PANEL_RES_X / 5, 6, cloudLight);
    drawCompactCloud(PANEL_RES_X / 3 + 2, 9, cloudMid);
    drawCompactCloud(PANEL_RES_X / 2 + 4, 8, cloudDark);
    drawCompactCloud(PANEL_RES_X - PANEL_RES_X / 4, 10, cloudMid);
    drawCompactCloud(PANEL_RES_X / 2 - 10, 12, cloudLight);
}

static void drawWeatherSceneCloudyNight()
{
    uint16_t skyColors[] = {
        scaleColor565(dma_display->color565(4, 6, 20), 1.0f),
        scaleColor565(dma_display->color565(8, 12, 26), 1.0f),
        scaleColor565(dma_display->color565(12, 18, 32), 1.0f),
        scaleColor565(dma_display->color565(16, 24, 40), 1.0f)
    };
    drawSkyGradient(skyColors, sizeof(skyColors) / sizeof(skyColors[0]));

    uint16_t fieldBase = dma_display->color565(90, 80, 40);
    uint16_t grassColor = dma_display->color565(20, 50, 25);
    dma_display->fillRect(0, PANEL_RES_Y - 6, PANEL_RES_X, 6, fieldBase);
    dma_display->fillRect(0, PANEL_RES_Y - 7, PANEL_RES_X, 1, grassColor);
    for (int x = 2; x < PANEL_RES_X; x += 5)
        dma_display->drawFastVLine(x, PANEL_RES_Y - 8, 2, scaleColor565(fieldBase, 1.1f));

    uint16_t cloudLight = dma_display->color565(200, 210, 225);
    uint16_t cloudMid = dma_display->color565(170, 180, 195);
    uint16_t cloudDark = dma_display->color565(140, 150, 170);

    drawCompactCloud(PANEL_RES_X / 5, 6, cloudLight);
    drawCompactCloud(PANEL_RES_X / 3 + 2, 9, cloudMid);
    drawCompactCloud(PANEL_RES_X / 2 + 4, 8, cloudDark);
    drawCompactCloud(PANEL_RES_X - PANEL_RES_X / 4, 10, cloudMid);
    drawCompactCloud(PANEL_RES_X / 2 - 10, 12, cloudLight);
}

static void drawWeatherSceneRain()
{
    uint16_t skyColors[] = {
        dma_display->color565(20, 110, 190),
        dma_display->color565(35, 140, 205),
        dma_display->color565(55, 170, 215),
        dma_display->color565(85, 200, 225)
    };
    drawSkyGradient(skyColors, sizeof(skyColors) / sizeof(skyColors[0]));

    uint16_t horizonGlow = dma_display->color565(230, 200, 110);
    uint16_t fieldBase = dma_display->color565(180, 145, 60);
    dma_display->fillRect(0, PANEL_RES_Y - 6, PANEL_RES_X, 6, fieldBase);
    dma_display->fillRect(0, PANEL_RES_Y - 7, PANEL_RES_X, 1, horizonGlow);
    for (int x = 2; x < PANEL_RES_X; x += 5)
    {
        int blades = (x % 10 == 0) ? 4 : 2;
        dma_display->drawFastVLine(x, PANEL_RES_Y - blades - 3, blades, horizonGlow);
    }
    dma_display->drawFastHLine(0, PANEL_RES_Y - 1, PANEL_RES_X, dma_display->color565(130, 80, 30));

    uint16_t cloudLight = dma_display->color565(225, 235, 245);
    uint16_t cloudMid = dma_display->color565(190, 200, 215);
    uint16_t cloudDark = dma_display->color565(150, 160, 180);

    struct Cloud
    {
        int x;
        int y;
        uint16_t color;
    } clouds[] = {
        {PANEL_RES_X / 4, 7, cloudLight},
        {PANEL_RES_X / 3 + 4, 9, cloudMid},
        {PANEL_RES_X / 2 + 6, 8, cloudDark},
        {PANEL_RES_X - PANEL_RES_X / 4, 11, cloudMid},
        {PANEL_RES_X / 2 - 10, 10, cloudLight}};

    for (const auto &cloud : clouds)
        drawCompactCloud(cloud.x, cloud.y, cloud.color);

    uint16_t rainColor = dma_display->color565(140, 230, 255);
    uint16_t rainShadow = dma_display->color565(80, 160, 220);
    for (const auto &cloud : clouds)
    {
        int baseX = cloud.x;
        int top = cloud.y + 4;
        for (int x = baseX - 4; x <= baseX + 4; x += 2)
        {
            int length = 11;
            for (int i = 0; i < length; ++i)
            {
                int px = x - i / 4;
                int py = top + i;
                if (px >= 0 && px < PANEL_RES_X && py < PANEL_RES_Y - 4)
                {
                    uint16_t color = (i % 4 == 0) ? rainShadow : rainColor;
                    dma_display->drawPixel(px, py, color);
                }
            }
        }
    }
}

static void drawWeatherSceneRainNight()
{
    uint16_t skyColors[] = {
        dma_display->color565(8, 16, 36),
        dma_display->color565(12, 22, 48),
        dma_display->color565(16, 26, 56),
        dma_display->color565(22, 32, 68)
    };
    drawSkyGradient(skyColors, sizeof(skyColors) / sizeof(skyColors[0]));

    uint16_t fieldBase = dma_display->color565(90, 80, 45);
    uint16_t horizonGlow = dma_display->color565(110, 90, 55);
    dma_display->fillRect(0, PANEL_RES_Y - 6, PANEL_RES_X, 6, fieldBase);
    dma_display->fillRect(0, PANEL_RES_Y - 7, PANEL_RES_X, 1, horizonGlow);
    for (int x = 2; x < PANEL_RES_X; x += 5)
    {
        int blades = (x % 10 == 0) ? 4 : 2;
        dma_display->drawFastVLine(x, PANEL_RES_Y - blades - 3, blades, horizonGlow);
    }
    dma_display->drawFastHLine(0, PANEL_RES_Y - 1, PANEL_RES_X, dma_display->color565(80, 60, 30));

    uint16_t cloudLight = dma_display->color565(190, 200, 215);
    uint16_t cloudMid = dma_display->color565(160, 170, 190);
    uint16_t cloudDark = dma_display->color565(120, 130, 150);

    struct Cloud
    {
        int x;
        int y;
        uint16_t color;
    } clouds[] = {
        {PANEL_RES_X / 4, 7, cloudLight},
        {PANEL_RES_X / 3 + 4, 9, cloudMid},
        {PANEL_RES_X / 2 + 6, 8, cloudDark},
        {PANEL_RES_X - PANEL_RES_X / 4, 11, cloudMid},
        {PANEL_RES_X / 2 - 10, 10, cloudLight}};

    for (const auto &cloud : clouds)
        drawCompactCloud(cloud.x, cloud.y, cloud.color);

    uint16_t rainColor = dma_display->color565(120, 200, 240);
    uint16_t rainShadow = dma_display->color565(70, 130, 200);
    for (const auto &cloud : clouds)
    {
        int baseX = cloud.x;
        int top = cloud.y + 4;
        for (int x = baseX - 4; x <= baseX + 4; x += 2)
        {
            int length = 11;
            for (int i = 0; i < length; ++i)
            {
                int px = x - i / 4;
                int py = top + i;
                if (px >= 0 && px < PANEL_RES_X && py < PANEL_RES_Y - 4)
                {
                    uint16_t color = (i % 4 == 0) ? rainShadow : rainColor;
                    dma_display->drawPixel(px, py, color);
                }
            }
        }
    }
}

static void drawWeatherSceneThunderstorm()
{
    uint16_t skyColors[] = {
        dma_display->color565(20, 110, 190),
        dma_display->color565(35, 140, 205),
        dma_display->color565(55, 170, 215),
        dma_display->color565(85, 200, 225)
    };
    drawSkyGradient(skyColors, sizeof(skyColors) / sizeof(skyColors[0]));

    uint16_t horizonGlow = dma_display->color565(230, 200, 110);
    uint16_t fieldBase = dma_display->color565(180, 145, 60);
    dma_display->fillRect(0, PANEL_RES_Y - 6, PANEL_RES_X, 6, fieldBase);
    dma_display->fillRect(0, PANEL_RES_Y - 7, PANEL_RES_X, 1, horizonGlow);
    for (int x = 2; x < PANEL_RES_X; x += 5)
    {
        int blades = (x % 10 == 0) ? 4 : 2;
        dma_display->drawFastVLine(x, PANEL_RES_Y - blades - 3, blades, horizonGlow);
    }
    dma_display->drawFastHLine(0, PANEL_RES_Y - 1, PANEL_RES_X, dma_display->color565(130, 80, 30));

    uint16_t cloudLight = dma_display->color565(225, 235, 245);
    uint16_t cloudMid = dma_display->color565(190, 200, 215);
    uint16_t cloudDark = dma_display->color565(150, 160, 180);

    struct Cloud
    {
        int x;
        int y;
        uint16_t color;
    } clouds[] = {
        {PANEL_RES_X / 4, 7, cloudLight},
        {PANEL_RES_X / 3 + 4, 9, cloudMid},
        {PANEL_RES_X / 2 + 6, 8, cloudDark},
        {PANEL_RES_X - PANEL_RES_X / 4, 11, cloudMid},
        {PANEL_RES_X / 2 - 10, 10, cloudLight}};

    for (const auto &cloud : clouds)
        drawCompactCloud(cloud.x, cloud.y, cloud.color);

    uint16_t rainColor = dma_display->color565(140, 230, 255);
    uint16_t rainShadow = dma_display->color565(80, 160, 220);
    for (const auto &cloud : clouds)
    {
        int baseX = cloud.x;
        int top = cloud.y + 4;
        for (int x = baseX - 4; x <= baseX + 4; x += 2)
        {
            int length = 11;
            for (int i = 0; i < length; ++i)
            {
                int px = x - i / 4;
                int py = top + i;
                if (px >= 0 && px < PANEL_RES_X && py < PANEL_RES_Y - 4)
                {
                    uint16_t color = (i % 4 == 0) ? rainShadow : rainColor;
                    dma_display->drawPixel(px, py, color);
                }
            }
        }
    }

    auto drawBolt = [&](int tipX, int tipY, uint16_t color) {
        int x = tipX;
        int y = tipY;
        for (int i = 0; i < 6; ++i)
        {
            int dx = ((i % 2) == 0) ? -1 : 1;
            int dy = random(4, 5);
            int nx = x + dx * 2;
            int ny = y + dy;
            if (ny >= PANEL_RES_Y - 6)
                break;
            dma_display->drawLine(x, y, nx, ny, color);
            if (i == 2 || i == 4)
            {
                int branchDy = dy / 2;
                int branchDx = (dx > 0) ? 2 : -2;
                uint16_t branchColor = scaleColor565(color, 0.6f);
                int branchX = x + branchDx;
                int branchY = y + branchDy;
                dma_display->drawLine(x, y, branchX, branchY, branchColor);
            }
            x = nx;
            y = ny;
        }
    };

    uint16_t boltColor = dma_display->color565(255, 240, 120);
    drawBolt(clouds[0].x, clouds[0].y - 2, boltColor);
}

static void drawWeatherSceneThunderstormNight()
{
    uint16_t skyColors[] = {
        dma_display->color565(10, 12, 30),
        dma_display->color565(16, 20, 40),
        dma_display->color565(20, 24, 48),
        dma_display->color565(26, 30, 58)
    };
    drawSkyGradient(skyColors, sizeof(skyColors) / sizeof(skyColors[0]));

    uint16_t fieldBase = dma_display->color565(90, 80, 45);
    uint16_t horizonGlow = dma_display->color565(110, 90, 55);
    dma_display->fillRect(0, PANEL_RES_Y - 6, PANEL_RES_X, 6, fieldBase);
    dma_display->fillRect(0, PANEL_RES_Y - 7, PANEL_RES_X, 1, horizonGlow);
    for (int x = 2; x < PANEL_RES_X; x += 5)
    {
        int blades = (x % 10 == 0) ? 4 : 2;
        dma_display->drawFastVLine(x, PANEL_RES_Y - blades - 3, blades, horizonGlow);
    }
    dma_display->drawFastHLine(0, PANEL_RES_Y - 1, PANEL_RES_X, dma_display->color565(80, 60, 30));

    uint16_t cloudLight = dma_display->color565(200, 210, 225);
    uint16_t cloudMid = dma_display->color565(170, 180, 195);
    uint16_t cloudDark = dma_display->color565(130, 140, 160);

    struct Cloud
    {
        int x;
        int y;
        uint16_t color;
    } clouds[] = {
        {PANEL_RES_X / 4, 7, cloudLight},
        {PANEL_RES_X / 3 + 4, 9, cloudMid},
        {PANEL_RES_X / 2 + 6, 8, cloudDark},
        {PANEL_RES_X - PANEL_RES_X / 4, 11, cloudMid},
        {PANEL_RES_X / 2 - 10, 10, cloudLight}};

    for (const auto &cloud : clouds)
        drawCompactCloud(cloud.x, cloud.y, cloud.color);

    uint16_t rainColor = dma_display->color565(120, 200, 240);
    uint16_t rainShadow = dma_display->color565(70, 130, 200);
    for (const auto &cloud : clouds)
    {
        int baseX = cloud.x;
        int top = cloud.y + 4;
        for (int x = baseX - 4; x <= baseX + 4; x += 2)
        {
            int length = 11;
            for (int i = 0; i < length; ++i)
            {
                int px = x - i / 4;
                int py = top + i;
                if (px >= 0 && px < PANEL_RES_X && py < PANEL_RES_Y - 4)
                {
                    uint16_t color = (i % 4 == 0) ? rainShadow : rainColor;
                    dma_display->drawPixel(px, py, color);
                }
            }
        }
    }

    auto drawBolt = [&](int tipX, int tipY, uint16_t color) {
        int x = tipX;
        int y = tipY;
        for (int i = 0; i < 6; ++i)
        {
            int dx = ((i % 2) == 0) ? -1 : 1;
            int dy = random(4, 5);
            int nx = x + dx * 2;
            int ny = y + dy;
            if (ny >= PANEL_RES_Y - 6)
                break;
            dma_display->drawLine(x, y, nx, ny, color);
            if (i == 2 || i == 4)
            {
                int branchDy = dy / 2;
                int branchDx = (dx > 0) ? 2 : -2;
                uint16_t branchColor = scaleColor565(color, 0.6f);
                int branchX = x + branchDx;
                int branchY = y + branchDy;
                dma_display->drawLine(x, y, branchX, branchY, branchColor);
            }
            x = nx;
            y = ny;
        }
    };

    uint16_t boltColor = dma_display->color565(255, 240, 120);
    drawBolt(clouds[0].x, clouds[0].y - 2, boltColor);
}

static void drawWeatherSceneSnow()
{
    uint16_t skyColors[] = {
        dma_display->color565(20, 110, 190),
        dma_display->color565(35, 140, 205),
        dma_display->color565(55, 170, 215),
        dma_display->color565(85, 200, 225)
    };
    drawSkyGradient(skyColors, sizeof(skyColors) / sizeof(skyColors[0]));

    uint16_t horizonGlow = dma_display->color565(230, 200, 110);
    uint16_t fieldBase = dma_display->color565(180, 145, 60);
    dma_display->fillRect(0, PANEL_RES_Y - 6, PANEL_RES_X, 6, fieldBase);
    dma_display->fillRect(0, PANEL_RES_Y - 7, PANEL_RES_X, 1, horizonGlow);
    for (int x = 2; x < PANEL_RES_X; x += 5)
    {
        int blades = (x % 10 == 0) ? 4 : 2;
        dma_display->drawFastVLine(x, PANEL_RES_Y - blades - 3, blades, horizonGlow);
    }
    dma_display->drawFastHLine(0, PANEL_RES_Y - 1, PANEL_RES_X, dma_display->color565(130, 80, 30));

    uint16_t cloudLight = dma_display->color565(240, 245, 255);
    uint16_t cloudMid = dma_display->color565(210, 220, 235);
    uint16_t cloudDark = dma_display->color565(180, 190, 205);

    struct SnowCloud
    {
        int x;
        int y;
        uint16_t color;
    } snowClouds[] = {
        {PANEL_RES_X / 4, 6, cloudLight},
        {PANEL_RES_X / 3 + 4, 8, cloudMid},
        {PANEL_RES_X / 2 + 6, 7, cloudDark},
        {PANEL_RES_X - PANEL_RES_X / 4, 9, cloudMid},
        {PANEL_RES_X / 2 - 12, 10, cloudLight}};

    for (const auto &cloud : snowClouds)
        drawCompactCloud(cloud.x, cloud.y, cloud.color);

    uint16_t snowColor = dma_display->color565(240, 250, 255);
    for (int i = 0; i < 32; ++i)
    {
        int x = random(0, PANEL_RES_X);
        int y = random(8, PANEL_RES_Y - 4);
        drawSnowflake(x, y, snowColor);
    }
}

static void drawWeatherSceneSnowNight()
{
    uint16_t skyColors[] = {
        dma_display->color565(10, 16, 32),
        dma_display->color565(14, 20, 40),
        dma_display->color565(18, 24, 48),
        dma_display->color565(22, 28, 56)
    };
    drawSkyGradient(skyColors, sizeof(skyColors) / sizeof(skyColors[0]));

    uint16_t fieldBase = dma_display->color565(90, 80, 40);
    uint16_t horizonGlow = dma_display->color565(110, 90, 55);
    dma_display->fillRect(0, PANEL_RES_Y - 6, PANEL_RES_X, 6, fieldBase);
    dma_display->fillRect(0, PANEL_RES_Y - 7, PANEL_RES_X, 1, horizonGlow);
    for (int x = 2; x < PANEL_RES_X; x += 5)
    {
        int blades = (x % 10 == 0) ? 4 : 2;
        dma_display->drawFastVLine(x, PANEL_RES_Y - blades - 3, blades, horizonGlow);
    }
    dma_display->drawFastHLine(0, PANEL_RES_Y - 1, PANEL_RES_X, dma_display->color565(80, 60, 30));

    uint16_t cloudLight = dma_display->color565(210, 220, 235);
    uint16_t cloudMid = dma_display->color565(180, 190, 210);
    uint16_t cloudDark = dma_display->color565(150, 160, 180);

    struct SnowCloud
    {
        int x;
        int y;
        uint16_t color;
    } snowClouds[] = {
        {PANEL_RES_X / 4, 6, cloudLight},
        {PANEL_RES_X / 3 + 4, 8, cloudMid},
        {PANEL_RES_X / 2 + 6, 7, cloudDark},
        {PANEL_RES_X - PANEL_RES_X / 4, 9, cloudMid},
        {PANEL_RES_X / 2 - 12, 10, cloudLight}};

    for (const auto &cloud : snowClouds)
        drawCompactCloud(cloud.x, cloud.y, cloud.color);

    uint16_t snowColor = dma_display->color565(220, 230, 240);
    for (int i = 0; i < 32; ++i)
    {
        int x = random(0, PANEL_RES_X);
        int y = random(8, PANEL_RES_Y - 4);
        drawSnowflake(x, y, snowColor);
    }
}

static void drawWeatherSceneClearNight()
{
    uint16_t skyColors[] = {
        dma_display->color565(4, 6, 20),
        dma_display->color565(8, 10, 30),
        dma_display->color565(12, 16, 40),
        dma_display->color565(16, 20, 50)
    };
    drawSkyGradient(skyColors, sizeof(skyColors) / sizeof(skyColors[0]));

    uint16_t fieldColor = dma_display->color565(200, 140, 40);
    uint16_t grassColor = dma_display->color565(30, 70, 25);
    dma_display->fillRect(0, PANEL_RES_Y - 6, PANEL_RES_X, 6, fieldColor);
    dma_display->fillRect(0, PANEL_RES_Y - 7, PANEL_RES_X, 1, grassColor);
    for (int x = 2; x < PANEL_RES_X; x += 5)
        dma_display->drawFastVLine(x, PANEL_RES_Y - 8, 2, scaleColor565(fieldColor, 1.1f));

    uint16_t moonColor = dma_display->color565(255, 240, 90);
    int moonX = PANEL_RES_X / 4;
    int moonY = PANEL_RES_Y / 2 - 6;
    dma_display->fillCircle(moonX, moonY, 7, moonColor);
    dma_display->fillCircle(moonX, moonY, 4, dma_display->color565(255, 255, 120));
    dma_display->drawCircle(moonX, moonY, 7, scaleColor565(moonColor, 0.8f));

    uint16_t starColor = moonColor;
    drawStar(4, 6, starColor);
    drawStar(PANEL_RES_X - 6, 8, starColor);
    drawStar(PANEL_RES_X / 2 + 8, 4, starColor);
    drawStar(PANEL_RES_X / 3, 2, starColor);
    drawStar(PANEL_RES_X / 2 + 12, 14, starColor);
    drawStar(PANEL_RES_X / 2 - 12, 10, starColor);

}

struct WeatherSceneRenderer
{
    WeatherSceneKind kind;
    void (*drawFn)();
};

static void drawWeatherSceneDefault()
{
    drawWeatherSceneSunny();
}

// Mono-friendly scene for theme 1
static void drawWeatherSceneMono()
{
    // Soft grayscale sky gradient
    uint16_t skyColors[] = {
        dma_display->color565(12, 12, 18),
        dma_display->color565(20, 20, 28),
        dma_display->color565(32, 32, 42),
        dma_display->color565(44, 44, 56)
    };
    drawSkyGradient(skyColors, sizeof(skyColors) / sizeof(skyColors[0]));

    uint16_t fieldBase = dma_display->color565(35, 35, 35);
    uint16_t horizonGlow = dma_display->color565(60, 60, 70);
    dma_display->fillRect(0, PANEL_RES_Y - 6, PANEL_RES_X, 6, fieldBase);
    dma_display->fillRect(0, PANEL_RES_Y - 7, PANEL_RES_X, 1, horizonGlow);
    dma_display->drawFastHLine(0, PANEL_RES_Y - 1, PANEL_RES_X, dma_display->color565(80, 80, 90));

    // Simple muted clouds
    uint16_t cloudLight = dma_display->color565(120, 120, 130);
    uint16_t cloudMid = dma_display->color565(90, 90, 100);
    uint16_t cloudDark = dma_display->color565(70, 70, 80);

    drawCompactCloud(PANEL_RES_X / 5, 8, cloudLight);
    drawCompactCloud(PANEL_RES_X / 2, 10, cloudMid);
    drawCompactCloud(PANEL_RES_X - PANEL_RES_X / 4, 12, cloudDark);
}

static const WeatherSceneRenderer WEATHER_SCENE_RENDERERS[] = {
    {WeatherSceneKind::Sunny, drawWeatherSceneSunny},
    {WeatherSceneKind::SunnyNight, drawWeatherSceneClearNight},
    {WeatherSceneKind::Cloudy, drawWeatherSceneCloudy},
    {WeatherSceneKind::CloudyNight, drawWeatherSceneCloudyNight},
    {WeatherSceneKind::Rain, drawWeatherSceneRain},
    {WeatherSceneKind::RainNight, drawWeatherSceneRainNight},
    {WeatherSceneKind::Thunderstorm, drawWeatherSceneThunderstorm},
    {WeatherSceneKind::ThunderstormNight, drawWeatherSceneThunderstormNight},
    {WeatherSceneKind::Snow, drawWeatherSceneSnow},
    {WeatherSceneKind::SnowNight, drawWeatherSceneSnowNight},
    {WeatherSceneKind::ClearNight, drawWeatherSceneClearNight}
};

struct WeatherSceneAlias
{
    const char *key;
    WeatherSceneKind kind;
};

static const WeatherSceneAlias WEATHER_SCENE_ALIASES[] = {
    {"sunny", WeatherSceneKind::Sunny},
    {"clear", WeatherSceneKind::Sunny},
    {"clear day", WeatherSceneKind::Sunny},
    {"clear sky", WeatherSceneKind::Sunny},
    {"clean", WeatherSceneKind::Sunny},
    {"clean day", WeatherSceneKind::Sunny},
    {"fair", WeatherSceneKind::Sunny},
    {"cloudy", WeatherSceneKind::Cloudy},
    {"mostly cloudy", WeatherSceneKind::Cloudy},
    {"partly cloudy", WeatherSceneKind::Cloudy},
    {"overcast", WeatherSceneKind::Cloudy},
    {"overcast clouds", WeatherSceneKind::Cloudy},
    {"few clouds", WeatherSceneKind::Cloudy},
    {"scattered clouds", WeatherSceneKind::Cloudy},
    {"broken clouds", WeatherSceneKind::Cloudy},
    {"mist", WeatherSceneKind::Cloudy},
    {"fog", WeatherSceneKind::Cloudy},
    {"haze", WeatherSceneKind::Cloudy},
    {"smoke", WeatherSceneKind::Cloudy},
    {"dust", WeatherSceneKind::Cloudy},
    {"sand", WeatherSceneKind::Cloudy},
    {"ash", WeatherSceneKind::Cloudy},
    {"rain", WeatherSceneKind::Rain},
    {"rainy", WeatherSceneKind::Rain},
    {"showers", WeatherSceneKind::Rain},
    {"shower rain", WeatherSceneKind::Rain},
    {"light rain", WeatherSceneKind::Rain},
    {"moderate rain", WeatherSceneKind::Rain},
    {"heavy rain", WeatherSceneKind::Rain},
    {"heavy intensity rain", WeatherSceneKind::Rain},
    {"drizzle", WeatherSceneKind::Rain},
    {"squalls", WeatherSceneKind::Rain},
    {"thunderstorm", WeatherSceneKind::Thunderstorm},
    {"thunderstorms", WeatherSceneKind::Thunderstorm},
    {"storm", WeatherSceneKind::Thunderstorm},
    {"tstorm", WeatherSceneKind::Thunderstorm},
    {"tornado", WeatherSceneKind::Thunderstorm},
    {"snow", WeatherSceneKind::Snow},
    {"snowy", WeatherSceneKind::Snow},
    {"light snow", WeatherSceneKind::Snow},
    {"heavy snow", WeatherSceneKind::Snow},
    {"snow shower", WeatherSceneKind::Snow},
    {"flurries", WeatherSceneKind::Snow},
    {"sleet", WeatherSceneKind::Snow},
    {"clear night", WeatherSceneKind::ClearNight},
    {"clean night", WeatherSceneKind::ClearNight},
    {"night", WeatherSceneKind::ClearNight},
    {"mostly clear night", WeatherSceneKind::ClearNight},
    {nullptr, WeatherSceneKind::Unknown}
};

static String normalizeConditionKey(const String &condition)
{
    String key = condition;
    key.trim();
    key.toLowerCase();
    key.replace('-', ' ');
    key.replace('_', ' ');
    while (key.indexOf("  ") >= 0)
    {
        key.replace("  ", " ");
    }
    return key;
}

static WeatherSceneKind resolveWeatherSceneKind(const String &condition)
{
    auto isNightNow = []() {
        DateTime utcNow = rtc.now();
        int offsetMinutes = timezoneIsCustom() ? tzStandardOffset : timezoneOffsetForUtc(utcNow);
        DateTime local = utcToLocal(utcNow, offsetMinutes);
        int hr = local.hour();
        return (hr < 6 || hr >= 18);
    };

    bool night = isNightNow();
    String normalized = normalizeConditionKey(condition);
    if (normalized.length() == 0)
        return night ? WeatherSceneKind::SunnyNight : WeatherSceneKind::Sunny;

    for (int i = 0; WEATHER_SCENE_ALIASES[i].key != nullptr; ++i)
    {
        if (normalized.equals(WEATHER_SCENE_ALIASES[i].key))
        {
            WeatherSceneKind base = WEATHER_SCENE_ALIASES[i].kind;
            if (base == WeatherSceneKind::Sunny && night)
                return WeatherSceneKind::SunnyNight;
            if (base == WeatherSceneKind::Cloudy && night)
                return WeatherSceneKind::CloudyNight;
            if (base == WeatherSceneKind::Rain && night)
                return WeatherSceneKind::RainNight;
            if (base == WeatherSceneKind::Thunderstorm && night)
                return WeatherSceneKind::ThunderstormNight;
            if (base == WeatherSceneKind::Snow && night)
                return WeatherSceneKind::SnowNight;
            return base;
        }
    }

    if (normalized.indexOf("thunder") >= 0 || normalized.indexOf("storm") >= 0)
        return night ? WeatherSceneKind::ThunderstormNight : WeatherSceneKind::Thunderstorm;
    if (normalized.indexOf("rain") >= 0 || normalized.indexOf("shower") >= 0 || normalized.indexOf("drizzle") >= 0)
        return night ? WeatherSceneKind::RainNight : WeatherSceneKind::Rain;
    if (normalized.indexOf("snow") >= 0 || normalized.indexOf("sleet") >= 0 || normalized.indexOf("flurry") >= 0)
        return night ? WeatherSceneKind::SnowNight : WeatherSceneKind::Snow;
    if (normalized.indexOf("night") >= 0)
        return WeatherSceneKind::ClearNight;
    if (normalized.indexOf("cloud") >= 0 || normalized.indexOf("overcast") >= 0 || normalized.indexOf("mist") >= 0)
        return night ? WeatherSceneKind::CloudyNight : WeatherSceneKind::Cloudy;
    return night ? WeatherSceneKind::SunnyNight : WeatherSceneKind::Sunny;
}

static uint16_t weatherSceneAccentColor(WeatherSceneKind kind)
{
    switch (kind)
    {
    case WeatherSceneKind::Sunny:
        return dma_display->color565(235, 185, 60);
    case WeatherSceneKind::SunnyNight:
        return dma_display->color565(180, 190, 240);
    case WeatherSceneKind::Cloudy:
        return dma_display->color565(190, 200, 220);
    case WeatherSceneKind::CloudyNight:
        return dma_display->color565(150, 170, 210);
    case WeatherSceneKind::Rain:
        return dma_display->color565(120, 170, 210);
    case WeatherSceneKind::RainNight:
        return dma_display->color565(100, 150, 200);
    case WeatherSceneKind::Thunderstorm:
        return dma_display->color565(220, 180, 50);
    case WeatherSceneKind::ThunderstormNight:
        return dma_display->color565(200, 160, 70);
    case WeatherSceneKind::Snow:
        return dma_display->color565(210, 220, 235);
    case WeatherSceneKind::SnowNight:
        return dma_display->color565(180, 190, 220);
    case WeatherSceneKind::ClearNight:
        return dma_display->color565(160, 180, 220);
    default:
        return dma_display->color565(200, 200, 210);
    }
}

static String formatConditionLabel(const String &condition)
{
    String label = condition;
    label.trim();
    if (label.length() == 0)
        return String("No Data");

    label.replace('_', ' ');
    label.replace('-', ' ');
    label.toLowerCase();

    bool capitalizeNext = true;
    for (int i = 0; i < label.length(); ++i)
    {
        char c = label.charAt(i);
        unsigned char uc = static_cast<unsigned char>(c);
        if (isalpha(uc))
        {
            if (capitalizeNext)
                label.setCharAt(i, static_cast<char>(toupper(uc)));
            capitalizeNext = false;
        }
        else if (isdigit(uc))
        {
            capitalizeNext = false;
        }
        else
        {
            capitalizeNext = true;
        }
    }
    return label;
}

void drawWeatherConditionScene(WeatherSceneKind kind)
{
    if (theme == 1)
    {
        drawWeatherSceneMono();
        return;
    }

    for (const auto &renderer : WEATHER_SCENE_RENDERERS)
    {
        if (renderer.kind == kind)
        {
            renderer.drawFn();
            return;
        }
    }
    drawWeatherSceneDefault();
}

void drawWeatherConditionScene(const String &condition)
{
    WeatherSceneKind kind = resolveWeatherSceneKind(condition);
    drawWeatherConditionScene(kind);
}

bool screenIsAllowed(ScreenMode mode)
{
    switch (mode)
    {
    case SCREEN_OWM:
        return isDataSourceOwm();
    case SCREEN_UDP_DATA:
    case SCREEN_UDP_FORECAST:
    case SCREEN_WIND_DIR:
    case SCREEN_CURRENT:
    case SCREEN_HOURLY:
        return isDataSourceWeatherFlow();
    case SCREEN_CONDITION_SCENE:
        return !isDataSourceNone();
    case SCREEN_NOAA_ALERT:
        return noaaAlertsEnabled;
    default:
        return true;
    }
}

ScreenMode nextAllowedScreen(ScreenMode start, int direction)
{
    if (direction == 0)
        direction = 1;

    int startIdx = -1;
    for (int i = 0; i < NUM_INFOSCREENS; ++i)
    {
        if (InfoScreenModes[i] == start)
        {
            startIdx = i;
            break;
        }
    }
    if (startIdx < 0)
        startIdx = 0;

    int idx = startIdx;
    for (int steps = 0; steps < NUM_INFOSCREENS; ++steps)
    {
        idx = (idx + direction + NUM_INFOSCREENS) % NUM_INFOSCREENS;
        ScreenMode candidate = InfoScreenModes[idx];
        if (screenIsAllowed(candidate))
            return candidate;
    }
    return SCREEN_CLOCK;
}

ScreenMode enforceAllowedScreen(ScreenMode desired)
{
    if (screenIsAllowed(desired))
        return desired;

    ScreenMode candidate = nextAllowedScreen(desired, +1);
    if (screenIsAllowed(candidate))
        return candidate;

    candidate = nextAllowedScreen(desired, -1);
    if (screenIsAllowed(candidate))
        return candidate;

    return SCREEN_CLOCK;
}

ScreenMode homeScreenForDataSource()
{
    if (isDataSourceOwm())
        return SCREEN_OWM;
    return SCREEN_CLOCK;
}
static String formatOutdoorTemperature()
{
    if (isDataSourceNone())
        return String("--");

    if (isDataSourceWeatherFlow())
    {
        if (!isnan(tempest.temperature))
            return fmtTemp(tempest.temperature, 0);
    }
    else
    {
        if (str_Temp.length() > 0)
            return fmtTemp(atof(str_Temp.c_str()), 0);
    }

    if (!isnan(tempest.temperature))
        return fmtTemp(tempest.temperature, 0);
    if (str_Temp.length() > 0)
        return fmtTemp(atof(str_Temp.c_str()), 0);
    return String("--");
}
static String formatOutdoorHumidity()
{
    if (isDataSourceNone())
        return String("--");

    if (isDataSourceWeatherFlow())
    {
        if (!isnan(tempest.humidity))
            return String((int)(tempest.humidity + 0.5f));
    }

    if (str_Humd.length() > 0)
        return str_Humd;

    if (!isnan(tempest.humidity))
        return String((int)(tempest.humidity + 0.5f));

    return String("--");
}

static String formatIndoorHumidity()
{
    float humiditySource = SCD40_hum;
    if (isnan(humiditySource))
        humiditySource = aht20_hum;

    if (!isnan(humiditySource))
    {
        float calibrated = humiditySource + static_cast<float>(humOffset);
        if (calibrated < 0.0f)
            calibrated = 0.0f;
        if (calibrated > 100.0f)
            calibrated = 100.0f;
        int rounded = static_cast<int>(calibrated + 0.5f);
        if (rounded < 0)
            rounded = 0;
        if (rounded > 100)
            rounded = 100;
        return String(rounded);
    }

    if (str_Humd.length() > 0 && str_Humd != "--")
        return str_Humd;

    return String("--");
}
static void drawWeatherFlowIcon()
{
    dma_display->fillRect(0, 0, 16, 16, myBLACK);
    String cond = currentCond.cond;
    if (cond.isEmpty())
        cond = str_Weather_Conditions_Des;
    const uint8_t *icon = getWeatherIconFromCondition(cond);
    uint16_t color = getIconColorFromCondition(cond);
    dma_display->drawBitmap(0, 0, icon, 16, 16, color);
}
static bool splashActive = false;
static uint16_t splashAccent = 0;
static uint16_t splashShadow = 0;
static uint16_t splashHighlight = 0;
static uint16_t splashMinimumMs = 0;
static unsigned long splashStartMs = 0;
static uint8_t splashGradStartR = 0;
static uint8_t splashGradStartG = 0;
static uint8_t splashGradStartB = 0;
static uint8_t splashGradStepR = 0;
static uint8_t splashGradStepG = 0;
static uint8_t splashGradStepB = 0;
static float splashTextIntensity = 1.0f;

int getTextWidth(const char *text);

static uint16_t splashRowColor(int y)
{
    if (y < 0)
        y = 0;
    if (y >= PANEL_RES_Y)
        y = PANEL_RES_Y - 1;

    uint16_t r = splashGradStartR + y * splashGradStepR;
    uint16_t g = splashGradStartG + y * splashGradStepG;
    uint16_t b = splashGradStartB + y * splashGradStepB;
    if (r > 255)
        r = 255;
    if (g > 255)
        g = 255;
    if (b > 255)
        b = 255;
    return dma_display->color565(r, g, b);
}

static uint16_t scaleSplashColor(uint16_t color, float factor)
{
    if (!dma_display)
        return 0;

    if (factor <= 0.0f)
        return 0;
    if (factor > 1.0f)
        factor = 1.0f;

    uint8_t r = ((color >> 11) & 0x1F) * 255 / 31;
    uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
    uint8_t b = (color & 0x1F) * 255 / 31;

    r = static_cast<uint8_t>(r * factor + 0.5f);
    g = static_cast<uint8_t>(g * factor + 0.5f);
    b = static_cast<uint8_t>(b * factor + 0.5f);

    return dma_display->color565(r, g, b);
}

static void drawSplashBackdrop()
{
    for (int y = 0; y < PANEL_RES_Y; ++y)
    {
        dma_display->drawFastHLine(0, y, PANEL_RES_X, splashRowColor(y));
    }

    uint16_t borderColor = scaleSplashColor(splashAccent, 0.85f);
    dma_display->drawFastHLine(0, 0, PANEL_RES_X, borderColor);
    dma_display->drawFastHLine(0, PANEL_RES_Y - 1, PANEL_RES_X, borderColor);
    dma_display->drawFastVLine(0, 0, PANEL_RES_Y, borderColor);
    dma_display->drawFastVLine(PANEL_RES_X - 1, 0, PANEL_RES_Y, borderColor);
}

static void drawSplashBranding(float intensity)
{
    if (!dma_display)
        return;

    if (intensity < 0.0f)
        intensity = 0.0f;
    if (intensity > 1.0f)
        intensity = 1.0f;

    const uint16_t frameColor = scaleSplashColor(splashHighlight, 0.7f * intensity + 0.3f);
    const uint16_t fillColor = scaleSplashColor(splashShadow, 0.25f + 0.35f * intensity);
    const uint16_t accentColor = scaleSplashColor(splashAccent, intensity);
    const uint16_t textColor = scaleSplashColor(myWHITE, intensity);

    const int bannerX = 4;
    const int bannerY = 6;
    const int bannerW = PANEL_RES_X - 8;
    const int bannerH = 12;

    dma_display->fillRect(bannerX, bannerY, bannerW, bannerH, fillColor);
    dma_display->drawRect(bannerX - 1, bannerY - 1, bannerW + 2, bannerH + 2, frameColor);

    dma_display->setTextWrap(false);
    dma_display->setFont();

    const char *title = "WxVision";
    dma_display->setTextSize(1);

    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    int titleX = (PANEL_RES_X - static_cast<int>(w)) / 2 - x1;
    int titleY = bannerY + (bannerH - static_cast<int>(h)) / 2 - y1;

    uint16_t shadowColor = scaleSplashColor(splashShadow, 0.5f * intensity);
    dma_display->setCursor(titleX + 1, titleY + 1);
    dma_display->setTextColor(shadowColor);
    dma_display->print(title);

    dma_display->setCursor(titleX, titleY);
    dma_display->setTextColor(textColor);
    dma_display->print(title);

    dma_display->setTextSize(1);
    const char *tagline = "Weather";
    dma_display->getTextBounds(tagline, 0, 0, &x1, &y1, &w, &h);
    int taglineX = (PANEL_RES_X - static_cast<int>(w)) / 2 - x1;
    int taglineTop = bannerY + bannerH + 2;
    int taglineBaseline = taglineTop - y1;

    dma_display->setCursor(taglineX + 1, taglineBaseline + 1);
    dma_display->setTextColor(shadowColor);
    dma_display->print(tagline);
    dma_display->setCursor(taglineX, taglineBaseline);
    dma_display->setTextColor(accentColor);
    dma_display->print(tagline);

    dma_display->setTextSize(1);
}

static void drawSplashScreen(float intensity)
{
    if (!dma_display)
        return;

    drawSplashBackdrop();
    drawSplashBranding(intensity);
}

void setPanelBrightness(uint8_t value)
{
    if (!dma_display)
        return;
    dma_display->setBrightness8(value);
    currentPanelBrightness = value;
}

void setupDisplay()
{
    HUB75_I2S_CFG::i2s_pins _pins = {
        R1_PIN, G1_PIN, B1_PIN,
        R2_PIN, G2_PIN, B2_PIN,
        A_PIN, B_PIN, C_PIN, D_PIN, E_PIN,
        LAT_PIN, OE_PIN, CLK_PIN};

    HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN, _pins);
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_15M;
    mxconfig.min_refresh_rate = 120;

    dma_display = new MatrixPanel_I2S_DMA(mxconfig);
    dma_display->begin();
    setPanelBrightness(map(brightness, 1, 100, 3, 255));
    dma_display->setTextSize(1);
    dma_display->setTextWrap(false);
    dma_display->setFont(&Font5x7Uts);

    myRED = dma_display->color565(255, 0, 0);
    myGREEN = dma_display->color565(0, 255, 0);
    myBLUE = dma_display->color565(0, 0, 255);
    myWHITE = dma_display->color565(255, 255, 255);
    myBLACK = dma_display->color565(0, 0, 0);
    myYELLOW = dma_display->color565(255, 255, 0);
    myCYAN = dma_display->color565(0, 255, 255);
}

void splashBegin(uint16_t minimumMs)
{
    if (!dma_display)
        return;

    splashActive = true;
    splashMinimumMs = minimumMs;
    splashStartMs = millis();
    if (theme == 1)
    {
        // Night theme palette
        splashAccent = dma_display->color565(210, 210, 230);
        splashShadow = dma_display->color565(18, 20, 34);
        splashHighlight = dma_display->color565(255, 255, 255);
        splashGradStartR = 14;
        splashGradStartG = 14;
        splashGradStartB = 18;
        splashGradStepR = 2;
        splashGradStepG = 2;
        splashGradStepB = 3;
    }
    else
    {
        // Day theme palette
        splashAccent = dma_display->color565(90, 210, 255);
        splashShadow = dma_display->color565(8, 22, 38);
        splashHighlight = dma_display->color565(230, 250, 255);
        splashGradStartR = 10;
        splashGradStartG = 26;
        splashGradStartB = 40;
        splashGradStepR = 2;
        splashGradStepG = 2;
        splashGradStepB = 3;
    }
    splashTextIntensity = 1.0f;
    drawSplashScreen(splashTextIntensity);
}

void splashUpdate(const char *status, uint8_t step, uint8_t total)
{
    if (!dma_display || !splashActive)
        return;

    (void)status;
    (void)step;
    (void)total;
    // Static splash – no animation required.
}

void splashEnd()
{
    if (!dma_display || !splashActive)
        return;

    while ((uint32_t)(millis() - splashStartMs) < splashMinimumMs)
    {
        delay(15);
    }

    const uint8_t fadeSteps = 12;
    for (int step = fadeSteps; step >= 0; --step)
    {
        float intensity = static_cast<float>(step) / static_cast<float>(fadeSteps);
        splashTextIntensity = intensity;
        drawSplashScreen(intensity);
        delay(25);
    }

    dma_display->fillScreen(0);
    splashTextIntensity = 1.0f;
    splashActive = false;
    splashMinimumMs = 0;
    dma_display->setTextColor(myWHITE);
    dma_display->setTextSize(1);
}

bool isSplashActive()
{
    return splashActive;
}

int getTextWidth(const char *text)
{
    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return w;
}

const uint8_t *getWeatherIconFromCode(String code)
{
    // Serial.printf("Code: %s", code);
    if (code.startsWith("01n"))
        return icon_clear_night;
    if (code.startsWith("02n"))
        return icon_cloud_night;
    if (code.startsWith("01d"))
        return icon_clear;
    if (code.startsWith("02d"))
        return icon_cloudy;
    if (code.startsWith("03") || code.startsWith("04"))
        return icon_cloudy;
    if (code.startsWith("09") || code.startsWith("10"))
        return icon_rain;
    if (code.startsWith("11"))
        return icon_thunder;
    if (code.startsWith("13"))
        return icon_snow;
    if (code.startsWith("50"))
        return icon_fog;
    return icon_clear; // fallback
}

const uint8_t *getWeatherIconFromCondition(String condition)
{
    condition.toLowerCase();
    const bool isNight = (condition.indexOf("night") >= 0) || condition.endsWith("-n");
    if (isNight && condition.indexOf("clear") >= 0)
        return icon_clear_night;
    if (isNight && condition.indexOf("cloud") >= 0)
        return icon_cloud_night;
    if (condition.indexOf("clear") >= 0)
        return icon_clear;
    if (condition.indexOf("cloud") >= 0)
        return icon_cloudy;
    if (condition.indexOf("rain") >= 0)
        return icon_rain;
    if (condition.indexOf("storm") >= 0)
        return icon_thunder;
    if (condition.indexOf("snow") >= 0)
        return icon_snow;
    if (condition.indexOf("fog") >= 0 || condition.indexOf("mist") >= 0)
        return icon_fog;
    return icon_clear;
}

const uint16_t getIconColorFromCondition(String condition)
{
    condition.toLowerCase();
    const bool isNight = (condition.indexOf("night") >= 0) || condition.endsWith("-n");
    if (isNight) {
        if (condition.indexOf("clear") >= 0)
            return myBLUE;
        if (condition.indexOf("cloud") >= 0)
            return dma_display->color565(120, 160, 220);
        if (condition.indexOf("rain") >= 0)
            return dma_display->color565(0, 120, 200);
        if (condition.indexOf("storm") >= 0)
            return dma_display->color565(180, 120, 255);
        if (condition.indexOf("snow") >= 0)
            return dma_display->color565(170, 220, 255);
        if (condition.indexOf("fog") >= 0 || condition.indexOf("mist") >= 0)
            return dma_display->color565(120, 140, 180);
        return myBLUE;
    }
    if (condition.indexOf("clear") >= 0)
        return dma_display->color565(255, 255, 0); // (yellow)
    if (condition.indexOf("cloud") >= 0)
        return dma_display->color565(180, 180, 180);
    if (condition.indexOf("rain") >= 0)
        return dma_display->color565(0, 200, 255);
    if (condition.indexOf("storm") >= 0)
        return dma_display->color565(255, 255, 0);
    if (condition.indexOf("snow") >= 0)
        return dma_display->color565(220, 255, 255);
    if (condition.indexOf("fog") >= 0 || condition.indexOf("mist") >= 0)
        return dma_display->color565(180, 180, 180);
    return dma_display->color565(255, 255, 0);
}

const uint16_t getDayNightColorFromCode(String code)
{
    if (code.indexOf("d") >= 0)
        return dma_display->color565(255, 170, 51); // day color
    else
        return myBLUE; // night color
}

void drawWeatherScreen()
{
    dma_display->fillScreen(0);
    dma_display->setCursor(0, 8);
    dma_display->setTextColor(dma_display->color565(80, 255, 255));
    dma_display->setTextSize(2);
    dma_display->print("72 F");
    dma_display->setTextSize(1);
    dma_display->setCursor(0, 26);
    dma_display->print("Clear Sky");
}

void drawSettingsScreen()
{
    dma_display->fillScreen(0);
    dma_display->setCursor(2, 10);
    dma_display->setTextColor(dma_display->color565(255, 255, 255));
    dma_display->print("Settings");
    // Draw options, etc.
}

void drawLunarScreenVi()
{
    // Always rebuild so lunar time/date stay in sync with current clock
    buildLunarLinesMerged();
    for (int i = 0; i < 3; ++i)
        lunarOffsets[i] = 0;
    lastLunarTick = millis();
    renderLunarLines(lunarLines, lunarWidths, lunarOffsets);
}

// OWS Weather /////////////////////////////////////////////////////////////////////////////////////////

// === WiFi & API Config ===
const char *ssid = "Polaris";
const char *password = "1339113391";
String openWeatherMapApiKey = "0db802af001e4a3c2b018d6e5e2a6632";
String city = "Garden%20Grove";
String countryCode = "US";
// === NTP/RTC ===
const char *ntpServer1 = "pool.ntp.org";
const long gmtOffset_sec = -8 * 3600;
const int daylightOffset_sec = 3600;
RTC_DS3231 rtc;
bool rtcReady = false;

String httpGETRequest(const char *url)
{
    WiFiClient client;
    HTTPClient http;
    http.begin(client, url);
    int httpCode = http.GET();
    String payload = "{}";
    if (httpCode > 0)
    {
        payload = http.getString();
    }
    else
    {
        Serial.printf("GET failed: %d\n", httpCode);
    }
    http.end();
    return payload;
}

// === Units Toggle ===
bool useImperial = false;
// === Clock Display Buffers ===
byte t_second = 0, t_minute = 0, t_hour = 0, d_day = 0, d_month = 0, d_daysOfTheWeek = 0;
int d_year = 0;
char chr_t_hour[3], chr_t_minute[3], chr_t_second[3], chr_d_day[3], chr_d_month[3], chr_d_year[5];
byte last_t_second = 0, last_t_minute = 0, last_t_hour = 0, last_d_day = 0, last_d_month = 0;
int last_d_year = 0;
bool reset_Time_and_Date_Display = false;
char daysOfTheWeek[7][12] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
char monthName[12][4] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// === Weather State ===
String jsonBuffer;
String str_Temp, str_Humd, str_Weather_Icon;
String str_Weather_Conditions, str_Weather_Conditions_Des;
String str_Feels_like, str_Pressure, str_Wind_Speed, str_City;
String str_Temp_max, str_Temp_min, str_Wind_Direction;

// === Scrolling State ===
unsigned long prevMill_Scroll_Text = 0;
int scrolling_Y_Pos = 25;
long scrolling_X_Pos;
uint16_t scrolling_Text_Color = 0;
String scrolling_Text = "";
uint16_t text_Length_In_Pixel = 0;
bool set_up_Scrolling_Text_Length = true;
bool start_Scroll_Text = false;

// Condition scene marquee state (use ScrollLine)
static String conditionSceneMarqueeBase = "";
static String conditionSceneMarqueeText = "";
static String conditionSceneMarqueePendingText = "";
static uint16_t conditionSceneMarqueeColor = 0;
static ScrollLine conditionSceneScroll(PANEL_RES_X, 60);


void getTimeFromRTC()
{
    DateTime now;
    bool haveTime = false;
    if (rtcReady)
    {
        DateTime utcNow = rtc.now();
        int offsetMinutes = timezoneIsCustom() ? tzStandardOffset : timezoneOffsetForUtc(utcNow);
        now = utcToLocal(utcNow, offsetMinutes);
        updateTimezoneOffsetWithUtc(utcNow);
        haveTime = true;
    }
    else if (getLocalDateTime(now))
    {
        haveTime = true;
    }

    if (!haveTime)
    {
        t_hour = t_minute = t_second = 0;
        d_day = d_month = 1;
        d_year = 2000;
        d_daysOfTheWeek = 0;
        sprintf(chr_t_hour, "%02d", t_hour);
        sprintf(chr_t_minute, "%02d", t_minute);
        sprintf(chr_t_second, "%02d", t_second);
        sprintf(chr_d_day, "%02d", d_day);
        sprintf(chr_d_month, "%02d", d_month);
        sprintf(chr_d_year, "%04d", d_year);
        return;
    }

    t_hour = now.hour();
    t_minute = now.minute();
    t_second = now.second();
    d_day = now.day();
    d_month = now.month();
    d_year = now.year();
    d_daysOfTheWeek = now.dayOfTheWeek();
    sprintf(chr_t_hour, "%02d", t_hour);
    sprintf(chr_t_minute, "%02d", t_minute);
    sprintf(chr_t_second, "%02d", t_second);
    sprintf(chr_d_day, "%02d", d_day);
    sprintf(chr_d_month, "%02d", d_month);
    sprintf(chr_d_year, "%04d", d_year);
}

void tickLunarMarquee()
{
    if (!dma_display)
        return;

    unsigned long nowMs = millis();

    if (currentScreen == SCREEN_LUNAR_VI)
    {
        if (!lunarInitialized)
            buildLunarLinesMerged();

        // Match global marquee speed (scrollSpeed from settings), fallback to 60 ms
        unsigned long intervalMs = (scrollSpeed > 0)
                                       ? static_cast<unsigned long>(scrollSpeed)
                                       : 60ul;

        if (nowMs - lastLunarTick >= intervalMs)
        {
            lastLunarTick = nowMs;

            int i = 2; // only line 3 scrolls
            int w = lunarWidths[i];
            if (w > 0)
            {
                lunarOffsets[i]++;

                int baseX = PANEL_RES_X - lunarOffsets[i];
                if (baseX + w < 0)
                    lunarOffsets[i] = 0;
            }

            renderLunarLines(lunarLines, lunarWidths, lunarOffsets);
        }
    }
}

void fetchWeatherFromOWM()
{
    if (WiFi.status() != WL_CONNECTED)
        return;
    String units = useImperial ? "imperial" : "metric";

    String apiKey = owmApiKey;
    apiKey.trim();
    if (apiKey.isEmpty()) {
        apiKey = openWeatherMapApiKey;
    }

    String selectedCity = owmCity;
    selectedCity.trim();
    if (selectedCity.isEmpty()) {
        selectedCity = city;
    }

    String selectedCountry;
    if (owmCountryIndex >= 0 && owmCountryIndex < (countryCount - 1)) {
        selectedCountry = countryCodes[owmCountryIndex];
    } else {
        selectedCountry = owmCountryCustom;
    }
    selectedCountry.trim();
    selectedCountry.toUpperCase();
    if (selectedCountry.isEmpty()) {
        selectedCountry = countryCode;
    }

    if (apiKey.isEmpty() || selectedCity.isEmpty()) {
        Serial.println("[OWM] Missing API key or city; skip fetch");
        return;
    }

    // Minimal encoding for city names with spaces/commas
    selectedCity.replace(" ", "%20");
    selectedCity.replace(",", "%2C");

    String url = "http://api.openweathermap.org/data/2.5/weather?q=" + selectedCity;
    if (!selectedCountry.isEmpty()) {
        url += "," + selectedCountry;
    }
    url += "&units=" + units + "&appid=" + apiKey;
    String jsonBuffer = httpGETRequest(url.c_str());
    if (jsonBuffer == "{}")
        return;
    JSONVar data = JSON.parse(jsonBuffer);
    if (JSON.typeof_(data) == "undefined")
    {
        Serial.println("Failed to parse weather JSON");
        return;
    }
    auto toCelsius = [](double raw) -> double {
        if (isnan(raw))
            return raw;
        if (useImperial)
            return (raw - 32.0) * (5.0 / 9.0);
        return raw;
    };
    auto toMetersPerSecond = [](double raw) -> double {
        if (isnan(raw))
            return raw;
        if (useImperial)
            return raw * 0.44704; // mph -> m/s
        return raw;
    };
    auto readNumber = [&](JSONVar obj, const char *key) -> double {
        if (JSON.typeof_(obj) != "object")
            return NAN;
        JSONVar v = obj[key];
        if (JSON.typeof_(v) == "undefined")
            return NAN;
        return double(v);
    };

    JSONVar mainBlock = data["main"];
    JSONVar windBlock = data["wind"];

    double tempC = toCelsius(readNumber(mainBlock, "temp"));
    double tempMaxC = toCelsius(readNumber(mainBlock, "temp_max"));
    double tempMinC = toCelsius(readNumber(mainBlock, "temp_min"));
    double feelsC = toCelsius(readNumber(mainBlock, "feels_like"));
    double humidity = readNumber(mainBlock, "humidity");
    double pressure = readNumber(mainBlock, "pressure");
    double windSpeed = toMetersPerSecond(readNumber(windBlock, "speed"));
    double windDir = readNumber(windBlock, "deg");

    str_Weather_Icon = JSON.stringify(data["weather"][0]["icon"]);
    str_Weather_Icon.replace("\"", "");
    str_Weather_Conditions = JSON.stringify(data["weather"][0]["main"]);
    str_Weather_Conditions.replace("\"", "");
    str_Weather_Conditions_Des = JSON.stringify(data["weather"][0]["description"]);
    str_Weather_Conditions_Des.replace("\"", "");
    str_City = JSON.stringify(data["name"]);
    str_City.replace("\"", "");

    str_Temp = String(tempC, 2);
    str_Temp_max = String(tempMaxC, 2);
    str_Temp_min = String(tempMinC, 2);
    str_Feels_like = String(feelsC, 2);
    str_Humd = isnan(humidity) ? String("--") : String(humidity, 0);
    str_Pressure = isnan(pressure) ? String("--") : String(pressure, 0);
    str_Wind_Speed = String(windSpeed, 2);
    str_Wind_Direction = isnan(windDir) ? String("--") : String(windDir, 0);

    Serial.println("Weather Updated:");
    Serial.printf("  Temp: %.1fC | Hum: %s%% | Wind: %.2fm/s\n",
                  tempC, str_Humd.c_str(), windSpeed);

    needScrollRebuild = true;
}

void drawOWMScreen()
{
    getTimeFromRTC();
    displayClock();
    displayDate();

    // Detect any change across temp/wind/press/precip/clock24h
    const uint16_t curSig = unitSignature();
    if (curSig != lastUnitSig)
    {
        lastUnitSig = curSig;
        needScrollRebuild = true; // units changed -> rebuild marquee
    }

    // Rebuild scrolling text only when needed
    if (needScrollRebuild)
    {
        createScrollingText(); // uses fmtTemp/fmtWind/... honoring 'units'
        // Reset marquee so it restarts smoothly
        start_Scroll_Text = false;
        set_up_Scrolling_Text_Length = true;
        text_Length_In_Pixel = getTextWidth(scrolling_Text.c_str());
        needScrollRebuild = false;
    }

    // Update weather every 10 minutes at :10s
    if ((t_minute % 10 == 0) && (t_second == 10))
    {
        fetchWeatherFromOWM();
        reset_Time_and_Date_Display = true;
        displayWeatherData();
        needScrollRebuild = true; // new values -> refresh marquee text
    }
}

void drawWeatherIcon(String iconCode)
{
    dma_display->fillRect(0, 0, 16, 16, myBLACK);
    dma_display->setCursor(1, 4);
    dma_display->setTextColor(theme == 1 ? dma_display->color565(110, 110, 180) : myYELLOW);
    uint16_t iconColor = getDayNightColorFromCode(iconCode);
    if (theme == 1)
        iconColor = dma_display->color565(90, 90, 150);
    dma_display->drawBitmap(0, 0, getWeatherIconFromCode(iconCode), 16, 16, iconColor);
}

void displayClock()
{
    int hour = atoi(chr_t_hour);
    char amPm[] = "A";
    if (hour >= 12)
    {
        if (hour > 12)
            hour -= 12;
        strcpy(amPm, "P");
    }
    else if (hour == 0)
    {
        hour = 12;
    }
    dma_display->fillRect(15, 9, 45, 7, myBLACK);
    dma_display->setCursor(16, 9);
    dma_display->setTextColor(theme == 1 ? dma_display->color565(90, 90, 150) : myRED);
    dma_display->printf("%02d:", hour);
    dma_display->print(chr_t_minute);
    dma_display->print(":");
    dma_display->print(chr_t_second);
    dma_display->fillRect(59, 9, 64, 16, myBLACK);
    dma_display->setCursor(59, 9);
    dma_display->printf("%s", amPm);
}

void displayDate()
{
    dma_display->fillRect(0, 17, 64, 7, myBLACK);
    dma_display->setCursor(0, 17);
    dma_display->setTextColor(theme == 1 ? dma_display->color565(70, 70, 130) : myCYAN);
    dma_display->printf("%s %s.%s.%s", daysOfTheWeek[d_daysOfTheWeek], chr_d_month, chr_d_day, chr_d_year + 2);
}

void displayWeatherData()
{
    if (isDataSourceNone())
    {
        dma_display->fillRect(0, 0, 64, 7, myBLACK);
        return;
    }

    if (isDataSourceWeatherFlow())
    {
        drawWeatherFlowIcon();
    }
    else
    {
        drawWeatherIcon(str_Weather_Icon);
    }

    dma_display->fillRect(18, 0, 46, 7, myBLACK);
    dma_display->setCursor(18, 0);
    dma_display->setTextColor(theme == 1 ? dma_display->color565(110, 110, 180) : myYELLOW);
    dma_display->print(formatOutdoorTemperature());

    String humidityStr = formatOutdoorHumidity();
    if (humidityStr != "--")
    {
        dma_display->setCursor(44, 0);
        dma_display->setTextColor(theme == 1 ? dma_display->color565(70, 70, 130) : myCYAN);
        dma_display->print(humidityStr);
        dma_display->print("%");
    }
}

void createScrollingText()
{
    if (!isDataSourceOwm())
    {
        scrolling_Text = "";
        text_Length_In_Pixel = 0;
        return;
    }

//   String unitT = useImperial ? " °F" : " °C";
    //   String unitW = useImperial ? "mph" : "m/s";

    scrolling_Text =
        "City: " + str_City + " ¦ " +
        "Weather: " + str_Weather_Conditions_Des + " ¦ " +
        "Feels Like: " + fmtTemp(atof(str_Feels_like.c_str()), 0) + " ¦ " +
        "Max: " + fmtTemp(atof(str_Temp_max.c_str()), 0) + "  ¦ Min: " + fmtTemp(atof(str_Temp_min.c_str()), 0) + " ¦ " +
        "Pressure: " + fmtPress(atof(str_Pressure.c_str()), 0) + " ¦ " +
        "Wind: " + fmtWind(atof(str_Wind_Speed.c_str()), 1) + " ¦ ";

    scrolling_Text_Color = (theme == 1) ? dma_display->color565(60, 60, 120) : myGREEN;
    text_Length_In_Pixel = getTextWidth(scrolling_Text.c_str());
}

// --- Scroll state variables for weather ---
void scrollWeatherDetails()
{
    if (!isDataSourceOwm())
        return;

    static unsigned long lastScrollTime = 0;
    static int scrollOffset = 0;

    if (!start_Scroll_Text)
    {
        String unitT = useImperial ? "°F" : "°C";
        String unitW = useImperial ? "mph" : "m/s";
        scrollOffset = 0;
        start_Scroll_Text = true;
    }

    // Use same timing as InfoModal
    if (millis() - lastScrollTime > scrollSpeed)
    {
        lastScrollTime = millis();

        if (text_Length_In_Pixel > PANEL_RES_X)
        {
            scrollOffset++;
            if (scrollOffset > text_Length_In_Pixel)
                scrollOffset = -PANEL_RES_X; // restart
        }
        else
        {
            scrollOffset = 0;
        }

        const uint16_t desiredColor =
            (theme == 1) ? dma_display->color565(60, 60, 120) : myGREEN;
        if (desiredColor != scrolling_Text_Color)
        {
            scrolling_Text_Color = desiredColor;
        }

        dma_display->fillRect(0, 25, PANEL_RES_X, 7, myBLACK);
        dma_display->setCursor(-scrollOffset, 25);
        dma_display->setTextColor(scrolling_Text_Color);
        dma_display->print(scrolling_Text);
    }
}

static String formatConditionSceneTimeTag()
{
    char buf[12];
    if (units.clock24h)
    {
        snprintf(buf, sizeof(buf), "%02d:%02d", t_hour, t_minute);
        return String(buf);
    }

    int hour = t_hour;
    const char *suffix = "AM";
    if (hour >= 12)
    {
        suffix = "PM";
        if (hour > 12)
            hour -= 12;
    }
    else if (hour == 0)
    {
        hour = 12;
    }
    snprintf(buf, sizeof(buf), "%02d:%02d %s", hour, t_minute, suffix);
    return String(buf);
}

// Build the condition marquee text with extra telemetry fields (humid, press, wind, feels like)
static String buildConditionMarqueeText(const String &label)
{
    String combined = label;

    auto appendField = [&](const String &field) {
        if (field.length() == 0)
            return;
        if (combined.length() > 0)
            combined += " ¦ ";
        combined += field;
    };

    // Time always first
    appendField(formatConditionSceneTimeTag());

    bool feelsAppended = false;

    if (isDataSourceWeatherFlow())
    {
        if (currentCond.humidity >= 0)
            appendField(String("Hum ") + currentCond.humidity + "%");
        else if (!isnan(tempest.humidity))
            appendField(String("Hum ") + String((int)roundf(tempest.humidity)) + "%");

        double press = !isnan(currentCond.pressure) ? currentCond.pressure : tempest.pressure;
        if (!isnan(press))
            appendField(String("Press ") + fmtPress(press, 0));

        double wind = !isnan(currentCond.windAvg) ? currentCond.windAvg : tempest.windAvg;
        if (!isnan(wind))
            appendField(String("Wind ") + fmtWind(wind, 1));

        double feels = !isnan(currentCond.feelsLike) ? currentCond.feelsLike : tempest.temperature;
        if (!isnan(feels))
        {
            appendField(String("Feels ") + fmtTemp(feels, 0));
            feelsAppended = true;
        }
    }
    else if (isDataSourceOwm())
    {
        String hum = formatOutdoorHumidity();
        if (hum.length() > 0 && hum != "--")
            appendField(String("Hum ") + hum + "%");

        if (str_Pressure.length() > 0 && str_Pressure != "--")
            appendField(String("Press ") + fmtPress(atof(str_Pressure.c_str()), 0));

        if (str_Wind_Speed.length() > 0 && str_Wind_Speed != "--")
            appendField(String("Wind ") + fmtWind(atof(str_Wind_Speed.c_str()), 1));

        if (str_Feels_like.length() > 0 && str_Feels_like != "--")
        {
            appendField(String("Feels ") + fmtTemp(atof(str_Feels_like.c_str()), 0));
            feelsAppended = true;
        }
        else if (str_Temp.length() > 0 && str_Temp != "--")
        {
            appendField(String("Feels ") + fmtTemp(atof(str_Temp.c_str()), 0));
            feelsAppended = true;
        }
    }

    // Fallback: if we still don't have a feels-like value but we have a temperature, reuse it
    if (!feelsAppended)
    {
        double tempVal = NAN;
        if (isDataSourceWeatherFlow())
            tempVal = !isnan(currentCond.temp) ? currentCond.temp : tempest.temperature;
        else if (isDataSourceOwm() && str_Temp.length() > 0 && str_Temp != "--")
            tempVal = atof(str_Temp.c_str());

        if (!isnan(tempVal))
            appendField(String("Feels ") + fmtTemp(tempVal, 0));
    }

    return combined;
}

static void renderConditionSceneMarquee(bool force)
{
    if (conditionSceneMarqueeText.length() == 0)
        return;

    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);

    if (conditionSceneMarqueePendingText.length() > 0)
    {
        String lines[] = {conditionSceneMarqueePendingText};
        conditionSceneMarqueePendingText = "";
        conditionSceneMarqueeText = lines[0];
        conditionSceneScroll.setLines(lines, 1, true);
        uint16_t textColors[] = {conditionSceneMarqueeColor};
        uint16_t bgColors[] = {myBLACK};
        conditionSceneScroll.setLineColors(textColors, bgColors, 1);
        conditionSceneScroll.setScrollSpeed(scrollSpeed);
    }

    const int marqueeY = PANEL_RES_Y - 7;
    int borderY = marqueeY - 1;
    if (borderY >= 0)
    {
        uint16_t borderColor = scaleColor565(conditionSceneMarqueeColor, 0.65f);
        dma_display->drawFastHLine(0, borderY, PANEL_RES_X, borderColor);
    }
    conditionSceneScroll.update();
    conditionSceneScroll.draw(0, marqueeY, conditionSceneMarqueeColor);
}
void drawSunIcon(int x, int y, uint16_t color)
{
    // Consistent 7x7 sun with center + 8 rays
    int cx = x + 3;
    int cy = y + 3;

    dma_display->drawLine(x + 3, y, x + 3, y + 6, color);     // Vertiacal
    dma_display->drawLine(x, y + 3, x + 6, y + 3, color);     // Horizontal
    dma_display->drawLine(x + 1, y + 1, x + 5, y + 5, color); // diagonal
    dma_display->drawLine(x + 5, y + 1, x + 1, y + 5, color); // diagonal

    /*
        dma_display->fillCircle(cx, cy, 1, color);   // center

        // main rays
        dma_display->drawPixel(cx, cy - 3, color);
        dma_display->drawPixel(cx, cy + 3, color);
        dma_display->drawPixel(cx - 3, cy, color);
        dma_display->drawPixel(cx + 3, cy, color);

        dma_display->drawPixel(x + 2, y + 2, color);
        dma_display->drawPixel(x + 4, y + 2, color);
        dma_display->drawPixel(x + 2, y + 4, color);
        dma_display->drawPixel(x + 4, y + 4, color);

        // diagonal rays
        dma_display->drawPixel(cx - 2, cy - 2, color);
        dma_display->drawPixel(cx + 2, cy - 2, color);
        dma_display->drawPixel(cx - 2, cy + 2, color);
        dma_display->drawPixel(cx + 2, cy + 2, color);
        */
}

void drawHouseIcon(int x, int y, uint16_t color)
{
    // Larger 7x7 house matching the sun's visual weight
    // Roof
    dma_display->drawPixel(x + 4, 0, color);
    dma_display->drawLine(x + 2, y + 2, x + 6, y + 2, color);
    dma_display->drawLine(x + 1, y + 3, x + 7, y + 3, color); // roof base
    dma_display->drawLine(x + 3, y + 1, x + 5, y + 1, color); // left slope

    // Walls
    dma_display->drawRect(x + 2, y + 4, 5, 3, color);

    // Door
    dma_display->drawLine(x + 4, y + 5, x + 4, y + 6, color);
}

void drawHumidityIcon(int x, int y, uint16_t color)
{
    // 7x7 droplet with pointed tip and rounded base
    dma_display->drawPixel(x + 3, y, color);                               // tip
    dma_display->drawLine(x + 2, y + 1, x + 4, y + 1, color);              // gentle slope
    dma_display->drawLine(x + 1, y + 2, x + 5, y + 2, color);              // upper bulb
    dma_display->drawLine(x + 1, y + 3, x + 5, y + 3, color);              // body
    dma_display->drawLine(x + 1, y + 4, x + 5, y + 4, color);              // body
    dma_display->drawLine(x + 2, y + 5, x + 4, y + 5, color);              // rounding into base
//    dma_display->drawLine(x + 1, y + 6, x + 5, y + 6, color);              // flat bottom
}

void drawWiFiIcon(int x, int y, uint16_t color)
{
    // Simple 7x5 Wi-Fi signal icon
    // (x,y) = top-left corner of the icon
    // three arcs + small dot
    dma_display->drawPixel(x + 3, y + 4, color);              // bottom dot
    dma_display->drawLine(x + 2, y + 3, x + 4, y + 3, color); // small arc
    dma_display->drawLine(x + 1, y + 2, x + 5, y + 2, color); // mid arc
    dma_display->drawLine(x + 0, y + 1, x + 6, y + 1, color); // top arc
    dma_display->drawLine(x + 3, y + 4, x + 3, y + 6, color); // support bar
}

void drawAlarmIcon(int x, int y, uint16_t color)
{
    // Draw 6x6 bell per provided pattern:
    // Row 0: ..XX..
    // Row 1: .XXXX.
    // Row 2: .XXXX.
    // Row 3: .XXXX.
    // Row 4: XXXXXX
    // Row 5: ..XX..
    dma_display->drawLine(x + 2, y + 0, x + 3, y + 0, color);
    dma_display->drawLine(x + 1, y + 1, x + 4, y + 1, color);
    dma_display->drawLine(x + 1, y + 2, x + 4, y + 2, color);
    dma_display->drawLine(x + 1, y + 3, x + 4, y + 3, color);
    dma_display->drawLine(x + 0, y + 4, x + 5, y + 4, color);
    dma_display->drawLine(x + 2, y + 5, x + 3, y + 5, color);
}

void drawClockScreen()
{

    dma_display->fillScreen(0);

    DateTime now;
    if (rtcReady)
    {
        DateTime utcNow = rtc.now();
        int offsetMinutes = timezoneIsCustom() ? tzStandardOffset : timezoneOffsetForUtc(utcNow);
        now = utcToLocal(utcNow, offsetMinutes);
        updateTimezoneOffsetWithUtc(utcNow);
    }
    else if (!getLocalDateTime(now))
    {
        now = DateTime(2000, 1, 1, 0, 0, 0);
    }
    tickAlarmState(now);
    int hour = now.hour(), minute = now.minute(), second = now.second();

    // 12h/24h handling
    bool isPM = false;
    if (!units.clock24h)
    {
        if (hour == 0)
            hour = 12;
        else if (hour >= 12)
        {
            if (hour > 12)
                hour -= 12;
            isPM = true;
        }
    }

    char timeStr[6]; // "HH:MM"
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hour, minute);
    bool alarmActive = isAlarmCurrentlyActive();
    bool showTimeDigits = !alarmActive || isAlarmFlashVisible();

    const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    char dateStr[14];
    snprintf(dateStr, sizeof(dateStr), "%s %02d/%02d",
             days[now.dayOfTheWeek()], now.month(), now.day());

    // ---- TIME (big Verdana Bold)
    dma_display->setFont(&verdanab8pt7b);
    dma_display->setTextSize(1);
    uint16_t timeColor = (theme == 1) ? dma_display->color565(60, 60, 120)
                                      : dma_display->color565(255, 255, 80);
    if (alarmActive)
        timeColor = dma_display->color565(255, 80, 80);
    dma_display->setTextColor(timeColor);

    int timeW = getTextWidth(timeStr);
    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
    int timeH = h;

    int ampmW = 0;
    if (!units.clock24h)
        ampmW = getTextWidth(isPM ? "PM" : "AM");
    int totalW = timeW + (ampmW ? ampmW + 2 : 0);
    int boxX = (64 - totalW) / 2;

    // Shift slightly left when 24-hour mode (to balance space)
    if (units.clock24h)
        boxX -= 3;

    if (boxX < 0)
        boxX = 0;

    int boxY = (32 - timeH) / 2;


    if (showTimeDigits)
    {
        dma_display->setCursor(boxX, boxY + timeH - 1);
        dma_display->print(timeStr);

        // --- draw AM/PM inline
        if (!units.clock24h)
        {
            String ampmStr = isPM ? "PM" : "AM";
            dma_display->setFont(&Font5x7Uts);
            dma_display->setTextSize(1);

            int16_t x1, y1;
            uint16_t w, h;
            dma_display->getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
            int digitH = h;
            dma_display->getTextBounds(ampmStr.c_str(), 0, 0, &x1, &y1, &w, &h);
            int ampmW = w;
            int ampmH = h;
            int ampmX = 64 - ampmW - 1;
            int ampmY = boxY + digitH - (digitH - ampmH) - 1;
            ampmY -= 1;

            uint16_t ampmColor, bgColor;

            if (theme == 1) {
                ampmColor = dma_display->color565(100, 100, 140);
                bgColor   = dma_display->color565(20, 20, 40);
            }
            else {
                if (isPM) {
                    ampmColor = dma_display->color565(255, 170, 60);
                    bgColor   = dma_display->color565(50, 30, 0);
                } else {
                    ampmColor = dma_display->color565(100, 200, 255);
                    bgColor   = dma_display->color565(10, 30, 50);
                }
            }

            dma_display->setTextColor(ampmColor);
            dma_display->fillRect(ampmX - 1, ampmY - ampmH + 6, ampmW + 2, ampmH + 2, bgColor);

            dma_display->setCursor(ampmX, ampmY);
            dma_display->print(ampmStr);
        }
    }
    int wifiX = 57;
    int wifiY = 7;
    int alarmX = units.clock24h ? wifiX : 51;
    int alarmY = units.clock24h ? (wifiY + 8) : 8; // drop the bell under Wi-Fi when 24h

    // ---- Draw Wi-Fi icon if connected ---- if connected ----
    if (WiFi.status() == WL_CONNECTED)
    {
        // Position the Wi-Fi icon just above AM/PM

        /*
                    int wifiX = ampmX + 5;   // just after time text
                    int wifiY = ampmY - 8;        // above AM/PM
        */

        uint16_t wifiColor = (theme == 1)
                                 ? dma_display->color565(90, 90, 120)    // dim gray for mono
                                 : dma_display->color565(100, 255, 120); // soft green for color
        drawWiFiIcon(wifiX, wifiY, wifiColor);
    }
    if (isAnyAlarmEnabled() || alarmActive)
    {
        uint16_t alarmColor = alarmActive
                                  ? dma_display->color565(255, 80, 80)
                                  : ((theme == 1) ? dma_display->color565(120, 120, 180)
                                                  : dma_display->color565(255, 255, 120));
        drawAlarmIcon(alarmX, alarmY, alarmColor);
    }
    // ---- DATE ----
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    uint16_t dateColor = (theme == 1) ? dma_display->color565(60, 60, 120)
                                      : dma_display->color565(150, 200, 255);
    dma_display->setTextColor(dateColor);
    dma_display->getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
    int dateX = (64 - (int)w) / 2;
    int dateY = 25;
    dma_display->setCursor(dateX, dateY);
    dma_display->print(dateStr);

    // ---- TEMPERATURES ----
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    uint16_t tempColor = (theme == 1) ? dma_display->color565(60, 60, 120)
                                      : dma_display->color565(200, 200, 255);
    dma_display->setTextColor(tempColor);
    String outdoorTempStr = formatOutdoorTemperature();
    bool showOutdoor = !isDataSourceNone() && outdoorTempStr != "--";
    String indoorHumidityStr = formatIndoorHumidity();
    bool showIndoorHumidity = isDataSourceNone() && indoorHumidityStr != "--";
    float indoorTempC = NAN;
    if (!isnan(SCD40_temp))
        indoorTempC = SCD40_temp + tempOffset;
    else if (!isnan(aht20_temp))
        indoorTempC = aht20_temp + tempOffset;
    String localTempStr = fmtTemp(indoorTempC, 0);        // Inside

    dma_display->fillRect(0, 0, 32, 7, myBLACK);

    if (showOutdoor)
    {
        const int iconWidth = 7;
        const int padding = 1;
        int sunX = 0;
        int sunY = 0;
        uint16_t sunColor = (theme == 1)
            ? dma_display->color565(100, 100, 140)
            : dma_display->color565(255, 200, 60);
        drawSunIcon(sunX, sunY, sunColor);

        int tempX = sunX + iconWidth + padding;
        dma_display->setCursor(tempX, 0);
        dma_display->print(outdoorTempStr);
    }
    else if (showIndoorHumidity)
    {
        String humidityDisplay = indoorHumidityStr + "%";
        const int iconWidth = 7;
        const int padding = 1;
        int dropX = 0;
        int dropY = 0;
        uint16_t dropColor = (theme == 1)
                                 ? dma_display->color565(100, 100, 160)
                                 : dma_display->color565(100, 200, 255);
        drawHumidityIcon(dropX, dropY, dropColor);

        int humidityX = dropX + iconWidth + padding;
        dma_display->setCursor(humidityX, 0);
        dma_display->print(humidityDisplay);
    }

    // Draw house icon to the left of inside temperature
    dma_display->getTextBounds(localTempStr.c_str(), 0, 0, &x1, &y1, &w, &h);
    int localX = 64 - w;
    int localY = 0;
    int houseX = localX - 9;
    int houseY = 0;
    // --- Indoor (House) color ---
    uint16_t houseColor = (theme == 1)
        ? dma_display->color565(100, 100, 140)    // dim gray-blue for mono
        : dma_display->color565(100, 180, 255);   // cool sky blue for indoor comfort
    drawHouseIcon(houseX, houseY, houseColor);

    dma_display->setCursor(localX, localY);
    dma_display->print(localTempStr);

    // ---- Environmental status bars ----
    const int dotRadius = 1;
    const int dotDiameter = dotRadius * 2 + 1;
    const int co2DotX = 2;
    const int eqDotX = co2DotX;
    const int eqDotY = 30;
    const int co2DotY = eqDotY - (dotDiameter + 1);
    const int clearTop = eqDotY - dotRadius;
    const int clearHeight = (eqDotY + dotRadius) - clearTop + 1;
    dma_display->fillRect(co2DotX - dotRadius - 1, clearTop, dotDiameter + 2, clearHeight, myBLACK);

    float co2Raw = (SCD40_co2 > 0) ? static_cast<float>(SCD40_co2) : NAN;
    float humiditySource = !isnan(SCD40_hum) ? SCD40_hum : aht20_hum;
    if (!isnan(humiditySource))
    {
        humiditySource += static_cast<float>(humOffset);
        if (humiditySource < 0.0f)
            humiditySource = 0.0f;
        else if (humiditySource > 100.0f)
            humiditySource = 100.0f;
    }
    float pressure = (!isnan(bmp280_pressure) && bmp280_pressure > 200.0f) ? bmp280_pressure : NAN;

    EnvBand co2Band = envBandFromCo2(co2Raw);
    EnvBand tempBand = envBandFromTemp(indoorTempC);
    EnvBand humidityBand = envBandFromHumidity(humiditySource);
    EnvBand pressureBand = envBandFromPressure(pressure);

    EnvBand bands[] = {co2Band, tempBand, humidityBand, pressureBand};
    int scoreSum = 0;
    int validCount = 0;
    for (EnvBand band : bands)
    {
        int score = envScoreForBand(band);
        if (score >= 0)
        {
            scoreSum += score;
            ++validCount;
        }
    }

    float eqIndex = (validCount > 0) ? (static_cast<float>(scoreSum) / (validCount * 3.0f)) * 100.0f : -1.0f;
    EnvBand eqBand = (validCount > 0) ? envBandFromIndex(eqIndex) : EnvBand::Unknown;

    auto intensityForBand = [&](EnvBand band) -> float {
        if (band == EnvBand::Critical)
            return (second % 2 == 0) ? 1.0f : 0.35f;
        if (band == EnvBand::Poor)
            return ((second / 2) % 2 == 0) ? 1.0f : 0.6f;
        return 1.0f;
    };

    uint16_t eqPulseColor = scaleColor565(envColorForBand(eqBand), intensityForBand(eqBand));
    uint16_t co2PulseColor = scaleColor565(envColorForBand(co2Band), intensityForBand(co2Band));

    dma_display->fillCircle(eqDotX, eqDotY, dotRadius, eqPulseColor);
    dma_display->fillCircle(co2DotX, co2DotY, dotRadius, co2PulseColor);

    // ---- Seconds pulse ----
    uint16_t pulseColor = (second % 2 == 0)
                              ? dma_display->color565(0, 150, 0)
                              : dma_display->color565(0, 60, 0);
    dma_display->fillCircle(62, 30, 1, pulseColor);
}

void drawConditionSceneScreen()
{
    String condition;
    bool hasData = true;

    getTimeFromRTC();

    if (isDataSourceWeatherFlow())
    {
        if (currentCond.cond.length() > 0)
            condition = currentCond.cond;
        else if (currentCond.icon.length() > 0)
            condition = currentCond.icon;
    }
    else if (isDataSourceOwm())
    {
        if (str_Weather_Conditions_Des.length() > 0)
            condition = str_Weather_Conditions_Des;
        else if (str_Weather_Conditions.length() > 0)
            condition = str_Weather_Conditions;
        else if (str_Weather_Icon.length() > 0)
            condition = str_Weather_Icon;
    }
    else
    {
        hasData = false;
    }

    if (condition.length() == 0)
    {
        hasData = false;
        condition = isDataSourceNone() ? "No data" : "Unknown";
    }

    WeatherSceneKind sceneKind = resolveWeatherSceneKind(condition);
    drawWeatherConditionScene(sceneKind);

    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);

    String label = formatConditionLabel(condition);
    if (isDataSourceNone())
        label = "No Data";
    else if (!hasData)
        label = "Waiting...";

    auto drawTextWithShadow = [&](int x, int y, const String &text, uint16_t color) {
        uint16_t shadow = scaleColor565(color, 0.25f);
        dma_display->setTextColor(shadow);
        dma_display->setCursor(x + 1, y + 1);
        dma_display->print(text);
        dma_display->setTextColor(color);
        dma_display->setCursor(x, y);
        dma_display->print(text);
    };

    uint16_t accent = weatherSceneAccentColor(sceneKind);

    String tempTag;
    if (isDataSourceWeatherFlow())
    {
        if (!isnan(currentCond.temp))
            tempTag = fmtTemp(currentCond.temp, 0);
        else if (!isnan(tempest.temperature))
            tempTag = fmtTemp(tempest.temperature, 0);
    }
    else if (isDataSourceOwm())
    {
        if (str_Temp.length() > 0)
            tempTag = fmtTemp(atof(str_Temp.c_str()), 0);
    }

    if (tempTag.length() > 0)
    {
        int tempWidth = getTextWidth(tempTag.c_str());
        int tempX = PANEL_RES_X - tempWidth - 3;
        if (tempX < 2)
            tempX = 2;
        drawTextWithShadow(tempX, 6, tempTag, accent);
    }

    // Only (re)initialize the marquee when the underlying condition label changes.
    // Time, wind, feels, etc. are updated via tickConditionSceneMarquee() using the pending buffer.
    if (label != conditionSceneMarqueeBase || conditionSceneMarqueeText.length() == 0)
    {
        conditionSceneMarqueeBase = label;
        conditionSceneMarqueeColor = accent;
        conditionSceneMarqueeText = buildConditionMarqueeText(conditionSceneMarqueeBase);
        String lines[] = {conditionSceneMarqueeText};
        conditionSceneScroll.setLines(lines, 1, true);
        uint16_t textColors[] = {conditionSceneMarqueeColor};
        uint16_t bgColors[] = {myBLACK};
        conditionSceneScroll.setLineColors(textColors, bgColors, 1);
        conditionSceneScroll.setScrollSpeed(scrollSpeed);
        conditionSceneMarqueePendingText = "";
    }
    else
    {
        // Keep accent color in sync even if we don't rebuild the text
        conditionSceneMarqueeColor = accent;
    }
    renderConditionSceneMarquee(true);
}

void tickConditionSceneMarquee()
{
    if (currentScreen != SCREEN_CONDITION_SCENE)
        return;

    if (conditionSceneMarqueeBase.length() == 0)
        return;

    String combined = buildConditionMarqueeText(conditionSceneMarqueeBase);

    if (combined != conditionSceneMarqueeText && combined != conditionSceneMarqueePendingText)
    {
        conditionSceneMarqueePendingText = combined;
    }

    renderConditionSceneMarquee(false);
}

// Public helpers
void requestScrollRebuild()
{
    needScrollRebuild = true;
}

void notifyUnitsMaybeChanged()
{
    const uint16_t sig = unitSignature();
    if (sig != lastUnitSig)
    {
        lastUnitSig = sig;
        needScrollRebuild = true;
    }
}

// Call this frequently; it only rebuilds when needed.
void serviceScrollRebuild()
{
    if (!needScrollRebuild)
        return;

    // Rebuild the line using the latest units + data
    createScrollingText();

    // Reset marquee state so it restarts smoothly
    start_Scroll_Text = false;
    set_up_Scrolling_Text_Length = true;
    text_Length_In_Pixel = getTextWidth(scrolling_Text.c_str());

    needScrollRebuild = false;
}

void applyUnitPreferences()
{
    useImperial = (units.temp == TempUnit::F);
    notifyUnitsMaybeChanged();
}



