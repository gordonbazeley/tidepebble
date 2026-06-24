# TidePebble — Current State

## What works

- Full 4-page watch UI: Overview (24h chart), NowNext, Then, Later.
- Phone fetches 24h of hourly `sea_level_height_msl` + `swell_wave_height` + `sea_surface_temperature` from Open-Meteo.
- Data encoded, chunked, and sent to watch reliably.
- Tide event detection (next up to 4 high/low turning points after now).
- NOW card: current tide height, rising/falling arrow, swell height (scalar), sea temp.
- Event cards (NEXT/THEN/LATER): time-to-tide, tide height, countdown.
- Overview chart: 24h line, current position dot, high/low arrows with time labels.
- Settings: GPS mode (phone location) or manual location search via Open-Meteo geocoding.
- Unit system follows Pebble's own metric/imperial preference.
- 12h/24h clock follows Pebble's own clock preference.
- Store assets present: banner (720×320), icons (48, 80, 144px), screenshots.
- Target platform: `emery` only. Builds via `pebble build`.

## Known gaps / dead code

- **`s_swell_values` is populated but never rendered.** Per-hour swell data arrives and is parsed into the array, but no draw function reads it. The swell chart was backed out at `e8271bf`. Either delete the swell parse path or wire up a chart.
- **Double-tap handler does nothing.** `prv_tap_handler` detects a double-tap within 500ms but the second tap only calls `light_enable_interaction()` and clears the timer — no page action. Likely a WIP navigation gesture.
- **`tide_times` is an unused message key.** Declared in `package.json`, but neither `src/pkjs/index.js` nor `src/c/tidepebble.c` reads or writes it.
- **`settings-html.js` is generated copy and appears out of sync.** `src/pkjs/index.js` requires `settings-html.js`, while `settings.html` is the human-editable source. Update both or regenerate the JS wrapper after settings UI changes.
- **No mid-sequence chunk recovery.** If a chunk fails mid-transfer, `sendStatus('Tide data unavailable')` fires but the watch may hold partial stale data from the previous session alongside new partial data (offset > 0 but count never updated).
- **Newquay fallback fires for all non-manual users.** If the user is mid-GPS-resolve, the watch briefly shows Newquay tide data. Not wrong, just potentially confusing for non-UK users.

## Build / toolchain state

- Built with Pebble SDK 3, `enableMultiJS: true`.
- `pebble build` produces `build/TidePebble.pbw`.
- Emulator target: `pebble install --emulator emery`.
- Phone companion syntax: `node --check src/pkjs/index.js`.
- No automated test suite.

## File inventory

| File | Role |
|------|------|
| `src/c/tidepebble.c` | Entire watch app (~757 lines) |
| `src/pkjs/index.js` | Phone companion: GPS, fetch, encode, send (~290 lines) |
| `src/pkjs/settings.html` | Settings UI (data-URI page, ~415 lines) |
| `src/pkjs/settings-html.js` | CommonJS string loaded by pkjs; generated copy of settings.html |
| `src/open_config.js` | Dev helper: opens settings in browser with URL params |
| `package.json` | Pebble metadata, message keys |
| `wscript` | SDK build rules |
| `store_assets/` | App store images (banner, icons, screenshots) |
| `resources/images/` | Watch-side image resources (menu icon) |
