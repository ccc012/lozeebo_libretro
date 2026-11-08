"""Functional boot render/audio witnesses; games are fixtures, not compatibility verdicts."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import uuid

HERE = Path(__file__).resolve().parent


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def read_log(path):
    data = Path(path).read_bytes()
    return data.decode('utf-16' if data.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8', errors='replace')


def screen(path):
    if not Path(path).exists():
        return 'absent'
    data = Path(path).read_bytes()
    head = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', data)
    if not head:
        raise ValueError('Invalid smoke PPM')
    pixels = data[head.end():]
    if len(pixels) != int(head[1]) * int(head[2]) * 3:
        raise ValueError('Truncated smoke PPM')
    if pixels == bytes(len(pixels)):
        return 'black'
    return 'uniform' if pixels == pixels[:3] * (len(pixels) // 3) else 'nonuniform'


def observe(log, ppm, exit_code, timed_out=False):
    text = read_log(log)
    frames = re.findall(r'\[SMOKE-TIMING\] frames=(\d+)', text)
    audio = re.findall(r'audio_nonzero=(\d+)', text)
    errors = sorted(set(line.strip() for line in text.splitlines() if '[ERROR]' in line))
    return dict(frames=int(frames[-1]) if frames else None,
                audio_nonzero=int(audio[-1]) if audio else None,
                screen=screen(ppm), ppm_sha256=sha(ppm) if Path(ppm).exists() else None,
                errors=errors, exit_code=exit_code, timed_out=timed_out)


def compare(base, actual):
    if actual.get('missing'):
        return 'INCONCLUSIVE', ['missing ROM']
    reasons = []
    if actual['timed_out']:
        reasons.append('functional witness timed out')
    if actual['exit_code'] != 0:
        reasons.append('load/run did not exit zero')
    if actual['frames'] != 180:
        reasons.append('180-frame render not completed')
    if set(actual['errors']) - set(base['errors']):
        reasons.append('new guest/load errors')
    if actual['screen'] != 'nonuniform':
        reasons.append('nonuniform render lost')
    # ponytail: unverified historical snapshots assert nonuniform output only; upgrade after controlled recapture/review.
    if base.get('snapshot_deterministic', True) and actual['ppm_sha256'] != base['ppm_sha256']:
        reasons.append('render/palette snapshot changed; review required')
    if actual['audio_nonzero'] is None or actual['audio_nonzero'] < base['audio_nonzero']:
        reasons.append('nonzero audio floor lost')
    return ('REGRESSION', reasons) if reasons else ('PASS', ['boot render/audio capabilities preserved; gameplay not asserted'])


def run_witness(base, args, run_dir, dll, smoke):
    out = run_dir / base['game']
    out.mkdir()
    rom = args.rom_dir / base['rom_filename']
    result = dict(fixture=base['game'], capabilities=['render.nonuniform'] + (['render.palette.snapshot'] if base.get('snapshot_deterministic', True) else []) + (['audio.nonzero'] if base['audio_nonzero'] else []))
    if not base.get('snapshot_deterministic', True):
        result['limits'] = ['historical snapshot reproducibility unverified; palette equality not asserted']
    if not rom.is_file():
        actual = {'missing': True}
    elif sha(rom) != base['rom_sha256']:
        result.update(status='INCONCLUSIVE', reasons=['ROM SHA256 differs from evidence'])
        return result
    else:
        env = {k: v for k, v in os.environ.items() if not k.upper().startswith('ZEEMU_SMOKE_')}
        command = [str(smoke), str(dll), str(rom), str(out / 'frame.ppm'), '180']
        started = time.monotonic()
        with open(out / 'smoke.log', 'wb') as log:
            proc = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=env, cwd=out)
            timed_out = False
            try:
                proc.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                if os.name == 'nt':
                    subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
                else:
                    proc.kill()
                proc.wait()
        try:
            actual = observe(out / 'smoke.log', out / 'frame.ppm', proc.returncode, timed_out)
        except ValueError as exc:
            result.update(status='REGRESSION', reasons=[str(exc)])
            return result
        actual.update(elapsed_seconds=round(time.monotonic() - started, 3), command=command, log=str(out / 'smoke.log'))
    status, reasons = compare(base, actual)
    result.update(status=status, reasons=reasons, observed=actual)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir', type=Path, required=True)
    p.add_argument('--rom-dir', type=Path, default=Path(r'D:\Games\Zeebo - Zeebo'))
    p.add_argument('--baseline', type=Path, default=HERE / 'evidence180.json')
    p.add_argument('--output', type=Path, default=HERE.parent.parent / 'artifacts' / 'capability-gate')
    p.add_argument('--filter', default='', help='Case-insensitive fixture substring')
    p.add_argument('--workers', type=int, choices=(1, 2), default=2)
    p.add_argument('--timeout', type=float, default=180)
    args = p.parse_args()
    if args.timeout <= 0:
        p.error('--timeout must be positive')
    evidence = json.loads(args.baseline.read_text(encoding='utf-8'))
    build = args.build_dir.resolve()
    if (build / 'Release').is_dir():
        build = build / 'Release'
    dll, smoke = build / 'lozeebo_libretro.dll', build / 'libretro_smoke.exe'
    if not dll.is_file() or not smoke.is_file():
        p.error('build-dir must contain existing lozeebo_libretro.dll and libretro_smoke.exe')
    fixtures = [b for b in evidence['witnesses'] if args.filter.casefold() in b['game'].casefold()]
    if not fixtures:
        p.error('filter selected no witnesses')
    run_dir = args.output.resolve() / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8])
    run_dir.mkdir(parents=True)
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        results = list(pool.map(lambda b: run_witness(b, args, run_dir, dll, smoke), fixtures))
    counts = dict(Counter(r['status'] for r in results))
    report = dict(schema=1, evidence_sha256=sha(args.baseline), dll_sha256=sha(dll), smoke_sha256=sha(smoke),
                  source_dll_sha256=evidence['provenance']['dll_sha256'], frames=180, input='none', counts=counts, results=results)
    (run_dir / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    lines = ['# Functional boot capability witnesses', '', str(counts), '', 'PASS applies to observed capabilities only, never whole-game compatibility.', '', '| Fixture | Status | Reason |', '|---|---|---|']
    lines += [f"| {r['fixture']} | {r['status']} | {'; '.join(r['reasons']).replace('|', '/')} |" for r in results]
    (run_dir / 'REPORT.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(json.dumps(dict(counts=counts, report=str(run_dir / 'report.json'))), flush=True)
    return int(any(r['status'] != 'PASS' for r in results))


if __name__ == '__main__':
    raise SystemExit(main())
