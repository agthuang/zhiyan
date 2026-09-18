#!/usr/bin/env python3
from __future__ import annotations
"""Serve aihook/codex web UI + SSE event stream."""

import argparse
import json
import os
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent
WEB_ROOT = ROOT / "web"
EVENTS_PATH = ROOT / "data" / "events.jsonl"
STATUS_PATH = Path(os.path.expanduser("~/.codex/agent-status.json"))


def _read_status() -> dict:
    try:
        return json.loads(STATUS_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


class Handler(BaseHTTPRequestHandler):
    server_version = "AIHookCodex/0.1"

    def log_message(self, fmt: str, *args) -> None:
        sys_stderr = __import__("sys").stderr
        print(f"[serve] {self.address_string()} {fmt % args}", file=sys_stderr, flush=True)

    def _send(self, code: int, body: bytes, content_type: str, extra: dict | None = None) -> None:
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        if extra:
            for k, v in extra.items():
                self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        path = parsed.path

        if path == "/api/status":
            body = json.dumps(_read_status(), ensure_ascii=False).encode("utf-8")
            self._send(200, body, "application/json; charset=utf-8")
            return

        if path == "/api/events":
            qs = parse_qs(parsed.query)
            try:
                limit = max(1, min(500, int(qs.get("limit", ["100"])[0])))
            except ValueError:
                limit = 100
            lines: list[str] = []
            if EVENTS_PATH.exists():
                with EVENTS_PATH.open("r", encoding="utf-8") as f:
                    lines = f.readlines()[-limit:]
            events = []
            for line in lines:
                line = line.strip()
                if not line:
                    continue
                try:
                    events.append(json.loads(line))
                except json.JSONDecodeError:
                    continue
            body = json.dumps(events, ensure_ascii=False).encode("utf-8")
            self._send(200, body, "application/json; charset=utf-8")
            return

        if path == "/events":
            self._sse()
            return

        self._static(path)

    def _static(self, path: str) -> None:
        if path == "/":
            path = "/index.html"
        # Prevent path escape
        rel = path.lstrip("/")
        target = (WEB_ROOT / rel).resolve()
        if not str(target).startswith(str(WEB_ROOT.resolve())) or not target.is_file():
            self._send(404, b"not found", "text/plain; charset=utf-8")
            return
        data = target.read_bytes()
        ctype = {
            ".html": "text/html; charset=utf-8",
            ".js": "text/javascript; charset=utf-8",
            ".css": "text/css; charset=utf-8",
            ".svg": "image/svg+xml",
            ".png": "image/png",
            ".json": "application/json",
        }.get(target.suffix.lower(), "application/octet-stream")
        self._send(200, data, ctype)

    def _sse(self) -> None:
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.send_header("X-Accel-Buffering", "no")
        self.end_headers()

        # Start from EOF so only new events stream; client loads history via /api/events.
        offset = EVENTS_PATH.stat().st_size if EVENTS_PATH.exists() else 0
        last_status_payload = ""
        try:
            while True:
                if EVENTS_PATH.exists():
                    size = EVENTS_PATH.stat().st_size
                    if size < offset:
                        offset = 0  # truncated / rotated
                    if size > offset:
                        with EVENTS_PATH.open("r", encoding="utf-8") as f:
                            f.seek(offset)
                            chunk = f.read()
                            offset = f.tell()
                        for line in chunk.splitlines():
                            line = line.strip()
                            if not line:
                                continue
                            payload = f"event: hook\ndata: {line}\n\n".encode("utf-8")
                            self.wfile.write(payload)
                        self.wfile.flush()

                status = _read_status()
                status_payload = json.dumps(status, ensure_ascii=False)
                if status_payload != last_status_payload:
                    last_status_payload = status_payload
                    msg = f"event: status\ndata: {status_payload}\n\n".encode("utf-8")
                    self.wfile.write(msg)
                    self.wfile.flush()

                # heartbeat
                self.wfile.write(b": ping\n\n")
                self.wfile.flush()
                time.sleep(0.4)
        except (BrokenPipeError, ConnectionResetError, OSError):
            return


def main() -> None:
    parser = argparse.ArgumentParser(description="AI Hook Codex live event server")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8777)
    args = parser.parse_args()

    EVENTS_PATH.parent.mkdir(parents=True, exist_ok=True)
    if not EVENTS_PATH.exists():
        EVENTS_PATH.touch()

    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"AI Hook Codex UI → http://{args.host}:{args.port}/", flush=True)
    print(f"SSE stream       → http://{args.host}:{args.port}/events", flush=True)
    print(f"Events file      → {EVENTS_PATH}", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nbye", flush=True)
    finally:
        httpd.server_close()


if __name__ == "__main__":
    main()
