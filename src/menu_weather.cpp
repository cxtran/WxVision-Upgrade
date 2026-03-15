#include <Arduino.h>
#include <WiFi.h>
#include <cstring>

#include "menu.h"
#include "display.h"
#include "settings.h"
#include "weather_countries.h"
#include "tempest.h"
#include "noaa.h"
#include "screen_manager.h"
#include "weather_provider.h"

namespace
{
bool s_preserveNoaaEnabledTemp = false;
}

void requestNoaaSettingsModalRefresh()
{
    s_preserveNoaaEnabledTemp = true;
    noaaModal.hide();
    pendingModalFn = showNoaaSettingsModal;
    pendingModalTime = millis() + 10;
}

void showNoaaSettingsModal()
{
    if (currentMenuLevel != MENU_NONE)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_NOAA;
    menuActive = true;

    static int noaaEnabledTemp = 0;

    if (!s_preserveNoaaEnabledTemp)
        noaaEnabledTemp = noaaAlertsEnabled ? 1 : 0;
    s_preserveNoaaEnabledTemp = false;

    String labels[2];
    InfoFieldType types[2];
    int lineCount = 0;
    const int alertsLineIdx = lineCount;
    labels[lineCount] = "Alerts";
    types[lineCount++] = InfoChooser;
    int getAlertLineIdx = -1;
    if (noaaEnabledTemp > 0)
    {
        getAlertLineIdx = lineCount;
        labels[lineCount] = "Get Alert";
        types[lineCount++] = InfoButton;
    }
    int *chooserRefs[] = {&noaaEnabledTemp};
    static const char *alertsOpts[] = {"Off", "On"};
    const char *const *chooserOpts[] = {alertsOpts};
    int chooserCounts[] = {2};

    noaaModal.setLines(labels, types, lineCount);
    noaaModal.setValueRefs(nullptr, 0, chooserRefs, 1, chooserOpts, chooserCounts, nullptr, 0, nullptr);
    noaaModal.setKeepOpenOnSelect(true);

    noaaModal.setCallback([alertsLineIdx, getAlertLineIdx](bool accepted, int btnIdx) {
        const bool prevEnabled = noaaAlertsEnabled;
        noaaAlertsEnabled = (noaaEnabledTemp > 0);
        const int selectedLine = noaaModal.getSelIndex();

        if (noaaAlertsEnabled != prevEnabled)
        {
            saveNoaaSettings();
            notifyNoaaSettingsChanged();
            Serial.printf("[NOAA] enabled=%d\n", noaaAlertsEnabled);
        }

        if (accepted && (btnIdx == getAlertLineIdx || selectedLine == getAlertLineIdx))
        {
            NoaaManualFetchResult result = requestNoaaManualFetch();
            if (result == NOAA_MANUAL_FETCH_STARTED)
            {
                noaaModal.hide();
                menuActive = false;
                currentMenuLevel = MENU_NONE;
                queueTemporaryAlertHeading("GETTING ALERT...", 1200);
            }
            else if (result == NOAA_MANUAL_FETCH_BUSY)
                showSectionHeading("FETCHING...", nullptr, 1200);
            else if (result == NOAA_MANUAL_FETCH_BLOCKED)
                showSectionHeading("WIFI BUSY", nullptr, 1200);
            else
                showSectionHeading("NOAA OFF", nullptr, 1200);
            return;
        }

        if (accepted && (btnIdx == alertsLineIdx || selectedLine == alertsLineIdx))
            return;
    });

    noaaModal.show();
}

void showDeviceLocationModal()
{
    if (currentMenuLevel != MENU_NONE)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_SYSLOCATION;
    menuActive = true;

    static char latBuf[16];
    static char lonBuf[16];
    snprintf(latBuf, sizeof(latBuf), "%.4f", noaaLatitude);
    snprintf(lonBuf, sizeof(lonBuf), "%.4f", noaaLongitude);

    String labels[] = {"Latitude", "Longitude"};
    InfoFieldType types[] = {InfoText, InfoText};
    char *textRefs[] = {latBuf, lonBuf};
    int textSizes[] = {static_cast<int>(sizeof(latBuf)), static_cast<int>(sizeof(lonBuf))};

    locationModal.setLines(labels, types, 2);
    locationModal.setValueRefs(nullptr, 0, nullptr, 0, nullptr, nullptr, textRefs, 2, textSizes);
    locationModal.setCallback([](bool /*accepted*/, int) {
        String latStr = String(latBuf);
        String lonStr = String(lonBuf);
        latStr.trim();
        lonStr.trim();

        float lat = constrain(latStr.toFloat(), -90.0f, 90.0f);
        float lon = constrain(lonStr.toFloat(), -180.0f, 180.0f);
        noaaLatitude = lat;
        noaaLongitude = lon;

        saveNoaaSettings();
        notifyNoaaSettingsChanged();

        if (WiFi.status() == WL_CONNECTED && isDataSourceOpenMeteo())
        {
            wxv::provider::fetchActiveProviderData();
        }

        Serial.printf("[Location] lat=%.4f lon=%.4f\n", noaaLatitude, noaaLongitude);
    });
    locationModal.show();
}

void showWeatherSettingsModal()
{

    if (currentMenuLevel != MENU_NONE)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_WEATHER;
    menuActive = true;

    static int owmCountryIndexTemp = owmCountryIndex;
    static char owmCountryCustomBuf[4] = "";
    static char owmCityBuf[32];
    static char owmKeyBuf[48];
    owmCountryIndexTemp = owmCountryIndex;
    strncpy(owmCountryCustomBuf, owmCountryCustom.c_str(), sizeof(owmCountryCustomBuf));
    strncpy(owmCityBuf, owmCity.c_str(), sizeof(owmCityBuf));
    strncpy(owmKeyBuf, owmApiKey.c_str(), sizeof(owmKeyBuf));

    String labels[] = {"Country", "Custom Code", "City", "OWM API Key"};
    InfoFieldType types[] = {InfoChooser, InfoText, InfoText, InfoText};
    int *chooserRefs[] = {&owmCountryIndexTemp};
    const char *const *chooserOpts[] = {countryLabels};
    int chooserCounts[] = {countryCount};
    char *textRefs[] = {owmCountryCustomBuf, owmCityBuf, owmKeyBuf};
    int textSizes[] = {sizeof(owmCountryCustomBuf), sizeof(owmCityBuf), sizeof(owmKeyBuf)};

    weatherModal.setLines(labels, types, 4);
    weatherModal.setValueRefs(nullptr, 0, chooserRefs, 1, chooserOpts, chooserCounts, textRefs, 3, textSizes);

    weatherModal.setCallback([](bool ok, int btnIdx) {
        String prevCity = owmCity;
        String prevApiKey = owmApiKey;
        int prevCountryIndex = owmCountryIndex;
        String prevCountryCustom = owmCountryCustom;
        String prevCountryCode = owmCountryCode;

        int newCountryIndex = owmCountryIndexTemp;
        String newCountryCustom = String(owmCountryCustomBuf);
        newCountryCustom.trim();
        String newCity = String(owmCityBuf);
        newCity.trim();
        String newApiKey = String(owmKeyBuf);
        newApiKey.trim();

        bool settingsChanged =
            (newCountryIndex != prevCountryIndex) ||
            !newCountryCustom.equals(prevCountryCustom) ||
            !newCity.equals(prevCity) ||
            !newApiKey.equals(prevApiKey);

        owmCountryIndex = newCountryIndex;
        owmCountryCustom = newCountryCustom;
        owmCity = newCity;
        owmApiKey = newApiKey;

        if (owmCountryIndex < 10)
        {
            owmCountryCode = countryCodes[owmCountryIndex];
        }
        else
        {
            owmCountryCode = owmCountryCustom;
        }

        saveWeatherSettings();
        Serial.printf("[WeatherModal] Saved Country=%s (%s), City=%s\n",
                      countryLabels[owmCountryIndex], owmCountryCode.c_str(), owmCity.c_str());

        if (settingsChanged)
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                wxv::provider::fetchActiveProviderData();
                requestScrollRebuild();
                serviceScrollRebuild();
                displayWeatherData();
            }
            reset_Time_and_Date_Display = true;
        }

        weatherModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    });
    weatherModal.show();
}

void showWfTempestModal()
{
    if (currentMenuLevel != MENU_NONE)
    {
        pushMenu(currentMenuLevel);
    }
    currentMenuLevel = MENU_TEMPEST;
    menuActive = true;

    static char wfTokenBuf[48];
    static char wfStationBuf[16];
    strncpy(wfTokenBuf, wfToken.c_str(), sizeof(wfTokenBuf));
    wfTokenBuf[sizeof(wfTokenBuf) - 1] = '\0';
    strncpy(wfStationBuf, wfStationId.c_str(), sizeof(wfStationBuf));
    wfStationBuf[sizeof(wfStationBuf) - 1] = '\0';

    String labels[] = {"WF Token", "WF Station ID"};
    InfoFieldType types[] = {InfoText, InfoText};
    char *textRefs[] = {wfTokenBuf, wfStationBuf};
    int textSizes[] = {sizeof(wfTokenBuf), sizeof(wfStationBuf)};

    tempestModal.setLines(labels, types, 2);
    tempestModal.setValueRefs(nullptr, 0, nullptr, 0, nullptr, nullptr, textRefs, 2, textSizes);

    tempestModal.setCallback([](bool, int) {
        String prevToken = wfToken;
        String prevStation = wfStationId;
        prevToken.trim();
        prevStation.trim();

        String newToken = String(wfTokenBuf);
        String newStation = String(wfStationBuf);
        newToken.trim();
        newStation.trim();

        bool credsChanged = !newToken.equals(prevToken) || !newStation.equals(prevStation);

        wfToken = newToken;
        wfStationId = newStation;
        saveWeatherSettings();

        if (credsChanged && isDataSourceWeatherFlow())
        {
            wxv::provider::fetchActiveProviderData();
        }

        tempestModal.hide();
        currentMenuLevel = MENU_MAIN;
        currentMenuIndex = 0;
        menuScroll = 0;
    });

    tempestModal.show();
}
