#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <esp_ota_ops.h>

#include "menu.h"
#include "display.h"
#include "settings.h"
#include "system.h"
#include "sensors.h"
#include "cloud_manager.h"
#include "device_identity.h"

namespace
{
void rebuildSystemInfoModal()
{
    const uint32_t flashChipSize = ESP.getFlashChipSize();
    const uint32_t sketchSize = ESP.getSketchSize();
    (void)flashChipSize;

    const esp_partition_t *running = esp_ota_get_running_partition();
    const uint32_t appPartition = running ? running->size : 0;

    const uint32_t heapFree = ESP.getFreeHeap();
    const uint32_t heapTotal = 327680;
    const uint32_t heapUsed = heapTotal - heapFree;
    const float heapPct = 100.0f * heapUsed / heapTotal;
    const float flashPct = (appPartition > 0) ? (100.0f * sketchSize / appPartition) : 0.0f;

    const uint32_t spiffsTotal = SPIFFS.totalBytes();
    const uint32_t spiffsUsed = SPIFFS.usedBytes();
    const float spiffsPct = (spiffsTotal > 0) ? (100.0f * spiffsUsed / spiffsTotal) : 0.0f;

    const wxv::cloud::CloudRuntimeState &cloud = wxv::cloud::cloudState();
    const String cloudState = !cloudEnabled ? "disabled"
                              : cloud.registered ? "registered"
                              : "pending";
    const String relayState = cloud.relayConnected ? "connected" : "disconnected";
    const String deviceUuid = wxv::cloud::deviceIdentity().uuid;

    String lines[] = {
        "FW: 1.0.0",
        "IP: " + WiFi.localIP().toString(),
        "mDNS: " + deviceHostname + ".local",
        "MAC: " + WiFi.macAddress(),
        "RSSI: " + String(WiFi.RSSI()) + " dBm",
        "Device UUID: " + deviceUuid,
        "Cloud: " + cloudState,
        "Relay: " + relayState,
        "RAM:   " + String(heapPct, 1) + "% (" + String(heapUsed) + "/" + String(heapTotal) + " B)",
        "Flash: " + String(flashPct, 1) + "% (" + String(sketchSize) + "/" + String(appPartition) + " B)",
        "SPIFFS: " + String(spiffsPct, 1) + "% (" + String(spiffsUsed / 1024) + "/" + String(spiffsTotal / 1024) + " KB)"};
    InfoFieldType types[] = {
        InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel,
        InfoLabel, InfoLabel, InfoLabel,
        InfoLabel, InfoLabel, InfoLabel};

    sysInfoModal.setLines(lines, types, 11);
    sysInfoModal.setKeepOpenOnSelect(true);
    sysInfoModal.setCallback([](bool, int) {});
}
} // namespace

void showSystemModal()
{
    if (currentMenuLevel != MENU_NONE && currentMenuLevel != MENU_MAIN)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_SYSTEM;
    menuActive = true;

    String labels[] = {
        "Sound Volume (0-100)",
        "Sound Profile",
        "Date & Time",
        "Units",
        "Device Location",
        "System Info",
        "WiFi Signal Test",
        "Preview Screens",
        "Learn Remote",
        "Clear Learned Remote",
        "Reset Settings",
        "Factory Reset (Erase Wi-Fi + Logs)",
        "Reboot"};

    InfoFieldType types[] = {
        InfoNumber, InfoChooser, InfoButton, InfoButton, InfoButton,
        InfoButton, InfoButton, InfoButton, InfoButton, InfoButton, InfoButton, InfoButton, InfoButton};
    int *numberRefs[] = {&buzzerVolume};
    int *chooserRefs[] = {&buzzerToneSet};
    static const char *toneOpts[] = {"Bright", "Soft", "Click", "Chime", "Pulse", "Warm", "Melody"};
    const char *const *chooserOpts[] = {toneOpts};
    int chooserCounts[] = {7};

    systemModal.setLines(labels, types, 13);
    systemModal.setValueRefs(numberRefs, 1, chooserRefs, 1, chooserOpts, chooserCounts, nullptr, 0, nullptr);
    systemModal.setShowNumberArrows(true);
    systemModal.setShowChooserArrows(true);
    systemModal.setShowForwardArrow(true);
    systemModal.setCallback([](bool accepted, int btnIdx) {
        int action = -1;
        if (btnIdx >= 0)
        {
            action = btnIdx;
        }
        else if (accepted)
        {
            action = systemModal.getSelIndex();
        }

        // Persist volume/profile regardless of which action chosen
        buzzerVolume = constrain(buzzerVolume, 0, 100);
        buzzerToneSet = constrain(buzzerToneSet, 0, 6);
        saveDeviceSettings();

        if (action >= 0)
        {
            switch (action)
            {
            case 0: // volume row -> no navigation
                break;
            case 1: // tone profile row
                break;
            case 2:
                systemModal.hide();
                showDateTimeModal();
                return;
            case 3:
                systemModal.hide();
                pendingModalFn = showUnitSettingsModal;
                pendingModalTime = millis();
                return;
            case 4:
                systemModal.hide();
                showDeviceLocationModal();
                return;
            case 5:
                systemModal.hide();
                showSystemInfoScreen();
                return;
            case 6:
                systemModal.hide();
                showWiFiSignalTest();
                return;
            case 7:
                systemModal.hide();
                showScenePreviewModal();
                return;
            case 8:
                systemModal.hide();
                startUniversalRemoteLearning();
                return;
            case 9:
                systemModal.hide();
                clearUniversalRemoteLearning();
                break;
            case 10:
                systemModal.hide();
                quickRestore();
                break;
            case 11:
                systemModal.hide();
                factoryReset();
                break;
            case 12:
                systemModal.hide();
                ESP.restart();
                return;
            default:
                break;
            }
        }
        // Always return to main menu after hiding systemModal
        systemModal.hide();
        menuStack.clear();
        currentMenuLevel = MENU_MAIN;
        menuActive = true;
        showMainMenuModal();
    });
    systemModal.show();
}

void showWiFiSignalTest()
{

    if (currentMenuLevel != MENU_NONE)
    {
        pushMenu(currentMenuLevel); // Push current menu to stack
    }
    currentMenuLevel = MENU_SYSWIFI; // You can define this enum, or use MENU_SYSTEM if preferred
    menuActive = true;
    scanWiFiNetworks();
    const int maxLines = 32;
    String lines[maxLines];
    InfoFieldType types[maxLines];
    int lineIndex = 0;

    lines[lineIndex] = "RSSI: " + String(WiFi.RSSI()) + " dBm";
    types[lineIndex++] = InfoLabel;
    lines[lineIndex] = "MAC: " + WiFi.macAddress();
    types[lineIndex++] = InfoLabel;
    lines[lineIndex] = "SSID: " + wifiSSID;
    types[lineIndex++] = InfoLabel;
    lines[lineIndex] = "IP: " + WiFi.localIP().toString();
    types[lineIndex++] = InfoLabel;
    lines[lineIndex] = "Subnet: " + WiFi.subnetMask().toString();
    types[lineIndex++] = InfoLabel;
    lines[lineIndex] = "Gateway: " + WiFi.gatewayIP().toString();
    types[lineIndex++] = InfoLabel;
    lines[lineIndex] = "DNS1: " + WiFi.dnsIP(0).toString();
    types[lineIndex++] = InfoLabel;
    lines[lineIndex] = "Channel: " + String(WiFi.channel());
    types[lineIndex++] = InfoLabel;

    for (int i = 0; i < wifiScanCount - 1 && lineIndex < maxLines; i++)
    {
        lines[lineIndex] = "Net[" + String(i + 1) + "]: " + scannedSSIDs[i];
        types[lineIndex++] = InfoLabel;
    }
    wifiInfoModal.setLines(lines, types, lineIndex);
    wifiInfoModal.show();
}

void showSystemInfoScreen()
{

    if (currentMenuLevel != MENU_NONE && currentMenuLevel != MENU_MAIN)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_SYSINFO; // You can define this enum, or use MENU_SYSTEM if preferred
    menuActive = true;

    rebuildSystemInfoModal();
    sysInfoModal.show();
}
