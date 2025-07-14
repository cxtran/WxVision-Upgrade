#include <Arduino.h>
#include "buzzer.h"
#include "menu.h"
#include "display.h"  // your display driver with dma_display->print()


// Menu navigation logic
void handleIR(uint32_t code)
{
    switch (code)
    {
    case 0xFF02FD: // "CH+" or UP
                   //     doUpAction();
        Serial.println("Up button pressed");
        playBuzzerTone(1500, 100); // Example tone
        break;
    case 0xFF9867: // "CH-" or DOWN
                   //     enterMenu();
        Serial.println("Down button pressed");
        playBuzzerTone(1200, 100); // Example tone
        break;
    case 0xFF906F: // ">>" or RIGHT
                   //     nextMenu();
        Serial.println("Right button pressed");
        playBuzzerTone(1800, 100); // Example tone
        break;
    case 0xFFE01F: // "<<" or LEFT
                   //     prevMenu();
        Serial.println("Left button pressed");
        playBuzzerTone(900, 100); // Example tone
        break;
    case 0xFFA857: // "OK" or center button
                   // maybe refresh weather manually
        Serial.println("OK button pressed");
        playBuzzerTone(2200, 100); // Example tone
        break;
    default:
        Serial.printf("Unknown code: 0x%X\n", code);
        playBuzzerTone(500, 100); // Default tone for unknown codes
        delay(100);               // Short delay for feedback
        playBuzzerTone(500, 100); // Repeat tone
        delay(100);               // Short delay for feedback
        playBuzzerTone(0, 0);     // Repeat tone

        break;
    }
}



MenuLevel currentMenuLevel = MENU_MAIN;
int currentMenuIndex = 0;

// Example: items for main menu
const char* mainMenuItems[] = {
  "Device Settings",
  "Display Settings",
  "Weather Settings",
  "Calibration",
  "System Actions",
  "Save & Exit"
};

const int mainMenuCount = sizeof(mainMenuItems) / sizeof(mainMenuItems[0]);

// Placeholder function to show menu on screen
void drawMenu() {
  dma_display->clearScreen();
  dma_display->setCursor(0,0);
  dma_display->setTextColor(dma_display->color565(255,255,255));

  if (currentMenuLevel == MENU_MAIN) {
    for (int i=0; i < mainMenuCount; i++) {
      if (i == currentMenuIndex) dma_display->setTextColor(dma_display->color565(255,0,0));
      else dma_display->setTextColor(dma_display->color565(255,255,255));

      dma_display->setCursor(0, i*8);
      dma_display->print(mainMenuItems[i]);
    }
  }

  // Repeat for other menu levels...
}

// Core menu update loop
void updateMenu() {
  // This function will be called in your main loop
  // Here, you check buttons & IR, and call handleUp/Down/Select
  drawMenu();
}

// Navigation functions
void handleUp() {
  currentMenuIndex--;
  if (currentMenuIndex < 0) currentMenuIndex = mainMenuCount-1;
  drawMenu();
}

void handleDown() {
  currentMenuIndex++;
  if (currentMenuIndex >= mainMenuCount) currentMenuIndex = 0;
  drawMenu();
}

void handleSelect() {
  if (currentMenuLevel == MENU_MAIN) {
    switch(currentMenuIndex) {
      case 0: currentMenuLevel = MENU_DEVICE; currentMenuIndex = 0; break;
      case 1: currentMenuLevel = MENU_DISPLAY; currentMenuIndex = 0; break;
      case 2: currentMenuLevel = MENU_WEATHER; currentMenuIndex = 0; break;
      case 3: currentMenuLevel = MENU_CALIBRATION; currentMenuIndex = 0; break;
      case 4: currentMenuLevel = MENU_SYSTEM; currentMenuIndex = 0; break;
      case 5: /* save settings here */ currentMenuLevel = MENU_MAIN; break;
    }
  }
  drawMenu();
}

