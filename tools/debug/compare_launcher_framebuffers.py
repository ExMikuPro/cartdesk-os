#!/usr/bin/env python3
"""Phase 5 pixel correctness A/B: native scroll versus slot-local redraw.

Compares the framebuffer dumps captured by
tools/debug/capture_scroll_frames.py --scroll-mode 0 and --scroll-mode 1 at the
same logical Launcher scroll positions (before / mid / after).

The dynamic debug overlay in the top rows carries the captured frame sequence,
so it is reported separately and excluded from the "viewport" metric.

Usage:
  python3 tools/debug/compare_launcher_framebuffers.py \
      --base <session>/ARGB --candidate <session>-slotlocal/ARGB \
      --output build/.../captures/phase5-slot-local/pixel-ab

Only the Python standard library is required.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path

WIDTH = 800
HEIGHT = 480
LABELS = ("before", "mid", "after")
# Dynamic debug overlay: the 4x4 calibration strip plus the frame-sequence marker.
OVERLAY_ROWS = 8
VIEWPORT = (0, 26, WIDTH - 1, 375)


def load_module(path: Path):
    spec = importlib.util.spec_from_file_location("framebuffer_to_png", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def sad_and_match(a: bytes, b: bytes, region, skip_rows: int = 0) -> dict:
    x1, y1, x2, y2 = region
    y1 = max(y1, skip_rows)
    sad = 0
    changed = 0
    total = 0
    for y in range(y1, y2 + 1):
        row = y * WIDTH
        for x in range(x1, x2 + 1):
            offset = (row + x) * 4
            pa = a[offset:offset + 4]
            pb = b[offset:offset + 4]
            total += 1
            if pa == pb:
                continue
            changed += 1
            sad += sum(abs(pa[i] - pb[i]) for i in range(3))
    return {
        "pixels": total,
        "changed_pixels": changed,
        "sad": sad,
        "exact_match_ratio": (total - changed) / total if total else 0.0,
    }


def diff_image(a: bytes, b: bytes, skip_rows: int) -> bytes:
    rgba = bytearray(WIDTH * HEIGHT * 4)
    for y in range(HEIGHT):
        row = y * WIDTH
        for x in range(WIDTH):
            offset = (row + x) * 4
            pa = a[offset:offset + 4]
            pb = b[offset:offset + 4]
            if y < skip_rows or pa == pb:
                rgba[offset:offset + 4] = b"\x10\x10\x10\xff" if y < skip_rows else b"\x00\x00\x00\xff"
                continue
            intensity = max(abs(pa[channel] - pb[channel]) for channel in range(3))
            rgba[offset:offset + 4] = bytes((255, 255 - min(255, intensity), 0, 255))
    return bytes(rgba)


def collect_buffers(module, folder: Path, label: str) -> dict:
    """Map logical scroll position -> the newest framebuffer dump for it.

    DIRECT double buffering means fb_a / fb_b hold different scroll steps; the
    two runs can also land on opposite swap parity, so buffers are matched by
    their recorded Launcher scroll position instead of by name.
    """
    metadata = json.loads((folder / label / "metadata.txt").read_text())
    buffers: dict[int, dict] = {}
    for buffer_name in ("fb_a", "fb_b"):
        scroll = int(metadata.get(f"{buffer_name}_scroll_x", metadata.get("scroll_x", 0)))
        seq = int(metadata.get(f"{buffer_name}_seq", 0))
        raw = module.load_raw(folder / label / f"{buffer_name}.raw")
        previous = buffers.get(scroll)
        if previous is None or seq > previous["seq"]:
            buffers[scroll] = {"name": buffer_name, "seq": seq, "raw": raw}
    return {"metadata": metadata, "buffers": buffers}


def compare_label(module, base: Path, candidate: Path, label: str, output: Path) -> dict:
    collected_base = collect_buffers(module, base, label)
    collected_candidate = collect_buffers(module, candidate, label)
    result: dict = {
        "label": label,
        "pairs": [],
        "metadata": {
            "base": {k: collected_base["metadata"].get(k) for k in
                     ("step", "frame", "fb_a_seq", "fb_b_seq", "fb_a_scroll_x", "fb_b_scroll_x")},
            "candidate": {k: collected_candidate["metadata"].get(k) for k in
                          ("step", "frame", "fb_a_seq", "fb_b_seq", "fb_a_scroll_x", "fb_b_scroll_x")},
        },
    }
    for scroll in sorted(set(collected_base["buffers"]) & set(collected_candidate["buffers"])):
        entry_base = collected_base["buffers"][scroll]
        entry_candidate = collected_candidate["buffers"][scroll]
        raw_base = entry_base["raw"]
        raw_cand = entry_candidate["raw"]
        pair = {
            "scroll_x": scroll,
            "base_buffer": entry_base["name"],
            "candidate_buffer": entry_candidate["name"],
            "calibration": {
                "base": module.verify_calibration(raw_base),
                "candidate": module.verify_calibration(raw_cand),
            },
            "full": sad_and_match(raw_base, raw_cand, (0, 0, WIDTH - 1, HEIGHT - 1)),
            "viewport": sad_and_match(raw_base, raw_cand, VIEWPORT),
            "slot_area": sad_and_match(raw_base, raw_cand, VIEWPORT, skip_rows=OVERLAY_ROWS),
            "overlay": sad_and_match(raw_base, raw_cand, (0, 0, WIDTH - 1, OVERLAY_ROWS - 1)),
            "sha256": {
                "base": module.hashlib.sha256(raw_base).hexdigest(),
                "candidate": module.hashlib.sha256(raw_cand).hexdigest(),
            },
        }
        pair["expected_dx"] = 0
        result["pairs"].append(pair)
        module.write_png(output / f"{label}_scroll{scroll}_diff.png", WIDTH, HEIGHT,
                         diff_image(raw_base, raw_cand, OVERLAY_ROWS))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True,
                        help="capture folder for --scroll-mode 0 (contains before/mid/after)")
    parser.add_argument("--candidate", type=Path, required=True,
                        help="capture folder for --scroll-mode 1")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--control", type=Path, default=None,
                        help="independent repeat of the --base capture; establishes "
                             "the run-to-run noise floor for the same mode")
    args = parser.parse_args()

    module = load_module(Path(__file__).with_name("framebuffer_to_png.py"))
    args.output.mkdir(parents=True, exist_ok=True)

    results = [compare_label(module, args.base, args.candidate, label, args.output)
               for label in LABELS]
    control = ([compare_label(module, args.base, args.control, label, args.output)
                for label in LABELS] if args.control else None)
    (args.output / "pixel_ab.json").write_text(
        json.dumps({"cross_mode": results, "noise_floor_control": control}, indent=2) + "\n")

    lines = ["# Launcher pixel correctness A/B (native scroll vs slot-local)", "",
             "Buffers are matched by recorded Launcher scroll position; `slot_area`",
             "masks the dynamic debug overlay rows 0-7.",
             "",
             "| Capture | scroll_x | base fb | candidate fb | Region | Changed px | SAD | Exact match |",
             "|---|---:|---|---|---|---:|---:|---:|"]
    for entry in results:
        for pair in entry["pairs"]:
            for region in ("full", "viewport", "slot_area"):
                stats = pair[region]
                lines.append(
                    f"| {entry['label']} | {pair['scroll_x']} | {pair['base_buffer']} | "
                    f"{pair['candidate_buffer']} | {region} | {stats['changed_pixels']} | "
                    f"{stats['sad']} | {stats['exact_match_ratio']:.6f} |")
    if control is not None:
        lines += ["", "## Run-to-run noise floor (base mode versus its own repeat)", "",
                  "| Capture | scroll_x | Region | Changed px | SAD | Exact match |",
                  "|---|---:|---|---:|---:|---:|"]
        for entry in control:
            for pair in entry["pairs"]:
                for region in ("full", "slot_area"):
                    stats = pair[region]
                    lines.append(
                        f"| {entry['label']} | {pair['scroll_x']} | {region} | "
                        f"{stats['changed_pixels']} | {stats['sad']} | "
                        f"{stats['exact_match_ratio']:.6f} |")
    (args.output / "pixel_ab.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"\nwrote {args.output / 'pixel_ab.md'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
