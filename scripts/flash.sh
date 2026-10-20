#!/usr/bin/env bash
# Build, upload and reset the LoRa32 in one go.
#
# Usage:
#   scripts/flash.sh [env] [port]
#     env   PlatformIO env (default ttgo-lora32-v21-mqtt)
#     port  serial port (default /dev/ttyACM0)
#
# Examples:
#   scripts/flash.sh
#   scripts/flash.sh ttgo-lora32-v21-sniffer
#
# Uses .venv/bin/pio if present, otherwise pio from PATH. The upload builds first;
# esptool hard-resets the board via RTS when the upload finishes.
set -euo pipefail

ENV=${1:-ttgo-lora32-v21-mqtt}
PORT=${2:-/dev/ttyACM0}

cd "$(dirname "$0")/.."

if [[ -x .venv/bin/pio ]]; then PIO=.venv/bin/pio
elif command -v pio >/dev/null; then PIO=pio
else echo "pio not found: python3 -m venv .venv && .venv/bin/pip install platformio" >&2; exit 1
fi

[[ -e $PORT ]] || { echo "$PORT not found, is the board connected?" >&2; exit 1; }
[[ -f extern/LoRa32/library.json ]] || git submodule update --init

"$PIO" run -e "$ENV" -t upload --upload-port "$PORT"
