#!/usr/bin/env python3
"""Reconstruct and compare CartDesk Launcher dual-LTDC-layer hardware pan frames.

Inputs are raw SDRAM dumps taken from the target:

  * HW Layer0 static framebuffer  (800x480 ARGB8888)
  * HW Layer1 Launcher strip      (2660x350 ARGB8888, physical stride 10656)
  * scroll_x                      (0..1860)

The tool rebuilds the panel image exactly the way the LTDC presents it:

    panel = Layer0
    panel[window_y : window_y + window_h, 0:800] = strip[0:window_h, x : x+800]

and reports geometry, edge behaviour and, when a baseline 800x480 LVGL
framebuffer is supplied, a per-pixel difference inside the strip window.

Only the Python standard library is required.

Layer mapping (frozen, see Core/Driver/LCD/ltdc_layer.h):

    HW Layer0 == HAL LayerIdx 0 == LTDC_Layer1 register block (offset 0x84)
    HW Layer1 == HAL LayerIdx 1 == LTDC_Layer2 register block (offset 0x104)
"""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path

PANEL_W = 800
PANEL_H = 480
BPP = 4

STRIP_W = 2660
STRIP_H = 350
STRIP_STRIDE = 10656          # bytes; 10640 aligned up to 32
STRIP_ROW_BYTES = STRIP_W * BPP

STRIP_WINDOW_X = 0
STRIP_WINDOW_Y = 26
STRIP_WINDOW_H = 304          # CIRCLE_Y(330) - BOX_CONTAINER_Y(26)

SCROLL_MAX_X = STRIP_W - PANEL_W   # 1860


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))


def write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    rows = bytearray()
    stride = width * 3
    for y in range(height):
        rows.append(0)
        rows += rgb[y * stride:(y + 1) * stride]
    data = (b"\x89PNG\r\n\x1a\n"
            + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + png_chunk(b"IDAT", zlib.compress(bytes(rows), 6))
            + png_chunk(b"IEND", b""))
    path.write_bytes(data)


def bgra_to_rgb(raw: bytes, count: int) -> bytearray:
    """Framebuffer stores BGRA (little-endian 0xAARRGGBB) -> RGB."""
    out = bytearray(count * 3)
    out[0::3] = raw[2::4][:count]
    out[1::3] = raw[1::4][:count]
    out[2::3] = raw[0::4][:count]
    return out


def load_layer0(path: Path) -> bytes:
    raw = path.read_bytes()
    if len(raw) != PANEL_W * PANEL_H * BPP:
        raise ValueError(f"layer0 size {len(raw)} != {PANEL_W * PANEL_H * BPP}")
    return raw


def load_strip(path: Path) -> bytes:
    raw = path.read_bytes()
    need = STRIP_STRIDE * STRIP_H
    if len(raw) < need:
        raise ValueError(f"strip size {len(raw)} < {need}")
    return raw[:need]


def strip_pixel(strip: bytes, x: int, y: int) -> bytes:
    off = y * STRIP_STRIDE + x * BPP
    return strip[off:off + BPP]


def composite(layer0: bytes, strip: bytes, scroll_x: int) -> bytes:
    """Return the presented panel image as BGRA."""
    panel = bytearray(layer0)

    for row in range(STRIP_WINDOW_H):
        panel_y = STRIP_WINDOW_Y + row
        dst = (panel_y * PANEL_W + STRIP_WINDOW_X) * BPP
        src = row * STRIP_STRIDE + scroll_x * BPP
        panel[dst:dst + PANEL_W * BPP] = strip[src:src + PANEL_W * BPP]

    return bytes(panel)


def check_edges(strip: bytes, scroll_x: int) -> list[str]:
    """Verify the pan never reads padding or runs past the logical strip."""
    problems: list[str] = []

    if not 0 <= scroll_x <= SCROLL_MAX_X:
        problems.append(f"scroll_x {scroll_x} outside [0, {SCROLL_MAX_X}]")

    last_visible = scroll_x + PANEL_W - 1
    if last_visible >= STRIP_W:
        problems.append(f"last visible x {last_visible} >= logical width {STRIP_W}")

    # Right-most visible pixel must still be inside the logical row, i.e. before
    # the 4 padding pixels that only exist to align the physical stride.
    if last_visible * BPP + BPP > STRIP_ROW_BYTES:
        problems.append("pan reads stride padding")

    # Every visible row must stay inside the allocated strip buffer.
    if (STRIP_WINDOW_H - 1) * STRIP_STRIDE + scroll_x * BPP + PANEL_W * BPP > len(strip):
        problems.append("pan reads past strip allocation")

    return problems


def compare(panel: bytes, baseline: bytes, limit: int) -> tuple[int, int, float]:
    """SAD / differing-pixel count / exact ratio inside the strip window."""
    sad = 0
    diff = 0
    total = 0
    for row in range(STRIP_WINDOW_H):
        y = STRIP_WINDOW_Y + row
        base = y * PANEL_W * BPP
        for x in range(PANEL_W):
            off = base + x * BPP
            p = panel[off:off + 3]
            b = baseline[off:off + 3]
            total += 1
            if p != b:
                diff += 1
                sad += sum(abs(p[i] - b[i]) for i in range(3))
    exact = 1.0 - (diff / total) if total else 0.0
    return sad, diff, exact


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layer0", type=Path, required=True)
    parser.add_argument("--strip", type=Path, required=True)
    parser.add_argument("--scroll-x", type=int, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--baseline", type=Path,
                        help="optional 800x480 LVGL framebuffer to diff against")
    args = parser.parse_args()

    layer0 = load_layer0(args.layer0)
    strip = load_strip(args.strip)

    problems = check_edges(strip, args.scroll_x)

    print(f"[ltdc layers] HW Layer0 800x480 @0x{0xD0177000:08X} (static UI)")
    print(f"[ltdc layers] HW Layer1 strip 2660x350 stride={STRIP_STRIDE} @0x{0xD0465000:08X}")
    print(f"[ltdc layers] window x={STRIP_WINDOW_X}..{STRIP_WINDOW_X + PANEL_W - 1} "
          f"y={STRIP_WINDOW_Y}..{STRIP_WINDOW_Y + STRIP_WINDOW_H - 1} "
          f"(h={STRIP_WINDOW_H})")
    print(f"[ltdc layers] scroll_x={args.scroll_x} "
          f"source=0x{0xD0465000 + args.scroll_x * 4:08X} "
          f"visible_x={args.scroll_x}..{args.scroll_x + PANEL_W - 1}")
    print(f"[ltdc layers] stride padding pixels = {STRIP_W - (STRIP_ROW_BYTES // BPP)}")

    if problems:
        for p in problems:
            print(f"[ltdc layers] EDGE FAIL: {p}")
        return 1
    print("[ltdc layers] edge check PASS (no padding read, no overrun, no wrap)")

    panel = composite(layer0, strip, args.scroll_x)

    # Sample pixels around the window seam to prove the window is aligned.
    for y in (STRIP_WINDOW_Y - 1, STRIP_WINDOW_Y, STRIP_WINDOW_Y + STRIP_WINDOW_H - 1,
              STRIP_WINDOW_Y + STRIP_WINDOW_H):
        off = (y * PANEL_W + 400) * BPP
        print(f"[ltdc layers] seam y={y}: BGR={panel[off:off + 3].hex()} "
              f"(layer0 BGR={layer0[off:off + 3].hex()})")

    if args.output:
        rgb = bgra_to_rgb(panel, PANEL_W * PANEL_H)
        write_png(args.output, PANEL_W, PANEL_H, bytes(rgb))
        print(f"[ltdc layers] wrote {args.output}")

    if args.baseline:
        baseline = args.baseline.read_bytes()
        if len(baseline) != PANEL_W * PANEL_H * BPP:
            print("[ltdc layers] baseline size mismatch")
            return 1
        sad, diff, exact = compare(panel, baseline, PANEL_W * STRIP_WINDOW_H)
        print(f"[ltdc layers] vs baseline inside window: "
              f"differing={diff} SAD={sad} exact={exact:.4f}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
