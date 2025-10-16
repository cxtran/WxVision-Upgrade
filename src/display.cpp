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

#include "tempest.h"
#include "weather_countries.h"

extern float aht20_temp;
extern float SCD40_temp;
extern int scrollLevel;
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

bool screenIsAllowed(ScreenMode mode)
{
    switch (mode)
    {
    case SCREEN_OWM:
        return isDataSourceOwm();
    case SCREEN_UDP_DATA:
    case SCREEN_UDP_FORECAST:
    case SCREEN_RAPID_WIND:
    case SCREEN_WIND_DIR:
    case SCREEN_CURRENT:
    case SCREEN_HOURLY:
        return isDataSourceWeatherFlow();
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
static uint16_t splashStatusBg = 0;
static uint16_t splashShadow = 0;
static uint16_t splashMinimumMs = 0;
static unsigned long splashStartMs = 0;
static uint16_t splashIdle = 0;
static uint16_t splashHighlight = 0;
static float splashPrevProgress = 0.0f;
static uint8_t splashGradStartR = 0;
static uint8_t splashGradStartG = 0;
static uint8_t splashGradStartB = 0;
static uint8_t splashGradStepR = 0;
static uint8_t splashGradStepG = 0;
static uint8_t splashGradStepB = 0;
static const int SPLASH_BAR_X = 6;
static const int SPLASH_BAR_Y = 25;
static const int SPLASH_BAR_W = PANEL_RES_X - 12;
static const int SPLASH_BAR_H = 5;
static const int SPLASH_STATUS_BASELINE = SPLASH_BAR_Y - 2;

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

static void drawSplashBackdrop()
{
    for (int y = 0; y < PANEL_RES_Y; ++y)
    {
        dma_display->drawFastHLine(0, y, PANEL_RES_X, splashRowColor(y));
    }

    dma_display->drawFastHLine(0, 0, PANEL_RES_X, splashHighlight);
}

static void drawSplashBranding()
{
    // Halo behind the weather icon
    dma_display->fillRect(4, 6, 20, 20, splashShadow);

    dma_display->drawBitmap(7, 9, icon_clear, 16, 16, splashShadow);
    dma_display->drawBitmap(6, 8, icon_clear, 16, 16, splashAccent);

    dma_display->setTextWrap(false);
    dma_display->setTextSize(1);

    const char *titleMain = "Vision";
    const char *titleAccent = "WX";
    int titleWidth = getTextWidth("VisionWX");
    int titleX = (PANEL_RES_X - titleWidth) / 2;
    if (titleX < 2)
        titleX = 2;
    dma_display->setCursor(titleX, 12);
    dma_display->setTextColor(myWHITE);
    dma_display->print(titleMain);
    dma_display->setTextColor(splashAccent);
    dma_display->print(titleAccent);

    String tagline = "Weather Hub";
    int tagWidth = getTextWidth(tagline.c_str());
    int tagX = (PANEL_RES_X - tagWidth) / 2;
    if (tagX < 2)
        tagX = 2;
    dma_display->setCursor(tagX, 20);
    dma_display->setTextColor(myWHITE);
    dma_display->print(tagline);
}

static void renderSplashFrame()
{
    dma_display->drawRoundRect(SPLASH_BAR_X, SPLASH_BAR_Y, SPLASH_BAR_W, SPLASH_BAR_H, 2, myWHITE);
    dma_display->fillRect(SPLASH_BAR_X + 1, SPLASH_BAR_Y + 1, SPLASH_BAR_W - 2, SPLASH_BAR_H - 2, splashIdle);
}

static void renderSplashBadge(uint8_t step, uint8_t total)
{
    const int badgeX = PANEL_RES_X - 20;
    const int badgeY = 2;
    const int badgeW = 18;
    const int badgeH = 10;

    for (int y = badgeY; y < badgeY + badgeH; ++y)
    {
        dma_display->drawFastHLine(badgeX, y, badgeW, splashRowColor(y));
    }

    if (total <= 1)
        return;

    if (step > total)
        step = total;

    dma_display->drawRoundRect(badgeX, badgeY, badgeW, badgeH, 2, splashAccent);
    dma_display->fillRect(badgeX + 1, badgeY + 1, badgeW - 2, badgeH - 2, splashShadow);

    char buf[10];
    snprintf(buf, sizeof(buf), "%u/%u", step, total);
    int textWidth = getTextWidth(buf);
    int cursorX = badgeX + (badgeW - textWidth) / 2;
    if (cursorX < badgeX + 1)
        cursorX = badgeX + 1;
    dma_display->setCursor(cursorX, badgeY + badgeH - 2);
    dma_display->setTextSize(1);
    dma_display->setTextColor(myWHITE);
    dma_display->print(buf);
}

static void renderSplashBar(float progress, bool animate)
{
    if (!dma_display)
        return;

    if (progress < 0.0f)
        progress = 0.0f;
    if (progress > 1.0f)
        progress = 1.0f;

    if (progress < 0.0f)
        progress = 0.0f;
    if (progress > 1.0f)
        progress = 1.0f;

    const int barInnerX = SPLASH_BAR_X + 1;
    const int barInnerY = SPLASH_BAR_Y + 1;
    const int barInnerW = SPLASH_BAR_W - 2;
    const int barInnerH = SPLASH_BAR_H - 2;

    int prevPixels = static_cast<int>(barInnerW * splashPrevProgress + 0.5f);
    if (prevPixels < 0)
        prevPixels = 0;
    if (prevPixels > barInnerW)
        prevPixels = barInnerW;

    int targetPixels = static_cast<int>(barInnerW * progress + 0.5f);
    if (targetPixels < 0)
        targetPixels = 0;
    if (targetPixels > barInnerW)
        targetPixels = barInnerW;

    // If progress moved backwards (shouldn't happen) redraw everything.
    if (targetPixels < prevPixels)
    {
        prevPixels = 0;
        dma_display->fillRect(barInnerX, barInnerY, barInnerW, barInnerH, splashStatusBg);
    }

    // Ensure base fill exists before animating.
    if (prevPixels == 0 && targetPixels >= 0)
    {
        dma_display->fillRect(barInnerX, barInnerY, barInnerW, barInnerH, splashStatusBg);
    }

    if (animate && targetPixels > prevPixels)
    {
        for (int x = prevPixels; x < targetPixels; ++x)
        {
            dma_display->drawFastVLine(barInnerX + x, barInnerY, barInnerH, splashAccent);
            dma_display->drawPixel(barInnerX + x, barInnerY, splashHighlight);
            delay(10);
        }
    }
    else if (targetPixels > prevPixels)
    {
        dma_display->fillRect(barInnerX + prevPixels, barInnerY, targetPixels - prevPixels, barInnerH, splashAccent);
        dma_display->drawFastHLine(barInnerX + prevPixels, barInnerY, targetPixels - prevPixels, splashHighlight);
    }

    if (targetPixels < barInnerW)
    {
        dma_display->fillRect(barInnerX + targetPixels, barInnerY, barInnerW - targetPixels, barInnerH, splashIdle);
    }

    splashPrevProgress = progress;
}

static void renderSplashStatusText(const String &rawStatus)
{
    if (!dma_display)
        return;

    String status = rawStatus;
    status.trim();
    if (status.isEmpty())
        status = "Starting...";
    if (status.length() > 18)
        status.remove(18);

    dma_display->fillRect(0, SPLASH_STATUS_BASELINE - 7, PANEL_RES_X, 8, splashShadow);

    int textWidth = getTextWidth(status.c_str());
    int cursorX = (PANEL_RES_X - textWidth) / 2;
    if (cursorX < 2)
        cursorX = 2;

    dma_display->setCursor(cursorX, SPLASH_STATUS_BASELINE);
    dma_display->setTextSize(1);
    dma_display->setTextColor(myWHITE);
    dma_display->print(status);
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
        // Monochrome theme palette
        splashAccent = dma_display->color565(210, 210, 230);
        splashStatusBg = dma_display->color565(40, 44, 68);
        splashShadow = dma_display->color565(18, 20, 34);
        splashIdle = dma_display->color565(26, 28, 44);
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
        // Color theme palette
        splashAccent = dma_display->color565(90, 210, 255);
        splashStatusBg = dma_display->color565(12, 40, 70);
        splashShadow = dma_display->color565(8, 22, 38);
        splashIdle = dma_display->color565(16, 32, 54);
        splashHighlight = dma_display->color565(230, 250, 255);
        splashGradStartR = 10;
        splashGradStartG = 26;
        splashGradStartB = 40;
        splashGradStepR = 2;
        splashGradStepG = 2;
        splashGradStepB = 3;
    }
    splashPrevProgress = 0.0f;

    drawSplashBackdrop();
    drawSplashBranding();
    renderSplashFrame();
    renderSplashBar(0.0f, false);
    renderSplashBadge(0, 0);
    renderSplashStatusText("Starting...");
}

void splashUpdate(const char *status, uint8_t step, uint8_t total)
{
    if (!dma_display || !splashActive)
        return;

    if (total == 0)
        total = 1;
    if (step > total)
        step = total;

    float target = static_cast<float>(step) / static_cast<float>(total);
    if (target < 0.0f)
        target = 0.0f;
    if (target > 1.0f)
        target = 1.0f;

    String text = status ? String(status) : String("");
    text.trim();
    if (text.length() == 0)
    {
        text = "Working...";
    }

    renderSplashBar(target, true);
    renderSplashBadge(step, total);
    renderSplashStatusText(text);
    delay(25);
}

void splashEnd()
{
    if (!dma_display || !splashActive)
        return;

    while ((uint32_t)(millis() - splashStartMs) < splashMinimumMs)
    {
        delay(15);
    }

    uint8_t original = currentPanelBrightness;
    if (original == 0)
        original = map(brightness, 1, 100, 3, 255);

    for (int level = original; level > 15; level -= 15)
    {
        setPanelBrightness(level);
        delay(25);
    }
    dma_display->fillScreen(0);
    setPanelBrightness(original);
    splashActive = false;
    splashMinimumMs = 0;
    splashPrevProgress = 0.0f;
    dma_display->setTextColor(myWHITE);
    dma_display->setTextSize(1);
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
    if (JSON.typeof(data) == "undefined")
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
        if (JSON.typeof(obj) != "object")
            return NAN;
        JSONVar v = obj[key];
        if (JSON.typeof(v) == "undefined")
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

        dma_display->fillRect(0, 25, PANEL_RES_X, 7, myBLACK);
        dma_display->setCursor(-scrollOffset, 25);
        dma_display->setTextColor(scrolling_Text_Color);
        dma_display->print(scrolling_Text);
    }
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

    const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    char dateStr[14];
    snprintf(dateStr, sizeof(dateStr), "%s %02d/%02d",
             days[now.dayOfTheWeek()], now.month(), now.day());

    // ---- TIME (big Verdana Bold)
    dma_display->setFont(&verdanab8pt7b);
    dma_display->setTextSize(1);
    uint16_t timeColor = (theme == 1) ? dma_display->color565(60, 60, 120)
                                      : dma_display->color565(255, 255, 80);
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


    dma_display->setCursor(boxX, boxY + timeH - 1);
    dma_display->print(timeStr);

    // --- draw AM/PM inline
    if (!units.clock24h)
    {
        String ampmStr = isPM ? "PM" : "AM";
        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);
        


        /*
        uint16_t ampmColor = (theme == 1) ? dma_display->color565(60, 60, 120)
                                          : dma_display->color565(255, 255, 200);
        dma_display->setTextColor(ampmColor);
*/
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
        //       if (!isPM) ampmY -= 6;
        //       ampmY -= 1;
/*
        uint16_t bgColor = (theme == 1) ? dma_display->color565(20, 20, 40)
                                        : dma_display->color565(40, 40, 40);
        dma_display->fillRect(ampmX - 1, ampmY - ampmH + 6, ampmW + 2, ampmH + 2, bgColor);
*/

        uint16_t ampmColor, bgColor;

        if (theme == 1) {
            // Mono theme: both AM/PM are dim gray-blue
            ampmColor = dma_display->color565(100, 100, 140);
            bgColor   = dma_display->color565(20, 20, 40);
        }
        else {
            if (isPM) {
                // Evening / night → warm amber on darker background
                ampmColor = dma_display->color565(255, 170, 60);   // warm orange-gold text
                bgColor   = dma_display->color565(50, 30, 0);      // dusk background
            } else {
                // Morning / day → cool blue on soft gray background
                ampmColor = dma_display->color565(100, 200, 255);  // light sky-blue text
                bgColor   = dma_display->color565(10, 30, 50);     // early-morning gray-blue
            }
        }

        dma_display->setTextColor(ampmColor);
        dma_display->fillRect(ampmX - 1, ampmY - ampmH + 6, ampmW + 2, ampmH + 2, bgColor);


        dma_display->setCursor(ampmX, ampmY);
        dma_display->print(ampmStr);
    }
    // ---- Draw Wi-Fi icon if connected ----
    if (WiFi.status() == WL_CONNECTED)
    {
        // Position the Wi-Fi icon just above AM/PM

        int wifiX = 57;
        int wifiY = 7;
        /*
                    int wifiX = ampmX + 5;   // just after time text
                    int wifiY = ampmY - 8;        // above AM/PM
        */

        uint16_t wifiColor = (theme == 1)
                                 ? dma_display->color565(90, 90, 120)    // dim gray for mono
                                 : dma_display->color565(100, 255, 120); // soft green for color
        drawWiFiIcon(wifiX, wifiY, wifiColor);
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
    String localTempStr = fmtTemp(SCD40_temp, 0);        // Inside

    dma_display->fillRect(0, 0, 24, 7, myBLACK);

    if (showOutdoor)
    {
        dma_display->setCursor(0, 0);
        dma_display->print(outdoorTempStr);

        dma_display->getTextBounds(outdoorTempStr.c_str(), 0, 0, &x1, &y1, &w, &h);
        int sunX = w + 1;
        int sunY = 0;
        uint16_t sunColor = (theme == 1)
            ? dma_display->color565(100, 100, 140)
            : dma_display->color565(255, 200, 60);
        drawSunIcon(sunX, sunY, sunColor);
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

    // ---- Seconds pulse ----
    uint16_t pulseColor = (second % 2 == 0)
                              ? dma_display->color565(0, 150, 0)
                              : dma_display->color565(0, 60, 0);
    dma_display->fillCircle(62, 30, 1, pulseColor);
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



