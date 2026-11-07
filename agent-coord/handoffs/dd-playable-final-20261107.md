# Double Dragon — Playable Final (2026-11-07)

**Branch:** `front/dd-playable`  
**Worktree:** `C:\Users\Lucas\AppData\Local\Temp\opencode\zeebo-worktrees\dd-playable`  
**HEAD:** `6ed6d92`  
**Base:** `5325349`

## Status

**Double Dragon JOGÁVEL COMPROVADO.**

Gate `-Playable` PASS completo:
- Exit 0
- varied=1, audio 684044 nz, peak 25626
- 1400 frames, 12.5s elapsed
- 47 dump PPM, 41 únicos
- DrawArrays=20, data.ggz aberto 34×

## Evidências movimento/soco

### LEFT hold (frames 560–649)
- `frame_0570`: `29506E2D6661A61A`
- `frame_0600`: `C98B2594742AEBC8`
- `frame_0630`: `051CF8D1240C94E1`
- `frame_0660`: `97108F5D940DD902`

### RIGHT hold (frames 700–789)
- `frame_0720`: `BF9DFC02F454138D`
- `frame_0750`: `D16865FF5756436A`
- `frame_0780`: `3956CC13B4AE87BF`

**Conjunto disjunto:** LEFT ∩ RIGHT = ∅ → movimento comprovado.

### A punch (frame ~820)
- `frame_0810` (pré-A): `F78CE10266F08B9F`
- `frame_0840` (A): `F69D1F684B7BC031`

**Hash diferente:** soco visual comprovado.

### PPM final
- `6589C89C345EB0B6AD594EA4079F881381C912F9ABC9BCDDBB3D64AA7878B8C5`
- **≠** title idle `2149E446...`
- START avançou além do title.

## Reprodução

```powershell
cd C:\Users\Lucas\AppData\Local\Temp\opencode\zeebo-worktrees\dd-playable
powershell -File tools\dd-gate\run.ps1 -Playable
```

Sequência automática:
```
START:340:8,START:420:8,LEFT:560:90,RIGHT:700:90,A:820:6,A:860:6,A:900:6,B:960:6,B:1000:6,LEFT:1060:60,A:1140:6,RIGHT:1180:60,A:1260:6
```

## Artefatos

- `artifacts/dd-gate/dd-playable.log`
- `artifacts/dd-gate/dd-playable.ppm`
- `artifacts/dd-gate/dump-playable/frame_*.ppm` (47 frames)
- `artifacts/dd-gate/dd-600.{log,ppm}` (baseline sem input)

## Commits

- `6ed6d92`: Playable proof com evidências
- `5325349`: Palette alpha fix + gate + dumps
- `3df7ff4`: START→HOME map, GRAPHICS stubs, media clear
- `ed609d0`: Ovos media_tick DONE + HID queue
- `4da83a4`: Ovos mixer pos_fp + heap auto-Play

## Integração

Pronto para merge em `wip/integration-20261106` após validação FP gate remoto.

Branch DD completo; sem trabalho pendente nesta frente.
