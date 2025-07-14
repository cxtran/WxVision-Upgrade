// === ESP32 Weather Display (Final Version) ===
// === PART 1: Includes, Panel Pins, Globals, Unit Toggle ===

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
#include "display.h" // includes extern declarations only
#include "pins.h"
#include "button.h"
#include "utils.h"
#include "icons.h"
#include "sensors.h"
#include <IRrecv.h>
#include <IRremoteESP8266.h>
// Web Server
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Update.h>
#include "utils.h"

// Buzzer
#include "buzzer.h"

#include "menu.h"

extern IRrecv irrecv;
extern decode_results results;

Preferences preferences;

int menuIndex = 0;
bool inSetupMenu = false;

const char *setupMenuItems[] = {
    "Unit: °F/°C",
    "Toggle Color",
    "OTA Update",
    "Reboot",
    "Save & Exit"};
const int setupMenuCount = sizeof(setupMenuItems) / sizeof(setupMenuItems[0]);

AsyncWebServer server(80);

WiFiUDP udp;
const int localPort = 50222;

// === Display Colors ===

uint16_t dayColor = dma_display->color565(255, 170, 51);
uint16_t nightColor = dma_display->color565(255, 255, 255);
String colorMode = "color"; // or load from Preferences

// === WiFi & API Config ===
const char *ssid = "Polaris";
const char *password = "1339113391";
String openWeatherMapApiKey = "0db802af001e4a3c2b018d6e5e2a6632";
String city = "Garden%20Grove"; // e.g., "Garden%20Grove"
String countryCode = "US";

// === Units Toggle ===
bool useImperial = true; // true = °F & mph, false = °C & m/s

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

// === PART 2: Display Init, Colors, Bitmap Icons ===

// Weather icon bitmaps (compressed for brevity; define full in real code)
/*
const unsigned char icon_clear_day[] PROGMEM = {
    0x01, 0x80, 0x21, 0x84, 0x71, 0x8e, 0x38, 0x1c, 0x13, 0xc8, 0x07, 0xe0, 0x0f, 0xf0, 0xef, 0xf7,
    0xef, 0xf7, 0x0f, 0xf0, 0x07, 0xe0, 0x13, 0xc8, 0x38, 0x1c, 0x71, 0x8e, 0x21, 0x84, 0x01, 0x80};
*/
// Define additional weather icons like icon_clouds_day[], icon_rain_day[], etc.
// You can reuse icons you already defined, like weather_icon_code_02d, 03d, etc.
// === PART 3: WiFi, NTP Sync, RTC Time ===

unsigned long lastBrightnessRead = 0;
unsigned long lastDHTRead = 0;
unsigned long lastIRCheck = 0;
// ...more for each timed task

const unsigned long brightnessInterval = 60000; // ms
const unsigned long dhtInterval = 2000;       // ms
const unsigned long irInterval = 50;          // ms

void connectToWiFi()
{

  if (!dma_display)
  {
    Serial.println("⚠️  dma_display not initialized. Skipping display output.");
  }
  else
  {
    dma_display->clearScreen();
    delay(500);
    dma_display->setCursor(0, 0);
    dma_display->setTextColor(myBLUE);
    dma_display->print("Connecting");

    dma_display->setCursor(0, 8);
    dma_display->setTextColor(myBLUE);
    dma_display->print("to WiFi...");
  }

  WiFi.mode(WIFI_STA);
  delay(1200);
  WiFi.begin(ssid, password);

  int attempts = 40; // 20s timeout
  while (WiFi.status() != WL_CONNECTED && attempts-- > 0)
  {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    dma_display->clearScreen();
    dma_display->setCursor(0, 0);
    dma_display->setTextColor(myGREEN);
    dma_display->print("WiFi");
    dma_display->setCursor(0, 9);
    dma_display->print("Connected.");
    dma_display->setCursor(0, 17);
    dma_display->setTextColor(myWHITE);
    dma_display->print(WiFi.localIP().toString());


    delay(1000);
  }
  else
  {
    dma_display->clearScreen();
    dma_display->setCursor(0, 0);
    dma_display->setTextColor(myRED);
    dma_display->print("WiFi Failed!");
    delay(1000);
    ESP.restart();
  }
}

void syncTimeFromNTP()
{
  Serial.println("Syncing time from NTP...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("❌ getLocalTime() failed");
    return;
  }

  int year = timeinfo.tm_year + 1900;
  if (year < 2020 || year > 2099)
  {
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

  // Ensure rtc is initialized
  if (!rtc.begin())
  {
    Serial.println("⚠️ rtc.begin() failed. RTC module missing or not connected?");
    return;
  }

  rtc.adjust(newTime);
  Serial.println("✅ Time set from NTP.");
}

void getTimeFromRTC()
{
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
// === PART 4: OpenWeatherMap + WeatherFlow API Fetch ===

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

void fetchWeatherFromOWM()
{
  if (WiFi.status() != WL_CONNECTED)
    return;

  String units = useImperial ? "imperial" : "metric";
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "," + countryCode +
               "&units=" + units + "&appid=" + openWeatherMapApiKey;

  String jsonBuffer = httpGETRequest(url.c_str());
  if (jsonBuffer == "{}")
    return;

  JSONVar data = JSON.parse(jsonBuffer);
  if (JSON.typeof(data) == "undefined")
  {
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

void fetchTempestData()
{
  int packetSize = udp.parsePacket();
  if (packetSize > 0)
  {
    char packet[1024];
    int len = udp.read(packet, sizeof(packet) - 1);
    if (len > 0)
    {
      packet[len] = 0;
      JSONVar doc = JSON.parse(packet);
      if (JSON.typeof(doc) == "undefined")
      {
        Serial.println("Failed to parse Tempest UDP");
        return;
      }

      String type = (const char *)doc["type"];
      if (type == "rapid_wind")
      {
        Serial.printf("Wind: %.2f m/s from %.1f°\n",
                      (double)doc["ob"][1], (double)doc["ob"][2]);
      }
      else if (type == "obs_st")
      {
        JSONVar obs = doc["obs"][0];
        Serial.printf("Air Temp: %.2f°C | Hum: %.1f%%\n",
                      (double)obs[7], (double)obs[8]);
      }
    }
  }
}
// === PART 5: Display Time, Weather, and Scroll ===

void drawWeatherIcon(String iconCode)
{
  dma_display->fillRect(0, 0, 16, 16, myBLACK);
  //    dma_display->drawRect(0, 0, 16, 16, myWHITE);  // Icon border
  dma_display->setCursor(1, 4);
  dma_display->setTextColor(myYELLOW);
  dma_display->drawBitmap(0, 0, getWeatherIconFromCode(iconCode), 16, 16, getDayNightColorFromCode(iconCode));
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
    hour = 12; // midnight case
  }

  dma_display->fillRect(15, 9, 45, 7, myBLACK);
  dma_display->setCursor(16, 9);
  dma_display->setTextColor(myRED);
  dma_display->printf("%02d:", hour); // use %d for integers
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
  dma_display->setTextColor(myCYAN);
  dma_display->printf("%s %s.%s.%s", daysOfTheWeek[d_daysOfTheWeek], chr_d_month, chr_d_day, chr_d_year + 2);
}

void displayWeatherData()
{
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

void scrollWeatherDetails()
{
  if (!start_Scroll_Text)
  {
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

  if (start_Scroll_Text && set_up_Scrolling_Text_Length)
  {
    text_Length_In_Pixel = getTextWidth(scrolling_Text.c_str());
    scrolling_X_Pos = PANEL_RES_X;
    set_up_Scrolling_Text_Length = false;
  }

  if (millis() - prevMill_Scroll_Text >= 35)
  {
    prevMill_Scroll_Text = millis();
    scrolling_X_Pos--;
    if (scrolling_X_Pos < -(text_Length_In_Pixel))
    {
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

bool isPressed(int pin)
{
  if (digitalRead(pin) == LOW)
  {
    delay(150);
    while (digitalRead(pin) == LOW)
      delay(10);
    return true;
  }
  return false;
}
// === PART 8: Menu Display and Logic ===

void drawSetupMenu()
{
  dma_display->clearScreen();
  for (int i = 0; i < setupMenuCount; i++)
  {
    dma_display->setCursor(0, i * 8);
    dma_display->setTextColor(i == menuIndex ? myCYAN : myWHITE);
    dma_display->print(setupMenuItems[i]);
  }
}

void handleSetupMenu()
{
  drawSetupMenu();

  while (inSetupMenu)
  {
    if (isPressed(BTN_UP))
    {
      menuIndex = (menuIndex - 1 + setupMenuCount) % setupMenuCount;
      drawSetupMenu();
    }
    else if (isPressed(BTN_DN))
    {
      menuIndex = (menuIndex + 1) % setupMenuCount;
      drawSetupMenu();
    }
    else if (isPressed(BTN_SEL))
    {
      String choice = setupMenuItems[menuIndex];

      if (choice == "Unit: °F/°C")
      {
        useImperial = !useImperial;
      }
      else if (choice == "Toggle Color")
      {
        colorMode = (colorMode == "color") ? "mono" : "color";
      }
      else if (choice == "OTA Update")
      {
        WiFiClient client;
        ArduinoOTA.begin();
        dma_display->clearScreen();
        dma_display->setCursor(0, 0);
        dma_display->setTextColor(myYELLOW);
        dma_display->print("OTA Ready");

        delay(5000);
      }
      else if (choice == "Reboot")
      {
        ESP.restart();
      }
      else if (choice == "Save & Exit")
      {
        preferences.begin("weatherApp", false);
        preferences.putBool("imperial", useImperial);
        preferences.putString("mode", colorMode);
        preferences.end();
        inSetupMenu = false;
        break;
      }
      drawSetupMenu();
    }
    delay(100);
  }
}
// === PART 9: Detect Setup Menu on Boot (Hold SELECT) ===

void checkForSetupMenu()
{
  dma_display->clearScreen();
  dma_display->setCursor(0, 0);
  dma_display->setTextColor(myCYAN);
  dma_display->print("Hold SELECT");
  dma_display->setCursor(0, 8);
  dma_display->print("for Setup...");

  unsigned long t0 = millis();
  while (millis() - t0 < 3000)
  {
    if (digitalRead(BTN_SEL == LOW))
    {
      delay(800);
      if (digitalRead(BTN_SEL == LOW))
      {
        inSetupMenu = true;
        handleSetupMenu();
        return;
      }
    }
    delay(100);
  }

  dma_display->clearScreen();
}
// === PART 10: Web Interface and OTA ===

void setupWebInterface()
{
  SPIFFS.begin(true);

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("config.html");

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req)
            {
    String status = "<html><body><h2>Status</h2>";
    status += "<p>WiFi: " + String(WiFi.SSID()) + "</p>";
    status += "<p>Weather: " + str_Weather_Conditions + " " + str_Temp + (useImperial ? "°F" : "°C") + "</p>";
    status += "<p>Humidity: " + str_Humd + "%</p>";
    status += "<p>Time: " + String(chr_t_hour) + ":" + chr_t_minute + ":" + chr_t_second + "</p>";
    status += "<p><a href='/ota'>Start OTA</a> | <a href='/reboot'>Reboot</a></p></body></html>";
    req->send(200, "text/html", status); });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *req)
            {
    req->send(200, "text/plain", "Rebooting...");
    delay(1000);
    ESP.restart(); });

  server.on("/ota", HTTP_GET, [](AsyncWebServerRequest *req)
            {
    String html = "<form method='POST' action='/update' enctype='multipart/form-data'>";
    html += "<input type='file' name='firmware'><input type='submit' value='Upload'></form>";
    req->send(200, "text/html", html); });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *req)
            {
    req->send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
    delay(1000);
    ESP.restart(); }, [](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final)
            {
    if (!index) Update.begin(UPDATE_SIZE_UNKNOWN);
    Update.write(data, len);
    if (final) Update.end(true); });

  server.begin();
}

///////////////////////// Setup function  ////////////////////////////////////



void setup()
{

  Serial.println("Display setup done.");

  // Initialize display
  delay(500);
  setupDisplay();

  Serial.begin(115200);

  // IR Sensor
  setupIRSensor();

  // Initialize DHT Sensor
  //setupDHTSensor();

  Serial.println("\nESP32 Weather Display");

  Serial.println("Setting up display...");

  Serial.println("Connecting WiFi...");
  // Connect to WiFi
  connectToWiFi();
  Serial.println("WiFi done.");

  // OTA
  ArduinoOTA.setHostname("ESP32-Weather");
  ArduinoOTA.begin();

  if (!SPIFFS.begin(true))
  {
    Serial.println("An error occurred while mounting SPIFFS");
  }

  Serial.println("Displaying Time...");
  // Initialize RTC from NTP
  syncTimeFromNTP();

  Serial.println("Done.");

  // Start UDP for Tempest
  udp.begin(localPort);
  Serial.printf("Listening for Tempest on UDP port %d\n", localPort);

  // First-time weather fetch
  fetchWeatherFromOWM();
  delay(500);
  getTimeFromRTC();
  reset_Time_and_Date_Display = true;

  // Show initial screen
  displayWeatherData();
  displayClock();
  displayDate();
  delay(2000);

  setupButtons();
 // setupBuzzer();

  setupWebInterface();
}

///////////////////////////////// Loop Function //////////////////////////////////////

void loop()
{

  unsigned long now = millis();

  // Time update every second
  if (now - prevMillis_ShowTimeDate >= interval_ShowTimeDate)
  {
    prevMillis_ShowTimeDate = now;

    getTimeFromRTC();
    displayClock();
    displayDate();

    // Update weather every 10 min at :10s
    if ((t_minute % 10 == 0) && (t_second == 10))
    {
      fetchWeatherFromOWM();
      reset_Time_and_Date_Display = true;
      displayWeatherData();
    }
  }

  ArduinoOTA.handle(); // Needed for OTA to run

  // Show scrolling text
  scrollWeatherDetails();

  // Process Tempest UDP if available
  fetchTempestData();



  if (now - lastBrightnessRead >= brightnessInterval)
  {
    lastBrightnessRead = now;
    readBrightnessSensor();
  }

  if (now - lastIRCheck >= irInterval)
  {
    lastIRCheck = now;
    // Check IR sensor, etc.
    readIRSensor();
  }

  // readDHTSensor();

  // Button handling
  getButton();




  // Check for setup menu on boot
  if (!inSetupMenu && isPressed(BTN_SEL))
  {
    inSetupMenu = true;
    handleSetupMenu();
  }


  // Display update
  if (reset_Time_and_Date_Display)
  {
    reset_Time_and_Date_Display = false;
    displayClock();
    displayDate();
    displayWeatherData();
  }


 // updateMenu();


}
