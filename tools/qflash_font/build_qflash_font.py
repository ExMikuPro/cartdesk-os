#!/usr/bin/env python3
"""Build and inspect CartDesk QFLASH A8 font packs."""

from __future__ import annotations

import argparse
import binascii
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from PIL import ImageFont


MAGIC = b"QFNT"
VERSION = 1
HEADER = struct.Struct("<4sHH6I32s")
FONT = struct.Struct("<HHhh6I")
GLYPH = struct.Struct("<IIHHHhhH")
HEADER_SIZE = 64
MAX_REGION_SIZE = 16 * 1024 * 1024


@dataclass
class BuiltFont:
    size: int
    line_height: int
    baseline: int
    glyphs: list[tuple[int, int, int, int, int, int, int]]
    bitmap: bytes


def parse_charset(font_path: Path) -> list[int]:
    try:
        charset = subprocess.check_output(
            ["fc-query", "--format=%{charset}", str(font_path)],
            text=True,
        )
    except FileNotFoundError as exc:
        raise SystemExit("缺少 fc-query，请安装 fontconfig。") from exc
    except subprocess.CalledProcessError as exc:
        raise SystemExit(f"fc-query 无法读取字体：{font_path}") from exc

    codepoints: set[int] = set()
    for token in charset.split():
        if "-" in token:
            first, last = token.split("-", 1)
            codepoints.update(range(int(first, 16), int(last, 16) + 1))
        else:
            codepoints.add(int(token, 16))

    return sorted(cp for cp in codepoints if 0 < cp <= 0x10FFFF)


def build_size(font_path: Path, size: int, codepoints: list[int]) -> BuiltFont:
    font = ImageFont.truetype(str(font_path), size)
    ascent, descent = font.getmetrics()
    glyphs: list[tuple[int, int, int, int, int, int, int]] = []
    bitmap = bytearray()

    for codepoint in codepoints:
        character = chr(codepoint)
        mask, (left, top) = font.getmask2(character, mode="L", anchor="ls")
        width, height = mask.size
        advance = max(0, min(0xFFFF, int(round(font.getlength(character)))))
        bitmap_offset = len(bitmap)

        if width and height:
            bitmap.extend(bytes(mask))

        # LVGL's ofs_y is the distance from the glyph box bottom to baseline.
        offset_y = -(top + height)
        glyphs.append(
            (
                codepoint,
                bitmap_offset,
                advance,
                width,
                height,
                left,
                offset_y,
            )
        )

    return BuiltFont(
        size=size,
        line_height=ascent + descent,
        baseline=descent,
        glyphs=glyphs,
        bitmap=bytes(bitmap),
    )


def align4(value: int) -> int:
    return (value + 3) & ~3


def build_pack(font_path: Path, sizes: list[int]) -> bytes:
    codepoints = parse_charset(font_path)
    fonts = [build_size(font_path, size, codepoints) for size in sizes]

    font_table_offset = HEADER_SIZE
    cursor = align4(font_table_offset + len(fonts) * FONT.size)
    glyph_offsets: list[int] = []
    bitmap_offsets: list[int] = []

    for built in fonts:
        glyph_offsets.append(cursor)
        cursor = align4(cursor + len(built.glyphs) * GLYPH.size)
        bitmap_offsets.append(cursor)
        cursor = align4(cursor + len(built.bitmap))

    total_size = cursor
    if total_size > MAX_REGION_SIZE:
        raise SystemExit(
            f"字体包 {total_size / 1024 / 1024:.2f} MiB 超过 QFLASH 前 16 MiB 分区"
        )

    output = bytearray(total_size)
    for index, built in enumerate(fonts):
        font_record_offset = font_table_offset + index * FONT.size
        output[font_record_offset : font_record_offset + FONT.size] = FONT.pack(
            built.size,
            0,
            built.line_height,
            built.baseline,
            len(built.glyphs),
            glyph_offsets[index],
            bitmap_offsets[index],
            len(built.bitmap),
            0,
            0,
        )

        glyph_cursor = glyph_offsets[index]
        for codepoint, relative_bitmap, advance, width, height, offset_x, offset_y in built.glyphs:
            record = GLYPH.pack(
                codepoint,
                bitmap_offsets[index] + relative_bitmap,
                advance,
                width,
                height,
                offset_x,
                offset_y,
                0,
            )
            output[glyph_cursor : glyph_cursor + GLYPH.size] = record
            glyph_cursor += GLYPH.size

        start = bitmap_offsets[index]
        output[start : start + len(built.bitmap)] = built.bitmap

    payload_crc = binascii.crc32(output[HEADER_SIZE:]) & 0xFFFFFFFF
    output[:HEADER_SIZE] = HEADER.pack(
        MAGIC,
        VERSION,
        HEADER_SIZE,
        total_size,
        len(fonts),
        font_table_offset,
        payload_crc,
        1,  # bit 0: glyph pixels are A8
        0,
        b"\0" * 32,
    )
    return bytes(output)


def inspect_pack(path: Path) -> int:
    data = path.read_bytes()
    if len(data) < HEADER_SIZE:
        print("文件小于 QFNT 头", file=sys.stderr)
        return 1

    magic, version, header_size, total_size, font_count, table_offset, crc, flags, _, _ = HEADER.unpack_from(data)
    actual_crc = binascii.crc32(data[header_size:total_size]) & 0xFFFFFFFF
    valid = (
        magic == MAGIC
        and version == VERSION
        and header_size == HEADER_SIZE
        and total_size == len(data)
        and actual_crc == crc
    )
    print(
        f"QFNT version={version} size={total_size} fonts={font_count} "
        f"flags=0x{flags:08x} crc32=0x{crc:08x} valid={valid}"
    )
    if not valid:
        return 1

    for index in range(font_count):
        record = FONT.unpack_from(data, table_offset + index * FONT.size)
        size, _, line_height, baseline, glyph_count, glyph_offset, bitmap_offset, bitmap_size, _, _ = record
        print(
            f"  {size}px: line_height={line_height} baseline={baseline} "
            f"glyphs={glyph_count} index=0x{glyph_offset:x} "
            f"bitmap=0x{bitmap_offset:x}+{bitmap_size}"
        )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    build_parser = subparsers.add_parser("build", help="从 TTF/OTF 生成 QFNT")
    build_parser.add_argument("font", type=Path)
    build_parser.add_argument("output", type=Path)
    build_parser.add_argument("--sizes", nargs="+", type=int, default=[16, 20, 24])

    inspect_parser = subparsers.add_parser("inspect", help="校验并显示 QFNT 信息")
    inspect_parser.add_argument("font_pack", type=Path)

    args = parser.parse_args()
    if args.command == "inspect":
        return inspect_pack(args.font_pack)

    font_path = args.font.resolve()
    sizes = sorted(set(args.sizes))
    if not font_path.is_file():
        parser.error(f"字体不存在：{font_path}")
    if any(size <= 0 or size > 255 for size in sizes):
        parser.error("字号必须位于 1..255")

    output = build_pack(font_path, sizes)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(
        f"已生成 {args.output}：{len(output) / 1024 / 1024:.2f} MiB，"
        f"字号={','.join(map(str, sizes))}，写入 QFLASH 偏移 0x00000000"
    )
    return inspect_pack(args.output)


if __name__ == "__main__":
    raise SystemExit(main())
