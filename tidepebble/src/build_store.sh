#!/usr/bin/env bash
  set -euo pipefail
  cd "$(dirname "$0")/.."
  pebble build
  zip -d build/tidepebble.pbw 'pebble-js-app.js.map'
  npx terser build/pebble-js-app.js -c -m -o build/pebble-js-app.js
  zip build/tidepebble.pbw build/pebble-js-app.js
  echo "Store build: $(wc -c < build/tidepebble.pbw | tr -d ' ') bytes → build/tidepebble.pbw"

