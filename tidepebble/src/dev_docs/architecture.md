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
3. If not → call `navigator.geolocation.getCurrentPosition`, showing a status message ("Finding phone location...") while it resolves. No fallback location/data is shown while waiting (the old Newquay-fallback data-first approach was removed — see `decisions.md`).
4. GPS reverse-geocoded via BigDataCloud → city/locality label.
5. If geolocation is unavailable or times out, show "Location unavailable - open settings" / "No location set - open settings" rather than any tide data.

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
- `s_tide_values[30]` — hourly tide heights in cm; only `s_tide_values[s_chart_start..]` (~23h) is charted, the rest is lookback-only (see below)
- `s_swell_values[30]` — hourly swell heights in cm (received and parsed; currently not rendered as chart)
- `s_current_minutes` — minutes since `s_tide_values[0]`
- `s_wave_height` — scalar swell height for NOW card (cm)
- `s_sea_temp` — sea surface temperature (tenths °C)
- `s_event_indices[4]` / `s_event_highs[4]` — next 4 tide turning points after now

### State computation (`prv_compute_state`)
Interpolates current tide value linearly between hourly samples. Detects tide events via simple peak/trough scan (local max/min, strictly past `current_minutes`). Up to 4 events captured.

Also derives `s_local_min`/`s_local_max` (the tide range either side of "now", bounded by the nearest confirmed turning points, or by the most extreme observed sample when a turning point isn't found within the fetched window) and `s_rising`. These feed the App Glance fill-percentage below. **Important:** if `s_local_min == s_local_max` (degenerate range — e.g. right after a resync when there's no confirmed previous turning point yet), it falls back to `prv_tide_min_max()` (min/max over the *entire* fetched series) rather than dividing by a zero/near-zero range. This guard was accidentally dropped once already during a turning-point-detection rework and caused the App Glance to show a stuck "0%" — keep it whenever touching this function.

**Constant coupling:** `TIDE_POINT_COUNT` (here, `30`) and `HOURS_TO_SEND` (`pkjs/index.js`, and duplicated again in `open_config.js`) must stay equal — commented cross-referencing each other. This 30 splits into `LOOKBACK_HOURS = 7` (past, hidden from the chart, see below) + 23 forward (visible). If either constant changes, keep that split in mind: the Overview page header (`"NEXT 24H"`, hardcoded in `prv_draw_overview_page`) and the event-label spacing in `prv_draw_chart` (fixed 56px-wide labels, no collision handling) were both tuned for a ~23-point *visible* window — widening the *visible forward* portion without updating both regresses to overlapping high/low time labels on screen. Widening `LOOKBACK_HOURS` alone doesn't affect this, since the chart only plots from `s_chart_start` onward (below).

`LOOKBACK_HOURS` (`pkjs/index.js`, duplicated in `open_config.js`; no C-side equivalent by that name — see `s_chart_start` below) is currently `7`. Do not shrink it back toward `1`: with too little real history in the fetched window, `prv_compute_state`'s backward turning-point search usually finds nothing, and the fallback bound-scan collapses `s_local_min`/`s_local_max` to nearly `s_current_value` — breaking both the App Glance percentage and the beachometer gauge (see their sections below) without visibly touching the chart, so it's easy to miss in testing.

**`s_chart_start`:** `prv_compute_state` sets this to the current-hour array index every update. `prv_draw_chart` (and `prv_current_sample_x`, and the min/max used for the chart's Y-axis, `prv_tide_min_max_range`) only look at `s_tide_values[s_chart_start..]` — the `LOOKBACK_HOURS` of history before it are fetched and kept in the array (so `prv_compute_state` can use them) but never drawn. The chart still shows the same ~23h forward window and pixel density as before `LOOKBACK_HOURS` existed; only the *invisible* portion of the array grew. The degenerate-range fallback (`prv_tide_min_max`, no `_range` suffix, always starts at index `0`) is the one place that intentionally still looks at the full array including the hidden lookback hours.

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
- Optional high/low time labels above/below plot area, fixed 56px wide, no overlap/collision handling — see the constant-coupling note above.

### Beachometer / tide bar (`prv_draw_tide_bar`, `prv_draw_tide_bar_for_content`)
Vertical fill gauge next to the Overview chart — 6 cells, `sea_cells` of them (top-down: beach fixed at the bottom of view, sea at the horizon/top, matching how a real beach looks) rendered as sea, the rest as beach below it, where `sea_cells` is how full `s_local_min..s_local_max` currently is (the *same* range the App Glance percentage uses, see above). It is a snapshot of "now", not a forecast strip. The boundary arrow's glyph direction (up/down) is the *inverse* of `s_rising` — it follows the boundary's actual on-screen travel direction (down during a rise, since sea fills down from the top), not the abstract "getting fuller" flag; only its color follows `s_rising` directly. See `decisions.md` → "Beachometer (tide bar) design" for the full reasoning and a real bug this once caused (all-beach shortly after a low, from too little lookback).

### App Glance (`prv_glance_reload_callback`, `prv_update_glance`)
Populates the pin/launcher subtitle (e.g. "Tide 40% in • 14°") via `app_glance_add_slice`. Fill percentage is `(s_current_value - s_local_min) * 100 / (s_local_max - s_local_min)`, flipped to `100 - fill` when falling, then rounded **up** to the nearest 10% (not nearest — a `fill_percent` of 1-9% displays as 10%, ceiling not round). Shows "Tide info out of date" if `s_is_stale` or no data yet. `prv_update_glance()` is called from `prv_set_text()`, i.e. on every redraw — relies on `s_local_min`/`s_local_max` from `prv_compute_state` never being degenerate (see above).

### Units
Reads Pebble's own measurement system preference (`HealthMetricWalkedDistanceMeters`). Metric: `m` with one decimal. Imperial: `'` with one decimal (converts via ×328084/1000000). Falls back to metric if health service unavailable.

### Tap / touch handlers
- `accel_tap_service` (`prv_tap_handler`): detects a double-tap within 500ms, but still only calls `light_enable_interaction()` — no page action wired up (legacy, rect platforms).
- `touch_service` (`prv_touch_handler`, gabbro/round touchscreen only): maps a touchdown's Y position to the same page navigation as the Up/Down/Select buttons — top third = up, bottom third = down, middle = select.

### Refresh request
`prv_send_refresh_request()` sends a `tide_refresh_request` AppMessage to the phone on window load, which triggers `refresh()` in `pkjs/index.js`. (Superseded the old "watch never sends one" behavior.)

## Settings UI (`pkjs/settings.html`)

Single-page HTML injected via `Pebble.openURL` as a data URI. State injected as a JS literal (`/*STATE_INIT*/` placeholder replaced in `index.js`). Two modes:

- **GPS** — clears stored location, uses phone GPS on next refresh.
- **Manual** — searches Open-Meteo geocoding API, stores selected lat/lon + name in `localStorage`.

Save sends state back via `pebblejs://close#<JSON>` URL fragment. `index.js` accepts the current `{mode, location, lat, lon}` format and a legacy `{latitude, longitude, name, admin1, country}` format.

`settings.html` is the editable source. `settings-html.js` is the bundled CommonJS string loaded by pkjs; regenerate or update it after changing the HTML.

### Refresh triggers
- `ready` refreshes on app start.
- Any watch `appmessage` refreshes from the companion side — including the watch's own `tide_refresh_request` sent from `prv_send_refresh_request()` on window load (see watch app section above).
- `webviewclosed` refreshes after settings are saved.

### Message keys
Active keys: `tide_location`, `tide_status`, `tide_current_minutes`, `tide_sample_offset`, `tide_values`, `tide_wave_height`, `tide_sea_temp`, `tide_wave_values`.

`tide_times` is still declared in `package.json` but is unused by both pkjs and C.

## Build

- SDK: Pebble SDK 3, `enableMultiJS: true`
- Target platforms: `emery` (rect) and `gabbro` (round, Pebble Round 2, touchscreen)
- `pebble build` → `pebble install --emulator emery` (or `--emulator gabbro`)
- `src/run.sh` automates clean build + install to a pinned-SDK-version emulator (see `current-state.md` for why the version pin matters) and launches `src/open_config.js`, a dev-only helper that opens a local settings page pre-filled with a hardcoded coastal default (`Newgale, Wales`) for quick emulator testing — see `decisions.md` → "Dev-only Newgale default for emulator testing". Not shipped code; lives only in `open_config.js`.
- Message inbox size: 512 bytes; outbox: 128 bytes
