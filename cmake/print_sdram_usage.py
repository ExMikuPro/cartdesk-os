#!/usr/bin/env python3
"""Print a focused SDRAM usage summary from the linker map."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


SDRAM_REGION_NAMES = (
    "SDRAM_LAYER1_FB0",
    "SDRAM_LAYER1_FB1",
    "SDRAM_LAYER2_FB0",
    "SDRAM_LVGL_HEAP",
    "SDRAM_DMA_POOL",
    "SDRAM_LAUNCHER",
    "SDRAM_APP_ARENA",
)

COLD_POOL_SIZE = 0x00800000
LUA_HEAP_SIZE = 0x00200000


def parse_int(value: str) -> int:
    return int(value, 0)


def fmt_size(value: int) -> str:
    if value >= 1024 * 1024:
        return f"{value / (1024 * 1024):.2f} MiB"
    if value >= 1024:
        return f"{value / 1024:.2f} KiB"
    return f"{value} B"


def parse_memory_regions(lines: list[str]) -> dict[str, tuple[int, int]]:
    regions: dict[str, tuple[int, int]] = {}
    pattern = re.compile(r"^(\S+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+")

    for line in lines:
        match = pattern.match(line)
        if not match:
            continue

        name = match.group(1)
        if name in SDRAM_REGION_NAMES:
            regions[name] = (parse_int(match.group(2)), parse_int(match.group(3)))

    return regions


def parse_output_sections(lines: list[str]) -> list[tuple[str, int, int]]:
    sections: list[tuple[str, int, int]] = []
    same_line = re.compile(r"^(\.[A-Za-z0-9_.$-]+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\b")
    next_line = re.compile(r"^\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\b")

    index = 0
    while index < len(lines):
        line = lines[index].rstrip()

        match = same_line.match(line)
        if match:
            sections.append((match.group(1), parse_int(match.group(2)), parse_int(match.group(3))))
            index += 1
            continue

        if line.startswith(".") and " " not in line and "\t" not in line and index + 1 < len(lines):
            match = next_line.match(lines[index + 1])
            if match:
                sections.append((line, parse_int(match.group(1)), parse_int(match.group(2))))
                index += 2
                continue

        index += 1

    return sections


def section_usage_by_region(
    regions: dict[str, tuple[int, int]],
    sections: list[tuple[str, int, int]],
) -> dict[str, int]:
    usage = {name: 0 for name in SDRAM_REGION_NAMES}

    for _, addr, size in sections:
        if size == 0:
            continue

        for name, (origin, length) in regions.items():
            if origin <= addr < origin + length:
                usage[name] += size
                break

    return usage


def print_region(name: str, origin: int, length: int, used: int) -> None:
    percent = (used / length * 100.0) if length else 0.0
    end = origin + length - 1
    print(
        f"  {name:<18} 0x{origin:08X}-0x{end:08X}  "
        f"static {fmt_size(used):>10} / {fmt_size(length):>10}  {percent:6.2f}%"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--map", required=True, type=Path, help="Linker map file")
    args = parser.parse_args()

    if not args.map.exists():
        print(f"[sdram] map file not found: {args.map}")
        return 0

    lines = args.map.read_text(encoding="utf-8", errors="replace").splitlines()
    regions = parse_memory_regions(lines)
    missing = [name for name in SDRAM_REGION_NAMES if name not in regions]
    if missing:
        print(f"[sdram] missing MEMORY regions in map: {', '.join(missing)}")
        return 0

    sections = parse_output_sections(lines)
    usage = section_usage_by_region(regions, sections)

    total_static = sum(usage.values())
    total_size = sum(regions[name][1] for name in SDRAM_REGION_NAMES)
    total_percent = total_static / total_size * 100.0

    app_origin, app_length = regions["SDRAM_APP_ARENA"]
    app_end = app_origin + app_length - 1
    lua_heap_base = app_origin
    lua_heap_end = lua_heap_base + LUA_HEAP_SIZE - 1
    cold_base = app_end + 1 - COLD_POOL_SIZE
    resource_base = lua_heap_end + 1
    resource_end = cold_base - 1
    resource_size = resource_end - resource_base + 1

    fb_size = (
        regions["SDRAM_LAYER1_FB0"][1]
        + regions["SDRAM_LAYER1_FB1"][1]
        + regions["SDRAM_LAYER2_FB0"][1]
    )

    print("")
    print("========== SDRAM usage ==========")
    print("Static linked SDRAM sections:")
    for name in SDRAM_REGION_NAMES:
        origin, length = regions[name]
        print_region(name, origin, length, usage[name])

    print(
        f"  {'SDRAM_TOTAL':<18} 0x{regions['SDRAM_LAYER1_FB0'][0]:08X}-0x{app_end:08X}  "
        f"static {fmt_size(total_static):>10} / {fmt_size(total_size):>10}  {total_percent:6.2f}%"
    )

    print("")
    print("Runtime SDRAM windows:")
    print(f"  FB reserved       {fmt_size(fb_size):>10}  Layer1_FB0 + Layer1_FB1 + Layer2_FB0")
    print(f"  LVGL_HEAP window  {fmt_size(regions['SDRAM_LVGL_HEAP'][1]):>10}  0x{regions['SDRAM_LVGL_HEAP'][0]:08X}-0x{regions['SDRAM_LVGL_HEAP'][0] + regions['SDRAM_LVGL_HEAP'][1] - 1:08X}")
    print(f"  DMA_POOL window   {fmt_size(regions['SDRAM_DMA_POOL'][1]):>10}  0x{regions['SDRAM_DMA_POOL'][0]:08X}-0x{regions['SDRAM_DMA_POOL'][0] + regions['SDRAM_DMA_POOL'][1] - 1:08X}")
    print(f"  LAUNCHER_CACHE    {fmt_size(regions['SDRAM_LAUNCHER'][1]):>10}  static used {fmt_size(usage['SDRAM_LAUNCHER'])}")
    print(f"  LUA_HEAP          {fmt_size(LUA_HEAP_SIZE):>10}  0x{lua_heap_base:08X}-0x{lua_heap_end:08X}")
    print(f"  RESOURCE_ARENA    {fmt_size(resource_size):>10}  0x{resource_base:08X}-0x{resource_end:08X}")
    print(f"  COLD_POOL         {fmt_size(COLD_POOL_SIZE):>10}  0x{cold_base:08X}-0x{app_end:08X}")
    print("Note: runtime allocations inside LUA_HEAP/DMA_POOL/RESOURCE_ARENA/COLD_POOL are not visible in the map.")
    print("=================================")
    print("")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
