#!/usr/bin/env python3
"""Drive deterministic Launcher scroll captures through scalar GDB mailboxes."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build" / "Debug-LTDC-Sync-Trace"
ELF = BUILD / "cartdesk-os.elf"
FB_A_START = 0xD0177000
FB_A_END = 0xD02EE000
FB_B_START = 0xD02EE000
FB_B_END = 0xD0465000
MODE_IDS = {"argb": 0, "xrgb": 3}
CAPTURES = (("before", 0), ("mid", 6), ("after", 12))


def gdb_common(mode: int) -> list[str]:
    return [
        "set pagination off",
        "set confirm off",
        "set breakpoint pending on",
        f"file {ELF}",
        "target extended-remote localhost:3333",
        "monitor reset halt",
        "load",
        "monitor reset halt",
        "tbreak LauncherScrollCapture_IconsReady",
        "continue",
        f"set variable g_launcher_slot_trace_visual_mode = {mode}",
        "tbreak DisplayTrace_RenderEnd",
        "continue",
        "set variable g_display_trace_command = 1",
        "monitor resume",
        "shell sleep 1",
        "monitor halt",
    ]


def scroll_setup(capture_step: int) -> list[str]:
    return [
        "set variable g_scroll_capture_delta_px = 240",
        "set variable g_scroll_capture_steps = 12",
        "set variable g_scroll_capture_step_interval_ms = 0",
        f"set variable g_fb_capture_request_step = {capture_step}",
        "set variable g_fb_capture_ready = 0",
    ]


def dump_lines(folder: Path, label: str, mode: int) -> list[str]:
    return [
        f'dump binary memory {folder / "fb_a.raw"} 0x{FB_A_START:08X} 0x{FB_A_END:08X}',
        f'dump binary memory {folder / "fb_b.raw"} 0x{FB_B_START:08X} 0x{FB_B_END:08X}',
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


def timing_run(mode_name: str, mode: int, folder: Path, gdb: str) -> None:
    lines = gdb_common(mode)
    lines += scroll_setup(0xFFFFFFFF)
    lines += [
        "tbreak LauncherScrollCapture_TimingComplete",
        "set variable g_scroll_capture_command = 1",
        "continue",
        ('printf "TIMING visual=%lu state=%lu step=%lu frame=%lu scroll_x=%ld '
         'render_count=%lu render_last=%lu render_min=%lu render_max=%lu render_total=%lu '
         'image=%lu argb=%lu xrgb=%lu blend=%lu pfc=%lu fill=%lu sw=%lu '
         'reload_req=%lu reload_event=%lu reload_complete=%lu reload_timeout=%lu '
         'ownership=%lu fifo=%lu transfer=%lu other=%lu\\n", '
         f'{mode}, g_scroll_capture_state, g_scroll_capture_step, g_scroll_capture_frame_seq, '
         'g_scroll_capture_scroll_x, g_display_render_count, g_display_render_last_cycles, '
         'g_display_render_min_cycles, g_display_render_max_cycles, g_display_render_total_cycles, '
         'g_display_dma2d_image_tasks, g_display_dma2d_argb_image_tasks, '
         'g_display_dma2d_xrgb_image_tasks, g_display_dma2d_blend_tasks, '
         'g_display_dma2d_pfc_tasks, g_display_dma2d_fill_tasks, g_display_sw_image_tasks, '
         'g_display_reload_requests, g_display_reload_events, g_display_reload_complete_signals, '
         'g_display_reload_wait_timeouts, g_display_potential_ownership_violations, '
         'g_display_ltdc_fifo_underruns, g_display_ltdc_transfer_errors, g_display_ltdc_other_errors'),
        "p g_display_dma2d_image_snapshots",
        "p g_display_trace_ring",
        "monitor reset halt",
    ]
    script = folder / f"{mode_name}_timing.gdb"
    script.write_text("\n".join(lines) + "\n")
    run_gdb(script, folder / "timing_trace.txt", gdb)


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


def capture_run(mode_name: str, mode: int, folder: Path, gdb: str) -> None:
    for label, _ in CAPTURES:
        (folder / label).mkdir(parents=True, exist_ok=True)
    lines = gdb_common(mode)
    lines += scroll_setup(CAPTURES[0][1])
    lines += [
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
    args = parser.parse_args()
    if not args.gdb:
        raise SystemExit("arm-none-eabi-gdb not found; pass --gdb")
    if not ELF.exists():
        raise SystemExit(f"missing {ELF}; build Debug-LTDC-Sync-Trace first")

    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    session = (args.session or (BUILD / "captures" / stamp)).resolve()
    session.mkdir(parents=True, exist_ok=True)
    for mode_name in args.modes:
        folder = session / mode_name.upper()
        folder.mkdir(exist_ok=True)
        mode = MODE_IDS[mode_name]
        if not args.skip_timing:
            timing_run(mode_name, mode, folder, args.gdb)
        if not args.skip_capture:
            capture_run(mode_name, mode, folder, args.gdb)
    print(session)


if __name__ == "__main__":
    main()
