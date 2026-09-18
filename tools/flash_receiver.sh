#!/usr/bin/env bash
# Flash VibeKey receiver firmware. Put the receiver in download mode first
# (hold BOOT, tap RESET, release BOOT), then run:
#   ./tools/flash_receiver.sh [/dev/cu.usbmodemXXXX]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RX="$ROOT/firmware/receiver"
ESP="${HOME}/.platformio/penv/bin/esptool"
PIO="${HOME}/.platformio/penv/bin/pio"
PORT="${1:-}"

"$PIO" run -e esp32s3 -d "$RX"

if [[ -z "$PORT" ]]; then
  echo "Usage: $0 /dev/cu.usbmodemXXXX" >&2
  echo "Available:" >&2
  ls /dev/cu.usbmodem* 2>/dev/null || true
  exit 1
fi

"$ESP" --chip esp32s3 -p "$PORT" -b 921600 \
  --before=default_reset --after=hard_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 4MB \
  0x0 "$RX/.pio/build/esp32s3/bootloader.bin" \
  0x8000 "$RX/.pio/build/esp32s3/partitions.bin" \
  0x10000 "$RX/.pio/build/esp32s3/firmware.bin"

echo "Done. Open http://localhost:8765 and connect the receiver CDC."
