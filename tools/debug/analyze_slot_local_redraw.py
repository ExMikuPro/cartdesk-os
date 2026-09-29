#!/usr/bin/env python3
"""Phase 5 host analysis: native scroll versus slot-local redraw invalidation.

Consumes one GDB transcript per mode (see tools/gdb/launcher_slot_local_redraw.gdb)
and reports the dirty geometry and render cost that the target board recorded.

Outputs:
  * dirty_regions.png   - every recorded frame's joined dirty rectangles, one row
                          per mode, one column per frame (row 0 = mode 0).
  * dirty_coverage.png  - 0x / 1x / 2x+ dirty coverage heatmap for the
                          representative frame of each mode.
  * dirty_regions_legend.txt - the numbers behind both images.
  * slot_local_redraw.json   - machine readable summary.

Only the Python standard library is required.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import zlib
from pathlib import Path

WIDTH = 800
HEIGHT = 480
MODE_NAMES = {0: "native scroll", 1: "slot-local"}


def parse_kv(line: str) -> dict[str, int]:
    return {key: int(value, 0)
            for key, value in re.findall(r"([A-Za-z0-9_]+)=(-?\d+|0x[0-9a-fA-F]+)", line)}


def parse_log(path: Path) -> dict:
    text = path.read_text(errors="replace")
    result: dict = {
        "path": str(path),
        "scalars": {},
        "categories": {},
        "draw_types": {},
        "units": {},
        "dma_modes": {},
        "objects": [],
        "frames": [],
        "slot_bbox": [],
        "draw_rects": [],
        "preclear_rects": [],
    }

    for line in text.splitlines():
        if line.startswith("AUDIT_STATUS "):
            result["scalars"]["audit_status"] = parse_kv(line)
        elif line.startswith("MODE "):
            result["scalars"].setdefault("mode", {}).update(parse_kv(line))
        elif line.startswith("SCROLL_MODE "):
            result["scalars"]["scroll_mode"] = parse_kv(line)
        elif line.startswith("FAULTS "):
            result["scalars"]["faults"] = parse_kv(line)
        elif line.startswith("DISPLAY "):
            result["scalars"]["display"] = parse_kv(line)
        elif line.startswith("TOTAL "):
            result["scalars"]["total"] = parse_kv(line)
        elif line.startswith("CAT "):
            result["categories"] = parse_kv(line)
        elif line.startswith("DRAWTYPE "):
            result["draw_types"] = parse_kv(line)
        elif line.startswith("UNIT "):
            result["units"] = parse_kv(line)
        elif line.startswith("DMAMODE "):
            result["dma_modes"] = parse_kv(line)
        elif line.startswith("DMADEP "):
            result["scalars"]["dma_dep"] = parse_kv(line)
        elif line.startswith("PRECLEAR "):
            result["scalars"]["preclear"] = parse_kv(line)
        elif line.startswith("OBJECTS "):
            result["scalars"]["objects"] = parse_kv(line)
        elif line.startswith("MERGE "):
            result["scalars"]["merge"] = parse_kv(line)
        elif line.startswith("INVALIDATE "):
            result["scalars"]["invalidate"] = parse_kv(line)
        elif line.startswith("LAYOUT "):
            result["scalars"]["layout"] = parse_kv(line)
        elif line.startswith("DIRTY_RING "):
            result["scalars"]["dirty_ring"] = parse_kv(line)
        elif line.startswith("DIRTY_FRAME "):
            result["frames"].append({"joined_rects": [], "pre_rects": [], **parse_kv(line)})
        elif line.startswith("DIRTY_JOINED "):
            record = parse_kv(line)
            if record["frame"] < len(result["frames"]):
                result["frames"][record["frame"]]["joined_rects"].append(record)
        elif line.startswith("DIRTY_PRE "):
            record = parse_kv(line)
            if record["frame"] < len(result["frames"]):
                result["frames"][record["frame"]]["pre_rects"].append(record)
        elif line.startswith("OBJECT "):
            result["objects"].append(parse_kv(line))
        elif line.startswith("SLOT_BBOX "):
            result["slot_bbox"].append(parse_kv(line))
        elif line.startswith("DRAW_RECT "):
            result["draw_rects"].append(parse_kv(line))
        elif line.startswith("PRECLEAR_RECT "):
            result["preclear_rects"].append(parse_kv(line))

    return result


def rect_area(rect: dict[str, int]) -> int:
    w = min(WIDTH - 1, rect["x2"]) - max(0, rect["x1"]) + 1
    h = min(HEIGHT - 1, rect["y2"]) - max(0, rect["y1"]) + 1
    if w <= 0 or h <= 0:
        return 0
    return w * h


def coverage(rects: list[dict[str, int]]) -> dict[str, object]:
    """0x / 1x / 2x+ histogram plus the unique and summed dirty pixel counts."""
    mask = bytearray(WIDTH * HEIGHT)
    total = 0
    for rect in rects:
        x1 = max(0, rect["x1"])
        y1 = max(0, rect["y1"])
        x2 = min(WIDTH - 1, rect["x2"])
        y2 = min(HEIGHT - 1, rect["y2"])
        if x2 < x1 or y2 < y1:
            continue
        total += (x2 - x1 + 1) * (y2 - y1 + 1)
        for y in range(y1, y2 + 1):
            row = y * WIDTH
            for x in range(x1, x2 + 1):
                index = row + x
                if mask[index] < 255:
                    mask[index] += 1
    unique = sum(1 for value in mask if value)
    once = sum(1 for value in mask if value == 1)
    multi = sum(1 for value in mask if value >= 2)
    return {
        "rects": len(rects),
        "sum_pixels": total,
        "unique_pixels": unique,
        "once_pixels": once,
        "multi_pixels": multi,
        "zero_pixels": WIDTH * HEIGHT - unique,
        "merge_factor": (total / unique) if unique else 0.0,
        "mask": mask,
    }


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


class Canvas:
    def __init__(self, width: int, height: int, color=(0, 0, 0, 255)):
        self.width = width
        self.height = height
        self.pixels = bytearray(bytes(color) * (width * height))

    def fill(self, rect, color) -> None:
        x1 = max(0, rect["x1"])
        y1 = max(0, rect["y1"])
        x2 = min(self.width - 1, rect["x2"])
        y2 = min(self.height - 1, rect["y2"])
        if x2 < x1 or y2 < y1:
            return
        row = bytes(color) * (x2 - x1 + 1)
        for y in range(y1, y2 + 1):
            start = (y * self.width + x1) * 4
            self.pixels[start:start + len(row)] = row

    def blend(self, rect, color, alpha: int) -> None:
        x1 = max(0, rect["x1"])
        y1 = max(0, rect["y1"])
        x2 = min(self.width - 1, rect["x2"])
        y2 = min(self.height - 1, rect["y2"])
        if x2 < x1 or y2 < y1:
            return
        for y in range(y1, y2 + 1):
            base = (y * self.width + x1) * 4
            for x in range(x1, x2 + 1):
                index = base + (x - x1) * 4
                for channel in range(3):
                    old = self.pixels[index + channel]
                    self.pixels[index + channel] = (
                        old * (255 - alpha) + color[channel] * alpha) // 255
                self.pixels[index + 3] = 255

    def border(self, rect, color) -> None:
        self.fill({"x1": rect["x1"], "y1": rect["y1"], "x2": rect["x2"], "y2": rect["y1"]}, color)
        self.fill({"x1": rect["x1"], "y1": rect["y2"], "x2": rect["x2"], "y2": rect["y2"]}, color)
        self.fill({"x1": rect["x1"], "y1": rect["y1"], "x2": rect["x1"], "y2": rect["y2"]}, color)
        self.fill({"x1": rect["x2"], "y1": rect["y1"], "x2": rect["x2"], "y2": rect["y2"]}, color)

    def blit_scaled(self, dst_x: int, dst_y: int, source: "Canvas", factor: int) -> None:
        for y in range(source.height // factor):
            src_row = (y * factor) * source.width * 4
            dst_index = ((dst_y + y) * self.width + dst_x) * 4
            for x in range(source.width // factor):
                src = src_row + (x * factor) * 4
                self.pixels[dst_index:dst_index + 4] = source.pixels[src:src + 4]
                dst_index += 4


def representative_frame(frames: list[dict]) -> dict | None:
    """The median-cost step frame, so the picture is not the first or last one."""
    candidates = [f for f in frames if f.get("joined_rects")]
    if not candidates:
        return None
    ordered = sorted(candidates, key=lambda f: f.get("joined_pixels", 0))
    return ordered[len(ordered) // 2]


def build_dirty_regions(mode0: dict, mode1: dict, out: Path, factor: int = 2) -> dict:
    rows = [m for m in (mode0, mode1) if m is not None]
    columns = max((len(m["frames"]) for m in rows), default=0)
    cell_w = WIDTH // factor
    cell_h = HEIGHT // factor
    canvas = Canvas(columns * cell_w, len(rows) * cell_h, (24, 24, 28, 255))
    legend: list[str] = []

    for row, mode in enumerate(rows):
        for column, frame in enumerate(mode["frames"]):
            panel = Canvas(WIDTH, HEIGHT, (10, 10, 12, 255))
            for rect in frame["pre_rects"]:
                panel.border(rect, (255, 176, 32, 255))
            for rect in frame["joined_rects"]:
                panel.blend(rect, (0, 176, 255, 255), 96)
                panel.border(rect, (0, 224, 255, 255))
            canvas.blit_scaled(column * cell_w, row * cell_h, panel, factor)
            legend.append(
                f"row={row} mode={mode['scalars'].get('mode', {}).get('requested', -1)} "
                f"frame={column} seq={frame.get('seq')} "
                f"pre={frame.get('pre_join_count')} joined={frame.get('joined_count')} "
                f"joined_px={frame.get('joined_pixels')} "
                f"render_ms={(frame.get('render_cycles', 0) / 480e6 * 1000.0):.3f}")
    write_png(out, canvas.width, canvas.height, bytes(canvas.pixels))
    return {"rows": len(rows), "columns": columns, "scale": factor, "legend": legend}


def build_coverage_heatmap(mode0: dict, mode1: dict, out: Path, factor: int = 2) -> dict:
    mode0_frame = representative_frame(mode0["frames"]) if mode0 else None
    mode1_frame = representative_frame(mode1["frames"]) if mode1 else None
    panels = []
    if mode0_frame is not None:
        panels.append((0, mode0_frame))
    if mode1_frame is not None:
        panels.append((1, mode1_frame))

    cell_w = WIDTH // factor
    cell_h = HEIGHT // factor
    canvas = Canvas(len(panels) * cell_w, cell_h, (16, 16, 20, 255))
    legend: list[str] = []
    stats: dict[str, dict] = {}

    for column, (mode_id, frame) in enumerate(panels):
        summary = coverage(frame["joined_rects"])
        mask = summary.pop("mask")
        stats[str(mode_id)] = summary
        panel = Canvas(WIDTH, HEIGHT, (0, 0, 0, 255))
        for y in range(HEIGHT):
            for x in range(WIDTH):
                depth = mask[y * WIDTH + x]
                if depth == 0:
                    continue
                if depth == 1:
                    color = (0, 64, 255, 255)
                elif depth == 2:
                    color = (255, 48, 48, 255)
                else:
                    color = (0, 255, 96, 255)
                index = (y * WIDTH + x) * 4
                panel.pixels[index:index + 4] = bytes(color)
        canvas.blit_scaled(column * cell_w, 0, panel, factor)
        legend.append(
            f"column={column} mode={mode_id} seq={frame.get('seq')} "
            f"rects={summary['rects']} sum_px={summary['sum_pixels']} "
            f"unique_px={summary['unique_pixels']} once={summary['once_pixels']} "
            f"multi={summary['multi_pixels']} zero={summary['zero_pixels']} "
            f"merge_factor={summary['merge_factor']:.4f}")
    write_png(out, canvas.width, canvas.height, bytes(canvas.pixels))
    return {"panels": len(panels), "scale": factor, "legend": legend, "stats": stats}


def draw_type_coverage(draw_rects: list[dict], draw_type: int) -> dict:
    """Total and unique pixels drawn by one draw-task type in the last frame."""
    rects = [r for r in draw_rects if r.get("type") == draw_type]
    summary = coverage(rects)
    summary.pop("mask")
    return summary


def summarise(mode: dict, clock: float) -> dict:
    scalars = mode["scalars"]
    frames = mode["frames"]
    total = scalars.get("total", {})
    frame_count = total.get("frames", 0)
    categories = mode["categories"]
    draw_types = mode["draw_types"]
    dma_modes = mode["dma_modes"]
    objects = mode["objects"]
    preclear = scalars.get("preclear", {})

    def per_frame(cycles: int) -> float:
        return (cycles / frame_count / clock * 1000.0) if frame_count else 0.0

    step_frames = [f for f in frames if f.get("joined_rects")]
    # A capture that is halted mid-step leaves the newest dirty record without a
    # paired render window; keep it for geometry but drop it from the cost mean.
    timed_frames = [f for f in step_frames if f.get("render_cycles", 0) > 0]
    step_cycles = sum(f.get("render_cycles", 0) for f in timed_frames)

    joined_stats = [coverage(f["joined_rects"]) for f in step_frames]
    for stat in joined_stats:
        stat.pop("mask")
    unique = [s["unique_pixels"] for s in joined_stats]
    summed = [s["sum_pixels"] for s in joined_stats]

    icon_objects = [o for o in objects if o["kind"] == 8]
    slot_objects = [o for o in objects if o["kind"] == 4]
    image_objects = [o for o in objects if o["kind"] == 5]

    return {
        "mode": scalars.get("mode", {}),
        "T_render_ms_window": per_frame(total.get("cycles", 0)),
        "T_render_ms_step_frames": (step_cycles / len(timed_frames) / clock * 1000.0)
                                    if timed_frames else 0.0,
        "audit_frames": frame_count,
        "step_frames": len(step_frames),
        "timed_step_frames": len(timed_frames),
        "presentation_interval_ms": (scalars.get("display", {}).get("render_total", 0)
                                     / frame_count / clock * 1000.0) if frame_count else 0.0,
        "dirty": {
            "joined_areas_per_frame": (sum(f.get("joined_count", 0) for f in step_frames)
                                       / len(step_frames)) if step_frames else 0.0,
            "pre_join_areas_per_frame": (sum(f.get("pre_join_count", 0) for f in step_frames)
                                         / len(step_frames)) if step_frames else 0.0,
            "sum_pixels_per_frame": (sum(summed) / len(summed)) if summed else 0.0,
            "unique_pixels_per_frame": (sum(unique) / len(unique)) if unique else 0.0,
            "unique_ratio": (sum(unique) / len(unique) / (WIDTH * HEIGHT)) if unique else 0.0,
            "merge_factor": (sum(summed) / sum(unique)) if sum(unique) else 0.0,
            "once_pixels_per_frame": (sum(s["once_pixels"] for s in joined_stats)
                                      / len(joined_stats)) if joined_stats else 0.0,
            "multi_pixels_per_frame": (sum(s["multi_pixels"] for s in joined_stats)
                                       / len(joined_stats)) if joined_stats else 0.0,
        },
        "invalidate": scalars.get("invalidate", {}),
        "layout": scalars.get("layout", {}),
        "merge": scalars.get("merge", {}),
        "preclear": {
            "pixels_per_frame": preclear.get("pixels", 0) / frame_count if frame_count else 0.0,
            "cleared_pixels_per_frame": (preclear.get("cleared_px", 0) / frame_count)
                                        if frame_count else 0.0,
            "skipped_pixels_per_frame": (preclear.get("skipped_px", 0) / frame_count)
                                        if frame_count else 0.0,
            "areas_per_frame": preclear.get("areas", 0) / frame_count if frame_count else 0.0,
            "ms_per_frame": per_frame(categories.get("drawbuf_clear", 0)),
        },
        "categories_ms": {key: per_frame(value) for key, value in categories.items()},
        "draw_types_ms": {
            "fill": per_frame(draw_types.get("fill_cy", 0)),
            "border": per_frame(draw_types.get("border_cy", 0)),
            "label": per_frame(draw_types.get("label_cy", 0)),
            "image": per_frame(draw_types.get("image_cy", 0)),
            "fill_tasks": draw_types.get("fill_n", 0) / frame_count if frame_count else 0.0,
            "border_tasks": draw_types.get("border_n", 0) / frame_count if frame_count else 0.0,
            "label_tasks": draw_types.get("label_n", 0) / frame_count if frame_count else 0.0,
            "image_tasks": draw_types.get("image_n", 0) / frame_count if frame_count else 0.0,
        },
        "dma2d": {
            "r2m_pixels_per_frame": dma_modes.get("r2m_px", 0) / frame_count if frame_count else 0.0,
            "r2m_ms_per_frame": per_frame(dma_modes.get("r2m_cy", 0)),
            "pfc_pixels_per_frame": dma_modes.get("pfc_px", 0) / frame_count if frame_count else 0.0,
            "pfc_ms_per_frame": per_frame(dma_modes.get("pfc_cy", 0)),
            "wait_ms_per_frame": per_frame(categories.get("dma2d_wait", 0)),
        },
        "fixed_ui": {
            "circle_icon_draws_per_frame": (sum(o["draw_total"] for o in icon_objects)
                                            / frame_count) if frame_count else 0.0,
            "circle_icon_objects": len(icon_objects),
            "slot_image_draws_per_frame": (sum(o["draw_total"] for o in image_objects)
                                           / frame_count) if frame_count else 0.0,
            "slot_draws_per_frame": (sum(o["draw_total"] for o in slot_objects)
                                     / frame_count) if frame_count else 0.0,
        },
        "fill_coverage_last_frame": draw_type_coverage(mode["draw_rects"], 1),
        "image_coverage_last_frame": draw_type_coverage(mode["draw_rects"], 6),
        "border_coverage_last_frame": draw_type_coverage(mode["draw_rects"], 2),
        "objects_totals": scalars.get("objects", {}),
        "faults": scalars.get("faults", {}),
        "display": scalars.get("display", {}),
    }


def mean_summary(summaries: list[dict]) -> dict:
    """Elementwise mean over the numeric leaves of several runs of one mode."""
    if len(summaries) == 1:
        return summaries[0]
    first = summaries[0]
    out: dict = {}
    for key, value in first.items():
        if isinstance(value, dict):
            nested = [s[key] for s in summaries if isinstance(s.get(key), dict)]
            if len(nested) == len(summaries):
                out[key] = mean_summary(nested)
        elif isinstance(value, (int, float)) and not isinstance(value, bool):
            values = [s[key] for s in summaries
                      if isinstance(s.get(key), (int, float)) and not isinstance(s.get(key), bool)]
            if values:
                out[key] = sum(values) / len(values)
        else:
            out[key] = value
    out["runs"] = len(summaries)
    return out


def format_table(summary0: dict | None, summary1: dict | None) -> str:
    rows = [
        ("Dirty pixels (unique/frame)", "dirty", "unique_pixels_per_frame"),
        ("Dirty pixels (sum/frame)", "dirty", "sum_pixels_per_frame"),
        ("Dirty ratio", "dirty", "unique_ratio"),
        ("Joined areas/frame", "dirty", "joined_areas_per_frame"),
        ("Pre-join areas/frame", "dirty", "pre_join_areas_per_frame"),
        ("Merge factor", "dirty", "merge_factor"),
        ("inv_p peak", "invalidate", "inv_p_peak"),
        ("inv_p overflow", "invalidate", "overflow"),
        ("pre-clear pixels/frame", "preclear", "pixels_per_frame"),
        ("pre-clear ms/frame", "preclear", "ms_per_frame"),
        ("fill ms/frame", "draw_types_ms", "fill"),
        ("DMA2D image ms/frame", "draw_types_ms", "image"),
        ("border ms/frame", "draw_types_ms", "border"),
        ("SW execute ms/frame", "categories_ms", "sw_execute"),
        ("DMA2D wait ms/frame", "categories_ms", "dma2d_wait"),
        ("draw-buffer clear ms/frame", "categories_ms", "drawbuf_clear"),
        ("T_render ms (window avg)", None, "T_render_ms_window"),
        ("T_render ms (step frames)", None, "T_render_ms_step_frames"),
        ("Presentation interval ms", None, "presentation_interval_ms"),
    ]

    def fetch(summary: dict, group: str | None, key: str):
        if summary is None:
            return None
        return summary[key] if group is None else summary[group][key]

    lines = [f"{'Metric':<34}{'Native Scroll':>16}{'Slot-Local':>16}{'Delta':>12}"]
    lines.append("-" * 78)
    for label, group, key in rows:
        a = fetch(summary0, group, key)
        b = fetch(summary1, group, key)
        if a is None and b is None:
            continue

        def fmt(value):
            if value is None:
                return "-"
            return f"{value:.2f}" if isinstance(value, float) else str(value)

        delta = "-"
        if a is not None and b is not None:
            delta = f"{b - a:+.2f}" if isinstance(a, float) else f"{b - a:+d}"
        lines.append(f"{label:<34}{fmt(a):>16}{fmt(b):>16}{delta:>12}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode0", type=Path, action="append", required=True,
                        help="GDB transcript for the native scroll baseline (repeatable)")
    parser.add_argument("--mode1", type=Path, action="append", default=None,
                        help="GDB transcript for the slot-local candidate (repeatable)")
    parser.add_argument("--out", type=Path, default=None,
                        help="output directory (default: alongside the mode1 transcript)")
    parser.add_argument("--clock", type=float, default=480e6, help="DWT clock in Hz")
    args = parser.parse_args()

    logs0 = [parse_log(path) for path in args.mode0]
    logs1 = [parse_log(path) for path in args.mode1] if args.mode1 else []
    mode0 = logs0[0]
    mode1 = logs1[0] if logs1 else None
    out = args.out or args.mode0[0].parent
    out.mkdir(parents=True, exist_ok=True)

    runs0 = [summarise(log, args.clock) for log in logs0]
    runs1 = [summarise(log, args.clock) for log in logs1]
    summary0 = mean_summary(runs0)
    summary1 = mean_summary(runs1) if runs1 else None

    table = format_table(summary0, summary1)
    print(table)

    regions = build_dirty_regions(mode0, mode1, out / "dirty_regions.png")
    heatmap = build_coverage_heatmap(mode0, mode1, out / "dirty_coverage.png")

    legend = ["# dirty_regions.png : row 0 = mode 0 native, row 1 = mode 1 slot-local",
              "# orange outline = inv_areas[] before join, cyan fill = joined dirty area",
              *regions["legend"],
              "",
              "# dirty_coverage.png : blue = dirty once, red = dirty twice, green = 3x or more",
              *heatmap["legend"]]
    (out / "dirty_regions_legend.txt").write_text("\n".join(legend) + "\n")

    payload = {
        "mode0_native": summary0,
        "mode1_slot_local": summary1,
        "mode0_runs": runs0,
        "mode1_runs": runs1,
        "dirty_regions": {k: v for k, v in regions.items() if k != "legend"},
        "dirty_coverage": heatmap["stats"],
    }
    (out / "slot_local_redraw.json").write_text(json.dumps(payload, indent=2) + "\n")

    print()
    print(f"wrote {out / 'dirty_regions.png'}")
    print(f"wrote {out / 'dirty_coverage.png'}")
    print(f"wrote {out / 'dirty_regions_legend.txt'}")
    print(f"wrote {out / 'slot_local_redraw.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
