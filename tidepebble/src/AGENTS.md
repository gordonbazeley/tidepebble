# Repository Guidelines

## App Purpose
- TidePebble shows the nearest available tide forecast from the user's phone location.
- The phone companion fetches hourly marine sea-level data and sends a compact 24-hour series to the watch.
- The watch renders the series as a tide-height chart.

## Project Structure
- `src/c/tidepebble.c`: Pebble watch app source.
- `src/pkjs/index.js`: phone companion for geolocation and tide API requests.
- `wscript`: Pebble SDK build rules.
- `package.json`: Pebble metadata, targets, and message keys.

## Development Commands
- `pebble build`: compile the watch app. Always follow a successful build with `pebble install --emulator emery`.
- Post-build hook (`wscript`): drops the JS source map, minifies the phone JS in `build/tidepebble.pbw`, and copies the pbw to `~/Nextcloud/pbws/` (skipped if that folder doesn't exist).
- `pebble logs --emulator emery`: stream emulator logs.
- `pebble screenshot /tmp/tidepebble.png`: capture the current emulator screen.

## Settings UI
- `pkjs/settings.html` is the editable source; `pkjs/settings-html.js` is the copy the app loads. After editing `settings.html`, update `settings-html.js` to match.

## Coding Style
- Use 2-space indentation and K&R-style braces.
- Prefix static globals with `s_` and internal functions with `prv_`.
- Keep handlers small and event-driven.

## Testing
- Run `pebble clean && pebble build`, then `pebble install --emulator emery`.
- Validate phone companion syntax with `node --check src/pkjs/index.js`.
- Validate location and network behavior on a paired phone; the emulator may not provide a GPS fix.

## Docs to keep current (for `dcp`)
- `../README.md`
- `dev_docs/architecture.md`, `dev_docs/current-state.md`, `dev_docs/decisions.md`, `dev_docs/todo.md`
