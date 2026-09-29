#!/usr/bin/env python3
"""Capture the last audited frame and build a conservative pre-clear coverage map."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WIDTH = 800
HEIGHT = 480


def parse_records(output: str, prefix: str) -> list[dict[str, int]]:
    records: list[dict[str, int]] = []
    for line in output.splitlines():
        if not line.startswith(prefix + " "):
            continue
        records.append({key: int(value, 0)
                        for key, value in re.findall(r"([a-z0-9_]+)=(-?\d+|0x[0-9a-fA-F]+)", line)})
    return records


def fill(mask: bytearray, rect: dict[str, int], increment: bool = False) -> None:
    x1 = max(0, rect["x1"])
    y1 = max(0, rect["y1"])
    x2 = min(WIDTH - 1, rect["x2"])
    y2 = min(HEIGHT - 1, rect["y2"])
    for y in range(y1, y2 + 1):
        start = y * WIDTH + x1
        end = y * WIDTH + x2 + 1
        if not increment:
            mask[start:end] = b"\x01" * (end - start)
        else:
            for index in range(start, end):
                mask[index] = min(255, mask[index] + 1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", type=int, choices=(0, 1, 2, 3), default=0)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--audit", type=Path,
                        help="Parse an existing launcher_full_render_audit GDB transcript")
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    if args.audit:
        output = args.audit.read_text()
    else:
        result = subprocess.run([
            "arm-none-eabi-gdb", "--batch", "-ex", f"set $preclear_mode={args.mode}",
            "-x", "tools/gdb/launcher_full_render_audit.gdb",
        ], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        output = result.stdout
        (args.output / "audit.txt").write_text(output)
        if result.returncode != 0:
            raise SystemExit(f"GDB failed ({result.returncode}); see {args.output / 'audit.txt'}")

    preclear_rects = parse_records(output, "PRECLEAR_RECT")
    draw_rects = parse_records(output, "DRAW_RECT")
    preclear = bytearray(WIDTH * HEIGHT)
    opaque = bytearray(WIDTH * HEIGHT)
    blend = bytearray(WIDTH * HEIGHT)
    draw_count = bytearray(WIDTH * HEIGHT)
    for rect in preclear_rects:
        fill(preclear, rect)
    for rect in draw_rects:
        fill(opaque if rect.get("coverage") == 1 else blend, rect)
        fill(draw_count, rect, increment=True)

    preclear_pixels = sum(preclear)
    opaque_pixels = sum(1 for i, active in enumerate(preclear) if active and opaque[i])
    blend_only_pixels = sum(1 for i, active in enumerate(preclear) if active and not opaque[i] and blend[i])
    untouched_pixels = sum(1 for i, active in enumerate(preclear) if active and not opaque[i] and not blend[i])
    multiple_pixels = sum(1 for i, active in enumerate(preclear) if active and draw_count[i] > 1)
    summary = {
        "mode": args.mode,
        "preclear_rectangles": len(preclear_rects),
        "draw_rectangles": len(draw_rects),
        "preclear_pixels": preclear_pixels,
        "opaque_overwritten_pixels": opaque_pixels,
        "blend_or_unknown_only_pixels": blend_only_pixels,
        "untouched_pixels": untouched_pixels,
        "multiply_covered_pixels": multiple_pixels,
        "opaque_overwritten_percent": 100.0 * opaque_pixels / preclear_pixels if preclear_pixels else 0.0,
    }
    (args.output / "coverage_summary.json").write_text(json.dumps(summary, indent=2) + "\n")

    # PPM colors: black=outside, green=opaque-overwritten, amber=blend/unknown, red=untouched.
    pixels = bytearray()
    for i, active in enumerate(preclear):
        if not active:
            pixels += bytes((0, 0, 0))
        elif opaque[i]:
            pixels += bytes((30, 180, 80))
        elif blend[i]:
            pixels += bytes((230, 160, 30))
        else:
            pixels += bytes((220, 40, 40))
    with (args.output / "coverage_map.ppm").open("wb") as stream:
        stream.write(f"P6\n{WIDTH} {HEIGHT}\n255\n".encode())
        stream.write(pixels)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
