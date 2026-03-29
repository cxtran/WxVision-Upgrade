#ifndef CONFIG_H
#define CONFIG_H

//==========================
// General Project Settings
//==========================
#define PROJECT_NAME "WxVision"
#define DEBUG true    // Set false to disable debug prints

//==========================
// Time Settings
//==========================
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC (-3600 * 8)    // UTC-8 (change as needed)
#define DAYLIGHT_OFFSET_SEC 3600     // 1 hour DST (set to 0 if not used)

//==========================
// WiFi / AP Settings
//==========================
#define WIFI_AP_NAME "WxVision-Setup"   // SoftAP SSID when no WiFi is configured
#define WIFI_AP_PASS "WxVision123"      // SoftAP password (min 8 chars)
#define WIFI_AP_CHANNEL 6               // SoftAP channel to broadcast on
#define WIFI_AP_MAX_CLIENTS 4           // Limit concurrent AP clients
#define WIFI_AP_IP 10,10,10,1           // SoftAP IP address
#define WIFI_AP_GATEWAY 10,10,10,1      // SoftAP gateway
#define WIFI_AP_SUBNET 255,255,255,0    // SoftAP subnet mask
#define WIFI_RETRY_TIMEOUT 20000        // Timeout for WiFi retry (ms)

//==========================
// Networking Helpers
//==========================
#define MDNS_BASE_HOSTNAME "wxvision"   // Base hostname (suffix appended automatically)

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

//==========================
// MQTT / Home Assistant
//==========================
#define MQTT_ENABLE false
#define MQTT_HOST ""
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASS ""
#define MQTT_DEVICE_ID "wxvision01"
#define MQTT_PUB_INTERVAL_MS 15000UL
#define MQTT_RETRY_INTERVAL_MS 5000UL
#define MQTT_BUFFER_SIZE 768
#define MQTT_KEEPALIVE_SEC 15

#endif
