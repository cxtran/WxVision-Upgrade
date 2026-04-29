#include "audio_announcer.h"

#include <FS.h>
#include <SD.h>
#include <cmath>
#include <memory>

#include "AudioFileSourceBuffer.h"
#include "AudioFileSourceSD.h"
#include "AudioOutputI2S.h"
#include "buzzer.h"
#include "datetimesettings.h"
#include "display.h"
#include "mp3_player.h"
#include "keyboard.h"
#include "menu.h"
#include "noaa.h"
#include "sd_card.h"
#include "screen_manager.h"
#include "sensors.h"
#include "settings.h"
#include "phrase_wav_generator.h"
#include "ui_tone.h"
#include "units.h"
#include "weather_provider.h"

namespace wxv::announce
{
    namespace
    {
        constexpr size_t kMaxQueuedClips = 64;
        constexpr size_t kWavBufferBytes = 16384;
        constexpr int kI2sDmaBufferCount = 20;
        constexpr int kI2sDmaBufferBytes = 1024;
        constexpr unsigned long kRequestDebounceMs = 250UL;
        constexpr unsigned long kSpeechClipGapMs = 0UL;
        constexpr uint8_t kWavLoopBurstCount = 12;
        constexpr uint32_t kWavLoopBurstBudgetUs = 7000;
        constexpr uint8_t kWavCatchupBurstCount = 28;
        constexpr uint32_t kWavCatchupBudgetUs = 15000;
        constexpr uint32_t kWavCatchupGapUs = 8000;
        constexpr const char *kChimeDir = "/audio/chimes/";
        constexpr const char *kAlarmDir = "/audio/alarm/";

        String g_clipQueue[kMaxQueuedClips];
        size_t g_clipCount = 0;
        size_t g_clipIndex = 0;
        String g_lastStatus = "idle";
        unsigned long g_lastRequestMs = 0;
        unsigned long g_nextClipStartMs = 0;
        bool g_active = false;
        uint32_t g_lastWavServiceUs = 0;

        std::unique_ptr<AudioFileSourceSD> g_file;
        std::unique_ptr<AudioFileSourceBuffer> g_bufferedFile;
        std::unique_ptr<PhraseWavGenerator> g_wav;
        std::unique_ptr<AudioOutputI2S> g_out;
        std::unique_ptr<uint8_t[]> g_streamBuffer;

        void cleanupPlayback(bool clearAll);

        float volumePercentToGain(int volumePercent)
        {
            const int clamped = constrain(volumePercent, 0, 100);
            const float normalized = static_cast<float>(clamped) / 100.0f;
            return 0.02f + (0.98f * normalized * normalized);
        }

        int effectiveAudioVolumePercent(int contentPercent)
        {
            const int master = constrain(buzzerVolume, 0, 100);
            const int content = constrain(contentPercent, 0, 100);
            return (master * content + 50) / 100;
        }

        float clipGainScaleForPath(const String &path)
        {
            if (path.startsWith("/audio/chimes/"))
            {
                return 0.72f;
            }

            if (path.startsWith("/audio/alarm/"))
            {
                return 0.72f;
            }

            return 1.0f;
        }

        void restoreUiAfterAnnouncement()
        {
            if (isSectionHeadingActive() || isTemporaryAlertActive())
            {
                return;
            }

            if (inKeyboardMode)
            {
                drawKeyboard();
                return;
            }

            if (setupPromptModal.isActive())
            {
                setupPromptModal.redraw();
                return;
            }
            if (sysInfoModal.isActive())
            {
                sysInfoModal.redraw();
                return;
            }
            if (wifiInfoModal.isActive())
            {
                wifiInfoModal.redraw();
                return;
            }
            if (dateModal.isActive())
            {
                dateModal.redraw();
                return;
            }
            if (mainMenuModal.isActive())
            {
                mainMenuModal.redraw();
                return;
            }
            if (deviceModal.isActive())
            {
                deviceModal.redraw();
                return;
            }
            if (dataSourceModal.isActive())
            {
                dataSourceModal.redraw();
                return;
            }
            if (displayModal.isActive())
            {
                displayModal.redraw();
                return;
            }
            if (mqttModal.isActive())
            {
                mqttModal.redraw();
                return;
            }
            if (weatherModal.isActive())
            {
                weatherModal.redraw();
                return;
            }
            if (tempestModal.isActive())
            {
                tempestModal.redraw();
                return;
            }
            if (calibrationModal.isActive())
            {
                calibrationModal.redraw();
                return;
            }
            if (systemModal.isActive())
            {
                systemModal.redraw();
                return;
            }
            if (audioAnnouncementsModal.isActive())
            {
                audioAnnouncementsModal.redraw();
                return;
            }
            if (wifiSettingsModal.isActive())
            {
                wifiSettingsModal.redraw();
                return;
            }
            if (unitSettingsModal.isActive())
            {
                unitSettingsModal.redraw();
                return;
            }
            if (alarmModal.isActive())
            {
                alarmModal.redraw();
                return;
            }
            if (noaaModal.isActive())
            {
                noaaModal.redraw();
                return;
            }
            if (locationModal.isActive())
            {
                locationModal.redraw();
                return;
            }
            if (worldTimeModal.isActive())
            {
                worldTimeModal.redraw();
                return;
            }
            if (manageTzModal.isActive())
            {
                manageTzModal.redraw();
                return;
            }
            if (scenePreviewModal.isActive())
            {
                scenePreviewModal.redraw();
                return;
            }

            if (wifiSelecting && currentMenuLevel == MENU_WIFI_SELECT)
            {
                drawWiFiMenu();
                return;
            }

            if (menuActive)
            {
                drawMenu();
                return;
            }

            refreshVisibleScreen();
        }

        String normalizeKey(String key)
        {
            key.trim();
            key.toLowerCase();

            String out;
            out.reserve(key.length());
            bool lastWasUnderscore = false;
            for (size_t i = 0; i < key.length(); ++i)
            {
                const char c = key.charAt(i);
                const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
                if (alnum)
                {
                    out += c;
                    lastWasUnderscore = false;
                }
                else if (!lastWasUnderscore && out.length() > 0)
                {
                    out += '_';
                    lastWasUnderscore = true;
                }
            }

            while (out.endsWith("_"))
            {
                out.remove(out.length() - 1);
            }
            return out;
        }

        bool isCuckooKey(const String &key)
        {
            return normalizeKey(key) == "cucook";
        }

        String normalizeUiSoundKey(const String &uiKey)
        {
            const String key = normalizeKey(uiKey);
            if (key == "nav_select" || key == "nav_confirm")
                return "select";
            if (key == "nav_back")
                return "back";
            if (key == "nav_move" || key == "page_change")
                return "right";
            return key;
        }

        bool uiSoundKeyToTone(const String &uiKey, UiTone &tone)
        {
            const String key = normalizeUiSoundKey(uiKey);
            if (key == "up")
                tone = UI_TONE_UP;
            else if (key == "down")
                tone = UI_TONE_DOWN;
            else if (key == "left")
                tone = UI_TONE_LEFT;
            else if (key == "right")
                tone = UI_TONE_RIGHT;
            else if (key == "select" || key == "confirm")
                tone = UI_TONE_SELECT;
            else if (key == "back" || key == "cancel")
                tone = UI_TONE_BACK;
            else if (key == "toggle_on" || key == "on")
                tone = UI_TONE_TOGGLE_ON;
            else if (key == "toggle_off" || key == "off")
                tone = UI_TONE_TOGGLE_OFF;
            else if (key == "volume_up")
                tone = UI_TONE_VOLUME_UP;
            else if (key == "volume_down")
                tone = UI_TONE_VOLUME_DOWN;
            else
                tone = UI_TONE_ERROR;

            return true;
        }

        void clearQueue()
        {
            g_clipCount = 0;
            g_clipIndex = 0;
            for (String &clip : g_clipQueue)
            {
                clip = "";
            }
        }

        void closeCurrentClip()
        {
            if (g_wav)
            {
                g_wav->stop();
            }
            if (g_bufferedFile)
            {
                g_bufferedFile->close();
            }
            if (g_file)
            {
                g_file->close();
            }

            g_wav.reset();
            g_bufferedFile.reset();
            g_file.reset();
            g_streamBuffer.reset();
        }

        bool advanceToNextClip()
        {
            if (g_out)
            {
                g_out->flush();
            }

            closeCurrentClip();
            ++g_clipIndex;
            if (g_clipIndex >= g_clipCount)
            {
                g_lastStatus = "done";
                cleanupPlayback(true);
                restoreUiAfterAnnouncement();
                return false;
            }

            g_nextClipStartMs = millis() + kSpeechClipGapMs;
            return true;
        }

        void cleanupPlayback(bool clearAll)
        {
            closeCurrentClip();
            if (g_out)
            {
                g_out->flush();
                g_out->stop();
            }

            g_out.reset();
            g_active = false;
            g_nextClipStartMs = 0;
            g_lastWavServiceUs = 0;

            if (clearAll)
            {
                clearQueue();
            }

            setupBuzzer();
        }

        bool ensureSdReady()
        {
            return wxv::storage::isMounted() || wxv::storage::begin();
        }

        bool clipExists(const String &path)
        {
            return !path.isEmpty() && ensureSdReady() && wxv::storage::exists(path.c_str());
        }

        bool enqueueClipInternal(const String &path)
        {
            if (path.isEmpty() || g_clipCount >= kMaxQueuedClips)
            {
                return false;
            }
            if (!clipExists(path))
            {
                g_lastStatus = String("missing clip: ") + path;
                return false;
            }

            g_clipQueue[g_clipCount++] = path;
            return true;
        }

        bool enqueueOptionalClip(const String &path)
        {
            if (path.isEmpty() || g_clipCount >= kMaxQueuedClips || !clipExists(path))
            {
                return false;
            }

            g_clipQueue[g_clipCount++] = path;
            return true;
        }

        bool enqueueFirstExisting(const String &primary, const String &fallback = String())
        {
            if (!primary.isEmpty() && clipExists(primary) && g_clipCount < kMaxQueuedClips)
            {
                g_clipQueue[g_clipCount++] = primary;
                return true;
            }
            if (!fallback.isEmpty() && clipExists(fallback) && g_clipCount < kMaxQueuedClips)
            {
                g_clipQueue[g_clipCount++] = fallback;
                return true;
            }
            return false;
        }

        String numberClipPath(const char *folder, int value)
        {
            return String(folder) + String(value) + ".wav";
        }

        bool queueNumberPhrase(const char *folder, int value)
        {
            if (value < 0)
            {
                return enqueueClipInternal(String(folder) + "minus.wav") &&
                       queueNumberPhrase(folder, -value);
            }

            if (value <= 20 || (value < 100 && value % 10 == 0))
            {
                return enqueueClipInternal(numberClipPath(folder, value));
            }

            if (value < 100)
            {
                const int tens = (value / 10) * 10;
                const int ones = value % 10;
                return enqueueClipInternal(numberClipPath(folder, tens)) &&
                       enqueueClipInternal(numberClipPath(folder, ones));
            }

            if (value < 1000)
            {
                const int hundreds = value / 100;
                const int remainder = value % 100;
                if (!enqueueClipInternal(numberClipPath(folder, hundreds)) ||
                    !enqueueClipInternal(String(folder) + "hundred.wav"))
                {
                    return false;
                }
                return remainder == 0 || queueNumberPhrase(folder, remainder);
            }

            if (value < 10000)
            {
                const int thousands = value / 1000;
                const int remainder = value % 1000;
                if (!queueNumberPhrase(folder, thousands) ||
                    !enqueueClipInternal(String(folder) + "thousand.wav"))
                {
                    return false;
                }
                return remainder == 0 || queueNumberPhrase(folder, remainder);
            }

            return enqueueClipInternal(numberClipPath(folder, value));
        }

        String normalizeWeatherClipKey(String condition)
        {
            condition.trim();
            condition.toLowerCase();
            condition.replace('_', ' ');
            condition.replace('-', ' ');

            if (condition.indexOf("thunder") >= 0 || condition.indexOf("storm") >= 0 || condition.indexOf("squall") >= 0)
            {
                return "storm";
            }
            if (condition.indexOf("drizzle") >= 0 || condition.indexOf("shower") >= 0 || condition.indexOf("rain") >= 0)
            {
                return "rain";
            }
            if (condition.indexOf("fog") >= 0 || condition.indexOf("mist") >= 0 || condition.indexOf("haze") >= 0 || condition.indexOf("smoke") >= 0)
            {
                return "fog";
            }
            if (condition.indexOf("cloud") >= 0 || condition.indexOf("overcast") >= 0)
            {
                return "cloudy";
            }
            if (condition.indexOf("clear") >= 0 || condition.indexOf("sun") >= 0 || condition.indexOf("fair") >= 0)
            {
                return "clear";
            }

            return "cloudy";
        }

        String normalizeAlertText(String value)
        {
            value.trim();
            value.toLowerCase();
            value.replace('_', ' ');
            value.replace('-', ' ');
            return value;
        }

        String alertSeverityClipPath(const String &severityRaw, const String &eventRaw)
        {
            String severity = normalizeAlertText(severityRaw);
            String event = normalizeAlertText(eventRaw);

            if (severity.indexOf("warning") >= 0 || event.indexOf("warning") >= 0)
            {
                return "/audio/alerts/severity/warning.wav";
            }
            if (severity.indexOf("watch") >= 0 || event.indexOf("watch") >= 0)
            {
                return "/audio/alerts/severity/watch.wav";
            }
            if (severity.indexOf("advisory") >= 0 || event.indexOf("advisory") >= 0)
            {
                return "/audio/alerts/severity/advisory.wav";
            }
            if (severity.indexOf("statement") >= 0 || event.indexOf("statement") >= 0)
            {
                return "/audio/alerts/severity/statement.wav";
            }

            return "/audio/alerts/severity/alert.wav";
        }

        String alertEventClipPath(const String &eventRaw, const String &headlineRaw)
        {
            String text = normalizeAlertText(eventRaw + " " + headlineRaw);

            if (text.indexOf("tornado") >= 0)
                return "/audio/alerts/events/tornado.wav";
            if (text.indexOf("flash flood") >= 0)
                return "/audio/alerts/events/flash_flood.wav";
            if (text.indexOf("flood") >= 0)
                return "/audio/alerts/events/flood.wav";
            if (text.indexOf("thunderstorm") >= 0)
                return "/audio/alerts/events/thunderstorm.wav";
            if (text.indexOf("lightning") >= 0)
                return "/audio/alerts/events/lightning.wav";
            if (text.indexOf("winter storm") >= 0)
                return "/audio/alerts/events/winter_storm.wav";
            if (text.indexOf("snow") >= 0 || text.indexOf("blizzard") >= 0)
                return "/audio/alerts/events/snow.wav";
            if (text.indexOf("ice") >= 0 || text.indexOf("freezing") >= 0)
                return "/audio/alerts/events/ice.wav";
            if (text.indexOf("wind") >= 0)
                return "/audio/alerts/events/wind.wav";
            if (text.indexOf("heat") >= 0)
                return "/audio/alerts/events/heat.wav";
            if (text.indexOf("cold") >= 0 || text.indexOf("freeze") >= 0)
                return "/audio/alerts/events/cold.wav";
            if (text.indexOf("fog") >= 0)
                return "/audio/alerts/events/fog.wav";
            if (text.indexOf("fire") >= 0 || text.indexOf("red flag") >= 0)
                return "/audio/alerts/events/fire.wav";
            if (text.indexOf("air quality") >= 0 || text.indexOf("smoke") >= 0)
                return "/audio/alerts/events/air_quality.wav";
            if (text.indexOf("hurricane") >= 0)
                return "/audio/alerts/events/hurricane.wav";
            if (text.indexOf("tropical storm") >= 0)
                return "/audio/alerts/events/tropical_storm.wav";
            if (text.indexOf("coastal") >= 0)
                return "/audio/alerts/events/coastal_flood.wav";
            if (text.indexOf("marine") >= 0 || text.indexOf("small craft") >= 0)
                return "/audio/alerts/events/marine.wav";
            if (text.indexOf("special weather") >= 0)
                return "/audio/alerts/events/special_weather.wav";

            return "/audio/alerts/events/weather_alert.wav";
        }

        bool resolveAnnouncementLocalTime(DateTime &out)
        {
            if (rtcReady)
            {
                DateTime utcNow = rtc.now();
                int offsetMinutes = timezoneIsCustom() ? tzStandardOffset : timezoneOffsetForUtc(utcNow);
                updateTimezoneOffsetWithUtc(utcNow);
                out = utcToLocal(utcNow, offsetMinutes);
                return true;
            }

            return getLocalDateTime(out);
        }

        bool queueTimePhrase()
        {
            DateTime localNow;
            if (!resolveAnnouncementLocalTime(localNow))
            {
                g_lastStatus = "local time unavailable";
                return false;
            }

            const int hour24 = localNow.hour();
            const int hour12 = (hour24 % 12 == 0) ? 12 : (hour24 % 12);
            const int minute = localNow.minute();
            const char *ampm = (hour24 >= 12) ? "pm" : "am";

            if (!enqueueFirstExisting("/audio/common/the_time_is.wav", "/audio/common/it_is.wav"))
            {
                return false;
            }

            if (!enqueueClipInternal(numberClipPath("/audio/numbers/", hour12)))
            {
                return false;
            }

            if (minute == 0)
            {
                if (!enqueueFirstExisting("/audio/time/oclock.wav", "/audio/numbers/0.wav"))
                {
                    return false;
                }
            }
            else if (minute > 0 && minute < 10)
            {
                if (!enqueueFirstExisting("/audio/numbers/oh.wav"))
                {
                    return false;
                }
                if (!enqueueClipInternal(numberClipPath("/audio/numbers/", minute)))
                {
                    return false;
                }
            }
            else if (!enqueueClipInternal(numberClipPath("/audio/numbers/", minute)))
            {
                return false;
            }

            return enqueueFirstExisting(String("/audio/common/") + ampm + ".wav",
                                        String("/audio/time/") + ampm + ".wav");
        }

        bool queueDatePhrase()
        {
            DateTime localNow;
            if (!resolveAnnouncementLocalTime(localNow))
            {
                g_lastStatus = "local date unavailable";
                return false;
            }

            static const char *kWeekdayNames[7] = {
                "sunday", "monday", "tuesday", "wednesday",
                "thursday", "friday", "saturday"};
            static const char *kMonthNames[12] = {
                "january", "february", "march", "april",
                "may", "june", "july", "august",
                "september", "october", "november", "december"};

            const uint8_t dow = static_cast<uint8_t>(localNow.dayOfTheWeek() % 7);
            const int month = localNow.month();
            const int day = localNow.day();

            if (!enqueueFirstExisting("/audio/common/today_is.wav"))
            {
                return false;
            }
            if (!enqueueClipInternal(String("/audio/date/") + kWeekdayNames[dow] + ".wav"))
            {
                return false;
            }
            if (month < 1 || month > 12)
            {
                g_lastStatus = "month out of range";
                return false;
            }
            if (!enqueueClipInternal(String("/audio/date/") + kMonthNames[month - 1] + ".wav"))
            {
                return false;
            }
            if (!enqueueClipInternal(String("/audio/date/") + String(day) + ".wav"))
            {
                return false;
            }
            return true;
        }

        bool queueTemperaturePhrase(float tempC, const char *locationKey = nullptr)
        {
            if (!isfinite(tempC))
            {
                g_lastStatus = "temperature unavailable";
                return false;
            }

            const int rounded = static_cast<int>(lroundf(static_cast<float>(dispTemp(tempC))));
            if (locationKey && *locationKey)
            {
                enqueueOptionalClip(String("/audio/common/") + locationKey + ".wav");
            }
            return enqueueClipInternal("/audio/common/temperature_is.wav") &&
                   queueNumberPhrase("/audio/numbers/", rounded) &&
                   enqueueFirstExisting("/audio/units/degrees.wav", "/audio/units/degree.wav") &&
                   enqueueFirstExisting((units.temp == TempUnit::F) ? "/audio/units/fahrenheit.wav"
                                                                   : "/audio/units/celsius.wav");
        }

        bool queueHumidityPhrase(float humidityPct)
        {
            if (!isfinite(humidityPct))
            {
                g_lastStatus = "humidity unavailable";
                return false;
            }

            const int rounded = static_cast<int>(lroundf(humidityPct));
            return enqueueClipInternal("/audio/common/humidity_is.wav") &&
                   queueNumberPhrase("/audio/numbers/", rounded) &&
                   enqueueFirstExisting("/audio/common/percent.wav", "/audio/units/percent.wav");
        }

        bool queueWeatherPhrase()
        {
            wxv::provider::WeatherSnapshot snapshot;
            if (!wxv::provider::readActiveProviderSnapshot(snapshot) || !snapshot.hasCurrent || snapshot.current.condition.isEmpty())
            {
                g_lastStatus = "weather unavailable";
                return false;
            }

            const String bucket = normalizeWeatherClipKey(snapshot.current.condition);
            return enqueueClipInternal("/audio/common/weather_is.wav") &&
                   enqueueClipInternal(String("/audio/weather/") + bucket + ".wav");
        }

        String windUnitClipPath()
        {
            switch (units.wind)
            {
            case WindUnit::MPH:
                return "/audio/units/mph.wav";
            case WindUnit::KTS:
                return "/audio/units/knots.wav";
            case WindUnit::KPH:
                return "/audio/units/kmh.wav";
            case WindUnit::MPS:
            default:
                return "/audio/units/meters_per_second.wav";
            }
        }

        String pressureUnitClipPath()
        {
            return (units.press == PressUnit::INHG) ? "/audio/units/inhg.wav" : "/audio/units/hpa.wav";
        }

        String precipUnitClipPath()
        {
            return (units.precip == PrecipUnit::INCH) ? "/audio/units/inches.wav" : "/audio/units/mm.wav";
        }

        bool queueDecimalNumberPhrase(float value, int decimals)
        {
            if (!isfinite(value))
            {
                return false;
            }

            const float scale = (decimals == 2) ? 100.0f : 10.0f;
            const int scaled = static_cast<int>(lroundf(value * scale));
            const int whole = scaled / static_cast<int>(scale);
            int frac = abs(scaled % static_cast<int>(scale));

            if (!queueNumberPhrase("/audio/numbers/", whole))
            {
                return false;
            }

            if (frac == 0)
            {
                return true;
            }

            if (!enqueueClipInternal("/audio/common/point.wav"))
            {
                return false;
            }

            if (decimals == 2)
            {
                return enqueueClipInternal(numberClipPath("/audio/numbers/", frac / 10)) &&
                       enqueueClipInternal(numberClipPath("/audio/numbers/", frac % 10));
            }

            return enqueueClipInternal(numberClipPath("/audio/numbers/", frac));
        }

        String windDirectionClipPath(float degrees)
        {
            static const char *names[8] = {
                "north", "northeast", "east", "southeast",
                "south", "southwest", "west", "northwest"};
            float normalized = fmodf(degrees, 360.0f);
            if (normalized < 0.0f)
            {
                normalized += 360.0f;
            }
            const int index = static_cast<int>(floorf((normalized + 22.5f) / 45.0f)) % 8;
            return String("/audio/wind/") + names[index] + ".wav";
        }

        bool queueWindSpeedPhrase()
        {
            wxv::provider::WeatherSnapshot snapshot;
            if (!wxv::provider::readActiveProviderSnapshot(snapshot) || !snapshot.hasCurrent || !isfinite(snapshot.current.windSpeedMps))
            {
                g_lastStatus = "wind speed unavailable";
                return false;
            }

            const int rounded = static_cast<int>(lround(dispWind(snapshot.current.windSpeedMps)));
            return enqueueClipInternal("/audio/common/wind_speed_is.wav") &&
                   queueNumberPhrase("/audio/numbers/", rounded) &&
                   enqueueClipInternal(windUnitClipPath());
        }

        bool queueWindDirectionPhrase()
        {
            wxv::provider::WeatherSnapshot snapshot;
            if (!wxv::provider::readActiveProviderSnapshot(snapshot) || !snapshot.hasCurrent || !isfinite(snapshot.current.windDirectionDeg))
            {
                g_lastStatus = "wind direction unavailable";
                return false;
            }

            return enqueueClipInternal("/audio/common/wind_direction_is.wav") &&
                   enqueueClipInternal(windDirectionClipPath(snapshot.current.windDirectionDeg));
        }

        bool queuePressurePhrase()
        {
            wxv::provider::WeatherSnapshot snapshot;
            if (!wxv::provider::readActiveProviderSnapshot(snapshot) || !snapshot.hasCurrent ||
                !isfinite(snapshot.current.pressureHpa) || snapshot.current.pressureHpa <= 200.0f)
            {
                g_lastStatus = "pressure unavailable";
                return false;
            }

            if (!enqueueClipInternal("/audio/common/pressure_is.wav"))
            {
                return false;
            }

            if (units.press == PressUnit::INHG)
            {
                const int scaled = static_cast<int>(lround(dispPress(snapshot.current.pressureHpa) * 100.0));
                const int whole = scaled / 100;
                const int frac = abs(scaled % 100);
                return queueNumberPhrase("/audio/numbers/", whole) &&
                       enqueueClipInternal("/audio/common/point.wav") &&
                       enqueueClipInternal(numberClipPath("/audio/numbers/", frac / 10)) &&
                       enqueueClipInternal(numberClipPath("/audio/numbers/", frac % 10)) &&
                       enqueueClipInternal(pressureUnitClipPath());
            }

            const int rounded = static_cast<int>(lround(dispPress(snapshot.current.pressureHpa)));
            return queueNumberPhrase("/audio/numbers/", rounded) &&
                   enqueueClipInternal(pressureUnitClipPath());
        }

        bool queueRainPhrase()
        {
            wxv::provider::WeatherSnapshot snapshot;
            if (!wxv::provider::readActiveProviderSnapshot(snapshot) || !snapshot.hasCurrent)
            {
                g_lastStatus = "rain unavailable";
                return false;
            }

            if (isfinite(snapshot.current.precipAmountMm) && snapshot.current.precipAmountMm >= 0.0f)
            {
                const float displayAmount = static_cast<float>(dispPrecip(snapshot.current.precipAmountMm));
                const int decimals = (units.precip == PrecipUnit::INCH) ? 2 : 1;
                return enqueueClipInternal("/audio/common/rain_is.wav") &&
                       queueDecimalNumberPhrase(displayAmount, decimals) &&
                       enqueueClipInternal(precipUnitClipPath());
            }

            if (snapshot.current.precipProbabilityPct >= 0)
            {
                return enqueueClipInternal("/audio/common/rain_chance_is.wav") &&
                       queueNumberPhrase("/audio/numbers/", snapshot.current.precipProbabilityPct) &&
                       enqueueFirstExisting("/audio/common/percent.wav", "/audio/units/percent.wav");
            }

            g_lastStatus = "rain unavailable";
            return false;
        }

        bool queueAlertsPhrase()
        {
            const size_t count = noaaAlertCount();
            if (count == 0)
            {
                return enqueueFirstExisting("/audio/common/no_active_alerts.wav",
                                            "/audio/alerts/severity/no_active_alerts.wav");
            }

            if (!enqueueClipInternal("/audio/common/weather_alerts.wav") ||
                !queueNumberPhrase("/audio/numbers/", static_cast<int>(count)) ||
                !enqueueFirstExisting("/audio/alerts/severity/active.wav"))
            {
                return false;
            }

            for (size_t i = 0; i < count && g_clipCount + 2 <= kMaxQueuedClips; ++i)
            {
                NwsAlert alert;
                if (!noaaGetAlert(i, alert))
                {
                    continue;
                }

                if (!enqueueClipInternal(alertSeverityClipPath(alert.severity, alert.event)) ||
                    !enqueueClipInternal(alertEventClipPath(alert.event, alert.headline)))
                {
                    return false;
                }
            }

            return true;
        }

        bool internalSensorFailureActive()
        {
            const bool scd40Fault = !scd40Ready || (!scd40IsWarmingUp() && !scd40DataIsFresh(180000UL));
            return scd40Fault || !bmp280Ready;
        }

        bool internalCo2AlertActive()
        {
            return envAlertCo2Enabled &&
                   SCD40_co2 > 0 &&
                   static_cast<float>(SCD40_co2) >= static_cast<float>(envAlertCo2Threshold);
        }

        bool internalTempAlertActive()
        {
            if (!envAlertTempEnabled || !isfinite(SCD40_temp))
            {
                return false;
            }
            return (SCD40_temp + tempOffset) > envAlertTempThresholdC;
        }

        int internalHumidityAlertDirection()
        {
            if (!envAlertHumidityEnabled || !isfinite(SCD40_hum))
            {
                return 0;
            }

            const float humidity = constrain(SCD40_hum + static_cast<float>(humOffset), 0.0f, 100.0f);
            if (humidity < static_cast<float>(envAlertHumidityLowThreshold))
            {
                return -1;
            }
            if (humidity > static_cast<float>(envAlertHumidityHighThreshold))
            {
                return 1;
            }
            return 0;
        }

        bool hasInternalSensorAlerts()
        {
            return internalSensorFailureActive() ||
                   internalCo2AlertActive() ||
                   internalTempAlertActive() ||
                   internalHumidityAlertDirection() != 0;
        }

        bool queueInternalAlertsPhrase(bool includeNormalMessage)
        {
            if (!hasInternalSensorAlerts())
            {
                return includeNormalMessage
                           ? enqueueClipInternal("/audio/common/internal_sensors_normal.wav")
                           : true;
            }

            if (!enqueueClipInternal("/audio/common/internal_sensor_alert.wav"))
            {
                return false;
            }

            if (internalSensorFailureActive() &&
                !enqueueClipInternal("/audio/alerts/internal/sensor_failure.wav"))
            {
                return false;
            }
            if (internalCo2AlertActive() &&
                !enqueueClipInternal("/audio/alerts/internal/co2_too_high.wav"))
            {
                return false;
            }
            if (internalTempAlertActive() &&
                !enqueueClipInternal("/audio/alerts/internal/temperature_too_high.wav"))
            {
                return false;
            }

            const int humidityDirection = internalHumidityAlertDirection();
            if (humidityDirection < 0)
            {
                return enqueueClipInternal("/audio/alerts/internal/humidity_too_low.wav");
            }
            if (humidityDirection > 0)
            {
                return enqueueClipInternal("/audio/alerts/internal/humidity_too_high.wav");
            }

            return true;
        }

        bool queueOutdoorTemperaturePhrase()
        {
            wxv::provider::WeatherSnapshot snapshot;
            if (!wxv::provider::readActiveProviderSnapshot(snapshot) || !snapshot.hasCurrent || !isfinite(snapshot.current.tempC))
            {
                g_lastStatus = "outdoor temp unavailable";
                return false;
            }
            return queueTemperaturePhrase(snapshot.current.tempC, "outdoor");
        }

        bool queueIndoorTemperaturePhrase()
        {
            if (!isfinite(SCD40_temp) || !scd40DataIsFresh())
            {
                g_lastStatus = "indoor temp unavailable";
                return false;
            }
            return queueTemperaturePhrase(SCD40_temp + tempOffset, "indoor");
        }

        bool queueIndoorHumidityPhrase()
        {
            if (!isfinite(SCD40_hum) || !scd40DataIsFresh())
            {
                g_lastStatus = "indoor humidity unavailable";
                return false;
            }
            const float calibratedHumidity = constrain(SCD40_hum + static_cast<float>(humOffset), 0.0f, 100.0f);
            return queueHumidityPhrase(calibratedHumidity);
        }

        bool startCurrentClip()
        {
            const bool createdOutput = (g_out == nullptr);
            if (g_clipIndex >= g_clipCount)
            {
                g_lastStatus = "done";
                cleanupPlayback(true);
                return false;
            }

            const String &path = g_clipQueue[g_clipIndex];
            if (!clipExists(path))
            {
                g_lastStatus = String("missing clip: ") + path;
                cleanupPlayback(true);
                return false;
            }

            closeCurrentClip();
            if (!g_out)
            {
                releaseSpeaker();
                g_out.reset(new (std::nothrow) AudioOutputI2S());
            }

            g_file.reset(new (std::nothrow) AudioFileSourceSD(path.c_str()));
            g_streamBuffer.reset(new (std::nothrow) uint8_t[kWavBufferBytes]);
            g_bufferedFile.reset(new (std::nothrow) AudioFileSourceBuffer(g_file.get(), g_streamBuffer.get(), kWavBufferBytes));
            g_wav.reset(new (std::nothrow) PhraseWavGenerator());

            if (!g_file || !g_bufferedFile || !g_out || !g_wav || !g_file->isOpen())
            {
                g_lastStatus = "wav alloc/open failed";
                cleanupPlayback(true);
                return false;
            }

            g_out->SetPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);
            g_out->SetBuffers(kI2sDmaBufferCount, kI2sDmaBufferBytes);
#if SOC_CLK_APLL_SUPPORTED
            g_out->SetUseAPLL();
#endif
            g_out->SetOutputModeMono(true);
            const float baseGain = volumePercentToGain(effectiveAudioVolumePercent(mp3Volume));
            const float clipGain = constrain(baseGain * clipGainScaleForPath(path), 0.01f, 0.95f);
            g_out->SetGain(clipGain);
            if (createdOutput && !g_out->begin())
            {
                g_lastStatus = "i2s begin failed";
                cleanupPlayback(true);
                return false;
            }
            g_wav->SetBufferSize(2048);

            if (!g_wav->begin(g_bufferedFile.get(), g_out.get()))
            {
                g_lastStatus = String("wav begin failed: ") + path;
                cleanupPlayback(true);
                return false;
            }

            g_lastStatus = String("playing: ") + path;
            g_nextClipStartMs = 0;
            g_active = true;
            g_lastWavServiceUs = micros();
            return true;
        }

        bool beginSequence()
        {
            if (!ensureSdReady())
            {
                g_lastStatus = "sd mount failed";
                clearQueue();
                return false;
            }

            if (wxv::audio::isSdMp3Active())
            {
                wxv::audio::stopSdMp3();
            }

            if (g_clipCount == 0)
            {
                g_lastStatus = "no audio data";
                return false;
            }

            g_clipIndex = 0;
            if (!startCurrentClip())
            {
                return false;
            }
            return true;
        }
    } // namespace

    bool playAudioPath(const char *path)
    {
        const unsigned long now = millis();
        if ((now - g_lastRequestMs) < kRequestDebounceMs)
        {
            return false;
        }
        g_lastRequestMs = now;

        clearQueue();
        if (!enqueueClipInternal(path == nullptr ? String() : String(path)))
        {
            g_lastStatus = "clip missing";
            clearQueue();
            return false;
        }

        return beginSequence();
    }

    bool playClip(const char *path)
    {
        return playAudioPath(path);
    }

    bool playChime(const String &chimeKey)
    {
        const String path = String(kChimeDir) + normalizeKey(chimeKey) + ".wav";
        return playAudioPath(path.c_str());
    }

    bool playAlarm(const String &alarmKey)
    {
        const String primary = String(kAlarmDir) + normalizeKey(alarmKey) + ".wav";
        const String fallback = String(kChimeDir) + normalizeKey(alarmKey) + ".wav";
        if (clipExists(primary))
            return playAudioPath(primary.c_str());
        return playAudioPath(fallback.c_str());
    }

    bool playUiSound(const String &uiKey)
    {
        UiTone tone = UI_TONE_ERROR;
        uiSoundKeyToTone(uiKey, tone);
        playUiTone(tone);
        return true;
    }

    bool playUiSound(const char *uiKey)
    {
        return playUiSound(uiKey == nullptr ? String() : String(uiKey));
    }

    bool speakTime()
    {
        const unsigned long now = millis();
        if ((now - g_lastRequestMs) < kRequestDebounceMs)
        {
            return false;
        }
        g_lastRequestMs = now;

        clearQueue();
        if (!queueTimePhrase())
        {
            clearQueue();
            return false;
        }
        return beginSequence();
    }

    bool speakTimeWithChime(const String &chimeKey)
    {
        const unsigned long now = millis();
        if ((now - g_lastRequestMs) < kRequestDebounceMs)
        {
            return false;
        }
        g_lastRequestMs = now;

        clearQueue();
        const String path = String(kChimeDir) + normalizeKey(chimeKey) + ".wav";
        enqueueOptionalClip(path);
        if (!queueTimePhrase())
        {
            clearQueue();
            return false;
        }
        return beginSequence();
    }

    bool speakDate()
    {
        const unsigned long now = millis();
        if ((now - g_lastRequestMs) < kRequestDebounceMs)
        {
            return false;
        }
        g_lastRequestMs = now;

        clearQueue();
        if (!queueDatePhrase())
        {
            clearQueue();
            return false;
        }
        return beginSequence();
    }

    bool speakIndoorTemperature()
    {
        const unsigned long now = millis();
        if ((now - g_lastRequestMs) < kRequestDebounceMs)
        {
            return false;
        }
        g_lastRequestMs = now;

        clearQueue();
        if (!queueIndoorTemperaturePhrase())
        {
            clearQueue();
            return false;
        }
        return beginSequence();
    }

    bool speakOutdoorTemperature()
    {
        const unsigned long now = millis();
        if ((now - g_lastRequestMs) < kRequestDebounceMs)
        {
            return false;
        }
        g_lastRequestMs = now;

        clearQueue();
        if (!queueOutdoorTemperaturePhrase())
        {
            clearQueue();
            return false;
        }
        return beginSequence();
    }

    bool speakIndoorHumidity()
    {
        const unsigned long now = millis();
        if ((now - g_lastRequestMs) < kRequestDebounceMs)
        {
            return false;
        }
        g_lastRequestMs = now;

        clearQueue();
        if (!queueIndoorHumidityPhrase())
        {
            clearQueue();
            return false;
        }
        return beginSequence();
    }

    bool speakOutdoorWeather()
    {
        const unsigned long now = millis();
        if ((now - g_lastRequestMs) < kRequestDebounceMs)
        {
            return false;
        }
        g_lastRequestMs = now;

        clearQueue();
        if (!queueWeatherPhrase())
        {
            clearQueue();
            return false;
        }
        return beginSequence();
    }

    bool speakWindSpeed()
    {
        const unsigned long now = millis();
        if ((now - g_lastRequestMs) < kRequestDebounceMs)
        {
            return false;
        }
        g_lastRequestMs = now;

        clearQueue();
        if (!queueWindSpeedPhrase())
        {
            clearQueue();
            return false;
        }
        return beginSequence();
    }

    bool speakWindDirection()
    {
        const unsigned long now = millis();
        if ((now - g_lastRequestMs) < kRequestDebounceMs)
        {
            return false;
        }
        g_lastRequestMs = now;

        clearQueue();
        if (!queueWindDirectionPhrase())
        {
            clearQueue();
            return false;
        }
        return beginSequence();
    }

    bool speakPressure()
    {
        const unsigned long now = millis();
        if ((now - g_lastRequestMs) < kRequestDebounceMs)
        {
            return false;
        }
        g_lastRequestMs = now;

        clearQueue();
        if (!queuePressurePhrase())
        {
            clearQueue();
            return false;
        }
        return beginSequence();
    }

    bool speakRain()
    {
        const unsigned long now = millis();
        if ((now - g_lastRequestMs) < kRequestDebounceMs)
        {
            return false;
        }
        g_lastRequestMs = now;

        clearQueue();
        if (!queueRainPhrase())
        {
            clearQueue();
            return false;
        }
        return beginSequence();
    }

    bool speakAlerts()
    {
        const unsigned long now = millis();
        if ((now - g_lastRequestMs) < kRequestDebounceMs)
        {
            return false;
        }
        g_lastRequestMs = now;

        clearQueue();
        if (!queueAlertsPhrase())
        {
            clearQueue();
            return false;
        }
        return beginSequence();
    }

    bool speakInternalAlerts()
    {
        const unsigned long now = millis();
        if ((now - g_lastRequestMs) < kRequestDebounceMs)
        {
            return false;
        }
        g_lastRequestMs = now;

        clearQueue();
        if (!queueInternalAlertsPhrase(true))
        {
            clearQueue();
            return false;
        }
        return beginSequence();
    }

    bool speakWeatherSummary()
    {
        const unsigned long now = millis();
        if ((now - g_lastRequestMs) < kRequestDebounceMs)
        {
            return false;
        }
        g_lastRequestMs = now;

        clearQueue();

        bool queuedAny = false;
        queuedAny = queueTimePhrase() || queuedAny;
        queuedAny = queueIndoorTemperaturePhrase() || queuedAny;
        queuedAny = queueOutdoorTemperaturePhrase() || queuedAny;
        queuedAny = queueIndoorHumidityPhrase() || queuedAny;
        queuedAny = queueWeatherPhrase() || queuedAny;
        queuedAny = queueWindSpeedPhrase() || queuedAny;
        queuedAny = queueWindDirectionPhrase() || queuedAny;
        queuedAny = queuePressurePhrase() || queuedAny;
        queuedAny = queueRainPhrase() || queuedAny;
        if (noaaAlertCount() > 0)
        {
            queuedAny = queueAlertsPhrase() || queuedAny;
        }
        if (hasInternalSensorAlerts())
        {
            queuedAny = queueInternalAlertsPhrase(false) || queuedAny;
        }

        if (!queuedAny || g_clipCount == 0)
        {
            clearQueue();
            return false;
        }

        return beginSequence();
    }

    void tick()
    {
        if (!g_active)
        {
            return;
        }

        if (!g_wav)
        {
            if (g_nextClipStartMs != 0 && static_cast<long>(millis() - g_nextClipStartMs) >= 0)
            {
                if (!startCurrentClip())
                {
                    restoreUiAfterAnnouncement();
                }
            }
            return;
        }

        if (!g_wav->isRunning())
        {
            advanceToNextClip();
            return;
        }

        const uint32_t burstStartUs = micros();
        const uint32_t sinceLastServiceUs =
            (g_lastWavServiceUs == 0) ? 0 : (burstStartUs - g_lastWavServiceUs);
        const bool catchupMode = sinceLastServiceUs >= kWavCatchupGapUs;
        const uint8_t burstIterations = catchupMode ? kWavCatchupBurstCount : kWavLoopBurstCount;
        const uint32_t burstBudgetUs = catchupMode ? kWavCatchupBudgetUs : kWavLoopBurstBudgetUs;
        uint8_t iterations = 0;

        while (g_active && g_wav && g_wav->isRunning())
        {
            if (!g_wav->loop())
            {
                break;
            }

            ++iterations;
            if (iterations >= burstIterations || (micros() - burstStartUs) >= burstBudgetUs)
            {
                break;
            }
        }

        g_lastWavServiceUs = micros();

        if (!g_wav || g_wav->isRunning())
        {
            return;
        }

        advanceToNextClip();
    }

    void stop()
    {
        g_lastStatus = "stopped";
        cleanupPlayback(true);
        restoreUiAfterAnnouncement();
    }

    bool isActive()
    {
        return g_active;
    }

    bool refreshOutputVolume()
    {
        if (!g_out)
        {
            return true;
        }

        String activePath;
        if (g_clipIndex < g_clipCount)
        {
            activePath = g_clipQueue[g_clipIndex];
        }

        const float baseGain = volumePercentToGain(effectiveAudioVolumePercent(mp3Volume));
        const float clipGain = constrain(baseGain * clipGainScaleForPath(activePath), 0.01f, 0.95f);
        return g_out->SetGain(clipGain);
    }

    const char *lastStatus()
    {
        return g_lastStatus.c_str();
    }
}
