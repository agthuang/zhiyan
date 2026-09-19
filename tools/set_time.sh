#!/usr/bin/env bash
# Push Mac wall clock to Zhiyan Receiver over CDC (UTC epoch).
# The receiver then forwards the time to the paired handheld.
#
# Usage:
#   ./tools/set_time.sh                  # once, auto-find CDC
#   ./tools/set_time.sh /dev/cu.usbmodem00011
#   ./tools/set_time.sh --watch          # keep syncing whenever the receiver is plugged in
set -euo pipefail

WATCH=0
PORT="${1:-}"
if [[ "${PORT}" == "--watch" || "${PORT}" == "-w" ]]; then
  WATCH=1
  PORT="${2:-}"
fi

find_port() {
  local p
  # Prefer the historical receiver CDC suffix, then any usbmodem.
  for p in /dev/cu.usbmodem00011 /dev/cu.usbmodem*; do
    [[ -e "$p" ]] || continue
    # Skip handheld USB-Serial/JTAG if both are present: receiver CDC is usually *00011
    # or appears alongside a Zhiyan Receiver product. When unsure, try every candidate.
    echo "$p"
    return 0
  done
  return 1
}

sync_once() {
  local port="$1"
  local ts
  ts="$(date +%s)"
  if ! printf 'T%s\n' "$ts" >"$port" 2>/dev/null; then
    echo "write failed → $port" >&2
    return 1
  fi
  echo "Sent T$ts → $port (receiver will push to handheld if paired)"
}

if [[ "$WATCH" -eq 1 ]]; then
  echo "Watching for Zhiyan Receiver CDC… (Ctrl+C to stop)"
  last=""
  while true; do
    if port="$(find_port)"; then
      if [[ "$port" != "$last" ]]; then
        # New plug-in session
        sleep 1
        sync_once "$port" || true
        last="$port"
      else
        # Re-sync every 10 minutes while plugged (drift / laptop sleep)
        sync_once "$port" || true
        sleep 600
        continue
      fi
    else
      last=""
    fi
    sleep 3
  done
fi

if [[ -z "${PORT}" ]]; then
  PORT="$(find_port)" || {
    echo "No CDC port found. Pass explicitly, e.g. $0 /dev/cu.usbmodem00011" >&2
    exit 1
  }
fi
sync_once "$PORT"
