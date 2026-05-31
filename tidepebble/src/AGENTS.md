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
- `pebble build`: compile the watch app.
- After every successful `pebble build`, run `pebble install --emulator emery`.
- `pebble logs --emulator emery`: stream emulator logs.
- `pebble screenshot /tmp/tidepebble.png`: capture the current emulator screen.

## Coding Style
- Use 2-space indentation and K&R-style braces.
- Prefix static globals with `s_` and internal functions with `prv_`.
- Keep handlers small and event-driven.

## Testing
- Run `pebble clean && pebble build`.
- Validate phone companion syntax with `node --check src/pkjs/index.js`.
- Validate location and network behavior on a paired phone; the emulator may not provide a GPS fix.
