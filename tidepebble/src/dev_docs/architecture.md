# TidePebble — Architecture

## Overview

Two-layer system: phone JS companion + watch C app. Phone owns all network, GPS, and encoding. Watch owns all rendering.

```
Open-Meteo Marine API
        │  sea_level_height_msl
        │  swell_wave_height
        │  sea_surface_temperature
        ▼
Phone (pkjs/index.js)
  ├── GPS / manual location
  ├── Reverse geocoding (BigDataCloud)
  ├── 12-bit custom encoding → 2-char pairs
  ├── Chunked AppMessage (12 samples/chunk, 150ms delay)
        │
        ▼
Watch (c/tidepebble.c)
  ├── Decode packed values
  ├── Compute tide events (local min/max scan)
  └── Render 4 pages
```

## Phone Companion (`pkjs/index.js`)

### Location resolution
1. Load `s_selectedLocation` from `localStorage` (key `tide_selected_location_v1`).
2. If set → use stored lat/lon directly, skip GPS.
3. If not → start with Newquay fallback (50.4155, -5.0737), fire GPS in parallel, replace when resolved.
4. GPS reverse-geocoded via BigDataCloud → city/locality label.

### Data fetch
- API: `https://marine-api.open-meteo.com/v1/marine`
- Fields: `sea_level_height_msl`, `swell_wave_height`, `sea_surface_temperature`
- `forecast_days=2`, `timezone=auto`
- Start index: first hour ≥ now, minus 1 (one hour of history for context).
- `currentMinutes`: elapsed minutes from start sample to now.

### Encoding
12-bit signed values, offset +2048, split into two 6-bit chars from alphabet `A–Z a–z 0–9 - _` (64 chars). Range: ±2047 cm. Tide heights in cm, swell heights in cm.

```js
encoded = value + 2048;          // shift to unsigned
high = (encoded >> 6) & 63;      // top 6 bits
low  = encoded & 63;             // bottom 6 bits
output = ALPHABET[high] + ALPHABET[low];
```

### Chunked send
`TIDE_CHUNK_SIZE = 12`. Two chunk arrays sent in lock-step (`tide_values` + `tide_wave_values`). 150ms between chunks to avoid AppMessage queue overflow. First message always carries scalar metadata: `tide_location`, `tide_status`, `tide_current_minutes`, `tide_wave_height` (current hour cm), `tide_sea_temp` (current hour tenths-°C).

## Watch App (`c/tidepebble.c`)

### State
- `s_tide_values[24]` — hourly tide heights in cm
- `s_swell_values[24]` — hourly swell heights in cm (received and parsed; currently not rendered as chart)
- `s_current_minutes` — minutes since `s_tide_values[0]`
- `s_wave_height` — scalar swell height for NOW card (cm)
- `s_sea_temp` — sea surface temperature (tenths °C)
- `s_event_indices[4]` / `s_event_highs[4]` — next 4 tide turning points after now

### State computation (`prv_compute_state`)
Interpolates current tide value linearly between hourly samples. Detects tide events via simple peak/trough scan (local max/min, strictly past `current_minutes`). Up to 4 events captured.

### Pages (`TidePage` enum)
| Page | Index | Content |
|------|-------|---------|
| Overview | 0 | 24h tide chart with event labels |
| NowNext | 1 | NOW card + NEXT event card (default) |
| Then | 2 | THEN event card (large) |
| Later | 3 | LATER event card (large) |

Navigation: Up/Down buttons cycle pages. Select returns to NowNext.

### NOW card
Three values on one row: sea temp · wave-icon + swell height · current tide height. Rising/falling arrow + label in header.

### Chart (`prv_draw_chart`)
- Blue line (3px thick via ±1 offset trick — no stroke-width API).
- Cyan dot with teal halo = current position.
- Green/orange arrows at tide turning points.
- Optional high/low time labels above/below plot area.

### Units
Reads Pebble's own measurement system preference (`HealthMetricWalkedDistanceMeters`). Metric: `m` with one decimal. Imperial: `'` with one decimal (converts via ×328084/1000000). Falls back to metric if health service unavailable.

### Tap handler
`accel_tap_service` subscribed. Detects double-tap within 500ms window. Currently only calls `light_enable_interaction()` — no page action wired up.

## Settings UI (`pkjs/settings.html`)

Single-page HTML injected via `Pebble.openURL` as a data URI. State injected as a JS literal (`/*STATE_INIT*/` placeholder replaced in `index.js`). Two modes:

- **GPS** — clears stored location, uses phone GPS on next refresh.
- **Manual** — searches Open-Meteo geocoding API, stores selected lat/lon + name in `localStorage`.

Save sends state back via `pebblejs://close#<JSON>` URL fragment. `index.js` accepts the current `{mode, location, lat, lon}` format and a legacy `{latitude, longitude, name, admin1, country}` format.

`settings.html` is the editable source. `settings-html.js` is the bundled CommonJS string loaded by pkjs; regenerate or update it after changing the HTML.

### Refresh triggers
- `ready` refreshes on app start.
- Any watch `appmessage` refreshes from the companion side, although the watch currently never sends one.
- `webviewclosed` refreshes after settings are saved.

### Message keys
Active keys: `tide_location`, `tide_status`, `tide_current_minutes`, `tide_sample_offset`, `tide_values`, `tide_wave_height`, `tide_sea_temp`, `tide_wave_values`.

`tide_times` is still declared in `package.json` but is unused by both pkjs and C.

## Build

- SDK: Pebble SDK 3, `enableMultiJS: true`
- Target platform: `emery` only
- `pebble build` → `pebble install --emulator emery`
- Message inbox size: 512 bytes; outbox: 128 bytes
