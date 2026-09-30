#!/usr/bin/env python3
"""Drive deterministic Launcher scroll captures through scalar GDB mailboxes."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import math
import re
import shutil
import statistics
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build" / "Debug-LTDC-Sync-Trace"
ELF = BUILD / "cartdesk-os.elf"
FB_A_START = 0xD0177000
FB_A_END = 0xD02EE000
FB_B_START = 0xD02EE000
FB_B_END = 0xD0465000
# Frame-buffer capture bounds must track Core/Inc/sdram_layout.h.  They are also
# resolved symbolically at dump time (see layout_lines) so the literals above
# remain only an offline fallback and never the source of truth.
LAYOUT_SYMBOLS = (
    ("$fb_a_start", "SDRAM_LVGL_FB_A_BASE"),
    ("$fb_a_end", "SDRAM_LVGL_FB_A_END"),
    ("$fb_b_start", "SDRAM_LVGL_FB_B_BASE"),
    ("$fb_b_end", "SDRAM_LVGL_FB_B_END"),
)
MODE_IDS = {"argb": 0, "xrgb": 3}
CAPTURES = (("before", 0), ("mid", 6), ("after", 12))
SCREEN_PIXELS = 800 * 480
PANEL_FRAME_MS = 16.984

EVENT_RENDER_BEGIN = 1
EVENT_RENDER_END = 2
EVENT_FLUSH_ENTER = 3
EVENT_RELOAD_REQUEST = 9
EVENT_RELOAD_EVENT = 12
EVENT_REFRESH_BEGIN = 13
EVENT_FLUSH_WAIT_BEGIN = 16
EVENT_FLUSH_WAIT_END = 17
EVENT_FLUSH_COMPLETE = 18
EVENT_BUFFER_SYNC_BEGIN = 21
EVENT_BUFFER_SYNC_END = 22


def gdb_common(mode: int, elf: Path, preclear_mode: int | None,
               scroll_mode: int = 0, move_api: int = 0) -> list[str]:
    lines = [
        "set pagination off",
        "set confirm off",
        "set breakpoint pending on",
        f"file {elf}",
        "target extended-remote localhost:3333",
        "monitor reset halt",
        "load",
        "monitor reset halt",
        "tbreak LauncherScrollCapture_IconsReady",
        "continue",
    ]
    if preclear_mode is not None:
        lines.append(f"set variable g_render_audit_preclear_mode = {preclear_mode}")
    lines += [
        f"set variable g_launcher_slot_trace_visual_mode = {mode}",
        # Phase 5 A/B: 0 keeps the production native scroll container, 1 freezes
        # it and moves every App Slot from the Launcher logical_scroll_x.
        f"set variable g_launcher_scroll_mode = {scroll_mode}",
        f"set variable g_launcher_slot_move_api = {move_api}",
        "tbreak DisplayTrace_RenderEnd",
        "continue",
        "monitor resume",
        "shell sleep 1",
        "monitor halt",
    ]
    return lines


def scroll_setup(capture_step: int) -> list[str]:
    return [
        "set variable g_scroll_capture_delta_px = 240",
        "set variable g_scroll_capture_steps = 12",
        "set variable g_scroll_capture_step_interval_ms = 0",
        f"set variable g_fb_capture_request_step = {capture_step}",
        "set variable g_fb_capture_ready = 0",
    ]


def layout_lines() -> list[str]:
    """Resolve SDRAM layout bounds from the ELF instead of hardcoding them."""
    return [
        f"set {name} = (unsigned long)({symbol})"
        for name, symbol in LAYOUT_SYMBOLS
    ]


def dump_lines(folder: Path, label: str, mode: int) -> list[str]:
    return layout_lines() + [
        f'dump binary memory {folder / "fb_a.raw"} $fb_a_start $fb_a_end',
        f'dump binary memory {folder / "fb_b.raw"} $fb_b_start $fb_b_end',
        (f'printf "CAPTURE label={label} visual={mode} step=%lu frame=%lu scroll_x=%ld '
         'fb_a_seq=%lu fb_b_seq=%lu fb_a_scroll_x=%ld fb_b_scroll_x=%ld '
         'presented_seq=%lu pending_seq=%lu render_seq=%lu front_fb=%lu pending_fb=%lu '
         'reload_request_seq=%lu reload_complete_seq=%lu srcr=0x%08lx isr=0x%08lx '
         'cpsr=0x%08lx cdsr=0x%08lx clock=%lu\\n", '
         'g_scroll_capture_step, g_fb_capture_frame_seq, g_scroll_capture_scroll_x, '
         'g_fb_capture_fb_a_seq, g_fb_capture_fb_b_seq, '
         'g_fb_capture_fb_a_scroll_x, g_fb_capture_fb_b_scroll_x, '
         'g_fb_capture_presented_seq, g_fb_capture_pending_seq, g_fb_capture_render_seq, '
         'g_fb_capture_front_fb, g_fb_capture_pending_fb, '
         'g_fb_capture_reload_request_seq, g_fb_capture_reload_complete_seq, '
         'g_fb_capture_ltdc_srcr, g_fb_capture_ltdc_isr, '
         'g_fb_capture_ltdc_cpsr, g_fb_capture_ltdc_cdsr, SystemCoreClock'),
    ]


def run_gdb(script: Path, output: Path, gdb: str) -> str:
    result = subprocess.run([gdb, "--batch", "-x", str(script)], cwd=ROOT,
                            text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    output.write_text(result.stdout)
    if result.returncode != 0:
        raise RuntimeError(f"GDB failed ({result.returncode}); see {output}")
    return result.stdout


def cycle_delta(end: int, start: int) -> int:
    return (end - start) & 0xFFFFFFFF


def percentile(values: list[float], percent: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    rank = max(1, math.ceil(percent * len(ordered)))
    return ordered[rank - 1]


def metric_summary(values: list[float]) -> dict[str, float | int | None]:
    if not values:
        return {"count": 0, "min": None, "avg": None, "p95": None,
                "p99": None, "max": None, "stddev": None}
    return {
        "count": len(values),
        "min": min(values),
        "avg": statistics.fmean(values),
        "p95": percentile(values, 0.95),
        "p99": percentile(values, 0.99),
        "max": max(values),
        "stddev": statistics.pstdev(values),
    }


def write_histogram_png(path: Path, values: list[float]) -> None:
    """Write a dependency-free RGB histogram when matplotlib is unavailable."""
    width, height = 800, 450
    left, right, top, bottom = 60, 780, 20, 400
    pixels = bytearray([255] * (width * height * 3))

    def fill_rect(x1: int, y1: int, x2: int, y2: int, color: tuple[int, int, int]) -> None:
        for y in range(max(0, y1), min(height, y2)):
            for x in range(max(0, x1), min(width, x2)):
                offset = (y * width + x) * 3
                pixels[offset:offset + 3] = bytes(color)

    fill_rect(left, bottom, right, bottom + 2, (0, 0, 0))
    fill_rect(left, top, left + 2, bottom, (0, 0, 0))
    if values:
        bin_count = max(1, min(12, len(values)))
        low, high = min(values), max(values)
        if high <= low:
            high = low + 1.0
        bins = [0] * bin_count
        for value in values:
            index = min(bin_count - 1, int((value - low) * bin_count / (high - low)))
            bins[index] += 1
        peak = max(bins)
        bin_width = (right - left) / bin_count
        for index, count in enumerate(bins):
            x1 = round(left + index * bin_width) + 2
            x2 = round(left + (index + 1) * bin_width) - 2
            y1 = bottom - round((bottom - top) * count / peak)
            fill_rect(x1, y1, x2, bottom, (31, 119, 180))
        for marker, color in ((PANEL_FRAME_MS, (44, 160, 44)),
                              (2 * PANEL_FRAME_MS, (255, 127, 14))):
            if low <= marker <= high:
                x = round(left + (marker - low) * (right - left) / (high - low))
                fill_rect(x, top, x + 2, bottom, color)

    def png_chunk(kind: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    raw = b"".join(b"\x00" + pixels[row * width * 3:(row + 1) * width * 3]
                   for row in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) +
                     png_chunk(b"IDAT", zlib.compress(raw, 9)) + png_chunk(b"IEND", b""))


def parse_trace(output: str) -> tuple[int, list[dict[str, int]]]:
    timing = next((line for line in output.splitlines() if line.startswith("TIMING ")), "")
    clock_match = re.search(r"\bclock=(\d+)", timing)
    if not clock_match:
        raise RuntimeError("TIMING output is missing SystemCoreClock")
    events: list[dict[str, int]] = []
    for line in output.splitlines():
        if not line.startswith("TRACE "):
            continue
        events.append({key: int(value, 0)
                       for key, value in re.findall(r"([a-z_]+)=([^ ]+)", line)})
    if not events:
        raise RuntimeError("timing trace did not contain normalized TRACE records")
    return int(clock_match.group(1)), events


def analyze_timing_trace(output: str, folder: Path) -> None:
    clock_hz, events = parse_trace(output)
    cycles_per_ms = clock_hz / 1000.0
    frames: dict[int, dict[str, float | int]] = {}
    starts: dict[tuple[int, str], int] = {}
    previous_present_cycle: int | None = None
    pending_request_cycle: int | None = None
    pending_request_frame: int | None = None
    last_presented_request_frame: int | None = None
    active_flush_wait_cycle: int | None = None
    active_flush_wait_frame: int | None = None
    flush_wait_samples: list[float] = []

    start_events = {
        EVENT_REFRESH_BEGIN: "refresh",
        EVENT_RENDER_BEGIN: "render",
        EVENT_BUFFER_SYNC_BEGIN: "buffer_sync",
    }
    end_events = {
        EVENT_RENDER_END: ("render", "render_ms"),
        EVENT_BUFFER_SYNC_END: ("buffer_sync", "buffer_sync_ms"),
    }

    for event in events:
        frame = event["frame"]
        event_type = event["event"]
        row = frames.setdefault(frame, {"frame_seq": frame, "dirty_pixels": 0,
                                        "flush_area_count": 0})
        if event_type in start_events:
            starts[(frame, start_events[event_type])] = event["cycle"]
        if event_type == EVENT_FLUSH_ENTER:
            row["dirty_pixels"] = int(row["dirty_pixels"]) + event.get("value", 0)
            row["flush_area_count"] = int(row["flush_area_count"]) + 1
            starts[(frame, "flush_submit")] = event["cycle"]
        elif event_type == EVENT_RELOAD_REQUEST:
            submit_start = starts.get((frame, "flush_submit"))
            if submit_start is not None:
                row["flush_submit_ms"] = cycle_delta(event["cycle"], submit_start) / cycles_per_ms
            pending_request_cycle = event["cycle"]
            pending_request_frame = frame
        elif event_type == EVENT_FLUSH_WAIT_BEGIN:
            active_flush_wait_cycle = event["cycle"]
            active_flush_wait_frame = pending_request_frame or last_presented_request_frame
        elif event_type == EVENT_FLUSH_WAIT_END:
            if active_flush_wait_cycle is not None:
                wait_ms = cycle_delta(event["cycle"], active_flush_wait_cycle) / cycles_per_ms
                flush_wait_samples.append(wait_ms)
                if active_flush_wait_frame is not None:
                    wait_row = frames.setdefault(active_flush_wait_frame,
                                                 {"frame_seq": active_flush_wait_frame,
                                                  "dirty_pixels": 0, "flush_area_count": 0})
                    wait_row["flush_wait_ms"] = wait_ms
            active_flush_wait_cycle = None
            active_flush_wait_frame = None
        elif event_type == EVENT_BUFFER_SYNC_END:
            row["sync_pixels"] = event.get("value", 0)
            row["sync_area_count"] = event.get("aux", 0)
        elif event_type == EVENT_RENDER_END:
            refresh_start = starts.get((frame, "refresh"))
            if refresh_start is not None:
                row["refresh_ms"] = cycle_delta(event["cycle"], refresh_start) / cycles_per_ms
        elif event_type == EVENT_RELOAD_EVENT:
            if pending_request_cycle is not None and pending_request_frame is not None:
                present_row = frames.setdefault(pending_request_frame,
                                                {"frame_seq": pending_request_frame,
                                                 "dirty_pixels": 0, "flush_area_count": 0})
                present_row["reload_wait_ms"] = cycle_delta(
                    event["cycle"], pending_request_cycle) / cycles_per_ms
                refresh_start = starts.get((pending_request_frame, "refresh"))
                if refresh_start is not None:
                    present_row["total_frame_ms"] = cycle_delta(
                        event["cycle"], refresh_start) / cycles_per_ms
                if previous_present_cycle is not None:
                    interval_ms = cycle_delta(event["cycle"], previous_present_cycle) / cycles_per_ms
                    present_row["frame_interval_ms"] = interval_ms
                    present_row["display_frames"] = max(1, round(interval_ms / PANEL_FRAME_MS))
                last_presented_request_frame = pending_request_frame
                pending_request_cycle = None
                pending_request_frame = None
            previous_present_cycle = event["cycle"]

        if event_type in end_events:
            start_name, metric_name = end_events[event_type]
            start = starts.get((frame, start_name))
            if start is not None:
                row[metric_name] = cycle_delta(event["cycle"], start) / cycles_per_ms

    rows = [frames[key] for key in sorted(frames) if "render_ms" in frames[key]]
    if not rows:
        raise RuntimeError("timing trace did not contain complete rendered frames")
    for row in rows:
        row["dirty_ratio"] = int(row["dirty_pixels"]) / SCREEN_PIXELS
        row["sync_ratio"] = int(row.get("sync_pixels", 0)) / SCREEN_PIXELS

    columns = ["frame_seq", "refresh_ms", "render_ms", "buffer_sync_ms",
               "flush_submit_ms", "flush_wait_ms", "reload_wait_ms", "total_frame_ms",
               "frame_interval_ms", "display_frames", "dirty_pixels", "dirty_ratio",
               "flush_area_count", "sync_pixels", "sync_ratio", "sync_area_count"]
    with (folder / "timing_breakdown.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)

    interval_rows = [row for row in rows if "frame_interval_ms" in row]
    with (folder / "frame_interval.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["frame_seq", "interval_ms", "display_frames", "missed_vblanks"])
        for row in interval_rows:
            display_frames = int(row["display_frames"])
            writer.writerow([row["frame_seq"], row["frame_interval_ms"],
                             display_frames, max(0, display_frames - 1)])

    metric_names = ["refresh_ms", "render_ms", "buffer_sync_ms", "flush_submit_ms",
                    "flush_wait_ms", "reload_wait_ms", "total_frame_ms",
                    "frame_interval_ms", "dirty_ratio", "sync_ratio"]
    summary = {
        "clock_hz": clock_hz,
        "panel_frame_ms": PANEL_FRAME_MS,
        "metrics": {
            name: metric_summary(flush_wait_samples if name == "flush_wait_ms" else
                                 [float(row[name]) for row in rows if name in row])
            for name in metric_names
        },
        "presentation_buckets": {
            "1_frame": sum(int(row["display_frames"]) == 1 for row in interval_rows),
            "2_frames": sum(int(row["display_frames"]) == 2 for row in interval_rows),
            "3_frames": sum(int(row["display_frames"]) == 3 for row in interval_rows),
            "4_plus_frames": sum(int(row["display_frames"]) >= 4 for row in interval_rows),
        },
    }
    (folder / "timing_summary.json").write_text(json.dumps(summary, indent=2) + "\n")

    intervals = [float(row["frame_interval_ms"]) for row in interval_rows]
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        write_histogram_png(folder / "frame_interval.png", intervals)
    else:
        figure, axis = plt.subplots(figsize=(8, 4.5))
        axis.hist(intervals, bins=max(1, min(12, len(intervals))), edgecolor="black")
        axis.axvline(PANEL_FRAME_MS, color="tab:green", linestyle="--", label="1 panel frame")
        axis.axvline(2 * PANEL_FRAME_MS, color="tab:orange", linestyle="--", label="2 panel frames")
        axis.set(title="Launcher presentation interval", xlabel="Interval (ms)", ylabel="Count")
        axis.legend()
        figure.tight_layout()
        figure.savefig(folder / "frame_interval.png", dpi=160)
        plt.close(figure)


def timing_run(mode_name: str, mode: int, folder: Path, gdb: str,
               elf: Path, preclear_mode: int | None,
               scroll_mode: int = 0, move_api: int = 0) -> None:
    lines = gdb_common(mode, elf, preclear_mode, scroll_mode, move_api)
    lines += scroll_setup(0xFFFFFFFF)
    lines += [
        "set variable g_display_trace_command = 1",
        "tbreak LauncherScrollCapture_TimingComplete",
        "set variable g_scroll_capture_command = 1",
        "continue",
        "if g_display_reload_events < g_display_reload_requests",
        "  tbreak lv_port_disp_signal_reload_complete",
        "  continue",
        "  finish",
        "end",
        ('printf "TIMING visual=%lu state=%lu step=%lu frame=%lu scroll_x=%ld '
         'render_count=%lu render_last=%lu render_min=%lu render_max=%lu render_total=%lu '
         'image=%lu argb=%lu xrgb=%lu blend=%lu pfc=%lu fill=%lu sw=%lu '
         'reload_req=%lu reload_event=%lu reload_complete=%lu reload_timeout=%lu '
         'ownership=%lu fifo=%lu transfer=%lu other=%lu clock=%lu\\n", '
         f'{mode}, g_scroll_capture_state, g_scroll_capture_step, g_scroll_capture_frame_seq, '
         'g_scroll_capture_scroll_x, g_display_render_count, g_display_render_last_cycles, '
         'g_display_render_min_cycles, g_display_render_max_cycles, g_display_render_total_cycles, '
         'g_display_dma2d_image_tasks, g_display_dma2d_argb_image_tasks, '
         'g_display_dma2d_xrgb_image_tasks, g_display_dma2d_blend_tasks, '
         'g_display_dma2d_pfc_tasks, g_display_dma2d_fill_tasks, g_display_sw_image_tasks, '
         'g_display_reload_requests, g_display_reload_events, g_display_reload_complete_signals, '
         'g_display_reload_wait_timeouts, g_display_potential_ownership_violations, '
         'g_display_ltdc_fifo_underruns, g_display_ltdc_transfer_errors, '
         'g_display_ltdc_other_errors, SystemCoreClock'),
        "p g_display_dma2d_image_snapshots",
        "set $trace_count = g_display_trace_count",
        "set $trace_index = 0",
        "set $trace_capacity = sizeof(g_display_trace_ring) / sizeof(g_display_trace_ring[0])",
        "set $trace_start = (g_display_trace_write_index + $trace_capacity - $trace_count) % $trace_capacity",
        "while $trace_index < $trace_count",
        "  set $trace_slot = ($trace_start + $trace_index) % $trace_capacity",
        ('  printf "TRACE index=%lu cycle=%lu frame=%lu event=%lu value=%lu aux=%lu\\n", '
         '$trace_index, g_display_trace_ring[$trace_slot].cycle, '
         'g_display_trace_ring[$trace_slot].frame_seq, '
         'g_display_trace_ring[$trace_slot].event, '
         'g_display_trace_ring[$trace_slot].value, '
         'g_display_trace_ring[$trace_slot].aux'),
        "  set $trace_index = $trace_index + 1",
        "end",
        "monitor reset halt",
    ]
    script = folder / f"{mode_name}_timing.gdb"
    script.write_text("\n".join(lines) + "\n")
    output = run_gdb(script, folder / "timing_trace.txt", gdb)
    analyze_timing_trace(output, folder)


def parse_captures(output: str) -> dict[str, dict[str, object]]:
    captures: dict[str, dict[str, object]] = {}
    for line in output.splitlines():
        if not line.startswith("CAPTURE "):
            continue
        values: dict[str, object] = {}
        for key, value in re.findall(r"([a-z_]+)=([^ ]+)", line):
            if key == "label":
                values[key] = value
            else:
                values[key] = int(value, 0)
        captures[str(values["label"])] = values
    return captures


def capture_run(mode_name: str, mode: int, folder: Path, gdb: str,
                elf: Path, preclear_mode: int | None,
                scroll_mode: int = 0, move_api: int = 0) -> None:
    for label, _ in CAPTURES:
        (folder / label).mkdir(parents=True, exist_ok=True)
    lines = gdb_common(mode, elf, preclear_mode, scroll_mode, move_api)
    lines += scroll_setup(CAPTURES[0][1])
    lines += [
        "set variable g_display_trace_command = 1",
        "break LauncherScrollCapture_CaptureReady",
        "set variable g_scroll_capture_command = 1",
        "continue",
    ]
    for index, (label, step) in enumerate(CAPTURES):
        lines += dump_lines(folder / label, label, mode)
        if index + 1 < len(CAPTURES):
            next_step = CAPTURES[index + 1][1]
            lines += [
                f"set variable g_fb_capture_request_step = {next_step}",
                "set variable g_fb_capture_ready = 0",
                "continue",
            ]
    lines += ["p g_display_dma2d_image_snapshots", "monitor reset halt"]
    script = folder / f"{mode_name}_capture.gdb"
    script.write_text("\n".join(lines) + "\n")
    output = run_gdb(script, folder / "capture_trace.txt", gdb)
    metadata = parse_captures(output)
    if set(metadata) != {label for label, _ in CAPTURES}:
        raise RuntimeError(f"Missing capture metadata in {folder / 'capture_trace.txt'}")

    analyzer = ROOT / "tools" / "debug" / "framebuffer_to_png.py"
    for label, _ in CAPTURES:
        capture = folder / label
        info = metadata[label]
        info["mode"] = mode_name
        (capture / "metadata.txt").write_text(json.dumps(info, indent=2) + "\n")
        subprocess.run([
            sys.executable, str(analyzer),
            "--fb-a", str(capture / "fb_a.raw"),
            "--fb-b", str(capture / "fb_b.raw"),
            "--metadata", str(capture / "metadata.txt"),
            "--output", str(capture),
        ], cwd=ROOT, check=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--modes", nargs="+", choices=MODE_IDS, default=["argb", "xrgb"])
    parser.add_argument("--skip-timing", action="store_true")
    parser.add_argument("--skip-capture", action="store_true")
    parser.add_argument("--gdb", default=shutil.which("arm-none-eabi-gdb"))
    parser.add_argument("--session", type=Path)
    parser.add_argument("--elf", type=Path, default=ELF)
    parser.add_argument("--preclear-mode", type=int, choices=(0, 1, 2, 3))
    parser.add_argument("--scroll-mode", type=int, choices=(0, 1), default=0,
                        help="0 = native LVGL scroll, 1 = Phase 5 slot-local candidate")
    parser.add_argument("--move-api", type=int, choices=(0, 1), default=0,
                        help="slot-local position API: 0 = lv_obj_set_x, 1 = translate_x")
    args = parser.parse_args()
    if not args.gdb:
        raise SystemExit("arm-none-eabi-gdb not found; pass --gdb")
    elf = args.elf.resolve()
    if not elf.exists():
        raise SystemExit(f"missing {elf}; build the requested firmware first")

    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    session = (args.session or (BUILD / "captures" / stamp)).resolve()
    session.mkdir(parents=True, exist_ok=True)
    for mode_name in args.modes:
        folder = session / mode_name.upper()
        folder.mkdir(exist_ok=True)
        mode = MODE_IDS[mode_name]
        if not args.skip_timing:
            timing_run(mode_name, mode, folder, args.gdb, elf,
                       args.preclear_mode, args.scroll_mode, args.move_api)
        if not args.skip_capture:
            capture_run(mode_name, mode, folder, args.gdb, elf,
                        args.preclear_mode, args.scroll_mode, args.move_api)
    print(session)


if __name__ == "__main__":
    main()
