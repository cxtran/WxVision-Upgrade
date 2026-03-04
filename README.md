# VisionWX

VisionWX is ESP32 firmware for a 64x32 HUB75 RGB matrix weather display with on-device menus, IR control, sensors, and a web configuration UI.

## Features

- Multiple weather providers: OWM, WeatherFlow, Open-Meteo, or None
- On-device modal menu system (IR + physical buttons)
- Web UI for full settings/configuration (`data/config.html`)
- Indoor environmental data (SCD40/AHT20/BMP280)
- Weather/forecast/current condition screens
- Lunar section with Vietnamese text support
- OTA, mDNS, Wi-Fi scanning/reconnect, AP fallback

## Recent Changes

- Fixed OWM startup bootstrap so first temperature is fetched reliably after boot/connect.
- Fixed deterministic stale pixels on `SCREEN_OWM` by improving lane/gap clearing and stable font state.
- Updated OWM humidity position handling and clipping behavior.
- Added section heading behavior updates:
  - Lunar Date + Lunar Luck treated as one lunar section.
  - Lunar Luck is now a subpage (inside lunar section), not a top-level rotation page.
- Reordered screen rotation so Weather Scene appears before Outdoor Conditions.
- Updated Lunar Date subpage layout:
  - Removed line 1 and line 2.
  - Uses large UTF-8 font for the remaining line.
  - Vertically centered rendering.
  - Vietnamese accents added (`Ngay/Nam` -> `Ngay/Nam` with accents in UI text data).
  - Separator between Vietnamese and English phrase changed to `*`.
- Updated Current WX source age text from compact format (`4m`) to full text (`4 minutes ago`).
- System reset behavior is now split clearly:
  - `Reset Settings`: resets preferences but keeps Wi-Fi credentials and logs.
  - `Factory Reset (Erase Wi-Fi + Logs)`: clears preferences and removes `/sensor_log.bin`.
- Updated System menu labels to reduce marquee jump in modal rows (`Units`, `Learn Remote`).
- Web UI updated to match:
  - Reset button labels aligned with firmware behavior.
  - Remote learning actions use `Learn Remote` / `Clear Learned Remote`.
  - Confirmation prompts added for reset actions.

## Project Layout

- `src/` firmware source
- `include/` shared headers
- `data/` SPIFFS web assets
- `platformio.ini` build/upload config

## Build and Flash

1. Build firmware:
   - `pio run -e esp32dev`
2. Upload firmware:
   - `pio run -e esp32dev -t upload`
3. Upload web assets (required after `data/` changes):
   - `pio run -e esp32dev -t uploadfs`

## Notes

- Factory reset does not erase firmware; it clears persisted settings/data and reboots.
- SPIFFS web assets must be re-uploaded after editing files in `data/`.
