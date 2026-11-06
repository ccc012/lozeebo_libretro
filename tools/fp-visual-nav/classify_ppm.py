#!/usr/bin/env python3
"""Classify smoke PPM dumps: hash, white/black ratio, diversity. Keep scene changes."""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import sys
from pathlib import Path


def read_ppm(path: Path):
    raw = path.read_bytes()
    if not raw.startswith(b"P6"):
        raise ValueError(f"not P6: {path}")
    i = 0
    nl = 0
    while nl < 3 and i < len(raw):
        if raw[i] == 10:
            nl += 1
        i += 1
    header = raw[:i].decode("ascii", "replace").split()
    w, h = int(header[1]), int(header[2])
    rgb = raw[i:]
    expect = w * h * 3
    if len(rgb) < expect:
        raise ValueError(f"short ppm {path}: {len(rgb)} < {expect}")
    return w, h, memoryview(rgb[:expect])


def stats(rgb: memoryview, w: int, h: int, sample_stride: int = 4):
    n = w * h
    white = black = 0
    xor_v = 0
    first = (rgb[0], rgb[1], rgb[2])
    varied = False
    # subsample for speed; full white/black count on stride-1 every Nth pixel row-major
    step = max(1, sample_stride)
    samples = 0
    # unique via 15-bit packed color buckets
    buckets = set()
    for pi in range(0, n, step):
        o = pi * 3
        r, g, b = rgb[o], rgb[o + 1], rgb[o + 2]
        samples += 1
        if r == 255 and g == 255 and b == 255:
            white += 1
        elif r == 0 and g == 0 and b == 0:
            black += 1
        else:
            buckets.add(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3))
        if (r, g, b) != first:
            varied = True
        xor_v ^= ((r << 16) | (g << 8) | b) + pi
    # digest of full buffer (cheap)
    digest = hashlib.blake2b(rgb, digest_size=8).hexdigest()
    return {
        "w": w,
        "h": h,
        "digest": digest,
        "xor": xor_v & 0xFFFFFFFF,
        "varied": varied,
        "white_frac": white / samples if samples else 0.0,
        "black_frac": black / samples if samples else 0.0,
        "color_buckets": len(buckets),
        "first": f"{first[0]:02X}{first[1]:02X}{first[2]:02X}",
        "samples": samples,
        "step": step,
    }


def classify_dir(dump_dir: Path, keep_dir: Path | None, min_bucket_delta: int = 2):
    frames = sorted(dump_dir.glob("frame_*.ppm"))
    rows = []
    prev = None
    changes = []
    for fp in frames:
        w, h, rgb = read_ppm(fp)
        st = stats(rgb, w, h)
        name = fp.name
        idx = int(name[6:10])
        st["frame"] = idx
        st["file"] = name
        change = False
        reason = []
        if prev is None:
            change = True
            reason.append("first")
        else:
            if st["digest"] != prev["digest"]:
                reason.append("digest")
                change = True
            if abs(st["white_frac"] - prev["white_frac"]) >= 0.05:
                reason.append("white")
                change = True
            if abs(st["color_buckets"] - prev["color_buckets"]) >= min_bucket_delta:
                reason.append("buckets")
                change = True
            if st["varied"] != prev["varied"]:
                reason.append("varied")
                change = True
        st["change"] = change
        st["reason"] = ",".join(reason)
        rows.append(st)
        if change:
            changes.append(st)
            if keep_dir is not None:
                keep_dir.mkdir(parents=True, exist_ok=True)
                shutil.copy2(fp, keep_dir / name)
        prev = st
    return rows, changes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump_dir")
    ap.add_argument("--keep", default="")
    ap.add_argument("--json", default="")
    ap.add_argument("--summary", default="")
    args = ap.parse_args()
    dump_dir = Path(args.dump_dir)
    keep = Path(args.keep) if args.keep else None
    rows, changes = classify_dir(dump_dir, keep)
    # FP menu is mostly white with sparse UI; treat varied+buckets as content
    non_wb = [
        r
        for r in rows
        if r["color_buckets"] >= 2
        and r["varied"]
        and r["black_frac"] < 0.995
    ]
    summary = {
        "total_frames": len(rows),
        "scene_changes": len(changes),
        "non_solid_frames": len(non_wb),
        "first_non_solid": non_wb[0]["frame"] if non_wb else None,
        "changes": [
            {
                "frame": c["frame"],
                "digest": c["digest"],
                "white": round(c["white_frac"], 4),
                "black": round(c["black_frac"], 4),
                "buckets": c["color_buckets"],
                "varied": c["varied"],
                "first": c["first"],
                "reason": c["reason"],
            }
            for c in changes
        ],
        "non_solid_sample": [
            {
                "frame": r["frame"],
                "white": round(r["white_frac"], 4),
                "buckets": r["color_buckets"],
                "first": r["first"],
                "digest": r["digest"],
            }
            for r in non_wb[:20]
        ],
    }
    text = json.dumps(summary, indent=2)
    if args.json:
        Path(args.json).write_text(text, encoding="utf-8")
    if args.summary:
        Path(args.summary).write_text(text, encoding="utf-8")
    print(text)
    # exit 0 if we found real content beyond solid white/black
    return 0 if non_wb else 1


if __name__ == "__main__":
    sys.exit(main())
