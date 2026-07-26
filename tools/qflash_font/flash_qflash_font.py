#!/usr/bin/env python3
"""Program a QFNT pack through OpenOCD/GDB and the firmware-side helper."""

from __future__ import annotations

import argparse
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path


QFNT_MAGIC = b"QFNT"
QFLASH_FONT_REGION_SIZE = 16 * 1024 * 1024
# QUADSPI dual-flash mode erases a 64 KiB block on both chips at once.
PROGRAM_BLOCK_SIZE = 128 * 1024


def validate_pack(path: Path) -> int:
    size = path.stat().st_size
    if size < 64 or size > QFLASH_FONT_REGION_SIZE:
        raise SystemExit(f"QFNT 大小非法：{size} bytes")
    with path.open("rb") as stream:
        magic, version, header_size, total_size = struct.unpack("<4sHHI", stream.read(12))
    if magic != QFNT_MAGIC or version != 1 or header_size != 64 or total_size != size:
        raise SystemExit("QFNT 文件头、版本或长度不匹配")
    return size


def reserve_local_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def wait_for_port(port: int, process: subprocess.Popen[str], timeout: float = 10.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("OpenOCD 在 GDB 连接前退出")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.1)
    raise RuntimeError("等待 OpenOCD GDB 端口超时")


def split_pack(pack: Path, directory: Path) -> list[tuple[Path, int, int]]:
    chunks: list[tuple[Path, int, int]] = []
    with pack.open("rb") as stream:
        offset = 0
        while True:
            data = stream.read(PROGRAM_BLOCK_SIZE)
            if not data:
                break
            chunk_path = directory / f"font-{offset:08x}.bin"
            chunk_path.write_bytes(data)
            chunks.append((chunk_path, offset, len(data)))
            offset += len(data)
    return chunks


def gdb_commands(elf: Path,
                 chunks: list[tuple[Path, int, int]],
                 port: int,
                 pack_size: int) -> str:
    buffer_address = 0xD3800000
    lines = [
        "set pagination off",
        "set confirm off",
        "set breakpoint pending on",
        f"target extended-remote 127.0.0.1:{port}",
        "monitor reset halt",
        "load",
        "tbreak QFlashFont_ProgrammerReady",
        "continue",
        "set $result = (int)QFlashFont_ProgramBegin()",
        "if $result != 0",
        '  printf "QFlashFont_ProgramBegin failed: %d\\n", $result',
        "  quit 1",
        "end",
    ]

    completed = 0
    for chunk_path, offset, length in chunks:
        completed += length
        percentage = completed * 100 // pack_size
        lines.extend(
            [
                f"restore {chunk_path} binary 0x{buffer_address:08x}",
                (
                    "set $result = (int)QFlashFont_ProgramBlock("
                    f"{offset}u, (const void *)0x{buffer_address:08x}, {length}u)"
                ),
                "if $result != 0",
                (
                    f'  printf "QFLASH block failed at 0x{offset:08x}: %d\\n", '
                    "$result"
                ),
                "  quit 1",
                "end",
                (
                    f'printf "QFLASH font: {completed}/{pack_size} bytes '
                    f'({percentage}%%)\\n"'
                ),
            ]
        )

    lines.extend(
        [
            "set $result = (int)QFlashFont_ProgramFinish()",
            "if $result != 0",
            '  printf "QFlashFont_ProgramFinish failed: %d\\n", $result',
            "  quit 1",
            "end",
            'printf "QFLASH font programming and block verification passed.\\n"',
            "monitor reset run",
            "disconnect",
            "quit",
        ]
    )
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--font-pack", required=True, type=Path)
    parser.add_argument("--openocd", required=True, type=Path)
    parser.add_argument("--gdb", required=True, type=Path)
    parser.add_argument("--openocd-config", required=True, type=Path)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    elf = args.elf.resolve()
    pack = args.font_pack.resolve()
    config = args.openocd_config.resolve()
    for path in (elf, pack, args.openocd, args.gdb, config):
        if not path.exists():
            parser.error(f"文件不存在：{path}")
    pack_size = validate_pack(pack)

    if args.dry_run:
        print(
            f"dry-run OK: elf={elf}, pack={pack} ({pack_size} bytes), "
            f"blocks={(pack_size + PROGRAM_BLOCK_SIZE - 1) // PROGRAM_BLOCK_SIZE}"
        )
        return 0

    port = reserve_local_port()
    with tempfile.TemporaryDirectory(prefix="cartdesk-qflash-") as temp_dir:
        temp = Path(temp_dir)
        openocd_log = temp / "openocd.log"
        command_file = temp / "flash-font.gdb"
        chunks = split_pack(pack, temp)
        command_file.write_text(
            gdb_commands(elf, chunks, port, pack_size),
            encoding="utf-8",
        )

        with openocd_log.open("w", encoding="utf-8") as log:
            openocd = subprocess.Popen(
                [
                    str(args.openocd),
                    "-f",
                    str(config),
                    "-c",
                    f"gdb_port {port}",
                    "-c",
                    "tcl_port disabled",
                    "-c",
                    "telnet_port disabled",
                ],
                stdout=log,
                stderr=subprocess.STDOUT,
                text=True,
            )
            try:
                wait_for_port(port, openocd)
                completed = subprocess.run(
                    [
                        str(args.gdb),
                        "--batch",
                        "--quiet",
                        str(elf),
                        "-x",
                        str(command_file),
                    ],
                    check=False,
                )
                if completed.returncode != 0:
                    print(openocd_log.read_text(encoding="utf-8"), file=sys.stderr)
                    return completed.returncode
            finally:
                openocd.terminate()
                try:
                    openocd.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    openocd.kill()
                    openocd.wait()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
