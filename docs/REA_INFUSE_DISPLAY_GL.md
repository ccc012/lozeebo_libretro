# REA Infuse — IDisplay / IGraphics / QEGL / soft-GL

> Clean-room. Contratos de slots + correções mínimas para RE4 e Zuma.
> Fontes: Ghidra headless Infuse.exe (projeto re_infuse/ghidra_project/InfuseRE,
> Ghidra 12.1.4), cruzado com zeemu BrewDisplay/BrewEGL/BrewGraphics e
> idisplay_real.c / egl_gl.c / boot.c do zeebo_libretro.
> **Sem cópia de código Infuse.**

Data: 2026-11-06. Binário: re_infuse/bin/Infuse.exe.

---

## 0. Achado Infuse (Ghidra)

| Achado | Evidência |
|---|---|
| Sem RTTI/string IDisplay/IGraphics/IQEGL/BrewDisplay/SoftGL | ScanRttiDisplay — zero hits |
| Sem immediates 0x0103D8EC / 0x010127D4 / 0x01014BC3 / DIBM no host | FindImmClsid — 0 hits |
| 0x01001001 (DISPLAY) só em .pdata/dados | 3 hits data — falso positivo |
| HLE BREW = hooks guest via Dynarmic UserCallbacks | RTTI N8Dynarmic3A3213UserCallbacksE |
| Host render = OpenGL::Renderer @ 0x1405b85c0 (24 slots) + RendererContextGLFW @ 0x1405b8060 (15) | DumpHalVtable |
| HAL BREW surface = NativeInfuseHAL @ 0x1405b7560 (78 slots) | IShell-like; não é IDisplay |

**Consequência:** layout de slots BREW Display/QEGL **não** está em vtables C++ do Infuse.
Contrato operacional = zeemu (GPL referência aberta) + traces RE4/Zuma no core.
Infuse confirma só arquitetura: guest HLE + host GL presentation 640x480.

Artefatos Ghidra: `C:\Users\Lucas\AppData\Local\Temp\opencode\re_infuse\`
(hal_vtable.txt, renderer_vtable.txt, clsid_hits.txt, rtti_display.txt).

---

## 1. IDisplay — 48 slots (contrato)

Espelho BrewDisplay::setup_vtable() / idisplay_real.c (IDISPLAY_SLOTS 48).

| Slot | Método | Assinatura (R0=this) | RE4 | Zuma |
|---:|---|---|---|---|
| 0 | AddRef | R0=this | — | — |
| 1 | Release | 0 | — | — |
| 2 | GetFontMetrics | (font, AEEFontMetrics*, u16* ascent) | sim | — |
| 3 | MeasureTextEx | (font, psz, nl, nMaxWidth, pnFits) | sim | — |
| 4 | DrawText | (font, psz, nl, x, y, prcBg, flags) | **sim** | — |
| 5 | DrawRect | (pRect, frame, fill, flags@SP0) | **sim** | — |
| 6 | BitBlt | (xD,yD,cx, cy@SP0, pSrc, xS, yS, rop) | possível | — |
| 7 | Update | () mark frame | **sim** | — |
| 8 | SetAnnunciators | stub OK | — | — |
| 9 | Backlight | SUCCESS | — | — |
| 10 | SetColor | (index, RGBVAL) prev | **sim** | — |
| 11 | GetSymbol | stub | — | — |
| 12 | DrawFrame | stub | — | — |
| 13 | CreateDIBitmap | (ppIDIB, depth, w, h) | — | — |
| 14 | SetDestination | (pBitmap); NULL=device | — | — |
| 15 | GetDestination | bitmap | — | — |
| **16** | **GetDeviceBitmap** | (ppBmp) *pp=IDIB, R0=SUCCESS | **sim** | **sim** |
| **17** | **SetFont** | (nFont, pFont) prev | **sim** | — |
| 18 | SetClipRect | (pRect) | sim | — |
| 19 | GetClipRect | (pRect) | sim | — |
| 20-23 | Clone/MakeDefault/IsEnabled/NotifyEnable | IsEnabled=1 | — | — |
| 24 | CreateDIBitmapEx | stub | — | — |
| 25-47 | FnN | SUCCESS (não EFAILED) | — | — |

### Flags DrawRect
- IDF_RECT_FRAME=1, IDF_RECT_FILL=2
- RGB_NONE=0xFFFFFFFF
- ClearScreen: DrawRect(NULL, NONE, NONE, 0) → fill=background, flags=FILL

### IDIB / device bitmap (crítico soft-GL + QEGL)

Layout objeto (ZCLSID_DEVICE_BITMAP / DIBM):

| Off | Campo | Device (GetDeviceBitmap) | Offscreen RGB565 | Offscreen XRGB |
|---:|---|---|---|---|
| +0 | vtbl | stub QI | idem | idem |
| +4 | **pPaletteMap** | **0** (nunca CLSID) | 0 | 0 |
| +8 | pBmp | ZMEM_VRAM_BASE | heap | heap |
| +20/+22 | cx/cy | 640/480 | w/h | w/h |
| +24 | pitch | ver abaixo | w*2 | w*4 |
| +28 | nDepth | ver abaixo | **2 (BYTES)** | **32 (BITS)** |
| +29 | colorScheme | **16** (RGB565) | 16 | 0 |

**Assimetria documentada (Zuma):**

1. **Device** (zbrew_device_bitmap): nDepth=**16 BITS**, scheme=16, pitch=640*2 — Zuma 0x1ABA0 rejeita scheme 0 pós-InitGL.
2. **Soft RGB565** (zbrew_create_device_bitmap(..., 16)): nDepth=**2 BYTES** — upload Zuma faz nDepth>>1; bits=16 cai em ramo A42C (eor pBmp / free) e smash heap.
3. **Family Pack / EGL window**: alguns paths exigem device 32bpp pitch 640*4 (FAMILYPACK_BLACKSCREEN). Conflito com (1) — ver seção 5.

Objeto IDisplay: mínimo 64 bytes; **obj+0x14 = self** (AH ldr r8,[display,#0x14] → GetDeviceBitmap via r8).

Thunk ARMCC: display_find_arg_base() / find_arg_base() — pula cópias R0/R1/R2 na pilha.

---

## 2. IGraphics — 44 slots (contrato zeemu)

BrewGraphics::setup_vtable — 44 hooks. Pouco usado por RE4/Zuma (caminho = IDisplay + QEGL).

| Slot | Método |
|---:|---|
| 0-1 | AddRef / Release |
| 2-3 | SetBackground / GetBackground |
| 4-5 | SetColor / GetColor |
| 6-9 | Set/Get FillMode, Set/Get FillColor |
| 10-11 | Set/Get PointSize |
| 12-15 | Set/Get Clip, Set/Get Viewport |
| 16 | ClearViewport |
| 17-18 | Set/Get PaintMode |
| 19 | GetColorDepth |
| 20-29 | DrawPoint/Line/**Rect**/Circle/Arc/Pie/Ellipse/Triangle/Polygon/Polyline |
| 30 | ClearRect |
| 31-32 | EnableDoubleBuffer / **Update** |
| 33-35 | Translate / Pan / StretchBlt |
| 36-37 | Set/Get AlgorithmHint |
| **38-39** | **SetDestination / GetDestination** |
| 40-43 | Set/Get StrokeStyle, DrawEllipticalArc, DrawRoundRectangle |

No core: boot.c case 38 = IGraphics_SetDestination; DrawRect estendido via dispatcher stub 0x66.
**Correção mínima:** se jogo criar 0x01002001, não reusar vtable IDisplay de 48 slots — layout 44 distinto.

---

## 3. QEGL — 40 slots (contrato zeemu) vs IEGL

### IQEGL (AEECLSID_QEGL_REAL = 0x0103D8EC) — layout zeemu

| Slot | Método | Notas RE4 / Zuma / DE |
|---:|---|---|
| 0-1 | AddRef / Release | |
| 2 | QueryInterface | IIDs GLES/EGLSurfaceManip (zeemu lista) |
| **3** | **GetError** OU **InitGLSurface** | Heurística: R1=IDIB → DE InitGLSurface; senão *R1=EGL_SUCCESS (RE4) |
| 4 | GetDisplay OU InitGLSurface(slot4) | DE alias |
| **5** | **Initialize** | *SP0=1 SUCCESS |
| 6 | Terminate | *pRet=1 |
| 7 | QueryString | vendor/version strings |
| 8 | GetConfigs | *out=1 |
| **9** | **ChooseConfig** | R2=attribs (NÃO gravar); R3=pConfigs→1; SP1=pNum→1; SP2=pRet→1 |
| 10 | GetConfigAttrib | |
| **11** | **CreateWindowSurface** OU **CreateGL** | R3/R2 IDIB → surface; senão CreateGL → IGLES iface (DE) |
| 12-15 | Pixmap/Pbuffer/Destroy/Query Surface | |
| **16** | **CreateContext** | cfg≤16 e share==0 → *SP1=3; senão DE surface |
| **17** | **DestroyContext** OU **GetNativeContext** | ctx abaixo de 0x1000 → Destroy; senão R0=native handle |
| **18** | **MakeCurrent** | *SP1=1 |
| 19 | GetCurrentContext | |
| 20-24 | GetCurrentSurface/Display, QueryContext, WaitGL/Native | |
| **25** | **SwapBuffers** | Zuma: mark frame, NÃO clear FB; *out=1 se out na stack |
| 26 | CopyBuffers | |
| 27-39 | FnN | SUCCESS |

### IEGL (camada AEEGL) — offsets +1 vs QEGL em vários slots

| IEGL slot | Método | QEGL equiv |
|---:|---|---|
| 8 | GetProcAddress | (QEGL não tem no mesmo índice) |
| 10 | ChooseConfig | QEGL 9 |
| 12 | CreateWindowSurface | QEGL 11 |
| 17 | CreateContext | QEGL 16 |
| 19 | MakeCurrent | QEGL 18 |
| **26** | **SwapBuffers** | QEGL **25** |

egl_gl.c zegl_handle: SwapBuffers = **slot 26** (IEGL). Autopresent chama zegl_handle(26).

### Soft-GL / IGLES

- Vtable IGLES ~160 slots; mapa SDK Qualcomm (Brew3DCommon / g_igles_slot_map).
- Raster soft: raster_triangle() em egl_gl.c.
- Zuma: upload textura lê IDIB nDepth como **BYTES**; device bitmap scheme=16 obrigatório pós-InitGL.
- RE4: caminho IDisplay (DrawRect/SetFont/Update) + predicado applet+0x6C — **não** SwapBuffers no render_fn (RE4_RENDER_FN.md).

---

## 4. Comparação core vs contrato

| Área | Estado em idisplay_real.c / boot.c / egl_gl.c | Gap |
|---|---|---|
| IDisplay 48 slots + traps | OK (zbrew_make_idisplay_real) | slots 8,11,12,20,21,23-47 = SUCCESS genérico |
| DrawRect/BitBlt/Update/SetColor/SetFont/GetDeviceBitmap | OK | BitBlt: só XRGB fonte DIBM; falta escala e RGB565→XRGB no blit IDisplay |
| SetDestination → dest real | parcial | idisplay_real guarda s_dest_bitmap mas DrawRect/BitBlt ainda pintam VRAM (zfb_*) — **não redireciona** |
| IDIB +4=0, scheme Zuma | OK em boot.c | **FP 32bpp vs Zuma 16bpp** no mesmo singleton — ver seção 5 |
| QEGL ChooseConfig/CreateContext/MakeCurrent/SwapBuffers | OK c/ heurísticas DE | slot 19 GetCurrentContext stub; CopyBuffers stub |
| QEGL slot3 dual GetError/InitGLSurface | OK | frágil se IDIB falso-positivo |
| IEGL SwapBuffers 26 vs QEGL 25 | ambos cobertos | garantir QI devolve vtable certa |
| IGraphics 44 | quase stub | só SetDestination (38) + DrawRect via stub ext |
| soft-GL raster | parcial | blend/depth/tex incompletos (já em STATUS) |

---

## 5. Correções mínimas (prioridade RE4 + Zuma)

Ordenadas por impacto / diff curto. **Não** copiar Infuse.

### M1 — SetDestination honra dest no IDisplay real
**Onde:** idisplay_real.c cases 5/6/4 (DrawRect/BitBlt/DrawText).
**O quê:** se s_dest_bitmap != 0 e é DIBM, escrever em pBmp do dest (pitch/depth do IDIB), não só zfb_*.
**Por quê:** pipeline BREW clássico DIB→BitBlt; RE4 UI pode depender.
**Teste:** smoke RE4 300f — video_varied estável; log SetDestination seguido de Draw* sem crash.

### M2 — BitBlt RGB565→XRGB + pitch
**Onde:** idisplay_real.c case 6 (hoje assume read32 4 bpp).
**O quê:** se idib_depth_bytes(src)==2, ler u16, expandir RGB565→XRGB8888 (igual IBitmap_BltIn em boot.c).
**Por quê:** device/offscreen Zuma = RGB565; BitBlt atual lê lixo.
**Teste:** assert self-check 2x2 RGB565 blit → pixels XRGB esperados.

### M3 — Singleton device depth: não quebrar Zuma nem FP
**Onde:** zbrew_device_bitmap() / path EGL CreateWindowSurface.
**O quê (mínimo):** manter device **16/scheme16** para Zuma; no path FP/EGL que exige 32bpp, ou (a) segundo IDIB wrapper 32bpp apontando mesma VRAM com pitch 640*4 + conversão na apresentação, ou (b) setar depth por jogo só no CreateInstance do display — sem flip global mid-frame.
**Não:** pintar FB fake para video_varied.

### M4 — QEGL SwapBuffers (25) não limpa; autopresent só RUNNING
Já parcialmente feito (EGL_AUTO_SWAP, guard FP). Confirmar Zuma chama 25 e marca frame sem glClear destrutivo.

### M5 — IDisplay slots estendidos RE4 (0x4B/0x54/0x66)
Já em boot.c (ClearScreen/FillRect/DrawRect). Garantir **mesma** semântica em idisplay_real se RE4 usar objeto real (48) em vez do stub 128 — ou mapear traps altos → mesmos handlers.

### Fora de escopo (não fazer agora)
- IGraphics Draw* completo
- Copiar/decompilar handlers Infuse
- Vulkan / host GL Infuse Renderer events como HLE BREW

---

## 6. Sequências esperadas

### Zuma (QEGL + soft-GL)
1. CreateInstance DISPLAY → GetDeviceBitmap (16) → IDIB scheme=16
2. CreateInstance QEGL → Init/ChooseConfig(9) → CreateGL/Window(11) → CreateContext(16) → MakeCurrent(18)
3. IGLES draw / tex upload (nDepth BYTES em soft bitmaps)
4. SwapBuffers **QEGL 25** (ou IEGL 26) → mark frame

### RE4 (IDisplay)
1. DISPLAY em applet+0x68; helper +0x3C
2. SetFont(17), SetColor(10), DrawRect(5), Update(7); GetDeviceBitmap se EGL auxiliar
3. **Não** depender de applet+0x6C como render GL (RE4_RENDER_FN.md)
4. Vtable **48** slots; SetFont obrigatório (já: 900f sem derail)

---

## 7. Checklist verificação

- [x] IDisplay_GetDeviceBitmap → IDIB +4=0, scheme!=0 para Zuma
- [x] Soft create RGB565 → +28=2 (bytes)
- [x] QEGL ChooseConfig não escreve em attribs (R2)
- [x] CreateContext → handle 3; MakeCurrent *out=1
- [x] SwapBuffers QEGL=25 / IEGL=26; sem clear do FB do jogo
- [x] SetDestination redireciona Draw*/BitBlt (após M1) — `idisplay_real.c` dest_* ; guarda ptr (IImage roundtrip); paint só se IDIB
- [x] BitBlt RGB565 (após M2) — `idib_bpp` + `rgb565_to_xrgb`; self-check 2x2
- [x] Smoke (temp PPM + `ZEEMU_STUB_THREAD=1`): RE4 600f `video_first=FF000000 xor=FF000F03 varied=1`; Zuma 900f `video_first=FF713BD8 xor=0087A4D8 varied=1` — sem derail. PPM em Google Drive trava o smoke (fwrite sync).

### M1–M5 status (2026-11-06)

| Gap | Status | Nota |
|---|---|---|
| M1 SetDestination | feito | `dest_*` em DrawText/Rect/BitBlt; guarda ptr (IImage); paint só se IDIB; CreateDIBitmap 13 |
| M2 BitBlt RGB565 | feito | `idib_bpp` + `rgb565_to_xrgb`; self-check 2x2 (LOGW só se falha) |
| M3 device depth | sem flip | device 16/scheme16 mantido; FP path intacto |
| M4 SwapBuffers | ok | QEGL 25 mark-only; autopresent só RUNNING |
| M5 EXT 0x4B/54/66 | feito | case 0x27 dispatcher no real (48 slots) |

---

## 8. Referências internas

- src/brew/idisplay_real.c — IDisplay 48
- src/brew/boot.c — QEGL/IEGL dispatch, IDIB, IGraphics SetDestination
- src/gpu/egl_gl.c — IEGL/IGLES/soft raster
- zeemu: BrewDisplay.cpp, BrewEGL.cpp, BrewGraphics.cpp (reference lote1)
- Docs: DDRENDER_2D.md, RE4_RENDER_FN.md, SUPBTIME_RENDER.md, FAMILYPACK_BLACKSCREEN_20261106.md, INFUSE_RTTI.md
