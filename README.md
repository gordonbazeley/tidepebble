# TidePebble

Pebble watch app showing a 24-hour tide chart for the user's nearest location.

The JavaScript phone companion:

- Gets the phone location, falling back to phone location when unavailable.
- Uses BigDataCloud reverse geocoding to display a nearby place name.
- Fetches hourly marine sea-level data from Open-Meteo.
- Sends a compact tide series to the watch.

The watch app displays a blue tide-height chart, high and low tide labels, axis values, and a green interpolated marker for the current time.

## Build

```sh
cd tidepebble
pebble build
pebble install --emulator emery
```

The generated sideload bundle is `tidepebble/build/tidepebble.pbw`.

## Data Sources

- Tide forecasts: [Open-Meteo Marine API](https://open-meteo.com/en/docs/marine-weather-api), sourced from DWD.
- Place names: [BigDataCloud reverse geocoding](https://www.bigdatacloud.com/free-api/free-reverse-geocode-to-city-api).
