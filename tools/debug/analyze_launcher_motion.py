#!/usr/bin/env python3
"""Launcher Motion Isolation Test analysis.

Parses the motion trace ring dumped by tools/gdb/launcher_motion_isolation.gdb
(``TRACE i=...`` records) and answers the quantitative half of the motion
isolation question:

* is ``logical_scroll_x`` exactly 1:1 with the pointer during a drag?
* how many pointer samples collapse into one rendered frame (coalescing)?
* what is the render / presentation cadence, expressed in panel frames?

The perceptual half ("does it visually jitter?") can only be answered by a
human operator; this tool deliberately does not guess at it.

Plotting uses matplotlib when it is importable and otherwise falls back to a
pure-stdlib PNG writer (the same approach as tools/debug/framebuffer_to_png.py)
so the tool runs on a bare checkout.

Usage:
    python3 tools/debug/analyze_launcher_motion.py \
        --current /tmp/motion_m0.log \
        --direct  /tmp/motion_m1.log \
        --output  build/motion-isolation
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import struct
import zlib
from pathlib import Path

PANEL_FRAME_MS = 16.984
TRACE_RE = re.compile(
    r"TRACE i=(\d+) t=(\d+) rs=(\d+) px=(-?\d+) pdx=(-?\d+) "
    r"lx=(\d+) ldx=(-?\d+) dt=(\d+) p=(\d+) st=(\d+)"
)
KEYVAL_RE = re.compile(r"^([A-Z_]+) (.*)$")


# --------------------------------------------------------------------------
# input
# --------------------------------------------------------------------------

def parse_trace(path: Path) -> list[dict]:
    rows: list[dict] = []
    for line in path.read_text(errors="replace").splitlines():
        m = TRACE_RE.match(line.strip())
        if not m:
            continue
        i, t, rs, px, pdx, lx, ldx, dt, p, st = (int(v) for v in m.groups())
        rows.append(
            dict(index=i, time_ms=t, render_seq=rs, pointer_x=px, pointer_dx=pdx,
                 logical_x=lx, logical_dx=ldx, dt_ms=dt, pressed=p, state=st)
        )
    return rows


def parse_summary(path: Path) -> dict:
    out: dict[str, object] = {}
    for line in path.read_text(errors="replace").splitlines():
        m = KEYVAL_RE.match(line.strip())
        if not m:
            continue
        key, rest = m.group(1), m.group(2)
        kv = {}
        for tok in rest.split():
            if "=" in tok:
                k, v = tok.split("=", 1)
                try:
                    kv[k] = int(v, 0)
                except ValueError:
                    kv[k] = v
        if kv:
            out[key] = kv
    return out


# --------------------------------------------------------------------------
# metrics
# --------------------------------------------------------------------------

def position_error(rows: list[dict]) -> dict:
    """During a drag, logical_dx should equal -pointer_dx (finger left => scroll up).

    Only samples where the strip is actually moving are used, so the
    threshold-absorption sample and the clamp are excluded by construction.
    """
    errs = [r["logical_dx"] + r["pointer_dx"]
            for r in rows if r["pressed"] == 1 and r["state"] == 1 and r["pointer_dx"] != 0]
    if not errs:
        return dict(count=0)
    ordered = sorted(errs)
    n = len(ordered)

    def pct(p: float) -> int:
        return ordered[min(n - 1, int(p * (n - 1)))]

    return dict(
        count=n,
        min=ordered[0],
        max=ordered[-1],
        avg=sum(ordered) / n,
        p95=pct(0.95),
        p99=pct(0.99),
        nonzero=sum(1 for e in ordered if e != 0),
        abs_gt1=sum(1 for e in ordered if abs(e) > 1),
        exact_fraction=(n - sum(1 for e in ordered if e != 0)) / n,
    )


def coalescing(rows: list[dict]) -> dict:
    pressed = [r for r in rows if r["pressed"] == 1]
    dragging = [r for r in rows if r["pressed"] == 1 and r["state"] == 1]
    updates = sum(1 for r in rows if r["logical_dx"] != 0)
    frames = len({r["render_seq"] for r in rows})
    return dict(
        samples=len(rows),
        pressed_samples=len(pressed),
        dragging_samples=len(dragging),
        logical_updates=updates,
        render_frames_in_window=frames,
        samples_per_frame=(len(rows) / frames) if frames else 0.0,
    )


def render_cadence(rows: list[dict]) -> dict:
    """Interval between consecutive render_seq changes, in ms and panel frames."""
    stamps: list[int] = []
    prev = None
    for r in rows:
        if r["render_seq"] != prev:
            stamps.append(r["time_ms"])
            prev = r["render_seq"]
    deltas = []
    for a, b in zip(stamps, stamps[1:]):
        d = (b - a) % 65536
        if 0 < d < 1000:
            deltas.append(d)
    if not deltas:
        return dict(count=0)
    ordered = sorted(deltas)
    n = len(ordered)

    def pct(p: float) -> int:
        return ordered[min(n - 1, int(p * (n - 1)))]

    panel = [round(d / PANEL_FRAME_MS) for d in deltas]
    hist: dict[int, int] = {}
    for v in panel:
        hist[v] = hist.get(v, 0) + 1
    return dict(
        count=n,
        min=ordered[0],
        avg=sum(ordered) / n,
        median=pct(0.5),
        p95=pct(0.95),
        max=ordered[-1],
        panel_frames_hist=dict(sorted(hist.items())),
        uniform=(len(hist) == 1),
    )


def post_release(rows: list[dict]) -> dict:
    """Behaviour after the last pressed sample: inertia/snap would move lx again."""
    last = None
    for k, r in enumerate(rows):
        if r["pressed"] == 1:
            last = k
    if last is None:
        return dict(available=False)
    tail = rows[last + 1:]
    moves = [r["logical_dx"] for r in tail if r["logical_dx"] != 0]
    return dict(
        available=True,
        samples=len(tail),
        release_lx=rows[last]["logical_x"],
        final_lx=rows[-1]["logical_x"],
        drift=rows[-1]["logical_x"] - rows[last]["logical_x"],
        post_release_moves=len(moves),
        states=sorted({r["state"] for r in tail}),
    )


# --------------------------------------------------------------------------
# plots
# --------------------------------------------------------------------------

def _png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))


def write_png(path: Path, width: int, height: int, rgba: bytearray) -> None:
    rows = bytearray()
    stride = width * 4
    for y in range(height):
        rows.append(0)
        rows.extend(rgba[y * stride:(y + 1) * stride])
    payload = b"\x89PNG\r\n\x1a\n"
    payload += _png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    payload += _png_chunk(b"IDAT", zlib.compress(bytes(rows), 6))
    payload += _png_chunk(b"IEND", b"")
    path.write_bytes(payload)


class Canvas:
    """Minimal RGBA canvas so plots work without matplotlib."""

    def __init__(self, w: int, h: int, bg=(255, 255, 255)) -> None:
        self.w, self.h = w, h
        self.buf = bytearray(w * h * 4)
        for y in range(h):
            row = bytes(bg) + b"\xff"
            self.buf[y * w * 4:(y + 1) * w * 4] = row * w

    def px(self, x: int, y: int, color) -> None:
        if 0 <= x < self.w and 0 <= y < self.h:
            o = (y * self.w + x) * 4
            self.buf[o:o + 4] = bytes(color) + b"\xff"

    def line(self, x0: int, y0: int, x1: int, y1: int, color) -> None:
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            self.px(x0, y0, color)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def rect(self, x0: int, y0: int, x1: int, y1: int, color) -> None:
        for x in range(x0, x1):
            for y in range(y0, y1):
                self.px(x, y, color)

    def frame(self, color=(40, 40, 40)) -> None:
        self.rect(0, 0, self.w, 1, color)
        self.rect(0, self.h - 1, self.w, self.h, color)
        self.rect(0, 0, 1, self.h, color)
        self.rect(self.w - 1, 0, self.w, self.h, color)

    def axes(self, l=60, r=20, t=30, b=40, color=(120, 120, 120)) -> None:
        self.line(l, b, self.w - r, b, color)
        self.line(l, t, l, self.h - b, color)


def plot_series(path: Path, title: str, series: list[tuple[str, list[tuple[float, float]], tuple[int, int, int]]],
                width: int = 900, height: int = 320) -> None:
    c = Canvas(width, height)
    c.frame()
    c.axes()
    left, right, top, bottom = 60, 20, 30, 40
    xs = [x for _, pts, _ in series for x, _ in pts]
    ys = [y for _, pts, _ in series for _, y in pts]
    if not xs or not ys:
        write_png(path, width, height, c.buf)
        return
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    if x1 == x0:
        x1 = x0 + 1
    if y1 == y0:
        y1 = y0 + 1
    pad = (y1 - y0) * 0.08
    y0, y1 = y0 - pad, y1 + pad

    def sx(v: float) -> int:
        return int(left + (v - x0) / (x1 - x0) * (width - left - right))

    def sy(v: float) -> int:
        return int(height - bottom - (v - y0) / (y1 - y0) * (height - top - bottom))

    # zero line where meaningful
    if y0 < 0 < y1:
        c.line(left, sy(0), width - right, sy(0), (210, 210, 210))

    for _name, pts, color in series:
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            c.line(sx(ax), sy(ay), sx(bx), sy(by), color)

    write_png(path, width, height, c.buf)
    _ = title  # titles are recorded in the markdown report instead


def plot_with_matplotlib(outdir: Path, trace_csv: Path) -> bool:
    """Prefer matplotlib when it is actually installed."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception:
        return False

    cols: dict[str, list[float]] = {}
    with trace_csv.open() as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            for k, v in row.items():
                try:
                    cols.setdefault(k, []).append(float(v))
                except (TypeError, ValueError):
                    cols.setdefault(k, []).append(float("nan"))
    if not cols:
        return False

    t = cols.get("time_us", [])
    panels = [
        ("pointer_x vs time", "pointer_x"),
        ("logical_scroll_x vs time", "logical_x"),
        ("logical velocity vs time", "logical_velocity"),
        ("presentation interval vs time", "render_interval_ms"),
    ]
    for title, key in panels:
        if key not in cols:
            continue
        fig, ax = plt.subplots(figsize=(9, 3.2))
        ax.plot(t, cols[key], linewidth=1.0)
        ax.set_title(title)
        ax.set_xlabel("time_us")
        ax.grid(True, linewidth=0.3)
        fig.tight_layout()
        fig.savefig(outdir / f"plot_{key}.png", dpi=110)
        plt.close(fig)
    return True


# --------------------------------------------------------------------------
# report
# --------------------------------------------------------------------------

def build_rows(rows: list[dict]) -> list[dict]:
    out = []
    t0 = rows[0]["time_ms"] if rows else 0
    prev_t = None
    for r in rows:
        t = (r["time_ms"] - t0) % 65536
        dt = (t - prev_t) if prev_t is not None else 0
        touch_dx = r["pointer_dx"]
        logical_dx = r["logical_dx"]
        logical_velocity = (logical_dx / dt * 1000.0) if dt else 0.0
        touch_velocity = (touch_dx / dt * 1000.0) if dt else 0.0
        out.append(dict(
            time_ms=t,
            time_us=t * 1000,
            pointer_x=r["pointer_x"],
            touch_dx=touch_dx,
            touch_dt=dt,
            touch_velocity=touch_velocity,
            logical_x=r["logical_x"],
            logical_dx=logical_dx,
            logical_velocity=logical_velocity,
            position_error=logical_dx + touch_dx,
            pressed=r["pressed"],
            state=r["state"],
            render_seq=r["render_seq"],
            presentation_seq=r["render_seq"],
            render_interval_ms="",
        ))
        prev_t = t
    return out


def analyze(label: str, log: Path, outdir: Path) -> dict:
    rows = parse_trace(log)
    summary = parse_summary(log)
    csv_rows = build_rows(rows)

    # annotate each row with the interval to the next render_seq change
    last_change_at = None
    next_change: dict[int, int] = {}
    for r in csv_rows:
        next_change.setdefault(r["render_seq"], r["time_ms"])
    seqs = sorted(next_change)
    for a, b in zip(seqs, seqs[1:]):
        next_change[a] = next_change[b]
    for r in csv_rows:
        r["render_interval_ms"] = (next_change[r["render_seq"]] - r["time_ms"]) % 65536

    csv_path = outdir / f"motion_trace_{label}.csv"
    with csv_path.open("w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(csv_rows[0].keys()) if csv_rows else [])
        if csv_rows:
            writer.writeheader()
            writer.writerows(csv_rows)

    result = dict(
        label=label,
        log=str(log),
        summary=summary,
        position_error=position_error(rows),
        coalescing=coalescing(rows),
        render_cadence=render_cadence(rows),
        post_release=post_release(rows),
    )
    (outdir / f"motion_summary_{label}.json").write_text(json.dumps(result, indent=2))

    if csv_rows:
        t = [(r["time_us"] / 1e6, r["pointer_x"]) for r in csv_rows]
        l = [(r["time_us"] / 1e6, r["logical_x"]) for r in csv_rows]
        v = [(r["time_us"] / 1e6, r["logical_velocity"]) for r in csv_rows]
        ri = [(r["time_us"] / 1e6, r["render_interval_ms"]) for r in csv_rows]
        plot_series(outdir / f"plot_pointer_x_{label}.png", "pointer_x vs time", [("pointer_x", t, (200, 30, 30))])
        plot_series(outdir / f"plot_logical_x_{label}.png", "logical_scroll_x vs time", [("logical_x", l, (30, 90, 200))])
        plot_series(outdir / f"plot_logical_velocity_{label}.png", "logical velocity vs time", [("velocity", v, (20, 130, 60))])
        plot_series(outdir / f"plot_render_interval_{label}.png", "presentation interval vs time", [("interval", ri, (120, 60, 180))])
    return result


def fmt_err(err: dict) -> str:
    if not err.get("count"):
        return "n/a"
    return (f"min={err['min']} avg={err['avg']:.3f} p95={err['p95']} "
            f"p99={err['p99']} max={err['max']} exact={err['exact_fraction']*100:.1f}%")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--current", type=Path, required=True, help="MODE 0 (CURRENT_MOTION) gdb log")
    ap.add_argument("--direct", type=Path, required=True, help="MODE 1 (DIRECT_DRAG_ONLY) gdb log")
    ap.add_argument("--output", type=Path, default=Path("build/motion-isolation"))
    args = ap.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)

    cur = analyze("current", args.current, args.output)
    dct = analyze("direct", args.direct, args.output)

    used_mpl = plot_with_matplotlib(args.output, args.output / "motion_trace_direct.csv")

    print("Launcher Motion Isolation\n")
    for res in (cur, dct):
        name = "CURRENT_MOTION" if res["label"] == "current" else "DIRECT_DRAG_ONLY"
        print(f"=== {name} ===")
        print(f"  position error : {fmt_err(res['position_error'])}")
        c = res["coalescing"]
        print(f"  samples        : total={c['samples']} pressed={c['pressed_samples']} "
              f"dragging={c['dragging_samples']} logical_updates={c['logical_updates']}")
        print(f"  render frames  : {c['render_frames_in_window']} "
              f"({c['samples_per_frame']:.2f} samples/frame -> coalescing)")
        cad = res["render_cadence"]
        if cad.get("count"):
            print(f"  render cadence : min={cad['min']} med={cad['median']} avg={cad['avg']:.1f} "
                  f"p95={cad['p95']} max={cad['max']} ms")
            print(f"  panel frames   : {cad['panel_frames_hist']}  uniform={cad['uniform']}")
        pr = res["post_release"]
        if pr.get("available"):
            print(f"  post-release   : drift={pr['drift']:+d} px over {pr['samples']} samples, "
                  f"moves={pr['post_release_moves']}, states={pr['states']}")
        print()

    print(f"plots written to {args.output}/ (matplotlib={'yes' if used_mpl else 'no, stdlib PNG'})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
