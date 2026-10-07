# TidePebble

Pebble watch app showing details of the tides for the next 24 hours for a selected location. Includes a sine chart showing the next high and low tides and the beachometer which shows how much beach is there right now.

On the phone settings page you can select specify a location or use the current location. Tide data will be fetched by default every hour outside of quiet times.

## Beachometer

The beachometer gives you a quick view of two things
* Roughly how much beach is present right now
* Is the tide going in or out 

The beachometer is split into six sections for the six hours between a high or low tide and shows you how roughly much beach is present right now. 

A marker arrow sits at the sea / beach boundary and points in the direction the tide is heading:
* Down when the tide is rising / coming in.
* Up when the tide is falling / going out 
Unlike the main beachometer with six hourly cells, the marker's position is in real time: it's placed at the exact tide height for the current minute.

This makes a bit more sense with some examples:

| Falling - big beach | Mid tide - balanced | Rising - small beach |
| --- | --- | --- |
| ![Falling tide, most of the bar is beach](docs/images/beachometer-falling.png) | ![Mid tide, beach and sea roughly balanced](docs/images/beachometer-mid.png) | ![Rising tide, most of the bar is sea](docs/images/beachometer-rising.png) |


## Support

Feedback and ideas are welcome. 

Tidepebble is free and always will be. If it's earned a spot on your watch, you can say thanks with a coffee. No pressure, entirely optional, and hugely appreciated.

Doctor's orders: one coffee a day. So it had better be a good one :-)

[![Support me on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/gordonbazeley)

[![Sponsor me on GitHub](https://img.shields.io/badge/Sponsor-%E2%9D%A4-db61a2.png?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/gordonbazeley)
## Data Sources

- Tide forecasts: [Open-Meteo Marine API](https://open-meteo.com/en/docs/marine-weather-api), sourced from DWD.
- Place names: [BigDataCloud reverse geocoding](https://www.bigdatacloud.com/free-api/free-reverse-geocode-to-city-api).

## Development
* Take a look at /tidepebble/src/dev_docs

```sh
cd tidepebble
pebble build
pebble install --emulator emery
```

The generated pbw bundle is `tidepebble/build/tidepebble.pbw`.

