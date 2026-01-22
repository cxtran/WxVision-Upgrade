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

static LunarDate convertSolar2Lunar(int dd, int mm, int yy, int timeZoneMinutes)
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
    uint16_t outline = scaleColor565(color, 0.62f);
    uint16_t highlight = scaleColor565(color, 1.08f);
    dma_display->fillCircle(cx, cy, 4, color);
    dma_display->fillCircle(cx - 4, cy + 1, 3, shade);
    dma_display->fillCircle(cx + 4, cy + 1, 3, shade);
    dma_display->fillCircle(cx - 2, cy + 3, 2, color);
    dma_display->fillCircle(cx + 2, cy + 3, 2, color);

    // Outline + highlight for better definition on 64x32 panels.
    dma_display->drawCircle(cx, cy, 4, outline);
    dma_display->drawCircle(cx - 4, cy + 1, 3, outline);
    dma_display->drawCircle(cx + 4, cy + 1, 3, outline);
    dma_display->drawCircle(cx - 2, cy + 3, 2, outline);
    dma_display->drawCircle(cx + 2, cy + 3, 2, outline);
    dma_display->drawFastHLine(cx - 8, cy + 5, 17, outline);
    dma_display->drawPixel(cx - 2, cy - 2, highlight);
    dma_display->drawPixel(cx + 1, cy - 1, highlight);
}

// Condition Scene (64x24) helpers ------------------------------------------------
static constexpr int SCENE_W = PANEL_RES_X;
static constexpr int SCENE_H = 24; // top area used for the scene; bottom is reserved for marquee/status

static void clearConditionSceneArea()
{
    dma_display->fillRect(0, 0, SCENE_W, SCENE_H, myBLACK);
    if (PANEL_RES_Y > SCENE_H)
        dma_display->fillRect(0, SCENE_H, SCENE_W, PANEL_RES_Y - SCENE_H, myBLACK);
}

static void fillDayBackground()
{
    // Pixel-art style (banded) sky like the reference image.
    uint16_t top = dma_display->color565(10, 120, 255);
    uint16_t mid = dma_display->color565(40, 165, 255);
    uint16_t bot = dma_display->color565(85, 205, 255);
    for (int y = 0; y < SCENE_H; ++y)
    {
        uint16_t c = (y < 8) ? top : (y < 16 ? mid : bot);
        dma_display->drawFastHLine(0, y, SCENE_W, c);
    }
}

static void fillNightBackground()
{
    // Deep blue + stars like the reference image.
    uint16_t top = dma_display->color565(2, 6, 28);
    uint16_t mid = dma_display->color565(4, 10, 40);
    uint16_t bot = dma_display->color565(7, 16, 58);
    for (int y = 0; y < SCENE_H; ++y)
    {
        uint16_t c = (y < 8) ? top : (y < 16 ? mid : bot);
        dma_display->drawFastHLine(0, y, SCENE_W, c);
    }

    uint16_t star = dma_display->color565(230, 240, 255);
    // Fixed starfield (no flicker)
    const uint8_t stars[][2] = {
        {6, 3},  {13, 6}, {18, 2}, {28, 5}, {36, 3}, {44, 7},
        {52, 4}, {58, 6}, {24, 11}, {40, 12}, {12, 14}, {54, 13}
    };
    for (auto &p : stars)
    {
        dma_display->drawPixel(p[0], p[1], star);
        // occasional plus star
        if ((p[0] % 3) == 0)
        {
            if (p[0] > 0) dma_display->drawPixel(p[0] - 1, p[1], star);
            if (p[0] < SCENE_W - 1) dma_display->drawPixel(p[0] + 1, p[1], star);
            if (p[1] > 0) dma_display->drawPixel(p[0], p[1] - 1, star);
            if (p[1] < SCENE_H - 1) dma_display->drawPixel(p[0], p[1] + 1, star);
        }
    }
}

static void drawPixelSun(int cx, int cy)
{
    // Compact pixel sun with orange edge + yellow center
    uint16_t edge = dma_display->color565(255, 170, 0);
    uint16_t fill = dma_display->color565(255, 235, 70);
    uint16_t hi = dma_display->color565(255, 255, 150);
    const int r = 6;
    for (int dy = -r; dy <= r; ++dy)
    {
        int ady = abs(dy);
        int span = (ady == 6) ? 1 : (ady == 5 ? 3 : (ady == 4 ? 4 : (ady == 3 ? 5 : (ady == 2 ? 6 : (ady == 1 ? 6 : 6)))));
        int y = cy + dy;
        if (y < 0 || y >= SCENE_H) continue;
        for (int dx = -span; dx <= span; ++dx)
        {
            int x = cx + dx;
            if (x < 0 || x >= SCENE_W) continue;
            bool border = (ady == r) || (abs(dx) == span);
            dma_display->drawPixel(x, y, border ? edge : fill);
        }
    }
    // highlight
    dma_display->drawPixel(cx - 2, cy - 2, hi);
    dma_display->drawPixel(cx - 1, cy - 3, hi);
    // rays (8-bit style)
    dma_display->drawPixel(cx, cy - 9, edge);
    dma_display->drawPixel(cx, cy + 9, edge);
    dma_display->drawPixel(cx - 9, cy, edge);
    dma_display->drawPixel(cx + 9, cy, edge);
    dma_display->drawPixel(cx - 7, cy - 7, edge);
    dma_display->drawPixel(cx + 7, cy - 7, edge);
    dma_display->drawPixel(cx - 7, cy + 7, edge);
    dma_display->drawPixel(cx + 7, cy + 7, edge);
}

static void drawPixelMoon(int cx, int cy)
{
    // Crescent moon (filled disc minus offset disc), tuned for pixel art.
    uint16_t moon = dma_display->color565(255, 235, 95);
    uint16_t rim = dma_display->color565(255, 210, 40);
    uint16_t skyCut = dma_display->color565(4, 10, 40);
    const int r = 7;
    for (int dy = -r; dy <= r; ++dy)
    {
        int y = cy + dy;
        if (y < 0 || y >= SCENE_H) continue;
        int span = (int)sqrt((double)(r * r - dy * dy));
        for (int dx = -span; dx <= span; ++dx)
        {
            int x = cx + dx;
            if (x < 0 || x >= SCENE_W) continue;
            dma_display->drawPixel(x, y, moon);
        }
    }
    // cut-out to form crescent
    int cutR = 6;
    for (int dy = -cutR; dy <= cutR; ++dy)
    {
        int y = cy + dy;
        if (y < 0 || y >= SCENE_H) continue;
        int span = (int)sqrt((double)(cutR * cutR - dy * dy));
        for (int dx = -span; dx <= span; ++dx)
        {
            int x = cx + dx + 3;
            if (x < 0 || x >= SCENE_W) continue;
            dma_display->drawPixel(x, y, skyCut);
        }
    }
    // rim accent
    dma_display->drawPixel(cx - 5, cy - 4, rim);
    dma_display->drawPixel(cx - 6, cy - 1, rim);
    dma_display->drawPixel(cx - 5, cy + 2, rim);
}

static void drawPixelCloud(int x, int y, bool night)
{
    // Pixel-art cloud built from scanline spans (outline + 2 shades).
    const uint8_t spans[][2] = {
        {12, 12}, {9, 18}, {6, 24}, {4, 28}, {2, 32}, {1, 34},
        {1, 34}, {2, 32}, {3, 30}, {5, 26}, {7, 22}, {9, 18}
    };
    uint16_t outline = night ? dma_display->color565(40, 55, 85) : dma_display->color565(70, 95, 140);
    uint16_t fill = night ? dma_display->color565(140, 160, 195) : dma_display->color565(215, 230, 245);
    uint16_t shade = night ? dma_display->color565(95, 115, 150) : dma_display->color565(170, 190, 215);
    uint16_t hi = night ? dma_display->color565(175, 195, 230) : dma_display->color565(245, 250, 255);

    for (int row = 0; row < 12; ++row)
    {
        int yy = y + row;
        if (yy < 0 || yy >= SCENE_H) continue;
        int start = x + spans[row][0];
        int width = spans[row][1];
        int end = start + width - 1;
        if (end < 0 || start >= SCENE_W) continue;

        // Outline edges
        for (int xx = start; xx <= end; ++xx)
        {
            if (xx < 0 || xx >= SCENE_W) continue;
            bool edge = (row == 0) || (row == 11) || (xx == start) || (xx == end);
            dma_display->drawPixel(xx, yy, edge ? outline : fill);
        }

        // Bottom shading band
        if (row >= 7 && width > 6)
        {
            for (int xx = start + 2; xx <= end - 2; ++xx)
            {
                if ((xx + row) % 3 == 0)
                    dma_display->drawPixel(xx, yy, shade);
            }
        }
    }

    // Highlights on upper-left
    dma_display->drawPixel(x + 16, y + 2, hi);
    dma_display->drawPixel(x + 18, y + 3, hi);
    dma_display->drawPixel(x + 14, y + 4, hi);
}

static void drawPixelRain(int x, int y, int w, bool night)
{
    uint16_t drop = night ? dma_display->color565(80, 170, 255) : dma_display->color565(0, 200, 255);
    uint16_t drop2 = night ? dma_display->color565(40, 120, 220) : dma_display->color565(0, 140, 220);
    int phase = (millis() / 130) % 6;
    for (int col = x; col < x + w; col += 4)
    {
        for (int i = 0; i < 6; ++i)
        {
            int yy = y + ((i * 3 + (col / 4) + phase) % 12);
            if (yy >= SCENE_H - 1) continue;
            dma_display->drawPixel(col, yy, (i % 2 == 0) ? drop : drop2);
            dma_display->drawPixel(col - 1, yy + 1, (i % 2 == 0) ? drop : drop2);
        }
    }
}

static void drawPixelSnow(int x, int y, int w, bool night)
{
    uint16_t snow = night ? dma_display->color565(200, 220, 255) : dma_display->color565(240, 255, 255);
    int phase = (millis() / 250) % 8;
    for (int col = x; col < x + w; col += 7)
    {
        for (int i = 0; i < 4; ++i)
        {
            int yy = y + ((i * 4 + (col / 7) + phase) % 12);
            if (yy >= SCENE_H - 1) continue;
            dma_display->drawPixel(col, yy, snow);
            if (col > 0) dma_display->drawPixel(col - 1, yy, snow);
            if (col < SCENE_W - 1) dma_display->drawPixel(col + 1, yy, snow);
            if (yy > 0) dma_display->drawPixel(col, yy - 1, snow);
            if (yy < SCENE_H - 1) dma_display->drawPixel(col, yy + 1, snow);
        }
    }
}

static void drawPixelBolt(int x, int y)
{
    uint16_t bolt = dma_display->color565(255, 210, 40);
    uint16_t hi = dma_display->color565(255, 255, 180);
    // thick zig-zag bolt
    dma_display->drawLine(x, y, x - 4, y + 7, bolt);
    dma_display->drawLine(x + 1, y, x - 3, y + 7, bolt);
    dma_display->drawLine(x - 4, y + 7, x + 2, y + 7, bolt);
    dma_display->drawLine(x + 2, y + 7, x - 5, y + 16, bolt);
    dma_display->drawLine(x - 5, y + 16, x + 3, y + 12, bolt);
    dma_display->drawLine(x + 3, y + 12, x - 1, y + 20, bolt);
    // highlight pixels
    dma_display->drawPixel(x - 1, y + 2, hi);
    dma_display->drawPixel(x - 2, y + 10, hi);
}

static void drawSceneSkyGradient(const uint16_t *colors, int count)
{
    if (count <= 0)
        return;
    for (int y = 0; y < SCENE_H; ++y)
    {
        int idx = (count == 1) ? 0 : (y * (count - 1)) / (SCENE_H - 1);
        dma_display->drawFastHLine(0, y, SCENE_W, colors[idx]);
    }
}

static uint16_t lerp565(uint16_t a, uint16_t b, int t256)
{
    uint8_t ar = ((a >> 11) & 0x1F) * 255 / 31;
    uint8_t ag = ((a >> 5) & 0x3F) * 255 / 63;
    uint8_t ab = (a & 0x1F) * 255 / 31;
    uint8_t br = ((b >> 11) & 0x1F) * 255 / 31;
    uint8_t bg = ((b >> 5) & 0x3F) * 255 / 63;
    uint8_t bb = (b & 0x1F) * 255 / 31;

    uint8_t r = (uint8_t)((ar * (256 - t256) + br * t256) / 256);
    uint8_t g = (uint8_t)((ag * (256 - t256) + bg * t256) / 256);
    uint8_t bl = (uint8_t)((ab * (256 - t256) + bb * t256) / 256);
    return dma_display->color565(r, g, bl);
}

static void drawSceneSkyLerp(uint16_t top, uint16_t bottom)
{
    for (int y = 0; y < SCENE_H; ++y)
    {
        int t256 = (SCENE_H <= 1) ? 0 : (y * 256) / (SCENE_H - 1);
        dma_display->drawFastHLine(0, y, SCENE_W, lerp565(top, bottom, t256));
    }
}

static void drawSceneGround(uint16_t groundTop, uint16_t groundBottom)
{
    const int h = 3;
    for (int i = 0; i < h; ++i)
    {
        int y = SCENE_H - h + i;
        int t256 = (h <= 1) ? 0 : (i * 256) / (h - 1);
        dma_display->drawFastHLine(0, y, SCENE_W, lerp565(groundTop, groundBottom, t256));
    }
    dma_display->drawFastHLine(0, SCENE_H - h - 1, SCENE_W, scaleColor565(groundTop, 1.1f));
}

static void drawCrescentMoon(int cx, int cy, int r, uint16_t moonColor, uint16_t skyColor)
{
    dma_display->fillCircle(cx, cy, r, moonColor);
    dma_display->fillCircle(cx + (r / 3), cy - (r / 4), r - 2, skyColor);
    dma_display->drawCircle(cx, cy, r, scaleColor565(moonColor, 0.75f));
}

static void drawSunWithGlow(int cx, int cy, int r, uint16_t sunColor)
{
    uint16_t glow = scaleColor565(sunColor, 0.45f);
    dma_display->fillCircle(cx, cy, r + 2, glow);
    dma_display->fillCircle(cx, cy, r, sunColor);
    dma_display->fillCircle(cx, cy, r - 3, dma_display->color565(255, 255, 160));
    dma_display->drawCircle(cx, cy, r, scaleColor565(sunColor, 0.75f));

    uint16_t ray = scaleColor565(sunColor, 0.80f);
    dma_display->drawLine(cx, cy - (r + 3), cx, cy - (r + 1), ray);
    dma_display->drawLine(cx, cy + (r + 1), cx, cy + (r + 3), ray);
    dma_display->drawLine(cx - (r + 3), cy, cx - (r + 1), cy, ray);
    dma_display->drawLine(cx + (r + 1), cy, cx + (r + 3), cy, ray);
    dma_display->drawLine(cx - (r + 2), cy - (r + 2), cx - r, cy - r, ray);
    dma_display->drawLine(cx + r, cy - (r + 2), cx + (r + 2), cy - r, ray);
    dma_display->drawLine(cx - (r + 2), cy + (r + 2), cx - r, cy + r, ray);
    dma_display->drawLine(cx + r, cy + (r + 2), cx + (r + 2), cy + r, ray);
}

static void drawCloudIconLarge(int x, int y, uint16_t cLight, uint16_t cMid, uint16_t cDark, uint16_t outline)
{
    // Large readable cloud (~40x16)
    dma_display->fillCircle(x + 10, y + 8, 6, cLight);
    dma_display->fillCircle(x + 20, y + 6, 7, cMid);
    dma_display->fillCircle(x + 30, y + 8, 6, cDark);
    dma_display->fillRoundRect(x + 6, y + 10, 32, 7, 3, cMid);
    dma_display->fillRect(x + 6, y + 12, 32, 5, cMid);

    // Shading band
    dma_display->drawFastHLine(x + 7, y + 12, 30, scaleColor565(cMid, 0.85f));
    dma_display->drawFastHLine(x + 8, y + 13, 28, scaleColor565(cMid, 0.80f));

    // Outline
    dma_display->drawCircle(x + 10, y + 8, 6, outline);
    dma_display->drawCircle(x + 20, y + 6, 7, outline);
    dma_display->drawCircle(x + 30, y + 8, 6, outline);
    dma_display->drawRoundRect(x + 6, y + 10, 32, 7, 3, outline);
    dma_display->drawFastHLine(x + 6, y + 16, 32, outline);

    // Highlights
    dma_display->drawPixel(x + 17, y + 3, scaleColor565(cLight, 1.08f));
    dma_display->drawPixel(x + 22, y + 4, scaleColor565(cLight, 1.08f));
}

static void drawRainStreaks(int x0, int y0, int w, int yMax)
{
    int phase = (millis() / 120) % 6;
    uint16_t dropA = dma_display->color565(170, 245, 255);
    uint16_t dropB = dma_display->color565(80, 170, 230);
    for (int x = x0; x < x0 + w; x += 4)
    {
        for (int i = 0; i < 10; ++i)
        {
            int y = y0 + ((i * 3 + (x / 4) + phase) % 16);
            if (y >= yMax)
                continue;
            uint16_t c = (i % 3 == 0) ? dropB : dropA;
            dma_display->drawPixel(x, y, c);
            if (x > 0 && y + 1 < yMax)
                dma_display->drawPixel(x - 1, y + 1, c);
        }
    }
}

static void drawSnowDrift(int x0, int y0, int w, int yMax, uint16_t snowColor)
{
    int phase = (millis() / 220) % 8;
    for (int x = x0; x < x0 + w; x += 7)
    {
        for (int i = 0; i < 5; ++i)
        {
            int y = y0 + ((i * 4 + (x / 7) + phase) % 16);
            if (y >= yMax)
                continue;
            drawSnowflake(x, y, snowColor);
        }
    }
}

static void drawLightningBolt(int tipX, int tipY, uint16_t bolt, uint16_t glow)
{
    dma_display->drawLine(tipX, tipY, tipX - 4, tipY + 8, bolt);
    dma_display->drawLine(tipX - 4, tipY + 8, tipX + 1, tipY + 8, bolt);
    dma_display->drawLine(tipX + 1, tipY + 8, tipX - 6, tipY + 18, bolt);
    dma_display->drawLine(tipX - 6, tipY + 18, tipX + 3, tipY + 14, bolt);
    dma_display->drawLine(tipX + 3, tipY + 14, tipX - 1, tipY + 22, bolt);
    dma_display->drawLine(tipX, tipY, tipX - 4, tipY + 8, glow);
}

static void drawWeatherSceneSunny()
{
    clearConditionSceneArea();
    fillDayBackground();
    // Sunny Day: cloud left, sun right (like reference)
    drawPixelCloud(2, 8, false);
    drawPixelSun(50, 11);
}

static void drawWeatherSceneCloudy()
{
    clearConditionSceneArea();
    fillDayBackground();
    // Cloudy Day: big cloud centered
    drawPixelCloud(12, 7, false);
    drawPixelCloud(2, 10, false);
}

static void drawWeatherSceneCloudyNight()
{
    clearConditionSceneArea();
    fillNightBackground();
    // Cloudy Night: cloud + moon peeking right
    drawPixelCloud(10, 8, true);
    drawPixelMoon(54, 9);
}

static void drawWeatherSceneRain()
{
    clearConditionSceneArea();
    fillDayBackground();
    // Rainy Day: cloud + rain
    drawPixelCloud(10, 6, false);
    drawPixelRain(12, 14, 50, false);
}

static void drawWeatherSceneRainNight()
{
    clearConditionSceneArea();
    fillNightBackground();
    // Rainy Night: cloud + moon + rain
    drawPixelCloud(10, 7, true);
    drawPixelMoon(54, 9);
    drawPixelRain(12, 14, 50, true);
}

static void drawWeatherSceneThunderstorm()
{
    clearConditionSceneArea();
    fillDayBackground();
    // Thunderstorm Day: cloud + bolt + rain
    drawPixelCloud(10, 6, false);
    drawPixelBolt(38, 5);
    drawPixelRain(12, 14, 50, false);
}

static void drawWeatherSceneThunderstormNight()
{
    clearConditionSceneArea();
    fillNightBackground();
    // Thunderstorm Night: cloud + moon + bolt + rain
    drawPixelCloud(10, 7, true);
    drawPixelMoon(54, 9);
    drawPixelBolt(38, 5);
    drawPixelRain(12, 14, 50, true);
}

static void drawWeatherSceneSnow()
{
    clearConditionSceneArea();
    fillDayBackground();
    // Snowy Day: cloud + snow
    drawPixelCloud(10, 6, false);
    drawPixelSnow(12, 14, 50, false);
}

static void drawWeatherSceneSnowNight()
{
    clearConditionSceneArea();
    fillNightBackground();
    // Snowy Night: cloud + moon + snow
    drawPixelCloud(10, 7, true);
    drawPixelMoon(54, 9);
    drawPixelSnow(12, 14, 50, true);
}

static void drawWeatherSceneClearNight()
{
    clearConditionSceneArea();
    fillNightBackground();
    // Clear Night: moon + stars only (like reference)
    drawPixelMoon(18, 11);
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

static int wifiSignalLevelFromRssi(int rssi)
{
    if (rssi >= -55) return 3;   // excellent
    if (rssi >= -67) return 2;   // good
    if (rssi >= -75) return 1;   // fair
    return 0;                    // weak/very weak
}

void drawWiFiIcon(int x, int y, uint16_t color, int rssi)
{
    // Simple 7x5 Wi-Fi signal icon that reflects RSSI strength.
    // (x,y) = top-left corner of the icon.
    int level = wifiSignalLevelFromRssi(rssi);

    // bottom dot always visible when connected
    dma_display->drawPixel(x + 3, y + 4, color);
    dma_display->drawLine(x + 3, y + 4, x + 3, y + 6, color); // support bar

    if (level >= 1)
        dma_display->drawLine(x + 2, y + 3, x + 4, y + 3, color); // small arc
    if (level >= 2)
        dma_display->drawLine(x + 1, y + 2, x + 5, y + 2, color); // mid arc
    if (level >= 3)
        dma_display->drawLine(x + 0, y + 1, x + 6, y + 1, color); // top arc
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
    const char *dayStr = days[now.dayOfTheWeek()];
    char dateSuffix[10];
    snprintf(dateSuffix, sizeof(dateSuffix), " %02d/%02d", now.month(), now.day());
    char dateStr[14];
    snprintf(dateStr, sizeof(dateStr), "%s%s", dayStr, dateSuffix);

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
        drawWiFiIcon(wifiX, wifiY, wifiColor, WiFi.RSSI());
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
    uint16_t sundayColor = (theme == 1) ? dma_display->color565(180, 80, 120)
                                        : dma_display->color565(255, 80, 120);
    uint16_t saturdayColor = (theme == 1) ? dma_display->color565(80, 140, 200)
                                          : dma_display->color565(80, 180, 255);
    dma_display->getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
    int dateX = (64 - (int)w) / 2;
    int dateY = 25;
    dma_display->setCursor(dateX, dateY);
    if (now.dayOfTheWeek() == 0)
    {
        dma_display->setTextColor(sundayColor);
        dma_display->print(dayStr);
        dma_display->setTextColor(dateColor);
        dma_display->print(dateSuffix);
    }
    else if (now.dayOfTheWeek() == 6)
    {
        dma_display->setTextColor(saturdayColor);
        dma_display->print(dayStr);
        dma_display->setTextColor(dateColor);
        dma_display->print(dateSuffix);
    }
    else
    {
        dma_display->setTextColor(dateColor);
        dma_display->print(dateStr);
    }

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
