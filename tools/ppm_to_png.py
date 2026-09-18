#!/usr/bin/env python3
"""Convert binary PPM (P6) files to PNG using only the stdlib."""
from __future__ import annotations

import struct
import sys
import zlib
from pathlib import Path


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise ValueError(f"not a P6 ppm: {path}")
    # Skip header lines / comments.
    i = 2
    while True:
        while i < len(data) and data[i] in b" \t\r\n":
            i += 1
        if data[i : i + 1] == b"#":
            while i < len(data) and data[i] not in b"\r\n":
                i += 1
            continue
        break
    end = data.find(b"\n", i)
    dims = data[i:end].decode().split()
    width, height = int(dims[0]), int(dims[1])
    i = end + 1
    while True:
        while i < len(data) and data[i] in b" \t\r\n":
            i += 1
        if data[i : i + 1] == b"#":
            while i < len(data) and data[i] not in b"\r\n":
                i += 1
            continue
        break
    end = data.find(b"\n", i)
    maxval = int(data[i:end].decode().strip())
    if maxval != 255:
        raise ValueError("only maxval 255 supported")
    raw = data[end + 1 :]
    expected = width * height * 3
    if len(raw) < expected:
        raise ValueError("truncated ppm")
    return width, height, raw[:expected]


def write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    rows = []
    stride = width * 3
    for y in range(height):
        rows.append(b"\x00" + rgb[y * stride : (y + 1) * stride])
    compressed = zlib.compress(b"".join(rows), 9)
    png = b"".join(
        [
            b"\x89PNG\r\n\x1a\n",
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)),
            chunk(b"IDAT", compressed),
            chunk(b"IEND", b""),
        ]
    )
    path.write_bytes(png)


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: ppm_to_png.py <file.ppm>...", file=sys.stderr)
        return 2
    for arg in sys.argv[1:]:
        ppm = Path(arg)
        w, h, rgb = read_ppm(ppm)
        out = ppm.with_suffix(".png")
        write_png(out, w, h, rgb)
        print(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
