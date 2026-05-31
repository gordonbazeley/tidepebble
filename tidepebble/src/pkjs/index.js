/* Tide chart data bridge: phone GPS -> Open-Meteo Marine API -> Pebble watch. */
(function() {
  var MARINE_API = 'https://marine-api.open-meteo.com/v1/marine';
  var REVERSE_GEOCODE_API = 'https://api.bigdatacloud.net/data/reverse-geocode-client';
  var HOURS_TO_SEND = 24;
  var NEWQUAY_FALLBACK = {
    coords: {
      latitude: 50.4155,
      longitude: -5.0737
    }
  };

  function send(payload) {
    Pebble.sendAppMessage(payload, function() {
      console.log('Sent tide chart to watch');
    }, function(error) {
      console.log('Unable to send tide chart: ' + JSON.stringify(error));
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

  function locationLabel(latitude, longitude) {
    return latitude.toFixed(2) + ', ' + longitude.toFixed(2);
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
        var labels = [];
        for (var i = start; i < times.length && values.length < HOURS_TO_SEND; i += 1) {
          if (heights[i] === null || typeof heights[i] === 'undefined') {
            continue;
          }
          values.push(Math.round(heights[i] * 100));
          var timestamp = new Date(times[i]);
          labels.push(pad(timestamp.getHours()) + ':' + pad(timestamp.getMinutes()));
        }
        if (values.length < 2) {
          sendStatus('No tide forecast near this location', label);
          return;
        }
        send({
          tide_location: label,
          tide_status: '',
          tide_current_minutes: currentMinutes,
          tide_values: values.join(','),
          tide_times: labels.join(',')
        });
      } catch (error) {
        console.log('Unable to parse tide response: ' + error);
        sendStatus('Invalid tide service response', label);
      }
    };
    request.onerror = function() {
      sendStatus('Phone could not reach tide service', label);
    };
    request.send();
  }

  function refresh() {
    if (!navigator.geolocation) {
      sendStatus('Using Newquay fallback...');
      fetchTides(NEWQUAY_FALLBACK, 'Newquay, Cornwall');
      return;
    }
    sendStatus('Finding phone location...');
    navigator.geolocation.getCurrentPosition(function(position) {
      reverseGeocode(position.coords.latitude, position.coords.longitude, function(label) {
        fetchTides(position, label);
      });
    }, function(error) {
      console.log('Location failed: ' + JSON.stringify(error));
      sendStatus('Using Newquay fallback...');
      fetchTides(NEWQUAY_FALLBACK, 'Newquay, Cornwall');
    }, {
      enableHighAccuracy: false,
      timeout: 15000,
      maximumAge: 30 * 60 * 1000
    });
  }

  Pebble.addEventListener('ready', refresh);
  Pebble.addEventListener('appmessage', refresh);
  Pebble.addEventListener('showConfiguration', function() {
    var html = '<!doctype html><html><head><meta name="viewport" content="width=device-width">' +
      '<style>body{font:16px sans-serif;margin:24px;line-height:1.5}h1{font-size:22px}</style></head>' +
      '<body><h1>TidePebble</h1><p>Tide forecasts: <a href="https://open-meteo.com/">Open-Meteo</a> ' +
      'marine data, sourced from DWD.</p><p>Place names: ' +
      '<a href="https://www.bigdatacloud.com/">BigDataCloud</a> reverse geocoding.</p></body></html>';
    Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
  });
}());
