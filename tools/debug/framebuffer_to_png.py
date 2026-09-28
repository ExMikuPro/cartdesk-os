#!/usr/bin/env python3
"""Convert and compare CartDesk ARGB8888 framebuffer captures.

The STM32H743 is little-endian and LVGL's ARGB8888 value is 0xAARRGGBB,
so framebuffer bytes are B, G, R, A.  The trace-only five-color strip at
y=0 is used to reject an incorrect channel interpretation before analysis.
Only the Python standard library is required.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import struct
import zlib
from pathlib import Path

WIDTH = 800
HEIGHT = 480
BPP = 4
EXPECTED_SIZE = WIDTH * HEIGHT * BPP
ICON_Y = 26 + 80
ICON_SIZE = 200
ICON_PITCH = 220
ICON_FIRST_X = 20


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))


def write_png(path: Path, width: int, height: int, rgba: bytes) -> None:
    if len(rgba) != width * height * 4:
        raise ValueError(f"RGBA size mismatch for {path}")
    rows = bytearray()
    stride = width * 4
    for y in range(height):
        rows.append(0)  # PNG filter: None
        rows.extend(rgba[y * stride:(y + 1) * stride])
    payload = b"\x89PNG\r\n\x1a\n"
    payload += png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    payload += png_chunk(b"IDAT", zlib.compress(bytes(rows), 6))
    payload += png_chunk(b"IEND", b"")
    path.write_bytes(payload)


def load_raw(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) != EXPECTED_SIZE:
        raise ValueError(f"{path}: expected {EXPECTED_SIZE} bytes, got {len(data)}")
    if data == bytes(EXPECTED_SIZE) or data == b"\xFF" * EXPECTED_SIZE:
        raise ValueError(f"{path}: capture is uniformly empty")
    return data


def bgra_to_rgba(raw: bytes, opaque: bool = False) -> bytes:
    rgba = bytearray(len(raw))
    for offset in range(0, len(raw), 4):
        b, g, r, a = raw[offset:offset + 4]
        rgba[offset:offset + 4] = bytes((r, g, b, 255 if opaque else a))
    return bytes(rgba)


def pixel_rgb(raw: bytes, x: int, y: int) -> tuple[int, int, int]:
    offset = (y * WIDTH + x) * 4
    b, g, r = raw[offset:offset + 3]
    return r, g, b


def verify_calibration(raw: bytes) -> list[dict[str, object]]:
    expected = [
        ("red", (255, 0, 0)),
        ("green", (0, 255, 0)),
        ("blue", (0, 0, 255)),
        ("white", (255, 255, 255)),
        ("black", (0, 0, 0)),
    ]
    result = []
    for index, (name, wanted) in enumerate(expected):
        actual = pixel_rgb(raw, index * 4 + 2, 2)
        result.append({"name": name, "expected": wanted, "actual": actual,
                       "pass": actual == wanted})
    if not all(item["pass"] for item in result):
        raise ValueError(f"BGRA channel calibration failed: {result}")
    return result


def crop_rgba(rgba: bytes, x: int, y: int, width: int, height: int) -> bytes:
    output = bytearray(width * height * 4)
    source_stride = WIDTH * 4
    target_stride = width * 4
    for row in range(height):
        source = (y + row) * source_stride + x * 4
        target = row * target_stride
        output[target:target + target_stride] = rgba[source:source + target_stride]
    return bytes(output)


def displacement(raw_a: bytes, raw_b: bytes, slot: int, scroll_a: int,
                 search: range) -> dict[str, object] | None:
    x_a = ICON_FIRST_X + slot * ICON_PITCH - scroll_a
    if x_a < 0 or x_a + ICON_SIZE > WIDTH:
        return None
    best: tuple[int, int, int] | None = None
    samples = 0
    for dx in search:
        x_b = x_a + dx
        if x_b < 0 or x_b + ICON_SIZE > WIDTH:
            continue
        sad = 0
        exact = 0
        count = 0
        # A two-pixel grid keeps the pure-Python search quick while preserving
        # the strong 200x200 icon signal.
        for y in range(ICON_Y, ICON_Y + ICON_SIZE, 2):
            for x in range(0, ICON_SIZE, 2):
                a = (y * WIDTH + x_a + x) * 4
                b = (y * WIDTH + x_b + x) * 4
                pa = raw_a[a:a + 3]
                pb = raw_b[b:b + 3]
                sad += abs(pa[0] - pb[0]) + abs(pa[1] - pb[1]) + abs(pa[2] - pb[2])
                exact += pa == pb
                count += 1
        candidate = (sad, -exact, dx)
        if best is None or candidate < best:
            best = candidate
            samples = count
    if best is None:
        return None
    return {
        "slot": slot,
        "best_dx": best[2],
        "sad": best[0],
        "exact_match_ratio": (-best[1] / samples) if samples else 0.0,
        "sample_count": samples,
    }


def compare(raw_a: bytes, raw_b: bytes) -> tuple[dict[str, object], bytes, list[int], list[int]]:
    row_counts = [0] * HEIGHT
    column_counts = [0] * WIDTH
    diff_rgba = bytearray(EXPECTED_SIZE)
    changed = 0
    min_x, min_y = WIDTH, HEIGHT
    max_x = max_y = -1
    for y in range(HEIGHT):
        for x in range(WIDTH):
            offset = (y * WIDTH + x) * 4
            pa = raw_a[offset:offset + 4]
            pb = raw_b[offset:offset + 4]
            if pa == pb:
                diff_rgba[offset:offset + 4] = b"\x00\x00\x00\xFF"
                continue
            changed += 1
            row_counts[y] += 1
            column_counts[x] += 1
            min_x, min_y = min(min_x, x), min(min_y, y)
            max_x, max_y = max(max_x, x), max(max_y, y)
            intensity = max(abs(pa[channel] - pb[channel]) for channel in range(4))
            diff_rgba[offset:offset + 4] = bytes((intensity, 0, 0, 255))
    bbox = None if changed == 0 else [min_x, min_y, max_x, max_y]
    peak_y = max(range(HEIGHT), key=row_counts.__getitem__)
    return ({"changed_pixels": changed, "diff_bbox": bbox,
             "row_change_peak_y": peak_y,
             "row_change_peak_pixels": row_counts[peak_y]},
            bytes(diff_rgba), row_counts, column_counts)


def write_row_profile(path: Path, row_counts: list[int]) -> None:
    rgba = bytearray(WIDTH * HEIGHT * 4)
    for y, count in enumerate(row_counts):
        bar = min(WIDTH, count)
        for x in range(WIDTH):
            offset = (y * WIDTH + x) * 4
            rgba[offset:offset + 4] = b"\xFF\x30\x30\xFF" if x < bar else b"\x10\x10\x10\xFF"
    write_png(path, WIDTH, HEIGHT, bytes(rgba))


def analyze(args: argparse.Namespace) -> None:
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    crops = output / "crops"
    crops.mkdir(exist_ok=True)
    raw_a = load_raw(args.fb_a)
    raw_b = load_raw(args.fb_b)
    metadata = json.loads(args.metadata.read_text()) if args.metadata else {}
    calibration = {"fb_a": verify_calibration(raw_a), "fb_b": verify_calibration(raw_b)}

    rgba_a = bgra_to_rgba(raw_a)
    rgba_b = bgra_to_rgba(raw_b)
    write_png(output / "fb_a_raw_alpha.png", WIDTH, HEIGHT, rgba_a)
    write_png(output / "fb_b_raw_alpha.png", WIDTH, HEIGHT, rgba_b)
    write_png(output / "fb_a_opaque.png", WIDTH, HEIGHT, bgra_to_rgba(raw_a, True))
    write_png(output / "fb_b_opaque.png", WIDTH, HEIGHT, bgra_to_rgba(raw_b, True))

    stats, diff_rgba, row_counts, column_counts = compare(raw_a, raw_b)
    write_png(output / "diff.png", WIDTH, HEIGHT, diff_rgba)
    write_row_profile(output / "row_diff.png", row_counts)
    with (output / "row_diff.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("row", "changed_pixels"))
        writer.writerows(enumerate(row_counts))
    with (output / "column_diff.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("column", "changed_pixels"))
        writer.writerows(enumerate(column_counts))

    scroll_a = int(metadata.get("fb_a_scroll_x", metadata.get("scroll_x", 0)))
    scroll_b = int(metadata.get("fb_b_scroll_x", metadata.get("scroll_x", 0)))
    displacements = []
    for slot in range(12):
        x_a = ICON_FIRST_X + slot * ICON_PITCH - scroll_a
        x_b = ICON_FIRST_X + slot * ICON_PITCH - scroll_b
        if 0 <= x_a and x_a + ICON_SIZE <= WIDTH:
            write_png(crops / f"fb_a_icon_{slot}.png", ICON_SIZE, ICON_SIZE,
                      crop_rgba(rgba_a, x_a, ICON_Y, ICON_SIZE, ICON_SIZE))
        if 0 <= x_b and x_b + ICON_SIZE <= WIDTH:
            write_png(crops / f"fb_b_icon_{slot}.png", ICON_SIZE, ICON_SIZE,
                      crop_rgba(rgba_b, x_b, ICON_Y, ICON_SIZE, ICON_SIZE))
        item = displacement(raw_a, raw_b, slot, scroll_a, range(-64, 65))
        if item is not None:
            item["expected_screen_dx"] = scroll_a - scroll_b
            displacements.append(item)

    for cut in (120, 180, 240, 300, 360):
        split = cut * WIDTH * 4
        composite = rgba_a[:split] + rgba_b[split:]
        write_png(output / f"reconstructed_tear_y{cut}.png", WIDTH, HEIGHT, composite)

    analysis = {
        **metadata,
        **stats,
        "raw_sha256": {
            "fb_a": hashlib.sha256(raw_a).hexdigest(),
            "fb_b": hashlib.sha256(raw_b).hexdigest(),
        },
        "raw_size": {"fb_a": len(raw_a), "fb_b": len(raw_b)},
        "byte_order": "BGRA bytes (little-endian LVGL 0xAARRGGBB)",
        "calibration": calibration,
        "icon_displacements": displacements,
    }
    (output / "analysis.json").write_text(json.dumps(analysis, indent=2) + "\n")
    lines = [
        f"# Framebuffer analysis: {metadata.get('label', output.name)}",
        "",
        f"- FB_A raw SHA-256: `{analysis['raw_sha256']['fb_a']}`",
        f"- FB_B raw SHA-256: `{analysis['raw_sha256']['fb_b']}`",
        f"- Raw size: `{len(raw_a)}` bytes each",
        f"- Changed pixels: `{stats['changed_pixels']}`",
        f"- Difference bounding box: `{stats['diff_bbox']}`",
        f"- Peak changed row: `{stats['row_change_peak_y']}` "
        f"(`{stats['row_change_peak_pixels']}` pixels)",
        f"- Channel calibration: `PASS` (BGRA memory to RGBA PNG)",
        "",
        "| Slot | Expected screen dx | Measured dx | Exact match | SAD |",
        "|---:|---:|---:|---:|---:|",
    ]
    for item in displacements:
        lines.append(f"| {item['slot']} | {item['expected_screen_dx']} | {item['best_dx']} | "
                     f"{item['exact_match_ratio']:.4f} | {item['sad']} |")
    (output / "summary.md").write_text("\n".join(lines) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fb-a", type=Path, required=True)
    parser.add_argument("--fb-b", type=Path, required=True)
    parser.add_argument("--metadata", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    analyze(parser.parse_args())


if __name__ == "__main__":
    main()
