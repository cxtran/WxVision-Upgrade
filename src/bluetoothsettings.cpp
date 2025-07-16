
#include <BLEDevice.h>
#include <BLEScan.h>
#include "display.h"
#include "bluetoothsettings.h"

std::vector<BleDeviceInfo> foundBleDevices;
int bleScanCount = 0;
int bleSelectIndex= 0  ;
int bleMenuScroll = 0;
const int bleVisibleLines = 0;
String  scannedBLENames[16];

namespace {
    class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
        void onResult(BLEAdvertisedDevice advertisedDevice) override {
            String name = advertisedDevice.getName().c_str();
            if (name.length() == 0) name = "(No name)";
            String address = advertisedDevice.getAddress().toString().c_str();

            // Avoid duplicates
            for (auto &d : foundBleDevices) {
                if (d.address == address) return;
            }
            foundBleDevices.push_back({name, address});
            bleScanCount = foundBleDevices.size();
        }
    };
}


void drawBLEMenu() {
    dma_display->fillScreen(dma_display->color565(0, 0, 0));
    dma_display->setFont(&Font5x7Uts);

    // Label
    dma_display->setTextColor(dma_display->color565(0,255,255));
    dma_display->setCursor(0, 0);
    dma_display->print("Select BLE:");

    // Show 3 at a time, starting from bleMenuScroll
    const int labelHeight = 7;
    const int listStartY = labelHeight + 1;
    const int lineHeight = 8;

    for (int i = 0; i < 3; ++i) {
        int idx = bleMenuScroll + i;
        if (idx >= bleScanCount) break;
        dma_display->setCursor(0, listStartY + lineHeight * i);
        if (idx == bleSelectIndex)
            dma_display->setTextColor(dma_display->color565(255,255,0));
        else
            dma_display->setTextColor(dma_display->color565(255,255,255));
        // Show name, or address if name is empty
        String showText = foundBleDevices[idx].name.length() ? foundBleDevices[idx].name : foundBleDevices[idx].address;
        dma_display->print(showText);
    }
}

void scanBleDevices() {
    // Use ESP32 BLE functions here to populate foundBleDevices[] and bleScanCount
    // Example:
    bleScanCount = 0;
    // Scan logic (pseudo):
    // for each found BLE device:
    //    foundBleDevices[bleScanCount++] = {name, address};
    // If no devices:
    if (bleScanCount == 0) {
        foundBleDevices[0] = {"(No devices)", ""};
        bleScanCount = 1;
    }
    bleSelectIndex = 0;
    bleMenuScroll = 0;
}
void ensureBleListFresh() {
    if (bleScanCount == 1 && foundBleDevices[0].name == "(No devices)") scanBleDevices();
}

void drawBleMenu() {
    ensureBleListFresh();
    dma_display->fillScreen(dma_display->color565(0, 0, 0));
    dma_display->setFont(&Font5x7Uts);

    dma_display->setTextColor(dma_display->color565(0,255,255));
    dma_display->setCursor(0, 0);
    dma_display->print("Select BLE Device:");

    const int labelHeight = 7;
    const int listStartY = labelHeight + 1;
    const int lineHeight = 8;
    for (int i = 0; i < bleVisibleLines; ++i) {
        int idx = bleMenuScroll + i;
        if (idx >= bleScanCount) break;
        dma_display->setCursor(0, listStartY + lineHeight * i);
        if (idx == bleSelectIndex)
            dma_display->setTextColor(dma_display->color565(255,255,0));
        else
            dma_display->setTextColor(dma_display->color565(255,255,255));
        dma_display->print(foundBleDevices[idx].name.c_str());
    }
}


void scanBLENetworks() {
    bleScanCount = 0;
    // TODO: Insert actual ESP32 BLE scan code here.
    // For now, simulate found devices or "no devices"
    // Example with some mock names:
    int n = 3; // suppose you found 3 devices; replace with BLE scan result

    if (n <= 0) {
        scannedBLENames[0] = "(No devices)";
        bleScanCount = 1;
    } else {
        scannedBLENames[0] = "Mi Band 6";
        scannedBLENames[1] = "ESP32 Beacon";
        scannedBLENames[2] = "BT Speaker";
        bleScanCount = n;
    }
    bleSelectIndex = 0;
    bleMenuScroll = 0;
}