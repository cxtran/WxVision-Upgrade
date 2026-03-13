#include <Arduino.h>
#include <math.h>

#include "display.h"
#include "display_condition.h"
#include "display_runtime.h"
#include "settings.h"
#include "units.h"
#include "tempest.h"
#include "ScrollLine.h"
#include "display_marquee_config.h"

namespace
{
    String conditionSceneMarqueeBase = "";
    String conditionSceneMarqueeText = "";
    String conditionSceneMarqueePendingText = "";
    uint16_t conditionSceneMarqueeColor = 0;
    ScrollLine conditionSceneScroll(PANEL_RES_X, kConditionMarqueeDefaultSpeedMs);

    String formatConditionSceneTimeTagLocal()
    {
        char buf[12];
        if (units.clock24h)
        {
            snprintf(buf, sizeof(buf), "%02d:%02d", t_hour, t_minute);
            return String(buf);
        }

        int hour = t_hour;
        const char *suffix = "AM";
        if (hour >= 12)
        {
            suffix = "PM";
            if (hour > 12)
                hour -= 12;
        }
        else if (hour == 0)
        {
            hour = 12;
        }

        snprintf(buf, sizeof(buf), "%02d:%02d %s", hour, t_minute, suffix);
        return String(buf);
    }

    String formatOutdoorHumidityForMarquee()
    {
        if (isDataSourceNone())
            return String("--");

        if (isDataSourceForecastModel())
        {
            if (!isnan(tempest.humidity))
                return String((int)(tempest.humidity + 0.5f));
        }

        if (str_Humd.length() > 0)
            return str_Humd;

        if (!isnan(tempest.humidity))
            return String((int)(tempest.humidity + 0.5f));

        return String("--");
    }

    String buildConditionMarqueeText(const String &label)
    {
        String combined = label;

        auto appendField = [&](const String &field) {
            if (field.length() == 0)
                return;
            if (combined.length() > 0)
                combined += kConditionMarqueeSeparator;
            combined += field;
        };

        if (!sceneClockEnabled)
            appendField(formatConditionSceneTimeTagLocal());

        bool feelsAppended = false;

        if (isDataSourceForecastModel())
        {
            if (currentCond.humidity >= 0)
                appendField(String("Hum ") + currentCond.humidity + "%");
            else if (!isnan(tempest.humidity))
                appendField(String("Hum ") + String((int)roundf(tempest.humidity)) + "%");

            double press = !isnan(currentCond.pressure) ? currentCond.pressure : tempest.pressure;
            if (!isnan(press))
                appendField(String("Press ") + fmtPress(press, 0));

            double wind = !isnan(currentCond.windAvg) ? currentCond.windAvg : tempest.windAvg;
            if (!isnan(wind))
                appendField(String("Wind ") + fmtWind(wind, 1));

            double feels = !isnan(currentCond.feelsLike) ? currentCond.feelsLike : tempest.temperature;
            if (!isnan(feels))
            {
                appendField(String("Feels ") + fmtTemp(feels, 0));
                feelsAppended = true;
            }
        }
        else if (isDataSourceOwm())
        {
            String hum = formatOutdoorHumidityForMarquee();
            if (hum.length() > 0 && hum != "--")
                appendField(String("Hum ") + hum + "%");

            if (str_Pressure.length() > 0 && str_Pressure != "--")
                appendField(String("Press ") + fmtPress(atof(str_Pressure.c_str()), 0));

            if (str_Wind_Speed.length() > 0 && str_Wind_Speed != "--")
                appendField(String("Wind ") + fmtWind(atof(str_Wind_Speed.c_str()), 1));

            if (str_Feels_like.length() > 0 && str_Feels_like != "--")
            {
                appendField(String("Feels ") + fmtTemp(atof(str_Feels_like.c_str()), 0));
                feelsAppended = true;
            }
            else if (str_Temp.length() > 0 && str_Temp != "--")
            {
                appendField(String("Feels ") + fmtTemp(atof(str_Temp.c_str()), 0));
                feelsAppended = true;
            }
        }

        if (!feelsAppended)
        {
            double tempVal = NAN;
            if (isDataSourceForecastModel())
                tempVal = !isnan(currentCond.temp) ? currentCond.temp : tempest.temperature;
            else if (isDataSourceOwm() && str_Temp.length() > 0 && str_Temp != "--")
                tempVal = atof(str_Temp.c_str());

            if (!isnan(tempVal))
                appendField(String("Feels ") + fmtTemp(tempVal, 0));
        }

        return combined;
    }

    void renderConditionSceneMarquee()
    {
        if (conditionSceneMarqueeText.length() == 0)
            return;

        dma_display->setFont(&Font5x7Uts);
        dma_display->setTextSize(1);
        const unsigned int sceneScrollMs = normalizeConditionMarqueeSpeedMs(scrollSpeed);

        if (conditionSceneMarqueePendingText.length() > 0)
        {
            String lines[] = {conditionSceneMarqueePendingText};
            conditionSceneMarqueePendingText = "";
            conditionSceneMarqueeText = lines[0];

            conditionSceneScroll.setLines(lines, 1, false);
            uint16_t textColors[] = {conditionSceneMarqueeColor};
            uint16_t bgColors[] = {myBLACK};
            conditionSceneScroll.setLineColors(textColors, bgColors, 1);
            conditionSceneScroll.setScrollSpeed(sceneScrollMs);
            conditionSceneScroll.setScrollStepPx(kConditionMarqueeStepPx);
        }
        else
        {
            conditionSceneScroll.setScrollSpeed(sceneScrollMs);
        }

        const int marqueeY = PANEL_RES_Y - 7;
        conditionSceneScroll.update();
        conditionSceneScroll.draw(0, marqueeY, conditionSceneMarqueeColor);
    }
}

void conditionSceneSyncMarquee(const String &label, uint16_t accent)
{
    if (label != conditionSceneMarqueeBase || conditionSceneMarqueeText.length() == 0)
    {
        conditionSceneMarqueeBase = label;
        conditionSceneMarqueeColor = accent;
        conditionSceneMarqueeText = buildConditionMarqueeText(conditionSceneMarqueeBase);
        String lines[] = {conditionSceneMarqueeText};
        conditionSceneScroll.setLines(lines, 1, true);
        uint16_t textColors[] = {conditionSceneMarqueeColor};
        uint16_t bgColors[] = {myBLACK};
        conditionSceneScroll.setLineColors(textColors, bgColors, 1);
        conditionSceneScroll.setScrollSpeed(normalizeConditionMarqueeSpeedMs(scrollSpeed));
        conditionSceneScroll.setScrollStepPx(kConditionMarqueeStepPx);
        conditionSceneMarqueePendingText = "";
    }
    else
    {
        conditionSceneMarqueeColor = accent;
    }

    renderConditionSceneMarquee();
}

void tickConditionSceneMarquee()
{
    if (currentScreen != SCREEN_CONDITION_SCENE)
        return;

    if (conditionSceneMarqueeBase.length() == 0)
        return;

    String combined = buildConditionMarqueeText(conditionSceneMarqueeBase);
    if (combined != conditionSceneMarqueeText && combined != conditionSceneMarqueePendingText)
        conditionSceneMarqueePendingText = combined;

    renderConditionSceneMarquee();
}
