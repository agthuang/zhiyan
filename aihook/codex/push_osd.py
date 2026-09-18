#!/usr/bin/env python3
"""Push colored English OSD words to Zhiyan handheld via receiver CDC.

Examples:
  python3 push_osd.py THINK
  python3 push_osd.py --color '#6EC48C' CODE
  python3 push_osd.py --clear
  python3 push_osd.py --watch
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

import osd_cdc

STATUS_PATH = Path(os.path.expanduser("~/.codex/agent-status.json"))


def read_state() -> str | None:
    try:
        data = json.loads(STATUS_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    st = data.get("state")
    return str(st) if st else None


def watch(interval: float, port: str | None) -> None:
    last = "__unset__"
    print(f"watching {STATUS_PATH} … Ctrl+C to stop")
    while True:
        st = read_state()
        key = st if st is not None else ""
        if key != last:
            last = key
            try:
                reply = osd_cdc.push_state(st, port=port)
                print(f"  state={st!r} {reply.strip() or osd_cdc.command_for_state(st)}")
            except Exception as exc:
                print(f"  state={st!r} skip: {exc}", file=sys.stderr)
        time.sleep(interval)


def main() -> None:
    ap = argparse.ArgumentParser(description="Push status word to Zhiyan handheld OSD")
    ap.add_argument("--port", "-p", help="receiver CDC port (auto-detect if omitted)")
    ap.add_argument("--color", "-c", default="#ECB048", help="#RRGGBB")
    ap.add_argument("--clear", action="store_true")
    ap.add_argument("--watch", action="store_true", help="follow Codex agent-status.json")
    ap.add_argument("--interval", type=float, default=0.4)
    ap.add_argument("word", nargs="?", help="English word to show")
    args = ap.parse_args()

    if args.watch:
        watch(args.interval, args.port)
        return
    if args.clear:
        print(osd_cdc.send_line("O!", port=args.port).strip() or "O!")
        return
    if args.word:
        color = args.color.strip()
        if not color.startswith("#"):
            color = "#" + color
        print(osd_cdc.send_line(f"O {color.upper()} {args.word}", port=args.port).strip())
        return
    ap.error("need WORD, --clear, or --watch")


if __name__ == "__main__":
    main()
