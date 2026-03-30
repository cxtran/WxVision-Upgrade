#include <Arduino.h>
#include <ctype.h>
#include "display.h"
#include "icons.h"
#include "settings.h"
#include "RTClib.h" // For RTC DateTime
#include "sensors.h"
// #include "fonts/FreeSans9pt7b.h"
#include "fonts/verdanab8pt7b.h"
#include "datetimesettings.h"
#include "units.h"
#include "env_quality.h"
#include "alarm.h"
#include <U8g2_for_Adafruit_GFX.h>

#include "tempest.h"
#include "weather_countries.h"
#include "InfoModal.h"
#include <math.h>
#include "fortune_headline.h"
#include "fortune_phrase_picker.h"
#include "ir_codes.h"
#include "worldtime.h"
#include "display_runtime.h"
#include "display_seven_segment.h"
#include "display_lunar_luck_sections.h"
#include "display_lunar_luck_nen_tranh.h"
#include "display_condition.h"
#include "app_state.h"
#include "ui_theme.h"
#include "display_widgets.h"
#include "notifications.h"
#include "screen_manager.h"

static AppState &app = appState();
#define scrollLevel app.scrollLevel
#define scrollSpeed app.scrollSpeed
#define theme app.theme
#define humOffset app.humOffset
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
    const char *stemsVi[10] = {"Giáp", "Ất", "Bính", "Đinh", "Mậu", "Kỷ", "Canh", "Tân", "Nhâm", "Quý"};
    const char *branchesVi[12] = {"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ", "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"};
    const char *animalsVi[12] = {"Chuột", "Trâu", "Hổ", "Mèo", "Rồng", "Rắn", "Ngựa", "Dê", "Khỉ", "Gà", "Chó", "Heo"};
    const char *animalsEn[12] = {"Rat", "Ox", "Tiger", "Cat", "Dragon", "Snake", "Horse", "Goat", "Monkey", "Rooster", "Dog", "Pig"};

    const char *elementsEn[5] = {"Wood", "Fire", "Earth", "Metal", "Water"};

    int stemIndex = (lunarYear + 6) % 10;
    int branchIndex = (lunarYear + 8) % 12;

    stemBranchVi = String(stemsVi[stemIndex]) + " " + branchesVi[branchIndex];
    zodiacVi = String("Năm con ") + animalsVi[branchIndex];
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

size_t buildNormalized(char *dst, size_t dstCap, const char *src);
size_t summarizeListToBullets(char *dst, size_t cap, const char *src, int maxItems);

static String formatLunarHourTag()
{
    // Map local hour (0â€“23) to Vietnamese lunar hour name
    static const char *names[12] = {
        "Giờ Tý",  "Giờ Sửu",  "Giờ Dần",  "Giờ Mão",
        "Giờ Thìn","Giờ Tỵ",   "Giờ Ngọ",  "Giờ Mùi",
        "Giờ Thân","Giờ Dậu",  "Giờ Tuất", "Giờ Hợi"};

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

static String formatSolarTermTagVi()
{
    int m = d_month;
    int d = d_day;

    switch (m)
    {
    case 1:
        return (d <= 15) ? String("Tiểu Hàn") : String("Đại Hàn");
    case 2:
        return (d <= 15) ? String("Lập Xuân") : String("Vũ Thủy");
    case 3:
        return (d <= 15) ? String("Kinh Trập") : String("Xuân Phân");
    case 4:
        return (d <= 15) ? String("Thanh Minh") : String("Cốc Vũ");
    case 5:
        return (d <= 15) ? String("Lập Hạ") : String("Tiểu Mãn");
    case 6:
        return (d <= 15) ? String("Mang Chủng") : String("Hạ Chí");
    case 7:
        return (d <= 15) ? String("Tiểu Thử") : String("Đại Thử");
    case 8:
        return (d <= 15) ? String("Lập Thu") : String("Xử Thử");
    case 9:
        return (d <= 15) ? String("Bạch Lộ") : String("Thu Phân");
    case 10:
        return (d <= 15) ? String("Hàn Lộ") : String("Sương Giáng");
    case 11:
        return (d <= 15) ? String("Lập Đông") : String("Tiểu Tuyết");
    case 12:
        return (d <= 15) ? String("Đại Tuyết") : String("Đông Chí");
    default:
        return String("");
    }
}

static String formatLunarDayName(int dd, int mm, int yy)
{
    long jd = jdFromDate(dd, mm, yy);

    static const char *stemsVi[10] = {"Giáp", "Ất", "Bính", "Đinh", "Mậu", "Kỷ", "Canh", "Tân", "Nhâm", "Quý"};
    static const char *branchesVi[12] = {"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ", "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"};

    int stemIndex = (int)((jd + 9) % 10);
    int branchIndex = (int)((jd + 1) % 12);
    if (stemIndex < 0)
        stemIndex += 10;
    if (branchIndex < 0)
        branchIndex += 12;

    return String(stemsVi[stemIndex]) + " " + branchesVi[branchIndex];
}

static String formatCanChiDayAscii(int dd, int mm, int yy)
{
    long jd = jdFromDate(dd, mm, yy);
    static const char *stems[10] = {"Giap", "At", "Binh", "Dinh", "Mau", "Ky", "Canh", "Tan", "Nham", "Quy"};
    static const char *branches[12] = {"Ty", "Suu", "Dan", "Mao", "Thin", "Ty", "Ngo", "Mui", "Than", "Dau", "Tuat", "Hoi"};
    int stemIndex = static_cast<int>((jd + 9) % 10);
    int branchIndex = static_cast<int>((jd + 1) % 12);
    if (stemIndex < 0)
        stemIndex += 10;
    if (branchIndex < 0)
        branchIndex += 12;
    return String(stems[stemIndex]) + " " + branches[branchIndex];
}

static String formatCanChiYearAscii(int lunarYear)
{
    static const char *stems[10] = {"Giap", "At", "Binh", "Dinh", "Mau", "Ky", "Canh", "Tan", "Nham", "Quy"};
    static const char *branches[12] = {"Ty", "Suu", "Dan", "Mao", "Thin", "Ty", "Ngo", "Mui", "Than", "Dau", "Tuat", "Hoi"};
    int stemIndex = (lunarYear + 6) % 10;
    int branchIndex = (lunarYear + 8) % 12;
    return String(stems[stemIndex]) + " " + branches[branchIndex];
}

static String formatCanChiYearVi(int lunarYear)
{
    static const char *stems[10] = {"Giáp", "Ất", "Bính", "Đinh", "Mậu", "Kỷ", "Canh", "Tân", "Nhâm", "Quý"};
    static const char *branches[12] = {"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ", "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"};
    int stemIndex = (lunarYear + 6) % 10;
    int branchIndex = (lunarYear + 8) % 12;
    return String(stems[stemIndex]) + " " + branches[branchIndex];
}

static String formatCanChiMonthAscii(int lunarYear, int lunarMonth)
{
    static const char *stems[10] = {"Giap", "At", "Binh", "Dinh", "Mau", "Ky", "Canh", "Tan", "Nham", "Quy"};
    static const char *branches[12] = {"Ty", "Suu", "Dan", "Mao", "Thin", "Ty", "Ngo", "Mui", "Than", "Dau", "Tuat", "Hoi"};

    // Month 1 is Dần. Stem of month 1 depends on year stem.
    // Giáp/Kỷ->Bính, Ất/Canh->Mậu, Bính/Tân->Canh, Đinh/Nhâm->Nhâm, Mậu/Quý->Giáp.
    const int month1StemByYearStem[10] = {2, 4, 6, 8, 0, 2, 4, 6, 8, 0};
    int yearStemIndex = (lunarYear + 6) % 10;
    int stemIndex = (month1StemByYearStem[yearStemIndex] + (lunarMonth - 1)) % 10;
    int branchIndex = (lunarMonth + 1) % 12; // lunar month 1 -> Dần
    return String(stems[stemIndex]) + " " + branches[branchIndex];
}

static String formatCanChiMonthVi(int lunarYear, int lunarMonth)
{
    static const char *stems[10] = {"Giáp", "Ất", "Bính", "Đinh", "Mậu", "Kỷ", "Canh", "Tân", "Nhâm", "Quý"};
    static const char *branches[12] = {"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ", "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"};

    const int month1StemByYearStem[10] = {2, 4, 6, 8, 0, 2, 4, 6, 8, 0};
    int yearStemIndex = (lunarYear + 6) % 10;
    int stemIndex = (month1StemByYearStem[yearStemIndex] + (lunarMonth - 1)) % 10;
    int branchIndex = (lunarMonth + 1) % 12;
    return String(stems[stemIndex]) + " " + branches[branchIndex];
}

// Lunar marquee state (merged screen)
static String lunarLines[3];
static uint16_t lunarWidths[3] = {0, 0, 0};
static int lunarOffsets[3] = {0, 0, 0};
static unsigned long lastLunarTick = 0;
static bool lunarInitialized = false;
static bool lunarLuckInitialized = false;
static int lunarLuckBuiltDay = -1;
static int lunarLuckBuiltMonth = -1;
static int lunarLuckBuiltYear = -1;
static int lunarLuckScore = 0;
static float lunarLuckSpeedScale = 1.0f; // runtime scale vs global speed (1.0 = global)
static U8G2_FOR_ADAFRUIT_GFX lunarLuckUtf8;
static bool lunarLuckUtf8Ready = false;

static void ensureLunarLuckUtf8()
{
    if (lunarLuckUtf8Ready || !dma_display)
        return;
    lunarLuckUtf8.begin(*dma_display);
    lunarLuckUtf8.setFontMode(1);
    lunarLuckUtf8.setFontDirection(0);
    lunarLuckUtf8Ready = true;
}

static void setLunarLuckUtf8Font()
{
    ensureLunarLuckUtf8();
    if (!lunarLuckUtf8Ready)
        return;
    lunarLuckUtf8.setFont(u8g2_font_unifont_t_vietnamese1);
}

#define MAX_SECTIONS 10
#define TITLE_MAX 48
#define CONTENT_MAX 420
#define GOIY_CONTENT_MAX 800
#define SEP " * "

static const char *const PHRASES_CADAO_TOT[] PROGMEM = {
    "Thiên thời, địa lợi, nhân hòa.",
    "Trời không phụ lòng người.",
    "Ở hiền gặp lành.",
    "Đức năng thắng số.",
    "Hữu xạ tự nhiên hương.",
    "Có công mài sắt, có ngày nên kim.",
    "Nước chảy đá mòn.",
    "Kiến tha lâu cũng đầy tổ.",
    "Tích tiểu thành đại.",
    "Góp gió thành bão."
};

static const char *const PHRASES_CADAO_BINH[] PROGMEM = {
    "Chậm mà chắc.",
    "Cẩn tắc vô áy náy.",
    "Liệu cơm gắp mắm.",
    "Lời nói chẳng mất tiền mua.",
    "Ăn quả nhớ kẻ trồng cây.",
    "Uống nước nhớ nguồn.",
    "Kính trên nhường dưới.",
    "Giấy rách phải giữ lấy lề.",
    "Khôn ngoan đối đáp người ngoài.",
    "Một cây làm chẳng nên non."
};

static const char *const PHRASES_CADAO_XAU[] PROGMEM = {
    "Dục tốc bất đạt.",
    "Tham thì thâm.",
    "Thấy lợi đừng vội mừng.",
    "Một điều nhịn, chín điều lành.",
    "Người tính không bằng trời tính.",
    "Mưu sự tại nhân, thành sự tại thiên.",
    "Lửa gần rơm lâu ngày cũng bén.",
    "Đánh kẻ chạy đi, không ai đánh người chạy lại.",
    "Có thờ có thiêng, có kiêng có lành.",
    "Cẩn tắc vô áy náy."
};

#define CADAO_TOT_COUNT (sizeof(PHRASES_CADAO_TOT) / sizeof(PHRASES_CADAO_TOT[0]))
#define CADAO_BINH_COUNT (sizeof(PHRASES_CADAO_BINH) / sizeof(PHRASES_CADAO_BINH[0]))
#define CADAO_XAU_COUNT (sizeof(PHRASES_CADAO_XAU) / sizeof(PHRASES_CADAO_XAU[0]))

struct LuckSection
{
    const char *title;
    char *content;
    uint16_t contentCap;
    uint16_t contentLen;
    bool marquee;
    int16_t contentWidthPx;
};

enum LuckTone : int8_t
{
    TONE_XAU = -1,
    TONE_BINH = 0,
    TONE_TOT = 1
};

struct XuatHanhInfo
{
    const char *name;
    LuckTone tone;
};

static const char *toneLabelVN(LuckTone t)
{
    if (t > 0)
        return "Tốt";
    if (t < 0)
        return "Xấu";
    return "Bình";
}

LuckSection g_sections[MAX_SECTIONS];
static char g_sectionContent[MAX_SECTIONS][CONTENT_MAX];
char g_goiyContent[GOIY_CONTENT_MAX];
uint8_t g_sectionCount = 0;
static char g_titleTopic[TITLE_MAX];

uint8_t currentSectionIndex = 0;
static int16_t marqueeOffsetPx = 0;
static uint32_t lastScrollMs = 0;
static uint32_t sectionStartMs = 0;
static bool marqueeActive = false;
static bool lunarLuckRotationPaused = false;
static bool lunarPreviewMode = false;
static int16_t lunarDayOffset = 0; // 0=today, +N future day, -N past day
static constexpr int16_t LUNAR_OFFSET_MIN = -30;
static constexpr int16_t LUNAR_OFFSET_MAX = 30;
static uint32_t upLastPressMs = 0;
static uint32_t downLastPressMs = 0;
static constexpr uint16_t DBL_MS = 320;
enum HeaderAnimState
{
    HDR_IDLE = 0,
    HDR_DOOR
};
HeaderAnimState hdrState = HDR_IDLE;
char hdrOld[TITLE_MAX];
char hdrNew[TITLE_MAX];
int16_t hdrDoorPx = 0;
bool hdrDrawNew = false;
static bool hdrReverse = false;
bool hdrBrightnessPulsed = false;
uint8_t hdrBrightnessSaved = 0;
static uint32_t hdrAnimStartMs = 0;
static uint32_t hdrLastFrameMs = 0;
static uint32_t hdrDelayStartMs = 0;
bool hdrDelayActive = false;
static constexpr uint16_t HDR_ANIM_MS = 160;
static constexpr uint16_t HDR_FRAME_MS = 16;
static constexpr int16_t HEADER_Y = 0;
static constexpr int16_t HEADER_H = 16;
static constexpr int16_t HEADER_W = PANEL_RES_X;
static constexpr int16_t HEADER_PAD_X = 2;
static constexpr uint8_t HDR_PULSE_DELTA = 18;
static constexpr uint16_t HDR_PULSE_START_MS = 0;
static constexpr uint16_t HDR_PULSE_END_MS = HDR_ANIM_MS;
static constexpr bool HDR_EDGE_HIGHLIGHT = true;
static constexpr uint16_t HDR_START_DELAY_MS = 50;

static constexpr uint32_t STATIC_DWELL_MS = 4500ul;
static constexpr float LUNAR_LUCK_SCROLL_FACTOR = 0.72f;

static unsigned long lunarLuckBaseScrollIntervalMs();
static constexpr int16_t SCROLL_STEP_PX = 1;
static constexpr int16_t GAP_PX = 16;
static constexpr int LINE1_Y = 14;
static constexpr int LINE2_Y = 30;
static constexpr int LINE2_CLEAR_Y = 18;
static constexpr int LINE2_CLEAR_H = 14;
// Clear full lower band to avoid residue on Vietnamese glyph extents.
static constexpr int LINE2_CLEAR_Y_SAFE = 16;
static constexpr int LINE2_CLEAR_H_SAFE = 16;

static inline float easeInOutCubic(float t)
{
    if (t < 0.5f)
        return 4.0f * t * t * t;
    const float u = -2.0f * t + 2.0f;
    return 1.0f - ((u * u * u) / 2.0f);
}

static bool isLeapYearGregorian(int year)
{
    if ((year % 400) == 0)
        return true;
    if ((year % 100) == 0)
        return false;
    return (year % 4) == 0;
}

static int daysInMonthGregorian(int year, int month)
{
    static const uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 30;
    if (month == 2 && isLeapYearGregorian(year))
        return 29;
    return kDays[month - 1];
}

static void addDaysToDate(int &year, int &month, int &day, int deltaDays)
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

static bool isDoublePress(uint32_t &lastMs)
{
    const uint32_t now = millis();
    if (lastMs != 0 && (now - lastMs) <= DBL_MS)
    {
        lastMs = 0;
        return true;
    }
    lastMs = now;
    return false;
}

static size_t boundedLen(const char *s, size_t cap)
{
    if (!s || cap == 0)
        return 0;
    size_t i = 0;
    while (i < cap && s[i] != '\0')
        ++i;
    return i;
}

static size_t safeAppendN(char *dst, size_t cap, const char *src, size_t srcLen)
{
    if (!dst || cap == 0 || !src)
        return 0;
    size_t len = boundedLen(dst, cap - 1);
    if (len >= cap - 1)
    {
        dst[cap - 1] = '\0';
        return cap - 1;
    }
    size_t room = (cap - 1) - len;
    size_t copyLen = (srcLen < room) ? srcLen : room;
    if (copyLen > 0)
        memcpy(dst + len, src, copyLen);
    dst[len + copyLen] = '\0';
    return len + copyLen;
}

static size_t safeAppend(char *dst, size_t cap, const char *text)
{
    if (!text)
        return boundedLen(dst, cap);
    return safeAppendN(dst, cap, text, strlen(text));
}

static size_t appendSeparator(char *dst, size_t cap)
{
    size_t len = boundedLen(dst, cap - 1);
    while (len > 0 && dst[len - 1] == ' ')
    {
        dst[len - 1] = '\0';
        --len;
    }
    if (len == 0)
        return 0;
    return safeAppend(dst, cap, SEP);
}


static void safeAppendClauseBoundary(char *dst, size_t cap, const char *clause)
{
    if (!dst || cap == 0 || !clause || clause[0] == '\0')
        return;

    const size_t len = boundedLen(dst, cap - 1);
    const size_t clauseLen = strlen(clause);
    const size_t sepLen = (len > 0) ? strlen(SEP) : 0;

    if (len + sepLen + clauseLen <= cap - 1)
    {
        if (len > 0)
            safeAppend(dst, cap, SEP);
        safeAppend(dst, cap, clause);
        return;
    }

    char *lastSep = nullptr;
    for (char *p = strstr(dst, SEP); p != nullptr; p = strstr(p + 1, SEP))
        lastSep = p;

    if (lastSep)
    {
        *lastSep = '\0';
    }
    else
    {
        if (cap < 4)
            return;
        dst[cap - 4] = '\0';
    }

    size_t cutLen = boundedLen(dst, cap - 1);
    while (cutLen > 0 && dst[cutLen - 1] == ' ')
    {
        dst[cutLen - 1] = '\0';
        --cutLen;
    }
    safeAppend(dst, cap, "...");
}

static int scoreToneSign(int score)
{
    if (score >= 2)
        return 1;
    if (score <= -1)
        return -1;
    return 0;
}

static void buildCaDaoPhrase(char *dst, size_t cap,
                             int score, int lunarDay, int lunarMonth, int lunarYear,
                             const LunarDayDetail &dayInfo)
{
    if (!dst || cap == 0)
        return;
    dst[0] = '\0';

    const int tone = scoreToneSign(score);
    const char *const *pool = PHRASES_CADAO_BINH;
    uint16_t poolCount = static_cast<uint16_t>(CADAO_BINH_COUNT);
    if (tone > 0)
    {
        pool = PHRASES_CADAO_TOT;
        poolCount = static_cast<uint16_t>(CADAO_TOT_COUNT);
    }
    else if (tone < 0)
    {
        pool = PHRASES_CADAO_XAU;
        poolCount = static_cast<uint16_t>(CADAO_XAU_COUNT);
    }

    if (poolCount == 0)
        return;

    const uint32_t seed = dailySeed(lunarYear, lunarMonth, lunarDay, score, dayInfo.branch);
    const uint32_t salted = seed ^ 0xCADA0123u;
    const uint16_t idx = static_cast<uint16_t>(salted % poolCount);
    const char *phrase = reinterpret_cast<const char *>(pgm_read_ptr(&pool[idx]));
    safeAppend(dst, cap, phrase ? phrase : "");
}

int measureTextWidthPx(const char *s, uint16_t len)
{
    if (!s || len == 0)
        return 0;

    setLunarLuckUtf8Font();
    if (lunarLuckUtf8Ready)
    {
        int w = static_cast<int>(lunarLuckUtf8.getUTF8Width(s));
        if (w > 0)
            return w;
    }

    int glyphs = 0;
    for (uint16_t i = 0; i < len; ++i)
    {
        const uint8_t b = static_cast<uint8_t>(s[i]);
        if ((b & 0xC0) != 0x80)
            ++glyphs;
    }
    return glyphs * 6;
}

static void addSection(const char *title, const char *contentSrc, bool normalize, bool summarize, int maxItems,
                       char *contentBuf = nullptr, size_t contentCap = 0)
{
    if (g_sectionCount >= MAX_SECTIONS)
        return;

    LuckSection &sec = g_sections[g_sectionCount];
    sec.title = title ? title : "";
    if (!contentBuf || contentCap == 0)
    {
        contentBuf = g_sectionContent[g_sectionCount];
        contentCap = CONTENT_MAX;
    }
    sec.content = contentBuf;
    sec.contentCap = static_cast<uint16_t>(contentCap);
    sec.content[0] = '\0';

    if (summarize)
        summarizeListToBullets(sec.content, sec.contentCap, contentSrc, maxItems);
    else if (normalize)
        buildNormalized(sec.content, sec.contentCap, contentSrc);
    else if (contentSrc)
        safeAppend(sec.content, sec.contentCap, contentSrc);

    sec.contentLen = static_cast<uint16_t>(strlen(sec.content));
    sec.contentWidthPx = static_cast<int16_t>(measureTextWidthPx(sec.content, sec.contentLen));
    sec.marquee = (sec.contentWidthPx > PANEL_RES_X);
    ++g_sectionCount;
}

enum ViToneMarkBits : uint16_t
{
    VI_TONE_NONE = 0,
    VI_TONE_ACUTE = 1 << 0,
    VI_TONE_GRAVE = 1 << 1,
    VI_TONE_HOOK = 1 << 2,
    VI_TONE_TILDE = 1 << 3,
    VI_TONE_DOT = 1 << 4,
    VI_SHAPE_CIRC = 1 << 5,
    VI_SHAPE_BREVE = 1 << 6,
    VI_SHAPE_HORN = 1 << 7,
    VI_SHAPE_DBAR = 1 << 8
};

struct ViGlyphTiny
{
    char base;
    uint16_t marks;
};

static bool decodeUtf8Tiny(const String &text, int &i, uint32_t &cp)
{
    if (i >= text.length())
        return false;
    const uint8_t c0 = static_cast<uint8_t>(text[i++]);
    if ((c0 & 0x80) == 0)
    {
        cp = c0;
        return true;
    }
    if ((c0 & 0xE0) == 0xC0 && i < text.length())
    {
        const uint8_t c1 = static_cast<uint8_t>(text[i++]);
        cp = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
        return true;
    }
    if ((c0 & 0xF0) == 0xE0 && (i + 1) < text.length())
    {
        const uint8_t c1 = static_cast<uint8_t>(text[i++]);
        const uint8_t c2 = static_cast<uint8_t>(text[i++]);
        cp = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        return true;
    }
    cp = '?';
    return true;
}

static ViGlyphTiny mapVietnameseTiny(uint32_t cp)
{
    ViGlyphTiny g = {'?', VI_TONE_NONE};
    if (cp < 128)
    {
        g.base = static_cast<char>(cp);
        return g;
    }

    switch (cp)
    {
    case 0x0110: g = {'D', VI_SHAPE_DBAR}; break; // Đ
    case 0x0111: g = {'d', VI_SHAPE_DBAR}; break; // đ

    case 0x00C0: g = {'A', VI_TONE_GRAVE}; break; // À
    case 0x00C1: g = {'A', VI_TONE_ACUTE}; break; // Á
    case 0x1EA2: g = {'A', VI_TONE_HOOK}; break;  // Ả
    case 0x00C3: g = {'A', VI_TONE_TILDE}; break; // Ã
    case 0x1EA0: g = {'A', VI_TONE_DOT}; break;   // Ạ
    case 0x0102: g = {'A', VI_SHAPE_BREVE}; break; // Ă
    case 0x1EAE: g = {'A', VI_SHAPE_BREVE | VI_TONE_ACUTE}; break; // Ắ
    case 0x1EB0: g = {'A', VI_SHAPE_BREVE | VI_TONE_GRAVE}; break; // Ằ
    case 0x1EB2: g = {'A', VI_SHAPE_BREVE | VI_TONE_HOOK}; break;  // Ẳ
    case 0x1EB4: g = {'A', VI_SHAPE_BREVE | VI_TONE_TILDE}; break; // Ẵ
    case 0x1EB6: g = {'A', VI_SHAPE_BREVE | VI_TONE_DOT}; break;   // Ặ
    case 0x00C2: g = {'A', VI_SHAPE_CIRC}; break; // Â
    case 0x1EA4: g = {'A', VI_SHAPE_CIRC | VI_TONE_ACUTE}; break; // Ấ
    case 0x1EA6: g = {'A', VI_SHAPE_CIRC | VI_TONE_GRAVE}; break; // Ầ
    case 0x1EA8: g = {'A', VI_SHAPE_CIRC | VI_TONE_HOOK}; break;  // Ẩ
    case 0x1EAA: g = {'A', VI_SHAPE_CIRC | VI_TONE_TILDE}; break; // Ẫ
    case 0x1EAC: g = {'A', VI_SHAPE_CIRC | VI_TONE_DOT}; break;   // Ậ

    case 0x00C8: g = {'E', VI_TONE_GRAVE}; break; // È
    case 0x00C9: g = {'E', VI_TONE_ACUTE}; break; // É
    case 0x1EBA: g = {'E', VI_TONE_HOOK}; break;  // Ẻ
    case 0x1EBC: g = {'E', VI_TONE_TILDE}; break; // Ẽ
    case 0x1EB8: g = {'E', VI_TONE_DOT}; break;   // Ẹ
    case 0x00CA: g = {'E', VI_SHAPE_CIRC}; break; // Ê
    case 0x1EBE: g = {'E', VI_SHAPE_CIRC | VI_TONE_ACUTE}; break; // Ế
    case 0x1EC0: g = {'E', VI_SHAPE_CIRC | VI_TONE_GRAVE}; break; // Ề
    case 0x1EC2: g = {'E', VI_SHAPE_CIRC | VI_TONE_HOOK}; break;  // Ể
    case 0x1EC4: g = {'E', VI_SHAPE_CIRC | VI_TONE_TILDE}; break; // Ễ
    case 0x1EC6: g = {'E', VI_SHAPE_CIRC | VI_TONE_DOT}; break;   // Ệ

    case 0x00CC: g = {'I', VI_TONE_GRAVE}; break; // Ì
    case 0x00CD: g = {'I', VI_TONE_ACUTE}; break; // Í
    case 0x1EC8: g = {'I', VI_TONE_HOOK}; break;  // Ỉ
    case 0x0128: g = {'I', VI_TONE_TILDE}; break; // Ĩ
    case 0x1ECA: g = {'I', VI_TONE_DOT}; break;   // Ị

    case 0x00D2: g = {'O', VI_TONE_GRAVE}; break; // Ò
    case 0x00D3: g = {'O', VI_TONE_ACUTE}; break; // Ó
    case 0x1ECE: g = {'O', VI_TONE_HOOK}; break;  // Ỏ
    case 0x00D5: g = {'O', VI_TONE_TILDE}; break; // Õ
    case 0x1ECC: g = {'O', VI_TONE_DOT}; break;   // Ọ
    case 0x00D4: g = {'O', VI_SHAPE_CIRC}; break; // Ô
    case 0x1ED0: g = {'O', VI_SHAPE_CIRC | VI_TONE_ACUTE}; break; // Ố
    case 0x1ED2: g = {'O', VI_SHAPE_CIRC | VI_TONE_GRAVE}; break; // Ồ
    case 0x1ED4: g = {'O', VI_SHAPE_CIRC | VI_TONE_HOOK}; break;  // Ổ
    case 0x1ED6: g = {'O', VI_SHAPE_CIRC | VI_TONE_TILDE}; break; // Ỗ
    case 0x1ED8: g = {'O', VI_SHAPE_CIRC | VI_TONE_DOT}; break;   // Ộ
    case 0x01A0: g = {'O', VI_SHAPE_HORN}; break; // Ơ
    case 0x1EDA: g = {'O', VI_SHAPE_HORN | VI_TONE_ACUTE}; break; // Ớ
    case 0x1EDC: g = {'O', VI_SHAPE_HORN | VI_TONE_GRAVE}; break; // Ờ
    case 0x1EDE: g = {'O', VI_SHAPE_HORN | VI_TONE_HOOK}; break;  // Ở
    case 0x1EE0: g = {'O', VI_SHAPE_HORN | VI_TONE_TILDE}; break; // Ỡ
    case 0x1EE2: g = {'O', VI_SHAPE_HORN | VI_TONE_DOT}; break;   // Ợ

    case 0x00D9: g = {'U', VI_TONE_GRAVE}; break; // Ù
    case 0x00DA: g = {'U', VI_TONE_ACUTE}; break; // Ú
    case 0x1EE6: g = {'U', VI_TONE_HOOK}; break;  // Ủ
    case 0x0168: g = {'U', VI_TONE_TILDE}; break; // Ũ
    case 0x1EE4: g = {'U', VI_TONE_DOT}; break;   // Ụ
    case 0x01AF: g = {'U', VI_SHAPE_HORN}; break; // Ư
    case 0x1EE8: g = {'U', VI_SHAPE_HORN | VI_TONE_ACUTE}; break; // Ứ
    case 0x1EEA: g = {'U', VI_SHAPE_HORN | VI_TONE_GRAVE}; break; // Ừ
    case 0x1EEC: g = {'U', VI_SHAPE_HORN | VI_TONE_HOOK}; break;  // Ử
    case 0x1EEE: g = {'U', VI_SHAPE_HORN | VI_TONE_TILDE}; break; // Ữ
    case 0x1EF0: g = {'U', VI_SHAPE_HORN | VI_TONE_DOT}; break;   // Ự

    case 0x00E0: g = {'a', VI_TONE_GRAVE}; break; // à
    case 0x00E1: g = {'a', VI_TONE_ACUTE}; break; // á
    case 0x1EA3: g = {'a', VI_TONE_HOOK}; break;  // ả
    case 0x00E3: g = {'a', VI_TONE_TILDE}; break; // ã
    case 0x1EA1: g = {'a', VI_TONE_DOT}; break;   // ạ
    case 0x0103: g = {'a', VI_SHAPE_BREVE}; break; // ă
    case 0x1EAF: g = {'a', VI_SHAPE_BREVE | VI_TONE_ACUTE}; break; // ắ
    case 0x1EB1: g = {'a', VI_SHAPE_BREVE | VI_TONE_GRAVE}; break; // ằ
    case 0x1EB3: g = {'a', VI_SHAPE_BREVE | VI_TONE_HOOK}; break;  // ẳ
    case 0x1EB5: g = {'a', VI_SHAPE_BREVE | VI_TONE_TILDE}; break; // ẵ
    case 0x1EB7: g = {'a', VI_SHAPE_BREVE | VI_TONE_DOT}; break;   // ặ
    case 0x00E2: g = {'a', VI_SHAPE_CIRC}; break; // â
    case 0x1EA5: g = {'a', VI_SHAPE_CIRC | VI_TONE_ACUTE}; break; // ấ
    case 0x1EA7: g = {'a', VI_SHAPE_CIRC | VI_TONE_GRAVE}; break; // ầ
    case 0x1EA9: g = {'a', VI_SHAPE_CIRC | VI_TONE_HOOK}; break;  // ẩ
    case 0x1EAB: g = {'a', VI_SHAPE_CIRC | VI_TONE_TILDE}; break; // ẫ
    case 0x1EAD: g = {'a', VI_SHAPE_CIRC | VI_TONE_DOT}; break;   // ậ

    case 0x00E8: g = {'e', VI_TONE_GRAVE}; break; // è
    case 0x00E9: g = {'e', VI_TONE_ACUTE}; break; // é
    case 0x1EBB: g = {'e', VI_TONE_HOOK}; break;  // ẻ
    case 0x1EBD: g = {'e', VI_TONE_TILDE}; break; // ẽ
    case 0x1EB9: g = {'e', VI_TONE_DOT}; break;   // ẹ
    case 0x00EA: g = {'e', VI_SHAPE_CIRC}; break; // ê
    case 0x1EBF: g = {'e', VI_SHAPE_CIRC | VI_TONE_ACUTE}; break; // ế
    case 0x1EC1: g = {'e', VI_SHAPE_CIRC | VI_TONE_GRAVE}; break; // ề
    case 0x1EC3: g = {'e', VI_SHAPE_CIRC | VI_TONE_HOOK}; break;  // ể
    case 0x1EC5: g = {'e', VI_SHAPE_CIRC | VI_TONE_TILDE}; break; // ễ
    case 0x1EC7: g = {'e', VI_SHAPE_CIRC | VI_TONE_DOT}; break;   // ệ

    case 0x00EC: g = {'i', VI_TONE_GRAVE}; break; // ì
    case 0x00ED: g = {'i', VI_TONE_ACUTE}; break; // í
    case 0x1EC9: g = {'i', VI_TONE_HOOK}; break;  // ỉ
    case 0x0129: g = {'i', VI_TONE_TILDE}; break; // ĩ
    case 0x1ECB: g = {'i', VI_TONE_DOT}; break;   // ị

    case 0x00F2: g = {'o', VI_TONE_GRAVE}; break; // ò
    case 0x00F3: g = {'o', VI_TONE_ACUTE}; break; // ó
    case 0x1ECF: g = {'o', VI_TONE_HOOK}; break;  // ỏ
    case 0x00F5: g = {'o', VI_TONE_TILDE}; break; // õ
    case 0x1ECD: g = {'o', VI_TONE_DOT}; break;   // ọ
    case 0x00F4: g = {'o', VI_SHAPE_CIRC}; break; // ô
    case 0x1ED1: g = {'o', VI_SHAPE_CIRC | VI_TONE_ACUTE}; break; // ố
    case 0x1ED3: g = {'o', VI_SHAPE_CIRC | VI_TONE_GRAVE}; break; // ồ
    case 0x1ED5: g = {'o', VI_SHAPE_CIRC | VI_TONE_HOOK}; break;  // ổ
    case 0x1ED7: g = {'o', VI_SHAPE_CIRC | VI_TONE_TILDE}; break; // ỗ
    case 0x1ED9: g = {'o', VI_SHAPE_CIRC | VI_TONE_DOT}; break;   // ộ
    case 0x01A1: g = {'o', VI_SHAPE_HORN}; break; // ơ
    case 0x1EDB: g = {'o', VI_SHAPE_HORN | VI_TONE_ACUTE}; break; // ớ
    case 0x1EDD: g = {'o', VI_SHAPE_HORN | VI_TONE_GRAVE}; break; // ờ
    case 0x1EDF: g = {'o', VI_SHAPE_HORN | VI_TONE_HOOK}; break;  // ở
    case 0x1EE1: g = {'o', VI_SHAPE_HORN | VI_TONE_TILDE}; break; // ỡ
    case 0x1EE3: g = {'o', VI_SHAPE_HORN | VI_TONE_DOT}; break;   // ợ

    case 0x00F9: g = {'u', VI_TONE_GRAVE}; break; // ù
    case 0x00FA: g = {'u', VI_TONE_ACUTE}; break; // ú
    case 0x1EE7: g = {'u', VI_TONE_HOOK}; break;  // ủ
    case 0x0169: g = {'u', VI_TONE_TILDE}; break; // ũ
    case 0x1EE5: g = {'u', VI_TONE_DOT}; break;   // ụ
    case 0x01B0: g = {'u', VI_SHAPE_HORN}; break; // ư
    case 0x1EE9: g = {'u', VI_SHAPE_HORN | VI_TONE_ACUTE}; break; // ứ
    case 0x1EEB: g = {'u', VI_SHAPE_HORN | VI_TONE_GRAVE}; break; // ừ
    case 0x1EED: g = {'u', VI_SHAPE_HORN | VI_TONE_HOOK}; break;  // ử
    case 0x1EEF: g = {'u', VI_SHAPE_HORN | VI_TONE_TILDE}; break; // ữ
    case 0x1EF1: g = {'u', VI_SHAPE_HORN | VI_TONE_DOT}; break;   // ự
    default: break;
    }
    return g;
}

static void drawTinyVietnameseMarks(int x, int yTop, uint16_t marks, uint16_t color)
{
    if (marks & VI_SHAPE_CIRC)
    {
        dma_display->drawPixel(x + 1, yTop - 1, color);
        dma_display->drawPixel(x + 2, yTop - 2, color);
        dma_display->drawPixel(x + 3, yTop - 1, color);
    }
    if (marks & VI_SHAPE_BREVE)
    {
        dma_display->drawPixel(x + 1, yTop - 2, color);
        dma_display->drawPixel(x + 2, yTop - 1, color);
        dma_display->drawPixel(x + 3, yTop - 2, color);
    }
    if (marks & VI_SHAPE_HORN)
    {
        dma_display->drawPixel(x + 4, yTop, color);
        dma_display->drawPixel(x + 5, yTop - 1, color);
    }
    if (marks & VI_TONE_ACUTE)
    {
        dma_display->drawPixel(x + 3, yTop - 2, color);
        dma_display->drawPixel(x + 4, yTop - 3, color);
    }
    if (marks & VI_TONE_GRAVE)
    {
        dma_display->drawPixel(x + 1, yTop - 3, color);
        dma_display->drawPixel(x + 2, yTop - 2, color);
    }
    if (marks & VI_TONE_HOOK)
    {
        dma_display->drawPixel(x + 2, yTop - 3, color);
        dma_display->drawPixel(x + 3, yTop - 3, color);
        dma_display->drawPixel(x + 3, yTop - 2, color);
    }
    if (marks & VI_TONE_TILDE)
    {
        dma_display->drawPixel(x + 1, yTop - 3, color);
        dma_display->drawPixel(x + 2, yTop - 2, color);
        dma_display->drawPixel(x + 3, yTop - 3, color);
        dma_display->drawPixel(x + 4, yTop - 2, color);
    }
    if (marks & VI_TONE_DOT)
    {
        dma_display->drawPixel(x + 2, yTop + 7, color);
    }
    if (marks & VI_SHAPE_DBAR)
    {
        dma_display->drawPixel(x + 1, yTop + 3, color);
        dma_display->drawPixel(x + 2, yTop + 3, color);
        dma_display->drawPixel(x + 3, yTop + 3, color);
    }
}

static int drawTinyVietnameseText(int x, int yTop, const String &text, uint16_t color)
{
    dma_display->setFont(nullptr);
    dma_display->setTextWrap(false);
    int cursorX = x;
    int i = 0;
    while (i < text.length())
    {
        uint32_t cp = '?';
        if (!decodeUtf8Tiny(text, i, cp))
            break;
        ViGlyphTiny glyph = mapVietnameseTiny(cp);
        dma_display->drawChar(cursorX, yTop, static_cast<unsigned char>(glyph.base), color, myBLACK, 1);
        if (glyph.marks != VI_TONE_NONE)
            drawTinyVietnameseMarks(cursorX, yTop, glyph.marks, color);
        cursorX += 6;
    }
    return cursorX - x;
}

static uint16_t tinyVietnameseTextWidth(const String &text)
{
    int count = 0;
    int i = 0;
    while (i < text.length())
    {
        uint32_t cp = '?';
        if (!decodeUtf8Tiny(text, i, cp))
            break;
        count++;
    }
    return static_cast<uint16_t>(max(1, count * 6));
}

static uint16_t lunarUtf8TextWidth(const String &text)
{
    setLunarLuckUtf8Font();
    if (lunarLuckUtf8Ready)
    {
        int w = static_cast<int>(lunarLuckUtf8.getUTF8Width(text.c_str()));
        return static_cast<uint16_t>(max(1, w));
    }
    return tinyVietnameseTextWidth(text);
}

static void renderLunarLines(const String lines[3], const uint16_t widths[3], const int offsets[3])
{
    dma_display->fillScreen(0);

    uint16_t lineColor;
    if (theme == 1)
    {
        lineColor = dma_display->color565(200, 200, 255);
    }
    else
    {
        lineColor = INFOMODAL_EDIT;
    }

    // Lunar Date subpage now shows only the merged detail line (line 3),
    // centered vertically, rendered with Unicode UTF-8 font.
    setLunarLuckUtf8Font();
    int w = widths[2];
    if (w > 0)
    {
        const int baseX = PANEL_RES_X - offsets[2];
        int baselineY = PANEL_RES_Y / 2;
        if (lunarLuckUtf8Ready)
        {
            const int ascent = static_cast<int>(lunarLuckUtf8.getFontAscent());
            const int descent = static_cast<int>(lunarLuckUtf8.getFontDescent());
            const int textHeight = ascent - descent;
            const int topY = (PANEL_RES_Y - textHeight) / 2;
            baselineY = topY + ascent;
            if (baselineY < 0)
                baselineY = 0;
        }
        lunarLuckUtf8.setForegroundColor(lineColor);
        lunarLuckUtf8.drawUTF8(baseX, baselineY, lines[2].c_str());
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
    String viDetail = String("Ng\xC3\xA0y ") + ld.day + "/" + ld.month +
                      " N\xC4\x83m " + stemBranchVi;

    // English year line: "The year of Wood Snake"
    String enDetail = String("The year of ") + yearEn;

    // Hour detail: e.g. "Gio Hoi  / 11:24 PM"
    String lunarHour = formatLunarHourTag();
    String clockTag = formatLunarClockTag();
    String hourDetail = lunarHour + "  / " + clockTag;

    // Line 3: combined marquee
    // Ngay 28/10 Nam At Ty * The year of Wood Snake Â¦ Gio Hoi / 11:24 PM
    lunarLines[2] = viDetail + " * " + enDetail + "  \xC2\xA6  " + hourDetail;

    lunarWidths[0] = 0;
    lunarWidths[1] = 0;
    lunarWidths[2] = lunarUtf8TextWidth(lunarLines[2]);
    if (lunarWidths[2] <= 0)
        lunarWidths[2] = 1;
    for (int i = 0; i < 3; ++i)
        lunarOffsets[i] = 0;
    lastLunarTick = millis();
    lunarInitialized = true;
}

static XuatHanhInfo calcXuatHanhKhongMinh(int lunarMonth, int lunarDay)
{
    if (lunarMonth < 1)
        lunarMonth = 1;
    if (lunarDay < 1)
        lunarDay = 1;

    // Group 1: months 1,4,7,10 (6-day cycle)
    if (lunarMonth == 1 || lunarMonth == 4 || lunarMonth == 7 || lunarMonth == 10)
    {
        const int m = lunarDay % 6;
        switch (m)
        {
        case 1: return {"Đường Phong", TONE_TOT};
        case 2: return {"Kim Thổ", TONE_XAU};
        case 3: return {"Kim Dương", TONE_TOT};
        case 4: return {"Thuần Dương", TONE_BINH};
        case 5: return {"Đạo Tặc", TONE_XAU};
        case 0:
        default: return {"Hảo Thương", TONE_TOT};
        }
    }

    // Group 2: months 2,5,8,11 (8-day cycle)
    if (lunarMonth == 2 || lunarMonth == 5 || lunarMonth == 8 || lunarMonth == 11)
    {
        const int m = lunarDay % 8;
        switch (m)
        {
        case 0: return {"Thiên Đạo", TONE_XAU};
        case 1: return {"Thiên Môn", TONE_TOT};
        case 2: return {"Thiên Đường", TONE_TOT};
        case 3: return {"Thiên Tài", TONE_TOT};
        case 4: return {"Thiên Tặc", TONE_XAU};
        case 5: return {"Thiên Dương", TONE_BINH};
        case 6: return {"Thiên Hầu", TONE_XAU};
        case 7:
        default: return {"Thiên Thương", TONE_BINH};
        }
    }

    // Group 3: months 3,6,9,12 (8-day cycle)
    const int m = lunarDay % 8;
    switch (m)
    {
    case 0: return {"Chu Tước", TONE_XAU};
    case 1: return {"Bạch Hổ Đầu", TONE_TOT};
    case 2: return {"Bạch Hổ Kiếp", TONE_BINH};
    case 3: return {"Bạch Hổ Túc", TONE_XAU};
    case 4: return {"Huyền Vũ", TONE_XAU};
    case 5: return {"Thanh Long Đầu", TONE_TOT};
    case 6: return {"Thanh Long Kiếp", TONE_BINH};
    case 7:
    default: return {"Thanh Long Túc", TONE_XAU};
    }
}

static int computeLunarLuckScore(const LunarDate &ld)
{
    int score = 0;

    const int luckyDays[] = {1, 6, 8, 11, 15, 18, 24, 28};
    const int cautiousDays[] = {3, 5, 7, 13, 14, 22, 23, 27, 29};

    for (int day : luckyDays)
    {
        if (ld.day == day)
        {
            score += 2;
            break;
        }
    }
    for (int day : cautiousDays)
    {
        if (ld.day == day)
        {
            score -= 2;
            break;
        }
    }

    if (ld.month == 1 || ld.month == 6 || ld.month == 8 || ld.month == 12)
        score += 1;
    if (ld.month == 4 || ld.month == 7 || ld.month == 10)
        score -= 1;

    if (ld.leap)
        score -= 1;

    if ((ld.day % 2) == 0)
        score += 1;
    else
        score -= 1;

    return score;
}

bool isTamNuongDay(int lunarDay)
{
    return (lunarDay == 3 || lunarDay == 7 || lunarDay == 13 ||
            lunarDay == 18 || lunarDay == 22 || lunarDay == 27);
}

bool isNguyetKyDay(int lunarDay)
{
    return (lunarDay == 5 || lunarDay == 14 || lunarDay == 23);
}

static void appendAdviceClause(String &target, const String &clause)
{
    if (clause.length() == 0)
        return;
    if (target.length() > 0)
        target += "; ";
    target += clause;
}

static String elementToVietnamese(const String &elementAscii)
{
    if (elementAscii == "Moc")
        return "Mộc";
    if (elementAscii == "Hoa")
        return "Hỏa";
    if (elementAscii == "Tho")
        return "Thổ";
    if (elementAscii == "Kim")
        return "Kim";
    if (elementAscii == "Thuy")
        return "Thủy";
    return elementAscii;
}

static LunarDayDetail buildLunarDayDetail(int dd, int mm, int yy)
{
    static const char *stems[10] = {"Giáp", "Ất", "Bính", "Đinh", "Mậu", "Kỷ", "Canh", "Tân", "Nhâm", "Quý"};
    static const char *branches[12] = {"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ", "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"};
    static const char *elementsByStem[10] = {"Moc", "Moc", "Hoa", "Hoa", "Tho", "Tho", "Kim", "Kim", "Thuy", "Thuy"};

    long jd = jdFromDate(dd, mm, yy);
    int stemIndex = static_cast<int>((jd + 9) % 10);
    int branchIndex = static_cast<int>((jd + 1) % 12);
    if (stemIndex < 0)
        stemIndex += 10;
    if (branchIndex < 0)
        branchIndex += 12;

    LunarDayDetail out;
    out.stem = String(stems[stemIndex]);
    out.branch = String(branches[branchIndex]);
    out.stemBranch = out.stem + " " + out.branch;
    out.element = String(elementsByStem[stemIndex]);
    out.branchIndex = branchIndex;
    return out;
}

void zodiacCompatByBranch(int branchIndex, String &hop1, String &hop2, String &ky)
{
    static const char *hopA[12] = {
        "Thân",  // Tý
        "Tý",    // Sửu
        "Ngọ",   // Dần
        "Hợi",   // Mão
        "Tý",    // Thìn
        "Dậu",   // Tỵ
        "Dần",   // Ngọ
        "Hợi",   // Mùi
        "Tý",    // Thân
        "Tý",    // Dậu
        "Ngọ",   // Tuất
        "Mão"    // Hợi
    };

    static const char *hopB[12] = {
        "Thìn",  // Tý
        "Dậu",   // Sửu
        "Tuất",  // Dần
        "Mùi",   // Mão
        "Thân",  // Thìn
        "Sửu",   // Tỵ
        "Tuất",  // Ngọ
        "Mão",   // Mùi
        "Thìn",  // Thân
        "Sửu",   // Dậu
        "Dần",   // Tuất
        "Mùi"    // Hợi
    };

    static const char *kyArr[12] = {
        "Ngọ",   // Tý
        "Mùi",   // Sửu
        "Thân",  // Dần
        "Dậu",   // Mão
        "Tuất",  // Thìn
        "Hợi",   // Tỵ
        "Tý",    // Ngọ
        "Sửu",   // Mùi
        "Dần",   // Thân
        "Mão",   // Dậu
        "Thìn",  // Tuất
        "Tỵ"     // Hợi
    };

    if (branchIndex < 0 || branchIndex > 11)
        branchIndex = 0;

    hop1 = String(hopA[branchIndex]);
    hop2 = String(hopB[branchIndex]);
    ky   = String(kyArr[branchIndex]);
}

static String buildHoangDaoHoursTag(int dayBranchIndex)
{
    static const char *chiLabels[12] = {
        "Giờ Tý (23:00-01:00)",
        "Giờ Sửu (01:00-03:00)",
        "Giờ Dần (03:00-05:00)",
        "Giờ Mão (05:00-07:00)",
        "Giờ Thìn (07:00-09:00)",
        "Giờ Tỵ (09:00-11:00)",
        "Giờ Ngọ (11:00-13:00)",
        "Giờ Mùi (13:00-15:00)",
        "Giờ Thân (15:00-17:00)",
        "Giờ Dậu (17:00-19:00)",
        "Giờ Tuất (19:00-21:00)",
        "Giờ Hợi (21:00-23:00)"};

    // Opposite day branches share the same set of auspicious hours.
    static const int setA[6] = {0, 1, 3, 6, 8, 9};
    static const int setB[6] = {2, 3, 5, 8, 10, 11};
    static const int setC[6] = {0, 1, 4, 5, 7, 10};
    static const int setD[6] = {2, 3, 6, 7, 9, 0};
    static const int setE[6] = {4, 5, 8, 9, 11, 2};
    static const int setF[6] = {6, 7, 10, 11, 1, 4};

    if (dayBranchIndex < 0 || dayBranchIndex > 11)
        dayBranchIndex = 0;

    const int *selected = setA;
    switch (dayBranchIndex)
    {
    case 0:
    case 6:
        selected = setA;
        break;
    case 1:
    case 7:
        selected = setB;
        break;
    case 2:
    case 8:
        selected = setC;
        break;
    case 3:
    case 9:
        selected = setD;
        break;
    case 4:
    case 10:
        selected = setE;
        break;
    case 5:
    case 11:
    default:
        selected = setF;
        break;
    }

    String out;
    for (int i = 0; i < 6; ++i)
    {
        if (i > 0)
            out += ", ";
        out += chiLabels[selected[i]];
    }
    return out;
}

static uint32_t adviceSeed(
    int score, int lunarDay, int lunarMonth, bool lunarLeap,
    const LunarDayDetail &dayInfo)
{
  // Deterministic seed (no random(), no RTC needed)
  // Branch + element influence: lightweight rolling hash
  uint32_t h = 2166136261u;
  auto mix = [&](uint8_t v) { h ^= v; h *= 16777619u; };

  mix((uint8_t)(score + 10));
  mix((uint8_t)lunarDay);
  mix((uint8_t)lunarMonth);
  mix((uint8_t)(lunarLeap ? 1 : 0));

  // Mix a few bytes from strings (safe + fast)
  for (int i = 0; i < dayInfo.branch.length(); i++) mix((uint8_t)dayInfo.branch[i]);
  for (int i = 0; i < dayInfo.element.length(); i++) mix((uint8_t)dayInfo.element[i]);

  return h;
}

static String pickOneProgmem(const char *const *arr, int n, uint32_t seed, uint32_t salt)
{
  if (!arr || n <= 0) return String();
  uint32_t idx = (seed ^ (salt * 2654435761u)) % (uint32_t)n;
  char phrase[256];
  readProgmemString(arr, static_cast<uint16_t>(idx), phrase, sizeof(phrase));
  return String(phrase);
}


// More natural Vietnamese phrasing + consistent style.
// Keeps your logic structure, only improves wording + avoids mixing commas/semicolons.
// Assumes appendAdviceClause(out, "phrase") adds "; " separator safely.
static void buildLunarActionAdvice(
    int score,
    int lunarDay,
    int lunarMonth,
    bool lunarLeap,
    const LunarDayDetail &dayInfo,
    String &nenLam,
    String &nenTranh)
{
    using namespace wxv::lunar_luck_nen_tranh;
    const bool tamNuong = isTamNuongDay(lunarDay);
    const bool nguyetKy = isNguyetKyDay(lunarDay);

    // 1) Base advice by score + element (more variety, no repeating)
    const uint32_t seed = adviceSeed(score, lunarDay, lunarMonth, lunarLeap, dayInfo);

    if (score >= 2)
    {
        if (dayInfo.element == "Moc")
            nenLam = pickOneProgmem(kNenLamMoc, (int)(sizeof(kNenLamMoc) / sizeof(kNenLamMoc[0])), seed, 11);
        else if (dayInfo.element == "Kim")
            nenLam = pickOneProgmem(kNenLamKim, (int)(sizeof(kNenLamKim) / sizeof(kNenLamKim[0])), seed, 12);
        else if (dayInfo.element == "Thuy")
            nenLam = pickOneProgmem(kNenLamThuy, (int)(sizeof(kNenLamThuy) / sizeof(kNenLamThuy[0])), seed, 13);
        else if (dayInfo.element == "Hoa")
            nenLam = pickOneProgmem(kNenLamHoa, (int)(sizeof(kNenLamHoa) / sizeof(kNenLamHoa[0])), seed, 14);
        else
            nenLam = pickOneProgmem(kNenLamTho, (int)(sizeof(kNenLamTho) / sizeof(kNenLamTho[0])), seed, 15);

        nenTranh = pickOneProgmem(kNenTranhHigh, (int)(sizeof(kNenTranhHigh) / sizeof(kNenTranhHigh[0])), seed, 21);
    }
    else if (score <= -1)
    {
        nenLam = pickOneProgmem(kNenLamLow, (int)(sizeof(kNenLamLow) / sizeof(kNenLamLow[0])), seed, 31);

        if (dayInfo.element == "Kim" || dayInfo.element == "Hoa")
            nenTranh = pickOneProgmem(kNenTranhLowKimHoa, (int)(sizeof(kNenTranhLowKimHoa) / sizeof(kNenTranhLowKimHoa[0])), seed, 32);
        else
            nenTranh = pickOneProgmem(kNenTranhLowOther, (int)(sizeof(kNenTranhLowOther) / sizeof(kNenTranhLowOther[0])), seed, 33);
    }
    else
    {
        nenLam = pickOneProgmem(kNenLamMid, (int)(sizeof(kNenLamMid) / sizeof(kNenLamMid[0])), seed, 41);
        nenTranh = pickOneProgmem(kNenTranhMid, (int)(sizeof(kNenTranhMid) / sizeof(kNenTranhMid[0])), seed, 42);
    }

    // 2) Lunar-cycle: early/mid/late month (phrases feel like guidance, not technical)
    if (lunarDay <= 10)
    {
        appendAdviceClause(nenLam, "hợp bắt đầu gọn gàng, đặt mục tiêu ngắn hạn");
        appendAdviceClause(nenTranh, "tránh ôm quá nhiều việc ngay đầu tháng âm lịch");
    }
    else if (lunarDay <= 20)
    {
        appendAdviceClause(nenLam, "hợp đẩy tiến độ, chốt các hạng mục đang chạy");
        appendAdviceClause(nenTranh, "tránh đổi mục tiêu liên tục khi việc đã vào nhịp");
    }
    else
    {
        appendAdviceClause(nenLam, "hợp tổng kết, nghiệm thu, dọn việc tồn");
        appendAdviceClause(nenTranh, "tránh mở thêm việc lớn sát cuối tháng âm lịch");
    }

    // 3) Seasonal-ish month buckets (keep your mapping, improve language)
    if (lunarMonth == 1 || lunarMonth == 2 || lunarMonth == 3)
    {
        appendAdviceClause(nenLam, "hợp mở rộng quan hệ, học kỹ năng mới");
    }
    else if (lunarMonth == 4 || lunarMonth == 5 || lunarMonth == 6)
    {
        appendAdviceClause(nenLam, "hợp chuẩn hóa quy trình, siết kế hoạch tài chính");
        appendAdviceClause(nenTranh, "tránh chi tiêu theo hứng");
    }
    else if (lunarMonth == 7 || lunarMonth == 8 || lunarMonth == 9)
    {
        appendAdviceClause(nenLam, "hợp rà soát rủi ro, củng cố phương án dự phòng");
        appendAdviceClause(nenTranh, "tránh quyết vội khi thiếu dữ liệu");
    }
    else
    {
        appendAdviceClause(nenLam, "hợp chốt kế hoạch dài hạn, gia cố việc nền tảng");
        appendAdviceClause(nenTranh, "tránh thay đổi lớn vào phút cuối");
    }

    // 4) Leap month nuance
    if (lunarLeap)
    {
        appendAdviceClause(nenLam, "ưu tiên kiểm chứng thông tin, rà soát điều khoản");
        appendAdviceClause(nenTranh, "hạn chế cam kết dài hạn khi chưa thật cần");
    }

    // 5) Folk flags
    if (tamNuong)
    {
        appendAdviceClause(nenLam, "ưu tiên việc nội bộ, kiểm tra và chỉnh sửa");
        appendAdviceClause(nenTranh, "hạn chế khai trương, cưới hỏi, động thổ, ký hợp đồng lớn");
    }
    if (nguyetKy)
    {
        appendAdviceClause(nenLam, "dành thời gian rà soát kế hoạch, quản trị rủi ro");
        appendAdviceClause(nenTranh, "tránh đầu tư lớn, đi xa, và tranh chấp");
    }

    // 6) Branch tuning (index-based to avoid UTF-8 compare mismatch)
    int bi = dayInfo.branchIndex;
    if (bi < 0 || bi > 11)
        bi = 0;

    switch (bi)
    {
    case 0:  // Tý
    case 11: // Hợi
        appendAdviceClause(nenLam, "hợp học tập, nghiên cứu, lập kế hoạch");
        appendAdviceClause(nenTranh, "tránh quyết định nóng theo cảm xúc");
        break;

    case 2: // Dần
    case 3: // Mão
        appendAdviceClause(nenLam, "hợp gặp gỡ, trao đổi ý tưởng");
        appendAdviceClause(nenTranh, "tránh tranh luận căng thẳng");
        break;

    case 4: // Thìn
    case 5: // Tỵ
        appendAdviceClause(nenLam, "hợp xử lý hồ sơ, chuẩn hóa quy trình");
        appendAdviceClause(nenTranh, "tránh ôm đồm nhiều việc cùng lúc");
        break;

    case 6: // Ngọ
    case 7: // Mùi
        appendAdviceClause(nenLam, "hợp chăm sóc gia đình, củng cố hậu cần");
        appendAdviceClause(nenTranh, "tránh chi tiêu theo hứng");
        break;

    case 8: // Thân
    case 9: // Dậu
        appendAdviceClause(nenLam, "hợp chốt việc ngắn hạn, dọn backlog");
        appendAdviceClause(nenTranh, "tránh ký cam kết dài hạn khi chưa đủ dữ liệu");
        break;

    case 1:  // Sửu
    case 10: // Tuất
        appendAdviceClause(nenLam, "hợp việc bền vững, gia cố kế hoạch dài hạn");
        appendAdviceClause(nenTranh, "tránh thay đổi lớn vào phút cuối");
        break;

    default:
        break;
    }
}
static void buildHoangDaoHoursCompact(int dayBranchIndex, char *dst, size_t cap)
{
    static const char *chiNames[12] = {
        "Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ",
        "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"};
    static const char *hourRanges[12] = {
        "23-01", "01-03", "03-05", "05-07", "07-09", "09-11",
        "11-13", "13-15", "15-17", "17-19", "19-21", "21-23"};

    static const int setA[6] = {0, 1, 3, 6, 8, 9};
    static const int setB[6] = {2, 3, 5, 8, 10, 11};
    static const int setC[6] = {0, 1, 4, 5, 7, 10};
    static const int setD[6] = {2, 3, 6, 7, 9, 0};
    static const int setE[6] = {4, 5, 8, 9, 11, 2};
    static const int setF[6] = {6, 7, 10, 11, 1, 4};

    if (!dst || cap == 0)
        return;
    dst[0] = '\0';

    if (dayBranchIndex < 0 || dayBranchIndex > 11)
        dayBranchIndex = 0;

    const int *selected = setA;
    switch (dayBranchIndex)
    {
    case 0:
    case 6:
        selected = setA;
        break;
    case 1:
    case 7:
        selected = setB;
        break;
    case 2:
    case 8:
        selected = setC;
        break;
    case 3:
    case 9:
        selected = setD;
        break;
    case 4:
    case 10:
        selected = setE;
        break;
    case 5:
    case 11:
    default:
        selected = setF;
        break;
    }

    for (int i = 0; i < 6; ++i)
    {
        if (i > 0)
            safeAppend(dst, cap, SEP);
        const int idx = selected[i];
        safeAppend(dst, cap, chiNames[idx]);
        safeAppend(dst, cap, " ");
        safeAppend(dst, cap, hourRanges[idx]);
    }
}

void setSectionStartState()
{
    if (g_sectionCount == 0)
        return;
    marqueeActive = g_sections[currentSectionIndex].marquee;
    marqueeOffsetPx = PANEL_RES_X;
    sectionStartMs = millis();
    lastScrollMs = sectionStartMs;
}

static void startHeaderDoor(const char *oldTitle, const char *newTitle)
{
    if (!oldTitle)
        oldTitle = "";
    if (!newTitle)
        newTitle = "";
    strncpy(hdrOld, oldTitle, TITLE_MAX - 1);
    hdrOld[TITLE_MAX - 1] = '\0';
    strncpy(hdrNew, newTitle, TITLE_MAX - 1);
    hdrNew[TITLE_MAX - 1] = '\0';
    hdrState = HDR_DOOR;
    hdrAnimStartMs = 0;
    hdrLastFrameMs = 0;
    hdrDoorPx = 0;
    hdrDrawNew = false;
    hdrDelayActive = true;
    hdrDelayStartMs = millis();

    if (!autoBrightness)
    {
        if (hdrBrightnessPulsed)
        {
            setPanelBrightness(hdrBrightnessSaved);
            hdrBrightnessPulsed = false;
        }
        hdrBrightnessSaved = (currentPanelBrightness > 0)
                                 ? currentPanelBrightness
                                 : static_cast<uint8_t>(map(brightness, 1, 100, 3, 255));
        const uint8_t dimmed = (hdrBrightnessSaved > HDR_PULSE_DELTA)
                                   ? static_cast<uint8_t>(hdrBrightnessSaved - HDR_PULSE_DELTA)
                                   : 1;
        setPanelBrightness(dimmed);
        hdrBrightnessPulsed = true;
    }
}

static void updateHeaderAnim()
{
    if (hdrState != HDR_DOOR)
        return;

    const uint32_t now = millis();
    if (hdrDelayActive)
    {
        if (now - hdrDelayStartMs < HDR_START_DELAY_MS)
            return;
        hdrDelayActive = false;
        hdrAnimStartMs = now;
        hdrLastFrameMs = 0;
    }

    if (hdrLastFrameMs != 0 && (now - hdrLastFrameMs < HDR_FRAME_MS))
        return;
    hdrLastFrameMs = now;

    const uint32_t elapsed = now - hdrAnimStartMs;
    const int16_t halfW = PANEL_RES_X / 2;
    if (elapsed >= HDR_ANIM_MS)
    {
        hdrState = HDR_IDLE;
        hdrDoorPx = halfW;
        hdrDrawNew = true;
        if (hdrBrightnessPulsed)
        {
            setPanelBrightness(hdrBrightnessSaved);
            hdrBrightnessPulsed = false;
        }
        return;
    }
    float t = static_cast<float>(elapsed) / static_cast<float>(HDR_ANIM_MS);
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    const float te = easeInOutCubic(t);
    hdrDoorPx = static_cast<int16_t>(te * static_cast<float>(halfW));
    hdrDrawNew = (t >= 0.5f);
}

static void renderHeaderLine1(const char *currentTitle, uint16_t headerFgColor, uint16_t headerBgColor, uint16_t headerDividerColor)
{
    dma_display->fillRect(0, HEADER_Y, HEADER_W, HEADER_H, headerBgColor);
    dma_display->drawFastHLine(0, HEADER_Y + HEADER_H - 1, HEADER_W, headerDividerColor);
    updateHeaderAnim();
    const int16_t cx = PANEL_RES_X / 2;
    const int16_t textX = HEADER_PAD_X;

    if (lunarLuckUtf8Ready)
    {
        // Opaque glyph draw on header band so gaps/inner counters use headerBgColor.
        lunarLuckUtf8.setFontMode(0);
        lunarLuckUtf8.setBackgroundColor(headerBgColor);
        lunarLuckUtf8.setForegroundColor(headerFgColor);
        if (hdrState == HDR_DOOR)
        {
            const char *activeTitle = hdrDrawNew ? hdrNew : hdrOld;
            lunarLuckUtf8.drawUTF8(textX, LINE1_Y, activeTitle);
            const int16_t door = constrain(hdrDoorPx, 0, cx);
            if (!hdrReverse)
            {
                const int16_t leftX = cx - door;
                const int16_t rightX = cx;
                if (door > 0)
                {
                    dma_display->fillRect(leftX, HEADER_Y, door, HEADER_H, headerBgColor);
                    dma_display->fillRect(rightX, HEADER_Y, door, HEADER_H, headerBgColor);
                }
                if (HDR_EDGE_HIGHLIGHT)
                {
                    dma_display->drawFastVLine(constrain(leftX, 0, PANEL_RES_X - 1), HEADER_Y, HEADER_H, headerFgColor);
                    dma_display->drawFastVLine(constrain(rightX + door - 1, 0, PANEL_RES_X - 1), HEADER_Y, HEADER_H, headerFgColor);
                }
            }
            else
            {
                const int16_t cover = cx - door;
                const int16_t leftW = cx - cover;
                const int16_t rightX = cx + cover;
                const int16_t rightW = PANEL_RES_X - rightX;
                if (leftW > 0)
                    dma_display->fillRect(0, HEADER_Y, leftW, HEADER_H, headerBgColor);
                if (rightW > 0)
                    dma_display->fillRect(rightX, HEADER_Y, rightW, HEADER_H, headerBgColor);
                if (HDR_EDGE_HIGHLIGHT)
                {
                    dma_display->drawFastVLine(constrain(leftW, 0, PANEL_RES_X - 1), HEADER_Y, HEADER_H, headerFgColor);
                    dma_display->drawFastVLine(constrain(rightX, 0, PANEL_RES_X - 1), HEADER_Y, HEADER_H, headerFgColor);
                }
            }
        }
        else
        {
            lunarLuckUtf8.drawUTF8(textX, LINE1_Y, currentTitle);
        }
        // Restore transparent mode for non-header lines.
        lunarLuckUtf8.setFontMode(1);
    }
    else
    {
        // Opaque text fallback so inter-letter background stays consistent.
        dma_display->setTextColor(headerFgColor, headerBgColor);
        if (hdrState == HDR_DOOR)
        {
            const char *activeTitle = hdrDrawNew ? hdrNew : hdrOld;
            dma_display->setCursor(textX, LINE1_Y);
            dma_display->print(activeTitle);
            const int16_t door = constrain(hdrDoorPx, 0, cx);
            if (!hdrReverse)
            {
                const int16_t leftX = cx - door;
                const int16_t rightX = cx;
                if (door > 0)
                {
                    dma_display->fillRect(leftX, HEADER_Y, door, HEADER_H, headerBgColor);
                    dma_display->fillRect(rightX, HEADER_Y, door, HEADER_H, headerBgColor);
                }
                if (HDR_EDGE_HIGHLIGHT)
                {
                    dma_display->drawFastVLine(constrain(leftX, 0, PANEL_RES_X - 1), HEADER_Y, HEADER_H, headerFgColor);
                    dma_display->drawFastVLine(constrain(rightX + door - 1, 0, PANEL_RES_X - 1), HEADER_Y, HEADER_H, headerFgColor);
                }
            }
            else
            {
                const int16_t cover = cx - door;
                const int16_t leftW = cx - cover;
                const int16_t rightX = cx + cover;
                const int16_t rightW = PANEL_RES_X - rightX;
                if (leftW > 0)
                    dma_display->fillRect(0, HEADER_Y, leftW, HEADER_H, headerBgColor);
                if (rightW > 0)
                    dma_display->fillRect(rightX, HEADER_Y, rightW, HEADER_H, headerBgColor);
                if (HDR_EDGE_HIGHLIGHT)
                {
                    dma_display->drawFastVLine(constrain(leftW, 0, PANEL_RES_X - 1), HEADER_Y, HEADER_H, headerFgColor);
                    dma_display->drawFastVLine(constrain(rightX, 0, PANEL_RES_X - 1), HEADER_Y, HEADER_H, headerFgColor);
                }
            }
        }
        else
        {
            dma_display->setCursor(textX, LINE1_Y);
            dma_display->print(currentTitle);
        }
    }
}

static void advanceSection()
{
    if (g_sectionCount == 0)
        return;
    const uint8_t oldIndex = currentSectionIndex;
    const uint8_t nextIndex = static_cast<uint8_t>((currentSectionIndex + 1) % g_sectionCount);
    hdrReverse = ((oldIndex & 1u) != 0u);
    startHeaderDoor(g_sections[oldIndex].title, g_sections[nextIndex].title);
    currentSectionIndex = nextIndex;
    setSectionStartState();
}

static size_t copyTrimmedToken(char *dst, size_t cap, const char *start, size_t len)
{
    if (!dst || cap == 0)
        return 0;
    dst[0] = '\0';
    if (!start || len == 0)
        return 0;

    size_t s = 0;
    while (s < len && isspace(static_cast<unsigned char>(start[s])))
        ++s;
    size_t e = len;
    while (e > s && isspace(static_cast<unsigned char>(start[e - 1])))
        --e;
    if (e <= s)
        return 0;
    return safeAppendN(dst, cap, start + s, e - s);
}

static bool startsWithTopicPrefix(const char *src, size_t &prefixLen)
{
    prefixLen = 0;
    if (!src)
        return false;

    static const char *kPrefix1 = "Chủ đề:";
    static const char *kPrefix2 = "Chu de:";
    static const char *kPrefix3 = "Chủ Đề:";
    const size_t n1 = strlen(kPrefix1);
    const size_t n2 = strlen(kPrefix2);
    const size_t n3 = strlen(kPrefix3);

    if (strncmp(src, kPrefix1, n1) == 0)
    {
        prefixLen = n1;
        return true;
    }
    if (strncmp(src, kPrefix2, n2) == 0)
    {
        prefixLen = n2;
        return true;
    }
    if (strncmp(src, kPrefix3, n3) == 0)
    {
        prefixLen = n3;
        return true;
    }
    return false;
}

static const char *skipTopicLeadForGoiY(const char *headlineRaw)
{
    if (!headlineRaw)
        return "";

    const char *p = headlineRaw;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
        ++p;

    size_t prefixLen = 0;
    bool hasPrefix = startsWithTopicPrefix(p, prefixLen);
    if (!hasPrefix)
    {
        // Non-accent fallback variants.
        if (strncmp(p, "Chu De:", 7) == 0 || strncmp(p, "chu de:", 7) == 0)
        {
            hasPrefix = true;
            prefixLen = 7;
        }
    }
    if (!hasPrefix)
        return p;

    p += prefixLen;
    while (*p != '\0' && *p != '.')
        ++p;
    if (*p == '.')
        ++p;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
        ++p;
    return p;
}

static const char *skipFocusLeadForGoiY(const char *goiYRaw, const char *focusPhrase)
{
    if (!goiYRaw)
        return "";
    const char *p = goiYRaw;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
        ++p;

    if (!focusPhrase || focusPhrase[0] == '\0')
        return p;

    size_t focusLen = strlen(focusPhrase);
    if (strncmp(p, focusPhrase, focusLen) != 0)
        return p;

    p += focusLen;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == '.' || *p == ';' || *p == ':')
        ++p;
    return p;
}

static void extractFocusPhraseFromHeadline(
    char *focusDst, size_t focusCap,
    const char *headlineRaw,
    const char *fallbackTopic)
{
    if (focusDst && focusCap > 0)
        focusDst[0] = '\0';

    if (!headlineRaw)
    {
        if (focusDst && focusCap > 0)
            safeAppend(focusDst, focusCap, fallbackTopic ? fallbackTopic : "");
        return;
    }

    size_t prefixLen = 0;
    if (!startsWithTopicPrefix(headlineRaw, prefixLen))
    {
        if (focusDst && focusCap > 0)
            safeAppend(focusDst, focusCap, fallbackTopic ? fallbackTopic : "");
        return;
    }

    const char *p = headlineRaw + prefixLen;
    while (*p == ' ')
        ++p;

    // Skip category name sentence: "Chủ đề: <CategoryName>."
    while (*p != '\0' && *p != '.')
        ++p;
    if (*p == '.')
        ++p;
    while (*p == ' ' || *p == '\n' || *p == '\r')
        ++p;

    const char *focusEnd = p;
    while (*focusEnd != '\0' && *focusEnd != '.' && *focusEnd != ';' && *focusEnd != '\n' && *focusEnd != '\r')
        ++focusEnd;

    if (focusDst && focusCap > 0)
        copyTrimmedToken(focusDst, focusCap, p, static_cast<size_t>(focusEnd - p));
    if (focusDst && focusCap > 0 && focusDst[0] == '\0')
        safeAppend(focusDst, focusCap, fallbackTopic ? fallbackTopic : "");
}


static void renderCurrentLunarLuckSection()
{
    if (!dma_display || g_sectionCount == 0)
        return;

    const bool mono = (theme == 1);
    const uint16_t headerFgColor = mono ? dma_display->color565(245, 245, 245)
                                        : dma_display->color565(255, 235, 180);
    const uint16_t headerBgColor = dma_display->color565(12, 22, 45);
    const uint16_t headerDividerColor = mono ? dma_display->color565(80, 80, 90)
                                             : dma_display->color565(55, 80, 120);
    const uint16_t contentColorDefault = mono ? dma_display->color565(180, 205, 230)
                                              : dma_display->color565(120, 210, 255);
    const uint16_t toneGoodColor = dma_display->color565(70, 235, 110); // green
    const uint16_t toneBadColor = dma_display->color565(255, 70, 70);   // red
    const uint16_t toneMidColor = dma_display->color565(90, 170, 255);  // blue

    const LuckSection &sec = g_sections[currentSectionIndex];
    const bool isVanKhi = (sec.title && strcmp(sec.title, "Vận Khí") == 0);

    setLunarLuckUtf8Font();
    renderHeaderLine1(sec.title, headerFgColor, headerBgColor, headerDividerColor);
    dma_display->fillRect(0, LINE2_CLEAR_Y_SAFE, PANEL_RES_X, LINE2_CLEAR_H_SAFE, myBLACK);

    if (isVanKhi)
    {
        const char *sepPtr = strstr(sec.content, SEP);
        if (!sepPtr)
        {
            // Fallback if format changes unexpectedly.
            if (lunarLuckUtf8Ready)
            {
                lunarLuckUtf8.setForegroundColor(contentColorDefault);
                const int x = marqueeActive ? marqueeOffsetPx : 0;
                lunarLuckUtf8.drawUTF8(x, LINE2_Y, sec.content);
            }
            else
            {
                dma_display->setTextColor(contentColorDefault);
                dma_display->setCursor(marqueeActive ? marqueeOffsetPx : 0, LINE2_Y);
                dma_display->print(sec.content);
            }
            return;
        }

        char ngayPart[CONTENT_MAX];
        char xhPart[CONTENT_MAX];
        xhPart[0] = '\0';
        const size_t ngayLen = static_cast<size_t>(sepPtr - sec.content);
        const size_t copyNgay = (ngayLen < (CONTENT_MAX - 1)) ? ngayLen : (CONTENT_MAX - 1);
        memcpy(ngayPart, sec.content, copyNgay);
        ngayPart[copyNgay] = '\0';
        safeAppend(xhPart, sizeof(xhPart), sepPtr + strlen(SEP));

        uint16_t ngayColor = toneMidColor;
        if (strstr(ngayPart, "Tốt") != nullptr)
            ngayColor = toneGoodColor;
        else if (strstr(ngayPart, "Xấu") != nullptr)
            ngayColor = toneBadColor;

        uint16_t xhColor = toneMidColor;
        if (strstr(xhPart, "(Tốt)") != nullptr)
            xhColor = toneGoodColor;
        else if (strstr(xhPart, "(Xấu)") != nullptr)
            xhColor = toneBadColor;

        const int baseX = marqueeActive ? marqueeOffsetPx : 0;
        if (lunarLuckUtf8Ready)
        {
            lunarLuckUtf8.setForegroundColor(ngayColor);
            lunarLuckUtf8.drawUTF8(baseX, LINE2_Y, ngayPart);
            int nextX = baseX + static_cast<int>(lunarLuckUtf8.getUTF8Width(ngayPart));

            lunarLuckUtf8.setForegroundColor(contentColorDefault);
            lunarLuckUtf8.drawUTF8(nextX, LINE2_Y, SEP);
            nextX += static_cast<int>(lunarLuckUtf8.getUTF8Width(SEP));

            lunarLuckUtf8.setForegroundColor(xhColor);
            lunarLuckUtf8.drawUTF8(nextX, LINE2_Y, xhPart);
        }
        else
        {
            dma_display->setTextColor(ngayColor);
            dma_display->setCursor(baseX, LINE2_Y);
            dma_display->print(ngayPart);
            int nextX = baseX + measureTextWidthPx(ngayPart, static_cast<uint16_t>(strlen(ngayPart)));

            dma_display->setTextColor(contentColorDefault);
            dma_display->setCursor(nextX, LINE2_Y);
            dma_display->print(SEP);
            nextX += measureTextWidthPx(SEP, static_cast<uint16_t>(strlen(SEP)));

            dma_display->setTextColor(xhColor);
            dma_display->setCursor(nextX, LINE2_Y);
            dma_display->print(xhPart);
        }
    }
    else
    {
        if (lunarLuckUtf8Ready)
        {
            lunarLuckUtf8.setForegroundColor(contentColorDefault);
            const int x = marqueeActive ? marqueeOffsetPx : 0;
            lunarLuckUtf8.drawUTF8(x, LINE2_Y, sec.content);
        }
        else
        {
            dma_display->setTextColor(contentColorDefault);
            dma_display->setCursor(marqueeActive ? marqueeOffsetPx : 0, LINE2_Y);
            dma_display->print(sec.content);
        }
    }
}

static void buildLunarLuckLinesForDate(int dd, int mm, int yy)
{
    const int savedDay = d_day;
    const int savedMonth = d_month;
    const int savedYear = d_year;

    // Some existing formatters use global date fields, so temporarily bind them.
    d_day = static_cast<byte>(dd);
    d_month = static_cast<byte>(mm);
    d_year = yy;

    const int tzMinutes = tzOffset;
    LunarDate ld = convertSolar2Lunar(dd, mm, yy, tzMinutes);
    LunarDayDetail dayInfo = buildLunarDayDetail(dd, mm, yy);
    const XuatHanhInfo xh = calcXuatHanhKhongMinh(ld.month, ld.day);

    lunarLuckScore = computeLunarLuckScore(ld);
    lunarLuckScore += static_cast<int>(xh.tone);

    String hop1, hop2, ky;
    zodiacCompatByBranch(dayInfo.branchIndex, hop1, hop2, ky);

    String nenLam, nenTranh;
    buildLunarActionAdvice(lunarLuckScore, ld.day, ld.month, ld.leap, dayInfo, nenLam, nenTranh);
    String headline = buildContextHeadline(lunarLuckScore, ld.day, ld.month, ld.year, ld.leap, dayInfo);
    char focusPhrase[CONTENT_MAX];
    String categoryNameVi = elementToVietnamese(dayInfo.element);
    extractFocusPhraseFromHeadline(focusPhrase, sizeof(focusPhrase), headline.c_str(), categoryNameVi.c_str());

    String dayCanChi = formatLunarDayName(dd, mm, yy);
    String monthCanChi = formatCanChiMonthVi(ld.year, ld.month);
    String yearCanChi = formatCanChiYearVi(ld.year);
    String tietKhi = formatSolarTermTagVi();

    char gioTotCompact[CONTENT_MAX];
    buildHoangDaoHoursCompact(dayInfo.branchIndex, gioTotCompact, sizeof(gioTotCompact));
    char xuatHanhLine[CONTENT_MAX];
    snprintf(xuatHanhLine, sizeof(xuatHanhLine), "%s (%s)", xh.name ? xh.name : "", toneLabelVN(xh.tone));
    xuatHanhLine[CONTENT_MAX - 1] = '\0';
    char caDaoLine[CONTENT_MAX];
    buildCaDaoPhrase(caDaoLine, sizeof(caDaoLine), lunarLuckScore, ld.day, ld.month, ld.year, dayInfo);

    buildLuckSections(
        ld.day, ld.month, ld.year,
        dayCanChi.c_str(), monthCanChi.c_str(), tietKhi.c_str(), yearCanChi.c_str(),
        hop1.c_str(), hop2.c_str(), ky.c_str(),
        gioTotCompact,
        lunarLuckScore,
        categoryNameVi.c_str(),
        focusPhrase,
        headline.c_str(),
        nenLam.c_str(),
        nenTranh.c_str(),
        xuatHanhLine,
        static_cast<int>(xh.tone),
        xh.name,
        caDaoLine);

    d_day = static_cast<byte>(savedDay);
    d_month = static_cast<byte>(savedMonth);
    d_year = savedYear;
    lunarLuckInitialized = true;
}

static void rebuildLunarForOffset()
{
    getTimeFromRTC();
    int baseDay = d_day;
    int baseMonth = d_month;
    int baseYear = d_year;
    lunarLuckBuiltDay = baseDay;
    lunarLuckBuiltMonth = baseMonth;
    lunarLuckBuiltYear = baseYear;

    lunarDayOffset = constrain(lunarDayOffset, LUNAR_OFFSET_MIN, LUNAR_OFFSET_MAX);
    lunarPreviewMode = (lunarDayOffset != 0);
    if (lunarPreviewMode)
        addDaysToDate(baseYear, baseMonth, baseDay, lunarDayOffset);

    buildLunarLuckLinesForDate(baseDay, baseMonth, baseYear);
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
static constexpr int SCENE_H = 25; // top area used for the scene; bottom is reserved for marquee/status

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

static void fillNightBackground(int starShiftX = 0)
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
        int sx = static_cast<int>(p[0]) + starShiftX;
        if (sx < 0 || sx >= SCENE_W)
            continue;
        dma_display->drawPixel(sx, p[1], star);
        // occasional plus star
        if ((p[0] % 3) == 0)
        {
            if (sx > 0) dma_display->drawPixel(sx - 1, p[1], star);
            if (sx < SCENE_W - 1) dma_display->drawPixel(sx + 1, p[1], star);
            if (p[1] > 0) dma_display->drawPixel(sx, p[1] - 1, star);
            if (p[1] < SCENE_H - 1) dma_display->drawPixel(sx, p[1] + 1, star);
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
    // Alternate style: smoother silhouette cloud using scanline spans.
    uint16_t outline = night ? dma_display->color565(92, 112, 150) : dma_display->color565(110, 132, 168);
    uint16_t fill = night ? dma_display->color565(176, 192, 220) : dma_display->color565(236, 243, 250);
    uint16_t shade = night ? dma_display->color565(128, 146, 184) : dma_display->color565(192, 208, 226);
    uint16_t hi = night ? dma_display->color565(214, 226, 246) : dma_display->color565(254, 255, 255);

    // left edge offset + run length for each row (about 24x11 cloud)
    const uint8_t spans[][2] = {
        {8, 8}, {6, 12}, {4, 16}, {3, 18}, {2, 20}, {1, 22},
        {1, 22}, {2, 20}, {3, 18}, {5, 14}, {7, 10}
    };

    for (int row = 0; row < 11; ++row)
    {
        int yy = y + row + 4; // move cloud up by 3 px
        if (yy < 0 || yy >= SCENE_H)
            continue;

        int start = x + spans[row][0] + 2;
        int width = spans[row][1];
        int end = start + width - 1;
        if (end < 0 || start >= SCENE_W)
            continue;

        for (int xx = start; xx <= end; ++xx)
        {
            if (xx < 0 || xx >= SCENE_W)
                continue;
            bool edge = (row == 0) || (row == 10) || (xx == start) || (xx == end);
            dma_display->drawPixel(xx, yy, edge ? outline : fill);
        }

        // underside shading inside cloud body
        if (row >= 6 && width > 6)
        {
            for (int xx = start + 2; xx <= end - 2; ++xx)
                dma_display->drawPixel(xx, yy, shade);
        }
    }

    // small top highlights to lift the shape
    dma_display->drawPixel(x + 14, y + 5, hi);
    dma_display->drawPixel(x + 18, y + 4, hi);
    dma_display->drawPixel(x + 21, y + 5, hi);
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
    // Clear/Sunny: keep sky visually clear (no cloud glyphs).
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
    drawPixelRain(2, 14, 10, false);   // left side band
    drawPixelRain(54, 15, 8, false);   // right side band
}

static void drawWeatherSceneRainNight()
{
    clearConditionSceneArea();
    fillNightBackground();
    // Rainy Night: cloud + moon + rain
    drawPixelCloud(10, 7, true);
    drawPixelMoon(54, 9);
    drawPixelRain(12, 14, 50, true);
    drawPixelRain(2, 14, 10, true);    // left side band
    drawPixelRain(54, 15, 8, true);    // right side band
}

static void drawWeatherSceneThunderstorm()
{
    clearConditionSceneArea();
    fillDayBackground();
    // Thunderstorm Day: cloud + bolt + rain
    drawPixelCloud(10, 6, false);
    drawPixelBolt(38, 5);
    drawPixelRain(12, 14, 50, false);
    drawPixelRain(2, 14, 10, false);   // left side band
    drawPixelRain(54, 15, 8, false);   // right side band
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
    drawPixelRain(2, 14, 10, true);    // left side band
    drawPixelRain(54, 15, 8, true);    // right side band
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
    drawPixelMoon(54, 9);
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
    {"partly cloudy", WeatherSceneKind::PartlyCloudy},
    {"partly sunny", WeatherSceneKind::PartlyCloudy},
    {"mostly clear", WeatherSceneKind::PartlyCloudy},
    {"overcast", WeatherSceneKind::Overcast},
    {"overcast clouds", WeatherSceneKind::Overcast},
    {"few clouds", WeatherSceneKind::PartlyCloudy},
    {"scattered clouds", WeatherSceneKind::PartlyCloudy},
    {"broken clouds", WeatherSceneKind::PartlyCloudy},
    {"mist", WeatherSceneKind::Fog},
    {"fog", WeatherSceneKind::Fog},
    {"haze", WeatherSceneKind::Fog},
    {"smoke", WeatherSceneKind::Fog},
    {"dust", WeatherSceneKind::Fog},
    {"sand", WeatherSceneKind::Fog},
    {"ash", WeatherSceneKind::Fog},
    {"windy", WeatherSceneKind::Windy},
    {"breezy", WeatherSceneKind::Windy},
    {"gusty", WeatherSceneKind::Windy},
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
            if (base == WeatherSceneKind::PartlyCloudy && night)
                return WeatherSceneKind::PartlyCloudyNight;
            if (base == WeatherSceneKind::Cloudy && night)
                return WeatherSceneKind::CloudyNight;
            if (base == WeatherSceneKind::Overcast && night)
                return WeatherSceneKind::OvercastNight;
            if (base == WeatherSceneKind::Fog && night)
                return WeatherSceneKind::FogNight;
            if (base == WeatherSceneKind::Windy && night)
                return WeatherSceneKind::WindyNight;
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
    if (normalized.indexOf("wind") >= 0 || normalized.indexOf("breez") >= 0 || normalized.indexOf("gust") >= 0)
        return night ? WeatherSceneKind::WindyNight : WeatherSceneKind::Windy;
    if (normalized.indexOf("rain") >= 0 || normalized.indexOf("shower") >= 0 || normalized.indexOf("drizzle") >= 0)
        return night ? WeatherSceneKind::RainNight : WeatherSceneKind::Rain;
    if (normalized.indexOf("snow") >= 0 || normalized.indexOf("sleet") >= 0 || normalized.indexOf("flurry") >= 0)
        return night ? WeatherSceneKind::SnowNight : WeatherSceneKind::Snow;
    if (normalized.indexOf("night") >= 0)
        return WeatherSceneKind::ClearNight;
    if (normalized.indexOf("overcast") >= 0)
        return night ? WeatherSceneKind::OvercastNight : WeatherSceneKind::Overcast;
    if (normalized.indexOf("fog") >= 0 || normalized.indexOf("mist") >= 0 || normalized.indexOf("haze") >= 0 ||
        normalized.indexOf("smoke") >= 0 || normalized.indexOf("dust") >= 0 || normalized.indexOf("sand") >= 0 || normalized.indexOf("ash") >= 0)
        return night ? WeatherSceneKind::FogNight : WeatherSceneKind::Fog;
    if (normalized.indexOf("partly") >= 0 || normalized.indexOf("few cloud") >= 0 || normalized.indexOf("scattered cloud") >= 0 || normalized.indexOf("broken cloud") >= 0)
        return night ? WeatherSceneKind::PartlyCloudyNight : WeatherSceneKind::PartlyCloudy;
    if (normalized.indexOf("cloud") >= 0 || normalized.indexOf("overcast") >= 0)
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
    case WeatherSceneKind::PartlyCloudy:
        return dma_display->color565(210, 205, 150);
    case WeatherSceneKind::PartlyCloudyNight:
        return dma_display->color565(170, 185, 220);
    case WeatherSceneKind::Cloudy:
        return dma_display->color565(190, 200, 220);
    case WeatherSceneKind::CloudyNight:
        return dma_display->color565(150, 170, 210);
    case WeatherSceneKind::Overcast:
        return dma_display->color565(176, 188, 202);
    case WeatherSceneKind::OvercastNight:
        return dma_display->color565(126, 144, 172);
    case WeatherSceneKind::Fog:
        return dma_display->color565(205, 210, 220);
    case WeatherSceneKind::FogNight:
        return dma_display->color565(145, 165, 190);
    case WeatherSceneKind::Windy:
        return dma_display->color565(180, 210, 230);
    case WeatherSceneKind::WindyNight:
        return dma_display->color565(135, 175, 215);
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

static bool weatherSceneIsNight(WeatherSceneKind kind)
{
    return kind == WeatherSceneKind::SunnyNight ||
           kind == WeatherSceneKind::PartlyCloudyNight ||
           kind == WeatherSceneKind::CloudyNight ||
           kind == WeatherSceneKind::OvercastNight ||
           kind == WeatherSceneKind::FogNight ||
           kind == WeatherSceneKind::WindyNight ||
           kind == WeatherSceneKind::RainNight ||
           kind == WeatherSceneKind::ThunderstormNight ||
           kind == WeatherSceneKind::SnowNight ||
           kind == WeatherSceneKind::ClearNight;
}

static uint16_t weatherSceneTempBgColor(WeatherSceneKind kind);
static uint16_t weatherSceneAdaptiveTempTextColor(WeatherSceneKind kind, uint16_t accent, bool secondary);

static void conditionSceneClockBounds(int &x, int &y, int &w, int &h)
{
    wxv::seg7::Metrics metrics;
    metrics.digitWidth = 3;
    metrics.digitHeight = 5;
    metrics.colonWidth = 1;
    metrics.spacing = 1;

    x = 2;
    y = SCENE_H - metrics.digitHeight - 3;
    w = wxv::seg7::measureTime(t_hour, t_minute, units.clock24h, 1, &metrics);
    h = metrics.digitHeight;
}

static int drawConditionSceneClockText(int x, int y, int hour24, int minute, bool use24h,
                                       const wxv::seg7::Metrics &metrics,
                                       uint16_t digitColor)
{
    int displayHour = hour24 % 24;
    char suffix = '\0';
    if (!use24h)
    {
        suffix = (displayHour >= 12) ? 'P' : 'A';
        if (displayHour == 0)
            displayHour = 12;
        else if (displayHour > 12)
            displayHour -= 12;
    }

    if (minute < 0)
        minute = 0;
    if (minute > 59)
        minute = 59;

    char buf[8];
    if (use24h)
        snprintf(buf, sizeof(buf), "%02d:%02d", displayHour, minute);
    else
        snprintf(buf, sizeof(buf), "%02d:%02d%c", displayHour, minute, suffix);

    int cursorX = x;
    bool afterColon = false;
    for (int i = 0; buf[i] != '\0'; ++i)
    {
        char c = buf[i];
        if (c == ':')
        {
            cursorX -= 1;
            wxv::seg7::drawColon(cursorX, y, digitColor, 1, metrics);
            cursorX += metrics.colonWidth + metrics.spacing;
            afterColon = true;
            continue;
        }

        if (afterColon)
        {
            cursorX += 1;
            afterColon = false;
        }

        wxv::seg7::drawDigit(cursorX, y, c, digitColor, 1, metrics);
        cursorX += metrics.digitWidth + metrics.spacing;
    }

    return cursorX - x;
}

static void drawConditionSceneClockOverlay(WeatherSceneKind kind)
{
    if (!sceneClockEnabled)
        return;

    int clockX = 0;
    int clockY = 0;
    int clockWidth = 0;
    int clockHeight = 0;
    conditionSceneClockBounds(clockX, clockY, clockWidth, clockHeight);

    wxv::seg7::Metrics metrics;
    metrics.digitWidth = 3;
    metrics.digitHeight = 5;
    metrics.colonWidth = 1;
    metrics.spacing = 1;

    const uint16_t accent = weatherSceneAccentColor(kind);
    const uint16_t timeColor = weatherSceneAdaptiveTempTextColor(kind, accent, false);
    const uint16_t shadow = scaleColor565(weatherSceneTempBgColor(kind), weatherSceneIsNight(kind) ? 0.45f : 0.72f);

    drawConditionSceneClockText(clockX + 1, clockY + 1, t_hour, t_minute, units.clock24h,
                                metrics, shadow);
    drawConditionSceneClockText(clockX, clockY, t_hour, t_minute, units.clock24h,
                                metrics, timeColor);
}

static uint16_t weatherSceneTempBgColor(WeatherSceneKind kind)
{
    // Approximate top-right sky color for each scene (where temp is rendered).
    switch (kind)
    {
    case WeatherSceneKind::Sunny:
        return dma_display->color565(45, 170, 255);
    case WeatherSceneKind::PartlyCloudy:
        return dma_display->color565(95, 175, 235);
    case WeatherSceneKind::Cloudy:
        return dma_display->color565(120, 165, 210);
    case WeatherSceneKind::Overcast:
        return dma_display->color565(128, 145, 165);
    case WeatherSceneKind::Fog:
        return dma_display->color565(160, 185, 205);
    case WeatherSceneKind::Windy:
        return dma_display->color565(85, 165, 225);
    case WeatherSceneKind::Rain:
        return dma_display->color565(55, 110, 175);
    case WeatherSceneKind::Thunderstorm:
        return dma_display->color565(38, 74, 128);
    case WeatherSceneKind::Snow:
        return dma_display->color565(175, 210, 240);
    case WeatherSceneKind::SunnyNight:
    case WeatherSceneKind::PartlyCloudyNight:
    case WeatherSceneKind::CloudyNight:
    case WeatherSceneKind::OvercastNight:
    case WeatherSceneKind::FogNight:
    case WeatherSceneKind::WindyNight:
    case WeatherSceneKind::RainNight:
    case WeatherSceneKind::ThunderstormNight:
    case WeatherSceneKind::SnowNight:
    case WeatherSceneKind::ClearNight:
        return dma_display->color565(8, 16, 52);
    default:
        return dma_display->color565(36, 78, 134);
    }
}

static int colorLuma565(uint16_t c)
{
    int r = ((c >> 11) & 0x1F) * 255 / 31;
    int g = ((c >> 5) & 0x3F) * 255 / 63;
    int b = (c & 0x1F) * 255 / 31;
    return (r * 30 + g * 59 + b * 11) / 100;
}

static uint16_t weatherSceneAdaptiveTempTextColor(WeatherSceneKind kind, uint16_t accent, bool secondary)
{
    if (theme == 1)
    {
        return secondary ? dma_display->color565(135, 155, 205)
                         : dma_display->color565(200, 215, 245);
    }

    if (weatherSceneIsNight(kind))
    {
        uint16_t base = secondary ? dma_display->color565(145, 205, 255)
                                  : dma_display->color565(235, 246, 255);
        return lerpColor565(base, accent, secondary ? 0.12f : 0.08f);
    }

    uint16_t base = secondary ? dma_display->color565(255, 188, 112)
                              : dma_display->color565(255, 236, 172);
    return lerpColor565(base, accent, secondary ? 0.10f : 0.06f);
}

static void drawSceneReadabilityOverlay(WeatherSceneKind kind)
{
    // Keep overlays lightweight so the scene top is never obscured.
    // Only draw the scene/status divider line.
    const bool night = weatherSceneIsNight(kind);
    const uint16_t accent = weatherSceneAccentColor(kind);
    const uint16_t divider = scaleColor565(accent, night ? 0.55f : 0.72f);

    dma_display->drawFastHLine(0, SCENE_H - 1, SCENE_W, divider);
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

String formatOutdoorTemperature()
{
    if (isDataSourceNone())
        return String("--");

    if (isDataSourceForecastModel())
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

    if (isDataSourceForecastModel())
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
    String key = currentCond.icon;
    if (key.isEmpty())
        key = currentCond.cond;
    if (key.isEmpty())
        key = str_Weather_Conditions_Des;

    auto isNightAt = [&](time_t epoch, const tm *tmi) -> bool {
        if (!tmi)
            return false;
        const int month = tmi->tm_mon + 1;
        const int day = tmi->tm_mday;
        for (int di = 0; di < forecast.numDays; ++di)
        {
            const ForecastDay &d = forecast.days[di];
            if (d.monthNum == month && d.dayNum == day && d.sunrise > 0 && d.sunset > 0)
            {
                return !(epoch >= static_cast<time_t>(d.sunrise) && epoch < static_cast<time_t>(d.sunset));
            }
        }
        const int hhLocal = tmi->tm_hour;
        return (hhLocal < 6 || hhLocal >= 18);
    };

    if (key.length() > 0 && key.indexOf("night") < 0)
    {
        time_t baseEpoch = (currentCond.time > 0) ? static_cast<time_t>(currentCond.time) : time(nullptr);
        tm *ti = localtime(&baseEpoch);
        if (ti && isNightAt(baseEpoch, ti))
            key += " night";
    }

    const uint8_t *icon = getWeatherIconFromCondition(key);
    uint16_t color = getIconColorFromCondition(key);
    ui_theme::drawBitmapThemed(0, 0, icon, 16, 16, color);
}
static bool splashActive = false;
static bool splashShownThisBoot = false;
static bool splashLockedOut = false;
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
    if (splashLockedOut)
        return;
    if (splashShownThisBoot)
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
    if (!dma_display || !splashActive || splashLockedOut)
        return;

    (void)status;
    (void)step;
    (void)total;
    // Static splash â€“ no animation required.
}

void splashEnd()
{
    if (!dma_display)
        return;
    if (splashLockedOut)
    {
        splashActive = false;
        splashMinimumMs = 0;
        splashShownThisBoot = true;
        return;
    }
    if (!splashActive)
    {
        splashShownThisBoot = true;
        return;
    }

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
    splashShownThisBoot = true;
    splashMinimumMs = 0;
    dma_display->setTextColor(myWHITE);
    dma_display->setTextSize(1);
}

bool isSplashActive()
{
    return splashActive;
}

void splashLockout(bool locked)
{
    splashLockedOut = locked;
    if (locked)
    {
        splashActive = false;
        splashMinimumMs = 0;
        splashShownThisBoot = true;
    }
}

int getTextWidth(const char *text)
{
    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return w;
}

static void copyTrimmed(const char *src, int start, int end, char *dst, size_t dstSize)
{
    if (!dst || dstSize == 0)
        return;
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    while (start < end && isspace(static_cast<unsigned char>(src[start])))
        ++start;
    while (end > start && isspace(static_cast<unsigned char>(src[end - 1])))
        --end;
    int len = end - start;
    if (len < 0)
        len = 0;
    if (len > static_cast<int>(dstSize) - 1)
        len = static_cast<int>(dstSize) - 1;
    if (len > 0)
        memcpy(dst, src + start, static_cast<size_t>(len));
    dst[len] = '\0';
}

static int textWidthOnCanvas(GFXcanvas16 &canvas, const char *text)
{
    int16_t x1, y1;
    uint16_t w, h;
    canvas.getTextBounds(text ? text : "", 0, 0, &x1, &y1, &w, &h);
    return static_cast<int>(w);
}

static void splitHeadingTitle(GFXcanvas16 &canvas, const char *title, char *line1, size_t line1Size, char *line2, size_t line2Size, int maxWidthPx)
{
    if (!line1 || line1Size == 0 || !line2 || line2Size == 0)
        return;

    line1[0] = '\0';
    line2[0] = '\0';

    if (!title || !title[0])
    {
        strncpy(line1, "Section", line1Size - 1);
        line1[line1Size - 1] = '\0';
        return;
    }

    const int len = static_cast<int>(strlen(title));
    if (textWidthOnCanvas(canvas, title) <= maxWidthPx)
    {
        strncpy(line1, title, line1Size - 1);
        line1[line1Size - 1] = '\0';
        return;
    }

    int bestSpace = -1;
    int bestPenalty = 32767;
    for (int i = 1; i < len - 1; ++i)
    {
        if (title[i] != ' ')
            continue;
        char left[48];
        char right[48];
        copyTrimmed(title, 0, i, left, sizeof(left));
        copyTrimmed(title, i + 1, len, right, sizeof(right));
        if (!left[0] || !right[0])
            continue;
        const int lw = textWidthOnCanvas(canvas, left);
        const int rw = textWidthOnCanvas(canvas, right);
        if (lw > maxWidthPx || rw > maxWidthPx)
            continue;
        int penalty = abs(lw - rw);
        if (penalty < bestPenalty)
        {
            bestPenalty = penalty;
            bestSpace = i;
        }
    }

    if (bestSpace >= 0)
    {
        copyTrimmed(title, 0, bestSpace, line1, line1Size);
        copyTrimmed(title, bestSpace + 1, len, line2, line2Size);
        return;
    }

    // No ideal word split found: hard split into two visual halves.
    int cut = len / 2;
    while (cut < len - 1 && title[cut] != ' ')
        ++cut;
    if (cut >= len - 1)
        cut = len / 2;
    copyTrimmed(title, 0, cut, line1, line1Size);
    copyTrimmed(title, cut + 1, len, line2, line2Size);
    if (!line2[0])
    {
        // Final fallback when string has no spaces.
        copyTrimmed(title, 0, len, line1, line1Size);
    }
}

void showSectionHeading(const char *title, const char *subtitle, uint16_t ms)
{
    (void)ms; // duration is managed by screen scheduler state.
    if (!dma_display)
        return;

    GFXcanvas16 canvas(PANEL_RES_X, PANEL_RES_Y);
    canvas.setFont(&Font5x7Uts);
    canvas.setTextWrap(false);
    canvas.setTextSize(1);

    const bool mono = (theme == 1);
    const uint16_t borderColor = ui_theme::applyGraphicColor(mono ? dma_display->color565(150, 150, 180)
                                                                   : dma_display->color565(80, 190, 255));
    const uint16_t titleColor = ui_theme::applyGraphicColor(mono ? dma_display->color565(240, 240, 250)
                                                                  : dma_display->color565(220, 245, 255));
    const uint16_t subtitleColor = ui_theme::applyGraphicColor(mono ? dma_display->color565(175, 175, 200)
                                                                     : dma_display->color565(125, 190, 235));

    char titleLine1[48];
    char titleLine2[48];
    splitHeadingTitle(canvas, title, titleLine1, sizeof(titleLine1), titleLine2, sizeof(titleLine2), PANEL_RES_X - 8);
    const bool hasTwoTitleLines = (titleLine2[0] != '\0');
    const bool hasSubtitle = (subtitle && subtitle[0] != '\0');

    auto measureLine = [&](const char *text, int16_t &x1, int16_t &y1, uint16_t &w, uint16_t &h) {
        canvas.getTextBounds((text && text[0]) ? text : " ", 0, 0, &x1, &y1, &w, &h);
    };

    auto drawCenteredAtTop = [&](const char *text, int topY, uint16_t color) {
        if (!text || !text[0])
            return;
        int16_t x1, y1;
        uint16_t w, h;
        measureLine(text, x1, y1, w, h);
        int x = (PANEL_RES_X - static_cast<int>(w)) / 2 - x1;
        if (x < 2)
            x = 2;
        int baselineY = topY - y1;
        dma_display->setTextColor(color, 0);
        dma_display->setCursor(x, baselineY);
        dma_display->print(text);
    };

    const char *lines[3];
    uint16_t lineColors[3];
    uint8_t lineCount = 0;
    lines[lineCount] = titleLine1;
    lineColors[lineCount++] = titleColor;
    if (hasTwoTitleLines)
    {
        lines[lineCount] = titleLine2;
        lineColors[lineCount++] = titleColor;
    }
    if (hasSubtitle)
    {
        lines[lineCount] = subtitle;
        lineColors[lineCount++] = subtitleColor;
    }

    int lineHeights[3] = {0, 0, 0};
    int totalTextH = 0;
    const int lineGap = 1;
    for (uint8_t i = 0; i < lineCount; ++i)
    {
        int16_t x1, y1;
        uint16_t w, h;
        measureLine(lines[i], x1, y1, w, h);
        lineHeights[i] = static_cast<int>(h);
        totalTextH += lineHeights[i];
    }
    if (lineCount > 1)
        totalTextH += static_cast<int>(lineCount - 1) * lineGap;

    const int innerTop = 2;
    const int innerBottom = PANEL_RES_Y - 3;
    const int innerH = innerBottom - innerTop + 1;
    int y = innerTop + (innerH - totalTextH) / 2;
    if (y < innerTop)
        y = innerTop;

    dma_display->fillScreen(0);
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextWrap(false);
    dma_display->setTextSize(1);
    dma_display->drawRect(1, 1, PANEL_RES_X - 2, PANEL_RES_Y - 2, borderColor);

    for (uint8_t i = 0; i < lineCount; ++i)
    {
        drawCenteredAtTop(lines[i], y, lineColors[i]);
        y += lineHeights[i] + lineGap;
    }
}

namespace
{
struct WeatherIconVariant
{
    const uint8_t *day;
    const uint8_t *night;
};

enum class IconGroup : uint8_t
{
    Clear,
    PartlyCloudy,
    Cloudy,
    Overcast,
    Fog,
    Drizzle,
    Rain,
    Thunder,
    Snow,
    Windy,
    Unknown
};

String normalizeWeatherIconKey(String input)
{
    input.trim();
    input.toLowerCase();
    input.replace('_', ' ');
    input.replace('-', ' ');
    while (input.indexOf("  ") >= 0)
        input.replace("  ", " ");
    return input;
}

bool isNightWeatherIconKey(const String &key)
{
    return key.endsWith("n") || key.indexOf("night") >= 0;
}

bool parseWeatherConditionCode(const String &key, int &codeOut)
{
    if (key.length() == 0)
        return false;

    char *end = nullptr;
    long parsed = strtol(key.c_str(), &end, 10);
    if (end == key.c_str() || *end != '\0')
        return false;

    codeOut = static_cast<int>(parsed);
    return true;
}

IconGroup weatherIconGroupFromProviderCode(int code)
{
    switch (code)
    {
    case 0:
        return IconGroup::Clear;
    case 1:
    case 2:
        return IconGroup::PartlyCloudy;
    case 3:
        return IconGroup::Overcast;
    case 45:
    case 48:
        return IconGroup::Fog;
    case 51:
    case 53:
    case 55:
        return IconGroup::Drizzle;
    case 56:
    case 57:
    case 66:
    case 67:
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86:
        return IconGroup::Snow;
    case 61:
    case 63:
    case 65:
    case 80:
    case 81:
    case 82:
        return IconGroup::Rain;
    case 95:
    case 96:
    case 99:
        return IconGroup::Thunder;
    default:
        break;
    }

    return IconGroup::Unknown;
}

IconGroup classifyWeatherIconGroup(const String &key)
{
    if (key.startsWith("01"))
        return IconGroup::Clear;
    if (key.startsWith("02") || key.startsWith("03"))
        return IconGroup::PartlyCloudy;
    if (key.startsWith("04"))
        return IconGroup::Overcast;
    if (key.startsWith("09") || key.startsWith("10"))
        return IconGroup::Rain;
    if (key.startsWith("11"))
        return IconGroup::Thunder;
    if (key.startsWith("13"))
        return IconGroup::Snow;
    if (key.startsWith("50"))
        return IconGroup::Fog;

    int providerCode = -1;
    if (parseWeatherConditionCode(key, providerCode))
    {
        IconGroup fromCode = weatherIconGroupFromProviderCode(providerCode);
        if (fromCode != IconGroup::Unknown)
            return fromCode;
    }

    if (key.indexOf("thunder") >= 0 || key.indexOf("tstorm") >= 0 || key.indexOf("storm") >= 0 ||
        key.indexOf("lightning") >= 0 || key.indexOf("squall") >= 0 || key.indexOf("tornado") >= 0 ||
        key.indexOf("hail") >= 0)
        return IconGroup::Thunder;
    if (key.indexOf("snow") >= 0 || key.indexOf("sleet") >= 0 || key.indexOf("flurr") >= 0 ||
        key.indexOf("ice pellet") >= 0 || key.indexOf("wintry") >= 0 ||
        key.indexOf("freezing rain") >= 0 || key.indexOf("freezing drizzle") >= 0)
        return IconGroup::Snow;
    if (key.indexOf("drizzle") >= 0)
        return IconGroup::Drizzle;
    if (key.indexOf("rain") >= 0 || key.indexOf("showers") >= 0 || key.indexOf("shower") >= 0)
        return IconGroup::Rain;
    if (key.indexOf("fog") >= 0 || key.indexOf("mist") >= 0 || key.indexOf("haze") >= 0 ||
        key.indexOf("smoke") >= 0 || key.indexOf("dust") >= 0 || key.indexOf("sand") >= 0 ||
        key.indexOf("ash") >= 0)
        return IconGroup::Fog;
    if (key.indexOf("overcast") >= 0 || key.indexOf("mostly cloudy") >= 0)
        return IconGroup::Overcast;
    if (key.indexOf("partly") >= 0 || key.indexOf("few cloud") >= 0 || key.indexOf("mostly clear") >= 0 ||
        key.indexOf("mostly sunny") >= 0 || key.indexOf("scattered cloud") >= 0 ||
        key.indexOf("broken cloud") >= 0)
        return IconGroup::PartlyCloudy;
    if (key.indexOf("cloud") >= 0)
        return IconGroup::Cloudy;
    if (key.indexOf("wind") >= 0 || key.indexOf("breez") >= 0 || key.indexOf("gust") >= 0)
        return IconGroup::Windy;
    if (key.indexOf("clear") >= 0 || key.indexOf("sunny") >= 0 || key.indexOf("fair") >= 0 ||
        key.indexOf("weather") >= 0)
        return IconGroup::Clear;

    return IconGroup::Unknown;
}

const uint8_t *selectWeatherIconVariant(const WeatherIconVariant &variant, bool actualNight)
{
    if (actualNight && variant.night != nullptr)
        return variant.night;
    if (variant.day != nullptr)
        return variant.day;
    return actualNight ? variant.night : variant.day;
}

WeatherIconVariant weatherIconVariantFromGroup(const String &key)
{
    switch (classifyWeatherIconGroup(key))
    {
    case IconGroup::Thunder:
        return {icon_thunder, nullptr};
    case IconGroup::Snow:
        return {icon_snow, nullptr};
    case IconGroup::Drizzle:
    case IconGroup::Rain:
        return {icon_rain, nullptr};
    case IconGroup::Fog:
        return {icon_fog, nullptr};
    case IconGroup::Overcast:
    case IconGroup::PartlyCloudy:
    case IconGroup::Cloudy:
    case IconGroup::Windy:
        return {icon_cloudy, icon_cloud_night};
    case IconGroup::Clear:
        return {icon_clear, icon_clear_night};
    case IconGroup::Unknown:
    default:
        return {icon_clear, icon_clear_night};
    }
}

WeatherIconVariant weatherIconVariantFromCodeKey(const String &key)
{
    return weatherIconVariantFromGroup(key);
}
} // namespace

const uint8_t *getWeatherIconFromCode(String code)
{
    const String key = normalizeWeatherIconKey(code);
    const WeatherIconVariant variant = weatherIconVariantFromCodeKey(key);
    if (variant.day != nullptr || variant.night != nullptr)
        return selectWeatherIconVariant(variant, isNightWeatherIconKey(key));
    return getWeatherIconFromCondition(key); // shared fallback for non-OWM code strings
}

const uint8_t *getWeatherIconFromCondition(String condition)
{
    const String key = normalizeWeatherIconKey(condition);
    return selectWeatherIconVariant(weatherIconVariantFromGroup(key), isNightWeatherIconKey(key));
}

const uint16_t getIconColorFromCondition(String condition)
{
    const String key = normalizeWeatherIconKey(condition);
    const bool isNight = (key.indexOf("night") >= 0) || key.endsWith("n");
    const IconGroup group = classifyWeatherIconGroup(key);

    if (isNight)
    {
        if (group == IconGroup::Thunder)
            return dma_display->color565(180, 120, 255);
        if (group == IconGroup::Snow)
            return dma_display->color565(170, 220, 255);
        if (group == IconGroup::Rain || group == IconGroup::Drizzle)
            return dma_display->color565(0, 120, 200);
        if (group == IconGroup::Fog)
            return dma_display->color565(120, 140, 180);
        if (group == IconGroup::Overcast)
            return dma_display->color565(135, 145, 160);
        if (group == IconGroup::Cloudy || group == IconGroup::PartlyCloudy || group == IconGroup::Windy)
            return dma_display->color565(120, 160, 220);
        if (group == IconGroup::Clear || group == IconGroup::Unknown)
            return myBLUE;
        return myBLUE;
    }

    if (group == IconGroup::Thunder)
        return dma_display->color565(255, 255, 0);
    if (group == IconGroup::Snow)
        return dma_display->color565(220, 255, 255);
    if (group == IconGroup::Rain || group == IconGroup::Drizzle)
        return dma_display->color565(0, 200, 255);
    if (group == IconGroup::Fog)
        return dma_display->color565(180, 180, 180);
    if (group == IconGroup::Overcast)
        return dma_display->color565(145, 150, 155);
    if (group == IconGroup::Cloudy || group == IconGroup::PartlyCloudy || group == IconGroup::Windy)
        return dma_display->color565(180, 180, 180);
    if (group == IconGroup::Clear || group == IconGroup::Unknown)
        return dma_display->color565(255, 255, 0);
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

void drawLunarLuckScreen()
{
    if (!lunarLuckInitialized)
    {
        rebuildLunarForOffset();
    }
    else
    {
        // Rebuild only when calendar date changes; preserve offsets across frames.
        getTimeFromRTC();
        if (d_day != lunarLuckBuiltDay || d_month != lunarLuckBuiltMonth || d_year != lunarLuckBuiltYear)
            rebuildLunarForOffset();
    }
    renderCurrentLunarLuckSection();
}

void resetLunarLuckSectionRotation()
{
    if (g_sectionCount == 0)
        return;
    lunarLuckRotationPaused = false;
    upLastPressMs = 0;
    downLastPressMs = 0;
    currentSectionIndex = 0;
    hdrState = HDR_IDLE;
    hdrDoorPx = PANEL_RES_X / 2;
    hdrDrawNew = true;
    hdrDelayActive = false;
    if (hdrBrightnessPulsed)
    {
        setPanelBrightness(hdrBrightnessSaved);
        hdrBrightnessPulsed = false;
    }
    hdrOld[0] = '\0';
    strncpy(hdrNew, g_sections[0].title ? g_sections[0].title : "", TITLE_MAX - 1);
    hdrNew[TITLE_MAX - 1] = '\0';
    setSectionStartState();
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

void tickLunarLuckMarquee()
{
    if (!dma_display)
        return;

    unsigned long nowMs = millis();
    if (currentScreen != SCREEN_LUNAR_LUCK)
        return;

    if (!lunarLuckInitialized)
        rebuildLunarForOffset();
    else
    {
        getTimeFromRTC();
        if (d_day != lunarLuckBuiltDay || d_month != lunarLuckBuiltMonth || d_year != lunarLuckBuiltYear)
        {
            rebuildLunarForOffset();
            renderCurrentLunarLuckSection();
            return;
        }
    }

    if (g_sectionCount == 0)
        return;

    // Keep header door animation progressing even on static line-2 sections.
    if (hdrState == HDR_DOOR)
        renderCurrentLunarLuckSection();

    uint32_t intervalMs = static_cast<uint32_t>(lunarLuckBaseScrollIntervalMs());
    if (lunarLuckSpeedScale != 1.0f)
    {
        float scaled = static_cast<float>(intervalMs) * lunarLuckSpeedScale;
        intervalMs = static_cast<uint32_t>(constrain(static_cast<int>(scaled), 8, 1200));
    }

    if (marqueeActive)
    {
        if (lunarLuckRotationPaused)
            return;

        if (nowMs - lastScrollMs >= intervalMs)
        {
            marqueeOffsetPx -= SCROLL_STEP_PX;
            lastScrollMs = nowMs;
            renderCurrentLunarLuckSection();
        }

        const LuckSection &sec = g_sections[currentSectionIndex];
        if (marqueeOffsetPx <= -(sec.contentWidthPx + GAP_PX))
        {
            advanceSection();
            renderCurrentLunarLuckSection();
        }
    }
    else
    {
        if (lunarLuckRotationPaused)
            return;

        if (nowMs - sectionStartMs >= STATIC_DWELL_MS)
        {
            advanceSection();
            renderCurrentLunarLuckSection();
        }
    }
}

void adjustLunarLuckSpeed(int delta)
{
    if (delta == 0)
        return;

    constexpr float kFasterPerClick = 0.92f;
    constexpr float kSlowerPerClick = 1.08f;
    if (delta > 0)
        lunarLuckSpeedScale *= kFasterPerClick;
    else
        lunarLuckSpeedScale *= kSlowerPerClick;

    lunarLuckSpeedScale = constrain(lunarLuckSpeedScale, 0.08f, 20.0f);
    const unsigned long baseMs = lunarLuckBaseScrollIntervalMs();
    float rawAdjustedMs = static_cast<float>(baseMs) * lunarLuckSpeedScale;
    int effectiveMs = constrain(static_cast<int>(rawAdjustedMs), 8, 1200);
    Serial.printf("[LUNAR_LUCK] Speed effective=%dms raw=%.2fms (global=%lums, scale=%.3f)\n",
                  effectiveMs, rawAdjustedMs, baseMs, lunarLuckSpeedScale);
}

bool handleLunarLuckInput(uint32_t code)
{
    if (currentScreen != SCREEN_LUNAR_LUCK)
        return false;

    if (code == IR_UP)
    {
        if (g_sectionCount == 0)
            return true;
        currentSectionIndex = static_cast<uint8_t>((currentSectionIndex + 1) % g_sectionCount);
        hdrState = HDR_IDLE;
        hdrDoorPx = PANEL_RES_X / 2;
        hdrDrawNew = true;
        hdrDelayActive = false;
        if (hdrBrightnessPulsed)
        {
            setPanelBrightness(hdrBrightnessSaved);
            hdrBrightnessPulsed = false;
        }
        strncpy(hdrNew, g_sections[currentSectionIndex].title ? g_sections[currentSectionIndex].title : "", TITLE_MAX - 1);
        hdrNew[TITLE_MAX - 1] = '\0';
        setSectionStartState();
        renderCurrentLunarLuckSection();
        return true;
    }

    if (code == IR_DOWN)
    {
        if (g_sectionCount == 0)
            return true;
        currentSectionIndex = (currentSectionIndex == 0)
                                  ? static_cast<uint8_t>(g_sectionCount - 1)
                                  : static_cast<uint8_t>(currentSectionIndex - 1);
        hdrState = HDR_IDLE;
        hdrDoorPx = PANEL_RES_X / 2;
        hdrDrawNew = true;
        hdrDelayActive = false;
        if (hdrBrightnessPulsed)
        {
            setPanelBrightness(hdrBrightnessSaved);
            hdrBrightnessPulsed = false;
        }
        strncpy(hdrNew, g_sections[currentSectionIndex].title ? g_sections[currentSectionIndex].title : "", TITLE_MAX - 1);
        hdrNew[TITLE_MAX - 1] = '\0';
        setSectionStartState();
        renderCurrentLunarLuckSection();
        return true;
    }

    if (code == IR_OK)
    {
        lunarLuckRotationPaused = !lunarLuckRotationPaused;
        if (!lunarLuckRotationPaused)
        {
            sectionStartMs = millis();
            lastScrollMs = sectionStartMs;
        }
        renderCurrentLunarLuckSection();
        return true;
    }

    return false;
}

void fetchWeatherFromOWM(bool showBusy)
{
    if (WiFi.status() != WL_CONNECTED)
        return;
    const bool showedBusy = showBusy && dma_display != nullptr && !isSplashActive();
    if (showedBusy)
    {
        wxv::notify::showNotification(wxv::notify::NotifyId::Busy, myCYAN, myWHITE, "UPDATING");
    }
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

    if (apiKey.isEmpty()) {
        Serial.println("[OWM] Missing API key or city; skip fetch");
        if (showedBusy) refreshVisibleScreen();
        return;
    }

    const bool hasValidCoords =
        isfinite(noaaLatitude) && isfinite(noaaLongitude) &&
        (noaaLatitude >= -90.0f && noaaLatitude <= 90.0f) &&
        (noaaLongitude >= -180.0f && noaaLongitude <= 180.0f) &&
        !(fabs(noaaLatitude) < 0.0001f && fabs(noaaLongitude) < 0.0001f);

    if (!hasValidCoords && selectedCity.isEmpty()) {
        Serial.println("[OWM] Missing city and invalid lat/lon; skip fetch");
        if (showedBusy) refreshVisibleScreen();
        return;
    }

    // Minimal encoding for city names with spaces/commas
    selectedCity.replace(" ", "%20");
    selectedCity.replace(",", "%2C");

    String url = "http://api.openweathermap.org/data/2.5/weather?";
    if (hasValidCoords) {
        url += "lat=" + String(noaaLatitude, 4) + "&lon=" + String(noaaLongitude, 4);
    } else {
        url += "q=" + selectedCity;
        if (!selectedCountry.isEmpty()) {
            url += "," + selectedCountry;
        }
    }
    url += "&units=" + units + "&appid=" + apiKey;
    String jsonBuffer = httpGETRequest(url.c_str());
    if (jsonBuffer == "{}")
    {
        if (showedBusy) refreshVisibleScreen();
        return;
    }
    JSONVar data = JSON.parse(jsonBuffer);
    if (JSON.typeof_(data) == "undefined")
    {
        Serial.println("Failed to parse weather JSON");
        if (showedBusy) refreshVisibleScreen();
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
    auto buildOwmQuery = [&]() -> String {
        String q;
        if (hasValidCoords) {
            q += "lat=" + String(noaaLatitude, 4) + "&lon=" + String(noaaLongitude, 4);
        } else {
            q += "q=" + selectedCity;
            if (!selectedCountry.isEmpty()) {
                q += "," + selectedCountry;
            }
        }
        q += "&units=" + units + "&appid=" + apiKey;
        return q;
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
    double windGust = toMetersPerSecond(readNumber(windBlock, "gust"));
    double obsTime = readNumber(data, "dt");

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

    // Map OWM payload into shared current/weather model so Wx screens behave
    // the same way as Open-Meteo/forecast providers.
    auto windCardinalFromDeg = [](double deg) -> String {
        if (isnan(deg))
            return "";
        static const char *labels[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        double normalized = fmod(deg, 360.0);
        if (normalized < 0)
            normalized += 360.0;
        int idx = static_cast<int>(floor((normalized + 22.5) / 45.0)) % 8;
        return String(labels[idx]);
    };

    currentCond.temp = tempC;
    currentCond.feelsLike = feelsC;
    currentCond.dewPoint = NAN;
    currentCond.humidity = isnan(humidity) ? -1 : static_cast<int>(lround(humidity));
    currentCond.pressure = pressure;
    currentCond.windAvg = windSpeed;
    currentCond.windGust = windGust;
    currentCond.windDir = windDir;
    currentCond.uv = -1;
    currentCond.precipProb = -1;
    currentCond.cond = str_Weather_Conditions_Des.length() ? str_Weather_Conditions_Des : str_Weather_Conditions;
    currentCond.icon = str_Weather_Icon;
    currentCond.windCardinal = windCardinalFromDeg(windDir);
    currentCond.time = isnan(obsTime) ? 0U : static_cast<uint32_t>(obsTime);

    tempest.temperature = currentCond.temp;
    tempest.humidity = (currentCond.humidity >= 0) ? static_cast<double>(currentCond.humidity) : NAN;
    tempest.pressure = currentCond.pressure;
    tempest.windAvg = currentCond.windAvg;
    tempest.windGust = currentCond.windGust;
    tempest.windDir = currentCond.windDir;
    tempest.obsWindAvg = currentCond.windAvg;
    tempest.obsWindDir = currentCond.windDir;
    tempest.obsEpoch = currentCond.time;
    tempest.epoch = currentCond.time;
    tempest.lastUpdate = millis();
    forecast.lastUpdate = tempest.lastUpdate;

    Serial.println("Weather Updated:");
    Serial.printf("  Temp: %.1fC | Hum: %s%% | Wind: %.2fm/s | Source: %s\n",
                  tempC, str_Humd.c_str(), windSpeed, hasValidCoords ? "lat/lon" : "city");

    String forecastUrl = "http://api.openweathermap.org/data/2.5/forecast?" + buildOwmQuery();
    String forecastJson = httpGETRequest(forecastUrl.c_str());
    if (forecastJson != "{}") {
        auto extractJsonStringValueLocal = [](const String &src, const char *key) -> String {
            String needle = "\"" + String(key) + "\"";
            int keyIdx = src.indexOf(needle);
            if (keyIdx < 0) return "";
            int colonIdx = src.indexOf(':', keyIdx + needle.length());
            if (colonIdx < 0) return "";
            int pos = colonIdx + 1;
            while (pos < src.length() && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\r' || src[pos] == '\n')) ++pos;
            if (pos >= src.length()) return "";

            if (src[pos] == '"') {
                ++pos;
                int end = pos;
                bool escaped = false;
                while (end < src.length()) {
                    char c = src[end];
                    if (escaped) escaped = false;
                    else if (c == '\\') escaped = true;
                    else if (c == '"') break;
                    ++end;
                }
                if (end <= src.length()) return src.substring(pos, end);
                return "";
            }

            int end = pos;
            while (end < src.length() && src[end] != ',' && src[end] != '}' && src[end] != '\r' && src[end] != '\n') ++end;
            String out = src.substring(pos, end);
            out.trim();
            return out;
        };

        String forecastCod = extractJsonStringValueLocal(forecastJson, "cod");
        String forecastMsg = extractJsonStringValueLocal(forecastJson, "message");
        if (forecastCod.length() > 0 && forecastCod != "200") {
            Serial.printf("[OWM] Forecast API error cod=%s message=%s\n",
                          forecastCod.c_str(),
                          forecastMsg.c_str());
            needScrollRebuild = true;
            return;
        }

        auto sanitizeBools = [](String &s) {
            s.replace(":true", ":1");
            s.replace(":false", ":0");
        };
        auto extractJsonObjectLocal = [](const String &src, const char *key) -> String {
            int keyIdx = src.indexOf(key);
            if (keyIdx < 0) return "";
            int start = src.indexOf('{', keyIdx);
            if (start < 0) return "";
            int depth = 1;
            bool inString = false;
            bool escaped = false;
            for (int i = start + 1; i < src.length(); ++i) {
                char c = src[i];
                if (inString) {
                    if (escaped) escaped = false;
                    else if (c == '\\') escaped = true;
                    else if (c == '"') inString = false;
                    continue;
                }
                if (c == '"') inString = true;
                else if (c == '{') depth++;
                else if (c == '}') {
                    depth--;
                    if (depth == 0) return src.substring(start, i + 1);
                }
            }
            return "";
        };
        auto extractJsonArrayLocal = [](const String &src, const char *key) -> String {
            int keyIdx = src.indexOf(key);
            if (keyIdx < 0) return "";
            int start = src.indexOf('[', keyIdx);
            if (start < 0) return "";
            int depth = 0;
            bool inString = false;
            bool escaped = false;
            for (int i = start; i < src.length(); ++i) {
                char c = src[i];
                if (inString) {
                    if (escaped) escaped = false;
                    else if (c == '\\') escaped = true;
                    else if (c == '"') inString = false;
                    continue;
                }
                if (c == '"') inString = true;
                else if (c == '[') depth++;
                else if (c == ']') {
                    depth--;
                    if (depth == 0) return src.substring(start, i + 1);
                }
            }
            return "";
        };

        String cityObjStr = extractJsonObjectLocal(forecastJson, "\"city\"");
        uint32_t citySunrise = 0;
        uint32_t citySunset = 0;
        if (cityObjStr.length() > 0) {
            sanitizeBools(cityObjStr);
            JSONVar cityBlock = JSON.parse(cityObjStr);
            if (JSON.typeof_(cityBlock) == "object") {
                citySunrise = static_cast<uint32_t>(readNumber(cityBlock, "sunrise"));
                citySunset = static_cast<uint32_t>(readNumber(cityBlock, "sunset"));
            }
        }

        String listArrayStr = extractJsonArrayLocal(forecastJson, "\"list\"");
        if (listArrayStr.length() == 0) {
            Serial.printf("[OWM] Forecast list missing cod=%s message=%s\n",
                          forecastCod.length() ? forecastCod.c_str() : "n/a",
                          forecastMsg.length() ? forecastMsg.c_str() : "n/a");
        } else {
            ForecastData parsedForecast{};
            parsedForecast.lastUpdate = forecast.lastUpdate;

            int dayBestNoonDelta[MAX_FORECAST_DAYS];
            for (int i = 0; i < MAX_FORECAST_DAYS; ++i) {
                dayBestNoonDelta[i] = 9999;
            }

            auto upsertDay = [&](const ForecastHour &hour, double highC, double lowC, int rainChance, const String &cond, const String &icon) {
                time_t tt = static_cast<time_t>(hour.time);
                struct tm *ti = localtime(&tt);
                if (!ti) return;

                int dayIdx = -1;
                for (int i = 0; i < parsedForecast.numDays; ++i) {
                    if (parsedForecast.days[i].yearNum == ti->tm_year + 1900 &&
                        parsedForecast.days[i].monthNum == ti->tm_mon + 1 &&
                        parsedForecast.days[i].dayNum == ti->tm_mday) {
                        dayIdx = i;
                        break;
                    }
                }
                if (dayIdx < 0) {
                    if (parsedForecast.numDays >= MAX_FORECAST_DAYS) return;
                    dayIdx = parsedForecast.numDays++;
                    parsedForecast.days[dayIdx] = ForecastDay{};
                    parsedForecast.days[dayIdx].dayNum = ti->tm_mday;
                    parsedForecast.days[dayIdx].monthNum = ti->tm_mon + 1;
                    parsedForecast.days[dayIdx].yearNum = ti->tm_year + 1900;
                    parsedForecast.days[dayIdx].highTemp = NAN;
                    parsedForecast.days[dayIdx].lowTemp = NAN;
                    parsedForecast.days[dayIdx].rainChance = -1;
                    parsedForecast.days[dayIdx].sunrise = citySunrise;
                    parsedForecast.days[dayIdx].sunset = citySunset;
                }

                ForecastDay &day = parsedForecast.days[dayIdx];
                if (isnan(day.highTemp) || (!isnan(highC) && highC > day.highTemp)) day.highTemp = highC;
                if (isnan(day.lowTemp) || (!isnan(lowC) && lowC < day.lowTemp)) day.lowTemp = lowC;
                if (rainChance > day.rainChance) day.rainChance = rainChance;

                const int noonDelta = abs(ti->tm_hour * 60 + ti->tm_min - (12 * 60));
                if (day.conditions.length() == 0 || noonDelta < dayBestNoonDelta[dayIdx]) {
                    dayBestNoonDelta[dayIdx] = noonDelta;
                    day.conditions = cond;
                    day.icon = icon;
                }
            };

            const int n = listArrayStr.length();
            int i = 0;
            while (i < n) {
                while (i < n && listArrayStr[i] != '{') ++i;
                if (i >= n) break;
                int start = i;
                int depth = 0;
                bool inString = false;
                bool escaped = false;
                for (; i < n; ++i) {
                    char c = listArrayStr[i];
                    if (inString) {
                        if (escaped) escaped = false;
                        else if (c == '\\') escaped = true;
                        else if (c == '"') inString = false;
                        continue;
                    }
                    if (c == '"') inString = true;
                    else if (c == '{') depth++;
                    else if (c == '}') {
                        depth--;
                        if (depth == 0) {
                            String objStr = listArrayStr.substring(start, i + 1);
                            sanitizeBools(objStr);
                            JSONVar item = JSON.parse(objStr);
                            if (JSON.typeof_(item) == "object") {
                                JSONVar itemMain = item["main"];
                                JSONVar weather0 = item["weather"][0];
                                uint32_t when = static_cast<uint32_t>(readNumber(item, "dt"));
                                if (when > 0) {
                                    ForecastHour hour;
                                    hour.time = when;
                                    hour.temp = toCelsius(readNumber(itemMain, "temp"));
                                    double pop = readNumber(item, "pop");
                                    hour.rainChance = isnan(pop) ? -1 : static_cast<int>(lround(pop * 100.0));
                                    hour.conditions = (JSON.typeof_(weather0["description"]) == "string") ? String((const char*)weather0["description"]) : "";
                                    hour.icon = (JSON.typeof_(weather0["icon"]) == "string") ? String((const char*)weather0["icon"]) : "";

                                    if (parsedForecast.numHours < MAX_FORECAST_HOURS) {
                                        parsedForecast.hours[parsedForecast.numHours++] = hour;
                                    }

                                    double highC = toCelsius(readNumber(itemMain, "temp_max"));
                                    double lowC = toCelsius(readNumber(itemMain, "temp_min"));
                                    if (isnan(highC)) highC = hour.temp;
                                    if (isnan(lowC)) lowC = hour.temp;
                                    upsertDay(hour, highC, lowC, hour.rainChance, hour.conditions, hour.icon);
                                }
                            }
                            ++i;
                            break;
                        }
                    }
                }
            }

            parsedForecast.hourlyKeyPresent = (parsedForecast.numHours > 0);
            if (parsedForecast.numDays > 0 || parsedForecast.numHours > 0) {
                parsedForecast.lastUpdate = forecast.lastUpdate;
                forecast = parsedForecast;
                Serial.printf("[OWM] Forecast parsed days=%d hours=%d\n",
                              forecast.numDays,
                              forecast.numHours);
            } else {
                Serial.println("[OWM] Forecast parse produced no usable entries; keeping previous forecast");
            }
        }
    } else {
        Serial.println("[OWM] Forecast request returned empty payload");
    }

    needScrollRebuild = true;
    if (showedBusy) refreshVisibleScreen();
}

void drawOWMScreen()
{
    static unsigned long s_lastOwmFetchAttemptMs = 0;

    // Keep OWM text rendering deterministic even if prior screens changed font.
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    dma_display->setTextWrap(false);

    // OWM layout draws in 7px text lanes (y: 0,9,17,25) plus 16x16 icon.
    // Clear separator gaps so stale pixels cannot persist at fixed coordinates.
    dma_display->fillRect(0, 7, PANEL_RES_X, 2, myBLACK);   // gap between temp lane and clock lane
    dma_display->fillRect(0, 16, PANEL_RES_X, 1, myBLACK);  // gap between clock lane and date lane
    dma_display->fillRect(0, 24, PANEL_RES_X, 1, myBLACK);  // gap between date lane and scroll lane
    dma_display->fillRect(16, 0, 2, 16, myBLACK);           // spacer between icon and right text lane

    getTimeFromRTC();
    displayClock();
    displayDate();
    displayWeatherData();

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

    // Update OWM weather with interval-based scheduling so fetch is not missed
    // by frame timing drift. Forecast-model sources are refreshed elsewhere.
    if (isDataSourceOwm())
    {
        const unsigned long nowMs = millis();
        const bool missingOwmData = (str_Temp.length() == 0 || str_Temp == "--");
        const unsigned long refreshInterval = 10UL * 60UL * 1000UL; // 10 minutes
        const unsigned long bootstrapInterval = 60UL * 1000UL;       // retry quickly until first data
        const unsigned long dueInterval = missingOwmData ? bootstrapInterval : refreshInterval;

        if ((s_lastOwmFetchAttemptMs == 0) || (nowMs - s_lastOwmFetchAttemptMs >= dueInterval))
        {
            s_lastOwmFetchAttemptMs = nowMs;
            // Screen-owned OWM refreshes should stay quiet to avoid overlay flicker
            // while the OWM screen itself is rendering.
            fetchWeatherFromOWM(false);
            reset_Time_and_Date_Display = true;
            displayWeatherData();
            needScrollRebuild = true; // new values -> refresh marquee text
        }
    }
}

void drawWeatherIcon(String iconCode)
{
    dma_display->fillRect(0, 0, 16, 16, myBLACK);
    dma_display->setCursor(1, 4);
    dma_display->setTextColor(theme == 1 ? dma_display->color565(110, 110, 180) : myYELLOW);

    uint16_t iconColor = getDayNightColorFromCode(iconCode);
    ui_theme::drawBitmapThemed(0, 0, getWeatherIconFromCode(iconCode), 16, 16, iconColor);

    // OWM few-clouds cleanup: remove known artifact lines.
    if (iconCode.startsWith("02d") || iconCode.startsWith("02n"))
    {
        // Remove vertical line (3,3) -> (3,7)
        dma_display->drawFastVLine(3, 3, 5, myBLACK);
        // Remove horizontal line (6,10) -> (10,10)
        dma_display->drawFastHLine(6, 10, 5, myBLACK);
        // Make cloud body one pixel thicker.
        dma_display->drawFastHLine(6, 11, 5, iconColor);
    }

    // Enforce crescent in the sun position for all night icons.
    const bool isNightIcon = iconCode.endsWith("n") || iconCode.indexOf("night") >= 0;
    if (isNightIcon)
    {
        uint16_t moonColor = ui_theme::applyGraphicColor(dma_display->color565(235, 235, 180));
        // Clear likely sun area first, then draw crescent where sun normally appears.
        const int moonX = 11;
        const int moonY = 4;
        // Remove residual sun rays / light beams in a wider top region.
        dma_display->fillRect(4, 0, 12, 10, myBLACK);
        dma_display->fillCircle(moonX, moonY, 4, myBLACK);
        dma_display->fillCircle(moonX, moonY, 3, moonColor);
        dma_display->fillCircle(moonX + 1, moonY - 1, 3, myBLACK);
    }
}

void displayClock()
{
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    dma_display->setTextWrap(false);

    const bool rightJustify = (currentScreen == SCREEN_OWM);
    int hour = atoi(chr_t_hour);
    bool isPM = false;
    if (!units.clock24h)
    {
        if (hour >= 12)
        {
            isPM = true;
            if (hour > 12)
                hour -= 12;
        }
        else if (hour == 0)
        {
            hour = 12;
        }
    }
    const uint16_t clockLineColor = rightJustify
                                        ? ((theme == 1) ? dma_display->color565(60, 60, 120)
                                                        : dma_display->color565(255, 255, 80))
                                        : ((theme == 1) ? dma_display->color565(90, 90, 150) : myRED);
    dma_display->setTextColor(clockLineColor);
    if (rightJustify)
    {
        char hmBuf[8];
        snprintf(hmBuf, sizeof(hmBuf), "%02d:%s", hour, chr_t_minute);

        const int hmW = getTextWidth(hmBuf);
        const int secondGap = 1;
        const int secondW = 7; // 2x 3px digits + 1px gap
        const int ampmW = units.clock24h ? 0 : 3;
        const int ampmGap = units.clock24h ? 0 : 1;
        const int totalW = hmW + secondGap + secondW + ampmGap + ampmW;
        int x = 64 - totalW;
        x -= 6;
        if (x < 0)
            x = 0;

        dma_display->fillRect(0, 9, 64, 7, myBLACK);
        dma_display->setCursor(x, 9);
        dma_display->print(hmBuf);

        const int secondX = x + hmW + secondGap + 1;
        const int secondY = 11;
        wxv::seg7::Metrics secondMetrics;
        secondMetrics.digitWidth = 3;
        secondMetrics.digitHeight = 5;
        secondMetrics.colonWidth = 1;
        secondMetrics.spacing = 1;
        wxv::seg7::drawDigit(secondX, secondY, chr_t_second[0], clockLineColor, 1, secondMetrics);
        wxv::seg7::drawDigit(secondX + 4, secondY, chr_t_second[1], clockLineColor, 1, secondMetrics);

        if (!units.clock24h)
        {
            wxv::seg7::drawDigit(secondX + secondW + ampmGap + 2, secondY, isPM ? 'P' : 'A',
                                 clockLineColor, 1, secondMetrics);
        }
    }
    else
    {
        dma_display->fillRect(15, 9, 45, 7, myBLACK);
        dma_display->setCursor(16, 9);
        dma_display->printf("%02d:", hour);
        dma_display->print(chr_t_minute);
        dma_display->print(":");
        dma_display->print(chr_t_second);
        dma_display->fillRect(59, 9, 5, 7, myBLACK);
        if (!units.clock24h)
        {
            dma_display->setCursor(59, 9);
            dma_display->print(isPM ? "P" : "A");
        }
    }
}

void displayDate()
{
    const bool rightJustify = (currentScreen == SCREEN_OWM);
    dma_display->fillRect(0, 17, 64, 7, myBLACK);
    const char *dayStr = daysOfTheWeek[d_daysOfTheWeek];
    char dateSuffix[10];
    snprintf(dateSuffix, sizeof(dateSuffix), " %02d/%02d", d_month, d_day);
    char dateStr[14];
    snprintf(dateStr, sizeof(dateStr), "%s%s", dayStr, dateSuffix);

    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    uint16_t dateColor = rightJustify
                             ? ((theme == 1) ? dma_display->color565(95, 95, 170)
                                             : dma_display->color565(255, 215, 120))
                             : ((theme == 1) ? dma_display->color565(60, 60, 120)
                                             : dma_display->color565(150, 200, 255));
    uint16_t sundayColor = (theme == 1) ? dma_display->color565(180, 80, 120)
                                        : dma_display->color565(255, 80, 120);
    uint16_t saturdayColor = (theme == 1) ? dma_display->color565(80, 140, 200)
                                          : dma_display->color565(80, 180, 255);

    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
    const int wifiIconW = 7;
    const int wifiIconGap = 2;
    const bool showWifiIcon = rightJustify && (WiFi.status() == WL_CONNECTED);
    int dateX = rightJustify ? 1 : ((64 - static_cast<int>(w)) / 2);
    if (dateX < 0)
        dateX = 0;

    if (showWifiIcon)
    {
        const int wifiX = 56;
        const int wifiY = 17;
        uint16_t wifiDim = (theme == 1)
                               ? dma_display->color565(35, 35, 60)
                               : dma_display->color565(80, 80, 80);
        uint16_t wifiActive = (theme == 1)
                                  ? dma_display->color565(90, 140, 200)
                                  : dma_display->color565(100, 255, 120);
        drawWiFiIcon(wifiX, wifiY, wifiDim, wifiActive, WiFi.RSSI());
    }

    dma_display->setCursor(dateX, 17);
    if (d_daysOfTheWeek == 0)
    {
        dma_display->setTextColor(sundayColor);
        dma_display->print(dayStr);
        dma_display->setTextColor(dateColor);
        dma_display->print(dateSuffix);
    }
    else if (d_daysOfTheWeek == 6)
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
}

void displayWeatherData()
{
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    dma_display->setTextWrap(false);

    if (isDataSourceNone())
    {
        dma_display->fillRect(0, 0, 64, 7, myBLACK);
        return;
    }

    if (isDataSourceForecastModel())
    {
        drawWeatherFlowIcon();
    }
    else
    {
        drawWeatherIcon(str_Weather_Icon);
    }

    // Keep a dedicated text lane on the right so it never overlaps the weather icon.
    const int textLaneX = 18;
    const int textLaneW = 64 - textLaneX;
    dma_display->fillRect(textLaneX, 0, textLaneW, 7, myBLACK);

    String outdoorTemp = formatOutdoorTemperature();
    int tempWidth = getTextWidth(outdoorTemp.c_str());
    int tempX = 64 - tempWidth;
    if (currentScreen == SCREEN_OWM)
        tempX -= 1;
    if (tempX < textLaneX)
        tempX = textLaneX;

    dma_display->setCursor(tempX, 0);
    dma_display->setTextColor((theme == 1) ? dma_display->color565(60, 60, 120)
                                           : dma_display->color565(200, 200, 255));
    dma_display->print(outdoorTemp);

    String humidityStr = formatOutdoorHumidity();
    if (humidityStr != "--")
    {
        String humidityText = humidityStr + "%";
        int humidityWidth = getTextWidth(humidityText.c_str());
        int humidityX = tempX - humidityWidth - 2 - 7;
        if (humidityX < textLaneX)
            humidityX = textLaneX;

        dma_display->setCursor(humidityX, 0);
        dma_display->setTextColor((theme == 1) ? dma_display->color565(90, 90, 150)
                                               : dma_display->color565(150, 200, 255));
        dma_display->print(humidityText);
    }
}

void createScrollingText()
{
    if (!(isDataSourceOwm() || isDataSourceForecastModel()))
    {
        scrolling_Text = "";
        text_Length_In_Pixel = 0;
        return;
    }

    auto tempTextOrDash = [](double valueC) -> String {
        return isnan(valueC) ? String("--") : fmtTemp(valueC, 0);
    };
    auto pressTextOrDash = [](double valueHpa) -> String {
        return isnan(valueHpa) ? String("--") : fmtPress(valueHpa, 0);
    };
    auto windTextOrDash = [](double valueMps) -> String {
        return isnan(valueMps) ? String("--") : fmtWind(valueMps, 1);
    };

    auto nonEmptyOrDash = [](String text) -> String {
        text.trim();
        return text.length() ? text : String("--");
    };

    String provider = isDataSourceWeatherFlow() ? String("WeatherFlow")
                    : (isDataSourceOpenMeteo() ? String("Open-Meteo")
                                               : String("OWM"));
    String weatherText = String("--");
    String feelsText = String("--");
    String maxText = String("--");
    String minText = String("--");
    String pressureText = String("--");
    String windText = String("--");
    String cityText = String("--");

    if (isDataSourceForecastModel())
    {
        weatherText = nonEmptyOrDash(currentCond.cond.length() ? currentCond.cond : currentCond.icon);
        feelsText = tempTextOrDash(currentCond.feelsLike);
        pressureText = pressTextOrDash(currentCond.pressure);
        windText = windTextOrDash(currentCond.windAvg);
        if (forecast.numDays > 0)
        {
            maxText = tempTextOrDash(forecast.days[0].highTemp);
            minText = tempTextOrDash(forecast.days[0].lowTemp);
        }
    }
    else
    {
        cityText = nonEmptyOrDash(str_City);
        weatherText = nonEmptyOrDash(str_Weather_Conditions_Des.length() ? str_Weather_Conditions_Des : str_Weather_Conditions);
        if (str_Feels_like.length() > 0 && str_Feels_like != "--")
            feelsText = fmtTemp(atof(str_Feels_like.c_str()), 0);
        if (str_Temp_max.length() > 0 && str_Temp_max != "--")
            maxText = fmtTemp(atof(str_Temp_max.c_str()), 0);
        if (str_Temp_min.length() > 0 && str_Temp_min != "--")
            minText = fmtTemp(atof(str_Temp_min.c_str()), 0);
        if (str_Pressure.length() > 0 && str_Pressure != "--")
            pressureText = fmtPress(atof(str_Pressure.c_str()), 0);
        if (str_Wind_Speed.length() > 0 && str_Wind_Speed != "--")
            windText = fmtWind(atof(str_Wind_Speed.c_str()), 1);
    }

    provider = nonEmptyOrDash(provider);
    weatherText = nonEmptyOrDash(weatherText);
    feelsText = nonEmptyOrDash(feelsText);
    maxText = nonEmptyOrDash(maxText);
    minText = nonEmptyOrDash(minText);
    pressureText = nonEmptyOrDash(pressureText);
    windText = nonEmptyOrDash(windText);
    cityText = nonEmptyOrDash(cityText);

    if (isDataSourceOwm())
    {
        scrolling_Text =
            "Src: " + provider + " Â¦ " +
            "City: " + cityText + " Â¦ " +
            "Weather: " + weatherText + " Â¦ " +
            "Feels Like: " + feelsText + " Â¦ " +
            "Max: " + maxText + "  Â¦ Min: " + minText + " Â¦ " +
            "Pressure: " + pressureText + " Â¦ " +
            "Wind: " + windText + " Â¦ ";
    }
    else
    {
        scrolling_Text =
            "Src: " + provider + " Â¦ " +
            "Weather: " + weatherText + " Â¦ " +
            "Feels Like: " + feelsText + " Â¦ " +
            "Max: " + maxText + "  Â¦ Min: " + minText + " Â¦ " +
            "Pressure: " + pressureText + " Â¦ " +
            "Wind: " + windText + " Â¦ ";
    }

    scrolling_Text_Color = (theme == 1) ? dma_display->color565(60, 60, 120)
                                        : dma_display->color565(150, 200, 255);
    text_Length_In_Pixel = getTextWidth(scrolling_Text.c_str());
}

// --- Scroll state variables for weather ---
void scrollWeatherDetails()
{
    if (!(isDataSourceOwm() || isDataSourceForecastModel()))
        return;

    static unsigned long lastScrollTime = 0;
    static int scrollOffset = 0;

    if (!start_Scroll_Text)
    {
        String unitT = useImperial ? "Â°F" : "Â°C";
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
            (theme == 1) ? dma_display->color565(60, 60, 120)
                         : dma_display->color565(150, 200, 255);
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

void drawConditionSceneScreen()
{
    String condition;
    bool hasData = true;

    getTimeFromRTC();

    if (isDataSourceForecastModel())
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

    uint16_t accent = weatherSceneAccentColor(sceneKind);
    if (theme == 1)
        accent = scaleColor565(accent, 0.60f);

    String tempTag;
    if (isDataSourceForecastModel())
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
        // Match Prediction style: large value + smaller degree/unit.
        char valueText[16] = {0};
        char unitChar = (units.temp == TempUnit::F) ? 'F' : 'C';
        size_t vIdx = 0;
        for (int i = 0; i < tempTag.length() && vIdx < sizeof(valueText) - 1; ++i)
        {
            char c = tempTag.charAt(i);
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.')
            {
                valueText[vIdx++] = c;
            }
            else if (c == 'C' || c == 'F')
            {
                unitChar = c;
            }
        }
        valueText[vIdx] = '\0';
        if (vIdx == 0)
        {
            strncpy(valueText, "--", sizeof(valueText) - 1);
            valueText[sizeof(valueText) - 1] = '\0';
        }

        dma_display->setFont(&verdanab8pt7b);
        dma_display->setTextSize(1);
        int16_t vX1, vY1;
        uint16_t vW, vH;
        dma_display->getTextBounds(valueText, 0, 0, &vX1, &vY1, &vW, &vH);

        char unitText[4] = {'\xB0', unitChar, '\0'};
        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);
        int16_t uX1, uY1;
        uint16_t uW, uH;
        dma_display->getTextBounds(unitText, 0, 0, &uX1, &uY1, &uW, &uH);

        const int unitGap = 1;
        int totalW = static_cast<int>(vW) + unitGap + static_cast<int>(uW);

        const int topPad = 1;
        int tempX = 2;
        int valueY = topPad - vY1;
        int unitX = tempX + static_cast<int>(vW) + unitGap;
        int unitY = topPad + 1;

        uint16_t valueColor = weatherSceneAdaptiveTempTextColor(sceneKind, accent, false);
        uint16_t unitColor = weatherSceneAdaptiveTempTextColor(sceneKind, accent, true);
        uint16_t shadow = scaleColor565(weatherSceneTempBgColor(sceneKind), weatherSceneIsNight(sceneKind) ? 0.45f : 0.72f);
        dma_display->setFont(&verdanab8pt7b);
        dma_display->setTextSize(1);
        dma_display->setTextColor(shadow);
        dma_display->setCursor(tempX + 1, valueY + 1);
        dma_display->print(valueText);
        dma_display->setTextColor(valueColor);
        dma_display->setCursor(tempX, valueY);
        dma_display->print(valueText);

        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);
        dma_display->setTextColor(shadow);
        dma_display->setCursor(unitX + 1, unitY + 1);
        dma_display->print(unitText);
        dma_display->setTextColor(unitColor);
        dma_display->setCursor(unitX, unitY);
        dma_display->print(unitText);

        // Restore small font for marquee rendering below.
        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);
    }

    drawConditionSceneClockOverlay(sceneKind);

    // Use the shared scene-marquee state engine so draw/tick update the same buffer.
    conditionSceneSyncMarquee(label, accent);
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

    const bool showedBusy = dma_display != nullptr && !isSplashActive();
    if (showedBusy)
    {
        wxv::notify::showNotification(wxv::notify::NotifyId::Busy, myCYAN, myWHITE, "REFRESH");
    }

    // Rebuild the line using the latest units + data
    createScrollingText();

    // Reset marquee state so it restarts smoothly
    start_Scroll_Text = false;
    set_up_Scrolling_Text_Length = true;
    text_Length_In_Pixel = getTextWidth(scrolling_Text.c_str());

    needScrollRebuild = false;
    if (showedBusy)
    {
        refreshVisibleScreen();
    }
}

void applyUnitPreferences()
{
    useImperial = (units.temp == TempUnit::F);
    notifyUnitsMaybeChanged();
}
static unsigned long lunarLuckBaseScrollIntervalMs()
{
    const int baseGlobalMs = constrain(scrollSpeed, 12, 240);
    const float scaled = static_cast<float>(baseGlobalMs) * LUNAR_LUCK_SCROLL_FACTOR;
    return static_cast<unsigned long>(constrain(static_cast<int>(scaled), 10, 120));
}
