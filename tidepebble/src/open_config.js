#!/usr/bin/env node
// Dev-only config UI for the emulator. `pebble emu-app-config` can't be used here:
// its default flow navigates to a data: URL (blocked by modern Chromium's top-frame
// data: URL restriction), and its --file fallback loads the page but Save then tries
// to launch the custom `pebblejs://close` scheme, which has no registered handler
// outside pebble-tool's own (broken, for the same reason) interception. So instead of
// going through Pebble.openURL/webviewclosed at all, this serves settings.html over a
// real http://127.0.0.1 URL (Save just navigates there, no special scheme involved),
// and on save, fetches Open-Meteo directly and pushes the result to the running
// emulator via send_tide_message.py (see that file for why: `pebble send-app-message`
// itself doesn't wait for the watch's ACK before disconnecting, so it silently drops
// messages under load). That means location set here is live-only for the running
// emulator: it is not written to pypkjs's own localStorage (an opaque per-platform
// shelve DB, not worth reverse-engineering for a dev tool), so it won't survive an
// app reinstall.

var http = require('http');
var fs = require('fs');
var path = require('path');
var crypto = require('crypto');
var child_process = require('child_process');

var PROJECT_ROOT = path.resolve(__dirname, '..');
var SETTINGS_HTML = path.resolve(__dirname, 'pkjs', 'settings.html');
var MESSAGE_KEYS_C = path.resolve(PROJECT_ROOT, 'build', 'src', 'message_keys.auto.c');
var SEND_SCRIPT = path.resolve(__dirname, 'send_tide_message.py');

var MARINE_API = 'https://marine-api.open-meteo.com/v1/marine';
var HOURS_TO_SEND = 24;
var TIDE_CHUNK_SIZE = 12;
var TIDE_VALUE_ALPHABET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';

var APP_UUID = require(path.resolve(PROJECT_ROOT, 'package.json')).pebble.uuid;

function loadMessageKeys() {
  if (!fs.existsSync(MESSAGE_KEYS_C)) {
    throw new Error('build/src/message_keys.auto.c not found — run `pebble build` first');
  }
  var text = fs.readFileSync(MESSAGE_KEYS_C, 'utf8');
  var keys = {};
  var re = /MESSAGE_KEY_(\w+)\s*=\s*(\d+);/g;
  var m;
  while ((m = re.exec(text))) {
    keys[m[1]] = m[2];
  }
  return keys;
}

var KEYS = loadMessageKeys();

// send_tide_message.py needs libpebble2/pebble_tool, which live in pebble-tool's
// own venv, not the system Python — use the interpreter sitting next to the
// `pebble` binary itself rather than hardcoding a path.
function findPebbleVenvPython() {
  var pebbleBin = child_process.execFileSync('which', ['pebble']).toString().trim();
  var realBin = fs.realpathSync(pebbleBin); // `which` finds a shim symlink; resolve it to the real venv bin/
  var candidate = path.join(path.dirname(realBin), 'python3');
  return fs.existsSync(candidate) ? candidate : 'python3';
}

var PYTHON_BIN = findPebbleVenvPython();

function encodeTideValue(value) {
  var encoded = value + 2048;
  if (encoded < 0) encoded = 0;
  if (encoded > 4095) encoded = 4095;
  return TIDE_VALUE_ALPHABET.charAt((encoded >> 6) & 63) +
    TIDE_VALUE_ALPHABET.charAt(encoded & 63);
}

function findFirstCurrentHour(times) {
  var now = Date.now();
  for (var i = 0; i < times.length; i += 1) {
    if (new Date(times[i]).getTime() >= now) return i;
  }
  return 0;
}

// Throws if the watch doesn't ACK within send_tide_message.py's timeout, so a
// dropped message surfaces as a real error instead of a false "OK".
function sendAppMessage(fields) {
  var args = [SEND_SCRIPT, APP_UUID];
  Object.keys(fields).forEach(function(name) {
    var value = fields[name];
    var key = KEYS[name];
    if (!key) throw new Error('Unknown message key: ' + name);
    var kind = typeof value === 'number' ? 'i' : 's';
    args.push(kind + ':' + key + '=' + value);
  });
  child_process.execFileSync(PYTHON_BIN, args, { cwd: PROJECT_ROOT });
}

async function pushLocationToEmulator(latitude, longitude, label) {
  var url = MARINE_API +
    '?latitude=' + encodeURIComponent(latitude) +
    '&longitude=' + encodeURIComponent(longitude) +
    '&hourly=sea_level_height_msl,swell_wave_height,sea_surface_temperature' +
    '&forecast_days=2&timezone=auto';

  var response = await fetch(url);
  if (!response.ok) {
    return { ok: false, message: 'Tide service unavailable (HTTP ' + response.status + ')' };
  }
  var data = await response.json();
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
    if (heights[i] === null || typeof heights[i] === 'undefined') continue;
    values.push(encodeTideValue(Math.round(heights[i] * 100)));
    var wv = swellHeights && swellHeights[i] != null ? swellHeights[i] : 0;
    waveValues.push(encodeTideValue(Math.round(wv * 100)));
  }
  if (values.length < 2) {
    return { ok: false, message: 'No tide forecast near ' + label + ' — Open-Meteo has no marine data there' };
  }

  var chunks = [];
  for (var chunkStart = 0; chunkStart < values.length; chunkStart += TIDE_CHUNK_SIZE) {
    chunks.push({
      offset: chunkStart,
      values: values.slice(chunkStart, chunkStart + TIDE_CHUNK_SIZE).join(''),
      waveValues: waveValues.slice(chunkStart, chunkStart + TIDE_CHUNK_SIZE).join(''),
    });
  }

  // Each sendAppMessage() call blocks until the watch ACKs (or throws on
  // timeout/NACK), so these can fire back-to-back with no artificial delay.
  async function sendAll() {
    sendAppMessage({
      tide_location: label,
      tide_status: '',
      tide_current_minutes: currentMinutes,
      tide_wave_height: Math.round(waveH * 100),
      tide_sea_temp: Math.round(seaT * 10),
      tide_sample_offset: chunks[0].offset,
      tide_values: chunks[0].values,
      tide_wave_values: chunks[0].waveValues,
      // Real units come from the phone's own settings (health_service, see
      // c/tidepebble.c prv_use_metric_units) — no user-facing override on
      // real devices. The emulator's simulated phone has no real region
      // setting and defaults to imperial, so force metric here specifically
      // for emulator testing.
      tide_units_override: 1,
    });

    for (var c = 1; c < chunks.length; c += 1) {
      sendAppMessage({
        tide_sample_offset: chunks[c].offset,
        tide_values: chunks[c].values,
        tide_wave_values: chunks[c].waveValues,
      });
    }

    sendAppMessage({ tide_sync_complete: 1 });
  }

  await sendAll();

  // The watch requests a refresh from the phone's real GPS on every launch
  // (c/tidepebble.c prv_init -> prv_send_refresh_request), and phone-side
  // geolocation has a 15s timeout before falling back — so a launch that's
  // still starting up can silently overwrite this push a few seconds from
  // now. Resend once more after that window closes so this push wins last.
  setTimeout(function() {
    sendAll().catch(function(err) { console.error('Guard resend failed:', err.message); });
  }, 18000);

  return { ok: true, message: 'Pushed ' + values.length + 'h of tide data for ' + label + ' to the running emulator.' };
}

// ---- HTTP server ----
if (!fs.existsSync(SETTINGS_HTML)) {
  console.error('Error: settings HTML not found at', SETTINGS_HTML);
  process.exit(1);
}
var html = fs.readFileSync(SETTINGS_HTML, 'utf8');
var saveToken = crypto.randomBytes(16).toString('hex');

// This tool only ever targets --emulator emery, so "preset for the emulator"
// just means: default the settings page to a known-good coastal spot instead
// of Phone GPS, which the emulator can't resolve to anywhere useful anyway.
var lastState = {
  mode: 'manual', location: 'Newgale, Wales', lat: 51.85785, lon: -5.12673,
};

function statusPage(title, message) {
  return [
    '<!doctype html><html><head><meta charset="utf-8">',
    '<style>body{font-family:Helvetica,Arial,sans-serif;margin:40px;background:#f5f5f5;text-align:center;}',
    'h1{color:#111;font-size:24px;}p{color:#555;font-size:16px;max-width:480px;margin:0 auto;}</style></head><body>',
    '<h1>' + title + '</h1>',
    '<p>' + message + '</p>',
    '<p>You may close this tab.</p>',
    '</body></html>',
  ].join('');
}

var serverTimer = null;
var server = http.createServer(function(req, res) {
  var parsed = new URL(req.url, 'http://127.0.0.1');

  if (parsed.pathname === '/save') {
    if (parsed.searchParams.get('token') !== saveToken) {
      res.writeHead(403, { 'Content-Type': 'text/plain' });
      res.end('Forbidden');
      return;
    }
    var rawData = parsed.searchParams.get('data') || '';
    var settings;
    try {
      settings = JSON.parse(rawData);
    } catch (e) {
      try { settings = JSON.parse(decodeURIComponent(rawData)); }
      catch (e2) {
        res.writeHead(400, { 'Content-Type': 'text/plain' });
        res.end('Bad request: could not parse settings');
        return;
      }
    }
    console.log('Received settings:', JSON.stringify(settings));

    var handled = (async function() {
      if (settings.mode === 'manual' && typeof settings.lat === 'number' && typeof settings.lon === 'number') {
        lastState = { mode: 'manual', location: settings.location, lat: settings.lat, lon: settings.lon };
        return pushLocationToEmulator(settings.lat, settings.lon, settings.location || 'selected location');
      }
      lastState = { mode: 'gps', location: 'Phone GPS', lat: null, lon: null };
      return {
        ok: false,
        message: 'GPS mode isn\'t automated by this dev tool (no real phone location available). ' +
          'Pick a coastal location manually to preview it on the emulator.',
      };
    })();

    handled.then(function(result) {
      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
      res.end(statusPage(result.ok ? 'Sent to emulator' : 'Not sent', result.message));
      console.log(result.ok ? 'OK:' : 'Skipped:', result.message);
      if (serverTimer) clearTimeout(serverTimer);
      setTimeout(function() { server.close(); }, 2000);
    }).catch(function(err) {
      res.writeHead(500, { 'Content-Type': 'text/html; charset=utf-8' });
      res.end(statusPage('Error', String(err && err.message || err)));
      console.error(err);
    });
    return;
  }

  res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
  res.end(html);
});

server.on('error', function(error) {
  console.error('Could not start config server:', error.message);
  process.exit(1);
});

server.listen(0, '127.0.0.1', function() {
  var port = server.address().port;

  serverTimer = setTimeout(function() {
    console.log('Config server closing after 10-minute timeout.');
    server.close();
  }, 600000);

  var base = 'http://127.0.0.1:' + port;
  var returnTo = encodeURIComponent(base + '/save?token=' + saveToken + '&data=');

  var params = [
    'return_to=' + returnTo,
    'mode=' + encodeURIComponent(lastState.mode),
    'location=' + encodeURIComponent(lastState.location),
  ];
  if (lastState.lat != null) params.push('lat=' + lastState.lat);
  if (lastState.lon != null) params.push('lon=' + lastState.lon);

  var configUrl = base + '/?' + params.join('&');

  try {
    child_process.execFileSync('open', ['-a', 'Brave Browser', configUrl]);
    console.log('Config server: http://127.0.0.1:' + port);
  } catch (e) {
    console.error('Could not open Brave:', e.message);
    server.close();
    process.exit(1);
  }
});
