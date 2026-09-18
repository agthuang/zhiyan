#!/usr/bin/env python3
"""Send OSD commands to Zhiyan (知言) receiver CDC. No pyserial required."""
from __future__ import annotations

import glob
import os
import select
import termios
from pathlib import Path

PORT_CACHE = Path(os.path.expanduser("~/.codex/vibekey-cdc-port"))

# Saturated colors so RGB565 on the 0.96" still reads apart.
STATE_MAP = {
    "idle": None,  # clear → clock
    "thinking": ("THINK...", "#FFB020"),  # amber
    "developing": ("WORKING", "#2AD4FF"),  # cyan
    "confirming": ("ASK?", "#FF4D6A"),  # hot pink-red
    "completed": ("INPUT", "#B794F6"),  # lilac — waiting for you
}
ERR = ("ERR", "#FF4D6A")


def command_for_state(state: str | None) -> str:
    if state is None or state == "idle":
        return "O!"
    mapped = STATE_MAP.get(state)
    if mapped is None:
        word, color = ERR
    else:
        word, color = mapped
    return f"O {color} {word}"


def _configure(fd: int) -> None:
    attrs = termios.tcgetattr(fd)
    iflag, oflag, cflag, lflag, _ispeed, _ospeed, cc = attrs
    iflag = 0
    oflag = 0
    lflag = 0
    cflag = termios.CS8 | termios.CREAD | termios.CLOCAL
    if hasattr(termios, "HUPCL"):
        cflag &= ~termios.HUPCL
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 0
    termios.tcsetattr(
        fd,
        termios.TCSANOW,
        [iflag, oflag, cflag, lflag, termios.B115200, termios.B115200, cc],
    )


def _io(port: str, line: str, wait_s: float = 0.35) -> str:
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        _configure(fd)
        try:
            termios.tcflush(fd, termios.TCIOFLUSH)
        except termios.error:
            pass
        os.write(fd, (line.rstrip("\r\n") + "\r\n").encode("ascii", errors="ignore"))
        chunks: list[bytes] = []
        deadline = wait_s
        while deadline > 0:
            ready, _, _ = select.select([fd], [], [], min(0.05, deadline))
            deadline -= 0.05
            if not ready:
                continue
            try:
                data = os.read(fd, 256)
            except BlockingIOError:
                continue
            if data:
                chunks.append(data)
                if b"\n" in data:
                    break
        return b"".join(chunks).decode("utf-8", errors="replace")
    finally:
        os.close(fd)


def looks_like_receiver(reply: str) -> bool:
    return (
        "S=" in reply
        or "O ok" in reply
        or "O cleared" in reply
        or "VibeKey" in reply
        or "Zhiyan" in reply
        or "知言" in reply
        or "O err" in reply
    )


def _read_cache() -> str:
    try:
        return PORT_CACHE.read_text(encoding="utf-8").strip()
    except OSError:
        return ""


def _write_cache(port: str) -> None:
    try:
        PORT_CACHE.parent.mkdir(parents=True, exist_ok=True)
        PORT_CACHE.write_text(port + "\n", encoding="utf-8")
    except OSError:
        pass


def list_candidate_ports() -> list[str]:
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    cached = _read_cache()
    if cached and cached in ports:
        ports = [cached] + [p for p in ports if p != cached]
    elif cached:
        ports = [cached] + ports
    return ports


def find_receiver_port() -> str | None:
    for port in list_candidate_ports():
        try:
            reply = _io(port, "S?", wait_s=0.4)
        except OSError:
            continue
        if looks_like_receiver(reply):
            _write_cache(port)
            return port
    return None


def send_line(line: str, port: str | None = None) -> str:
    """Send CDC line; prefer cached port, retry a few times across candidates."""
    if port:
        last_err: Exception | None = None
        for _ in range(3):
            try:
                reply = _io(port, line)
                if looks_like_receiver(reply) or reply.strip():
                    return reply
            except OSError as exc:
                last_err = exc
        if last_err:
            raise last_err
        raise RuntimeError(f"no reply from {port}")

    last_err = None
    tried: list[str] = []
    for candidate in list_candidate_ports():
        tried.append(candidate)
        for _ in range(2):
            try:
                reply = _io(candidate, line)
            except OSError as exc:
                last_err = exc
                continue
            if looks_like_receiver(reply):
                _write_cache(candidate)
                return reply
            # Some firmwares echo slowly — accept empty after write for O!
            if reply.strip() == "" and line.startswith("O"):
                # one more read attempt via S? to confirm it's the receiver
                try:
                    probe = _io(candidate, "S?", wait_s=0.25)
                except OSError as exc:
                    last_err = exc
                    continue
                if looks_like_receiver(probe):
                    _write_cache(candidate)
                    return _io(candidate, line)
    detail = f"tried {tried}" if tried else "no /dev/cu.usbmodem*"
    if last_err:
        raise RuntimeError(f"no Zhiyan receiver CDC found ({detail}): {last_err}")
    raise RuntimeError(f"no Zhiyan receiver CDC found ({detail})")


def push_state(state: str | None, port: str | None = None) -> str:
    return send_line(command_for_state(state), port=port)
