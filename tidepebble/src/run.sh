#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR/.."

unset PYTHONPATH
unset PYTHONHOME

# pebble-tool keys running emulators globally by (platform, sdk version), not
# per-project (see pebble_tool/sdk/emulator.py get_emulator_info) — two repos
# both targeting plain "emery" fight over the same instance. Pin a specific
# version so tidepebble always gets its own, independent of whatever other
# Pebble project (e.g. ruckpebble) might be running its own emery emulator
# under a different pinned version alongside this one.
TIDEPEBBLE_SDK_VERSION="4.33"
export PEBBLE_EMULATOR_VERSION="$TIDEPEBBLE_SDK_VERSION"

install_with_timeout() {
  local timeout_s="$1"
  pebble install --emulator emery --sdk "$TIDEPEBBLE_SDK_VERSION" &
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
  echo "Resetting stale Emery emulator state (SDK $TIDEPEBBLE_SDK_VERSION only)..."
  pebble kill 2>/dev/null || true
  # Scoped to this pinned SDK version's paths specifically — a bare
  # "qemu-pebble"/"pypkjs" pattern would also kill another project's emery
  # emulator if it's running under a different pinned version alongside this.
  pkill -f "qemu-pebble.*SDKs/${TIDEPEBBLE_SDK_VERSION}/" 2>/dev/null || true
  pkill -f "pypkjs.*Pebble SDK/${TIDEPEBBLE_SDK_VERSION}/emery" 2>/dev/null || true
  pkill -f "pebble install --emulator emery --sdk ${TIDEPEBBLE_SDK_VERSION}" 2>/dev/null || true
  sleep 2

  local sdk_root="$HOME/Library/Application Support/Pebble SDK/$TIDEPEBBLE_SDK_VERSION"
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
