#!/usr/bin/env node
// Opens TidePebble config in Brave via a local HTTP server.
// Uses the config page's built-in 'return_to' query param to receive saved settings,
// persists them to pypkjs localStorage, and asks the watch to refresh.

var http = require('http');
var fs = require('fs');
var path = require('path');
var child_process = require('child_process');

// Build name-to-numeric-index map from package.json messageKeys.
var pkg = require(path.resolve(__dirname, '..', 'package.json'));
var MSG_KEY_INDEX = {};
(pkg.pebble.messageKeys || []).forEach(function(name, idx) { MSG_KEY_INDEX[name] = idx; });

var SELECTED_LOCATION_KEY = 'tide_selected_location_v1';
var APP_UUID = pkg.pebble.uuid;

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

// ---- Mock Pebble / browser environment ----
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

global.localStorage = {
  getItem: function(k) { return Object.prototype.hasOwnProperty.call(storageData, k) ? storageData[k] : null; },
  setItem: function(k, v) { storageData[k] = String(v); },
  removeItem: function(k) { delete storageData[k]; },
};

var sendAppMessageLog = [];
var eventHandlers = {};

global.Pebble = {
  openURL: function() {},
  sendAppMessage: function(msg) { sendAppMessageLog.push(msg); },
  addEventListener: function(event, handler) { eventHandlers[event] = handler; },
  getTimelineToken: function(_s, fail) { if (fail) fail('not available'); },
};

global.XMLHttpRequest = function() {
  this.open = function() {};
  this.setRequestHeader = function() {};
  this.send = function() {};
  this.onload = null;
  this.onerror = null;
};

// ---- Load app JS ----
var appJs = path.resolve(__dirname, '..', 'build', 'pebble-js-app.js');
if (!fs.existsSync(appJs)) {
  console.error('Error: ' + appJs + ' not found. Run pebble build first.');
  process.exit(1);
}
try {
  require(appJs);
} catch (e) {
  console.error('Error loading app JS:', e.message);
  process.exit(1);
}

// Fire ready so the app initializes its selected-location state.
if (eventHandlers['ready']) {
  try { eventHandlers['ready'](); } catch (e) {}
}

// Fire showConfiguration to capture the config HTML
var capturedUrl = null;
global.Pebble.openURL = function(u) { capturedUrl = u; };
if (eventHandlers['showConfiguration']) {
  try { eventHandlers['showConfiguration'](); } catch (e) {}
}

if (!capturedUrl) {
  console.error('Error: app JS did not call Pebble.openURL()');
  process.exit(1);
}

var html;
if (capturedUrl.indexOf('base64,') !== -1) {
  html = Buffer.from(capturedUrl.split('base64,')[1], 'base64').toString('utf8');
} else if (capturedUrl.startsWith('data:text/html,')) {
  html = decodeURIComponent(capturedUrl.slice('data:text/html,'.length));
} else {
  console.error('Unexpected config URL format:', capturedUrl.slice(0, 40));
  process.exit(1);
}

// ---- Helpers ----
function persistSettings() {
  if (!lsFile) {
    lsFile = path.join(process.env.HOME, '.pebble-dev', APP_UUID, 'localStorage.json');
  }
  try {
    fs.mkdirSync(path.dirname(lsFile), { recursive: true });
    fs.writeFileSync(lsFile, JSON.stringify(storageData, null, 2));
    console.log('Selected location persisted to', lsFile);
  } catch (e) {
    console.warn('Could not write localStorage file:', e.message);
  }
}

function refreshWatch() {
  var key = MSG_KEY_INDEX.tide_status;
  if (key === undefined) {
    return;
  }
  var args = ['send-app-message', '--emulator', 'emery', '--string', key + '=Refreshing tide data...'];
  try {
    child_process.execFileSync('pebble', args, { timeout: 5000 });
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

    // Fire webviewclosed: this updates localStorage in the mock environment.
    sendAppMessageLog = [];
    if (eventHandlers['webviewclosed']) {
      try {
        eventHandlers['webviewclosed']({ response: encodeURIComponent(JSON.stringify(settings)) });
      } catch (e) {
        console.warn('webviewclosed error:', e.message);
      }
    }

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

  // Serve config HTML for all other paths; the page reads ?return_to from location.search.
  res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
  res.end(html);
});

server.listen(0, '127.0.0.1', function() {
  var port = server.address().port;

  // Auto-close after 10 minutes if the user never saves
  serverTimer = setTimeout(function() {
    console.log('Config server closing after 10-minute timeout.');
    server.close();
  }, 600000);

  var base = 'http://127.0.0.1:' + port;
  var returnTo = encodeURIComponent(base + '/save?data=');
  var configUrl = base + '/?return_to=' + returnTo;

  try {
    child_process.execSync('open -a "Brave Browser" ' + JSON.stringify(configUrl));
    console.log('Config server: http://127.0.0.1:' + port);
  } catch (e) {
    console.error('Could not open Brave:', e.message);
    server.close();
    process.exit(1);
  }
});
