#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR/.."

unset PYTHONPATH
unset PYTHONHOME

# Same pin as run.sh — keeps this script's emulators independent of any
# other Pebble project's, and independent of run.sh's own emulator instance.
TIDEPEBBLE_SDK_VERSION="4.33"
export PEBBLE_EMULATOR_VERSION="$TIDEPEBBLE_SDK_VERSION"

OUT_DIR="store_assets"
PAGES=(overview now then later)
PLATFORMS=(emery gabbro)

install_with_timeout() {
  local platform="$1" timeout_s="$2"
  pebble install --emulator "$platform" --sdk "$TIDEPEBBLE_SDK_VERSION" &
  local install_pid=$!

  for ((i = 0; i < timeout_s; i += 1)); do
    if ! kill -0 "$install_pid" 2>/dev/null; then
      wait "$install_pid"
      return $?
    fi
    sleep 1
  done

  echo "Install timed out after ${timeout_s}s."
  kill "$install_pid" 2>/dev/null || true
  wait "$install_pid" 2>/dev/null || true
  return 124
}

reset_emulator_state() {
  local platform="$1"
  echo "Resetting stale $platform emulator state (SDK $TIDEPEBBLE_SDK_VERSION only)..."
  pebble kill 2>/dev/null || true
  pkill -f "qemu-pebble.*SDKs/${TIDEPEBBLE_SDK_VERSION}/" 2>/dev/null || true
  pkill -f "pypkjs.*Pebble SDK/${TIDEPEBBLE_SDK_VERSION}/${platform}" 2>/dev/null || true
  pkill -f "pebble install --emulator ${platform} --sdk ${TIDEPEBBLE_SDK_VERSION}" 2>/dev/null || true
  sleep 2

  local sdk_root="$HOME/Library/Application Support/Pebble SDK/$TIDEPEBBLE_SDK_VERSION"
  local flash
  while IFS= read -r flash; do
    mv "$flash" "$flash.bak-$(date +%Y%m%d-%H%M%S)"
  done < <(find "$sdk_root" -path "*/${platform}/qemu_spi_flash.bin" -print 2>/dev/null)
}

install_app() {
  local platform="$1"
  echo "Starting $platform emulator..."
  if ! install_with_timeout "$platform" 45; then
    reset_emulator_state "$platform"
    echo "Retrying install with clean emulator state..."
    install_with_timeout "$platform" 60
  fi
}

mkdir -p "$OUT_DIR"

pebble build

for platform in "${PLATFORMS[@]}"; do
  install_app "$platform"

  # Let pkjs finish its readyForm handshake and initial tide-data AppMessage
  # before the first capture — otherwise the Overview chart is still blank.
  sleep 8

  # App launches on the Now page (see s_page init in tidepebble.c). Up
  # walks back to Overview (its lower bound), then down steps forward
  # through Now/Then/Later in enum order — one pass covers all four pages.
  pebble emu-button click up --emulator "$platform" --sdk "$TIDEPEBBLE_SDK_VERSION"
  sleep 1

  for page in "${PAGES[@]}"; do
    out_file="$OUT_DIR/${platform}_${page}.png"
    echo "Capturing $out_file"
    pebble screenshot --emulator "$platform" --sdk "$TIDEPEBBLE_SDK_VERSION" \
      --no-open "$out_file"
    if [[ "$page" != "later" ]]; then
      pebble emu-button click down --emulator "$platform" --sdk "$TIDEPEBBLE_SDK_VERSION"
      sleep 1
    fi
  done
done

echo "Done. Screenshots in $OUT_DIR/"
