#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <vector>

#include "menu.h"
#include "display.h"
#include "settings.h"
#include "system.h"
#include "sensors.h"
#include "cloud_manager.h"
#include "device_identity.h"
#include "buzzer.h"
#include "mp3_player.h"
#include "audio_out.h"
#include "pins.h"
#include "sd_card.h"

namespace
{
bool g_audioTestToneActive = false;
uint8_t g_audioTestPhase = 0;
unsigned long g_audioTestNextAtMs = 0;
Mp3PathList g_sdMp3Paths;
String g_selectedSdMp3Path;
size_t g_sdMp3Page = 0;
size_t g_currentSdMp3Index = 0;
bool g_sdMp3PlaybackUiActive = false;
bool g_sdMp3VolumeDirty = false;
constexpr size_t kSdMp3PageSize = 14;
enum SdMp3Mode
{
    SdMp3PlayOne = 0,
    SdMp3ContinueNext = 1,
    SdMp3RepeatOne = 2,
};

AudioOut &audioTestOut()
{
    return speakerAudioOut();
}

String mp3DisplayName(const String &path)
{
    int slash = path.lastIndexOf('/');
    String name = (slash >= 0) ? path.substring(slash + 1) : path;
    if (name.isEmpty())
    {
        name = path;
    }
    return name;
}

const char *sdMp3ModeName()
{
    switch (mp3PlayMode)
    {
    case SdMp3PlayOne:
        return "Play One";
    case SdMp3RepeatOne:
        return "Repeat";
    default:
        return "Continue Next";
    }
}

void showAudioTestModal()
{
    sysInfoModal = InfoModal("Audio Test");
    String lines[] = {"Playing I2S", "Back to stop"};
    InfoFieldType types[] = {InfoLabel, InfoLabel};
    sysInfoModal.setLines(lines, types, 2);
    sysInfoModal.setKeepOpenOnSelect(true);
    sysInfoModal.setCallback([](bool, int) {});
    sysInfoModal.show();
}

void showSdMp3ListModal();
void showSdMp3PlaybackModal();

void beginSdMp3ListLoad()
{
    currentMenuLevel = MENU_SYSINFO;
    menuActive = true;

    sysInfoModal = InfoModal("MP3 Files");
    String lines[] = {"Loading MP3 list...", "Scanning SD card"};
    InfoFieldType types[] = {InfoLabel, InfoLabel};
    sysInfoModal.setLines(lines, types, 2);
    sysInfoModal.setKeepOpenOnSelect(true);
    sysInfoModal.setCallback([](bool, int) {});
    sysInfoModal.show();

    delay(30);

    if (g_sdMp3Paths.empty())
    {
        wxv::audio::listSdMp3Files(g_sdMp3Paths, 512);
    }

    showSdMp3ListModal();
}

void rebuildSdMp3PlaybackModal()
{
    sysInfoModal = InfoModal("Now Playing");
    String lines[] = {
        mp3DisplayName(g_selectedSdMp3Path),
        "Volume: " + String(buzzerVolume),
        "Mode: " + String(sdMp3ModeName()),
        "Up/Dn Vol  OK Stop",
        "Left Prev  Right Next"};
    InfoFieldType types[] = {InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel};
    sysInfoModal.setLines(lines, types, 5);
    sysInfoModal.setKeepOpenOnSelect(true);
    sysInfoModal.setCallback([](bool, int) {});
}

void showSdMp3PlaybackModal()
{
    currentMenuLevel = MENU_SYSINFO;
    menuActive = true;
    g_sdMp3PlaybackUiActive = true;
    rebuildSdMp3PlaybackModal();
    sysInfoModal.show();
}

bool startPlaybackForCurrentSelection()
{
    if (g_sdMp3Paths.empty() || g_currentSdMp3Index >= g_sdMp3Paths.size())
    {
        return false;
    }

    g_selectedSdMp3Path = g_sdMp3Paths[g_currentSdMp3Index];
    if (!wxv::audio::startSdMp3(g_selectedSdMp3Path, buzzerVolume))
    {
        showSectionHeading("MP3 START ERR", nullptr, 1200);
        return false;
    }

    showSdMp3PlaybackModal();
    return true;
}

void startSelectedSdMp3Playback()
{
    if (!startPlaybackForCurrentSelection())
    {
        pendingModalFn = showSdMp3ListModal;
        pendingModalTime = millis() + 1200UL;
    }
}

void showSdMp3ListModal()
{
    if (currentMenuLevel != MENU_NONE && currentMenuLevel != MENU_MAIN)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_SYSINFO;
    menuActive = true;
    sysInfoModal = InfoModal("MP3 Files");
    String lines[InfoModal::MAX_LINES];
    InfoFieldType types[InfoModal::MAX_LINES];
    int count = 0;

    if (g_sdMp3Paths.empty())
    {
        lines[count] = "No MP3 files";
        types[count++] = InfoLabel;
    }
    else
    {
        const size_t pageCount = (g_sdMp3Paths.size() + kSdMp3PageSize - 1) / kSdMp3PageSize;
        if (g_sdMp3Page >= pageCount)
        {
            g_sdMp3Page = pageCount - 1;
        }

        const size_t start = g_sdMp3Page * kSdMp3PageSize;
        const size_t end = min(start + kSdMp3PageSize, g_sdMp3Paths.size());

        if (g_sdMp3Page > 0)
        {
            lines[count] = "< Previous Page";
            types[count++] = InfoButton;
        }

        for (size_t i = start; i < end && count < InfoModal::MAX_LINES; ++i)
        {
            lines[count] = mp3DisplayName(g_sdMp3Paths[i]);
            types[count++] = InfoButton;
        }

        if (end < g_sdMp3Paths.size() && count < InfoModal::MAX_LINES)
        {
            lines[count] = "Next Page >";
            types[count++] = InfoButton;
        }
    }

    sysInfoModal.setLines(lines, types, count);
    sysInfoModal.setKeepOpenOnSelect(true);
    sysInfoModal.setShowForwardArrow(true);
    sysInfoModal.setCallback([](bool accepted, int btnIdx) {
        if (!accepted)
        {
            return;
        }

        int selected = btnIdx >= 0 ? btnIdx : sysInfoModal.getSelIndex();
        if (selected < 0)
        {
            return;
        }

        const size_t start = g_sdMp3Page * kSdMp3PageSize;
        int cursor = selected;

        if (g_sdMp3Page > 0)
        {
            if (cursor == 0)
            {
                --g_sdMp3Page;
                showSdMp3ListModal();
                return;
            }
            --cursor;
        }

        const size_t remaining = g_sdMp3Paths.size() - start;
        const size_t fileCountOnPage = min(kSdMp3PageSize, remaining);
        if (cursor >= static_cast<int>(fileCountOnPage))
        {
            ++g_sdMp3Page;
            showSdMp3ListModal();
            return;
        }

        const size_t selectedIndex = start + static_cast<size_t>(cursor);
        if (selectedIndex >= g_sdMp3Paths.size())
        {
            return;
        }

        g_currentSdMp3Index = selectedIndex;
        g_selectedSdMp3Path = g_sdMp3Paths[selectedIndex];
        sysInfoModal.hide();
        pendingModalFn = startSelectedSdMp3Playback;
        pendingModalTime = millis() + 10;
    });
    sysInfoModal.show();
}

void startAudioTestTone()
{
    if (!ensureSpeakerReady())
    {
        showSectionHeading("AUDIO INIT ERR", nullptr, 1200);
        return;
    }

    suppressNextMenuOkTone();
    audioTestOut().setSampleRate(22050);
    audioTestOut().stop();
    g_audioTestToneActive = true;
    g_audioTestPhase = 0;
    g_audioTestNextAtMs = 0;
    showAudioTestModal();
}

void rebuildSystemInfoModal()
{
    const uint32_t flashChipSize = ESP.getFlashChipSize();
    const uint32_t sketchSize = ESP.getSketchSize();
    (void)flashChipSize;

    const esp_partition_t *running = esp_ota_get_running_partition();
    const uint32_t appPartition = running ? running->size : 0;

    const uint32_t heapFree = ESP.getFreeHeap();
    const uint32_t heapMinFree = ESP.getMinFreeHeap();
    const uint32_t heapLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t heapTotal = 327680;
    const uint32_t heapUsed = heapTotal - heapFree;
    const float heapPct = 100.0f * heapUsed / heapTotal;
    const bool hasPsram = psramFound();
    const uint32_t psramTotal = hasPsram ? ESP.getPsramSize() : 0;
    const uint32_t psramFree = hasPsram ? ESP.getFreePsram() : 0;
    const uint32_t psramLargest = hasPsram ? heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : 0;
    const uint32_t psramUsed = hasPsram ? (psramTotal - psramFree) : 0;
    const float psramPct = (psramTotal > 0) ? (100.0f * psramUsed / psramTotal) : 0.0f;
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
        "Heap Free: " + String(heapFree) + " B (min " + String(heapMinFree) + ")",
        "Heap MaxBlk: " + String(heapLargest) + " B",
        "PSRAM: " + String(hasPsram ? String(psramPct, 1) + "% (" + String(psramUsed) + "/" + String(psramTotal) + " B)" : String("not found")),
        "PSRAM Free: " + String(hasPsram ? String(psramFree) + " B (blk " + String(psramLargest) + ")" : String("--")),
        "Flash: " + String(flashPct, 1) + "% (" + String(sketchSize) + "/" + String(appPartition) + " B)",
        "SPIFFS: " + String(spiffsPct, 1) + "% (" + String(spiffsUsed / 1024) + "/" + String(spiffsTotal / 1024) + " KB)"};
    InfoFieldType types[] = {
        InfoLabel, InfoLabel, InfoLabel, InfoLabel, InfoLabel,
        InfoLabel, InfoLabel, InfoLabel,
        InfoLabel, InfoLabel, InfoLabel, InfoLabel,
        InfoLabel, InfoLabel};

    sysInfoModal.setLines(lines, types, 14);
    sysInfoModal.setKeepOpenOnSelect(true);
    sysInfoModal.setCallback([](bool, int) {});
}

void rebuildSdDiagnosticsModal()
{
    const bool mountOk = wxv::storage::begin();
    const char *mountState = mountOk ? "OK" : "FAIL";
    const char *cardType = wxv::storage::isMounted() ? wxv::storage::cardTypeName() : "NONE";
    Mp3PathList mp3Paths;
    const size_t mp3Count = wxv::audio::listSdMp3Files(mp3Paths, 4);
    size_t probeBytesRead = 0;
    String probeStatus = "no file";
    if (!mp3Paths.empty())
    {
        wxv::audio::testSdFileRead(mp3Paths.front(), probeBytesRead, probeStatus);
    }

    String lines[InfoModal::MAX_LINES];
    InfoFieldType types[InfoModal::MAX_LINES];
    int lineCount = 0;

    lines[lineCount] = "Mount: " + String(mountState);
    types[lineCount++] = InfoLabel;
    lines[lineCount] = "Status: " + String(wxv::storage::lastStatus());
    types[lineCount++] = InfoLabel;
    lines[lineCount] = "Mounted: " + String(wxv::storage::isMounted() ? "yes" : "no");
    types[lineCount++] = InfoLabel;
    if (lineCount < InfoModal::MAX_LINES)
    {
        lines[lineCount] = "PSRAM: " + String(psramFound() ? "yes" : "no");
        types[lineCount++] = InfoLabel;
    }
    lines[lineCount] = "Type: " + String(cardType);
    types[lineCount++] = InfoLabel;
    lines[lineCount] = "Card Size: " + String(wxv::storage::cardSizeMB()) + " MB";
    types[lineCount++] = InfoLabel;
    lines[lineCount] = "Total: " + String(wxv::storage::totalMB()) + " MB";
    types[lineCount++] = InfoLabel;
    lines[lineCount] = "Used: " + String(wxv::storage::usedMB()) + " MB";
    types[lineCount++] = InfoLabel;
    lines[lineCount] = "MP3 Count: " + String(static_cast<unsigned int>(mp3Count));
    types[lineCount++] = InfoLabel;
    lines[lineCount] = "Read Test: " + probeStatus;
    types[lineCount++] = InfoLabel;
    lines[lineCount] = "Read Bytes: " + String(static_cast<unsigned int>(probeBytesRead));
    types[lineCount++] = InfoLabel;
    if (lineCount < InfoModal::MAX_LINES)
    {
        lines[lineCount] = "MP3 Status: " + String(wxv::audio::lastMp3Status());
        types[lineCount++] = InfoLabel;
    }
    if (lineCount < InfoModal::MAX_LINES)
    {
        lines[lineCount] = "MP3 Frames: " + String(wxv::audio::lastMp3FrameCount());
        types[lineCount++] = InfoLabel;
    }
    for (size_t i = 0; i < mp3Paths.size() && lineCount < InfoModal::MAX_LINES; ++i)
    {
        lines[lineCount] = "MP3[" + String(static_cast<unsigned int>(i + 1)) + "]: " + mp3Paths[i];
        types[lineCount++] = InfoLabel;
    }
    if (mp3Paths.empty() && lineCount < InfoModal::MAX_LINES)
    {
        lines[lineCount] = "MP3[1]: none found";
        types[lineCount++] = InfoLabel;
    }
    if (lineCount < InfoModal::MAX_LINES)
    {
        lines[lineCount] = "SCK: GPIO" + String(SD_SCK_PIN);
        types[lineCount++] = InfoLabel;
    }
    if (lineCount < InfoModal::MAX_LINES)
    {
        lines[lineCount] = "MOSI: GPIO" + String(SD_MOSI_PIN);
        types[lineCount++] = InfoLabel;
    }
    if (lineCount < InfoModal::MAX_LINES)
    {
        lines[lineCount] = "MISO: GPIO" + String(SD_MISO_PIN);
        types[lineCount++] = InfoLabel;
    }
    if (lineCount < InfoModal::MAX_LINES)
    {
        lines[lineCount] = "CS: GPIO" + String(SD_CS_PIN);
        types[lineCount++] = InfoLabel;
    }

    sysInfoModal.setLines(lines, types, lineCount);
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
        "MP3 Mode",
        "Date & Time",
        "Units",
        "Device Location",
        "System Info",
        "WiFi Signal Test",
        "SD Diagnostics",
        "Audio Test Tone",
        "Play SD MP3",
        "Now Playing",
        "Preview Screens",
        "Learn Remote",
        "Clear Learned Remote",
        "Reset Settings",
        "Factory Reset (Erase Wi-Fi + Logs)",
        "Reboot"};

    InfoFieldType types[] = {
        InfoNumber, InfoChooser, InfoChooser, InfoButton, InfoButton, InfoButton,
        InfoButton, InfoButton, InfoButton, InfoButton, InfoButton,
        InfoButton, InfoButton, InfoButton, InfoButton, InfoButton, InfoButton, InfoButton};
    int *numberRefs[] = {&buzzerVolume};
    int *chooserRefs[] = {&buzzerToneSet, &mp3PlayMode};
    static const char *toneOpts[] = {"Bright", "Soft", "Click", "Chime", "Pulse", "Warm", "Melody"};
    static const char *mp3ModeOpts[] = {"Play One", "Continue Next", "Repeat"};
    const char *const *chooserOpts[] = {toneOpts, mp3ModeOpts};
    int chooserCounts[] = {7, 3};

    systemModal.setLines(labels, types, 18);
    systemModal.setValueRefs(numberRefs, 1, chooserRefs, 2, chooserOpts, chooserCounts, nullptr, 0, nullptr);
    systemModal.setShowNumberArrows(true);
    systemModal.setShowChooserArrows(true);
    systemModal.setShowForwardArrow(true);
    systemModal.setKeepOpenOnSelect(true);
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
        mp3PlayMode = constrain(mp3PlayMode, 0, 2);
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
                break;
            case 3:
                systemModal.hide();
                showDateTimeModal();
                return;
            case 4:
                systemModal.hide();
                pendingModalFn = showUnitSettingsModal;
                pendingModalTime = millis();
                return;
            case 5:
                systemModal.hide();
                showDeviceLocationModal();
                return;
            case 6:
                systemModal.hide();
                showSystemInfoScreen();
                return;
            case 7:
                systemModal.hide();
                showWiFiSignalTest();
                return;
            case 8:
                systemModal.hide();
                showSdDiagnosticsScreen();
                return;
            case 9:
                systemModal.hide();
                pendingModalFn = playAudioTestTone;
                pendingModalTime = millis() + 10;
                return;
            case 10:
                systemModal.hide();
                g_sdMp3Paths.clear();
                g_sdMp3Page = 0;
                pendingModalFn = beginSdMp3ListLoad;
                pendingModalTime = millis() + 10;
                return;
            case 11:
                systemModal.hide();
                if (wxv::audio::isSdMp3Active())
                {
                    pendingModalFn = showSdMp3PlaybackModal;
                    pendingModalTime = millis() + 10;
                }
                else
                {
                    showSectionHeading("NO MP3 PLAY", nullptr, 1000);
                    pendingModalFn = showSystemModal;
                    pendingModalTime = millis() + 1100;
                }
                return;
            case 12:
                systemModal.hide();
                showScenePreviewModal();
                return;
            case 13:
                systemModal.hide();
                startUniversalRemoteLearning();
                return;
            case 14:
                systemModal.hide();
                clearUniversalRemoteLearning();
                break;
            case 15:
                systemModal.hide();
                quickRestore();
                break;
            case 16:
                systemModal.hide();
                factoryReset();
                break;
            case 17:
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

void showSdDiagnosticsScreen()
{
    if (currentMenuLevel != MENU_NONE && currentMenuLevel != MENU_MAIN)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_SYSINFO;
    menuActive = true;

    sysInfoModal = InfoModal("SD Diag");
    rebuildSdDiagnosticsModal();
    sysInfoModal.show();
}

void playAudioTestTone()
{
    startAudioTestTone();
}

bool isAudioTestToneActive()
{
    return g_audioTestToneActive;
}

void tickAudioTestTone()
{
    if (!g_audioTestToneActive)
    {
        return;
    }

    const unsigned long now = millis();
    if (now < g_audioTestNextAtMs)
    {
        return;
    }

    switch (g_audioTestPhase)
    {
    case 0:
        audioTestOut().playTone(330, 300, 9000);
        g_audioTestNextAtMs = millis() + 180;
        break;
    case 1:
        audioTestOut().playTone(660, 300, 9000);
        g_audioTestNextAtMs = millis() + 180;
        break;
    default:
        audioTestOut().playTone(990, 500, 9000);
        g_audioTestNextAtMs = millis() + 600;
        break;
    }

    g_audioTestPhase = static_cast<uint8_t>((g_audioTestPhase + 1) % 3);
}

void stopAudioTestTone(bool reopenSystemMenu)
{
    if (!g_audioTestToneActive)
    {
        return;
    }

    g_audioTestToneActive = false;
    audioTestOut().stop();
    if (sysInfoModal.isActive())
    {
        sysInfoModal.hide();
    }
    showSectionHeading("AUDIO TEST OFF", nullptr, 700);

    if (reopenSystemMenu)
    {
        pendingModalFn = showSystemModal;
        pendingModalTime = millis() + 10;
    }
}

bool isSdMp3PlaybackActive()
{
    return g_sdMp3PlaybackUiActive;
}

bool isSdMp3PlaybackRunning()
{
    return wxv::audio::isSdMp3Active();
}

void closeSdMp3PlaybackUi(bool reopenList)
{
    if (g_sdMp3VolumeDirty)
    {
        saveDeviceSettings();
        g_sdMp3VolumeDirty = false;
    }

    g_sdMp3PlaybackUiActive = false;
    if (sysInfoModal.isActive())
    {
        sysInfoModal.hide();
    }

    if (reopenList)
    {
        pendingModalFn = showSdMp3ListModal;
        pendingModalTime = millis() + 10;
    }
}

void stopSdMp3PlaybackUi(bool reopenList)
{
    if (g_sdMp3VolumeDirty)
    {
        saveDeviceSettings();
        g_sdMp3VolumeDirty = false;
    }

    wxv::audio::stopSdMp3();
    g_sdMp3PlaybackUiActive = false;
    if (sysInfoModal.isActive())
    {
        sysInfoModal.hide();
    }

    if (reopenList)
    {
        pendingModalFn = showSdMp3ListModal;
        pendingModalTime = millis() + 10;
    }
}

void tickSdMp3Playback()
{
    const bool wasRunning = wxv::audio::isSdMp3Active();
    if (!wasRunning)
    {
        return;
    }

    if (g_sdMp3PlaybackUiActive && sysInfoModal.isActive())
    {
        sysInfoModal.tick();
    }

    wxv::audio::tickSdMp3();
    if (!wxv::audio::isSdMp3Active())
    {
        const String lastStatus = wxv::audio::lastMp3Status();
        const bool naturalEnd = lastStatus != "stopped";

        if (naturalEnd && !g_sdMp3Paths.empty())
        {
            if (mp3PlayMode == SdMp3RepeatOne)
            {
                startPlaybackForCurrentSelection();
                return;
            }
            if (mp3PlayMode == SdMp3ContinueNext)
            {
                g_currentSdMp3Index = (g_currentSdMp3Index + 1) % g_sdMp3Paths.size();
                startPlaybackForCurrentSelection();
                return;
            }
        }

        if (g_sdMp3VolumeDirty)
        {
            saveDeviceSettings();
            g_sdMp3VolumeDirty = false;
        }
        const bool reopenList = g_sdMp3PlaybackUiActive;
        g_sdMp3PlaybackUiActive = false;
        if (reopenList && sysInfoModal.isActive())
        {
            sysInfoModal.hide();
        }
        if (reopenList)
        {
            pendingModalFn = showSdMp3ListModal;
            pendingModalTime = millis() + 700UL;
        }
    }
}

void handleSdMp3PlaybackIR(IRCodes::WxKey key)
{
    if (!g_sdMp3PlaybackUiActive)
    {
        return;
    }

    switch (key)
    {
    case IRCodes::WxKey::Up:
        buzzerVolume = constrain(buzzerVolume + 5, 0, 100);
        wxv::audio::setSdMp3VolumePercent(buzzerVolume);
        g_sdMp3VolumeDirty = true;
        rebuildSdMp3PlaybackModal();
        sysInfoModal.show();
        break;
    case IRCodes::WxKey::Down:
        buzzerVolume = constrain(buzzerVolume - 5, 0, 100);
        wxv::audio::setSdMp3VolumePercent(buzzerVolume);
        g_sdMp3VolumeDirty = true;
        rebuildSdMp3PlaybackModal();
        sysInfoModal.show();
        break;
    case IRCodes::WxKey::Left:
        if (!g_sdMp3Paths.empty())
        {
            g_currentSdMp3Index = (g_currentSdMp3Index == 0) ? (g_sdMp3Paths.size() - 1) : (g_currentSdMp3Index - 1);
            wxv::audio::stopSdMp3();
            startPlaybackForCurrentSelection();
        }
        break;
    case IRCodes::WxKey::Right:
        if (!g_sdMp3Paths.empty())
        {
            g_currentSdMp3Index = (g_currentSdMp3Index + 1) % g_sdMp3Paths.size();
            wxv::audio::stopSdMp3();
            startPlaybackForCurrentSelection();
        }
        break;
    case IRCodes::WxKey::Ok:
        stopSdMp3PlaybackUi(true);
        break;
    case IRCodes::WxKey::Cancel:
    case IRCodes::WxKey::Menu:
        closeSdMp3PlaybackUi(true);
        break;
    default:
        break;
    }
}
