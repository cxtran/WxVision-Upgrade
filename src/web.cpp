#include <ESPAsyncWebServer.h>
#include "web.h"
#include "settings.h"
#include <SPIFFS.h>
#include <Update.h>
#include <WiFi.h>
#include <ArduinoJson.h>

// Declare globals somewhere (or extern if in other file)
extern int units, dayFormat, theme, brightness, scrollSpeed;
extern int manualScreen;
extern String customMsg; // or your default value
extern String owmCity; // Default city for OpenWeatherMap
extern String owmApiKey ; // Your OpenWeatherMap API key
extern String wfToken ; // Your WeatherFlow token
extern String wfStationId ; // Your WeatherFlow station ID
extern void saveAllSettings();
extern String str_Weather_Conditions, str_Temp, str_Humd;
extern bool useImperial;
extern char chr_t_hour[3], chr_t_minute[3], chr_t_second[3];
extern int humOffset; // Humidity offset for calibration



// Declare your global variables as extern
extern int units, dayFormat, forecastSrc, autoRotate, manualScreen;
extern int theme, brightness, scrollSpeed;
extern String customMsg;
extern String wifiSSID, wifiPass;
extern String owmCity, owmApiKey, wfToken, wfStationId;
extern int tempOffset, humOffset, lightGain;
extern void saveAllSettings();
extern void loadSettings();
extern String str_Weather_Conditions, str_Temp, str_Humd;
extern bool useImperial;
extern char chr_t_hour[3], chr_t_minute[3], chr_t_second[3];

AsyncWebServer server(80);

void setupWebServer()
{
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed!");
        return;
    }

    // Serve static files (config.html, favicon, etc)
    server.serveStatic("/", SPIFFS, "/").setDefaultFile("config.html");

    // GET all settings as JSON for config.html
    server.on("/settings.json", HTTP_GET, [](AsyncWebServerRequest *req) {
        StaticJsonDocument<1024> doc;
        doc["wifiSSID"]     = wifiSSID;
        doc["wifiPass"]     = wifiPass;
        doc["units"]        = units;
        doc["dayFormat"]    = dayFormat;
        doc["forecastSrc"]  = forecastSrc;
        doc["autoRotate"]   = autoRotate;
        doc["manualScreen"] = manualScreen;
        doc["theme"]        = theme;
        doc["brightness"]   = brightness;
        doc["scrollSpeed"]  = scrollSpeed;
        doc["customMsg"]    = customMsg;
        doc["owmCity"]      = owmCity;
        doc["owmApiKey"]    = owmApiKey;
        doc["wfToken"]      = wfToken;
        doc["wfStationId"]  = wfStationId;
        doc["tempOffset"]   = tempOffset;
        doc["humOffset"]    = humOffset;
        doc["lightGain"]    = lightGain;

        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    // POST to update all settings (from config.html)
    server.on("/settings", HTTP_POST, [](AsyncWebServerRequest *req) {}, NULL,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
            StaticJsonDocument<1024> doc;
            DeserializationError err = deserializeJson(doc, (const char*)data, len);
            if (err) {
                req->send(400, "text/plain", "Invalid JSON");
                return;
            }
            wifiSSID      = doc["wifiSSID"]     | "";
            wifiPass      = doc["wifiPass"]     | "";
            units         = doc["units"]        | 0;
            dayFormat     = doc["dayFormat"]    | 0;
            forecastSrc   = doc["forecastSrc"]  | 0;
            autoRotate    = doc["autoRotate"]   | 1;
            manualScreen  = doc["manualScreen"] | 0;
            theme         = doc["theme"]        | 0;
            brightness    = doc["brightness"]   | 50;
            scrollSpeed   = doc["scrollSpeed"]  | 2;
            customMsg     = doc["customMsg"]    | "";
            owmCity       = doc["owmCity"]      | "";
            owmApiKey     = doc["owmApiKey"]    | "";
            wfToken       = doc["wfToken"]      | "";
            wfStationId   = doc["wfStationId"]  | "";
            tempOffset    = doc["tempOffset"]   | 0;
            humOffset     = doc["humOffset"]    | 0;
            lightGain     = doc["lightGain"]    | 100;

            saveAllSettings();
            req->send(200, "application/json", "{\"ok\":true}");
            delay(1200); // Optionally restart WiFi if SSID/pass changed
            //ESP.restart(); // Uncomment if you want auto-reboot after settings
        }
    );

    // STATUS PAGE
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        String status = "<html><body><h2>Status</h2>";
        status += "<p>WiFi: " + String(WiFi.SSID()) + "</p>";
        status += "<p>Weather: " + str_Weather_Conditions + " " + str_Temp + (useImperial ? "°F" : "°C") + "</p>";
        status += "<p>Humidity: " + str_Humd + "%</p>";
        status += "<p>Time: " + String(chr_t_hour) + ":" + String(chr_t_minute) + ":" + String(chr_t_second) + "</p>";
        status += "<p><a href='/ota'>Start OTA</a> | <a href='/reboot'>Reboot</a> | <a href='/'>Settings</a></p></body></html>";
        req->send(200, "text/html", status);
    });

    // REBOOT
    server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/plain", "Rebooting...");
        delay(1000);
        ESP.restart();
    });

    // OTA PAGE (HTML upload)
    server.on("/ota", HTTP_GET, [](AsyncWebServerRequest *req) {
        String html = "<form method='POST' action='/update' enctype='multipart/form-data'>";
        html += "<input type='file' name='firmware'><input type='submit' value='Upload'></form>";
        req->send(200, "text/html", html);
    });

    // OTA POST handler
    server.on("/update", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            bool ok = !Update.hasError();
            req->send(200, "text/html", ok ?
                "<h2>Update Successful!</h2><a href='/'>Return to Settings</a><script>setTimeout(()=>location.href='/',2000);</script>"
                : "<h2>Update FAILED!</h2><a href='/ota'>Try Again</a>");
            delay(1000);
            if (ok) ESP.restart();
        },
        [](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (!index) {
                Serial.printf("OTA Update Start: %s\n", filename.c_str());
                Update.begin(UPDATE_SIZE_UNKNOWN);
            }
            if (Update.write(data, len) != len) {
                Serial.println("OTA Write Fail!");
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.println("OTA Update Success.");
                } else {
                    Serial.println("OTA Update Error!");
                }
            }
        }
    );


    // SYSTEM ACTIONS (quick restore, factory reset)
    server.on("/quickrestore", HTTP_GET, [](AsyncWebServerRequest *req) {
        // implement quick restore logic
        req->send(200, "text/plain", "Quick restore done (not implemented)");
    });
    server.on("/factoryreset", HTTP_GET, [](AsyncWebServerRequest *req) {
        // implement factory reset logic
        req->send(200, "text/plain", "Factory reset done (not implemented)");
    });

    // 404
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not found");
    });

    server.begin();
}
