#!/usr/bin/env python3
"""Export final sequence of key PPMs to PNG for visual inspection."""
from pathlib import Path
from classify_ppm import read_ppm, stats
from ppm_to_png import write_png

def export_keys(dump_dir: Path, out_dir: Path):
    out_dir.mkdir(parents=True, exist_ok=True)
    frames = sorted(dump_dir.glob("frame_*.ppm"))
    prev = None
    saved = []
    for fp in frames:
        w, h, rgb = read_ppm(fp)
        st = stats(rgb, w, h, 1)
        idx = int(fp.name[6:10])
        # save key milestones: frame 0, transition to loading, progress steps, solid shift
        is_key = False
        if prev is None:
            is_key = True
        elif idx in (0, 60, 75, 90, 105, 120, 210, 600, 1800):
            is_key = True
        elif st['color_buckets'] != prev['color_buckets'] and abs(st['color_buckets'] - prev['color_buckets']) >= 10:
            is_key = True

        if is_key:
            png_out = out_dir / f"frame_{idx:04d}.png"
            write_png(png_out, w, h, rgb)
            ppm_out = out_dir / f"frame_{idx:04d}.ppm"
            ppm_out.write_bytes(fp.read_bytes())
            saved.append((idx, st['color_buckets'], st['white_frac'], st['black_frac'], png_out.name))
            prev = st
    return saved

if __name__ == "__main__":
    import sys
    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])
    res = export_keys(src, dst)
    for r in res:
        print(f"frame {r[0]:04d}: buckets={r[1]} white={r[2]:.3f} black={r[3]:.3f} -> {r[4]}")
