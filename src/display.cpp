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

#include "tempest.h"
#include "weather_countries.h"

extern float aht20_temp;
extern float SCD40_temp;
extern float SCD40_hum;
extern float aht20_hum;
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
extern int humOffset;
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
        // Monochrome theme palette
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
        // Color theme palette
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



