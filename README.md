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

## Support

tidepebble is free and always will be. If it's earned a spot on your watch, you can say thanks with a coffee. No pressure, entirely optional, and hugely appreciated.
Doctor's orders: one coffee a day. So it had better be a good one :-)
[![Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/gordonbazeley)
[![Sponsor](https://img.shields.io/badge/Sponsor-%E2%9D%A4-db61a2.png?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/gordonbazeley)
## Data Sources

- Tide forecasts: [Open-Meteo Marine API](https://open-meteo.com/en/docs/marine-weather-api), sourced from DWD.
- Place names: [BigDataCloud reverse geocoding](https://www.bigdatacloud.com/free-api/free-reverse-geocode-to-city-api).
