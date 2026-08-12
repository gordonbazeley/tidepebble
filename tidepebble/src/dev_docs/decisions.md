# TidePebble — Key Decisions

## Data source: Open-Meteo Marine API

**Chose:** Open-Meteo (free, no API key, `sea_level_height_msl`).  
**Why:** Free tier covers all needs. No key management on the watch or phone. `sea_level_height_msl` gives absolute tide height (not anomaly), which is what you want for a tide chart.  
**Trade-off:** Data is hourly model output (DWD ICON), not station-measured tidal harmonics. Peak timing can be off by 30–60 min in some coastal areas. Acceptable for a surf/beach planning app.

## Custom 12-bit encoding

**Chose:** Base64-style alphabet (64 chars), 2 chars per sample = 12 bits per value.  
**Why:** Pebble AppMessage max payload is ~256 bytes usable. 12 bits gives ±2047 cm range (far more than any real tide) and fits 12 samples in 24 chars, leaving room for other keys.  
**Trade-off:** Bespoke codec. Must match exactly between pkjs and C decoder. Offset is +2048 (not standard base64).

## Chunked sending with 150ms delay

**Chose:** Send 12 samples per AppMessage, 150ms gap between chunks.  
**Why:** Pebble AppMessage queue drops messages if sent too fast. 150ms is empirically safe.  
**Trade-off:** Full 24-sample dataset takes ~300ms to transfer. Watch shows partial data briefly during load.

## Newquay fallback while GPS resolves

**Chose:** Load Newquay (50.4155, -5.0737) immediately, replace with GPS result when ready.  
**Why:** Watch shows something useful instantly rather than spinning. Author is UK-based.  
**Trade-off:** Non-UK users see irrelevant data for a few seconds on first load.

## Units from Pebble system preference

**Chose:** Read `health_service_get_measurement_system_for_display` rather than a separate toggle.  
**Why:** User already set their preference in Pebble settings. No duplicated UI.  
**Trade-off:** Requires `health` capability; falls back to metric if unavailable.

## Four-page design (Overview / NowNext / Then / Later)

**Chose:** Four named pages navigated with Up/Down, Select resets to NowNext.  
**Why:** NowNext is the most-used view. Overview gives the 24h picture. Then/Later give event detail without cluttering the main screen. Simple page-dot navigation matches Pebble conventions.  
**Prior art:** Originally a single-page design; refactored at commit `df62254`.

## Swell values received but chart not rendered

**Chose:** Receive and parse `swell_wave_height` per-hour series into `s_swell_values`, but only display the scalar current-hour value on the NOW card.  
**Why:** A per-hour swell chart was added then backed out (`e8271bf`) — it cluttered the display without adding clear value at watch resolution.  
**State:** `s_swell_values` is populated on every update but never read by any draw function.

## BigDataCloud for reverse geocoding

**Chose:** `https://api.bigdatacloud.net/data/reverse-geocode-client` (no API key, browser-side).  
**Why:** Free, no key, returns `city`/`locality`/`principalSubdivision` which compose a readable label.  
**Trade-off:** Third-party dependency; label quality varies outside major cities.

## Beachometer (tide bar) design

**Chose:** Vertical strip of `TIDE_BAR_SEGMENTS` (6) cells, one per hour, starting at the current hour. Cell `i` = `sample + i` into `s_tide_values` (clamped to last index). Cell `i=0` draws at the top, `i=5` at the bottom (`prv_draw_tide_bar`, `c/tidepebble.c:451`). Each cell renders wavy lines (sea) if its tide value is above `mid_v`, or sand dots (beach) if below. `mid_v` is `(min + max) / 2` computed once over the *entire* fetched `s_tide_values` series (`prv_tide_min_max`, `c/tidepebble.c:312`), not just the 6 visible hours. A big arrow is drawn at the sea/beach boundary, pointing up (rising) or down (falling) per `s_rising`.  
**Why:** One segment per hour reads as a short-range forecast strip, not just a snapshot of "now" — lets you see an ebbing tide approaching low water within the visible window. Global min/max keeps the threshold stable across redraws instead of jittering as the window slides.  
**Trade-off:** Because it's a 6-hour forecast, not a single "now" indicator, it's normal to see beach segments near the bottom even when the top (now) is sea — a falling tide is roughly halfway to low water ~6h after high water (semi-diurnal period ≈ 12.4h). Also, `mid_v` uses the global min/max of the whole fetched series, so the sea/beach split can be skewed by diurnal inequality (the two daily highs/lows aren't equal height) rather than reflecting the local high/low of the visible window.  
**Gotcha:** If `sample + i` runs past `s_tide_count - 1` (near the end of the fetched series), the index clamps to the last sample and repeats it — the tail of the bar can show a flat, non-representative segment.

## Manual location stored in localStorage

**Chose:** `localStorage` with key `tide_selected_location_v1`.  
**Why:** Pebble JS localStorage survives app restarts. The `_v1` suffix allows future schema migration without collision.  
**Trade-off:** Cleared if the user reinstalls the app.
