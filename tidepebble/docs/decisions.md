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

## Manual location stored in localStorage

**Chose:** `localStorage` with key `tide_selected_location_v1`.  
**Why:** Pebble JS localStorage survives app restarts. The `_v1` suffix allows future schema migration without collision.  
**Trade-off:** Cleared if the user reinstalls the app.
