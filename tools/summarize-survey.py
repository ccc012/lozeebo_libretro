"""Reclassify existing smoke logs; no inference of playability from pixels."""
import json
import re
from pathlib import Path
from collections import Counter
from PIL import Image

root = Path(__file__).resolve().parents[1]
art = root / 'artifacts/smoke-all'
rows = []
for log in sorted(art.glob('*.log')):
    if log.stem == 'fp-long':
        continue
    text = log.read_text(encoding='utf-8', errors='replace')
    ppm = log.with_suffix('.ppm')
    screen = 'absent'
    if ppm.exists():
        with Image.open(ppm) as image:
            extrema = image.convert('RGB').getextrema()
            screen = ('black' if all(hi == 0 for lo, hi in extrema) else
                      'uniform' if all(lo == hi for lo, hi in extrema) else 'nonuniform')
    def metric(name):
        found = re.search(r'\b' + name + r'=(\d+)', text)
        return int(found[1]) if found else None
    faults = list(dict.fromkeys(re.findall(r'\[ERROR\].*', text)))
    rows.append(dict(game=log.stem, screen=screen,
                     frames=metric('video_frames'), audio_nonzero=metric('audio_nonzero'),
                     guest_fault=bool(re.search(r'CPU descarrilou|STACK OVERFLOW', text)),
                     load_failed='retro_load_game failed' in text,
                     errors=faults[:5]))
(art / 'inventory.json').write_text(json.dumps(rows, indent=2, ensure_ascii=False), encoding='utf-8')
counts = Counter(row['screen'] for row in rows)
lines = ['# Triagem automatizada do catálogo', '',
         f'Total: {len(rows)} pacotes; 180 frames solicitados, sem comandos de jogo.',
         'Build: build-fp-gate; testes em paralelo. Tempos não medem desempenho isolado.',
         'Tela final não demonstra gameplay. Falha guest pode ocorrer mesmo com exit 0.', '',
         '## Telas finais', '', *[f'- {key}: {value}' for key, value in sorted(counts.items())], '',
         '## Inventário', '', '| Jogo | Tela final | Frames | Áudio não zero | Falha guest |',
         '|---|---|---:|---:|---|']
for row in rows:
    lines.append(f"| {row['game']} | {row['screen']} | {row['frames']} | {row['audio_nonzero']} | {'sim' if row['guest_fault'] else 'não detectada'} |")
lines += ['', '## Limites', '',
          '- 180 frames são triagem de inicialização; carregamentos longos permanecem inconclusivos.',
          '- Sem áudio não comprova falha: muitas telas iniciais são silenciosas.',
          '- CLSID desconhecido é aviso, não prova de falha de carregamento.',
          '- Testes estendidos e revisão visual separados antes de classificar compatibilidade.',
          '- RetroArch real não foi exercitado nesta rodada.']
(art / 'REPORT.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
print(f'{len(rows)} packages: {dict(counts)}; guest faults: {sum(r["guest_fault"] for r in rows)}')
