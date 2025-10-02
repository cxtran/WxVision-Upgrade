#include "display.h"
#include "icons.h"
#include "settings.h"
#include "RTClib.h"   // For RTC DateTime
#include "sensors.h"
//#include "fonts/FreeSans9pt7b.h"
//#include "fonts/tahomabd7pt7b.h"
//#include "fonts/tahomabd8pt7b.h"
#include "fonts/verdanab8pt7b.h"

#include "tempest.h"

extern float aht20_temp;
extern float SCD40_temp;
extern int scrollLevel;
extern void displayClock();
extern void displayDate();
extern void displayWeatherData();
extern int theme;
/*
const ScreenMode InfoScreenModes[] = {
    SCREEN_UDP_DATA,
    SCREEN_UDP_FORECAST,
    // ...add more InfoScreen-based modes here
};
*/
// const int NUM_INFOSCREENS = sizeof(InfoScreenModes) / sizeof(InfoScreenModes[0]);

MatrixPanel_I2S_DMA *dma_display = nullptr;

uint16_t myRED, myGREEN, myBLUE, myWHITE, myBLACK, myYELLOW, myCYAN;

void setupDisplay() {
  HUB75_I2S_CFG::i2s_pins _pins = {
    R1_PIN, G1_PIN, B1_PIN,
    R2_PIN, G2_PIN, B2_PIN,
    A_PIN, B_PIN, C_PIN, D_PIN, E_PIN,
    LAT_PIN, OE_PIN, CLK_PIN
  };

  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN, _pins);
  mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_15M;
  mxconfig.min_refresh_rate = 120;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(map(brightness, 1, 100, 3, 255));
  dma_display->setTextSize(1);
  dma_display->setTextWrap(false);
  dma_display->setFont(&Font5x7Uts);

  myRED    = dma_display->color565(255, 0, 0);
  myGREEN  = dma_display->color565(0, 255, 0);
  myBLUE   = dma_display->color565(0, 0, 255);
  myWHITE  = dma_display->color565(255, 255, 255);
  myBLACK  = dma_display->color565(0, 0, 0);
  myYELLOW = dma_display->color565(255, 255, 0);
  myCYAN = dma_display->color565(0, 255, 255);
}


int getTextWidth(const char* text) {
  int16_t x1, y1;
  uint16_t w, h;
  dma_display->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return w;
}

const uint8_t* getWeatherIconFromCode(String code) {
 // Serial.printf("Code: %s", code);
  if (code.startsWith("01n")) return icon_clear_night;
  if (code.startsWith("02n")) return icon_cloud_night;
  if (code.startsWith("01d")) return icon_clear;
  if (code.startsWith("02d")) return icon_cloudy;
  if (code.startsWith("03") || code.startsWith("04")) return icon_cloudy;
  if (code.startsWith("09") || code.startsWith("10")) return icon_rain;
  if (code.startsWith("11")) return icon_thunder;
  if (code.startsWith("13")) return icon_snow;
  if (code.startsWith("50")) return icon_fog;
  return icon_clear; // fallback
}

const uint8_t* getWeatherIconFromCondition(String condition) {
  condition.toLowerCase();
  if (condition.indexOf("clear") >= 0) return icon_clear;
  if (condition.indexOf("cloud") >= 0) return icon_cloudy;
  if (condition.indexOf("rain") >= 0) return icon_rain;
  if (condition.indexOf("storm") >= 0) return icon_thunder;
  if (condition.indexOf("snow") >= 0) return icon_snow;
  if (condition.indexOf("fog") >= 0 || condition.indexOf("mist") >= 0) return icon_fog;
  return icon_clear;
}

const uint16_t getIconColorFromCondition(String condition){
  if (condition.indexOf("clear") >= 0) return dma_display->color565(255, 255, 0); // (yellow)
  if (condition.indexOf("cloud") >= 0) return dma_display->color565(180, 180, 180);
  if (condition.indexOf("rain") >= 0) return dma_display->color565(0, 200, 255);
  if (condition.indexOf("storm") >= 0) return dma_display->color565(255, 255, 0);
  if (condition.indexOf("snow") >= 0) return dma_display->color565(220, 255, 255);
  if (condition.indexOf("fog") >= 0 || condition.indexOf("mist") >= 0) return dma_display->color565(180, 180, 180);
  return dma_display->color565(255, 255, 0);
}

const uint16_t getDayNightColorFromCode( String code){
  if (code.indexOf("d") >= 0) return dma_display->color565(255, 170, 51); // day color
  else return myBLUE; // night color
}



void drawWeatherScreen() {
    dma_display->fillScreen(0);
    dma_display->setCursor(0, 8);
    dma_display->setTextColor(dma_display->color565(80,255,255));
    dma_display->setTextSize(2);
    dma_display->print("72 F");
    dma_display->setTextSize(1);
    dma_display->setCursor(0, 26);
    dma_display->print("Clear Sky");
}

 
void drawSettingsScreen() {
    dma_display->fillScreen(0);
    dma_display->setCursor(2, 10);
    dma_display->setTextColor(dma_display->color565(255,255,255));
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


String httpGETRequest(const char *url) {
    WiFiClient client;
    HTTPClient http;
    http.begin(client, url);
    int httpCode = http.GET();
    String payload = "{}";
    if (httpCode > 0) {
        payload = http.getString();
    } else {
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


void getTimeFromRTC() {
    DateTime now = rtc.now();
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


void fetchWeatherFromOWM() {
    if (WiFi.status() != WL_CONNECTED)
        return;
    String units = useImperial ? "imperial" : "metric";
    String url = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "," + countryCode +
                 "&units=" + units + "&appid=" + openWeatherMapApiKey;
    String jsonBuffer = httpGETRequest(url.c_str());
    if (jsonBuffer == "{}")
        return;
    JSONVar data = JSON.parse(jsonBuffer);
    if (JSON.typeof(data) == "undefined") {
        Serial.println("Failed to parse weather JSON");
        return;
    }
    str_Weather_Icon = JSON.stringify(data["weather"][0]["icon"]);
    str_Weather_Icon.replace("\"", "");
    str_Weather_Conditions = JSON.stringify(data["weather"][0]["main"]);
    str_Weather_Conditions.replace("\"", "");
    str_Weather_Conditions_Des = JSON.stringify(data["weather"][0]["description"]);
    str_Weather_Conditions_Des.replace("\"", "");
    str_Temp = JSON.stringify(data["main"]["temp"]);
    str_Humd = JSON.stringify(data["main"]["humidity"]);
    str_Pressure = JSON.stringify(data["main"]["pressure"]);
    str_Wind_Speed = JSON.stringify(data["wind"]["speed"]);
    str_Wind_Direction = JSON.stringify(data["wind"]["deg"]);
    str_City = JSON.stringify(data["name"]);
    str_City.replace("\"", "");
    str_Temp_max = JSON.stringify(data["main"]["temp_max"]);
    str_Temp_min = JSON.stringify(data["main"]["temp_min"]);
    str_Feels_like = JSON.stringify(data["main"]["feels_like"]);
    Serial.println("Weather Updated:");
    Serial.printf("  Temp: %s | Hum: %s%% | Wind: %s %s\n",
                  str_Temp.c_str(), str_Humd.c_str(), str_Wind_Speed.c_str(),
                  useImperial ? "mph" : "m/s");

    needScrollRebuild = true;
             
}



void drawOWMScreen(){
    getTimeFromRTC();
    displayClock();
    displayDate();

    // Detect any change across temp/wind/press/precip/clock24h
    const uint16_t curSig = unitSignature();
    if (curSig != lastUnitSig) {
        lastUnitSig = curSig;
        needScrollRebuild = true;          // units changed -> rebuild marquee
    }

    // Rebuild scrolling text only when needed
    if (needScrollRebuild) {
        createScrollingText();             // uses fmtTemp/fmtWind/... honoring 'units'
        // Reset marquee so it restarts smoothly
        start_Scroll_Text = false;
        set_up_Scrolling_Text_Length = true;
        text_Length_In_Pixel = getTextWidth(scrolling_Text.c_str());
        needScrollRebuild = false;
    }

    // Update weather every 10 minutes at :10s
    if ((t_minute % 10 == 0) && (t_second == 10)) {
        fetchWeatherFromOWM();
        reset_Time_and_Date_Display = true;
        displayWeatherData();
        needScrollRebuild = true;          // new values -> refresh marquee text
    }
}


void drawWeatherIcon(String iconCode) {
    dma_display->fillRect(0, 0, 16, 16, myBLACK);
    dma_display->setCursor(1, 4);
    dma_display->setTextColor(theme == 1 ? dma_display->color565(110,110,180) : myYELLOW);
    dma_display->drawBitmap(0, 0, getWeatherIconFromCode(iconCode), 16, 16, getDayNightColorFromCode(iconCode));
}

void displayClock() {
    int hour = atoi(chr_t_hour);
    char amPm[] = "A";
    if (hour >= 12) {
        if (hour > 12)
            hour -= 12;
        strcpy(amPm, "P");
    } else if (hour == 0) {
        hour = 12;
    }
    dma_display->fillRect(15, 9, 45, 7, myBLACK);
    dma_display->setCursor(16, 9);
    dma_display->setTextColor(theme == 1 ? dma_display->color565(90,90,150) : myRED);
    dma_display->printf("%02d:", hour);
    dma_display->print(chr_t_minute);
    dma_display->print(":");
    dma_display->print(chr_t_second);
    dma_display->fillRect(59, 9, 64, 16, myBLACK);
    dma_display->setCursor(59, 9);
    dma_display->printf("%s", amPm);
}

void displayDate() {
    dma_display->fillRect(0, 17, 64, 7, myBLACK);
    dma_display->setCursor(0, 17);
    dma_display->setTextColor(theme == 1 ? dma_display->color565(70,70,130) : myCYAN);
    dma_display->printf("%s %s.%s.%s", daysOfTheWeek[d_daysOfTheWeek], chr_d_month, chr_d_day, chr_d_year + 2);
}

void displayWeatherData() {
    drawWeatherIcon(str_Weather_Icon);
    dma_display->fillRect(18, 0, 46, 7, myBLACK);
    dma_display->setCursor(18, 0);
    dma_display->setTextColor(theme == 1 ? dma_display->color565(110,110,180) : myYELLOW);
 //   dma_display->print(customRoundString(str_Temp.c_str()));  
    dma_display->print( fmtTemp( atof(str_Temp.c_str()), 0)); 
//    dma_display->print(useImperial ? "°F" : "°C");
    dma_display->setCursor(44, 0);
    dma_display->setTextColor(theme == 1 ? dma_display->color565(70,70,130) : myCYAN);
    dma_display->print(str_Humd);
    dma_display->print("%");
}

void createScrollingText() {
 //   String unitT = useImperial ? "°F" : "°C";
 //   String unitW = useImperial ? "mph" : "m/s";

    scrolling_Text =
        "City: " + str_City + " ¦ " +
        "Weather: " + str_Weather_Conditions_Des + " ¦ " +
        "Feels Like: " + fmtTemp(atof(str_Feels_like.c_str()), 0) +  " ¦ " +
        "Max: " + fmtTemp(atof(str_Temp_max.c_str()), 0) +  " ¦ Min: " + fmtTemp(atof(str_Temp_min.c_str()), 0) +  " ¦ " +
        "Pressure: " + fmtPress(atof(str_Pressure.c_str()), 0) + " ¦ " +
        "Wind: " + fmtWind(atof(str_Wind_Speed.c_str()), 1) +  " ¦ ";

    scrolling_Text_Color = (theme == 1) ? dma_display->color565(60,60,120) : myGREEN;
    text_Length_In_Pixel = getTextWidth(scrolling_Text.c_str());
}



// --- Scroll state variables for weather ---
void scrollWeatherDetails() {
    static unsigned long lastScrollTime = 0;
    static int scrollOffset = 0;

    if (!start_Scroll_Text) {
        String unitT = useImperial ? "°F" : "°C";
        String unitW = useImperial ? "mph" : "m/s";
        scrollOffset = 0;
        start_Scroll_Text = true;
    }

    // Use same timing as InfoModal
    if (millis() - lastScrollTime > scrollSpeed) {
        lastScrollTime = millis();

        if (text_Length_In_Pixel > PANEL_RES_X) {
            scrollOffset++;
            if (scrollOffset > text_Length_In_Pixel)
                scrollOffset = -PANEL_RES_X; // restart
        } else {
            scrollOffset = 0;
        }

        dma_display->fillRect(0, 25, PANEL_RES_X, 7, myBLACK);
        dma_display->setCursor(-scrollOffset, 25);
        dma_display->setTextColor(scrolling_Text_Color);
        dma_display->print(scrolling_Text);
    }
}


void drawClockScreen() {

    dma_display->fillScreen(0);

    DateTime now = rtc.now();
    int hour = now.hour(), minute = now.minute(), second = now.second();

    // 12h/24h handling
    bool isPM = false;
    if (!units.clock24h) {
        if (hour == 0) hour = 12;
        else if (hour >= 12) { if (hour > 12) hour -= 12; isPM = true; }
    }

    char timeStr[6]; // "HH:MM"
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hour, minute);

    const char *days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    char dateStr[14];
    snprintf(dateStr, sizeof(dateStr), "%s %02d/%02d",
             days[now.dayOfTheWeek()], now.month(), now.day());

    // ---- TIME (big Verdana Bold)
    dma_display->setFont(&verdanab8pt7b);
    dma_display->setTextSize(1);
    uint16_t timeColor = (theme == 1) ? dma_display->color565(60,60,120)
                                      : dma_display->color565(255,255,80);
    dma_display->setTextColor(timeColor);

    // width of time string
    int timeW = getTextWidth(timeStr);

    // measure height for vertical centering
    int16_t x1, y1; uint16_t w, h;
    dma_display->getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
    int timeH = h;

    // total width (digits + AM/PM)
    int ampmW = 0;
    if (!units.clock24h) {
        ampmW = getTextWidth(isPM ? "PM" : "AM");
    }
    int totalW = timeW + (ampmW ? ampmW + 2 : 0);

    // center the whole block
    int boxX = (64 - totalW) / 2;
    if (boxX < 0) boxX = 0;
    int boxY = (32 - timeH) / 2;

    // --- draw time string (shifted up by 1)
    dma_display->setFont(&verdanab8pt7b);
    dma_display->setCursor(boxX, boxY + timeH - 1);
    dma_display->print(timeStr);

    // --- draw AM/PM inline
    if (!units.clock24h) {
        String ampmStr = isPM ? "PM" : "AM";
        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);
        uint16_t ampmColor = (theme == 1) ? dma_display->color565(60,60,120)
                                          : dma_display->color565(255,255,200);
        dma_display->setTextColor(ampmColor);

        // measure digit height
        int16_t x1,y1; uint16_t w,h;
        dma_display->getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
        int digitH = h;

            // measure AM/PM width & height
        dma_display->getTextBounds(ampmStr.c_str(), 0, 0, &x1, &y1, &w, &h);
        int ampmW = w;
        int ampmH = h;

        // anchor to right frame (2px margin)
        int ampmX = 64 - ampmW - 1; 

        // align baseline with digits, adjusted
        int ampmY = boxY + digitH - (digitH - ampmH) - 1;
        if (!isPM) {
            ampmY -= 6;
        }
        // shift up 1 pixel as before
        ampmY -= 1;

        // background box
        uint16_t bgColor = (theme == 1) ? dma_display->color565(20,20,40)
                                        : dma_display->color565(40,40,40);
        dma_display->fillRect(ampmX - 1, ampmY - ampmH + 6, ampmW + 2, ampmH + 2, bgColor);

        dma_display->setCursor(ampmX, ampmY);
        dma_display->print(ampmStr);

    }

    // ---- DATE (unchanged)
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    uint16_t dateColor = (theme == 1) ? dma_display->color565(60,60,120)
                                      : dma_display->color565(150,200,255);
    dma_display->setTextColor(dateColor);
    dma_display->getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
    int dateX = (64 - (int)w) / 2;
    if (dateX < 0) dateX = 0;
    int dateY = 25;
    dma_display->setCursor(dateX, dateY);
    dma_display->print(dateStr);

    // ---- TEMPERATURES (unchanged)
    dma_display->setFont(&Font5x7Uts);
    dma_display->setTextSize(1);
    uint16_t tempColor = (theme == 1) ? dma_display->color565(60,60,120)
                                      : dma_display->color565(200,200,255);
    dma_display->setTextColor(tempColor);

    String udpTempStr = fmtTemp(tempest.temperature, 0);  // UDP temp
//    String localTempStr = fmtTemp(aht20_temp, 0);         // Local temp via AHT20
    String localTempStr = fmtTemp(SCD40_temp,0);         // Local temp via SCD40

    // UDP temp at (0,0)
    dma_display->setCursor(0, 0);
    dma_display->print(udpTempStr);

    // Local temp stays right-aligned at top-right
    dma_display->getTextBounds(localTempStr.c_str(), 0, 0, &x1, &y1, &w, &h);
    int localX = 64 - w;
    int localY = 0;
    dma_display->setCursor(localX, localY);
    dma_display->print(localTempStr);

    // ---- Seconds pulse (dimmer but same color tone)
    uint16_t pulseColor = (second % 2 == 0)
        ? dma_display->color565(0,150,0)   // dimmer bright green
        : dma_display->color565(0,60,0);   // dimmer dark green
    dma_display->fillCircle(62, 30, 1, pulseColor);

}


// Public helpers
void requestScrollRebuild() {
  needScrollRebuild = true;
}

void notifyUnitsMaybeChanged() {
  const uint16_t sig = unitSignature();
  if (sig != lastUnitSig) {
    lastUnitSig = sig;
    needScrollRebuild = true;
  }
}

// Call this frequently; it only rebuilds when needed.
void serviceScrollRebuild() {
  if (!needScrollRebuild) return;

  // Rebuild the line using the latest units + data
  createScrollingText();

  // Reset marquee state so it restarts smoothly
  start_Scroll_Text = false;
  set_up_Scrolling_Text_Length = true;
  text_Length_In_Pixel = getTextWidth(scrolling_Text.c_str());

  needScrollRebuild = false;
}
