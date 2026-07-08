#!/usr/bin/env python3
"""Inspect and extract Data East arcade-wrapper PACK asset containers.

Observed in Zeebo arcade ports such as Caveman Ninja / Joe & Mac, Spinmaster,
Street Hoop, Super BurgerTime, Karnov's Revenge, Wizard Fire, Magical Drop III,
and Dark Seal.

The known layout is intentionally narrow and trace-backed:

    0x00  char[4]  "PACK"
    0x04  u32      entry count
    0x08  u32      package body/end size field
    0x0c..0x10b    reserved/zero area
    0x10c          entry table, 20 bytes per entry

Each observed entry is:

    u32 kind
    u32 asset_id
    u32 compressed_size
    u32 decompressed_size
    u32 payload_offset

Payload offsets are relative to the start of the PACK container. Observed
payloads are zlib streams and decompressed_size matches zlib output.
"""

from __future__ import annotations

import argparse
import json
import math
import zlib
from dataclasses import dataclass
from pathlib import Path


PACK_MAGIC = b"PACK"
PACK_TABLE_OFFSET = 0x10C
PACK_ENTRY_SIZE = 20


@dataclass
class PackEntry:
    index: int
    kind: int
    asset_id: int
    compressed_size: int
    decompressed_size: int
    payload_offset: int
    absolute_offset: int
    zlib_ok: bool
    actual_decompressed_size: int
    first16: str
    ascii_preview: str
    entropy: float
    error: str = ""


@dataclass
class PackFile:
    path: Path
    pack_offset: int
    entry_count: int
    body_size: int
    entries: list[PackEntry]


def u32(data: bytes, off: int) -> int:
    if off + 4 > len(data):
        return 0
    return data[off] | (data[off + 1] << 8) | (data[off + 2] << 16) | (data[off + 3] << 24)


def hex_bytes(data: bytes, count: int = 16) -> str:
    return " ".join(f"{value:02x}" for value in data[:count])


def ascii_preview(data: bytes, count: int = 48) -> str:
    out = []
    for value in data[:count]:
        out.append(chr(value) if 32 <= value < 127 else ".")
    return "".join(out)


def shannon_entropy(data: bytes) -> float:
    if not data:
        return 0.0
    counts = [0] * 256
    for value in data:
        counts[value] += 1
    entropy = 0.0
    size = len(data)
    for count in counts:
        if not count:
            continue
        p = count / size
        entropy -= p * math.log2(p)
    return entropy


def find_pack_offset(data: bytes, explicit_offset: int | None) -> int:
    if explicit_offset is not None:
        if explicit_offset < 0 or explicit_offset + 4 > len(data):
            raise ValueError(f"PACK offset 0x{explicit_offset:x} is outside the file")
        if data[explicit_offset:explicit_offset + 4] != PACK_MAGIC:
            raise ValueError(f"offset 0x{explicit_offset:x} does not point to PACK magic")
        return explicit_offset

    if data.startswith(PACK_MAGIC):
        return 0

    offset = data.find(PACK_MAGIC)
    if offset < 0:
        raise ValueError("PACK magic not found")
    return offset


def parse_pack(path: Path, explicit_offset: int | None = None) -> PackFile:
    data = path.read_bytes()
    pack_offset = find_pack_offset(data, explicit_offset)
    entry_count = u32(data, pack_offset + 4)
    body_size = u32(data, pack_offset + 8)
    table_offset = pack_offset + PACK_TABLE_OFFSET
    table_end = table_offset + entry_count * PACK_ENTRY_SIZE

    if table_end > len(data):
        raise ValueError(
            f"entry table ends at 0x{table_end:x}, beyond file size 0x{len(data):x}"
        )

    entries: list[PackEntry] = []
    for index in range(entry_count):
        entry_off = table_offset + index * PACK_ENTRY_SIZE
        kind = u32(data, entry_off)
        asset_id = u32(data, entry_off + 4)
        compressed_size = u32(data, entry_off + 8)
        decompressed_size = u32(data, entry_off + 12)
        payload_offset = u32(data, entry_off + 16)
        absolute_offset = pack_offset + payload_offset
        payload_end = absolute_offset + compressed_size

        error = ""
        decompressed = b""
        zlib_ok = False
        if absolute_offset < pack_offset or payload_end > len(data):
            error = (
                f"payload range 0x{absolute_offset:x}..0x{payload_end:x} "
                f"is outside file size 0x{len(data):x}"
            )
        else:
            payload = data[absolute_offset:payload_end]
            try:
                decompressed = zlib.decompress(payload)
                zlib_ok = len(decompressed) == decompressed_size
                if not zlib_ok:
                    error = (
                        f"decompressed size mismatch: expected 0x{decompressed_size:x}, "
                        f"got 0x{len(decompressed):x}"
                    )
            except zlib.error as exc:
                error = str(exc)

        entries.append(
            PackEntry(
                index=index,
                kind=kind,
                asset_id=asset_id,
                compressed_size=compressed_size,
                decompressed_size=decompressed_size,
                payload_offset=payload_offset,
                absolute_offset=absolute_offset,
                zlib_ok=zlib_ok,
                actual_decompressed_size=len(decompressed),
                first16=hex_bytes(decompressed),
                ascii_preview=ascii_preview(decompressed),
                entropy=shannon_entropy(decompressed),
                error=error,
            )
        )

    return PackFile(
        path=path,
        pack_offset=pack_offset,
        entry_count=entry_count,
        body_size=body_size,
        entries=entries,
    )


def pack_to_json(pack: PackFile) -> dict:
    return {
        "path": str(pack.path),
        "pack_offset": pack.pack_offset,
        "entry_count": pack.entry_count,
        "body_size": pack.body_size,
        "entries": [
            {
                "index": entry.index,
                "kind": entry.kind,
                "asset_id": entry.asset_id,
                "compressed_size": entry.compressed_size,
                "decompressed_size": entry.decompressed_size,
                "payload_offset": entry.payload_offset,
                "absolute_offset": entry.absolute_offset,
                "zlib_ok": entry.zlib_ok,
                "actual_decompressed_size": entry.actual_decompressed_size,
                "first16": entry.first16,
                "ascii_preview": entry.ascii_preview,
                "entropy": entry.entropy,
                "error": entry.error,
            }
            for entry in pack.entries
        ],
    }


def print_entries(pack: PackFile, analyze: bool) -> None:
    print(
        f"format=pack path={pack.path} pack_offset=0x{pack.pack_offset:x} "
        f"entries={pack.entry_count} body_size=0x{pack.body_size:x}"
    )
    for entry in pack.entries:
        status = "ok" if entry.zlib_ok else "bad"
        print(
            f"[{entry.index:02d}] kind=0x{entry.kind:08x} "
            f"id=0x{entry.asset_id:08x} csize=0x{entry.compressed_size:06x} "
            f"dsize=0x{entry.decompressed_size:06x} off=0x{entry.payload_offset:06x} "
            f"abs=0x{entry.absolute_offset:06x} zlib={status}"
        )
        if entry.error:
            print(f"     error={entry.error}")
        if analyze:
            print(
                f"     first16={entry.first16} entropy={entry.entropy:.3f} "
                f"ascii={entry.ascii_preview}"
            )


def extract_entries(path: Path, pack: PackFile, out_dir: Path) -> None:
    data = path.read_bytes()
    out_dir.mkdir(parents=True, exist_ok=True)
    for entry in pack.entries:
        if not entry.zlib_ok:
            print(f"skip entry {entry.index:02d}: {entry.error or 'zlib failed'}")
            continue
        payload = data[entry.absolute_offset:entry.absolute_offset + entry.compressed_size]
        decompressed = zlib.decompress(payload)
        out_path = out_dir / f"entry_{entry.index:02d}_{entry.asset_id:08x}.bin"
        out_path.write_bytes(decompressed)
        print(f"wrote {out_path} ({len(decompressed)} bytes)")


def parse_int(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect or extract Zeebo PACK asset containers")
    parser.add_argument("pack", type=Path, help="PACK/.pkg file or module containing PACK magic")
    parser.add_argument("--offset", type=parse_int, help="Explicit PACK offset, e.g. 0x1bf14c")
    parser.add_argument("--json", action="store_true", help="Emit JSON")
    parser.add_argument("--analyze", action="store_true", help="Print entry hexdumps, entropy, and ASCII previews")
    parser.add_argument("--extract", type=Path, help="Extract decompressed entries to this directory")
    args = parser.parse_args()

    try:
        pack = parse_pack(args.pack, args.offset)
    except (OSError, ValueError) as exc:
        print(f"{args.pack}: {exc}")
        return 1

    if args.json:
        print(json.dumps(pack_to_json(pack), indent=2))
    else:
        print_entries(pack, args.analyze)

    if args.extract:
        extract_entries(args.pack, pack, args.extract)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
