#!/usr/bin/env python3
"""Pack wait-pup GIF into firmware/components/common/src/vk_pup_sprite.inc (4bpp)."""
from pathlib import Path
from PIL import Image, ImageSequence

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "docs/ui-assets/wait-pup.gif"
OUT = ROOT / "firmware/components/common/src/vk_pup_sprite.inc"
TARGET_H = 56
NCOL = 15
N_FRAMES_KEEP = 10

def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def extract_frames(im):
    frames = []
    for frame in ImageSequence.Iterator(im):
        f = frame.convert("RGBA")
        composed = Image.new("RGBA", im.size, (0, 0, 0, 255))
        layer = Image.new("RGBA", im.size, (0, 0, 0, 0))
        layer.paste(f, (0, 0), f)
        composed.alpha_composite(layer)
        frames.append(composed)
    return frames

def content_bbox(imgs):
    minx, miny = imgs[0].size[0], imgs[0].size[1]
    maxx, maxy = 0, 0
    for img in imgs:
        px = img.load()
        w, h = img.size
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                if a > 20 and (r + g + b) > 40:
                    minx = min(minx, x); miny = min(miny, y)
                    maxx = max(maxx, x); maxy = max(maxy, y)
    pad = 2
    return (max(0, minx - pad), max(0, miny - pad),
            min(imgs[0].size[0], maxx + 1 + pad), min(imgs[0].size[1], maxy + 1 + pad))

def main():
    im = Image.open(SRC)
    raw = extract_frames(im)
    idxs = [int(round(i * (len(raw) - 1) / (N_FRAMES_KEEP - 1))) for i in range(N_FRAMES_KEEP)]
    raw = [raw[i] for i in idxs]
    box = content_bbox(raw)
    cropped = [fr.crop(box) for fr in raw]
    cw, ch = cropped[0].size
    tw = max(1, int(round(cw * TARGET_H / ch)))
    if tw % 2:
        tw += 1
    th = TARGET_H
    scaled = [fr.resize((tw, th), Image.Resampling.LANCZOS) for fr in cropped]

    pixels = []
    for fr in scaled:
        px = fr.load()
        for y in range(th):
            for x in range(tw):
                r, g, b, a = px[x, y]
                if a < 40 or (r + g + b) < 35:
                    continue
                pixels.append((r, g, b))
    swatch = Image.new("RGB", (len(pixels), 1))
    swatch.putdata(pixels)
    q = swatch.quantize(colors=NCOL - 1, method=Image.Quantize.MEDIANCUT)
    pal = q.getpalette()[: (NCOL - 1) * 3]
    palette_rgb = [(0, 0, 0)]
    palette_565 = [0]
    for i in range(NCOL - 1):
        r, g, b = pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]
        palette_rgb.append((r, g, b))
        palette_565.append(rgb565(r, g, b))

    def nearest(r, g, b):
        best, bd = 1, 1e18
        for i in range(1, len(palette_rgb)):
            pr, pg, pb = palette_rgb[i]
            d = (pr - r) ** 2 + (pg - g) ** 2 + (pb - b) ** 2
            if d < bd:
                bd = d
                best = i
        return best

    frame_bytes = []
    for fr in scaled:
        px = fr.load()
        idxs_pix = []
        for y in range(th):
            for x in range(tw):
                r, g, b, a = px[x, y]
                if a < 40 or (r + g + b) < 35:
                    idxs_pix.append(0)
                else:
                    idxs_pix.append(nearest(r, g, b))
        packed = []
        for i in range(0, len(idxs_pix), 2):
            hi = idxs_pix[i]
            lo = idxs_pix[i + 1] if i + 1 < len(idxs_pix) else 0
            packed.append((hi << 4) | lo)
        frame_bytes.append(packed)

    NF = len(frame_bytes)
    lines = [
        f"/* Wait-pup sprite from docs/ui-assets/wait-pup.gif, {tw}x{th}, {NF} frames, 4bpp */",
        f"#define VK_PUP_W {tw}",
        f"#define VK_PUP_H {th}",
        f"#define VK_PUP_FRAMES {NF}",
        f"#define VK_PUP_NCOL {NCOL}",
        "static const uint16_t vk_pup_pal[VK_PUP_NCOL] = {",
    ]
    for i, c in enumerate(palette_565):
        comma = "," if i + 1 < NCOL else ""
        if i == 0:
            lines.append(f"    0x{c:04X}{comma} /* transparent */")
        else:
            r, g, b = palette_rgb[i]
            lines.append(f"    0x{c:04X}{comma} /* ({r},{g},{b}) */")
    lines.append("};")
    lines.append("static const uint8_t vk_pup_pix[VK_PUP_FRAMES][(VK_PUP_W * VK_PUP_H + 1) / 2] = {")
    for fi, packed in enumerate(frame_bytes):
        lines.append(f"  {{ /* frame {fi} */")
        row = []
        for i, b in enumerate(packed):
            row.append(f"0x{b:02X}")
            if len(row) == 16 or i + 1 == len(packed):
                lines.append("    " + ", ".join(row) + ("," if i + 1 < len(packed) else ""))
                row = []
        lines.append("  }," if fi + 1 < NF else "  }")
    lines.append("};")
    OUT.write_text("\n".join(lines) + "\n")
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes, {NF}x{tw}x{th})")

if __name__ == "__main__":
    main()
