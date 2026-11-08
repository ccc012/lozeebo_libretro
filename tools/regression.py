"""One entry point for working-capability regression contracts."""
import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def run(command, log, timeout, build=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith('ZEEMU_')}
    if build is not None:
        env['ZEEBO_TEST_BUILD_DIR'] = str(build)
    start = time.monotonic()
    with log.open('wb') as output:
        process = subprocess.Popen(command, cwd=ROOT, env=env,
                                   stdout=output, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=timeout)
            status = 'PASS' if code == 0 else 'FAIL'
        except subprocess.TimeoutExpired:
            if os.name == 'nt':
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            process.kill()
            process.wait()
            code, status = None, 'TIMEOUT'
    return dict(status=status, exit=code, seconds=round(time.monotonic()-start, 2), log=str(log))


def self_test():
    with tempfile.TemporaryDirectory() as tmp:
        log = Path(tmp) / 'check.log'
        assert run([sys.executable, '-c', 'print("ok")'], log, 5)['status'] == 'PASS'
        assert run([sys.executable, '-c', 'raise SystemExit(7)'], log, 5)['status'] == 'FAIL'
        assert run([sys.executable, '-c', 'import time; time.sleep(10)'], log, .1)['status'] == 'TIMEOUT'
    print('PASS runner success, failure, timeout')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', default='build-fp-gate')
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--contracts-only', action='store_true', help='Explicit partial run; excludes render/audio integration witnesses')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    build = ROOT / args.build_dir
    release = build / 'Release'
    out = ROOT / 'artifacts/regression' / datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    out.mkdir(parents=True)
    checks = []
    def check(name, command, timeout=240):
        try:
            result = run([str(x) for x in command], out / (name + '.log'), timeout, build)
        except OSError as error:
            result = dict(status='FAIL', error=str(error))
        result['name'] = name
        checks.append(result)
        (out / 'results.json').write_text(json.dumps(checks, indent=2), encoding='utf-8')
        print(f"{result['status']} {name}", flush=True)
        return result['status'] == 'PASS'
    if not args.skip_build:
        if not check('configure', ['cmake', '-S', ROOT, '-B', build, '-DZEEBO_BUILD_SMOKE_TEST=ON']):
            return 1
        if not check('build', ['cmake', '--build', build, '--config', 'Release'], 600):
            return 1
    check('cpu-hle-probe', [sys.executable, ROOT / 'tests/hle_probe_cost.py', build])
    for name in ('check-content-packages', 'check-content-frontend'):
        check(name, [sys.executable, ROOT / 'tests' / (name + '.py')], 480)
    check('capability-comparator', [sys.executable, ROOT / 'tools/capability-gate/test_comparator.py'])
    check('gles-composition', [release / 'gles_composition.exe'])
    for name in ('audio_adpcm_rate', 'content_run7_check'):
        check(name, [release / (name + '.exe')])
    for case in ('buttons', 'position', 'busy', 'abi', 'busy-position'):
        check('input-' + case, [release / 'input_transitions.exe', case])
    for name, script in [('fp-reference', 'fp-gate/run.ps1'),
                         ('dd-boot', 'dd-gate/run.ps1'),
                         ('ovos-boot', 'ovos-gate/run.ps1')]:
        check(name, ['powershell', '-NoProfile', '-File', ROOT / 'tools' / script,
                     '-BuildDir', args.build_dir, '-SkipBuild'], 480)
    check('dd-directions', ['powershell', '-NoProfile', '-File', ROOT / 'tools/dd-gate/check-directions.ps1',
                            '-BuildDir', args.build_dir])
    check('dd-stage-gameplay', [sys.executable, ROOT / 'tools/dd-gate/gameplay.py',
                                '--build-dir', build, '--output', out / 'dd-gameplay'])
    fp = r'D:\Temp\zeebo_all\Zeebo_Family_Pack__Brazil___Es_Pt_\mod\277229\game.mod'
    check('fp-neutral-rearm', [sys.executable, '-u', ROOT / 'tests/fp_position_transitions.py',
                               release / 'lozeebo_libretro.dll', fp])
    if not args.contracts_only:
        check('render-audio-capabilities', [sys.executable, ROOT / 'tools/capability-gate/capability_gate.py', '--build-dir', build], 2400)
    print(f'Report: {out}')
    print('Scope: ' + ('PARTIAL (contracts only)' if args.contracts_only else 'contracts + render/audio integration witnesses'))
    return int(any(result['status'] != 'PASS' for result in checks))


if __name__ == '__main__':
    sys.exit(main())
