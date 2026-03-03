#include <Arduino.h>
#include "pins.h"
#include "utils.h"

void triggerPhysicalReset();

void handleResetButton()
{
    static bool buttonWasDown = false;
    static unsigned long buttonDownMillis = 0;
    static bool resetLongPressHandled = false;
    static bool resetArmReady = false;
    static unsigned long resetGuardUntilMs = 0;
    const unsigned long resetHoldTime = 3000;
    const unsigned long resetStartupGuardMs = 5000;
    const unsigned long releaseArmMs = 300;
    unsigned long now = millis();

    if (resetGuardUntilMs == 0)
    {
        // Ignore reset key early after boot to avoid startup pin transients/reset loops.
        resetGuardUntilMs = now + resetStartupGuardMs;
    }
    if (now < resetGuardUntilMs)
    {
        buttonWasDown = false;
        resetLongPressHandled = false;
        return;
    }

    bool buttonDown = (digitalRead(BTN_SEL) == LOW);
    static unsigned long releasedSinceMs = 0;

    // Arm long-press reset only after button has been released stably.
    if (!resetArmReady)
    {
        if (!buttonDown)
        {
            if (releasedSinceMs == 0)
                releasedSinceMs = now;
            if ((now - releasedSinceMs) >= releaseArmMs)
                resetArmReady = true;
        }
        else
        {
            releasedSinceMs = 0;
        }
        buttonWasDown = buttonDown;
        return;
    }

    if (buttonDown && !buttonWasDown)
    {
        buttonDownMillis = now;
        resetLongPressHandled = false;
        buttonWasDown = true;
    }
    if (buttonDown && !resetLongPressHandled)
    {
        if (now - buttonDownMillis > resetHoldTime)
        {
            resetLongPressHandled = true;
            triggerPhysicalReset();
        }
    }
    if (!buttonDown && buttonWasDown)
    {
        buttonWasDown = false;
        resetLongPressHandled = false;
    }
}
