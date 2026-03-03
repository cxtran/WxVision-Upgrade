# WxVision Architecture

## Purpose
This document defines module boundaries for `VisionWX` so new changes are predictable, testable, and easier to maintain.

## Core Principles
- Single source of truth for app settings/state.
- UI modules render normalized data, not provider-specific parsing logic.
- Data providers fetch/parse data and expose normalized snapshots.
- Managers coordinate runtime flow; feature modules do not own the main loop.

## State Ownership
- Global settings and runtime config:
  - `settings.cpp` / `include/settings.h`
- Aggregated app references:
  - `app_state.cpp` / `include/app_state.h`
- Provider-normalized weather snapshots:
  - `weather_provider.cpp` / `include/weather_provider.h`
- Display runtime strings and time fields:
  - `display.cpp` + `include/display_runtime.h`

Rule:
- Do not duplicate persisted configuration in feature modules.
- If many modules need the same setting, expose through `settings.h` (or `AppState` aliasing), not ad-hoc globals.

## Module Map
- Bootstrap and top-level loop glue:
  - `main.cpp`
- Runtime managers:
  - `input_manager.cpp`
  - `screen_manager.cpp`
  - `render_scheduler.cpp`
- Menu feature modules:
  - `menu_*.cpp` files
- Display feature modules:
  - `display_*.cpp` files
- Weather/data provider abstraction:
  - `weather_provider.cpp`
- Concrete weather transport/parser logic:
  - `tempest.cpp` (WeatherFlow/Open-Meteo model path)
  - `display.cpp` OWM fetch path (`fetchWeatherFromOWM`)
- NOAA alerts domain:
  - `noaa.cpp`, `display_noaa.cpp`
- Web API/UI backend:
  - `web.cpp`

## Allowed Dependencies (Boundary Contract)
- `main.cpp` may call managers + initialization services.
- Managers may call:
  - menu/display facade functions
  - provider facade (`weather_provider.h`)
  - settings/runtime state
- Menu modules may call:
  - settings save/load and provider facade
  - display refresh helpers (`requestScrollRebuild`, etc.)
- Display modules may read:
  - normalized provider snapshots and display runtime state
  - settings/theme constants
- Provider facade may call concrete provider fetch functions.
- `web.cpp` may call settings/services/provider facade, but should not embed provider-specific parsing.

Not allowed:
- Display modules adding new network fetch/parsing logic.
- Menu/web modules directly depending on provider internals when facade APIs exist.
- New long-lived state variables hidden in random feature files.

## Data Provider Layer
Entry point:
- `include/weather_provider.h`

Current provider IDs:
- `None`
- `Owm`
- `WeatherFlow`
- `OpenMeteo`

Facade APIs:
- `activeProvider()`
- `fetchActiveProviderData()`
- `readActiveProviderSnapshot()`
- `sourceUsesUdpMulticast()`
- `sourceIsForecastModel()`

Rule:
- New code should call provider facade functions instead of branching on `isDataSource*()` + direct fetch calls.

## UI Theme/Layout
- Shared visual constants live in:
  - `include/ui_theme.h`
  - `src/ui_theme.cpp`

Rule:
- New colors/spacing for reusable UI patterns must be added in `ui_theme`, not hardcoded repeatedly.

## How To Add a New Screen
1. Create `src/display_<feature>.cpp` (+ optional header in `include/`).
2. Keep rendering logic and per-screen state local to that module.
3. Register draw/tick behavior through `screen_manager.cpp` / routing points.
4. Read data via normalized models/provider snapshot where possible.
5. Avoid adding network code in display module.

## How To Add a New Weather Provider
1. Add provider ID to `WeatherProviderId` in `weather_provider.h`.
2. Implement `IWeatherProvider` adapter in `weather_provider.cpp` (or dedicated provider file if large).
3. Map `dataSource -> provider` in `providerIdFromDataSource()`.
4. Implement `fetch()` + `snapshot()` so UI can consume normalized data.
5. Update settings/menu/web options for provider selection (if user-facing).

## Practical Review Checklist (Before Merge)
- Does this change introduce new global state? If yes, is ownership clear?
- Did any module cross a boundary (UI parsing provider payload, etc.)?
- Are provider calls using `weather_provider` facade?
- Are reusable colors/layout values in `ui_theme`?
- Does `platformio run` pass?

## Current Known Debt
- Some legacy provider-specific globals remain and are incrementally being wrapped by provider snapshot usage.
- Some code paths still branch by `isDataSource*()` directly; preferred direction is provider facade first.
