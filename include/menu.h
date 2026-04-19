#pragma once
#include <Arduino.h>
#include <vector>
#include "InfoModal.h"
#include "sensors.h"
#include "settings.h"

// --- Menu Core Functions ---
void handleIRKey(IRCodes::WxKey key);
void handleIR(uint32_t code);
bool handleGlobalIRKey(IRCodes::WxKey key);
bool handleGlobalIRCode(uint32_t code);
void connectToWiFi();
void drawWiFiMenu();
void onWiFiConnectFailed();
void updateMenu();
void drawMenu();
void handleUp();
void handleDown();
void handleSelect();
void handleLeft();
void handleRight();

// --- Feature handlers for new System Menu items ---
void showSystemInfoScreen();
void showSdDiagnosticsScreen();
void playAudioTestTone();
bool isAudioTestToneActive();
void tickAudioTestTone();
void stopAudioTestTone(bool reopenSystemMenu = true);
bool isSdMp3PlaybackActive();
bool isSdMp3PlaybackRunning();
void tickSdMp3Playback();
void handleSdMp3PlaybackIR(IRCodes::WxKey key);
void showDateTimeModal();
void showWiFiSignalTest();
void showMainMenuModal();
void showDeviceSettingsModal();
void showDisplaySettingsModal();
void showAlarmSettingsModal();
void showMqttSettingsModal();
void showNoaaSettingsModal();
void requestNoaaSettingsModalRefresh();
void showDeviceLocationModal();
void showWeatherSettingsModal();
void showWfTempestModal();
void showCalibrationModal();
void showSystemModal();
void showScenePreviewModal();
void showUnitSettingsModal();
void showWorldTimeModal();
void showInitialSetupPrompt();
void showWiFiSettingsModal();
bool isWeatherScenePreviewActive();
void handleWeatherScenePreviewIR(IRCodes::WxKey key);

int scanWiFiNetworks();
void handleScreenSwitch(int dir);

void exitToHomeScreen();

// --- WiFi/Network Globals ---
extern std::vector<String> foundSSIDs;
extern int selectedWifiIdx;
extern bool wifiSelecting;
extern int wifiScanCount;
extern String scannedSSIDs[16];
extern int wifiSelectIndex;
extern int wifiMenuScroll;


// --- Menu Identifiers (Modal Mode) ---
enum MenuLevel {
    MENU_NONE = -1, // Modal or inactive menu
    MENU_MAIN,
    MENU_DEVICE,
    MENU_WIFISETTINGS,
    MENU_DISPLAY,
    MENU_ALARM,
    MENU_MQTT,
    MENU_NOAA,
    MENU_WEATHER,
    MENU_TEMPEST,
    MENU_CALIBRATION,
    MENU_SYSTEM,
    MENU_WORLDTIME,
    MENU_SCENE_PREVIEW,
    MENU_INITIAL_SETUP,
    MENU_SYSINFO,
    MENU_SYSDATETIME,
    MENU_SYSUNITS,
    MENU_SYSWIFI,
    MENU_SYSLOCATION,
    MENU_WIFI_SELECT = 99
};
extern std::vector<MenuLevel> menuStack;
void pushMenu(MenuLevel newMenu);
// --- Menu State ---
extern MenuLevel currentMenuLevel;
extern int currentMenuIndex;
extern int menuScroll;
extern bool menuActive;
extern unsigned long lastMenuActivity;

// --- Core App Logic Externs ---
extern void saveDeviceSettings();
extern void saveDisplaySettings();
extern void saveWeatherSettings();
extern void saveCalibrationSettings();
extern void saveDateTimeSettings();
extern void displayClock();
extern void displayDate();
extern void displayWeatherData();

extern bool reset_Time_and_Date_Display;

// --- Weather/Device/Display/Calibration/OWM ---
extern String wifiSSID;
extern String wifiPass;
extern String owmCity;
extern String owmApiKey;
extern String wfToken;
extern String wfStationId;
//extern int units;
extern int dayFormat;
extern int dataSource;
extern int autoRotate;
extern int autoRotateInterval;
extern int manualScreen;
extern int theme;
extern int brightness;
extern int scrollSpeed;
extern int verticalScrollSpeed;
extern int scrollLevel;
extern int verticalScrollLevel;
extern String customMsg;
extern String deviceHostname;
extern float tempOffset;
extern int humOffset;
extern int lightGain;

extern int owmCountryIndex;
extern String owmCountryCustom;
extern String owmCountryCode;

// --- System/Modal Navigation ---
extern void quickRestore();
extern void resetPowerUsage();
extern void factoryReset();

// --- Modal Instances ---
extern InfoModal sysInfoModal;
extern InfoModal wifiInfoModal;
extern InfoModal dateModal;
extern InfoModal mainMenuModal;
extern InfoModal deviceModal;
extern InfoModal displayModal;
extern InfoModal weatherModal;
extern InfoModal tempestModal;
extern InfoModal calibrationModal;
extern InfoModal systemModal;
extern InfoModal scenePreviewModal;
extern InfoModal setupPromptModal;
extern InfoModal wifiSettingsModal;
extern InfoModal unitSettingsModal;
extern InfoModal alarmModal;
extern InfoModal mqttModal;
extern InfoModal noaaModal;
extern InfoModal locationModal;
extern InfoModal worldTimeModal;
extern InfoModal manageTzModal;

extern int alarmSlotSelection;
extern int alarmSlotShown;

// --- Helper for modal "reopen after delay" ---
extern void (*pendingModalFn)();
extern unsigned long pendingModalTime;

// Display modal state helpers
extern bool preserveDisplayModeTemp;
extern int cachedDisplayModeTemp;
extern int autoThemeModeTemp;


extern int scrollOffset;

