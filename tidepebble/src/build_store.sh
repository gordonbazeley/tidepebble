#!/usr/bin/env bash
  set -euo pipefail
  cd "$(dirname "$0")"
  pebble build
  zip -d build/ruckpebble.pbw 'pebble-js-app.js.map'
  npx terser build/pebble-js-app.js -c -m -o build/pebble-js-app.js
  zip build/ruckpebble.pbw build/pebble-js-app.js
  echo "Store build: $(wc -c < build/ruckpebble.pbw | tr -d ' ') bytes → build/ruckpebble.pbw"

