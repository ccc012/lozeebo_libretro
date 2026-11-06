#!/usr/bin/env python3
from pathlib import Path
import sys
from classify_ppm import read_ppm, stats

for arg in sys.argv[1:]:
    p = Path(arg)
    w, h, rgb = read_ppm(p)
    st = stats(rgb, w, h, 1)
    print(
        f"{p.name} white={st['white_frac']:.4f} black={st['black_frac']:.4f} "
        f"buckets={st['color_buckets']} varied={st['varied']} first={st['first']} dig={st['digest']}"
    )
