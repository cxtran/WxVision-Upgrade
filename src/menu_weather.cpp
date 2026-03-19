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
bool s_preserveIndoorAlertTemps = false;
}

void requestNoaaSettingsModalRefresh()
{
    s_preserveNoaaEnabledTemp = true;
    s_preserveIndoorAlertTemps = true;
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
    static int co2EnabledTemp = 0;
    static int tempEnabledTemp = 0;
    static int humidityEnabledTemp = 0;
    static int envTempThresholdTenths = 0;

    if (!s_preserveNoaaEnabledTemp)
        noaaEnabledTemp = noaaAlertsEnabled ? 1 : 0;
    if (!s_preserveIndoorAlertTemps)
    {
        co2EnabledTemp = envAlertCo2Enabled ? 1 : 0;
        tempEnabledTemp = envAlertTempEnabled ? 1 : 0;
        humidityEnabledTemp = envAlertHumidityEnabled ? 1 : 0;
        envTempThresholdTenths = static_cast<int>(lroundf(static_cast<float>(dispTemp(envAlertTempThresholdC)) * 10.0f));
    }
    s_preserveNoaaEnabledTemp = false;
    s_preserveIndoorAlertTemps = false;

    String labels[InfoModal::MAX_LINES];
    InfoFieldType types[InfoModal::MAX_LINES];
    int *numberRefs[InfoModal::MAX_LINES];
    int numberCount = 0;
    int *chooserRefs[InfoModal::MAX_LINES];
    const char *const *chooserOpts[InfoModal::MAX_LINES];
    int chooserCounts[InfoModal::MAX_LINES];
    int chooserCount = 0;
    int lineCount = 0;
    const int noaaLineIdx = lineCount;
    labels[lineCount] = "NOAA Alerts";
    types[lineCount++] = InfoChooser;
    int getAlertLineIdx = -1;
    if (noaaEnabledTemp > 0)
    {
        getAlertLineIdx = lineCount;
        labels[lineCount] = "Get Alert";
        types[lineCount++] = InfoButton;
    }

    const int co2LineIdx = lineCount;
    labels[lineCount] = "CO2 Alert";
    types[lineCount++] = InfoChooser;
    if (co2EnabledTemp > 0)
    {
        labels[lineCount] = "CO2 Threshold (ppm)";
        types[lineCount] = InfoNumber;
        numberRefs[numberCount++] = &envAlertCo2Threshold;
        ++lineCount;
    }

    const int tempLineIdx = lineCount;
    labels[lineCount] = "Temp Alert";
    types[lineCount++] = InfoChooser;
    if (tempEnabledTemp > 0)
    {
        labels[lineCount] = (units.temp == TempUnit::F) ? "Temp Threshold (F)" : "Temp Threshold (C)";
        types[lineCount] = InfoNumber;
        numberRefs[numberCount++] = &envTempThresholdTenths;
        ++lineCount;
    }

    const int humidityLineIdx = lineCount;
    labels[lineCount] = "Humidity Alert";
    types[lineCount++] = InfoChooser;
    if (humidityEnabledTemp > 0)
    {
        labels[lineCount] = "Hum Low Threshold (%)";
        types[lineCount] = InfoNumber;
        numberRefs[numberCount++] = &envAlertHumidityLowThreshold;
        ++lineCount;
        labels[lineCount] = "Hum High Threshold (%)";
        types[lineCount] = InfoNumber;
        numberRefs[numberCount++] = &envAlertHumidityHighThreshold;
        ++lineCount;
    }

    static const char *alertsOpts[] = {"Off", "On"};
    chooserRefs[chooserCount] = &noaaEnabledTemp;
    chooserOpts[chooserCount] = alertsOpts;
    chooserCounts[chooserCount] = 2;
    ++chooserCount;
    chooserRefs[chooserCount] = &co2EnabledTemp;
    chooserOpts[chooserCount] = alertsOpts;
    chooserCounts[chooserCount] = 2;
    ++chooserCount;
    chooserRefs[chooserCount] = &tempEnabledTemp;
    chooserOpts[chooserCount] = alertsOpts;
    chooserCounts[chooserCount] = 2;
    ++chooserCount;
    chooserRefs[chooserCount] = &humidityEnabledTemp;
    chooserOpts[chooserCount] = alertsOpts;
    chooserCounts[chooserCount] = 2;
    ++chooserCount;

    noaaModal.setLines(labels, types, lineCount);
    noaaModal.setValueRefs(numberRefs, numberCount, chooserRefs, chooserCount, chooserOpts, chooserCounts, nullptr, 0, nullptr);
    noaaModal.setKeepOpenOnSelect(true);

    NumberFieldConfig co2Cfg;
    co2Cfg.step = 50;
    co2Cfg.minValue = 400;
    co2Cfg.maxValue = 5000;
    co2Cfg.hasBounds = true;
    co2Cfg.accelerateOnHold = true;

    NumberFieldConfig tempAlertCfg;
    tempAlertCfg.step = 5;
    tempAlertCfg.minValue = (units.temp == TempUnit::F) ? 500 : 100;
    tempAlertCfg.maxValue = (units.temp == TempUnit::F) ? 1220 : 500;
    tempAlertCfg.hasBounds = true;
    tempAlertCfg.accelerateOnHold = true;

    NumberFieldConfig humidityCfg;
    humidityCfg.step = 1;
    humidityCfg.minValue = 0;
    humidityCfg.maxValue = 100;
    humidityCfg.hasBounds = true;
    humidityCfg.accelerateOnHold = true;

    for (int i = 0; i < lineCount; ++i)
    {
        if (labels[i].startsWith("CO2 Threshold"))
            noaaModal.setNumberFieldConfig(i, co2Cfg);
        else if (labels[i].startsWith("Temp Threshold"))
            noaaModal.setNumberFieldConfig(i, tempAlertCfg);
        else if (labels[i].startsWith("Hum Low Threshold") || labels[i].startsWith("Hum High Threshold"))
            noaaModal.setNumberFieldConfig(i, humidityCfg);
    }

    noaaModal.setCallback([noaaLineIdx, co2LineIdx, tempLineIdx, humidityLineIdx, getAlertLineIdx](bool accepted, int btnIdx) {
        const bool prevEnabled = noaaAlertsEnabled;
        noaaAlertsEnabled = (noaaEnabledTemp > 0);
        envAlertCo2Enabled = (co2EnabledTemp > 0);
        envAlertTempEnabled = (tempEnabledTemp > 0);
        envAlertHumidityEnabled = (humidityEnabledTemp > 0);
        const int selectedLine = noaaModal.getSelIndex();

        if (noaaAlertsEnabled != prevEnabled)
        {
            saveNoaaSettings();
            notifyNoaaSettingsChanged();
            Serial.printf("[NOAA] enabled=%d\n", noaaAlertsEnabled);
        }
        saveCalibrationSettings();

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

        if (accepted &&
            (btnIdx == noaaLineIdx || selectedLine == noaaLineIdx ||
             btnIdx == co2LineIdx || selectedLine == co2LineIdx ||
             btnIdx == tempLineIdx || selectedLine == tempLineIdx ||
             btnIdx == humidityLineIdx || selectedLine == humidityLineIdx))
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
