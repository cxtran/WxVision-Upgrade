
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

/*
void scanAndSelectWiFi()
{
    Serial.println("Scanning for WiFi networks...");
    WiFi.mode(WIFI_STA); // <-- ADD THIS
    delay(200);          // <-- (optional, helps after disconnect)
    wifiSelecting = true;
    wifiScanCount = WiFi.scanNetworks();
    Serial.printf("Scan complete: %d network(s) found\n", wifiScanCount);

    if (wifiScanCount == 0)
    {
        if (dma_display)
        {
            dma_display->clearScreen();
            dma_display->setCursor(0, 0);
            dma_display->setTextColor(myRED);
            dma_display->print("No WiFi Found!");
            dma_display->setCursor(0, 10);
            dma_display->setTextColor(myWHITE);
            dma_display->print("Press OK to rescan");
        }
        // Wait for user to retry, DO NOT restart
        wifiScanIndex = 0;
        return;
    }
    for (int i = 0; i < wifiScanCount && i < 20; i++)
    {
        wifiScanSSIDs[i] = WiFi.SSID(i);
        wifiScanRSSI[i] = String(WiFi.RSSI(i));
        wifiScanEncr[i] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Open" : "Secured";
        Serial.printf("SSID[%d]: %s, RSSI: %s, ENC: %s\n",
                      i, wifiScanSSIDs[i].c_str(), wifiScanRSSI[i].c_str(), wifiScanEncr[i].c_str());
    }
    wifiScanIndex = 0;

    if (dma_display)
    {
        dma_display->clearScreen();
        dma_display->setCursor(0, 0);
        dma_display->setTextColor(myCYAN);
        dma_display->print("Select WiFi:");
        dma_display->setCursor(0, 10);
        dma_display->setTextColor(myYELLOW);
        dma_display->print(wifiScanSSIDs[wifiScanIndex]);
        dma_display->setCursor(0, 18);
        dma_display->setTextColor(myWHITE);
        dma_display->printf("Sig:%s  %s", wifiScanRSSI[wifiScanIndex].c_str(), wifiScanEncr[wifiScanIndex].c_str());
        dma_display->setCursor(0, 26);
        dma_display->setTextColor(myWHITE);
        dma_display->print("CH+/CH-:Move OK:Select");
    }
}
void selectWiFiNetwork(int delta)
{
    wifiScanIndex += delta;
    if (wifiScanIndex < 0)
        wifiScanIndex = 0;
    if (wifiScanIndex >= wifiScanCount)
        wifiScanIndex = wifiScanCount - 1;

    if (dma_display)
    {
        dma_display->clearScreen();
        dma_display->setCursor(0, 0);
        dma_display->setTextColor(myCYAN);
        dma_display->print("Select WiFi:");
        dma_display->setCursor(0, 10);
        dma_display->setTextColor(myYELLOW);
        dma_display->print(wifiScanSSIDs[wifiScanIndex]);
        dma_display->setCursor(0, 18);
        dma_display->setTextColor(myWHITE);
        dma_display->printf("Sig:%s  %s", wifiScanRSSI[wifiScanIndex].c_str(), wifiScanEncr[wifiScanIndex].c_str());
        dma_display->setCursor(0, 26);
        dma_display->setTextColor(myWHITE);
        dma_display->print("CH+/CH-:Move OK:Select");
    }
}
void confirmWiFiSelection()
{
    wifiSSID = wifiScanSSIDs[wifiScanIndex];
    wifiPass = ""; // Always ask for password (user sets in Device Settings)
    saveDeviceSettings();
    wifiSelecting = false;

    if (dma_display)
    {
        dma_display->clearScreen();
        dma_display->setCursor(0, 0);
        dma_display->setTextColor(myGREEN);
        dma_display->print("SSID Saved!");
        dma_display->setCursor(0, 8);
        dma_display->setTextColor(myWHITE);
        dma_display->print(wifiSSID);
        dma_display->setCursor(0, 18);
        dma_display->setTextColor(myYELLOW);
        dma_display->print("Edit pass in menu");
    }
    delay(1200);
    connectToWiFi();
}
void cancelWiFiSelection()
{
    wifiSelecting = false;
    wifiSSID = "";
    wifiPass = "";
    saveDeviceSettings();
    if (dma_display)
    {
        dma_display->clearScreen();
        dma_display->setCursor(0, 0);
        dma_display->setTextColor(myRED);
        dma_display->print("WiFi Cancelled");
    }
    delay(1000);
    ESP.restart();
}
*/

// =========== WiFi Connection ===========


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

    // Show connecting status
    if (!dma_display)
    {
        Serial.println("⚠️  dma_display not initialized. Skipping display output.");
    }
    else
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
        if (dma_display)
        {
            dma_display->clearScreen();
            dma_display->setCursor(0, 0);
            dma_display->setTextColor(myGREEN);
            dma_display->print("WiFi OK");
            dma_display->setCursor(0, 8);
            dma_display->setTextColor(myBLUE);
            dma_display->print(WiFi.SSID());
            dma_display->setCursor(0, 16);
            dma_display->setTextColor(myWHITE);
            dma_display->print(WiFi.localIP().toString());
            delay(3000);
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
        if (dma_display)
        {
            dma_display->clearScreen();
            dma_display->setCursor(0, 0);
            dma_display->setTextColor(myRED);
            dma_display->print("WiFi Failed!");
        }
        delay(2000);
        // Clear SSID so user can try again
        wifiSSID = "";
        // Show WiFi menu for retry (handled by onWiFiConnectFailed)
        onWiFiConnectFailed(); // Should update scannedSSIDs and show WiFi Select menu
        return;
    }
}
