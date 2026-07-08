#!/usr/bin/env python3
"""Inspect and extract Zeebo/BREW BAR-like resource files.

The parser mirrors Zeemu's runtime BAR handling in brew/BrewShellResources.cpp,
but is standalone so resource archives can be inspected without launching the
emulator.

Some commercial titles also use the .bar extension for custom binary resource
tables. Those are not BREW BAR archives, but this tool still exposes their
indexed chunks so the payloads can be inspected without guessing inside the
emulator.
"""

from __future__ import annotations

import argparse
import gzip
import json
import math
import re
from dataclasses import dataclass
from pathlib import Path


@dataclass
class BarEntry:
    res_id: int
    mime: str
    data: bytes
    offset: int
    raw_size: int


@dataclass
class ChunkAnalysis:
    signature: str
    entropy: float
    first16: str
    ascii_preview: str
    hints: list[str]


@dataclass
class BarResource:
    base_id: int = 0
    res_type: int = 0
    format_name: str = "brew-bar"
    entries: list[BarEntry] | None = None

    def __post_init__(self) -> None:
        if self.entries is None:
            self.entries = []


def u16(data: bytes, off: int) -> int:
    if off + 2 > len(data):
        return 0
    return data[off] | (data[off + 1] << 8)


def u32(data: bytes, off: int) -> int:
    if off + 4 > len(data):
        return 0
    return data[off] | (data[off + 1] << 8) | (data[off + 2] << 16) | (data[off + 3] << 24)


def be16(data: bytes, off: int) -> int:
    if off + 2 > len(data):
        return 0
    return (data[off] << 8) | data[off + 1]


def bytes_mime(data: bytes) -> str:
    if data.startswith(b"\x89PNG\r\n\x1a\n"):
        return "image/png"
    if data.startswith(b"BM"):
        return "image/bmp"
    if data.startswith(b"\xff\xd8\xff"):
        return "image/jpeg"
    if data.startswith((b"GIF87a", b"GIF89a")):
        return "image/gif"
    if looks_like_tga(data):
        return "image/x-tga"
    if data.startswith(b"RIFF") and len(data) >= 12 and data[8:12] == b"WAVE":
        return "audio/wav"
    if data.startswith(b"RIFF") and len(data) >= 12 and data[8:12] == b"QLCM":
        return "audio/qcp"
    if data.startswith(b"RIFF") and len(data) >= 12 and data[8:12] == b"DLS ":
        return "audio/dls"
    if data.startswith(b"MThd"):
        return "audio/mid"
    if data.startswith((b"#!AMR\n", b"#!AMR-WB\n")):
        return "audio/amr"
    if looks_like_adts_aac(data):
        return "audio/aac"
    if data.startswith(b"ID3") or looks_like_mp3_frame(data):
        return "audio/mp3"
    if data.startswith(b"MMMD"):
        return "audio/mmf"
    if data.startswith(b"XMF_"):
        return "audio/xmf"
    if data.startswith(b"cmid"):
        return "video/pmd"
    if looks_like_imelody(data):
        return "audio/imy"
    if looks_like_iso_media(data):
        return "video/mp4"
    if data.startswith(b"\x30\x26\xb2\x75\x8e\x66\xcf\x11\xa6\xd9\x00\xaa\x00\x62\xce\x6c"):
        return "video/wmv"
    if looks_like_svg(data):
        return "image/svg+xml"
    if data.startswith(b"\x1f\x8b"):
        try:
            if looks_like_svg(gzip.decompress(data)):
                return "image/svg+xml"
        except (OSError, EOFError):
            pass
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return "text/plain"
    if looks_like_utf16le(data):
        return "text/plain"
    return "application/octet-stream"


def looks_like_mp3_frame(data: bytes) -> bool:
    return len(data) >= 2 and data[0] == 0xFF and (data[1] & 0xE0) == 0xE0


def looks_like_adts_aac(data: bytes) -> bool:
    return len(data) >= 2 and data[0] == 0xFF and (data[1] & 0xF6) == 0xF0


def looks_like_iso_media(data: bytes) -> bool:
    if len(data) < 12 or data[4:8] != b"ftyp":
        return False
    major = data[8:12]
    compatible = data[16:64]
    brands = (b"isom", b"iso2", b"mp41", b"mp42", b"3gp", b"3g2", b"M4V ", b"M4A ")
    return any(major.startswith(brand) or brand in compatible for brand in brands)


def looks_like_tga(data: bytes) -> bool:
    if len(data) < 18:
        return False
    id_len = data[0]
    color_map_type = data[1]
    image_type = data[2]
    if color_map_type not in (0, 1):
        return False
    if image_type not in (1, 2, 3, 9, 10, 11):
        return False
    if color_map_type == 0 and image_type in (1, 9):
        return False

    color_map_len = u16(data, 5)
    color_map_depth = data[7]
    if color_map_type == 1:
        if color_map_len == 0 or color_map_depth not in (15, 16, 24, 32):
            return False
    elif color_map_len != 0:
        return False

    width = u16(data, 12)
    height = u16(data, 14)
    pixel_depth = data[16]
    descriptor = data[17]
    if width == 0 or height == 0 or width > 8192 or height > 8192:
        return False
    if pixel_depth not in (8, 15, 16, 24, 32):
        return False
    if descriptor & 0xC0:
        return False

    color_map_bytes = 0
    if color_map_type == 1:
        color_map_bytes = color_map_len * ((color_map_depth + 7) // 8)
    header_end = 18 + id_len + color_map_bytes
    if header_end > len(data):
        return False

    if image_type in (1, 2, 3):
        pixel_bytes = ((pixel_depth + 7) // 8) * width * height
        expected = header_end + pixel_bytes
        return expected <= len(data) and len(data) - expected <= 4096

    return header_end < len(data)


def looks_like_imelody(data: bytes) -> bool:
    head = data[:256].lstrip().upper()
    return head.startswith(b"BEGIN:IMELODY") or b"\nBEGIN:IMELODY" in head


def looks_like_svg(data: bytes) -> bool:
    sample = data[:4096]
    if sample.startswith(b"\xef\xbb\xbf"):
        sample = sample[3:]
    head = sample.lstrip().lower()
    if head.startswith(b"<svg"):
        return True
    if head.startswith(b"<?xml"):
        svg_pos = head.find(b"<svg")
        return 0 <= svg_pos < 512
    if head.startswith(b"<!doctype"):
        svg_pos = head.find(b"svg")
        return 0 <= svg_pos < 128
    return False


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


def hex_bytes(data: bytes, count: int = 16) -> str:
    return " ".join(f"{value:02x}" for value in data[:count])


def ascii_preview(data: bytes, count: int = 32) -> str:
    out = []
    for value in data[:count]:
        out.append(chr(value) if 32 <= value < 127 else ".")
    return "".join(out)


def printable_ratio(data: bytes) -> float:
    if not data:
        return 0.0
    printable = sum(1 for value in data if value in (9, 10, 13) or 32 <= value < 127)
    return printable / len(data)


def count_zero_words_be(data: bytes) -> int:
    return sum(1 for off in range(0, len(data) - 1, 2) if be16(data, off) == 0)


def monotonic_be16_prefix(data: bytes) -> list[int]:
    values: list[int] = []
    prev = -1
    for off in range(0, min(len(data), 0x80), 2):
        cur = be16(data, off)
        if cur == 0 or cur <= prev or cur > len(data):
            break
        values.append(cur)
        prev = cur
    return values


def detect_chunk(data: bytes) -> ChunkAnalysis:
    mime = bytes_mime(data)
    hints: list[str] = []
    if mime != "application/octet-stream":
        hints.append(mime)
    if len(data) >= 0x4000:
        entry_count = u32(data, 0)
        data_start = u32(data, 4)
        marker = u32(data, 12)
        if 0 < entry_count < 10000 and data_start == 0x4000 and marker == 0x0100005D:
            hints.append(f"ea-pak-header:count={entry_count}")
            if shannon_entropy(data[: min(len(data), 0x4000)]) > 7.6:
                hints.append("high-entropy-index")
    if data.startswith(b"\x78\x01") or data.startswith(b"\x78\x9c") or data.startswith(b"\x78\xda"):
        hints.append("zlib-stream")
    if data.startswith(b"\x1f\x8b"):
        hints.append("gzip-stream")
    if printable_ratio(data) > 0.75 and len(data) >= 8:
        hints.append("mostly-ascii")
    if looks_like_utf16le(data):
        hints.append("utf16le-text")

    be_offsets = monotonic_be16_prefix(data)
    if len(be_offsets) >= 3:
        hints.append(f"nested-be16-offsets:{len(be_offsets)}")

    if len(data) % 2 == 0 and len(data) >= 16:
        zero_words = count_zero_words_be(data)
        words = len(data) // 2
        if zero_words * 4 >= words:
            hints.append("sparse-16bit-table")

    common_tile_sizes = (8 * 8, 16 * 16, 32 * 32, 8 * 8 * 2, 16 * 16 * 2, 32 * 32 * 2)
    if len(data) in common_tile_sizes:
        hints.append("tile-sized-payload")
    if len(data) % 32 == 0 and len(data) >= 64:
        hints.append("32-byte-aligned-records")
    if len(data) % 24 == 0 and len(data) >= 48:
        hints.append("24-byte-aligned-records")
    if len(data) % 4 == 0 and len(data) >= 32:
        hints.append("4-byte-aligned-records")
    if len(data) >= 64:
        low_nibbles = sum(1 for value in data[: min(len(data), 0x400)] if value <= 0x8F)
        sample = min(len(data), 0x400)
        if low_nibbles * 4 >= sample * 3:
            hints.append("nibble-heavy-table")

    signature = data[:4].hex() if data else ""
    return ChunkAnalysis(signature, shannon_entropy(data), hex_bytes(data), ascii_preview(data), hints)


def looks_like_utf16le(data: bytes) -> bool:
    if len(data) < 4 or len(data) & 1:
        return False
    pairs = len(data) // 2
    zero_hi = sum(1 for i in range(1, len(data), 2) if data[i] == 0)
    return zero_hi * 2 >= pairs


def clean_mime(raw: bytes) -> str:
    raw = raw.split(b"\0", 1)[0]
    try:
        return raw.decode("ascii", errors="ignore")
    except UnicodeDecodeError:
        return ""


def plausible_mime(mime: str) -> bool:
    if not mime:
        return False
    if len(mime) > 96 or "/" not in mime:
        return False
    return re.fullmatch(r"[A-Za-z0-9.+_-]+/[A-Za-z0-9.+_-]+", mime) is not None


def parse_mime_blob(data: bytes, start: int, end: int) -> tuple[str, bytes] | None:
    if start + 2 >= end:
        return None
    data_offset = data[start]
    if data[start + 1] != 0 or data_offset < 3 or start + data_offset > end:
        return None
    mime = clean_mime(data[start + 2 : start + data_offset])
    if not plausible_mime(mime):
        return None
    payload = data[start + data_offset : end]
    return mime, payload


def parse_range_bar(data: bytes) -> BarResource | None:
    range_count = u16(data, 0x06)
    range_table = u32(data, 0x08)
    offset_table = u32(data, 0x10)
    first_blob = u32(data, 0x18)
    if not (
        range_count > 0
        and range_table >= 0x20
        and range_table + range_count * 8 <= len(data)
        and offset_table > range_table
        and first_blob > offset_table
        and first_blob <= len(data)
    ):
        return None

    blob_offsets: list[int] = []
    for off in range(offset_table, first_blob + 1, 4):
        blob_off = u32(data, off)
        if first_blob <= blob_off <= len(data):
            blob_offsets.append(blob_off)
    if not blob_offsets:
        return None
    if blob_offsets[-1] != len(data):
        blob_offsets.append(len(data))

    out = BarResource()
    covered = [False] * len(blob_offsets)

    for rng in range(range_count):
        desc = range_table + rng * 8
        res_type = u16(data, desc)
        base_id = u16(data, desc + 2)
        count = u16(data, desc + 4)
        first_index = u16(data, desc + 6)
        if count == 0 or first_index >= len(blob_offsets):
            continue
        if not out.entries:
            out.base_id = base_id
            out.res_type = res_type
        # Some Zeebo BARs use count as an inclusive local end index for raw runs.
        for i in range(count + 1):
            index = first_index + i
            if index + 1 >= len(blob_offsets):
                break
            start = blob_offsets[index]
            end = blob_offsets[index + 1]
            parsed = parse_mime_blob(data, start, end)
            if not parsed:
                continue
            mime, payload = parsed
            out.entries.append(BarEntry(base_id + i, mime or bytes_mime(payload), payload, start, end - start))
            covered[index] = True

    if out.entries:
        existing_ids = {entry.res_id for entry in out.entries}
        for rng in range(range_count - 1, -1, -1):
            desc = range_table + rng * 8
            base_id = u16(data, desc + 2)
            count = u16(data, desc + 4)
            first_index = u16(data, desc + 6)
            if count != 0 or first_index >= len(blob_offsets):
                continue
            end_index = len(blob_offsets) - 1
            for next_rng in range(rng + 1, range_count):
                next_first = u16(data, range_table + next_rng * 8 + 6)
                if first_index < next_first < len(blob_offsets):
                    end_index = next_first
                    break
            for i in range(end_index - first_index):
                entry_id = base_id + i
                if entry_id in existing_ids:
                    continue
                collides = False
                for other_rng in range(range_count):
                    other_desc = range_table + other_rng * 8
                    other_base = u16(data, other_desc + 2)
                    other_count = u16(data, other_desc + 4)
                    if other_count and other_base <= entry_id <= other_base + other_count:
                        collides = True
                        break
                if collides:
                    continue
                index = first_index + i
                if index + 1 >= len(blob_offsets):
                    break
                start = blob_offsets[index]
                end = blob_offsets[index + 1]
                parsed = parse_mime_blob(data, start, end)
                if not parsed:
                    continue
                mime, payload = parsed
                out.entries.append(BarEntry(entry_id, mime or bytes_mime(payload), payload, start, end - start))
                existing_ids.add(entry_id)

        for index in range(len(blob_offsets) - 1):
            if covered[index]:
                continue
            start = blob_offsets[index]
            end = blob_offsets[index + 1]
            parsed = parse_mime_blob(data, start, end)
            if not parsed:
                continue
            mime, payload = parsed
            if mime.lower() not in ("image/png", "image/bmp", "image/jpeg", "image/svg+xml", "image/x-tga"):
                continue
            inferred_id = None
            next_base = 0xFFFF
            for rng in range(range_count):
                desc = range_table + rng * 8
                base_id = u16(data, desc + 2)
                count = u16(data, desc + 4)
                first_index = u16(data, desc + 6)
                if count == 0:
                    continue
                if first_index + count <= index:
                    inferred_id = base_id + (index - first_index)
                elif first_index > index:
                    next_base = base_id
                    break
            if inferred_id is None or inferred_id >= next_base:
                continue
            if inferred_id in existing_ids:
                continue
            out.entries.append(BarEntry(inferred_id, mime, payload, start, end - start))
            existing_ids.add(inferred_id)
        return out

    existing_ids: set[int] = set()
    for rng in range(range_count):
        desc = range_table + rng * 8
        res_type = u16(data, desc)
        base_id = u16(data, desc + 2)
        first_index = u16(data, desc + 6)
        table_count = u16(data, desc + 4)
        count = table_count
        if first_index >= len(blob_offsets):
            continue
        end_index = len(blob_offsets) - 1
        if rng + 1 < range_count:
            next_first = u16(data, range_table + (rng + 1) * 8 + 6)
            if first_index < next_first < len(blob_offsets):
                end_index = next_first
        inferred_count = end_index - first_index
        if count == 0 or count < inferred_count:
            count = inferred_count
        if table_count:
            count += 1
        if not out.entries:
            out.base_id = base_id
            out.res_type = res_type
        for i in range(count):
            entry_id = base_id + i
            if entry_id in existing_ids:
                continue
            if table_count == 0:
                collides = False
                for other_rng in range(range_count):
                    other_desc = range_table + other_rng * 8
                    other_base = u16(data, other_desc + 2)
                    other_count = u16(data, other_desc + 4)
                    if other_count and other_base <= entry_id <= other_base + other_count:
                        collides = True
                        break
                if collides:
                    continue
            index = first_index + i
            if index + 1 >= len(blob_offsets):
                break
            start = blob_offsets[index]
            end = blob_offsets[index + 1]
            payload = data[start:end]
            if res_type == 1 and payload.startswith((b"\xff\xfe", b"\xfe\xff")):
                out.entries.append(BarEntry(entry_id, "text/plain", payload, start, len(payload)))
                existing_ids.add(entry_id)
                continue
            parsed = parse_mime_blob(data, start, end)
            if parsed:
                mime, payload = parsed
                out.entries.append(BarEntry(entry_id, mime or bytes_mime(payload), payload, start, end - start))
                existing_ids.add(entry_id)
                continue
            payload = data[start:end]
            out.entries.append(BarEntry(entry_id, bytes_mime(payload), payload, start, end - start))
            existing_ids.add(entry_id)

    return out if out.entries else None


def parse_legacy_bar(data: bytes) -> BarResource | None:
    if len(data) < 0x3C:
        return None
    class_word = u32(data, 0x20)
    out = BarResource(base_id=(class_word >> 16) & 0xFFFF, res_type=class_word & 0xFFFF)
    offsets: list[int] = []
    prev = 0
    for off in range(0x28, len(data) + 1, 4):
        cur = u32(data, off)
        if cur == 0 or cur > len(data) or cur <= prev:
            break
        offsets.append(cur)
        prev = cur
        if cur == len(data):
            break
    if len(offsets) < 2:
        return None
    if offsets[-1] != len(data):
        offsets.append(len(data))
    for i in range(len(offsets) - 1):
        start = offsets[i]
        end = offsets[i + 1]
        if start + 2 >= end:
            continue
        data_offset = u16(data, start)
        if data_offset < 2 or start + data_offset > end:
            continue
        mime = clean_mime(data[start + 2 : start + data_offset])
        if not plausible_mime(mime):
            continue
        payload = data[start + data_offset : end]
        out.entries.append(BarEntry(out.base_id + i, mime or bytes_mime(payload), payload, start, end - start))
    return out if out.entries else None


def parse_bar(data: bytes) -> BarResource:
    parsed = parse_range_bar(data) or parse_legacy_bar(data) or parse_be_offset_table(data) or parse_raw_binary(data)
    if not parsed:
        raise ValueError("not a recognized BAR resource file")
    parsed.entries.sort(key=lambda entry: entry.res_id)
    return parsed


def parse_raw_binary(data: bytes) -> BarResource | None:
    if not data:
        return None
    out = BarResource(base_id=0, res_type=0xFFFF, format_name="raw-binary")
    out.entries.append(BarEntry(0, bytes_mime(data), data, 0, len(data)))
    return out


def parse_be_offset_table(data: bytes) -> BarResource | None:
    """Parse title-specific .bar files that start with big-endian u16 offsets.

    Seen in commercial game asset packs where the .bar extension is reused for
    compact binary tables rather than BREW BAR resources. The format has no
    MIME header, so entries are intentionally exported as opaque chunks.
    """
    if len(data) < 8:
        return None

    offsets: list[int] = []
    prev = -1
    for off in range(0, min(len(data), 0x200), 2):
        cur = be16(data, off)
        if cur == 0:
            break
        if cur <= prev or cur > len(data):
            break
        offsets.append(cur)
        prev = cur

    if len(offsets) < 3:
        return None

    table_end = len(offsets) * 2
    while table_end < len(data) and data[table_end] == 0:
        table_end += 1
    if table_end & 1:
        table_end += 1

    payload_offsets = [off for off in offsets if off >= table_end and off < len(data)]
    if not payload_offsets:
        return None
    if payload_offsets[0] != table_end and table_end < payload_offsets[0]:
        payload_offsets.insert(0, table_end)
    if payload_offsets[-1] != len(data):
        payload_offsets.append(len(data))

    out = BarResource(base_id=0, res_type=0x8000, format_name="be-offset-table")
    for i in range(len(payload_offsets) - 1):
        start = payload_offsets[i]
        end = payload_offsets[i + 1]
        if end <= start:
            continue
        payload = data[start:end]
        out.entries.append(BarEntry(i, bytes_mime(payload), payload, start, len(payload)))

    return out if out.entries else None


def ext_for_mime(mime: str, payload: bytes) -> str:
    mime = (mime or bytes_mime(payload)).lower()
    if mime in ("image/svg+xml", "image/svg", "image/svg-xml", "video/svg", "video/svgz"):
        return ".svgz" if payload.startswith(b"\x1f\x8b") else ".svg"
    if mime == "video/mp4":
        if len(payload) >= 12 and payload[4:8] == b"ftyp":
            brand_area = payload[8:64]
            if b"3g2" in brand_area:
                return ".3g2"
            if b"3gp" in brand_area:
                return ".3gp"
            if b"M4A " in brand_area:
                return ".m4a"
        return ".mp4"
    mapping = {
        "image/png": ".png",
        "image/bmp": ".bmp",
        "image/jpeg": ".jpg",
        "image/jpg": ".jpg",
        "image/gif": ".gif",
        "image/x-tga": ".tga",
        "image/tga": ".tga",
        "image/bci": ".bci",
        "snd/midi": ".mid",
        "snd/mp3": ".mp3",
        "snd/qcp": ".qcp",
        "snd/vnd.qcelp": ".qcp",
        "snd/qcf": ".qcf",
        "snd/mmf": ".mmf",
        "snd/spf": ".spf",
        "snd/imy": ".imy",
        "snd/wav": ".wav",
        "snd/aac": ".aac",
        "snd/amr": ".amr",
        "snd/wma": ".wma",
        "audio/mid": ".mid",
        "audio/midi": ".mid",
        "audio/mp3": ".mp3",
        "audio/mpeg": ".mp3",
        "audio/qcp": ".qcp",
        "audio/vnd.qcelp": ".qcp",
        "audio/qcf": ".qcf",
        "audio/mmf": ".mmf",
        "audio/spf": ".spf",
        "audio/imy": ".imy",
        "audio/wav": ".wav",
        "audio/wave": ".wav",
        "audio/x-wav": ".wav",
        "audio/aac": ".aac",
        "audio/amr": ".amr",
        "audio/wma": ".wma",
        "audio/hvs": ".hvs",
        "audio/saf": ".saf",
        "audio/xmf": ".xmf",
        "audio/mxmf": ".mxmf",
        "audio/xmf0": ".xmf",
        "audio/xmf1": ".xmf",
        "audio/dls": ".dls",
        "video/pmd": ".pmd",
        "video/wmv": ".wmv",
        "text/plain": ".txt",
    }
    return mapping.get(mime, ".bin")


def safe_name(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", name).strip("._") or "entry"


def list_entries(path: Path, bar: BarResource, as_json: bool, analyze: bool) -> None:
    rows = [
        {
            "id": entry.res_id,
            "id_hex": f"0x{entry.res_id:04x}",
            "mime": entry.mime,
            "size": len(entry.data),
            "offset": entry.offset,
            "raw_size": entry.raw_size,
            "analysis": (
                {
                    "signature": detect_chunk(entry.data).signature,
                    "entropy": round(detect_chunk(entry.data).entropy, 3),
                    "first16": detect_chunk(entry.data).first16,
                    "ascii": detect_chunk(entry.data).ascii_preview,
                    "hints": detect_chunk(entry.data).hints,
                }
                if analyze
                else None
            ),
        }
        for entry in bar.entries or []
    ]
    if not analyze:
        for row in rows:
            row.pop("analysis", None)
    if as_json:
        print(
            json.dumps(
                {
                    "path": str(path),
                    "format": bar.format_name,
                    "base_id": bar.base_id,
                    "type": bar.res_type,
                    "entries": rows,
                },
                indent=2,
            )
        )
        return
    print(f"{path}")
    print(f"format={bar.format_name} base_id=0x{bar.base_id:04x} type=0x{bar.res_type:04x} entries={len(rows)}")
    for row in rows:
        analysis = row.get("analysis")
        hint_text = ""
        if analysis and analysis["hints"]:
            hint_text = " hints=" + ",".join(analysis["hints"])
        print(
            f"  {row['id_hex']} size={row['size']:7d} raw={row['raw_size']:7d} "
            f"off=0x{row['offset']:08x} mime={row['mime']}{hint_text}"
        )
        if analysis:
            print(
                f"        sig={analysis['signature']} entropy={analysis['entropy']:.3f} "
                f"hex={analysis['first16']} ascii='{analysis['ascii']}'"
            )


def extract_entries(path: Path, bar: BarResource, out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    for entry in bar.entries or []:
        ext = ext_for_mime(entry.mime, entry.data)
        base = safe_name(f"{path.stem}_{entry.res_id:04x}_{entry.mime.replace('/', '_')}")
        out_path = out_dir / f"{base}{ext}"
        out_path.write_bytes(entry.data)
        print(f"wrote {out_path} ({len(entry.data)} bytes)")


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect or extract BREW BAR resource files.")
    parser.add_argument("bar", type=Path, help="BAR file to inspect")
    parser.add_argument("--json", action="store_true", help="print entry metadata as JSON")
    parser.add_argument("--analyze", action="store_true", help="print per-entry signatures, entropy, and format hints")
    parser.add_argument("--extract", type=Path, help="extract entries to this directory")
    args = parser.parse_args()

    data = args.bar.read_bytes()
    try:
        bar = parse_bar(data)
    except ValueError as exc:
        print(f"{args.bar}: {exc}")
        return 1
    list_entries(args.bar, bar, args.json, args.analyze)
    if args.extract:
        extract_entries(args.bar, bar, args.extract)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
