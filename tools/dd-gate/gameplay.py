"""DD stage witness: player-only displacement/stop/opposite/action, not frame hashes.
Stdlib only. ROM/cache remain read-only; smoke runs inside its output sandbox.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SEQ = 'START:340:8,START:420:8,RIGHT:1250:40,LEFT:1380:40,A:1500:6,A:1540:6,B:1580:6,RIGHT:1650:40'
BLUE = bytes((0, 49, 156))  # Billy's trousers; excludes enemy jeans and animated scenery.


def player(path):
    data = path.read_bytes()
    match = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', data)
    assert match, f'invalid PPM: {path}'
    width, height = map(int, match.groups())
    pixels = data[match.end():]
    assert (width, height) == (640, 480) and len(pixels) == width * height * 3
    points = [(x, y) for y in range(220, 420) for x in range(width)
              if pixels[(y * width + x) * 3:(y * width + x) * 3 + 3] == BLUE]
    assert len(points) >= 500, f'player missing/occluded: {path}'
    xs, ys = zip(*points)
    assert max(xs) - min(xs) < 100, f'ambiguous player palette: {path}'
    return dict(x=sum(xs) / len(xs), top=min(ys), bottom=max(ys), pixels=len(points))


def check(samples):
    x = lambda n: samples[n]['x']
    assert x(1280) - x(1240) > 30, 'RIGHT did not move Billy right'
    assert max(x(n) for n in (1320, 1340, 1360, 1380)) - min(x(n) for n in (1320, 1340, 1360, 1380)) < 3, 'Billy keeps moving after RIGHT release'
    assert x(1380) - x(1420) > 40, 'opposite LEFT did not move Billy left'
    assert max(x(n) for n in (1440, 1460, 1480)) - min(x(n) for n in (1440, 1460, 1480)) < 3, 'Billy keeps moving after LEFT release'
    # ponytail: isolated first action pose, not enemy damage/kill; add guest HP evidence before claiming combat completion.
    assert samples[1520]['top'] - samples[1480]['top'] > 20, 'A did not change Billy action pose'
    assert x(1680) - x(1640) > 30, 'movement lost after actions'


def self_test():
    samples = {n: dict(x=230, top=264) for n in FRAMES}
    for n in (1280,): samples[n]['x'] = 280
    for n in (1320, 1340, 1360, 1380): samples[n]['x'] = 301
    samples[1420]['x'] = 232
    samples[1520]['top'] = 298
    samples[1640]['x'], samples[1680]['x'] = 148, 200
    check(samples)
    for symptom in ('no-motion', 'stuck-release', 'wrong-opposite', 'no-action'):
        bad = {n: dict(v) for n, v in samples.items()}
        if symptom == 'no-motion': bad[1280]['x'] = 230
        if symptom == 'stuck-release': bad[1360]['x'] = 340
        if symptom == 'wrong-opposite': bad[1420]['x'] = 350
        if symptom == 'no-action': bad[1520]['top'] = 264
        try: check(bad)
        except AssertionError: continue
        raise AssertionError(f'false positive: {symptom}')
    print('PASS checker rejects no-motion, stuck release, wrong opposite, no action')


FRAMES = (1240, 1280, 1320, 1340, 1360, 1380, 1420, 1440, 1460, 1480, 1520, 1640, 1680)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-progress')
    parser.add_argument('--rom', type=Path, default=Path(r'D:\Games\Zeebo - Zeebo\Double Dragon (Brazil) (Es,Pt).7z'))
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/dd-gameplay')
    parser.add_argument('--check-dir', type=Path, help='check an existing smoke dump; no execution')
    parser.add_argument('--sequence', default=SEQ, help='override for negative-control reproduction')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    out = (args.check_dir or args.output).resolve()
    if not args.check_dir:
        out.mkdir(parents=True, exist_ok=True)
        assert not (out / 'smoke.log').exists(), 'use a fresh --output directory; preserve evidence'
        assert args.rom.is_file(), f'ROM missing: {args.rom}'
        build = args.build_dir.resolve() / 'Release'
        env = {k: v for k, v in os.environ.items() if not k.upper().startswith('ZEEMU_SMOKE_')}
        env.update(ZEEMU_SMOKE_SEQ=args.sequence, ZEEMU_SMOKE_HOLD_FRAMES='0',
                   ZEEMU_SMOKE_MAX_SECONDS='200', ZEEMU_SMOKE_DUMP_DIR=str(out),
                   ZEEMU_SMOKE_DUMP_STRIDE='20', ZEEMU_TRACE_HID='1')
        with (out / 'smoke.log').open('wb') as log:
            proc = subprocess.Popen([str(build / 'libretro_smoke.exe'), str(build / 'lozeebo_libretro.dll'),
                                     str(args.rom.resolve()), str(out / 'final.ppm'), '1900'],
                                    cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                assert proc.wait(timeout=230) == 0, 'smoke failed; inspect smoke.log'
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'], check=False)
                raise
    text = (out / 'smoke.log').read_text(errors='replace')
    assert '[SMOKE-TIMING] frames=1900' in text, 'sustained stage run incomplete'
    assert not re.search(r'\[ERROR\]|FATAL|STACK overflow|INVALID opcode', text), 'guest error; inspect smoke.log'
    audio = re.search(r'audio_nonzero=(\d+)', text)
    assert audio and int(audio[1]) > 0, 'audio absent'
    for uid in ('0106C403', '0106C401', '0106C3FF', '0106C40A', '0106C40B'):
        for down in (0, 1):
            assert f'HID: uid=0x{uid} down={down}' in text, f'missing UID transition {uid}/{down}'
    samples = {n: player(out / f'frame_{n:04}.ppm') for n in FRAMES}
    (out / 'player.json').write_text(json.dumps(samples, indent=2) + '\n')
    check(samples)
    # Sustained recognizable player/stage through frame 1900; enemies may occlude trousers late.
    player(out / 'frame_1800.ppm')
    print('PASS Billy moves right, stops, moves left, stops, action pose, resumes movement; 1900 frames/audio')
    print('LIMIT: action animation only; enemy damage, full stage completion, long-duration stability unproven')
    print(out)


if __name__ == '__main__':
    try:
        main()
    except (AssertionError, OSError, subprocess.TimeoutExpired) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        sys.exit(1)
