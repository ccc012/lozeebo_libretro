"""Run after cmake --build build-content --config Release.
Windows + installed 7-Zip. Fixtures stay inside this worktree's artifacts.
"""
import hashlib
import os
from pathlib import Path
import shutil
import stat
import subprocess
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PROBE = Path(os.environ.get('ZEEBO_TEST_BUILD_DIR', str(ROOT / 'build-content'))) / 'Release/content_package_check.exe'
FIXTURES = ROOT / 'artifacts/content-packages'
FIXTURES.mkdir(parents=True, exist_ok=True)


def probe(source, member=None, reject=False):
    result = subprocess.run([str(PROBE), str(source)] + ([member] if member else []),
                            capture_output=True, text=True, encoding='utf-8')
    if reject:
        assert result.returncode == 2, (source, result.stdout, result.stderr)
        assert result.stderr.strip()
        return
    assert result.returncode == 0, (source, result.stdout, result.stderr)
    return Path(result.stdout.strip())


def package(name, files):
    source = FIXTURES / name
    with zipfile.ZipFile(source, 'w', zipfile.ZIP_DEFLATED) as z:
        for path, data in files:
            z.writestr(path, data)
    return source


# Red against original core: raw 7z cannot load; old ZIP extracts only one member.
source = package('complete.zip', [('mif/123.mif', b'metadata'),
                 ('mod/123/game.mod', b'MOD fixture'), ('mod/123/data.vfs', b'assets')])
mod = probe(source)
assert mod.parts[-3:] == ('mod', '123', 'game.mod')
assert mod.with_name('data.vfs').read_bytes() == b'assets'
assert (mod.parents[2] / 'mif/123.mif').read_bytes() == b'metadata'
before = {p: (p.stat().st_mtime_ns, hashlib.sha256(p.read_bytes()).digest())
          for p in mod.parents[2].rglob('*') if p.is_file()}
t0 = time.perf_counter()
assert probe(source) == mod
assert before == {p: (p.stat().st_mtime_ns, hashlib.sha256(p.read_bytes()).digest()) for p in before}
print(f'PASS complete ZIP, unchanged cache hit {time.perf_counter()-t0:.3f}s')
assert probe(source, 'mod/123/game.mod') == mod
probe(source, 'mif/123.mif', reject=True)
for i, name in enumerate(['../escape.mod', '/absolute.mod', 'C:/escape.mod',
                          'mod/../escape.mod', 'mod/NUL.mod', 'mod/game.mod:stream',
                          'mod/trailing./game.mod', 'mod//game.mod']):
    probe(package(f'unsafe-{i}.zip', [(name, b'bad')]), reject=True)
symlink = zipfile.ZipInfo('mod/link.mod')
symlink.create_system = 3
symlink.external_attr = (stat.S_IFLNK | 0o777) << 16
probe(package('symlink.zip', [(symlink, b'../../escape')]), reject=True)
probe(package('duplicate.zip', [('game.mod', b'one'), ('GAME.MOD', b'two')]), reject=True)
probe(package('ambiguous.zip', [('one.mod', b'one'), ('two.mod', b'two')]), reject=True)
assert probe(FIXTURES / 'ambiguous.zip', 'two.mod').read_bytes() == b'two'
probe(package('too-many.zip', [(f'file-{i}', b'') for i in range(8193)]), reject=True)
# Declared oversized output must be rejected before the decoder runs.
import struct
oversized = package('oversized.zip', [('game.mod', b'x')])
raw = bytearray(oversized.read_bytes())
central = raw.index(b'PK\x01\x02')
struct.pack_into('<I', raw, central + 24, 513 * 1024 * 1024)
struct.pack_into('<I', raw, 22, 513 * 1024 * 1024)
oversized.write_bytes(raw)
probe(oversized, reject=True)
print('PASS traversal, absolute, ADS, reserved names, links, duplicates, count/size limits, ambiguous MOD rejection')
# Source-content identity changes cache even if size and mtime are restored.
stamp = source.stat()
source = package('complete.zip', [('mif/123.mif', b'metadata'),
                 ('mod/123/game.mod', b'NEW fixture'), ('mod/123/data.vfs', b'assets')])
import os
os.utime(source, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
assert probe(source) != mod
# Copying identical bytes to another origin also isolates cache.
copy = FIXTURES / 'copy.zip'
shutil.copyfile(source, copy)
assert probe(copy) != probe(source)
# Tampering is rejected, not silently used. Restore generated fixture after test.
current = probe(source)
asset = current.with_name('data.vfs')
original = asset.read_bytes()
try:
    asset.write_bytes(b'BROKEN')
    probe(source, reject=True)
finally:
    asset.write_bytes(original)
assert probe(source) == current
print('PASS origin/content cache identity')
real = Path(r'D:\Games\Zeebo - Zeebo\Zeebo Family Pack (Brazil) (Es,Pt).7z')
if real.exists():
    t0 = time.perf_counter()
    fp = probe(real)
    assert fp.parts[-3:] == ('mod', '277229', 'game.mod')
    assert fp.with_name('data.vfs').stat().st_size == 21730345
    assert (fp.parents[2] / 'mif/277229.mif').stat().st_size == 6608
    reference = Path(r'C:\Users\Lucas\AppData\Local\Temp\opencode\content-repro\fp\mod\277229\game.mod')
    if reference.exists():
        for name in ('game.mod', 'data.vfs', 'game.sig'):
            assert hashlib.sha256(fp.with_name(name).read_bytes()).digest() == hashlib.sha256(reference.with_name(name).read_bytes()).digest()
    print(f'PASS real FP complete 7z {time.perf_counter()-t0:.3f}s: {fp}')
print('PASS all content-package checks')
