# TidePebble — Todo

Items are rough-priority order. Mark done or delete when shipped.

## Cleanup / dead code

- [ ] **Remove or wire up `s_swell_values`.** Either delete the `wave_values` parse path in `prv_inbox_received` and the `s_swell_values` / `s_swell_count` globals (if swell chart is definitely not happening), or add a swell chart to the Overview page. Leaving it populated-but-unused is confusing.
- [ ] **Resolve double-tap handler.** `prv_tap_handler` detects double-tap but does nothing useful. Either wire it to an action (e.g. cycle page, refresh) or remove the whole tap subscription to save battery.
- [ ] **Remove unused `tide_times` message key.** It is declared in `package.json` but unused in both pkjs and C.
- [ ] **Regenerate `src/pkjs/settings-html.js` from `src/pkjs/settings.html`.** The app loads the JS wrapper, so source-only settings edits will not reach the companion until this is synced.

## Reliability

- [ ] **Chunk transfer recovery.** If any chunk after the first fails, the watch ends up with partial data but no clear error state. Options: (a) send a total-chunk-count in the first message and show "partial data" if count doesn't match after timeout; (b) retry failed chunk once before giving up.
- [ ] **Stale data on chunk offset > 0.** When `offset == 0`, counts reset. For subsequent chunks, a mid-sequence failure leaves `s_tide_count` at whatever the last successful chunk reached. Consider adding a "data complete" message after the final chunk.

## UX / display

- [ ] **Swell chart on Overview.** If reinstating swell, overlay a second (dimmer) line on the 24h chart. `s_swell_values` already holds the data. Would need a second min/max pass and a separate color (maybe `GColorLightGray`).
- [ ] **Double-tap to refresh.** Wire the existing double-tap detection to trigger an `appmessage` back to the phone requesting a refresh, instead of just lighting the screen.
- [ ] **Newquay fallback opt-out.** Non-UK users see Newquay data while GPS resolves. Could show "Locating…" with no chart instead of showing wrong data.

## Platform / future

- [ ] **Add more target platforms.** Currently `emery` only. `basalt` (Pebble Time) and `chalk` (Pebble Time Round) are common. Test font size selections — the `PBL_PLATFORM_SWITCH` calls are already there for layout, just needs build target additions in `package.json`.
- [ ] **Rebble store listing.** Store assets are ready. Verify app description in `store_assets/app_description.md` and submit to Rebble.
