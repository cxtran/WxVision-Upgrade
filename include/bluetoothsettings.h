#pragma once
#include <Arduino.h>
#include <vector>

struct BleDeviceInfo {
    String name;
    String address;
};

extern std::vector<BleDeviceInfo> foundBleDevices;
extern int bleScanCount;
extern int bleSelectIndex;
extern int bleMenuScroll;
extern const int bleVisibleLines ;
extern String scannedBLENames[];
// BLE device names, max 16 (legacy, if still needed)
extern String scannedBLENames[16];
// BLE menu and scan functions
void scanBleDevices();
void ensureBleListFresh();
void drawBleMenu();
void scanBLENetworks();
void selectBleNetwork(int delta);

// Optionally: selection and connection logic
// void connectToBleDevice(int idx);
