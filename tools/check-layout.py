"""Check repository layout without ROMs: python tools/check-layout.py"""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
allowed = {'.gitattributes', '.gitignore', 'CMakeLists.txt', 'CMakePresets.json', 'LICENSE', 'README.md'}
assert {p.name for p in ROOT.iterdir() if p.is_file()} == allowed
cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
for source in set(re.findall(r'\bsrc/[\w/]+\.c\b', cmake)):
    assert (ROOT / source).is_file(), source
for path in ('src', 'tests', 'tools', 'docs', 'dist/info', 'tools/legado', 'docs/historico'):
    assert (ROOT / path).is_dir(), path
preserved = ROOT / 'preservacao'
if preserved.exists():
    log = preserved / 'registros/organizacao/movimentacoes.json'
    if log.exists():
        for old, new in json.loads(log.read_text(encoding='utf-8')).items():
            assert not (ROOT / old).exists(), old
            assert (ROOT / new).is_file(), new
for folder in ('src', 'tests', 'tools'):
    for path in (ROOT / folder).rglob('*'):
        assert path.suffix.lower() not in {'.o', '.obj', '.7z', '.zip', '.rar', '.pakz'}, path
print('PASS layout, canonical sources and archived paths')
