#!/usr/bin/env node
// Opens TidePebble settings in Brave via a local HTTP server.
// Serves src/pkjs/settings.html directly; passes current settings as query params;
// receives saved settings at /save and persists them to pypkjs localStorage.

var http = require('http');
var fs = require('fs');
var path = require('path');
var child_process = require('child_process');

var pkg = require(path.resolve(__dirname, '..', 'package.json'));
var MSG_KEY_INDEX = {};
(pkg.pebble.messageKeys || []).forEach(function(name, idx) { MSG_KEY_INDEX[name] = idx; });

var SELECTED_LOCATION_KEY = 'tide_selected_location_v1';
var APP_UUID = pkg.pebble.uuid;
var PROJECT_ROOT = path.resolve(__dirname, '..');
var SETTINGS_HTML = path.resolve(__dirname, 'pkjs', 'settings.html');

function findLocalStorageFile() {
  var candidates = [
    path.join(process.env.HOME, '.pebble-dev', APP_UUID, 'localStorage.json'),
    path.join(process.env.HOME, '.pebble-dev', 'pypkjs_localStorage.json'),
    path.join(process.env.HOME, 'Library', 'Application Support', 'Pebble SDK', '4.9.169', 'emery', 'localStorage.json'),
  ];
  for (var i = 0; i < candidates.length; i++) {
    if (fs.existsSync(candidates[i])) return candidates[i];
  }
  try {
    var result = child_process.execSync(
      'find "$HOME/.pebble-dev" "$HOME/Library/Application Support/Pebble SDK" -name "*.json" -exec grep -l "' + SELECTED_LOCATION_KEY + '" {} \\; 2>/dev/null | head -1',
      { timeout: 5000, env: process.env }
    ).toString().trim();
    if (result) return result;
  } catch (e) {}
  return null;
}

var storageData = {};
var lsFile = findLocalStorageFile();
if (lsFile) {
  try {
    storageData = JSON.parse(fs.readFileSync(lsFile, 'utf8'));
    console.log('Loaded settings from', lsFile);
  } catch (e) {
    console.warn('Could not parse localStorage file:', e.message);
  }
} else {
  console.warn('pypkjs localStorage not found; using default settings');
}

// Read HTML from disk
if (!fs.existsSync(SETTINGS_HTML)) {
  console.error('Error: settings HTML not found at', SETTINGS_HTML);
  process.exit(1);
}
var html = fs.readFileSync(SETTINGS_HTML, 'utf8');

// Determine current state from localStorage
function getCurrentState() {
  var state = { mode: 'gps', location: 'Phone GPS', lat: null, lon: null, units: 'm', clock: '24' };
  try {
    var raw = storageData[SELECTED_LOCATION_KEY];
    if (raw) {
      var loc = JSON.parse(raw);
      state.mode = 'manual';
      state.location = loc.name + (loc.admin1 ? ', ' + loc.admin1 : '');
      state.lat = loc.latitude;
      state.lon = loc.longitude;
    }
  } catch (e) {}
  try {
    var saved = JSON.parse(storageData['tidepebble.settings'] || 'null');
    if (saved) {
      if (saved.units) state.units = saved.units;
      if (saved.clock) state.clock = saved.clock;
    }
  } catch (e) {}
  return state;
}

function persistSettings() {
  if (!lsFile) {
    lsFile = path.join(process.env.HOME, '.pebble-dev', APP_UUID, 'localStorage.json');
  }
  try {
    fs.mkdirSync(path.dirname(lsFile), { recursive: true });
    fs.writeFileSync(lsFile, JSON.stringify(storageData, null, 2));
    console.log('Settings persisted to', lsFile);
  } catch (e) {
    console.warn('Could not write localStorage file:', e.message);
  }
}

function refreshWatch() {
  var key = MSG_KEY_INDEX.tide_status;
  if (key === undefined) return;
  var args = ['send-app-message', '--emulator', 'emery', '--string', key + '=Refreshing tide data...'];
  try {
    child_process.execFileSync('pebble', args, { cwd: PROJECT_ROOT, timeout: 5000 });
    console.log('Watch refresh requested.');
  } catch (e) {
    console.warn('Watch refresh failed (selection saved for next launch):', e.message.slice(0, 120));
  }
}

// ---- HTTP server ----
var serverTimer = null;
var server = http.createServer(function(req, res) {
  var parsed = new URL(req.url, 'http://127.0.0.1');

  if (parsed.pathname === '/save') {
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

    // Persist selected location
    if (settings.mode === 'gps' || settings.usePhoneLocation) {
      delete storageData[SELECTED_LOCATION_KEY];
      console.log('GPS mode: cleared manual location');
    } else if (settings.mode === 'manual' && typeof settings.lat === 'number') {
      // New state format
      var locParts = (settings.location || '').split(', ');
      storageData[SELECTED_LOCATION_KEY] = JSON.stringify({
        latitude: settings.lat,
        longitude: settings.lon,
        name: locParts[0] || settings.location || 'Unknown',
        admin1: locParts[1] || '',
        country: locParts[2] || '',
      });
    } else if (typeof settings.latitude === 'number') {
      // Legacy format
      storageData[SELECTED_LOCATION_KEY] = JSON.stringify(settings);
    }

    // Persist units/clock preferences
    try {
      var existing = JSON.parse(storageData['tidepebble.settings'] || '{}');
      if (settings.units) existing.units = settings.units;
      if (settings.clock) existing.clock = settings.clock;
      storageData['tidepebble.settings'] = JSON.stringify(existing);
    } catch(e) {}

    persistSettings();
    refreshWatch();

    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    res.end([
      '<!doctype html><html><head><meta charset="utf-8">',
      '<style>body{font-family:Helvetica,Arial,sans-serif;margin:40px;background:#f5f5f5;text-align:center;}',
      'h1{color:#111;font-size:24px;}p{color:#555;font-size:16px;}</style></head><body>',
      '<h1>Settings saved</h1>',
      '<p>You may close this tab.</p>',
      '</body></html>',
    ].join(''));

    if (serverTimer) clearTimeout(serverTimer);
    setTimeout(function() { server.close(); }, 2000);
    return;
  }

  // Serve settings HTML with current state as query params
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
  var returnTo = encodeURIComponent(base + '/save?data=');

  // Build query string from current state
  var state = getCurrentState();
  var params = [
    'return_to=' + returnTo,
    'mode=' + encodeURIComponent(state.mode),
    'location=' + encodeURIComponent(state.location),
    'units=' + encodeURIComponent(state.units),
    'clock=' + encodeURIComponent(state.clock),
  ];
  if (state.lat != null) params.push('lat=' + state.lat);
  if (state.lon != null) params.push('lon=' + state.lon);

  var configUrl = base + '/?' + params.join('&');

  try {
    child_process.execSync('open -a "Brave Browser" ' + JSON.stringify(configUrl));
    console.log('Config server: http://127.0.0.1:' + port);
    console.log('Current state:', JSON.stringify(state));
  } catch (e) {
    console.error('Could not open Brave:', e.message);
    server.close();
    process.exit(1);
  }
});
