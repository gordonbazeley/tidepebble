/* Tide chart data bridge: phone GPS -> Open-Meteo Marine API -> Pebble watch. */
(function() {
  var MARINE_API = 'https://marine-api.open-meteo.com/v1/marine';
  var REVERSE_GEOCODE_API = 'https://api.bigdatacloud.net/data/reverse-geocode-client';
  var HOURS_TO_SEND = 24;
  var TIDE_CHUNK_SIZE = 12;
  var TIDE_VALUE_ALPHABET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';
  var NEWQUAY_FALLBACK = {
    coords: {
      latitude: 50.4155,
      longitude: -5.0737
    }
  };
  var SELECTED_LOCATION_KEY = 'tide_selected_location_v1';
  var s_selectedLocation = null;

  function send(payload, onSuccess, onError) {
    Pebble.sendAppMessage(payload, onSuccess, onError);
  }

  function sendChunkSequence(chunks, waveChunks, index, currentMinutes, fallbackLabel) {
    if (index >= chunks.length) {
      return;
    }
    var payload = {
      tide_current_minutes: currentMinutes,
      tide_sample_offset: chunks[index].offset,
      tide_values: chunks[index].values
    };
    if (waveChunks && index < waveChunks.length) {
      payload.tide_wave_values = waveChunks[index].values;
    }
    send(payload, function() {
      setTimeout(function() {
        sendChunkSequence(chunks, waveChunks, index + 1, currentMinutes, fallbackLabel);
      }, 150);
    }, function() {
      sendStatus('Tide data unavailable', fallbackLabel);
    });
  }

  function sendStatus(status, location) {
    var payload = { tide_status: status };
    if (location) {
      payload.tide_location = location;
    }
    send(payload);
  }

  function encodeTideValue(value) {
    var encoded = value + 2048;
    if (encoded < 0) {
      encoded = 0;
    }
    if (encoded > 4095) {
      encoded = 4095;
    }
    return TIDE_VALUE_ALPHABET.charAt((encoded >> 6) & 63) +
      TIDE_VALUE_ALPHABET.charAt(encoded & 63);
  }

  function locationLabel(latitude, longitude) {
    return latitude.toFixed(2) + ', ' + longitude.toFixed(2);
  }

  function geocodingLabel(location) {
    var parts = [location.name];
    if (location.admin1) {
      parts.push(location.admin1);
    }
    if (location.country) {
      parts.push(location.country);
    }
    return parts.join(', ');
  }

  function loadSelectedLocation() {
    var raw = localStorage.getItem(SELECTED_LOCATION_KEY);
    if (!raw) {
      s_selectedLocation = null;
      return;
    }
    try {
      s_selectedLocation = JSON.parse(raw);
    } catch (error) {
      s_selectedLocation = null;
    }
  }

  function saveSelectedLocation(location) {
    s_selectedLocation = location;
    localStorage.setItem(SELECTED_LOCATION_KEY, JSON.stringify(location));
  }

  function clearSelectedLocation() {
    s_selectedLocation = null;
    localStorage.removeItem(SELECTED_LOCATION_KEY);
  }

  function reverseGeocode(latitude, longitude, onComplete) {
    var fallback = locationLabel(latitude, longitude);
    var request = new XMLHttpRequest();
    request.open('GET', REVERSE_GEOCODE_API +
      '?latitude=' + encodeURIComponent(latitude) +
      '&longitude=' + encodeURIComponent(longitude) +
      '&localityLanguage=en', true);
    request.onload = function() {
      if (request.status < 200 || request.status >= 300) {
        onComplete(fallback);
        return;
      }
      try {
        var data = JSON.parse(request.responseText);
        onComplete(data.city || data.locality || data.principalSubdivision || fallback);
      } catch (error) {
        onComplete(fallback);
      }
    };
    request.onerror = function() {
      onComplete(fallback);
    };
    request.send();
  }

  function findFirstCurrentHour(times) {
    var now = Date.now();
    for (var i = 0; i < times.length; i += 1) {
      if (new Date(times[i]).getTime() >= now) {
        return i;
      }
    }
    return 0;
  }

  function fetchTides(position, fallbackLabel) {
    var latitude = position.coords.latitude;
    var longitude = position.coords.longitude;
    if (s_selectedLocation) {
      latitude = s_selectedLocation.latitude;
      longitude = s_selectedLocation.longitude;
    }
    var label = fallbackLabel || locationLabel(latitude, longitude);
    var url = MARINE_API +
      '?latitude=' + encodeURIComponent(latitude) +
      '&longitude=' + encodeURIComponent(longitude) +
      '&hourly=sea_level_height_msl,swell_wave_height,sea_surface_temperature' +
      '&forecast_days=2' +
      '&timezone=auto';

    sendStatus('Loading nearest marine forecast...', label);
    var request = new XMLHttpRequest();
    request.open('GET', url, true);
    request.onload = function() {
      if (request.status < 200 || request.status >= 300) {
        sendStatus('Tide service unavailable', label);
        return;
      }
      try {
        var data = JSON.parse(request.responseText);
        var times = data.hourly.time;
        var heights = data.hourly.sea_level_height_msl;
        var swellHeights = data.hourly.swell_wave_height;
        var seaTemps = data.hourly.sea_surface_temperature;
        var start = Math.max(0, findFirstCurrentHour(times) - 1);
        var currentMinutes = Math.round((Date.now() - new Date(times[start]).getTime()) / 60000);
        var waveH = swellHeights && swellHeights[start] != null ? swellHeights[start] : 0;
        var seaT = seaTemps && seaTemps[start] != null ? seaTemps[start] : 0;
        var values = [];
        var waveValues = [];
        for (var i = start; i < times.length && values.length < HOURS_TO_SEND; i += 1) {
          if (heights[i] === null || typeof heights[i] === 'undefined') {
            continue;
          }
          values.push(encodeTideValue(Math.round(heights[i] * 100)));
          var wv = swellHeights && swellHeights[i] != null ? swellHeights[i] : 0;
          waveValues.push(encodeTideValue(Math.round(wv * 100)));
        }
        if (values.length < 2) {
          sendStatus('No tide forecast near this location', label);
          return;
        }
        var chunks = [];
        var waveChunks = [];
        for (var chunkStart = 0; chunkStart < values.length; chunkStart += TIDE_CHUNK_SIZE) {
          chunks.push({
            offset: chunkStart,
            values: values.slice(chunkStart, chunkStart + TIDE_CHUNK_SIZE).join('')
          });
          waveChunks.push({
            offset: chunkStart,
            values: waveValues.slice(chunkStart, chunkStart + TIDE_CHUNK_SIZE).join('')
          });
        }
        send({
          tide_location: label,
          tide_status: '',
          tide_current_minutes: currentMinutes,
          tide_wave_height: Math.round(waveH * 100),
          tide_sea_temp: Math.round(seaT * 10)
        }, function() {
          setTimeout(function() {
            sendChunkSequence(chunks, waveChunks, 0, currentMinutes, label);
          }, 150);
        }, function() {
          sendStatus('Tide data unavailable', label);
        });
      } catch (error) {
        sendStatus('Invalid tide service response', label);
      }
    };
    request.onerror = function() {
      sendStatus('Phone could not reach tide service', label);
    };
    request.send();
  }

  function refresh() {
    loadSelectedLocation();
    if (s_selectedLocation) {
      fetchTides({ coords: s_selectedLocation }, geocodingLabel(s_selectedLocation));
      return;
    }
    sendStatus('Using Newquay fallback...');
    fetchTides(NEWQUAY_FALLBACK, 'Newquay, Cornwall');
    if (!navigator.geolocation) {
      return;
    }
    sendStatus('Finding phone location...');
    navigator.geolocation.getCurrentPosition(function(position) {
      reverseGeocode(position.coords.latitude, position.coords.longitude, function(label) {
        fetchTides(position, label);
      });
    }, function() {
    }, {
      enableHighAccuracy: false,
      timeout: 15000,
      maximumAge: 30 * 60 * 1000
    });
  }

  Pebble.addEventListener('ready', refresh);
  Pebble.addEventListener('appmessage', refresh);
  Pebble.addEventListener('webviewclosed', function(e) {
    if (!e || !e.response) {
      return;
    }
    try {
      var data = JSON.parse(decodeURIComponent(e.response));
      if (!data) {
        return;
      }
      // New state format: {mode, location, lat, lon}
      if (data.mode === 'gps' || data.usePhoneLocation) {
        clearSelectedLocation();
        refresh();
      } else if (data.mode === 'manual' && typeof data.lat === 'number') {
        var nameParts = (data.location || '').split(', ');
        saveSelectedLocation({
          latitude: data.lat,
          longitude: data.lon,
          name: nameParts[0] || data.location || 'Unknown',
          admin1: nameParts[1] || '',
          country: nameParts[2] || '',
        });
        refresh();
      } else if (typeof data.latitude === 'number') {
        // Legacy format (kept for compatibility)
        saveSelectedLocation({
          latitude: data.latitude,
          longitude: data.longitude,
          name: data.name,
          admin1: data.admin1,
          country: data.country
        });
        refresh();
      }
    } catch (error) {
    }
  });
  Pebble.addEventListener('showConfiguration', function() {
    loadSelectedLocation();
    var SETTINGS_HTML = require('./settings-html');
    var state = {
      mode: s_selectedLocation ? 'manual' : 'gps',
      location: s_selectedLocation ? geocodingLabel(s_selectedLocation) : 'Phone GPS',
      lat: s_selectedLocation ? s_selectedLocation.latitude : null,
      lon: s_selectedLocation ? s_selectedLocation.longitude : null,
    };
    var init = 'var injected=' + JSON.stringify(state) + ';';
    var html = SETTINGS_HTML.replace('/*STATE_INIT*/', init);
    Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
  });
}());
