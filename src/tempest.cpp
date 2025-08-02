#include "tempest.h"
#include <Arduino_JSON.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>


// ---- Externals ----
extern WiFiUDP udp;
TempestData tempest;
ForecastData forecast;
extern InfoScreen udpScreen;
extern InfoScreen forecastScreen;


void updateTempestFromUDP(const char* jsonStr) {
    JSONVar doc = JSON.parse(jsonStr);
    if (JSON.typeof(doc) == "undefined") return;
    String type = (const char*)doc["type"];

    if (type == "obs_st" && doc.hasOwnProperty("obs")) {
        JSONVar obs = doc["obs"][0];
        tempest.temperature = (double)obs[7];
        tempest.humidity    = (double)obs[8];
        tempest.pressure    = (double)obs[6];
        tempest.windSpeed   = (double)obs[2];
        tempest.windDir     = (double)obs[3];
        tempest.uv          = (double)obs[11];
        tempest.solar       = (double)obs[10];
        tempest.rain        = (double)obs[12];
        tempest.battery     = (double)obs[16];
        tempest.lastObsTime = String((uint32_t)obs[0]);
        tempest.lastUpdate = millis();
    } else if (type == "rapid_wind" && doc.hasOwnProperty("ob")) {
        JSONVar ob = doc["ob"];
        tempest.windSpeed = (double)ob[0][1];
        tempest.windDir = (double)ob[0][2];
        tempest.lastUpdate = millis();
    }
}

String getTempestField(const char* field) {
    if (!strcmp(field, "temp"))     return isnan(tempest.temperature) ? "--" : String(tempest.temperature, 1) + "C";
    if (!strcmp(field, "hum"))      return isnan(tempest.humidity) ? "--" : String(tempest.humidity, 0) + "%";
    if (!strcmp(field, "pres"))     return isnan(tempest.pressure) ? "--" : String(tempest.pressure, 1) + "hPa";
    if (!strcmp(field, "wind"))     return isnan(tempest.windSpeed) ? "--" : String(tempest.windSpeed, 1) + "m/s";
    if (!strcmp(field, "winddir"))  return isnan(tempest.windDir) ? "--" : String(tempest.windDir, 0) + "°";
    if (!strcmp(field, "uv"))       return isnan(tempest.uv) ? "--" : String(tempest.uv, 1);
    if (!strcmp(field, "solar"))    return isnan(tempest.solar) ? "--" : String(tempest.solar, 0) + "W";
    if (!strcmp(field, "rain"))     return isnan(tempest.rain) ? "--" : String(tempest.rain, 2) + "mm";
    if (!strcmp(field, "battery"))  return isnan(tempest.battery) ? "--" : String(tempest.battery, 2) + "V";
    if (!strcmp(field, "obs_time")) return tempest.lastObsTime;
    return "";
}

void updateForecastFromJson(const String& jsonStr) {
    JSONVar doc = JSON.parse(jsonStr);
    if (JSON.typeof(doc) == "undefined") return;

    // WeatherFlow Better Forecast format
    if (doc.hasOwnProperty("forecast")) {
        JSONVar fc = doc["forecast"];
        if (fc.hasOwnProperty("daily")) {
            JSONVar daily = fc["daily"];
            if (daily.length() > 0) {
                JSONVar today = daily[0];
                forecast.highTemp  = (double)today["air_temp_hi"];
                forecast.lowTemp   = (double)today["air_temp_low"];
                forecast.rainChance = (int)today["precip_probability"];
                forecast.wind = (double)today["wind_avg"];
                forecast.humidity = (double)today["relative_humidity"];
                forecast.day = (const char*)today["day_name"];
                forecast.summary = (const char*)today["conditions"];
                forecast.lastUpdate = millis();
            }
        }
    }
}

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

// NOTE: Replace XXXX with your station_id and YYYY with your token.
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

void showUdpScreen() {
    String lines[8];
    lines[0] = "Temp:  " + getTempestField("temp");
    lines[1] = "Hum:   " + getTempestField("hum");
    lines[2] = "Pres:  " + getTempestField("pres");
    lines[3] = "Wind:  " + getTempestField("wind") + " " + getTempestField("winddir");
    lines[4] = "Rain:  " + getTempestField("rain");
    lines[5] = "UV:    " + getTempestField("uv");
    lines[6] = "Solar: " + getTempestField("solar");
    udpScreen.setLines(lines, 7);
    udpScreen.show([](){ currentScreen = SCREEN_OWM; });  // on exit, return to main screen
}

void showForecastScreen() {
    String lines[8];
    lines[0] = "Day:  " + forecast.day;
    lines[1] = "High: " + (isnan(forecast.highTemp) ? "--" : String(forecast.highTemp, 1) + "C");
    lines[2] = "Low:  " + (isnan(forecast.lowTemp)  ? "--" : String(forecast.lowTemp, 1) + "C");
    lines[3] = "Rain: " + (forecast.rainChance < 0   ? "--" : String(forecast.rainChance) + "%");
    lines[4] = "Wind: " + (isnan(forecast.wind)      ? "--" : String(forecast.wind, 1) + "m/s");
    lines[5] = "Hum:  " + (isnan(forecast.humidity)  ? "--" : String(forecast.humidity, 0) + "%");
    lines[6] = "Cond: " + forecast.summary;
    forecastScreen.setLines(lines, 7);
    forecastScreen.show([](){ currentScreen = SCREEN_OWM; });
}
