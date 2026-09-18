#!/usr/bin/env bash
# Push Mac wall clock to VibeKey Receiver over CDC (UTC epoch).
set -euo pipefail
PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  for p in /dev/cu.usbmodem00011 /dev/cu.usbmodem*; do
    [[ -e "$p" ]] || continue
    # Prefer the receiver CDC (historically *00011); skip handheld JTAG-ish ports if both exist.
    if [[ "$p" == *00011* ]] || [[ -z "${PORT:-}" ]]; then
      PORT="$p"
    fi
  done
fi
if [[ -z "${PORT:-}" || ! -e "$PORT" ]]; then
  echo "No CDC port found. Pass explicitly, e.g. $0 /dev/cu.usbmodem00011" >&2
  exit 1
fi
TS="$(date +%s)"
printf 'T%s\n' "$TS" > "$PORT"
echo "Sent T$TS → $PORT"
