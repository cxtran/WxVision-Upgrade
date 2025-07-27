#include "display.h"
#include "icons.h"
#include "settings.h"



extern void displayClock();
extern void displayDate();
extern void displayWeatherData();

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


void drawClockScreen() {
    dma_display->fillScreen(0);
    dma_display->setCursor(4, 8);
    dma_display->setTextColor(dma_display->color565(255,255,0));
    dma_display->setTextSize(2);
    dma_display->print("12:34");
    dma_display->setTextSize(1);
    dma_display->setCursor(4, 26);
    dma_display->print("Sat, Jul 20");
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

void drawUdpDataScreen() {
  
    dma_display->fillScreen(0);
    dma_display->setCursor(2, 12);
    dma_display->setTextColor(dma_display->color565(255,160,0));
    dma_display->setTextSize(1);
    dma_display->print("UDP:");
    dma_display->setCursor(2, 22);
    dma_display->print("DATA HERE");
  
    // Fill "DATA HERE" with actual UDP data


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

void syncTimeFromNTP1() {
    Serial.println("Syncing time from NTP...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1);
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("❌ getLocalTime() failed");
        return;
    }
    int year = timeinfo.tm_year + 1900;
    if (year < 2020 || year > 2099) {
        Serial.printf("⚠️ Invalid year from NTP: %d\n", year);
        return;
    }
    DateTime newTime(year,
                     timeinfo.tm_mon + 1,
                     timeinfo.tm_mday,
                     timeinfo.tm_hour,
                     timeinfo.tm_min,
                     timeinfo.tm_sec);
    Serial.printf("Updating RTC with: %04d-%02d-%02d %02d:%02d:%02d\n",
                  newTime.year(), newTime.month(), newTime.day(),
                  newTime.hour(), newTime.minute(), newTime.second());
    if (!rtc.begin()) {
        Serial.println("⚠️ rtc.begin() failed. RTC module missing or not connected?");
        return;
    }
    rtc.adjust(newTime);
    Serial.println("✅ Time set from NTP.");
}

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
bool useImperial = true;
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
}



void drawOWMScreen(){
            getTimeFromRTC();
            displayClock();
            displayDate();
            // Update weather every 10 minutes at :10s
            if ((t_minute % 10 == 0) && (t_second == 10)) {
                fetchWeatherFromOWM();
                reset_Time_and_Date_Display = true;
                displayWeatherData();
            }
    
}

void drawWeatherIcon(String iconCode) {
    dma_display->fillRect(0, 0, 16, 16, myBLACK);
    dma_display->setCursor(1, 4);
    dma_display->setTextColor(myYELLOW);
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
    dma_display->setTextColor(myRED);
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
    dma_display->setTextColor(myCYAN);
    dma_display->printf("%s %s.%s.%s", daysOfTheWeek[d_daysOfTheWeek], chr_d_month, chr_d_day, chr_d_year + 2);
}

void displayWeatherData() {
    drawWeatherIcon(str_Weather_Icon);
    dma_display->fillRect(18, 0, 46, 7, myBLACK);
    dma_display->setCursor(18, 0);
    dma_display->setTextColor(myYELLOW);
    dma_display->print(customRoundString(str_Temp.c_str()));
    dma_display->print(useImperial ? "°F" : "°C");
    dma_display->setCursor(44, 0);
    dma_display->setTextColor(myCYAN);
    dma_display->print(str_Humd);
    dma_display->print("%");
}

void scrollWeatherDetails() {
    if (!start_Scroll_Text) {
        String unitT = useImperial ? "°F" : "°C";
        String unitW = useImperial ? "mph" : "m/s";
        scrolling_Text = "City: " + str_City + " ¦ " +
                         "Weather: " + str_Weather_Conditions_Des + " ¦ " +
                         "Feels Like: " + str_Feels_like + unitT + " ¦ " +
                         "Max: " + str_Temp_max + unitT + " ¦ Min: " + str_Temp_min + unitT + " ¦ " +
                         "Pressure: " + str_Pressure + " hPa ¦ " +
                         "Wind: " + str_Wind_Speed + " " + unitW + " ¦ ";
        scrolling_Text_Color = myGREEN;
        set_up_Scrolling_Text_Length = true;
        start_Scroll_Text = true;
    }
    if (start_Scroll_Text && set_up_Scrolling_Text_Length) {
        text_Length_In_Pixel = getTextWidth(scrolling_Text.c_str());
        scrolling_X_Pos = PANEL_RES_X;
        set_up_Scrolling_Text_Length = false;
    }
    if (millis() - prevMill_Scroll_Text >= 35) {
        prevMill_Scroll_Text = millis();
        scrolling_X_Pos--;
        if (scrolling_X_Pos < -(text_Length_In_Pixel)) {
            set_up_Scrolling_Text_Length = true;
            start_Scroll_Text = false;
            return;
        }
        dma_display->fillRect(0, 25, 64, 7, myBLACK);
        dma_display->setCursor(scrolling_X_Pos, 25);
        dma_display->setTextColor(scrolling_Text_Color);
        dma_display->print(scrolling_Text);
    }
}
