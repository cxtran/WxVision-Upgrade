#include "tempest.h"
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include <WiFi.h>
#include "ScrollLine.h"
#include "units.h"
#include "settings.h"

extern WiFiUDP udp;
extern InfoScreen udpScreen;
extern InfoScreen forecastScreen;
extern InfoScreen currentCondScreen;
extern InfoScreen hourlyScreen;
extern ScreenMode currentScreen;
extern WindMeter windMeter;
extern ScrollLine scrollLine;
extern ScrollLine windInfo;
extern int scrollSpeed;
extern int theme;

// Support Functions
String formatEpochTime(uint32_t epoch) {
    if (epoch == 0) return "--";
    time_t rawTime = (time_t)epoch;
    struct tm * ti = localtime(&rawTime);
    char buf[22];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ti);
    return String(buf);
}

// --- DATA ---
TempestData tempest;
ForecastData forecast;
CurrentConditions currentCond;

bool newTempestData = false;
bool newRapidWindData = false;

// --------- Tempest UDP JSON Parsing ----------
void updateTempestFromUDP(const char* jsonStr) {
    JSONVar doc = JSON.parse(jsonStr);
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

            // was: Serial.printf("rapid_wind: epoch=%lu windAvg=%.2f windDir=%.1f\n", ...)
            Serial.print("rapid_wind: epoch=");
            Serial.print((unsigned long)tempest.epoch);
            Serial.print(" windAvg=");
            Serial.print(tempest.windAvg, 2);
            Serial.print(" windDir=");
            Serial.println(tempest.windDir, 1);
        } else {
            Serial.println("rapid_wind: ob array not length 3!");
        }
    }
}

// Extracts a full JSON object by key, handling nested braces.
String extractJsonObject(const String& src, const char* key) {
    int keyIdx = src.indexOf(key);
    if (keyIdx < 0) return "";
    int start = src.indexOf('{', keyIdx);
    if (start < 0) return "";
    int brace = 1;
    int i = start + 1;
    while (i < src.length() && brace > 0) {
        if (src[i] == '{') ++brace;
        else if (src[i] == '}') --brace;
        ++i;
    }
    if (brace == 0) return src.substring(start, i);
    return "";
}

String extractJsonArray(const String& json, const String& key) {
    int keyIdx = json.indexOf(key);
    if (keyIdx < 0) return "";
    int arrayStart = json.indexOf('[', keyIdx);
    if (arrayStart < 0) return "";
    int depth = 0, arrayEnd = -1;
    for (int i = arrayStart; i < json.length(); ++i) {
        if (json[i] == '[') depth++;
        else if (json[i] == ']') {
            depth--;
            if (depth == 0) {
                arrayEnd = i;
                break;
            }
        }
    }
    if (arrayEnd < 0) return "";
    return json.substring(arrayStart, arrayEnd + 1);
}

// ============ Forecast parsing (split) ============

static inline void _sanitizeBools(String& s) {
    s.replace(":true",  ":1");
    s.replace(":false", ":0");
}

void updateCurrentConditionsFromJson(const String& jsonStr) {
    String currentStr = extractJsonObject(jsonStr, "\"current_conditions\"");
    currentStr.trim();
    _sanitizeBools(currentStr);

    // Remove hidden/bad chars that can break Arduino_JSON
    for (int i = 0; i < currentStr.length(); ++i) {
        char c = currentStr[i];
        if (!(c == '{' || c == '}' || c == ':' || c == ',' || (c >= 32 && c <= 126))) {
            currentStr.remove(i, 1);
            --i;
        }
    }

    JSONVar cur = nullptr;
    if (currentStr.startsWith("{") && currentStr.endsWith("}")) cur = JSON.parse(currentStr);
    if (cur == nullptr) {
        String asArray = "[" + currentStr + "]";
        cur = JSON.parse(asArray);
        if (cur != nullptr && JSON.typeof(cur) == "array" && cur.length() > 0) cur = cur[0];
        else cur = nullptr;
    }

    if (cur == nullptr || JSON.typeof(cur) != "object") {
        memset(&currentCond, 0, sizeof(CurrentConditions));
        Serial.println("[FORECAST] No valid current_conditions object");
        return;
    }

    currentCond.temp         = (JSON.typeof(cur["air_temperature"]) == "number") ? (double)cur["air_temperature"] : NAN;
    currentCond.feelsLike    = (JSON.typeof(cur["feels_like"]) == "number") ? (double)cur["feels_like"] : NAN;
    currentCond.dewPoint     = (JSON.typeof(cur["dew_point"]) == "number") ? (double)cur["dew_point"] : NAN;
    currentCond.humidity     = (JSON.typeof(cur["relative_humidity"]) == "number") ? (int)cur["relative_humidity"] : -1;
    currentCond.pressure     = (JSON.typeof(cur["sea_level_pressure"]) == "number") ? (double)cur["sea_level_pressure"] : NAN;
    currentCond.windAvg      = (JSON.typeof(cur["wind_avg"]) == "number") ? (double)cur["wind_avg"] : NAN;
    currentCond.windGust     = (JSON.typeof(cur["wind_gust"]) == "number") ? (double)cur["wind_gust"] : NAN;
    currentCond.windDir      = (JSON.typeof(cur["wind_direction"]) == "number") ? (double)cur["wind_direction"] : NAN;
    currentCond.windCardinal = (JSON.typeof(cur["wind_direction_cardinal"]) == "string") ? (const char*)cur["wind_direction_cardinal"] : "";
    currentCond.uv           = (JSON.typeof(cur["uv"]) == "number") ? (int)cur["uv"] : -1;
    currentCond.precipProb   = (JSON.typeof(cur["precip_probability"]) == "number") ? (int)cur["precip_probability"] : -1;
    currentCond.cond         = (JSON.typeof(cur["conditions"]) == "string") ? (const char*)cur["conditions"] : "";
    currentCond.icon         = (JSON.typeof(cur["icon"]) == "string") ? (const char*)cur["icon"] : "";
    currentCond.time         = (JSON.typeof(cur["time"]) == "number") ? (uint32_t)(double)cur["time"] : 0;

    Serial.println("[FORECAST] Current conditions updated");
}

void updateDailyForecastFromJson(const String& jsonStr) {
    int forecastIdx = jsonStr.indexOf("\"forecast\"");
    if (forecastIdx < 0) {
        Serial.println("[ERROR] No 'forecast' found!");
        forecast.numDays = 0;
        return;
    }

    int dailyIdx = jsonStr.indexOf("\"daily\"", forecastIdx);
    if (dailyIdx < 0) {
        Serial.println("[ERROR] No 'daily' in forecast!");
        forecast.numDays = 0;
        return;
    }

    int arrayStart = jsonStr.indexOf('[', dailyIdx);
    int arrayEnd   = (arrayStart >= 0) ? jsonStr.indexOf(']', arrayStart) : -1;
    if (arrayStart < 0 || arrayEnd < 0) {
        Serial.println("[ERROR] Could not find daily array brackets!");
        forecast.numDays = 0;
        return;
    }

    String dailyArrayStr = jsonStr.substring(arrayStart, arrayEnd + 1);
    _sanitizeBools(dailyArrayStr);

    JSONVar daily = JSON.parse(dailyArrayStr);
    if (JSON.typeof(daily) != "array") {
        Serial.println("[ERROR] Parsed daily forecast is not an array!");
        forecast.numDays = 0;
        return;
    }

    int days = min((int)daily.length(), MAX_FORECAST_DAYS);
    forecast.numDays = days;
    for (int i = 0; i < days; i++) {
        JSONVar d = daily[i];
        ForecastDay& f = forecast.days[i];
        f.highTemp   = (JSON.typeof(d["air_temp_high"]) == "number") ? (double)d["air_temp_high"] : NAN;
        f.lowTemp    = (JSON.typeof(d["air_temp_low"])  == "number") ? (double)d["air_temp_low"]  : NAN;
        f.rainChance = (JSON.typeof(d["precip_probability"]) == "number") ? (int)d["precip_probability"] : -1;
        f.conditions = (JSON.typeof(d["conditions"]) == "string") ? (const char*)d["conditions"] : "";
        f.icon       = (JSON.typeof(d["icon"])       == "string") ? (const char*)d["icon"]       : "";
        f.sunrise    = (JSON.typeof(d["sunrise"])    == "number") ? (uint32_t)(double)d["sunrise"] : 0;
        f.sunset     = (JSON.typeof(d["sunset"])     == "number") ? (uint32_t)(double)d["sunset"]  : 0;
        f.dayNum     = (JSON.typeof(d["day_num"])    == "number") ? (int)d["day_num"]    : 0;
        f.monthNum   = (JSON.typeof(d["month_num"])  == "number") ? (int)d["month_num"]  : 0;
    }
    // was: Serial.printf("[FORECAST] Parsed %d daily entries\n", forecast.numDays);
    Serial.print("[FORECAST] Parsed ");
    Serial.print(forecast.numDays);
    Serial.println(" daily entries");
}

// Parse hourly array by extracting individual objects (memory-safe on ESP32)
void updateHourlyForecastFromJson(const String& jsonStr) {
    forecast.hourlyKeyPresent = (jsonStr.indexOf("\"hourly\"") >= 0);
    forecast.numHours = 0;

    if (!forecast.hourlyKeyPresent) {
        Serial.println("[FORECAST] 'hourly' key NOT found in payload");
        return;
    }

    // Find the start of the hourly array: "hourly" : [
    int keyIdx = jsonStr.indexOf("\"hourly\"");
    if (keyIdx < 0) {
        Serial.println("[FORECAST] 'hourly' key vanished?!");
        return;
    }
    int arrStart = jsonStr.indexOf('[', keyIdx);
    if (arrStart < 0) {
        Serial.println("[FORECAST] Could not find '[' after 'hourly'");
        return;
    }

    const int n = jsonStr.length();
    int i = arrStart + 1; // move past '['
    int count = 0;

    auto sanitizeBools = [](String &s){
        s.replace(":true",  ":1");
        s.replace(":false", ":0");
    };

    // Walk the array char-by-char; for each top-level { ... } object,
    // parse just that substring to keep memory usage tiny.
    while (i < n && count < MAX_FORECAST_HOURS) {
        // Skip whitespace/commas
        while (i < n) {
            char c = jsonStr[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',') { i++; continue; }
            break;
        }

        if (i >= n) break;
        if (jsonStr[i] == ']') { // end of array
            i++;
            break;
        }

        if (jsonStr[i] != '{') {
            // Unexpected token; advance cautiously
            i++;
            continue;
        }

        // Capture a single { ... } object safely (skip strings so braces in strings don't confuse us)
        int objStart = i++;
        int depth = 1;
        bool inString = false;
        while (i < n && depth > 0) {
            char c = jsonStr[i];

            if (inString) {
                if (c == '\\') { // escape
                    i += 2;      // skip escaped char
                    continue;
                } else if (c == '"') {
                    inString = false;
                }
                i++;
                continue;
            }

            if (c == '"') {
                inString = true;
            } else if (c == '{') {
                depth++;
            } else if (c == '}') {
                depth--;
                if (depth == 0) { i++; break; } // include closing brace
            }
            i++;
        }

        if (depth != 0) {
            Serial.println("[FORECAST] Hourly object brace mismatch");
            break;
        }

        String objStr = jsonStr.substring(objStart, i);
        sanitizeBools(objStr);

        JSONVar h = JSON.parse(objStr);
        if (JSON.typeof(h) != "object") {
            Serial.println("[FORECAST] Skipping non-object hourly entry");
            continue;
        }

        ForecastHour& fh = forecast.hours[count];
        fh.temp       = (JSON.typeof(h["air_temperature"])    == "number") ? (double)h["air_temperature"] : NAN;
        fh.rainChance = (JSON.typeof(h["precip_probability"]) == "number") ? (int)h["precip_probability"] : -1;
        fh.conditions = (JSON.typeof(h["conditions"])         == "string") ? String((const char*)h["conditions"]) : "";
        fh.icon       = (JSON.typeof(h["icon"])               == "string") ? String((const char*)h["icon"])       : "";
        fh.time       = (JSON.typeof(h["time"])               == "number") ? (uint32_t)(double)h["time"]          : 0;

        count++;
    }

    forecast.numHours = count;
    if (count == 0) {
        Serial.println("[FORECAST] Parsed 0 hourly entries");
    } else {
        // was: Serial.printf(...)
        Serial.print("[FORECAST] Parsed ");
        Serial.print(count);
        Serial.print(" hourly entries (max ");
        Serial.print(MAX_FORECAST_HOURS);
        Serial.println(")");
        Serial.print("  first=");
        Serial.print(forecast.hours[0].time);
        Serial.print(" last=");
        Serial.println(forecast.hours[count-1].time);
    }
}

// Back-compat wrapper: do all three
void updateForecastFromJson(const String& jsonStr) {
    updateCurrentConditionsFromJson(jsonStr);
    updateDailyForecastFromJson(jsonStr);
    updateHourlyForecastFromJson(jsonStr);
    forecast.lastUpdate = millis();
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
    String stationId = wfStationId;
    String token = wfToken;
    stationId.trim();
    token.trim();

    if (stationId.isEmpty() || token.isEmpty()) {
        Serial.println("[Tempest] Missing WeatherFlow credentials");
        forecast.numHours = 0;
        forecast.hourlyKeyPresent = false;
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Tempest] Forecast fetch skipped (WiFi offline)");
        return;
    }

    String url = "https://swd.weatherflow.com/swd/rest/better_forecast?station_id=" + stationId +
                 "&token=" + token;
    // Keep it simple: HTTP/1.0 + longer timeout helps on some networks
    http.useHTTP10(true);
    http.setTimeout(15000);

    if (!http.begin(url.c_str())) {
        Serial.println("[HTTP] begin() failed");
        forecast.numHours = 0;
        forecast.hourlyKeyPresent = false;
        return;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        // was: Serial.printf("[HTTP] Forecast error: %d\n", httpCode);
        Serial.print("[HTTP] Forecast error: ");
        Serial.println(httpCode);
        http.end();
        forecast.numHours = 0;
        forecast.hourlyKeyPresent = false;
        return;
    }

    String payload = http.getString();
    http.end();

    updateForecastFromJson(payload); // calls all 3 parts
}

// --------- String Helpers ----------
String getTempestField(const char* field) {
    if (!strcmp(field, "temp"))        return isnan(tempest.temperature) ? "--" : fmtTemp(tempest.temperature, 1);
    if (!strcmp(field, "hum"))         return isnan(tempest.humidity)    ? "--" : String(tempest.humidity, 0) + "%";
    if (!strcmp(field, "pres"))        return isnan(tempest.pressure)    ? "--" : fmtPress(tempest.pressure, 1);
    if (!strcmp(field, "wind"))        return isnan(tempest.windAvg)     ? "--" : fmtWind(tempest.windAvg, 1);
    if (!strcmp(field, "winddir"))     return isnan(tempest.windDir)     ? "--" : String(tempest.windDir, 0) + "°";
    if (!strcmp(field, "uv"))          return isnan(tempest.uv)          ? "--" : String(tempest.uv, 1);
    if (!strcmp(field, "solar"))       return isnan(tempest.solar)       ? "--" : String(tempest.solar, 0) + "W";
    if (!strcmp(field, "rain"))        return isnan(tempest.rain)        ? "--" : fmtPrecip(tempest.rain, 2);
    if (!strcmp(field, "battery"))     return isnan(tempest.battery)     ? "--" : String(tempest.battery, 2) + "V";
    if (!strcmp(field, "illuminance")) return isnan(tempest.illuminance) ? "--" : String(tempest.illuminance, 0) + "lx";
    if (!strcmp(field, "gust"))        return isnan(tempest.windGust)    ? "--" : fmtWind(tempest.windGust, 1);
    if (!strcmp(field, "lull"))        return isnan(tempest.windLull)    ? "--" : fmtWind(tempest.windLull, 1);
    if (!strcmp(field, "preciptype"))  return String(tempest.precipType);
    if (!strcmp(field, "strikes"))     return String(tempest.strikeCount);
    if (!strcmp(field, "strikedist"))  return isnan(tempest.strikeDist)  ? "--" : String(tempest.strikeDist, 1) + "km";
    if (!strcmp(field, "obs_time"))    return tempest.lastObsTime;
    return "";
}


String getRapidWindField(const char* field) {
    if (!strcmp(field, "speed"))   return isnan(tempest.windAvg) ? String("--") : fmtWind(tempest.windAvg, 1);
    if (!strcmp(field, "dir"))     return isnan(tempest.windDir) ? String("--") : String(tempest.windDir, 0) + "°";
    if (!strcmp(field, "epoch"))   return String(tempest.epoch);
    if (!strcmp(field, "time"))    return formatEpochTime(tempest.epoch);
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
        udpScreen.setLines(lines, 9, false);
    }
}

void showForecastScreen() {
    int num = forecast.numDays;
    // was: Serial.printf("Showing %d forecast days\n", num);
    Serial.print("Showing ");
    Serial.print(num);
    Serial.println(" forecast days");

    String lines[MAX_FORECAST_DAYS];
    if (num == 0) {
        lines[0] = "No forecast data!";
        forecastScreen.setLines(lines, 1);
        forecastScreen.show([](){ currentScreen = ScreenMode::SCREEN_OWM; });
        return;
    }
    for (int i = 0; i < num; ++i) {
        const ForecastDay& f = forecast.days[i];
        lines[i] = String(f.monthNum) + "/" + String(f.dayNum) + ": " +
                (isnan(f.highTemp) ? String("--") : fmtTemp(f.highTemp, 0)) + "/" +
                (isnan(f.lowTemp)  ? String("--") : fmtTemp(f.lowTemp,  0)) + "  " +
                f.conditions + " " + String(f.rainChance) + "%";
    }

    forecastScreen.setLines(lines, num);
    forecastScreen.show([](){ currentScreen = ScreenMode::SCREEN_OWM; });
}

void showHourlyForecastScreen() {
    // How many hours to show on the screen (compile-time constant)
    static constexpr uint8_t HOURLY_DISPLAY_COUNT = 24;

    // Case 1: No hourly data parsed at all
    if (forecast.numHours <= 0) {
        String lines[1];
        lines[0] = "No hour data.";
        hourlyScreen.setLines(lines, 1, true);
        hourlyScreen.show([](){ currentScreen = ScreenMode::SCREEN_OWM; });
        return;
    }

    // Find first entry at/after "now" (tolerance -60s)
    uint32_t nowTs = (uint32_t)time(nullptr);
    int start = 0;
    while (start < forecast.numHours && forecast.hours[start].time < nowTs - 60) start++;

    // Compute how many we can actually show
    int count = min((int)HOURLY_DISPLAY_COUNT, forecast.numHours - start);

    // Case 2: We have hourly data, but all hours are in the past
    if (count <= 0) {
        String lines[1];
        lines[0] = "No upcoming hour data.";
        hourlyScreen.setLines(lines, 1, true);
        hourlyScreen.show([](){ currentScreen = ScreenMode::SCREEN_OWM; });
        return;
    }

    // Build lines for the next up-to-HOURLY_DISPLAY_COUNT hours
    String lines[HOURLY_DISPLAY_COUNT];
    for (int i = 0; i < count; ++i) {
        const ForecastHour& h = forecast.hours[start + i];

        time_t tt = (time_t)h.time;
        struct tm* ti = localtime(&tt);

        // e.g., "14:00  21.5C  30%"
        String line;
        int hh = ti ? ti->tm_hour : 0;
        int mm = ti ? ti->tm_min  : 0;

        if (hh < 10) line += '0';
        line += String(hh);
        line += ':';
        if (mm < 10) line += '0';
        line += String(mm);

        line += "  ";
        line += isnan(h.temp) ? String("--") : fmtTemp(h.temp, 1);
        line += "  ";
        line += (h.rainChance >= 0) ? String(h.rainChance) : String('-');
        line += '%';


        lines[i] = line;
        if (h.conditions.length()) lines[i] += "  " + h.conditions;
    }

    hourlyScreen.setLines(lines, count, true);
    hourlyScreen.show([](){ currentScreen = ScreenMode::SCREEN_OWM; });
}

void showRapidWindScreen() {
    String lines[3];
    lines[0] = "Speed: " + getRapidWindField("speed");
    lines[1] = "Dir:   " + getRapidWindField("dir");
    lines[2] = "Time:  " + getRapidWindField("time");
    if (!rapidWindScreen.isActive()) {
        rapidWindScreen.setLines(lines, 3, true);
        rapidWindScreen.show([](){ currentScreen = SCREEN_OWM; });
    } else {
        rapidWindScreen.setLines(lines, 3, false);
    }
}

void showWindDirectionScreen() {
    dma_display->fillScreen(0);
    int cx = 10;
    int cy = dma_display->height() / 2;
    windMeter.drawWindDirection(cx, cy, tempest.windDir, tempest.windAvg);

    const bool monoTheme = (theme == 1);
    const uint16_t primaryText = monoTheme ? dma_display->color565(90,90,150)
                                           : dma_display->color565(255, 255, 255);
    const uint16_t accentText = monoTheme ? dma_display->color565(60,60,120)
                                          : myCYAN;
    const uint16_t lineColor = monoTheme ? dma_display->color565(60,60,120)
                                         : 0xFFFF;

    dma_display->setTextColor(primaryText);
    dma_display->setCursor(cx + 12, cy - 10);

    if (isnan(tempest.windDir)) {
        dma_display->print("No data");
        return;
    }

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
    scrollLine.draw(0, 0, lineColor);

    windInfo.update();
    windInfo.draw(0,24,lineColor);

    dma_display->setTextColor(accentText);
    dma_display->setCursor(cx + 12, 8);

    // was: dma_display->printf("%s %d°", ...)
    dma_display->print(dirNames[nearestIndex]);
    dma_display->print(' ');
    dma_display->print(static_cast<int>(tempest.windDir));
    dma_display->print((char)0xC2); // UTF-8 degree symbol
    dma_display->print((char)0xB0);

    dma_display->setCursor(cx + 12, cy);
    if (isnan(tempest.windAvg)) {
        dma_display->print("--");
    } else {
        dma_display->print(fmtWind(tempest.windAvg, 1));
    }

}

void showCurrentConditionsScreen() {
    String lines[8];
    lines[0] = "Temp:   " + (isnan(currentCond.temp)      ? String("--") : fmtTemp(currentCond.temp, 1));
    lines[1] = "Feels:  " + (isnan(currentCond.feelsLike) ? String("--") : fmtTemp(currentCond.feelsLike, 1));
    lines[2] = "DewPt:  " + (isnan(currentCond.dewPoint)  ? String("--") : fmtTemp(currentCond.dewPoint, 1));
    lines[3] = "Humid:  " + (currentCond.humidity < 0     ? String("--") : String(currentCond.humidity) + "%");
    lines[4] = "Press:  " + (isnan(currentCond.pressure)  ? String("--") : fmtPress(currentCond.pressure, 1));
    lines[5] = "Wind:   " + (isnan(currentCond.windAvg)   ? String("--") : fmtWind(currentCond.windAvg, 1)) + " " + currentCond.windCardinal;
    lines[6] = "Gust:   " + (isnan(currentCond.windGust)  ? String("--") : fmtWind(currentCond.windGust, 1));
    lines[7] = "Cond:   " + (String(currentCond.cond).length() == 0 ? String("--") : currentCond.cond);
    currentCondScreen.setLines(lines, 8);
    currentCondScreen.show([](){ currentScreen = ScreenMode::SCREEN_OWM; });

}


