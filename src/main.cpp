// === ESP32 Weather Display (Final Version) ===

#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include "time.h"
#include "RTClib.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include "display.h"
#include "pins.h"
#include "button.h"
#include "utils.h"
#include "icons.h"
#include "sensors.h"
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Update.h>
#include "settings.h"
#include "web.h"
#include "buzzer.h"
#include "menu.h"
#include "keyboard.h" // <-- Include keyboard header


extern int wifiSelectIndex;

extern IRrecv irrecv;
extern decode_results results;
extern unsigned long lastMenuActivity;
extern int menuScroll;
bool menuActive = false;
Preferences preferences;
int menuIndex = 0;
bool inSetupMenu = false;
WiFiUDP udp;
const int localPort = 50222;

// === Display Colors ===
uint16_t dayColor = dma_display->color565(255, 170, 51);
uint16_t nightColor = dma_display->color565(255, 255, 255);
String colorMode = "color";

// === WiFi & API Config ===
const char *ssid = "Polaris";
const char *password = "1339113391";
String openWeatherMapApiKey = "0db802af001e4a3c2b018d6e5e2a6632";
String city = "Garden%20Grove";
String countryCode = "US";

// === Units Toggle ===
bool useImperial = true;

// === NTP/RTC ===
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -8 * 3600;
const int daylightOffset_sec = 3600;
RTC_DS3231 rtc;

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

// === Display Timers ===
unsigned long prevMillis_ShowTimeDate = 0;
const long interval_ShowTimeDate = 1000;
unsigned long lastBrightnessRead = 0;
unsigned long lastDHTRead = 0;
unsigned long lastIRCheck = 0;
unsigned long lastButtonCheck = 0;
const unsigned long brightnessInterval = 60000;
const unsigned long dhtInterval = 2000;
const unsigned long irInterval = 50;
const unsigned long buttonInterval = 100;



void syncTimeFromNTP() {
    Serial.println("Syncing time from NTP...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
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
void fetchTempestData() {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
        char packet[1024];
        int len = udp.read(packet, sizeof(packet) - 1);
        if (len > 0) {
            packet[len] = 0;
            JSONVar doc = JSON.parse(packet);
            if (JSON.typeof(doc) == "undefined") {
                Serial.println("Failed to parse Tempest UDP");
                return;
            }
            String type = (const char *)doc["type"];
            if (type == "rapid_wind") {
                Serial.printf("Wind: %.2f m/s from %.1f°\n",
                              (double)doc["ob"][1], (double)doc["ob"][2]);
            } else if (type == "obs_st") {
                JSONVar obs = doc["obs"][0];
                Serial.printf("Air Temp: %.2f°C | Hum: %.1f%%\n",
                              (double)obs[7], (double)obs[8]);
            }
        }
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

void setup() {
    Serial.begin(115200);
    delay(100);
    loadSettings();
    delay(500);
    setupDisplay();
    Serial.println("Display setup done.");
    setupIRSensor();
    Serial.println("\nESP32 Weather Display");

    // ---- WiFi credential check ----
    if (wifiSSID.isEmpty() || wifiPass.isEmpty()) {
        Serial.println("[WiFi] No credentials, showing WiFi menu...");
        onWiFiConnectFailed();  // Show WiFi select menu immediately
        return;                 // Pause setup until WiFi is configured
    }

    Serial.println("Connecting WiFi...");
    connectToWiFi();

    // If WiFi menu is open, pause rest of setup (user needs to pick network)
    if (wifiSelecting) return;

    // Extra connection check (in case connectToWiFi fails to connect)
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Connection failed, showing WiFi menu...");
        onWiFiConnectFailed();
        return;
    }


    Serial.println("WiFi done.");
    ArduinoOTA.setHostname("ESP32-Weather");
    ArduinoOTA.begin();
    setupWebServer();
    Serial.println("Displaying Time...");
    syncTimeFromNTP();
    Serial.println("Done.");
    udp.begin(localPort);
    Serial.printf("Listening for Tempest on UDP port %d\n", localPort);
    fetchWeatherFromOWM();
    delay(500);
    getTimeFromRTC();
    reset_Time_and_Date_Display = true;
    displayWeatherData();
    displayClock();
    displayDate();
    delay(2000);
    setupButtons();

    currentMenuLevel = MENU_MAIN;
    currentMenuIndex = 0;
    menuActive = false;
    menuScroll = 0;
}

void loop() {
    unsigned long now = millis();

    static unsigned long lastBlink = 0;
    const unsigned long blinkInterval = 500; // ms

    // --- Keyboard blinking cursor ---
    if (inKeyboardMode && now - lastBlink >= blinkInterval) {
        lastBlink = now;
        keyboardBlinkTick();
    }


    // --- Always check IR sensor ---
    if (now - lastIRCheck >= irInterval) {
        lastIRCheck = now;
        readIRSensor();  // This will handle navigation, editing, WiFi select, etc.
    }

    // --- Always check buttons ---
    if (now - lastButtonCheck >= buttonInterval) {
        lastButtonCheck = now;
        getButton();
    }

    // --- WiFi Selection UI (acts like a menu page) ---
    if (wifiSelecting) {
        updateMenu(); // Draw/refresh the WiFi selection menu
        // Allow IR and buttons to operate as usual
        // (Don't return yet; allow the rest of the system to pause here if you want)
        delay(60);    // Small delay for UI responsiveness, can adjust as needed
        return;       // Pause all background/weather updates until user exits WiFi select
    }

    // --- Standard Menu Navigation ---
if (menuActive) {
    updateMenu();

    static unsigned long lastAutoReturnCheck = 0; // Remembers last checked activity

    // Only auto-back-to-main if NO navigation activity happened since last check
    if (millis() - lastMenuActivity > 30000 && lastMenuActivity == lastAutoReturnCheck) {
        if (currentMenuLevel != MENU_MAIN || currentMenuIndex != 0 || menuScroll != 0) {
            currentMenuLevel = MENU_MAIN;
            currentMenuIndex = 0;
            menuScroll = 0;
            drawMenu();
        }
    } else {
        // If the user did something, update the marker so the 30s timer resets
        lastAutoReturnCheck = lastMenuActivity;
    }

    delay(100);
    return;
}
    // --- Main background tasks (run only if not in menu or wifi selection) ---

    // Update time/date display every second
    if (now - prevMillis_ShowTimeDate >= interval_ShowTimeDate) {
        prevMillis_ShowTimeDate = now;
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

    // OTA handler (always safe to call)
    ArduinoOTA.handle();

    // Scrolling weather text
    scrollWeatherDetails();

    // Tempest UDP weather station integration
    fetchTempestData();

    // Periodically read brightness sensor
    if (now - lastBrightnessRead >= brightnessInterval) {
        lastBrightnessRead = now;
        readBrightnessSensor();
    }

    // Redraw time/weather if flagged
    if (reset_Time_and_Date_Display) {
        reset_Time_and_Date_Display = false;
        displayClock();
        displayDate();
        displayWeatherData();
    }
}
