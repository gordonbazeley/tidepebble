/* Tide chart data bridge: phone GPS -> Open-Meteo Marine API -> Pebble watch. */
(function() {
  var MARINE_API = 'https://marine-api.open-meteo.com/v1/marine';
  var GEOCODING_API = 'https://geocoding-api.open-meteo.com/v1/search';
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

  function send(payload) {
    Pebble.sendAppMessage(payload);
  }

  function sendChunkSequence(chunks, index, currentMinutes, fallbackLabel) {
    if (index >= chunks.length) {
      return;
    }
    var payload = {
      tide_current_minutes: currentMinutes,
      tide_sample_offset: chunks[index].offset,
      tide_values: chunks[index].values
    };
    Pebble.sendAppMessage(payload, function() {
      setTimeout(function() {
        sendChunkSequence(chunks, index + 1, currentMinutes, fallbackLabel);
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

  function pad(number) {
    return number < 10 ? '0' + number : String(number);
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

  function htmlEscape(value) {
    return String(value)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
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

  function searchLocations(query, onComplete) {
    var request = new XMLHttpRequest();
    request.open('GET', GEOCODING_API +
      '?name=' + encodeURIComponent(query) +
      '&count=10&language=en&format=json', true);
    request.onload = function() {
      if (request.status < 200 || request.status >= 300) {
        onComplete([]);
        return;
      }
      try {
        var data = JSON.parse(request.responseText);
        var results = data.results || [];
        results.sort(function(a, b) {
          return (a.name || '').localeCompare(b.name || '');
        });
        onComplete(results);
      } catch (error) {
        onComplete([]);
      }
    };
    request.onerror = function() {
      onComplete([]);
    };
    request.send();
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
      '&hourly=sea_level_height_msl' +
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
        var start = Math.max(0, findFirstCurrentHour(times) - 1);
        var currentMinutes = Math.round((Date.now() - new Date(times[start]).getTime()) / 60000);
        var values = [];
        for (var i = start; i < times.length && values.length < HOURS_TO_SEND; i += 1) {
          if (heights[i] === null || typeof heights[i] === 'undefined') {
            continue;
          }
          values.push(encodeTideValue(Math.round(heights[i] * 100)));
        }
        if (values.length < 2) {
          sendStatus('No tide forecast near this location', label);
          return;
        }
        var chunks = [];
        for (var chunkStart = 0; chunkStart < values.length; chunkStart += TIDE_CHUNK_SIZE) {
          chunks.push({
            offset: chunkStart,
            values: values.slice(chunkStart, chunkStart + TIDE_CHUNK_SIZE).join('')
          });
        }
        Pebble.sendAppMessage({
          tide_location: label,
          tide_status: '',
          tide_current_minutes: currentMinutes
        }, function() {
          setTimeout(function() {
            sendChunkSequence(chunks, 0, currentMinutes, label);
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
      if (data && data.usePhoneLocation) {
        clearSelectedLocation();
        refresh();
      } else if (data && typeof data.latitude === 'number' && typeof data.longitude === 'number') {
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
    var currentLocation = s_selectedLocation ?
      htmlEscape(geocodingLabel(s_selectedLocation)) : 'Phone GPS';
    var phoneLocationButton = s_selectedLocation ?
      '<button id="phone" style="margin-bottom:10px;padding:10px;width:100%">Use phone location</button>' : '';
    var html = '<!doctype html><html><head><meta name="viewport" content="width=device-width">' +
      '<style>body{font:16px sans-serif;margin:24px;line-height:1.5}h1{font-size:22px}' +
      'input,button,ul{font:inherit}li{margin:0.4em 0}#current{margin-bottom:10px}' +
      '#search{display:flex;gap:8px}#q{box-sizing:border-box;min-width:0;padding:10px;flex:1}' +
      '#find{padding:10px}' +
      '</style></head><body>' +
      '<div id="current">Current location: ' + currentLocation + '</div>' +
      phoneLocationButton +
      '<h1>TidePebble</h1>' +
      '<p>Type a place name to override phone GPS.</p>' +
      '<div id="search"><input id="q" placeholder="Location name ..." autocomplete="off" />' +
      '<button id="find">Search</button></div>' +
      '<ul id="results"></ul>' +
      '<p>Tide forecasts: <a href="https://open-meteo.com/">Open-Meteo</a> marine data, sourced from DWD.</p>' +
      '<p>Place names: <a href="https://www.bigdatacloud.com/">BigDataCloud</a> reverse geocoding.</p>' +
      '<script>' +
      'var q=document.getElementById("q"),r=document.getElementById("results"),current=document.getElementById("current");' +
      'function returnTo(){var match=window.location.href.match(/[?&]return_to=([^&]+)/);return match?decodeURIComponent(match[1]):"";}' +
      'function closeWith(value){var response=encodeURIComponent(JSON.stringify(value)),callback=returnTo();window.location=callback?callback+response:"pebblejs://close#"+response;}' +
      'function nameFor(item){return item.name + (item.admin1 ? ", " + item.admin1 : "") + (item.country ? ", " + item.country : "");}' +
      'function selectLocation(item){var selected={latitude:item.latitude,longitude:item.longitude,name:item.name,admin1:item.admin1,country:item.country};current.textContent="Current location: " + nameFor(item);closeWith(selected);}' +
      'function render(items){r.innerHTML="";items.forEach(function(item){var li=document.createElement("li");var a=document.createElement("a");a.href="#";a.textContent=nameFor(item);a.onclick=function(){selectLocation(item);return false;};li.appendChild(a);r.appendChild(li);});}' +
      'function search(){var v=q.value.trim();if(!v){render([]);return;}fetch("https://geocoding-api.open-meteo.com/v1/search?name="+encodeURIComponent(v)+"&count=10&language=en&format=json").then(function(x){return x.json();}).then(function(j){render((j.results||[]).sort(function(a,b){return (a.name||"").localeCompare(b.name||"");}));});}' +
      'document.getElementById("find").onclick=search;var phone=document.getElementById("phone");if(phone){phone.onclick=function(){current.textContent="Current location: Phone GPS";closeWith({usePhoneLocation:true});};}q.oninput=function(){clearTimeout(window.t);window.t=setTimeout(search,250)};' +
      '</script></body></html>';
    Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
  });
}());
