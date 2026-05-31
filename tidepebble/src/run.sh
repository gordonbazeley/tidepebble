#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

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

  local emery_dir="$HOME/Library/Application Support/Pebble SDK/4.9.169/emery"
  local flash="$emery_dir/qemu_spi_flash.bin"
  if [[ -f "$flash" ]]; then
    mv "$flash" "$flash.bak-$(date +%Y%m%d-%H%M%S)"
  fi
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

pebble emu-app-config --emulator emery &
config_pid=$!
for _ in {1..10}; do
  if ! kill -0 "$config_pid" 2>/dev/null; then
    wait "$config_pid"
    exit $?
  fi
  sleep 1
done

echo "App installed. Config browser did not finish opening; leaving emulator running."
kill "$config_pid" 2>/dev/null || true
