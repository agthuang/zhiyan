#!/usr/bin/env bash
# Build + flash handheld firmware. Flash size follows the chip (4/8/16MB),
# or pass it explicitly: ./tools/flash_handheld.sh /dev/cu.usbmodemXXX [4MB|8MB|16MB]
# See docs/flashing.md §4.0.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HH="$ROOT/firmware/handheld"
ESP="${HOME}/.platformio/penv/bin/esptool"
PIO="${HOME}/.platformio/penv/bin/pio"
PY="${HOME}/.platformio/penv/bin/python"
ESPTOOL_PY="${HOME}/.platformio/packages/tool-esptoolpy/esptool.py"
PORT="${1:-}"
SIZE_ARG="${2:-}"

if [[ -z "$PORT" ]]; then
  echo "Usage: $0 /dev/cu.usbmodemXXXX [4MB|8MB|16MB]" >&2
  ls /dev/cu.usbmodem* 2>/dev/null || true
  exit 1
fi

detect_size() {
  local out
  if [[ -x "$ESP" ]]; then
    out="$("$ESP" --chip esp32s3 -p "$PORT" flash_id 2>&1 || true)"
  else
    out="$("$PY" "$ESPTOOL_PY" --chip esp32s3 -p "$PORT" flash_id 2>&1 || true)"
  fi
  if [[ "$out" =~ Detected\ flash\ size:\ ([0-9]+MB) ]]; then
    echo "${BASH_REMATCH[1]}"
    return 0
  fi
  return 1
}

normalize_size() {
  case "$1" in
    4|4MB|4mb) echo 4MB ;;
    8|8MB|8mb) echo 8MB ;;
    16|16MB|16mb) echo 16MB ;;
    *) return 1 ;;
  esac
}

if [[ -n "$SIZE_ARG" ]]; then
  SIZE="$(normalize_size "$SIZE_ARG")" || {
    echo "Unknown flash size '$SIZE_ARG' (use 4MB, 8MB, or 16MB)" >&2
    exit 1
  }
else
  SIZE="$(detect_size)" || {
    echo "Could not detect flash size on $PORT; pass 4MB|8MB|16MB explicitly." >&2
    exit 1
  }
fi

case "$SIZE" in
  4MB) ENV=flash_4mb ;;
  8MB) ENV=flash_8mb ;;
  16MB) ENV=flash_16mb ;;
esac

echo "Port $PORT → flash $SIZE (PIO env $ENV)"
"$PIO" run -e "$ENV" -d "$HH"

BUILD="$HH/.pio/build/$ENV"
FLASH_TOOL=("$ESP")
if [[ ! -x "$ESP" ]]; then
  FLASH_TOOL=("$PY" "$ESPTOOL_PY")
fi

"${FLASH_TOOL[@]}" --chip esp32s3 -p "$PORT" -b 921600 \
  --before=default_reset --after=hard_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size "$SIZE" \
  0x0 "$BUILD/bootloader.bin" \
  0x8000 "$BUILD/partitions.bin" \
  0x10000 "$BUILD/firmware.bin"

echo "Handheld restored ($SIZE)."
