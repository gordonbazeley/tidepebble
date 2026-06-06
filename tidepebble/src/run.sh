#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR/.."

unset PYTHONPATH
unset PYTHONHOME

install_with_timeout() {
  local timeout_s="$1"
  pebble install --emulator emery &
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
  echo "Resetting stale Emery emulator state..."
  pebble kill 2>/dev/null || true
  pkill -f "qemu-pebble" 2>/dev/null || true
  pkill -f "pypkjs" 2>/dev/null || true
  pkill -f "pebble install --emulator emery" 2>/dev/null || true
  sleep 2

  local sdk_root="$HOME/Library/Application Support/Pebble SDK"
  local flash
  while IFS= read -r flash; do
    mv "$flash" "$flash.bak-$(date +%Y%m%d-%H%M%S)"
  done < <(find "$sdk_root" -path "*/emery/qemu_spi_flash.bin" -print 2>/dev/null)
}

pebble clean
pebble build

# Install to the running emulator, or start one if needed.
echo "Starting emulator..."
if ! install_with_timeout 45; then
  reset_emulator_state
  echo "Retrying install with clean emulator state..."
  install_with_timeout 60
fi

echo "App installed. Opening config in Brave..."
node "$SCRIPT_DIR/open_config.js" &
