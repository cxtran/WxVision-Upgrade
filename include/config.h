#ifndef CONFIG_H
#define CONFIG_H

//==========================
// General Project Settings
//==========================
#define PROJECT_NAME "VisionWX"
#define DEBUG true    // Set false to disable debug prints

//==========================
// Time Settings
//==========================
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC (-3600 * 8)    // UTC-8 (change as needed)
#define DAYLIGHT_OFFSET_SEC 3600     // 1 hour DST (set to 0 if not used)

//==========================
// WiFiManager Settings
//==========================
#define WIFI_AP_NAME "VisionWX-Setup"   // Name of the setup portal SSID
#define WIFI_RETRY_TIMEOUT 20000        // Timeout for WiFi retry (ms)

//==========================
// Weather Settings
//==========================
#define OPENWEATHER_API_KEY "your_api_key_here"
#define CITY_NAME "Garden Grove"
#define COUNTRY_CODE "US"
#define USE_IMPERIAL true     // true for °F, false for °C

//==========================
// Display Brightness
//==========================
#define DEFAULT_BRIGHTNESS 3  // 0–255

//==========================
// Local UDP for WeatherFlow
//==========================
#define WEATHERFLOW_UDP_PORT 50222

#endif
