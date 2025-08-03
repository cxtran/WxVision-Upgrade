#include "tempest.h"
#include <HTTPClient.h>
#include "ScrollLine.h"

extern WiFiUDP udp;
extern InfoScreen udpScreen;
extern InfoScreen forecastScreen;
extern ScreenMode currentScreen;
extern WindMeter windMeter;
extern ScrollLine scrollLine;
extern int scrollSpeed;

// Support Functions
String formatEpochTime(uint32_t epoch) {
    if (epoch == 0) return "--";
    time_t rawTime = (time_t)epoch;
    struct tm * ti;
    ti = localtime(&rawTime);

    char buf[22];
    // Adjust the format string as needed: "%Y-%m-%d %H:%M:%S"
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ti);
    return String(buf);
}




// --- DATA ---
TempestData tempest;
ForecastData forecast;

bool newTempestData = false;
bool newRapidWindData = false;


// --------- Tempest UDP JSON Parsing ----------
void updateTempestFromUDP(const char* jsonStr) {
    JSONVar doc = JSON.parse(jsonStr);   // Only once here!
    if (JSON.typeof(doc) == "undefined") return;
    String type = (const char*)doc["type"];
    Serial.print("Received UDP type: "); Serial.println(type);

    if (type == "obs_st" && doc.hasOwnProperty("obs")) {
        JSONVar obs = doc["obs"][0];
        tempest.epoch         = (uint32_t)obs[0];
        tempest.windLull      = (double)obs[1];
        tempest.windAvg       = (double)obs[2];
        tempest.windGust      = (double)obs[3];
        tempest.windDir       = (double)obs[4];
        tempest.windSampleInt = (double)obs[5];
        tempest.pressure      = (double)obs[6];
        tempest.temperature   = (double)obs[7];
        tempest.humidity      = (double)obs[8];
        tempest.illuminance   = (double)obs[9];
        tempest.uv            = (double)obs[10];
        tempest.solar         = (double)obs[11];
        tempest.rain          = (double)obs[12];
        tempest.precipType    = (int)obs[13];
        tempest.strikeCount   = (int)obs[14];
        tempest.strikeDist    = (double)obs[15];
        tempest.battery       = (double)obs[16];
        tempest.reportInt     = (int)obs[17];
        tempest.lastObsTime   = String((uint32_t)obs[0]);
        tempest.lastUpdate    = millis();
        newTempestData = true;
    }
    else if (type == "rapid_wind" && doc.hasOwnProperty("ob")) {
        JSONVar ob = doc["ob"];
        if (ob.length() == 3) {
            tempest.epoch      = (uint32_t)ob[0];
            tempest.windAvg    = (double)ob[1];
            tempest.windDir    = (double)ob[2];
            tempest.lastUpdate = millis();
            newRapidWindData   = true;
            Serial.printf("rapid_wind: epoch=%lu windAvg=%.2f windDir=%.1f\n",
                (unsigned long)tempest.epoch, tempest.windAvg, tempest.windDir);
        } else {
            Serial.println("rapid_wind: ob array not length 3!");
        }
    }

}

// --------- WeatherFlow Better Forecast Parsing ----------
void updateForecastFromJson(const String& jsonStr) {
    JSONVar doc = JSON.parse(jsonStr);
    if (JSON.typeof(doc) == "undefined") return;
    if (doc.hasOwnProperty("forecast")) {
        JSONVar fc = doc["forecast"];
        if (fc.hasOwnProperty("daily")) {
            JSONVar daily = fc["daily"];
            if (daily.length() > 0) {
                JSONVar today = daily[0];
                forecast.highTemp   = (double)today["air_temp_hi"];
                forecast.lowTemp    = (double)today["air_temp_low"];
                forecast.rainChance = (int)today["precip_probability"];
                forecast.wind       = (double)today["wind_avg"];
                forecast.humidity   = (double)today["relative_humidity"];
                forecast.day        = (const char*)today["day_name"];
                forecast.summary    = (const char*)today["conditions"];
                forecast.lastUpdate = millis();
            }
        }
    }
}

// --------- UDP Listener ----------
void fetchTempestData() {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
        char packet[1024];
        int len = udp.read(packet, sizeof(packet) - 1);
        if (len > 0) {
            packet[len] = 0;
            updateTempestFromUDP(packet);
        }
    }
}

// --------- WeatherFlow Forecast Fetch ----------
void fetchForecastData() {
    HTTPClient http;
    http.begin("https://swd.weatherflow.com/swd/rest/better_forecast?station_id=79762&token=b802af9d-477d-4490-8da2-3f78badb07a7");
    int httpCode = http.GET();
    if (httpCode == 200) {
        String payload = http.getString();
        updateForecastFromJson(payload);
    }
    http.end();
}

// --------- String Helpers ----------
String getTempestField(const char* field) {
    if (!strcmp(field, "temp"))        return isnan(tempest.temperature) ? "--" : String(tempest.temperature, 1) + "C";
    if (!strcmp(field, "hum"))         return isnan(tempest.humidity) ? "--" : String(tempest.humidity, 0) + "%";
    if (!strcmp(field, "pres"))        return isnan(tempest.pressure) ? "--" : String(tempest.pressure, 1) + "hPa";
    if (!strcmp(field, "wind"))        return isnan(tempest.windAvg) ? "--" : String(tempest.windAvg, 1) + "m/s";
    if (!strcmp(field, "winddir"))     return isnan(tempest.windDir) ? "--" : String(tempest.windDir, 0) + "°";
    if (!strcmp(field, "uv"))          return isnan(tempest.uv) ? "--" : String(tempest.uv, 1);
    if (!strcmp(field, "solar"))       return isnan(tempest.solar) ? "--" : String(tempest.solar, 0) + "W";
    if (!strcmp(field, "rain"))        return isnan(tempest.rain) ? "--" : String(tempest.rain, 2) + "mm";
    if (!strcmp(field, "battery"))     return isnan(tempest.battery) ? "--" : String(tempest.battery, 2) + "V";
    if (!strcmp(field, "illuminance")) return isnan(tempest.illuminance) ? "--" : String(tempest.illuminance, 0) + "lx";
    if (!strcmp(field, "gust"))        return isnan(tempest.windGust) ? "--" : String(tempest.windGust, 1) + "m/s";
    if (!strcmp(field, "lull"))        return isnan(tempest.windLull) ? "--" : String(tempest.windLull, 1) + "m/s";
    if (!strcmp(field, "preciptype"))  return String(tempest.precipType);
    if (!strcmp(field, "strikes"))     return String(tempest.strikeCount);
    if (!strcmp(field, "strikedist"))  return isnan(tempest.strikeDist) ? "--" : String(tempest.strikeDist, 1) + "km";
    if (!strcmp(field, "obs_time"))    return tempest.lastObsTime;
    return "";
}

String getRapidWindField(const char* field) {
    if (!strcmp(field, "speed"))   return isnan(tempest.windAvg) ? "--" : String(tempest.windAvg, 1) + " m/s";
    if (!strcmp(field, "dir"))     return isnan(tempest.windDir) ? "--" : String(tempest.windDir, 0) + "°";
    if (!strcmp(field, "epoch"))   return String(tempest.epoch); // raw epoch
    if (!strcmp(field, "time"))    return formatEpochTime(tempest.epoch);
    return "";
}



String getForecastField(const char* field) {
    if (!strcmp(field, "day"))         return forecast.day;
    if (!strcmp(field, "high"))        return isnan(forecast.highTemp) ? "--" : String(forecast.highTemp, 1) + "C";
    if (!strcmp(field, "low"))         return isnan(forecast.lowTemp) ? "--" : String(forecast.lowTemp, 1) + "C";
    if (!strcmp(field, "rain"))        return (forecast.rainChance < 0) ? "--" : String(forecast.rainChance) + "%";
    if (!strcmp(field, "wind"))        return isnan(forecast.wind) ? "--" : String(forecast.wind, 1) + "m/s";
    if (!strcmp(field, "hum"))         return isnan(forecast.humidity) ? "--" : String(forecast.humidity, 0) + "%";
    if (!strcmp(field, "cond"))        return forecast.summary;
    return "";
}

// --------- InfoScreen Display Functions ----------
void showUdpScreen() {
    String lines[9];
    lines[0] = "Temp:  " + getTempestField("temp");
    lines[1] = "Hum:   " + getTempestField("hum");
    lines[2] = "Pres:  " + getTempestField("pres");
    lines[3] = "Wind:  " + getTempestField("wind");
    lines[4] = "Dir:   " + getTempestField("winddir");
    lines[5] = "Rain:  " + getTempestField("rain");
    lines[6] = "UV:    " + getTempestField("uv");
    lines[7] = "Solar: " + getTempestField("solar");
    lines[8] = "Batt:  " + getTempestField("battery");

    if (!udpScreen.isActive()) {
        udpScreen.setLines(lines, 9, true);
        udpScreen.show([](){ currentScreen = ScreenMode::SCREEN_OWM; });
    } else {
        udpScreen.setLines(lines, 9, false);  // just update text, keep scroll position
    }
}



void showForecastScreen() {
    String lines[8];
    lines[0] = "Day:  " + getForecastField("day");
    lines[1] = "High: " + getForecastField("high");
    lines[2] = "Low:  " + getForecastField("low");
    lines[3] = "Rain: " + getForecastField("rain");
    lines[4] = "Wind: " + getForecastField("wind");
    lines[5] = "Hum:  " + getForecastField("hum");
    lines[6] = "Cond: " + getForecastField("cond");
    forecastScreen.setLines(lines, 7);
    forecastScreen.show([](){ currentScreen = ScreenMode::SCREEN_OWM; });
}

void showRapidWindScreen() {
    String lines[3];
    lines[0] = "Speed: " + getRapidWindField("speed");
    lines[1] = "Dir:   " + getRapidWindField("dir");
    lines[2] = "Time:  " + getRapidWindField("time");
    rapidWindScreen.setLines(lines, 3, false);
    rapidWindScreen.show([](){ currentScreen = SCREEN_OWM; });
}

void showWindDirectionScreen() {
    Serial.printf("scrollSpeed: %d",scrollSpeed);

    dma_display->fillScreen(0);

    int cx = 10; // icon center x
    int cy = dma_display->height() / 2;

    // Draw wind meter icon
    windMeter.drawWindDirection(cx, cy, tempest.windDir, tempest.windAvg);

    dma_display->setTextColor(dma_display->color565(255, 255, 255));
    dma_display->setCursor(cx + 12, cy - 10); // shifted 8px left from +20

    if (isnan(tempest.windDir)) {
        dma_display->print("No data");
        return;
    }

    // Find nearest direction name
    const char* dirNames[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    float DIR_ANGLES[8] = {0, 45, 90, 135, 180, 225, 270, 315};

    float windDir = fmod(tempest.windDir, 360.0f);
    if (windDir < 0) windDir += 360.0f;

    int nearestIndex = 0;
    float minDiff = 360.0f;
    for (int i = 0; i < 8; ++i) {
        float diff = fabs(windDir - DIR_ANGLES[i]);
        if (diff > 180) diff = 360 - diff;
        if (diff < minDiff) {
            minDiff = diff;
            nearestIndex = i;
        }
    }


    scrollLine.update();
    scrollLine.draw(0, 0, 0xFFFF); // draw at x=0,y=0 default white text
  

    dma_display->setTextColor(myCYAN);
    dma_display->setCursor(cx + 12, 8);
    // Print direction name and angle
    dma_display->printf("%s %d°", dirNames[nearestIndex], static_cast<int>(tempest.windDir));

    // Print wind speed below, shifted 8px left as well
    dma_display->setCursor(cx + 12, cy);
    if (isnan(tempest.windAvg)) {
        dma_display->print("-- m/s");
    } else {
        dma_display->printf("%.1f m/s", tempest.windAvg);
    }
}

