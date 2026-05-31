#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

unset PYTHONPATH
unset PYTHONHOME

pebble logs --emulator emery
