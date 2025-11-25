# VisionWX

VisionWX is a fully custom ESP32 firmware that drives a 64×32 RGB LED panel to render a rich weather and device-dashboard experience. The project integrates sensor readings, remote control input, Wi‑Fi management, and an on-device configuration UI that uses modal dialogs rendered directly on the panel.

## Features

- **Weather & Sensor Display** – Shows live weather data, environmental sensor readings (CO₂, temperature, humidity, pressure) and device status information using animated scenes tailored for day and night themes.
- **On-Device Menu System** – Navigate settings such as Wi‑Fi credentials, units, calibration, system info, and display preferences via IR remote control. Menus use the shared `InfoModal` component to handle chooser, number, text, and button fields.
- **Theme Modes (Manual / Scheduled / Light Sensor)** – Choose Day/Night manually, set schedule start times, or let the light sensor pick Day/Night using a raw lux threshold (live lux is shown while editing). Theme switches can also be triggered instantly using the IR `Theme` button and the on-device display menu now hides fields not relevant to the selected mode.
- **Wi-Fi & OTA Friendly** – Manage multiple Wi-Fi networks, perform scans, and expose a web UI (`data/config.html`) for configuring every device parameter through REST endpoints defined in `src/web.cpp`.
- **Sensor Integration** – Reads SCD40, AHT20, BMP280, and other sensors (see `src/sensors.cpp`) for accurate environmental monitoring, adjusting display brightness automatically when desired.
- **Extensible Rendering** – The display pipeline (e.g., `src/display.cpp`, `src/InfoScreen.cpp`) uses custom drawing utilities to render gradients, icons, scrolling text, and button bars optimized for small RGB matrices.
- **Alarm Clock** – Built-in alarm can be toggled on/off, set to repeat (daily, weekly, weekdays, weekend, or one-shot), and edited from the main menu. Settings persist across reboots, support both 12 h and 24 h formats (with AM/PM marquee feedback in 12 h mode), and trigger a flashing clock indication when active.
- **NOAA Alerts** – Optional screen that queries the U.S. National Weather Service API using your configured latitude/longitude to display the latest alert event, severity, and a scrolling description/instruction string. A dedicated menu lets you enable the feature and store coordinates, with data preserved across restarts.
- **Lunar Date Screen** – Dedicated screen that shows Vietnamese lunar information with:
  - Line 1: daily Can Chi name (e.g. `Quy Mao`).
  - Line 2: solar term (e.g. `Lap Dong`), derived from the current Gregorian date.
  - Line 3: a combined marquee with compact Vietnamese date, English year name, and local time, for example:  
    `Ngay 28/10 Nam At Ty ¦ The year of Wood Snake ¦ Gio Hoi  / 11:24 PM`.  
    The 12/24‑hour format and marquee speed both respect the global settings configured in the main UI.

## Project Layout

- `src/` – Main firmware sources (display rendering, menu system, sensor drivers, networking, etc.).
- `include/` – Shared headers for configuration, IR codes, settings, and utilities.
- `data/` – SPIFFS assets such as the configuration web page (`config.html`) and accompanying scripts.
- `lib/` / `.pio/` – PlatformIO-managed dependencies (Adafruit GFX, IRremoteESP8266, AsyncWebServer, etc.).
- `platformio.ini` – PlatformIO environment definitions.

## Getting Started

1. Install [PlatformIO](https://platformio.org/) and clone this repository.
2. Connect an ESP32 driving a compatible RGB LED matrix (default is 64×32) plus the supported sensors.
3. Configure your build environment if needed (see `platformio.ini`).
4. Run `pio run --target uploadfs` to flash SPIFFS assets, then `pio run --target upload` to flash the firmware.
5. After boot, use the configured IR remote or the web UI (browse to the device’s IP) to set up Wi‑Fi and preferences.

## Contributing

Issues and pull requests are welcome. Please format code using the existing style (clang-format/PlatformIO defaults), avoid committing auto-generated files, and ensure features are testable via the on-device UI or web interface.

## License

VisionWX is released under the MIT License. See `LICENSE` (add one if missing) for details.
