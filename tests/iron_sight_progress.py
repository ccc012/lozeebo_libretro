"""Iron Sight real timer/HID lifetime regression; screenshots require human review."""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir', type=Path, default=Path('build-progress/Release'))
p.add_argument('--frames', type=int, default=360)
p.add_argument('--output', type=Path, default=Path('artifacts/iron/red'))
p.add_argument('--seq', default='DOWN:190:10,UP:220:10,A:250:10,A:300:10')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[1]
env = {k: v for k, v in os.environ.items() if not k.startswith('ZEEMU_SMOKE_')}
env.update(ZEEMU_SMOKE_PRESS_FRAME='999999', ZEEMU_SMOKE_SEQ=a.seq,
           ZEEMU_SMOKE_DUMP_DIR=str(a.output.resolve()), ZEEMU_SMOKE_DUMP_STRIDE='30',
           ZEEMU_SMOKE_MAX_SECONDS='120')
command = [str((a.build_dir / 'libretro_smoke.exe').resolve()),
           str((a.build_dir / 'lozeebo_libretro.dll').resolve()),
           str(root / 'artifacts/iron/content/mod/280221/ironsight.mod'),
           str((a.output / 'frame.ppm').resolve()), str(a.frames)]
with (a.output / 'smoke.log').open('wb') as log:
    child = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
    try:
        child.wait(timeout=150)
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'], check=False)
        child.wait()
        raise
text = (a.output / 'smoke.log').read_text(errors='replace')
assert child.returncode == 0, child.returncode
assert '[SMOKE-TIMING] frames=' + str(a.frames) in text, 'incomplete run'
assert 'IHIDDevice_RegisterForButtonEvent' in text, 'HID registration missing'
assert 'IHIDDevice Release: button/position signals liberados' not in text, 'unsolicited suspend released live HID'
assert 'timer de game loop (16ms) registrado' not in text, 'synthetic EVT_APP_SUSPEND timer'
print('PASS real timer/HID lifetime; gameplay NOT asserted:', a.output)
