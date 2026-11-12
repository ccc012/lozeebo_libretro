# 🎯 PROMPT — Rodada 5: 6 IAs em Paralelo (zeebo_libretro)

> Cole este arquivo inteiro na conversa de cada uma das 6 IAs. A atribuição é feita na
> hora do envio: o Lucas escreve junto com o prompt algo como **"você é responsável
> pela TAREFA B"** — e você faz SÓ essa tarefa, respeitando o escopo de arquivos dela.
> Se nenhuma tarefa foi indicada, pergunte antes de começar. Responda tudo em pt-BR.

---

## 🗺️ PARTE 0.1 — MAPA DE LOCAIS NO PC

| Local | Caminho completo | O que é |
|---|---|---|
| **Repositório** | `C:\Users\Lucas\source\repos\zeebo_libretro` | Código-fonte, scripts, testes, docs. Onde se trabalha. Branch principal: `master` (já contém todo o trabalho das rodadas anteriores). |
| **ROMs Tier 1** | `C:\Users\Lucas\source\repos\zeebo_libretro\tests\roms\` | As 4 ROMs prioritárias JÁ ESTÃO NO REPO: `real_pacmania_game.mod`, `real_family_pack_game.mod`, `real_ddragon_game.mod`, `real_zeeboids_game.mod`. Use essas. |
| **Romset completo (68 jogos)** | `C:\Users\Lucas\Downloads\zeebo-romset-and-devtools` | Estrutura: `Zeebo\Zeebo\Zeebo Game & App Compilation - OpenZeebo\` com `.7z` por jogo + pasta `274804\` já extraída (`mif\<id>.mif`, `mod\<id>\...`). Necessário só pra Tarefa F. |
| **RetroArch (cores)** | `C:\Program Files (x86)\Steam\steamapps\common\RetroArch\cores` | Onde a DLL é instalada (`build_safe.ps1` instala sozinho). Logs: `...\RetroArch\logs`. |
| **Vault Obsidian do Lucas** | `G:\Meu Drive\Documentos obi\Projeto\Emulador` | Documentação pessoal. Só leitura; quem atualiza no fim é a Tarefa F ou o Lucas. |

## 🧭 PARTE 0.2 — ONBOARDING RÁPIDO

Core libretro/RetroArch que emula o console **Zeebo** (2009). Jogos são **applets
BREW/AEE** (plataforma da Qualcomm) em ARM. O core interpreta CPU ARM/Thumb real (sem
JIT) e reimplementa **toda** a camada BREW via HLE — chamadas de API viram traps em
endereços mágicos `0xF0000000+` (enum `ZT_*` + macro `ZTRAP_ID()` em `src/brew/brew.h`;
**sempre decodifique um endereço `0xF0000xxx` com `ZTRAP_ID()` antes de concluir "API
não implementada"**).

Arquivos mais quentes desta rodada:

- `src/brew/boot.c` — máquina de estados de boot, `zbrew_handle_stub()` (os `case` por
  CLSID), `zboot_process_timers()` (timers), `zboot_tick()` (input/HID), timers em
  `g_timers[]`, signals do HID em `g_hid_button_signal`/`g_hid_position_signal`.
- `src/brew/brew.c/h` — dispatcher de traps.
- `src/gpu/framebuffer.c/h` — VRAM 640×480 XRGB8888 (`zfb_fill_rect` etc.).
- `src/gpu/egl_gl.c` (~1400 linhas) — HLE EGL/GLES 1.x, rasterizador de software.
- `src/loader/mod_loader.c` — inclui o **HLE do bootstrap PIC** (`e747bd8`) que
  destravou Pac-Mania/Zeeboids: módulos `mod1` são mapeados na VA 0 direto.
- `src/input/input.c` — RetroPad → bitmask Zeebo.
- `src/core/libretro_core.c` — `retro_run()` (loop por frame).
- Relatórios das rodadas anteriores (LEIA o da sua tarefa): `STATUS_A_EVENTO_DOUBLE_DRAGON.md`,
  `STATUS_B_RENDER_FAMILYPACK.md`, `STATUS_C_BOOT_PACMANIA_ZEEBOIDS.md` (raiz do repo),
  `docs/PROGRESS.md` (sessão "2026-07-11 (3)" tem o resumo mais atual).

### Compilar e testar

```powershell
cd <SEU WORKTREE>          # ver regra de worktree abaixo
.\build_safe.ps1           # testes pré-build + compila + instala no RetroArch
# (se o .ps1 falhar por encoding, chame o msbuild direto:)
#   msbuild zeebo_libretro.sln /p:Configuration=Release /p:Platform=x64 /m:4 /v:minimal

tests\libretro_smoke.exe x64\Release\zeebo_libretro.dll tests\roms\real_ddragon_game.mod
```

Na saída (stderr): `[INFO]/[ERROR] [Zeebo] ...` = progresso do boot; `frame N: ...
instrucoes=X PC=... halted=... fb[0]=0x...` a cada 60 frames = pulso da CPU e cor do
primeiro pixel; `CPU descarrilou: fetch em 0x...` + trace de 64 PCs = travou (leia o
trace). **Atenção ao campo `instrucoes=`**: se ele congela entre frames, a CPU está
estacionada — foi assim que a regressão de signals foi descoberta.

## 📖 PARTE 1 — ESTADO ATUAL (verificado por smoke test em 2026-07-11, pós-rodada 3)

**Marco: os 4 jogos Tier 1 completam o boot inteiro (`AEEMod_Load → CreateInstance →
EVT_APP_START → "rodando"`) sem nenhum descarrilamento.** O gargalo mudou de "boot
trava" para "entregar os eventos e o desenho que o jogo pede DEPOIS do boot".

| Jogo | Depois do boot | Bloqueio atual |
|---|---|---|
| **Double Dragon** ★ | Timers ativos; `IDisplay_DrawRect` REAL pinta o framebuffer (preto → branco confirmado) | Só desenha retângulos/clear — falta DrawText/BitBlt/bitmaps pra virar imagem reconhecível |
| **Pac-Mania** | Timers disparam callbacks continuamente (~70k instruções/60 frames) | Tela ainda preta — não sabemos qual caminho de desenho ele usa |
| **Family Pack** | **REGRESSÃO**: 0 chamadas GL; instruções congeladas em 729.568 do frame 1 ao 121 | Agenda o game loop via **signals** (`SignalCBFactory`), não timers; a política "CPU parada sem timers" (`a2f6767`) estaciona a CPU pra sempre. Antes dessa política, o loop GL dele JÁ RENDERIZAVA (sessões 2026-07-08). |
| **Zeeboids** | ~545k instruções no `EVT_APP_START`, depois idle | Mesmo caso de signals do Family Pack |

### ⚠️ Regra de isolamento (lição da rodada 2 — obrigatória)

Cada IA usa **`git worktree`** (diretório físico próprio), nunca só `git checkout -b`
no diretório principal — na rodada 2 uma IA trocou o branch por baixo dos pés da outra
e um fix quase se perdeu sem commit.

```powershell
cd C:\Users\Lucas\source\repos\zeebo_libretro
git status                                    # não atropele nada pendente
git worktree add ..\zeebo_libretro-<slug> -b rodada5/<slug> master
cd ..\zeebo_libretro-<slug>                   # trabalhe SÓ aqui
```

`<slug>` = nome da sua tarefa: `signals`, `idisplay`, `pacmania-draw`, `gles-fp`,
`input-hid`, `validacao`. Commits pequenos em pt-BR terminando com
`Co-Authored-By: <seu nome> <noreply@anthropic.com>`. Nunca commitar em `master`,
nunca `push --force`. **Não commite mudanças no `.vcxproj` de toolset (v142/v143)** —
é ajuste local da máquina.

## 🔎 PARTE 2 — INVESTIGAR ANTES DE COMPILAR (obrigatório)

Padrão de qualidade = `STATUS_C_BOOT_PACMANIA_ZEEBOIDS.md`: causa raiz confirmada por
desmontagem (capstone via Python) e matemática exata, não suposição.

1. Leia o `STATUS_*`/seção de `docs/PROGRESS.md` relevante à sua tarefa.
2. Rode o smoke test ANTES de mudar qualquer coisa — essa é sua baseline.
3. Hipótese sem certeza? Desmonte o trecho do `.mod` com capstone ou adicione
   `LOGE` temporário (revertido antes do commit final) pra ver valores reais.
4. Só edite código com hipótese + evidência.
5. Depois: `.\test_before_build.ps1` → build → smoke test comparando com a baseline.
6. Nunca reporte "✅ implementado" sem colar o log de DEPOIS do fix.
7. **Regressão obrigatória**: antes de encerrar, rode o smoke test nos 4 jogos Tier 1 e
   compare o campo `instrucoes=`/`fb[0]=` com a baseline — a rodada 3 introduziu uma
   regressão exatamente por não checar isso num jogo que "não era o alvo" da mudança.

---

## 🅰️ TAREFA A — Signals: reativar Family Pack e Zeeboids (bloqueio nº 1)

**Worktree/branch:** `rodada5/signals` · **Saída:** `STATUS_R5_A_SIGNALS.md`
**Escopo:** `src/brew/boot.c` (registro/dispatch de signals, `zboot_process_timers`,
`zboot_tick`), `src/core/libretro_core.c` se precisar de um hook por frame. NÃO mexa
no IDisplay (Tarefa B) nem no rasterizador GLES (Tarefa D).

**O problema, com precisão:** o Family Pack cria signals via
`SignalCBFactory_CreateSignal(cb=..., user=...)` (case 3 de `zbrew_handle_stub()` — já
funciona e devolve objetos válidos). Mas nada no core **dispara** esses signals depois:
com `a2f6767` ("CPU fica parada quando não há timers"), a CPU estaciona após o
`EVT_APP_START` e o jogo nunca mais executa. Antes dessa política, o Family Pack rodava
seu loop GL porque a CPU ficava livre (`halted=false`) — ou seja, o jogo NÃO depende de
um evento externo real; ele só precisa de CPU pra rodar o scheduler próprio dele.

**Investigação (antes de codar):**
1. Baseline dos 4 jogos. No log do Family Pack, colete TODOS os
   `SignalCBFactory_CreateSignal` (cb + user) e o log suspeito
   `SignalCBFactory outputs: signal=0x00000000` — o out-param `signal` está sendo
   escrito como 0? Se sim, o jogo recebe um ponteiro nulo de signal e pode nem tentar
   usá-lo. Confira no case 3 o que é escrito nos dois out-params.
2. Desmonte (capstone) o que o callback `cb` de cada signal faz — é o tick do
   scheduler do jogo? Isso decide o design.

**Design a implementar (ajuste conforme a investigação):**
- Registrar cada signal criado numa tabela (`g_signals[]`, análoga a `g_timers[]`):
  callback, user, estado (armado/desarmado).
- Semântica BREW: `ISignalCtl_Enable` arma o signal; quando o "evento" ocorre, o core
  chama o callback e o signal desarma até o jogo rearmar. Para signals de
  render/scheduler (caso do Family Pack), o "evento" pode ser o próprio frame: despache
  1 signal armado por `retro_run()` (mesmo padrão de `zboot_process_timers`, estado
  `BOOT_SIGNAL_CALL` já existe no enum).
- Alternativa mínima se o dispatch genérico travar: se há signals registrados e nenhum
  timer ativo, liberar a CPU (`halted=false`) — recupera o comportamento pré-`a2f6767`
  só pros jogos com signals, preservando o ganho pros jogos de timer. Documente qual
  caminho escolheu e por quê.

**Meta:** Family Pack volta a emitir `glClear`/`glDrawArrays`/`eglSwapBuffers`
(compare `grep -c` no log com a baseline = 0) e Zeeboids sai de idle (instruções
crescendo entre frames). **Regressão:** Pac-Mania e Double Dragon continuam iguais ou
melhores.

---

## 🅱️ TAREFA B — IDisplay completo: Double Dragon rumo à primeira imagem reconhecível

**Worktree/branch:** `rodada5/idisplay` · **Saída:** `STATUS_R5_B_IDISPLAY.md`
**Escopo:** `src/brew/boot.c` (SÓ os cases do `AEECLSID_DISPLAY_REAL` no
`zbrew_handle_stub()` e os helpers `display_*` já existentes), `src/gpu/framebuffer.c`
e `src/brew/ibitmap.c` se precisar de primitivas novas. NÃO mexa em signals (Tarefa A).

**Contexto:** o commit `65ae02e` provou o método: o IDisplay real tem layout de 48
slots (referência: `BrewDisplay` do zeemu, em `reference/zeemu-main/`), e implementar o
slot certo com a semântica certa fez o Double Dragon pintar a tela. Hoje só
DrawRect/Update/SetColor existem. O jogo certamente chama mais slots — cada um que
cai em stub genérico é desenho perdido.

**Investigação:** rode o Double Dragon com log dos slots do display (adicione um `LOGD`
temporário no dispatch do stub do display listando `slot`, `r0-r3`, `stack[0..2]`).
Liste TODOS os slots que o jogo chama e a frequência. Priorize por aí.

**Implementar (ordem provável de valor):**
1. **DrawText** (slot 4 no layout de 48) — mesmo que renderize com uma fonte embutida
   tosca de 8×8 (bitmap font hardcoded serve), texto na tela é um salto enorme.
2. **BitBlt** (slot 6) — copiar bitmap pro framebuffer; conecte com `ibitmap.c`.
3. **Update/UpdateEx** — garantir que apresenta e não apaga.
4. `GetDeviceBitmap`/`CreateDIBitmap` se o jogo desenhar direto no bitmap do device
   (nesse caso o desenho já cai na VRAM e "só" precisa não ser sobrescrito).
5. Slots de clipping/cor que aparecerem no log.

**Meta:** o framebuffer do Double Dragon mostra mais que branco sólido — texto,
sprites ou pelo menos retângulos coloridos distintos. Cole os valores de `fb[0]` e, se
possível, um dump de mais pixels (ex.: `LOGD` de 8 pixels espalhados) como evidência.
Teste também o Pac-Mania (a tela preta dele pode ser exatamente um desses slots
faltando).

---

## 🅲️ TAREFA C — Pac-Mania: descobrir e destravar o caminho de desenho

**Worktree/branch:** `rodada5/pacmania-draw` · **Saída:** `STATUS_R5_C_PACMANIA_DRAW.md`
**Escopo:** diagnóstico livre (logs temporários em qualquer lugar), mas fixes só em
código que NÃO seja: cases do display (Tarefa B), signals (Tarefa A), rasterizador
GLES (Tarefa D). Se a causa cair no território de outra tarefa, documente com precisão
e entregue o diagnóstico pra ela — isso conta como sucesso da tarefa.

**Contexto:** Pac-Mania boota, timers disparam callbacks continuamente (~70k
instruções/60 frames — a CPU ESTÁ trabalhando), mas `fb[0]` fica em `0xFF000000`
(preto) pra sempre. Alguma coisa que o jogo tenta fazer pra desenhar está caindo num
stub silencioso.

**Investigação:**
1. Baseline + log completo. Liste todas as chamadas de trap/stub que o jogo faz DEPOIS
   do `EVT_APP_START` (CLSIDs pedidos no `CreateInstance` do IShell, slots chamados nos
   stubs, helpers). O que ele pede que devolvemos vazio?
2. Candidatos prováveis: `IGraphics` (`AEECLSID_GRAPHICS`), `IImage`/`IImageDecoder`
   (o jogo pode desenhar via imagens BMP/PNG dos resources), `IDisplay_BitBlt` com
   bitmap carregado do `.bar`, ou o próprio `IDisplay_DrawText`. O `.bar` companheiro
   (resources) hoje é só detectado, não parseado — se o jogo tenta carregar sprites
   dele e recebe erro, ele pode desistir de desenhar sem travar.
3. Se for o `.bar`: faça um hexdump dos primeiros KB do `.bar` do Pac-Mania (romset
   externo, `274804\`) e identifique o formato (BAR do BREW é um container de
   resources com IDs — referência no zeemu).

**Meta:** ou (a) um fix que faz o Pac-Mania desenhar QUALQUER coisa, ou (b) um
diagnóstico com evidência dizendo exatamente qual interface/resource falta e pra qual
tarefa/rodada isso aponta. Regressão nos outros 3 antes de encerrar.

---

## 🅳️ TAREFA D — GLES/Family Pack: frame de menu correto (depende parcialmente da A)

**Worktree/branch:** `rodada5/gles-fp` · **Saída:** `STATUS_R5_D_GLES_FP.md`
**Escopo:** `src/gpu/egl_gl.c` apenas.

**Contexto:** quando a Tarefa A reativar o loop GL do Family Pack, os problemas antigos
de render voltam a ser visíveis: vértices computados caindo fora da área visível
(suspeita de transformação/viewport) e cobertura mínima de texturas/blend. O fix de
ponteiros (`f4ba57d`, heap aceito em `decode_vertex_ptr`) já está em master.

**Enquanto a Tarefa A não entrega** (não fique bloqueado):
1. Baseline histórica: leia `STATUS_B_RENDER_FAMILYPACK.md` e a sessão 2026-07-10 (2/3)
   de `docs/PROGRESS.md` — os logs antigos mostram `glVertexPointer`/`DrawArrays` com
   endereços e contagens reais.
2. **Teste unitário do rasterizador sem o jogo**: escreva um harness mínimo (pode ser
   um `#ifdef` de teste ou um teste em `tests/`) que chama `zgl_handle()` diretamente
   com a MESMA sequência de chamadas GL que o log antigo do Family Pack mostra
   (LoadIdentity → Orthox → VertexPointer → TexCoordPointer → DrawArrays TRIANGLE_FAN
   count=4) e verifica em que pixels o rasterizador escreve. Se os vértices caem fora
   de 640×480 com entradas conhecidas, o bug de transformação é reproduzível e
   consertável SEM o jogo rodar.
3. Suspeitos concretos do código atual: ordem linha×coluna nas multiplicações de matriz
   em `transform_vertex()` (`g_m_mv[0*4+i]*in[0] + ...` — confira se é column-major
   como o GL real), sinal do Y na conversão pra tela, e `g_gl_viewport_*` inicializados
   com o valor certo quando o jogo NÃO chama `glViewport`.
4. Quando/se a Tarefa A entregar na branch dela, puxe o commit dela pro seu worktree
   (`git cherry-pick` ou merge da branch `rodada5/signals`) e valide contra o jogo real.

**Meta:** triângulos do Family Pack dentro da área visível com coordenadas coerentes
de menu 2D; idealmente `fb[0]` deixando de ser cor sólida. Evidência: coordenadas de
tela logadas antes/depois + dump de pixels.

---

## 🅴️ TAREFA E — Input/HID ponta a ponta

**Worktree/branch:** `rodada5/input-hid` · **Saída:** `STATUS_R5_E_INPUT.md`
**Escopo:** `src/input/input.c`, a parte de HID de `src/brew/boot.c` (`zboot_tick`,
`g_hid_button_signal`, cases do IHID) e `tests/libretro_smoke.c` (pra injetar input).

**Contexto:** o caminho RetroPad → bitmask → evento HID → signal existe desde cedo mas
NUNCA foi testado ponta a ponta (nenhum jogo chegava vivo o bastante). Agora Double
Dragon e Pac-Mania rodam com timers ativos — dá pra testar de verdade.

1. **Primeiro problema prático**: o smoke test não injeta input. Adicione ao
   `libretro_smoke.c` uma simulação simples (ex.: a partir do frame 120, reportar
   START ou botão A pressionado por 10 frames no `input_state_cb`). Isso vira
   infraestrutura permanente de teste.
2. Rode Double Dragon e Pac-Mania com o input simulado. O log mostra
   `IHIDDevice_RegisterForButtonEvent`/`HID: uid=... down=...`? O callback do signal
   de botão é chamado no guest? O jogo reage (instruções/framebuffer mudam)?
3. Se o jogo nem registra o signal de botão: descubra o que ele usa em vez disso
   (`ISHELL_GetKeys`? eventos `EVT_KEY` via `HandleEvent`?). Muitos jogos BREW recebem
   input por `EVT_KEY`/`EVT_KEY_PRESS` no `IAPPLET_HandleEvent` — se for o caso,
   implemente o envio de `EVT_KEY` a partir do `zboot_tick()` (o `HandleEvent` do
   applet já é chamado no boot; reutilize o mesmo mecanismo de `guest_call`).
4. Cuidado com colisão com a Tarefa A: se precisar tocar no dispatch de signals,
   coordene pelo relatório (implemente atrás do mecanismo dela ou documente o handoff).

**Meta:** evidência de UM jogo reagindo a input (mudança de estado/framebuffer/log
após o botão simulado). Se nenhum reagir, o diagnóstico exato de qual mecanismo cada
jogo espera já vale a tarefa.

---

## 🅵️ TAREFA F — Varredura dos 68 jogos + integração + consolidação

**Worktree/branch:** `rodada5/validacao` · **Saída:** `RODADA_5_CONSOLIDADO.md`
**Escopo:** leitura de tudo; escrita só de documentação, scripts e merge de integração.
NÃO mexe em `src/`.

**Fase 1 (paralela, começa já):**
1. Rode `analyze_clsids.ps1` contra o romset externo e registre quantos dos 68 jogos
   têm CLSID resolvível.
2. **Varredura de boot**: script PowerShell que roda `tests\libretro_smoke.exe` contra
   cada `.mod` da pasta `274804\mod\` (o HLE do bootstrap PIC de `e747bd8` deve
   destravar vários módulos `mod1` além do Tier 1) e classifica cada jogo pela última
   linha relevante do log: `rodando` / `descarrilou em 0x...` / `CLSID desconhecido` /
   `formato não aceito`. Salve a tabela completa (68 linhas) no consolidado. Essa
   tabela define o Tier 2 real com dados, não chute.
3. Destaque os 5-10 jogos que chegarem mais longe fora do Tier 1 (candidatos: Crash
   `274214`, Quake `274802`, Quake II `276153`, FIFA 09 `274803`, RE4 `276675`).

**Fase 2 (quando A-E terminarem):**
4. Worktree de integração: merge das branches `rodada5/*` com resultado positivo, na
   ordem A → B → C → D → E (A primeiro porque D e E dependem dela). Conflito de lógica
   → devolve pra IA da tarefa; conflito trivial → resolve.
5. Rebuild + smoke test dos 4 Tier 1 no resultado integrado + re-rode a varredura dos
   68 nos 10 melhores. Compare com as baselines individuais — integração que regride
   algum jogo não vai pra frente sem investigação.
6. `RODADA_5_CONSOLIDADO.md`: comece com resumo de 10 linhas em linguagem simples (o
   Lucas não é programador): quais jogos melhoraram, o que dá pra VER na tela agora, o
   que falta. Depois: tabela por jogo antes→depois com evidência, tabela dos 68, e o
   próximo passo nº 1 pra Rodada 6.
7. Se tiver acesso de escrita ao vault (`G:\Meu Drive\Documentos obi\Projeto\Emulador\
   12_Estado_Atual\`), atualize `00_STATUS_ATUAL.md` e `02_HISTORICO_PROGRESSO.md`;
   senão, inclua uma seção "para o Lucas colar no Obsidian".

---

## 🔄 SINCRONIZAÇÃO (todas as tarefas)

1. Resumo de até 8 linhas no topo do seu `STATUS_R5_*.md`: o que mudou, se compila, se
   foi testado DE VERDADE (log colado), resultado real. **Evidência > afirmação.**
2. Handoffs entre tarefas (A→D, A↔E, C→B) acontecem via os arquivos de status e
   cherry-pick/merge entre branches `rodada5/*` — nunca editando o worktree alheio.
3. Ninguém faz merge pra `master` exceto a Tarefa F na fase de integração — e mesmo
   esse merge fica numa branch `rodada5/integracao` pro Lucas aprovar depois de testar
   no RetroArch de verdade.
4. Ao terminar, cada IA remove o próprio worktree (`git worktree remove
   ..\zeebo_libretro-<slug>`) — a branch fica, só o diretório sai.
