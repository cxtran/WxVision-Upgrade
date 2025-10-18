
#include <WiFi.h>
#include "wifisettings.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "display.h"
#include "settings.h"
#include "utils.h"
#include "menu.h"
#include <Preferences.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <Update.h>

#include <WiFiUdp.h>
// =======================
// WiFi SCAN/SELECT logic
// =======================

extern bool wifiSelecting;
extern int wifiScanCount;
int wifiScanIndex = 0;
String wifiScanSSIDs[20];
String wifiScanEncr[20];
String wifiScanRSSI[20];
extern int menuScroll;
extern bool menuActive;
extern int wifiSelectIndex;

extern String wifiSSID;
extern String wifiPass;
extern String owmCity;
extern String owmApiKey;
extern String wfToken;
extern String wfStationId;
extern int humOffset;


void connectToWiFi()
{
    Serial.printf("SSID: %s\n", wifiSSID.c_str());

    // --- Block empty or fake SSIDs ---
    if (wifiSSID.isEmpty() || wifiSSID == "(No networks)")
    {
        wifiSelecting = true;
        currentMenuLevel = MENU_WIFI_SELECT;
        menuActive = true;
        menuScroll = 0;
        wifiSelectIndex = 0;
        drawMenu();  // Will show drawWiFiMenu via drawMenu()
        return;
    }

    const bool allowDisplay = (dma_display != nullptr) && !isSplashActive();

    if (!dma_display)
    {
        Serial.println("dma_display not initialized. Skipping display output.");
    }
    else if (allowDisplay)
    {
        dma_display->clearScreen();
        dma_display->setCursor(0, 0);
        dma_display->setTextColor(myBLUE);
        dma_display->print("Connecting");
        dma_display->setCursor(0, 8);
        dma_display->setTextColor(myBLUE);
        dma_display->print("to WiFi...");
    }

    Serial.printf("[WiFi] Connecting to SSID: %s\n", wifiSSID.c_str());

    WiFi.mode(WIFI_STA);
    delay(200);
    WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
    int attempts = 40;
    while (WiFi.status() != WL_CONNECTED && attempts-- > 0)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("[WiFi] Connected!");
        Serial.print("[WiFi] IP Address: ");
        Serial.println(WiFi.localIP());
        if (allowDisplay)
        {
            String ssidStr = WiFi.SSID();
            String ipStr = WiFi.localIP().toString();

            dma_display->clearScreen();
            dma_display->setCursor(0, 0);
            dma_display->setTextColor(myBLUE);
            dma_display->print("WiFi:");
            dma_display->setCursor(0, 8);
            dma_display->setTextColor(myWHITE);
            dma_display->printf(" %s", ssidStr.c_str());
            dma_display->setCursor(0, 16);
            dma_display->setTextColor(myBLUE);
            dma_display->print("IP: ");
            dma_display->setCursor(0, 24);
            dma_display->setTextColor(myWHITE);
            dma_display->printf(" %s", ipStr.c_str());
            delay(5000);
            dma_display->clearScreen();
        }
        // Back to main menu on success
        menuActive = false;
        wifiSelecting = false;
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
        reset_Time_and_Date_Display = true; // Force refresh
    }
    else
    {
        Serial.println("[WiFi] Connection FAILED!");
        if (allowDisplay)
        {
            dma_display->clearScreen();
            dma_display->setCursor(0, 0);
            dma_display->setTextColor(myRED);
            dma_display->print("WiFi Failed!");
            delay(2000);
        }
        // Clear SSID so user can try again
        wifiSSID = "";
        // Show WiFi menu for retry (handled by onWiFiConnectFailed)
        onWiFiConnectFailed(); // Should update scannedSSIDs and show WiFi Select menu
        return;
    }
}


