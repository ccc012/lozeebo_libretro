"""Exercise archive#member fallback without modifying RetroArch or smoke harness."""
import hashlib
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
build = Path(os.environ.get('ZEEBO_TEST_BUILD_DIR', str(root / 'build-content'))) / 'Release'
out = root / 'artifacts/content-packages'
out.mkdir(parents=True, exist_ok=True)
package = r'D:\Games\Zeebo - Zeebo\Zeebo Family Pack (Brazil) (Es,Pt).7z'
env = dict(os.environ, ZEEMU_SMOKE_CONTENT_PATH=package + '#mod/277229/game.mod', ZEEMU_SMOKE_SEQ='A:240:10,A:300:10,RIGHT:420:120,LEFT:570:120,A:720:60',
           ZEEMU_SMOKE_MAX_SECONDS='120', ZEEMU_SMOKE_HOLD_FRAMES='0')
with (out / 'archive-member.log').open('w') as log:
    p = subprocess.run([str(build / 'libretro_smoke.exe'), str(build / 'lozeebo_libretro.dll'),
                        package, str(out / 'archive-member.ppm'), '900'],
                       cwd=root, env=env, stdout=log, stderr=log)
assert p.returncode == 0
assert hashlib.sha256((out / 'archive-member.ppm').read_bytes()).hexdigest().upper() == 'DC43FEA183E9930E6C6C24F1012680CBCC9CBD754CEFDA39BE98A31F258947C0'
assert 'audio_nonzero=931855' in (out / 'archive-member.log').read_text(errors='replace')
print('PASS archive#mod member: exact FP video/audio reference')
env['ZEEMU_SMOKE_CONTENT_PATH'] = r'C:\frontend-temporary\missing-game.mod'
env['ZEEMU_SMOKE_ARCHIVE'] = package
env['ZEEMU_SMOKE_MEMBER'] = 'mod/277229/game.mod'
with (out / 'archive-ext.log').open('w') as log:
    p = subprocess.run([str(build / 'libretro_smoke.exe'), str(build / 'lozeebo_libretro.dll'),
                        package, str(out / 'archive-ext.ppm'), '900'],
                       cwd=root, env=env, stdout=log, stderr=log)
assert p.returncode == 0
assert hashlib.sha256((out / 'archive-ext.ppm').read_bytes()).hexdigest().upper() == 'DC43FEA183E9930E6C6C24F1012680CBCC9CBD754CEFDA39BE98A31F258947C0'
assert 'audio_nonzero=931855' in (out / 'archive-ext.log').read_text(errors='replace')
print('PASS GET_GAME_INFO_EXT: exact FP video/audio reference')
