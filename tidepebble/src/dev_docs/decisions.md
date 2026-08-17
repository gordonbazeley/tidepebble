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

## Removed: Newquay fallback while GPS resolves

**Chose (superseded):** Load Newquay (50.4155, -5.0737) immediately, replace with GPS result when ready.  
**Why it was removed:** The fallback fired unconditionally for *every* non-manual refresh — even once GPS was already known to work — briefly showing wrong tide data to all non-UK users on every launch, not just first load. Replaced (commit `400a9c3`) with an explicit status message ("Finding phone location..." / "Location unavailable - open settings") and no fallback data. See `architecture.md` → Location resolution.  
**Trade-off now:** Watch shows no chart at all while GPS resolves, instead of (wrong) data — a deliberate accuracy-over-liveliness trade.

## Degenerate tide-range fallback in `prv_compute_state`

**Chose:** If `s_local_min == s_local_max` (the bounds either side of "now" collapse to the same value — most often when there's no confirmed previous turning point yet, e.g. right after a resync), recompute both from `prv_tide_min_max()` (min/max over the whole fetched series) instead of leaving a zero-width range.  
**Why:** Anything dividing by `(s_local_max - s_local_min)` — currently only the App Glance fill-percentage — would otherwise divide by zero/near-zero and snap to a meaningless 0% or 100%.  
**Trade-off:** The whole-series min/max is a coarser approximation of "the current tide cycle's range" than a real previous turning point would be, but it's always well-defined and non-degenerate.  
**Gotcha:** This guard was accidentally deleted once during a turning-point-detection rework (while adding the App Glance feature) and reintroduced the exact bug it prevents — a stuck "Tide 0% in" on the pin subtitle. Keep it whenever touching `prv_compute_state`.

## Dev-only Newgale default for emulator testing

**Chose:** `src/open_config.js` (used by `src/run.sh`) hardcodes its initial settings-page state to `Newgale, Wales` (51.85785, -5.12673) rather than defaulting to GPS/"Phone GPS".  
**Why:** This tool only ever targets the emulator, whose simulated phone GPS can't resolve to anywhere useful for tide data — defaulting to a real coastal location means a single click of "Save settings" (or nothing, if the pre-fill is trusted) gets a working emulator session, without having to search for a location every run.  
**Trade-off:** Purely a dev convenience — this default lives only in `open_config.js`, is never compiled into the watch app or `pkjs/index.js`, and is **not** automatic: the settings page still requires a manual "Save settings" click in the browser to push it to the running emulator. Until saved, the watch keeps whatever the emulator's real (simulated) GPS resolves to, which can look like a "wrong location" bug if you didn't realize a click was needed.

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

**Chose:** Vertical fill gauge, not a per-hour forecast strip (an earlier design was — see gotcha below). `TIDE_BAR_SEGMENTS` (6) cells represent how full the *current* local tidal range is, all evaluated at "now": `sea_cells = round((s_current_value - s_local_min) * 6 / (s_local_max - s_local_min))`, clamped to `[0, 6]`. **Beach is fixed at the bottom, sea at the top** (`is_sea_seg[i] = i < sea_cells`, `prv_draw_tide_bar`, `c/tidepebble.c:522`) — matches looking at a real beach: sand underfoot at the bottom of view, sea toward the horizon at the top. As `sea_cells` grows the sea fills down from the top, so during a rise the boundary advances *downward*, toward the viewer. The arrow at that boundary points **down for a rise, up for a fall** — the inverse of the abstract `s_rising` flag itself — to match the boundary's actual on-screen direction of travel; only the arrow's fill *color* (green/orange) follows `s_rising` directly. (An intermediate version anchored sea at the bottom instead, reasoning that the arrow should simply point the same way as `s_rising`'s "up" glyph — that got the beach-viewing framing backwards, since it meant the beach shrank from the top instead of the bottom.)  
**Why:** Reuses exactly the same `s_local_min`/`s_local_max`/`s_rising` that `prv_compute_state` already derives for the App Glance percentage (bracketed by the nearest confirmed past/future turning points, not the whole fetched series) — one tide-range computation feeds both the pin subtitle and this gauge, kept in sync by construction.  
**Trade-off:** It's a snapshot of "now", not a look-ahead — don't read it as "the next 6 hours". Roughly a third full 2 hours after a low is expected (rise follows a cosine curve, slow at the start) — if it reads emptier than that, `s_local_min` is probably wrong, see gotcha.  
**Gotcha (real bug, fixed once):** `s_local_min`/`s_local_max` depend on `prv_compute_state` finding a real *previous* turning point within the fetched window. With too little lookback (the pkjs `LOOKBACK_HOURS` was briefly reverted to 1h during an unrelated fix), there's usually no previous turning point in view yet, so the fallback bound-scan collapses to a value very close to `s_current_value` itself — the gauge reads all-beach (or all-sea) regardless of the real tide state, even right after a real low. Fixed by keeping `LOOKBACK_HOURS = 7` in both `pkjs/index.js` and `open_config.js` (comfortably more than the ~6.2h semi-diurnal half-period) with `&past_days=1` on the Open-Meteo request so the lookback has data to draw from even shortly after local midnight. `TIDE_POINT_COUNT`/`HOURS_TO_SEND` grew from 24 to 30 to fit this lookback *in addition to* the original ~23h forward window, rather than stealing from it — see `s_chart_start` in `architecture.md`, which keeps those extra lookback hours out of the chart entirely (fetched for calculation only, never drawn).  
**Prior art:** An earlier version of this widget really was a 6-hour-forward, one-cell-per-hour forecast strip sampling `s_tide_values[sample + i]` directly, thresholded against the whole-series midpoint. It was replaced by the current fill-gauge design; this doc previously still described the old version — if you're reading an older copy of this section (or its `git blame`), treat it as stale.

## Manual location stored in localStorage

**Chose:** `localStorage` with key `tide_selected_location_v1`.  
**Why:** Pebble JS localStorage survives app restarts. The `_v1` suffix allows future schema migration without collision.  
**Trade-off:** Cleared if the user reinstalls the app.
