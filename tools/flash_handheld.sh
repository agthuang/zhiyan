#!/usr/bin/env bash
# Restore handheld firmware. Put the handheld in download mode first.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HH="$ROOT/firmware/handheld"
ESP="${HOME}/.platformio/penv/bin/esptool"
PIO="${HOME}/.platformio/penv/bin/pio"
PORT="${1:-}"

"$PIO" run -e esp32s3 -d "$HH"

if [[ -z "$PORT" ]]; then
  echo "Usage: $0 /dev/cu.usbmodemXXXX" >&2
  ls /dev/cu.usbmodem* 2>/dev/null || true
  exit 1
fi

"$ESP" --chip esp32s3 -p "$PORT" -b 921600 \
  --before=default_reset --after=hard_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x0 "$HH/.pio/build/esp32s3/bootloader.bin" \
  0x8000 "$HH/.pio/build/esp32s3/partitions.bin" \
  0x10000 "$HH/.pio/build/esp32s3/firmware.bin"

echo "Handheld restored."
