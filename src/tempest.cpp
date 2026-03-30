#include "tempest.h"
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#ifdef typeof
#undef typeof
#endif
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <time.h>
#include "ScrollLine.h"
#include "screen_manager.h"
#include "units.h"
#include "settings.h"

extern WiFiUDP udp;
extern InfoScreen udpScreen;
extern InfoScreen lightningScreen;
extern InfoScreen forecastScreen;
extern InfoScreen currentCondScreen;
extern InfoScreen hourlyScreen;
extern ScreenMode currentScreen;
extern WindMeter windMeter;
extern ScrollLine scrollLine;
extern ScrollLine windInfo;
extern int scrollSpeed;
extern int theme;
extern bool useImperial;

static String formatWindDirectionLabel(double degrees);
static String formatSampleInterval(double seconds);
static String formatWindTimestamp(uint32_t epoch);
static String composeWindInfoLine();
static bool shouldProcessRapidWind();
static bool readHttpBody(HTTPClient &http, String &bodyOut, size_t maxBytes, unsigned long timeoutMs);
static String formatLightningDistance(double km);
static bool isLightningSummaryFresh(unsigned long nowMs);
static bool isLightningEventFresh(unsigned long nowMs);
static String lightningAgeShort(unsigned long nowMs, unsigned long updateMs);
static void handleLightningAlertForEvent(uint32_t epoch, double distanceKm, uint32_t energy);

static constexpr unsigned long kLightningStaleTimeoutMs = 15UL * 60UL * 1000UL;
static constexpr unsigned long kLightningRetriggerCooldownMs = 2UL * 60UL * 1000UL;
static constexpr uint16_t kLightningAlertDisplayMs = 2800;
static constexpr double kLightningNearbyThresholdKm = 16.0;
static constexpr size_t kOpenMeteoMaxBodyBytes = 24576;
static constexpr size_t kWeatherFlowForecastMaxBodyBytes = 49152;

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
static uint32_t s_lastLightningAlertEpoch = 0;
static unsigned long s_lastLightningAlertMs = 0;

static bool shouldProcessRapidWind() {
    return isDataSourceWeatherFlow() && currentScreen == SCREEN_WIND_DIR;
}

static bool readHttpBody(HTTPClient &http, String &bodyOut, size_t maxBytes, unsigned long timeoutMs)
{
    bodyOut = "";

    const int contentLength = http.getSize();
    if (contentLength > 0)
    {
        if (contentLength > static_cast<int>(maxBytes))
        {
            Serial.printf("[HTTP] Payload too large: %d bytes\n", contentLength);
            return false;
        }
        bodyOut.reserve(static_cast<unsigned>(contentLength + 1));
    }
    else
    {
        bodyOut.reserve(static_cast<unsigned>(min<size_t>(4096, maxBytes)));
    }

    WiFiClient *stream = http.getStreamPtr();
    if (!stream)
    {
        Serial.println("[HTTP] Stream pointer unavailable");
        return false;
    }

    char buffer[257];
    unsigned long lastDataMs = millis();
    unsigned long startMs = lastDataMs;
    bool receivedAnyBytes = false;
    while (http.connected() || stream->available() > 0)
    {
        int available = stream->available();
        if (available <= 0)
        {
            if ((millis() - lastDataMs) >= timeoutMs)
            {
                Serial.printf("[HTTP] Body read timeout after %lu ms (connected=%d)\n",
                              millis() - startMs,
                              http.connected() ? 1 : 0);
                break;
            }
            delay(1);
            continue;
        }

        int toRead = available;
        if (toRead > static_cast<int>(sizeof(buffer) - 1))
            toRead = static_cast<int>(sizeof(buffer) - 1);

        int n = stream->readBytes(buffer, static_cast<size_t>(toRead));
        if (n <= 0)
            continue;

        lastDataMs = millis();
        receivedAnyBytes = true;
        buffer[n] = '\0';

        if ((bodyOut.length() + static_cast<unsigned>(n)) > maxBytes)
        {
            Serial.printf("[HTTP] Body exceeded %u bytes\n", static_cast<unsigned>(maxBytes));
            return false;
        }

        bodyOut += buffer;
        if (contentLength > 0 && bodyOut.length() >= static_cast<unsigned>(contentLength))
            break;
    }

    if (!receivedAnyBytes)
    {
        Serial.printf("[HTTP] No body bytes received (contentLength=%d connected=%d)\n",
                      contentLength,
                      http.connected() ? 1 : 0);
    }

    return bodyOut.length() > 0;
}

// --------- Tempest UDP JSON Parsing ----------
void updateTempestFromUDP(const char* jsonStr) {
    if (!isDataSourceWeatherFlow()) {
        return;
    }
    JSONVar doc = JSON.parse(jsonStr);
    if (JSON.typeof_(doc) == "undefined") return;
    String type = (const char*)doc["type"];

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
        tempest.lightningSummaryEpoch = tempest.epoch;
        tempest.lightningSummaryLastUpdate = millis();
        tempest.battery       = (double)obs[16];
        tempest.reportInt     = (int)obs[17];
        tempest.lastObsTime   = String((uint32_t)obs[0]);
        tempest.lastUpdate    = millis();
        tempest.obsWindAvg    = tempest.windAvg;
        tempest.obsWindDir    = tempest.windDir;
        tempest.obsEpoch      = tempest.epoch;
        tempest.obsLastUpdate = tempest.lastUpdate;
        newTempestData = true;
        updateWindInfoScroll(false);
    }
    else if (type == "evt_strike" && doc.hasOwnProperty("evt")) {
        JSONVar evt = doc["evt"];
        if (evt.length() >= 3) {
            tempest.lightningLastEventEpoch = (uint32_t)evt[0];
            tempest.lightningLastEventDistanceKm = (double)evt[1];
            tempest.lightningLastEventEnergy = (uint32_t)(double)evt[2];
            tempest.lightningLastEventUpdate = millis();
            newTempestData = true;
            handleLightningAlertForEvent(tempest.lightningLastEventEpoch,
                                         tempest.lightningLastEventDistanceKm,
                                         tempest.lightningLastEventEnergy);
        }
    }
    else if (type == "rapid_wind" && doc.hasOwnProperty("ob")) {
        if (!shouldProcessRapidWind()) {
            return;
        }
        JSONVar ob = doc["ob"];
        if (ob.length() == 3) {
            tempest.epoch      = (uint32_t)ob[0];
            tempest.windAvg    = (double)ob[1];
            tempest.windDir    = (double)ob[2];
            tempest.lastUpdate = millis();
            tempest.rapidWindAvg    = tempest.windAvg;
            tempest.rapidWindDir    = tempest.windDir;
            tempest.rapidEpoch      = tempest.epoch;
            tempest.rapidLastUpdate = tempest.lastUpdate;
            newRapidWindData   = true;

            updateWindInfoScroll(false);
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
    bool inString = false;
    bool escaped = false;
    while (i < src.length() && brace > 0) {
        char c = src[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
        } else {
            if (c == '"') inString = true;
            else if (c == '{') ++brace;
            else if (c == '}') --brace;
        }
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
    bool inString = false;
    bool escaped = false;
    for (int i = arrayStart; i < json.length(); ++i) {
        char c = json[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
            continue;
        }
        if (c == '[') depth++;
        else if (c == ']') {
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

static String formatWindDirectionLabel(double degrees) {
    if (isnan(degrees)) return "--";
    static const char* names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    double normalized = fmod(degrees, 360.0);
    if (normalized < 0) normalized += 360.0;
    int index = static_cast<int>(floor((normalized + 22.5) / 45.0)) % 8;
    int degInt = static_cast<int>(floor(normalized + 0.5));
    if (degInt >= 360) degInt -= 360;
    return String(names[index]) + " " + String(degInt) + "°";
}

static String formatSampleInterval(double seconds) {
    if (isnan(seconds)) return String("--");
    int sec = static_cast<int>(floor(seconds + 0.5));
    if (sec < 0) sec = 0;
    return String(sec) + "s";
}

static String formatWindTimestamp(uint32_t epoch) {
    if (epoch == 0) return "--";
    time_t rawTime = static_cast<time_t>(epoch);
    struct tm* ti = localtime(&rawTime);
    if (!ti) return "--";
    char buf[9];
    strftime(buf, sizeof(buf), "%H:%M:%S", ti);
    return String(buf);
}

static String composeWindInfoLine() {
    String line;
    line.reserve(192);

    const String obsAvg = isnan(tempest.obsWindAvg) ? String("--") : fmtWind(tempest.obsWindAvg, 1);
    const String gust   = isnan(tempest.windGust)   ? String("--") : fmtWind(tempest.windGust, 1);
    const String lull   = isnan(tempest.windLull)   ? String("--") : fmtWind(tempest.windLull, 1);
    const String obsDir = formatWindDirectionLabel(tempest.obsWindDir);
    const String sample = formatSampleInterval(tempest.windSampleInt);
    const String obsAt  = formatWindTimestamp(tempest.obsEpoch);

    const String rapidAvg = isnan(tempest.rapidWindAvg) ? String("--") : fmtWind(tempest.rapidWindAvg, 1);
    const String rapidDir = formatWindDirectionLabel(tempest.rapidWindDir);
    const String rapidAt  = formatWindTimestamp(tempest.rapidEpoch);


    line += "¦ Observe at: " + obsAt + " ¦";
    line += " Avg: " + obsAvg  + " ¦";
    line += " Gust: " + gust  + " ¦";
    line += " Lull: " + lull  + " ¦";
    line += " Dir: " + ((obsDir == "--") ? String("--") : obsDir) + " ¦";
    /* 
    line += " Sample: " + sample + " ¦";
    line += " Rapid: " + rapidAvg ;
    if (rapidDir != "--") {
        line += " " + rapidDir;
    }
    line += " at: " + rapidAt;
    */
    return line;
}

void updateWindInfoScroll(bool resetPosition) {
    String line = composeWindInfoLine();
    if (line.length() == 0) {
        line = "No wind data available";
    }

    static String previousLine;
    bool contentChanged = (line != previousLine);
    if (!contentChanged && !resetPosition) {
        return;
    }

    String lines[1];
    lines[0] = line;
    windInfo.setLines(lines, 1, resetPosition);
    previousLine = line;
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
        if (cur != nullptr && JSON.typeof_(cur) == "array" && cur.length() > 0) cur = cur[0];
        else cur = nullptr;
    }

    if (cur == nullptr || JSON.typeof_(cur) != "object") {
        currentCond = CurrentConditions{};
        currentCond.humidity = -1;
        currentCond.uv = -1;
        currentCond.precipProb = -1;
        Serial.println("[FORECAST] No valid current_conditions object");
        return;
    }

    currentCond.temp         = (JSON.typeof_(cur["air_temperature"]) == "number") ? (double)cur["air_temperature"] : NAN;
    currentCond.feelsLike    = (JSON.typeof_(cur["feels_like"]) == "number") ? (double)cur["feels_like"] : NAN;
    currentCond.dewPoint     = (JSON.typeof_(cur["dew_point"]) == "number") ? (double)cur["dew_point"] : NAN;
    currentCond.humidity     = (JSON.typeof_(cur["relative_humidity"]) == "number") ? (int)cur["relative_humidity"] : -1;
    currentCond.pressure     = (JSON.typeof_(cur["sea_level_pressure"]) == "number") ? (double)cur["sea_level_pressure"] : NAN;
    currentCond.windAvg      = (JSON.typeof_(cur["wind_avg"]) == "number") ? (double)cur["wind_avg"] : NAN;
    currentCond.windGust     = (JSON.typeof_(cur["wind_gust"]) == "number") ? (double)cur["wind_gust"] : NAN;
    currentCond.windDir      = (JSON.typeof_(cur["wind_direction"]) == "number") ? (double)cur["wind_direction"] : NAN;
    currentCond.windCardinal = (JSON.typeof_(cur["wind_direction_cardinal"]) == "string") ? (const char*)cur["wind_direction_cardinal"] : "";
    currentCond.uv           = (JSON.typeof_(cur["uv"]) == "number") ? (int)cur["uv"] : -1;
    currentCond.precipProb   = (JSON.typeof_(cur["precip_probability"]) == "number") ? (int)cur["precip_probability"] : -1;
    currentCond.cond         = (JSON.typeof_(cur["conditions"]) == "string") ? (const char*)cur["conditions"] : "";
    currentCond.icon         = (JSON.typeof_(cur["icon"]) == "string") ? (const char*)cur["icon"] : "";
    currentCond.time         = (JSON.typeof_(cur["time"]) == "number") ? (uint32_t)(double)cur["time"] : 0;

    Serial.println("[FORECAST] Current conditions updated");
}

void updateDailyForecastFromJson(const String& jsonStr) {
    ForecastDay parsed[MAX_FORECAST_DAYS];
    int parsedDays = 0;

    auto parseDailyObject = [&](JSONVar d) {
            if (JSON.typeof_(d) != "object") {
                return;
            }
            ForecastDay f;
            f.highTemp   = (JSON.typeof_(d["air_temp_high"]) == "number") ? (double)d["air_temp_high"] : NAN;
            f.lowTemp    = (JSON.typeof_(d["air_temp_low"])  == "number") ? (double)d["air_temp_low"]  : NAN;
            f.rainChance = (JSON.typeof_(d["precip_probability"]) == "number") ? (int)d["precip_probability"] : -1;
            f.conditions = (JSON.typeof_(d["conditions"]) == "string") ? (const char*)d["conditions"] : "";
            f.icon       = (JSON.typeof_(d["icon"])       == "string") ? (const char*)d["icon"]       : "";
            f.sunrise    = (JSON.typeof_(d["sunrise"])    == "number") ? (uint32_t)(double)d["sunrise"] : 0;
            f.sunset     = (JSON.typeof_(d["sunset"])     == "number") ? (uint32_t)(double)d["sunset"]  : 0;
            f.dayNum     = (JSON.typeof_(d["day_num"])    == "number") ? (int)d["day_num"]    : 0;
            f.monthNum   = (JSON.typeof_(d["month_num"])  == "number") ? (int)d["month_num"]  : 0;
            f.yearNum    = (JSON.typeof_(d["year_num"])   == "number") ? (int)d["year_num"]   : 0;
            if (f.yearNum <= 0)
            {
                uint32_t dayEpoch = (JSON.typeof_(d["time"]) == "number") ? (uint32_t)(double)d["time"] : 0;
                if (dayEpoch > 0)
                {
                    time_t tt = static_cast<time_t>(dayEpoch);
                    struct tm *ti = localtime(&tt);
                    if (ti)
                    {
                        f.yearNum = ti->tm_year + 1900;
                        if (f.monthNum <= 0) f.monthNum = ti->tm_mon + 1;
                        if (f.dayNum <= 0) f.dayNum = ti->tm_mday;
                    }
                }
            }
            parsed[parsedDays++] = f;
    };

    auto parseDailyJsonArray = [&](JSONVar daily) {
        int days = min((int)daily.length(), MAX_FORECAST_DAYS);
        for (int i = 0; i < days; ++i) {
            parseDailyObject(daily[i]);
        }
    };

    auto parseDailyArrayString = [&](const String& dailyArrayStr) {
        if (dailyArrayStr.length() == 0) {
            return;
        }

        const int n = dailyArrayStr.length();
        int i = 0;
        while (i < n && parsedDays < MAX_FORECAST_DAYS) {
            while (i < n && dailyArrayStr[i] != '{') {
                ++i;
            }
            if (i >= n) break;

            int start = i;
            int braceDepth = 0;
            bool inString = false;
            bool escaped = false;
            for (; i < n; ++i) {
                char c = dailyArrayStr[i];
                if (inString) {
                    if (escaped) {
                        escaped = false;
                    } else if (c == '\\') {
                        escaped = true;
                    } else if (c == '"') {
                        inString = false;
                    }
                    continue;
                }
                if (c == '"') {
                    inString = true;
                    continue;
                }
                if (c == '{') {
                    ++braceDepth;
                } else if (c == '}') {
                    --braceDepth;
                    if (braceDepth == 0) {
                        String objStr = dailyArrayStr.substring(start, i + 1);
                        _sanitizeBools(objStr);
                        JSONVar d = JSON.parse(objStr);
                        parseDailyObject(d);
                        ++i;
                        break;
                    }
                }
            }
        }
    };

    auto parseDailyArrayFromSource = [&](const String& src, int keyIdx) {
        if (keyIdx < 0) {
            return;
        }
        const int arrayStart = src.indexOf('[', keyIdx);
        if (arrayStart < 0) {
            return;
        }

        int depth = 0;
        int arrayEnd = -1;
        bool inString = false;
        bool escaped = false;
        for (int i = arrayStart; i < src.length(); ++i) {
            char c = src[i];
            if (inString) {
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    inString = false;
                }
                continue;
            }
            if (c == '"') {
                inString = true;
                continue;
            }
            if (c == '[') {
                ++depth;
            } else if (c == ']') {
                --depth;
                if (depth == 0) {
                    arrayEnd = i;
                    break;
                }
            }
        }

        if (arrayEnd < 0) {
            return;
        }

        const int n = arrayEnd;
        int i = arrayStart + 1;
        while (i < n && parsedDays < MAX_FORECAST_DAYS) {
            while (i < n && src[i] != '{') {
                ++i;
            }
            if (i >= n) break;

            int start = i;
            int braceDepth = 0;
            bool inObjString = false;
            bool objEscaped = false;
            for (; i < n; ++i) {
                char c = src[i];
                if (inObjString) {
                    if (objEscaped) {
                        objEscaped = false;
                    } else if (c == '\\') {
                        objEscaped = true;
                    } else if (c == '"') {
                        inObjString = false;
                    }
                    continue;
                }
                if (c == '"') {
                    inObjString = true;
                    continue;
                }
                if (c == '{') {
                    ++braceDepth;
                } else if (c == '}') {
                    --braceDepth;
                    if (braceDepth == 0) {
                        String objStr = src.substring(start, i + 1);
                        _sanitizeBools(objStr);
                        JSONVar d = JSON.parse(objStr);
                        parseDailyObject(d);
                        ++i;
                        break;
                    }
                }
            }
        }
    };

    auto parseDailyIndexedObject = [&](JSONVar daily) {
        for (int i = 0; i < MAX_FORECAST_DAYS; ++i) {
            JSONVar d = daily[i];
            if (JSON.typeof_(d) != "object") break;
            parseDailyObject(d);
        }
    };

    auto parseDailyFieldObject = [&](JSONVar daily) {
        auto arrLen = [](JSONVar arr) -> int {
            return (JSON.typeof_(arr) == "array") ? static_cast<int>(arr.length()) : 0;
        };
        auto arrNum = [](JSONVar arr, int idx) -> double {
            if (JSON.typeof_(arr) != "array" || idx < 0 || idx >= static_cast<int>(arr.length())) return NAN;
            JSONVar v = arr[idx];
            return (JSON.typeof_(v) == "number") ? static_cast<double>(v) : NAN;
        };
        auto arrStr = [](JSONVar arr, int idx) -> String {
            if (JSON.typeof_(arr) != "array" || idx < 0 || idx >= static_cast<int>(arr.length())) return "";
            JSONVar v = arr[idx];
            return (JSON.typeof_(v) == "string") ? String((const char*)v) : String("");
        };

        JSONVar timeArr = daily["day_start_local"];
        if (arrLen(timeArr) == 0) timeArr = daily["time"];
        JSONVar hiArr = daily["air_temp_high"];
        JSONVar loArr = daily["air_temp_low"];
        JSONVar precipArr = daily["precip_probability"];
        if (arrLen(precipArr) == 0) precipArr = daily["precip_probability_max"];
        JSONVar condArr = daily["conditions"];
        JSONVar iconArr = daily["icon"];
        JSONVar sunriseArr = daily["sunrise"];
        JSONVar sunsetArr = daily["sunset"];

        int days = arrLen(timeArr);
        if (days == 0) days = arrLen(hiArr);
        if (days == 0) days = arrLen(loArr);
        days = min(days, MAX_FORECAST_DAYS);

        for (int i = 0; i < days; ++i) {
            ForecastDay f;
            f.highTemp = arrNum(hiArr, i);
            f.lowTemp = arrNum(loArr, i);
            double precip = arrNum(precipArr, i);
            f.rainChance = isnan(precip) ? -1 : static_cast<int>(precip);
            f.conditions = arrStr(condArr, i);
            f.icon = arrStr(iconArr, i);
            double sunrise = arrNum(sunriseArr, i);
            double sunset = arrNum(sunsetArr, i);
            f.sunrise = isnan(sunrise) ? 0U : static_cast<uint32_t>(sunrise);
            f.sunset = isnan(sunset) ? 0U : static_cast<uint32_t>(sunset);

            double dayEpoch = arrNum(timeArr, i);
            if (!isnan(dayEpoch) && dayEpoch > 0)
            {
                time_t tt = static_cast<time_t>(dayEpoch);
                struct tm *ti = localtime(&tt);
                if (ti)
                {
                    f.dayNum = ti->tm_mday;
                    f.monthNum = ti->tm_mon + 1;
                    f.yearNum = ti->tm_year + 1900;
                }
            }

            parsed[parsedDays++] = f;
        }
    };

    int forecastIdx = jsonStr.indexOf("\"forecast\"");
    if (forecastIdx < 0) {
        Serial.println("[ERROR] No 'forecast' found (daily) - keeping previous daily data");
        return;
    }

    int dailyIdx = jsonStr.indexOf("\"daily\"", forecastIdx);
    if (dailyIdx < 0) {
        Serial.println("[ERROR] No 'daily' in forecast - keeping previous daily data");
        return;
    }

    parseDailyArrayFromSource(jsonStr, dailyIdx);
    if (parsedDays <= 0) {
        Serial.println("[ERROR] Parsed daily forecast block invalid - keeping previous daily data");
        return;
    }

    if (parsedDays <= 0) {
        Serial.println("[FORECAST] Parsed 0 daily entries - keeping previous daily data");
        return;
    }

    forecast.numDays = parsedDays;
    for (int i = 0; i < parsedDays; ++i) {
        forecast.days[i] = parsed[i];
    }
    Serial.print("[FORECAST] Parsed ");
    Serial.print(forecast.numDays);
    Serial.println(" daily entries");
}

// Parse hourly array by extracting individual objects (memory-safe on ESP32)
void updateHourlyForecastFromJson(const String& jsonStr) {
    forecast.hourlyKeyPresent = (jsonStr.indexOf("\"hourly\"") >= 0);
    ForecastHour parsed[MAX_FORECAST_HOURS];
    int parsedCount = 0;

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
        if (JSON.typeof_(h) != "object") {
            Serial.println("[FORECAST] Skipping non-object hourly entry");
            continue;
        }

        ForecastHour fh;
        fh.temp       = (JSON.typeof_(h["air_temperature"])    == "number") ? (double)h["air_temperature"] : NAN;
        fh.rainChance = (JSON.typeof_(h["precip_probability"]) == "number") ? (int)h["precip_probability"] : -1;
        fh.conditions = (JSON.typeof_(h["conditions"])         == "string") ? String((const char*)h["conditions"]) : "";
        fh.icon       = (JSON.typeof_(h["icon"])               == "string") ? String((const char*)h["icon"])       : "";
        fh.time       = (JSON.typeof_(h["time"])               == "number") ? (uint32_t)(double)h["time"]          : 0;

        parsed[count] = fh;
        count++;
    }

    if (count == 0) {
        Serial.println("[FORECAST] Parsed 0 hourly entries - keeping previous hourly data");
        return;
    } else {
        // was: Serial.printf(...)
        Serial.print("[FORECAST] Parsed ");
        Serial.print(count);
        Serial.print(" hourly entries (max ");
        Serial.print(MAX_FORECAST_HOURS);
        Serial.println(")");
        Serial.print("  first=");
        Serial.print(parsed[0].time);
        Serial.print(" last=");
        Serial.println(parsed[count-1].time);
    }

    forecast.numHours = count;
    for (int j = 0; j < count; ++j) {
        forecast.hours[j] = parsed[j];
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
    if (!isDataSourceWeatherFlow()) {
        return;
    }
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

static String openMeteoConditionFromCode(int code) {
    switch (code) {
    case 0: return "Clear";
    case 1: return "Mostly Clear";
    case 2: return "Partly Cloudy";
    case 3: return "Overcast";
    case 45:
    case 48: return "Fog";
    case 51:
    case 53:
    case 55: return "Drizzle";
    case 56:
    case 57: return "Freezing Drizzle";
    case 61:
    case 63:
    case 65: return "Rain";
    case 66:
    case 67: return "Freezing Rain";
    case 71:
    case 73:
    case 75:
    case 77: return "Snow";
    case 80:
    case 81:
    case 82: return "Rain Showers";
    case 85:
    case 86: return "Snow Showers";
    case 95:
    case 96:
    case 99: return "Thunderstorm";
    default: return "Weather";
    }
}

static String windCardinalFromDegrees(double deg) {
    if (isnan(deg)) return "";
    static const char* labels[8] = {"N","NE","E","SE","S","SW","W","NW"};
    double normalized = fmod(deg, 360.0);
    if (normalized < 0) normalized += 360.0;
    int idx = static_cast<int>(floor((normalized + 22.5) / 45.0)) % 8;
    return String(labels[idx]);
}

static bool resolveOpenMeteoCoordinates(double &lat, double &lon) {
    // Prefer user-configured device location first (shared with NOAA).
    if (noaaLatitude >= -90.0f && noaaLatitude <= 90.0f &&
        noaaLongitude >= -180.0f && noaaLongitude <= 180.0f &&
        (fabs(noaaLatitude) > 0.001f || fabs(noaaLongitude) > 0.001f)) {
        lat = static_cast<double>(noaaLatitude);
        lon = static_cast<double>(noaaLongitude);
        return true;
    }

    int tzIdx = timezoneCurrentIndex();
    if (tzIdx >= 0 && tzIdx < static_cast<int>(timezoneCount())) {
        const TimezoneInfo &tz = timezoneInfoAt(static_cast<size_t>(tzIdx));
        if (tz.latitude >= -90.0f && tz.latitude <= 90.0f &&
            tz.longitude >= -180.0f && tz.longitude <= 180.0f) {
            lat = static_cast<double>(tz.latitude);
            lon = static_cast<double>(tz.longitude);
            return true;
        }
    }

    return false;
}

static bool parseOpenMeteoForecastPayload(const String &payload) {
    JSONVar doc = JSON.parse(payload);
    if (JSON.typeof_(doc) != "object") {
        Serial.println("[Open-Meteo] JSON parse failed");
        return false;
    }

    auto toNumber = [](JSONVar obj, const char *key) -> double {
        if (JSON.typeof_(obj) != "object") return NAN;
        JSONVar v = obj[key];
        return (JSON.typeof_(v) == "number") ? static_cast<double>(v) : NAN;
    };
    auto toInt = [](JSONVar obj, const char *key, int fallback) -> int {
        if (JSON.typeof_(obj) != "object") return fallback;
        JSONVar v = obj[key];
        return (JSON.typeof_(v) == "number") ? static_cast<int>(static_cast<double>(v)) : fallback;
    };
    auto arrLen = [](JSONVar arr) -> int {
        return (JSON.typeof_(arr) == "array") ? static_cast<int>(arr.length()) : 0;
    };
    auto arrNum = [](JSONVar arr, int idx) -> double {
        if (JSON.typeof_(arr) != "array" || idx < 0 || idx >= static_cast<int>(arr.length())) return NAN;
        JSONVar v = arr[idx];
        return (JSON.typeof_(v) == "number") ? static_cast<double>(v) : NAN;
    };
    auto arrInt = [&](JSONVar arr, int idx, int fallback) -> int {
        double n = arrNum(arr, idx);
        return isnan(n) ? fallback : static_cast<int>(n);
    };

    // --- Current ---
    JSONVar current = doc["current"];
    currentCond = CurrentConditions{};
    currentCond.humidity = -1;
    currentCond.uv = -1;
    currentCond.precipProb = -1;

    if (JSON.typeof_(current) == "object") {
        currentCond.temp = toNumber(current, "temperature_2m");
        currentCond.feelsLike = toNumber(current, "apparent_temperature");
        currentCond.dewPoint = toNumber(current, "dew_point_2m");
        currentCond.humidity = toInt(current, "relative_humidity_2m", -1);
        currentCond.pressure = toNumber(current, "pressure_msl");
        currentCond.windAvg = toNumber(current, "wind_speed_10m");
        currentCond.windGust = toNumber(current, "wind_gusts_10m");
        currentCond.windDir = toNumber(current, "wind_direction_10m");
        currentCond.windCardinal = windCardinalFromDegrees(currentCond.windDir);
        currentCond.time = static_cast<uint32_t>(toInt(current, "time", 0));
        int code = toInt(current, "weather_code", -1);
        currentCond.cond = openMeteoConditionFromCode(code);
        currentCond.icon = currentCond.cond;
    }

    // --- Daily forecast (up to 10 days) ---
    JSONVar daily = doc["daily"];
    forecast.numDays = 0;
    if (JSON.typeof_(daily) == "object") {
        JSONVar tArr = daily["time"];
        JSONVar hiArr = daily["temperature_2m_max"];
        JSONVar loArr = daily["temperature_2m_min"];
        JSONVar pArr = daily["precipitation_probability_max"];
        JSONVar cArr = daily["weather_code"];
        JSONVar srArr = daily["sunrise"];
        JSONVar ssArr = daily["sunset"];

        int days = arrLen(tArr);
        if (days > MAX_FORECAST_DAYS) days = MAX_FORECAST_DAYS;
        for (int i = 0; i < days; ++i) {
            ForecastDay f;
            uint32_t dayEpoch = static_cast<uint32_t>(arrInt(tArr, i, 0));
            time_t tt = static_cast<time_t>(dayEpoch);
            struct tm *ti = localtime(&tt);
            if (ti) {
                f.dayNum = ti->tm_mday;
                f.monthNum = ti->tm_mon + 1;
                f.yearNum = ti->tm_year + 1900;
            }
            f.highTemp = arrNum(hiArr, i);
            f.lowTemp = arrNum(loArr, i);
            f.rainChance = arrInt(pArr, i, -1);
            int code = arrInt(cArr, i, -1);
            f.conditions = openMeteoConditionFromCode(code);
            f.icon = f.conditions;
            f.sunrise = static_cast<uint32_t>(arrInt(srArr, i, 0));
            f.sunset = static_cast<uint32_t>(arrInt(ssArr, i, 0));
            forecast.days[forecast.numDays++] = f;
        }
    }

    // --- Hourly forecast (next 24h mapped to existing WF shape) ---
    forecast.numHours = 0;
    forecast.hourlyKeyPresent = false;
    JSONVar hourly = doc["hourly"];
    if (JSON.typeof_(hourly) == "object") {
        JSONVar tArr = hourly["time"];
        JSONVar tempArr = hourly["temperature_2m"];
        JSONVar pArr = hourly["precipitation_probability"];
        JSONVar cArr = hourly["weather_code"];

        int hours = arrLen(tArr);
        if (hours > MAX_FORECAST_HOURS) hours = MAX_FORECAST_HOURS;
        for (int i = 0; i < hours; ++i) {
            ForecastHour h;
            h.time = static_cast<uint32_t>(arrInt(tArr, i, 0));
            h.temp = arrNum(tempArr, i);
            h.rainChance = arrInt(pArr, i, -1);
            int code = arrInt(cArr, i, -1);
            h.conditions = openMeteoConditionFromCode(code);
            h.icon = h.conditions;

            // Add "night" marker where possible so existing icon mapper picks correct style.
            bool isNight = false;
            time_t htt = static_cast<time_t>(h.time);
            struct tm *hti = localtime(&htt);
            if (hti) {
                for (int di = 0; di < forecast.numDays; ++di) {
                    const ForecastDay &d = forecast.days[di];
                    if (d.monthNum == (hti->tm_mon + 1) && d.dayNum == hti->tm_mday &&
                        d.sunrise > 0 && d.sunset > 0) {
                        isNight = !(h.time >= d.sunrise && h.time < d.sunset);
                        break;
                    }
                }
                if (forecast.numDays == 0) {
                    isNight = (hti->tm_hour < 6 || hti->tm_hour >= 18);
                }
            }
            if (isNight && h.icon.length() > 0 && h.icon.indexOf("night") < 0) {
                h.icon += " night";
            }

            forecast.hours[forecast.numHours++] = h;
        }
        forecast.hourlyKeyPresent = (forecast.numHours > 0);
    }

    // Map into Tempest live fields so existing "Live Weather" and scene code can reuse it.
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

    forecast.lastUpdate = millis();
    return (forecast.numDays > 0 || forecast.numHours > 0 || !isnan(currentCond.temp));
}

static void fetchOpenMeteoForecastData() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Open-Meteo] Forecast fetch skipped (WiFi offline)");
        return;
    }

    double lat = NAN;
    double lon = NAN;
    if (!resolveOpenMeteoCoordinates(lat, lon)) {
        Serial.println("[Open-Meteo] Missing coordinates (set timezone or NOAA lat/lon)");
        return;
    }

    String baseQuery = String("api.open-meteo.com/v1/forecast?latitude=") + String(lat, 4) +
                       "&longitude=" + String(lon, 4) +
                       "&current=temperature_2m,apparent_temperature,dew_point_2m,relative_humidity_2m,pressure_msl,wind_speed_10m,wind_gusts_10m,wind_direction_10m,weather_code" +
                       "&hourly=temperature_2m,precipitation_probability,weather_code" +
                       "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code,sunrise,sunset";

    String httpPrimary = "http://" + baseQuery + "&forecast_days=10&forecast_hours=24&timezone=auto&timeformat=unixtime";
    String httpFallback = "http://" + baseQuery + "&forecast_days=10&timezone=auto&timeformat=unixtime";
    String httpsPrimary = "https://" + baseQuery + "&forecast_days=10&forecast_hours=24&timezone=auto&timeformat=unixtime";
    String httpsFallback = "https://" + baseQuery + "&forecast_days=10&timezone=auto&timeformat=unixtime";

    auto requestPayload = [&](const String& url, bool useTls, String& payloadOut) -> bool {
        HTTPClient http;
        http.useHTTP10(true);
        http.setTimeout(15000);
        http.setReuse(false);

        bool began = false;
        if (useTls) {
            WiFiClientSecure client;
            client.setInsecure();
            began = http.begin(client, url);
        } else {
            WiFiClient client;
            began = http.begin(client, url);
        }

        if (!began) {
            Serial.println("[Open-Meteo] HTTP begin failed");
            return false;
        }

        http.addHeader("Accept-Encoding", "identity");
        http.addHeader("Connection", "close");

        int httpCode = http.GET();
        Serial.print("[Open-Meteo] Forecast status: ");
        Serial.println(httpCode);
        if (httpCode != HTTP_CODE_OK) {
            http.end();
            return false;
        }

        if (!readHttpBody(http, payloadOut, kOpenMeteoMaxBodyBytes, 15000UL)) {
            http.end();
            return false;
        }
        http.end();
        return payloadOut.length() > 0;
    };

    String payload;
    bool gotPayload = requestPayload(httpPrimary, false, payload);
    if (!gotPayload) {
        Serial.println("[Open-Meteo] HTTP primary failed, retrying HTTP fallback URL.");
        gotPayload = requestPayload(httpFallback, false, payload);
    }
    if (!gotPayload) {
        Serial.println("[Open-Meteo] HTTP failed, retrying HTTPS primary URL.");
        gotPayload = requestPayload(httpsPrimary, true, payload);
    }
    if (!gotPayload) {
        Serial.println("[Open-Meteo] HTTPS primary failed, retrying HTTPS fallback URL.");
        gotPayload = requestPayload(httpsFallback, true, payload);
    }
    if (!gotPayload) {
        Serial.println("[Open-Meteo] Forecast fetch failed");
        return;
    }

    if (!parseOpenMeteoForecastPayload(payload)) {
        Serial.println("[Open-Meteo] Forecast parse failed");
        return;
    }

    Serial.print("[Open-Meteo] Updated current/daily/hourly: ");
    Serial.print(forecast.numDays);
    Serial.print(" days, ");
    Serial.print(forecast.numHours);
    Serial.println(" hours");
}

// --------- WeatherFlow Forecast Fetch ----------
void fetchForecastData() {
    if (isDataSourceOpenMeteo()) {
        fetchOpenMeteoForecastData();
        requestScrollRebuild();
        return;
    }

    String stationId = wfStationId;
    String token = wfToken;
    stationId.trim();
    token.trim();

    if (stationId.isEmpty() || token.isEmpty()) {
        Serial.println("[Tempest] Missing WeatherFlow credentials");
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Tempest] Forecast fetch skipped (WiFi offline)");
        return;
    }

    // Use plain HTTP to save RAM and avoid TLS allocation errors
    const String url = "http://swd.weatherflow.com/swd/rest/better_forecast?station_id=" + stationId +
                       "&token=" + token;

    HTTPClient http;
    WiFiClient client;
    http.useHTTP10(true);
    http.setTimeout(15000);
    http.setReuse(false);

    if (!http.begin(client, url)) {
        Serial.println("[HTTP] begin() failed");
        return;
    }

    Serial.print("[HTTP] Forecast URL: ");
    Serial.println(url);
    int httpCode = http.GET();
    Serial.print("[HTTP] Forecast status: ");
    Serial.println(httpCode);
    if (httpCode != HTTP_CODE_OK) {
        Serial.print("[HTTP] Forecast error: ");
        Serial.println(httpCode);
        http.end();
        return;
    }

    String payload;
    if (!readHttpBody(http, payload, kWeatherFlowForecastMaxBodyBytes, 15000UL)) {
        http.end();
        return;
    }
    http.end();
    updateForecastFromJson(payload);
    requestScrollRebuild();
}

void resetForecastModelData() {
    forecast = ForecastData{};
    currentCond = CurrentConditions{};
    currentCond.humidity = -1;
    currentCond.uv = -1;
    currentCond.precipProb = -1;

    tempest = TempestData{};
    tempest.temperature = NAN;
    tempest.humidity = NAN;
    tempest.pressure = NAN;
    tempest.windAvg = NAN;
    tempest.windGust = NAN;
    tempest.windDir = NAN;
    tempest.windLull = NAN;
    tempest.windSampleInt = NAN;
    tempest.illuminance = NAN;
    tempest.uv = NAN;
    tempest.solar = NAN;
    tempest.rain = NAN;
    tempest.strikeDist = NAN;
    tempest.lightningLastEventDistanceKm = NAN;
    tempest.battery = NAN;
    tempest.obsWindAvg = NAN;
    tempest.obsWindDir = NAN;
    tempest.rapidWindAvg = NAN;
    tempest.rapidWindDir = NAN;
    tempest.lastObsTime = "";
    tempest.lightningSummaryEpoch = 0;
    tempest.lightningSummaryLastUpdate = 0;
    tempest.lightningLastEventEpoch = 0;
    tempest.lightningLastEventUpdate = 0;
    tempest.lightningLastEventEnergy = 0;

    newTempestData = false;
    newRapidWindData = false;
    s_lastLightningAlertEpoch = 0;
    s_lastLightningAlertMs = 0;
    updateWindInfoScroll(true);
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
    if (!strcmp(field, "strikedist"))  return formatLightningDistance(tempest.strikeDist);
    if (!strcmp(field, "strike_last")) return formatWindTimestamp(tempest.lightningLastEventEpoch);
    if (!strcmp(field, "strike_age"))  return lightningAgeShort(millis(), tempest.lightningLastEventUpdate);
    if (!strcmp(field, "obs_time"))    return tempest.lastObsTime;
    return "";
}

static String ageShort(unsigned long ageMs)
{
    unsigned long sec = ageMs / 1000UL;
    if (sec < 60UL)
        return String(sec) + "s";
    unsigned long min = sec / 60UL;
    if (min < 60UL)
        return String(min) + "m";
    unsigned long hrs = min / 60UL;
    if (hrs < 24UL)
        return String(hrs) + "h";
    unsigned long days = hrs / 24UL;
    return String(days) + "d";
}

static String ageLongAgo(unsigned long ageMs)
{
    unsigned long sec = ageMs / 1000UL;
    if (sec < 60UL)
        return String(sec) + " second" + ((sec == 1UL) ? String("") : String("s")) + " ago";

    unsigned long min = sec / 60UL;
    if (min < 60UL)
        return String(min) + " minute" + ((min == 1UL) ? String("") : String("s")) + " ago";

    unsigned long hrs = min / 60UL;
    if (hrs < 24UL)
        return String(hrs) + " hour" + ((hrs == 1UL) ? String("") : String("s")) + " ago";

    unsigned long days = hrs / 24UL;
    return String(days) + " day" + ((days == 1UL) ? String("") : String("s")) + " ago";
}

static String formatLightningDistance(double km)
{
    return fmtDistanceKm(km, 1);
}

static bool isLightningSummaryFresh(unsigned long nowMs)
{
    return tempest.lightningSummaryLastUpdate > 0 &&
           nowMs >= tempest.lightningSummaryLastUpdate &&
           (nowMs - tempest.lightningSummaryLastUpdate) <= kLightningStaleTimeoutMs;
}

static bool isLightningEventFresh(unsigned long nowMs)
{
    return tempest.lightningLastEventUpdate > 0 &&
           nowMs >= tempest.lightningLastEventUpdate &&
           (nowMs - tempest.lightningLastEventUpdate) <= kLightningStaleTimeoutMs;
}

static String lightningAgeShort(unsigned long nowMs, unsigned long updateMs)
{
    if (updateMs == 0 || nowMs < updateMs)
        return "--";
    return ageShort(nowMs - updateMs);
}

static void handleLightningAlertForEvent(uint32_t epoch, double distanceKm, uint32_t energy)
{
    (void)energy;
    const unsigned long nowMs = millis();
    const bool sameEvent = (epoch != 0 && epoch == s_lastLightningAlertEpoch);
    const bool withinCooldown = (s_lastLightningAlertMs > 0 && (nowMs - s_lastLightningAlertMs) < kLightningRetriggerCooldownMs);
    if (sameEvent || withinCooldown)
        return;

    const bool nearby = !isnan(distanceKm) && distanceKm <= kLightningNearbyThresholdKm;
    queueTemporaryAlertHeading(nearby ? "Lightning Nearby..." : "Lightning Detected",
                               kLightningAlertDisplayMs,
                               (epoch != 0) ? epoch : static_cast<uint32_t>(nowMs));
    s_lastLightningAlertEpoch = epoch;
    s_lastLightningAlertMs = nowMs;
}

static String compactTemp(double tempC)
{
    return isnan(tempC) ? String("--") : fmtTemp(tempC, 1);
}

static String compactHum(int hum)
{
    return (hum < 0) ? String("--") : (String(hum) + "%");
}

static String compactPress(double hpa)
{
    return isnan(hpa) ? String("--") : fmtPress(hpa, 1);
}

static String compactWind(double speed, const String &card)
{
    String v = isnan(speed) ? String("--") : fmtWind(speed, 1);
    if (card.length() == 0)
        return v;
    return v + " " + card;
}

static String deltaTempText(double aC, double bC)
{
    if (isnan(aC) || isnan(bC))
        return String("--");
    double delta = aC - bC;
    double disp = (units.temp == TempUnit::F) ? (delta * 9.0 / 5.0) : delta;
    char buf[24];
    snprintf(buf, sizeof(buf), "%+.1f%c", disp, (units.temp == TempUnit::F) ? 'F' : 'C');
    return String(buf);
}

static String compactCondition(const String &condition)
{
    String out = condition.length() ? condition : String("--");
    if (out.length() > 20)
        out = out.substring(0, 20);
    return out;
}

static String providerShortLabel()
{
    if (isDataSourceWeatherFlow())
        return "WF CLOUD";
    if (isDataSourceOpenMeteo())
        return "OPEN-MET";
    if (isDataSourceOwm())
        return "OWM";
    return "UNKNOWN";
}


// --------- InfoScreen Display Functions ----------
void showUdpScreen() {
    const unsigned long nowMs = millis();
    const unsigned long udpAgeMs = (tempest.obsLastUpdate > 0 && nowMs >= tempest.obsLastUpdate)
                                        ? (nowMs - tempest.obsLastUpdate)
                                        : 0UL;
    const String windDir = formatWindDirectionLabel(tempest.windDir);
    const String sourceAge = String("LOCAL ") + ((tempest.obsLastUpdate > 0) ? ageShort(udpAgeMs) : String("--"));
    String lines[INFOSCREEN_MAX_LINES];
    int lineCount = 0;
    auto addLine = [&](const String &line) {
        if (lineCount < INFOSCREEN_MAX_LINES)
            lines[lineCount++] = line;
    };

    addLine("Source: " + sourceAge);
    addLine("Obs At: " + formatWindTimestamp(tempest.obsEpoch));
    addLine("Temp:   " + compactTemp(tempest.temperature) + " Feels " + compactTemp(currentCond.feelsLike));
    addLine("Dew:    " + (isnan(currentCond.dewPoint) ? String("--") : fmtTemp(currentCond.dewPoint, 0)));
    addLine("Hum:    " + compactHum(isnan(tempest.humidity) ? -1 : static_cast<int>(lround(tempest.humidity))));
    addLine("Press:  " + compactPress(tempest.pressure));

    String windLine = "Wind:   " + compactWind(tempest.windAvg, (windDir == "--") ? String("") : windDir);
    windLine += " G" + (isnan(tempest.windGust) ? String("--") : fmtWind(tempest.windGust, 1));
    windLine += " L" + (isnan(tempest.windLull) ? String("--") : fmtWind(tempest.windLull, 1));
    addLine(windLine);

    addLine("Sample: " + formatSampleInterval(tempest.windSampleInt) + " Rep " + String(tempest.reportInt) + "s");
    addLine("UV:     " + (isnan(tempest.uv) ? String("--") : String(tempest.uv, 1)) +
            " Solar " + (isnan(tempest.solar) ? String("--") : String(tempest.solar, 0) + "W"));
    addLine("Lux:    " + (isnan(tempest.illuminance) ? String("--") : String(tempest.illuminance, 0) + "lx"));
    addLine("Rain:   " + (isnan(tempest.rain) ? String("--") : fmtPrecip(tempest.rain, 2)) +
            " Type " + String(tempest.precipType));
    addLine("Strike: " + String(tempest.strikeCount) + " Dist " +
            formatLightningDistance(tempest.strikeDist));
    addLine("Batt:   " + (isnan(tempest.battery) ? String("--") : String(tempest.battery, 2) + "V"));

    if (tempest.rapidEpoch > 0)
    {
        const unsigned long rapidAgeMs = (tempest.rapidLastUpdate > 0 && nowMs >= tempest.rapidLastUpdate)
                                             ? (nowMs - tempest.rapidLastUpdate)
                                             : 0UL;
        String rapidLine = "Rapid:  " + (isnan(tempest.rapidWindAvg) ? String("--") : fmtWind(tempest.rapidWindAvg, 1));
        const String rapidDir = formatWindDirectionLabel(tempest.rapidWindDir);
        if (rapidDir != "--")
            rapidLine += " " + rapidDir;
        rapidLine += " (" + ageShort(rapidAgeMs) + ")";
        addLine(rapidLine);
    }

    addLine("Cond:   " + compactCondition(currentCond.cond));
    if (isDataSourceWeatherFlow())
        addLine("Delta:  Cld " + deltaTempText(currentCond.temp, tempest.temperature));

    udpScreen.setTitle("Live Station");

    if (!udpScreen.isActive()) {
        udpScreen.setLines(lines, lineCount, true);
        udpScreen.show([](){ currentScreen = homeScreenForDataSource(); });
    } else {
        udpScreen.setLines(lines, lineCount, false);
    }
}

void showLightningScreen() {
    const unsigned long nowMs = millis();
    const bool summaryFresh = isLightningSummaryFresh(nowMs);
    const bool eventFresh = isLightningEventFresh(nowMs);
    const String sourceAge = String("LOCAL ") + lightningAgeShort(nowMs, tempest.lightningSummaryLastUpdate);

    String lines[INFOSCREEN_MAX_LINES];
    int lineCount = 0;
    auto addLine = [&](const String &line) {
        if (lineCount < INFOSCREEN_MAX_LINES)
            lines[lineCount++] = line;
    };

    addLine("Source: " + sourceAge);
    addLine("Strikes: " + (summaryFresh ? String(tempest.strikeCount) : String("--")));
    addLine("Near: " + (summaryFresh ? formatLightningDistance(tempest.strikeDist) : String("--")));
    addLine("Last: " + (eventFresh ? ageShort(nowMs - tempest.lightningLastEventUpdate) : String("--")));
    addLine("At: " + (eventFresh ? formatWindTimestamp(tempest.lightningLastEventEpoch) : String("--")));
    addLine("Evt Near: " + (eventFresh ? formatLightningDistance(tempest.lightningLastEventDistanceKm) : String("--")));
    addLine("Energy: " + (eventFresh ? String(tempest.lightningLastEventEnergy) : String("--")));

    if (!summaryFresh && !eventFresh)
        addLine("Status: Stale");
    else if (eventFresh && !isnan(tempest.lightningLastEventDistanceKm) && tempest.lightningLastEventDistanceKm <= kLightningNearbyThresholdKm)
        addLine("Status: Nearby");
    else if (summaryFresh && tempest.strikeCount > 0)
        addLine("Status: Active");
    else
        addLine("Status: Clear");

    lightningScreen.setTitle("Lightning");
    const bool reset = !lightningScreen.isActive();
    lightningScreen.setLines(lines, lineCount, reset);
    lightningScreen.show([](){ currentScreen = homeScreenForDataSource(); });
}

void showForecastScreen() {
    auto fallbackForecastYear = []() -> int {
        // Prefer known-valid weather epoch first.
        if (currentCond.time >= 1577836800UL) // 2020-01-01 UTC
        {
            time_t tt = static_cast<time_t>(currentCond.time);
            struct tm *ti = localtime(&tt);
            if (ti)
                return ti->tm_year + 1900;
        }
        // Then use local clock only if plausibly set.
        time_t nowTs = time(nullptr);
        if (nowTs >= 1577836800UL)
        {
            struct tm *tiNow = localtime(&nowTs);
            if (tiNow)
                return tiNow->tm_year + 1900;
        }
        // Final fallback: build year from compiler date (e.g. "Mar  2 2026").
        const char *buildDate = __DATE__;
        int y = atoi(buildDate + 7);
        return (y >= 2020) ? y : 2026;
    };

    int num = min(forecast.numDays, 10); // show up to 10 days
    Serial.print("Showing ");
    Serial.print(num);
    Serial.println(" forecast days");

    String lines[MAX_FORECAST_DAYS];
    if (num == 0) {
        lines[0] = "No forecast data!";
        forecastScreen.setLines(lines, 1);
        forecastScreen.show([](){ currentScreen = homeScreenForDataSource(); });
        return;
    }
    for (int i = 0; i < num; ++i) {
        const ForecastDay& f = forecast.days[i];
        String iconKey = f.conditions.length() ? f.conditions : f.icon;
        String tempUnit = (units.temp == TempUnit::F) ? "F" : "C";

        String hiText = "--";
        String loText = "--";
        if (!isnan(f.highTemp))
        {
            hiText = String(static_cast<int>(lround(dispTemp(f.highTemp))));
        }
        if (!isnan(f.lowTemp))
        {
            loText = String(static_cast<int>(lround(dispTemp(f.lowTemp))));
        }

        String hiLine = "[up] " + hiText + "\xB0 " + tempUnit;
        String loLine = "[down] " + loText + "\xB0 " + tempUnit;
        if (iconKey.length())
        {
            hiLine += " [icon=" + iconKey + "]";
        }

        String detailLine = f.conditions.length() ? f.conditions : String("No details");
        if (f.rainChance >= 0)
        {
            detailLine += " " + String(f.rainChance) + "%";
        }

        int year = f.yearNum;
        if (year <= 0)
        {
            year = fallbackForecastYear();
        }
        char dateBuf[24];
        snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d/%04d", f.monthNum, f.dayNum, year);
        lines[i] = String(dateBuf) + "\n" + hiLine + "\n" + loLine + "\n" + detailLine;
    }

    bool reset = !forecastScreen.isActive();
    forecastScreen.setLines(lines, num, reset);
    forecastScreen.show([](){ currentScreen = homeScreenForDataSource(); });
}

void showHourlyForecastScreen() {
    // How many hours to show on the screen (compile-time constant)
    static constexpr uint8_t HOURLY_DISPLAY_COUNT = 24;
    const bool reset = !hourlyScreen.isActive();

    // Case 1: No hourly data parsed at all
    if (forecast.numHours <= 0) {
        String lines[1];
        lines[0] = "No hour data.";
        hourlyScreen.setLines(lines, 1, reset);
        hourlyScreen.show([](){ currentScreen = homeScreenForDataSource(); });
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
        hourlyScreen.setLines(lines, 1, reset);
        hourlyScreen.show([](){ currentScreen = homeScreenForDataSource(); });
        return;
    }

    // Build lines for the next up-to-HOURLY_DISPLAY_COUNT hours
    String lines[HOURLY_DISPLAY_COUNT];
    for (int i = 0; i < count; ++i) {
        const ForecastHour& h = forecast.hours[start + i];

        time_t tt = (time_t)h.time;
        struct tm* ti = localtime(&tt);

        // Build a wrapped, two-line friendly string per hour
        String line;
        int hh = ti ? ti->tm_hour : 0;
        int mm = ti ? ti->tm_min  : 0;

        char timeBuf[20];
        if (units.clock24h)
        {
            snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", hh, mm);
        }
        else
        {
            int hour12 = hh % 12;
            if (hour12 == 0)
                hour12 = 12;
            const char *suffix = (hh >= 12) ? "PM" : "AM";
            snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d %s", hour12, mm, suffix);
        }
        String timeLabel(timeBuf);

        // Keep hourly entries to a predictable 3-line block.
        // Icon hint is consumed by the renderer and not shown in text.
        String iconKey = h.conditions.length() ? h.conditions : h.icon;
        auto isNightAt = [&](time_t epoch, const tm* tmi) -> bool {
            if (!tmi) return false;
            const int month = tmi->tm_mon + 1;
            const int day = tmi->tm_mday;
            for (int di = 0; di < forecast.numDays; ++di) {
                const ForecastDay& d = forecast.days[di];
                if (d.monthNum == month && d.dayNum == day && d.sunrise > 0 && d.sunset > 0) {
                    return !(epoch >= (time_t)d.sunrise && epoch < (time_t)d.sunset);
                }
            }
            const int hhLocal = tmi->tm_hour;
            return (hhLocal < 6 || hhLocal >= 18);
        };
        const bool night = isNightAt(tt, ti);
        if (night && iconKey.length() && iconKey.indexOf("night") < 0) {
            iconKey += " night";
        }
        String labelLine = timeLabel;
        if (iconKey.length())
        {
            labelLine += " [icon=" + iconKey + "]";
        }

        String dataLine;
        dataLine += (isnan(h.temp) ? String("--") : fmtTemp(h.temp, 0));
        dataLine += " ";
        String condLine = h.conditions.length() ? h.conditions : String("");
        lines[i] = labelLine + "\n" + dataLine + "\n" + condLine;
    }

    hourlyScreen.setLines(lines, count, reset);
    hourlyScreen.show([](){ currentScreen = homeScreenForDataSource(); });
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
    const unsigned long nowMs = millis();
    const unsigned long cloudAgeMs = (forecast.lastUpdate > 0 && nowMs >= forecast.lastUpdate)
                                          ? (nowMs - forecast.lastUpdate)
                                          : 0UL;
    const String sourceAge = providerShortLabel() + " " + ((forecast.lastUpdate > 0) ? ageLongAgo(cloudAgeMs) : String("--"));

    String lines[8];
    int lineCount = 0;
    lines[lineCount++] = "Source: " + sourceAge;
    lines[lineCount++] = "Temp:   " + compactTemp(currentCond.temp) + " Feels " + compactTemp(currentCond.feelsLike);
    lines[lineCount++] = "Wind:   " + compactWind(currentCond.windAvg, currentCond.windCardinal) + " G" + (isnan(currentCond.windGust) ? String("--") : fmtWind(currentCond.windGust, 1));
    lines[lineCount++] = "Press:  " + compactPress(currentCond.pressure);
    lines[lineCount++] = "Hum:    " + compactHum(currentCond.humidity);
    lines[lineCount++] = "Cond:   " + compactCondition(currentCond.cond);
    if (isDataSourceWeatherFlow())
    {
        lines[lineCount++] = "Delta:  Loc " + deltaTempText(tempest.temperature, currentCond.temp);
    }
    currentCondScreen.setTitle("Current WX");
    bool reset = !currentCondScreen.isActive();
    currentCondScreen.setLines(lines, lineCount, reset);
    currentCondScreen.show([](){ currentScreen = homeScreenForDataSource(); });

}
