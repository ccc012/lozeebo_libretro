# Guia de Testes — Zeebo LibRetro Core

Este documento substitui `TESTE_AVANCADO.md` e `TESTE_RETROARCH.md` (removidos). Reflete o
estado **atual** do core, não mais a fase de esqueleto nem a fase "0.5 avançado" original.

## Estado atual (o que há para testar)

- ✅ CPU ARM com suporte completo a Thumb (interpretador, ~60 MIPS)
- ✅ Memória com heap, RAM 64MB, stack 4MB, VRAM 2MB
- ✅ Loader de ROMs reais (MOD/MIF/BAR), com CLSIDs conhecidos de vários jogos
- ✅ BREW HLE: 117 funções helper (`AEEHelperFuncs`) + 128 métodos de `IShell`
- ✅ GPU (framebuffer XRGB8888) e áudio (mixer 44.1kHz estéreo)
- ✅ Input (RetroPad → bitmask Zeebo)
- ✅ Ferramentas de debug: log `[Zeebo]`, ring buffer de trace de PCs, detecção de fetch inválido

O core já carrega ROMs comerciais reais (não mais apenas um `.mod` fake) e avança pela
máquina de estados de boot real: `AEEMod_Load → IModule_CreateInstance → EVT_APP_START →
rodando`. Dependendo do jogo, o progresso para em pontos diferentes dessa cadeia — veja
"Estado esperado por ROM" abaixo e `docs/PROGRESS.md` para o
detalhe módulo a módulo.

**Não teste mais com um `.mod` fake esperando "tela preta, sem crash"** — isso valia para a
fase de esqueleto. Hoje o teste relevante é: o boot avança pelos estados reais, ou a CPU
"descarrila" (fetch fora de região executável) em algum ponto específico?

---

## TESTE RÁPIDO: smoke test sem abrir o RetroArch

A forma mais rápida de testar uma mudança é `tests/libretro_smoke.c`: um host libretro mínimo
que carrega a DLL do core diretamente via `LoadLibraryA`, chama as APIs libretro na ordem
certa e roda 180 frames, sem precisar da UI do RetroArch.

### Compilar (Developer Command Prompt / MSVC)

```powershell
cl /nologo /W3 tests\libretro_smoke.c /Fe:tests\libretro_smoke.exe
```

### Rodar contra uma ROM real

As 4 ROMs do Tier 1 estão dentro do repo, em `tests/roms/`:

```powershell
tests\libretro_smoke.exe x64\Release\zeebo_libretro.dll tests\roms\real_pacmania_game.mod
tests\libretro_smoke.exe x64\Release\zeebo_libretro.dll tests\roms\real_family_pack_game.mod
tests\libretro_smoke.exe x64\Release\zeebo_libretro.dll tests\roms\real_ddragon_game.mod
tests\libretro_smoke.exe x64\Release\zeebo_libretro.dll tests\roms\real_zeeboids_game.mod
```

O romset completo (68 jogos, ~2.1 GB) vive fora do repo, em:

```
C:\Users\Lucas\Downloads\zeebo-romset-and-devtools\Zeebo\Zeebo\Zeebo Game & App Compilation - OpenZeebo\
```

(pacotes `.7z` por jogo, mais uma pasta `274804\` já extraída com `mif/<id>.mif` e
`mod/<id>/...` de vários títulos juntos — usada para varredura em lote, ver
`analyze_clsids.ps1` na raiz do repo). IDs Tier 1: `276212` (Pac-Mania), `277229`
(Family Pack), `274754` (Double Dragon), `279382` (Zeeboids). Os demais 64 jogos ainda
não têm CLSID/fluxo de boot validado — ver `BLOCKERS_ANALYSIS.md` para o checklist.

**O que observar na saída (stderr)**:
- Linhas `[INFO] [Zeebo] ...` / `[ERROR] [Zeebo] ...` — progresso do boot (loader, CLSID
  resolvido, `AEEMod_Load`, `CreateInstance`, etc.)
- Linhas `[VIDEO] 640x480 pitch=... first=0x########` a cada 60 frames — confirma que
  `retro_run` está de fato produzindo frames (mesmo que a tela ainda esteja preta/sem
  desenho, o pipeline video está rodando)
- Se aparecer `CPU descarrilou: fetch em 0x... (LR=0x... SP=0x...)` seguido de um dump de
  trace, o boot travou — veja a seção "Lendo um crash de CPU descarrilou" abaixo.

Esse smoke test é o método preferido para iteração rápida durante desenvolvimento. O teste
manual no RetroArch (abaixo) continua útil para validar a integração com a UI/frontend real
(detecção do core, input, áudio, vídeo através do driver do RetroArch).

---

## TESTE NO RETROARCH

### Passo 1: Copiar a DLL

```powershell
Copy-Item -Path "C:\Users\Lucas\source\repos\zeebo_libretro\x64\Release\zeebo_libretro.dll" `
          -Destination "C:\RetroArch\cores\" -Force
```

(ajuste o caminho de destino para a instalação real do RetroArch, ex.:
`E:\SteamLibrary\steamapps\common\RetroArch\cores\`)

### Passo 2: Abrir o RetroArch e carregar o core

```
Main Menu → Load Core → buscar "Zeebo"
```

**Resultado esperado**: o core aparece na lista como "Zeebo" com uma string de versão (ex.:
`0.2-cpu` — essa string muda conforme o desenvolvimento avança; o que importa é que o core
seja reconhecido e listado, não o número exato).

### Passo 3: Carregar uma ROM real

```
Load Content → navegar até a pasta externa do romset (ver acima) → mod\<id>\... → selecionar o arquivo .mod
```

Extensões aceitas hoje pelo core: `.mod`, `.mif` e `.zip`
(`valid_extensions = "mod|mif|zip"` em `retro_get_system_info`). Em `.zip`, a extração
é responsabilidade do frontend libretro; o core não mantém cache próprio persistente de
arquivos extraídos. Não é mais necessário (nem representativo) criar um `.mod` fake —
use as ROMs reais da pasta externa do romset (ver Passo 3 acima).

**Resultado esperado**: depende do jogo (veja tabela abaixo). Em nenhum caso deve haver crash
do próprio RetroArch — se o boot travar, deve ser a CPU emulada que para (`g_cpu.halted =
true`) e loga `CPU descarrilou`, mantendo o RetroArch responsivo.

---

## Estado esperado por ROM

| ROM | CLSID | Progresso atual (2026-07-11, pós-rodada 3) |
|---|---|---|
| Double Dragon | `0x0102F789` | Boot completo até "rodando" sem descarrilar. Timers ativos e **desenho real**: `IDisplay_DrawRect` preenche o framebuffer (fb sai de `0xFF000000` para `0xFFFFFFFF` no smoke test). O mais avançado. |
| Pac-Mania | `0x01087B72` | Boot completo até "rodando" **sem descarrilar** — o estouro de stack (SP→VRAM) foi resolvido pelo HLE do bootstrap PIC no loader (`e747bd8`). Timers disparam callbacks continuamente. Tela ainda preta. |
| Zeebo Family Pack | `0x010903C6` | Boot completo até "rodando". **Regressão conhecida**: o jogo agenda o game loop via signals (não timers), e a política "CPU parada sem timers" (`a2f6767`) deixa a CPU idle após o boot — o loop GL que já funcionou parou de rodar (0 chamadas GL no smoke test). Bloqueio nº 1 atual. |
| Zeeboids | `0x0108FF1A` | Boot completo até "rodando" (antes travava no `AEEMod_Load`). Executa ~545k instruções no `EVT_APP_START` e fica idle — mesmo caso de signals do Family Pack. |

Se o comportamento observado ao testar divergir do descrito aqui, é sinal de que ou algo
quebrou ou algo avançou — atualize `docs/PROGRESS.md` de acordo.

---

## Ver logs de debug

### Rodando pelo terminal

```
RetroArch.exe --verbose --log-file retroarch.log
```

### Log já configurado

```
Verificar arquivo de log do RetroArch:
%APPDATA%\RetroArch\logs\ (ou o caminho passado em --log-file)
```

Em ambos os casos, procure por linhas prefixadas com `[Zeebo]` — todo log do core passa por
esse prefixo (ver `zlog` em `src/core/libretro_core.c`). É lá que aparecem as mensagens de
`AEEMod_Load`, resolução de CLSID, chamadas de trap HLE não implementadas, e qualquer
`CPU descarrilou`.

```
RetroArch → Settings → Logging → Verbose = ON
```

---

## Lendo um crash de "CPU descarrilou"

Quando o fetch da CPU cai fora de qualquer região executável válida (RAM, heap ou stack —
único lugar onde o BREW materializa código/thunks), o core loga e para a CPU em vez de
travar o processo:

```
[ERROR] [Zeebo] CPU descarrilou: fetch em 0x######## (LR=0x######## SP=0x########)
[INFO]  [Zeebo] trace: ultimos 64 PCs (mais recente por ultimo):
[INFO]  [Zeebo]   0x... 0x... 0x... 0x... 0x... 0x... 0x... 0x...
        ... (8 linhas de 8 valores = 64 PCs)
```

Como interpretar:

1. **`fetch em 0x...`** — o PC inválido em si. Compare a faixa com os limites de memória em
   `src/memory/memory.h` (`ZMEM_RAM_SIZE`, `ZMEM_HEAP_BASE/SIZE`, `ZMEM_STACK_BASE/SIZE`,
   `ZMEM_HLE_BASE/END`). Um valor como `0xFF000000`+ costuma ser um valor "de cor"/VRAM lido
   por engano como endereço — sinal de vtable/ponteiro corrompido, não de um branch real.
2. **`LR=0x...`** — para onde a CPU voltaria se desse `BX LR` no ponto do crash. Ajuda a achar
   *quem* chamou a função que descarrilou.
3. **`SP=0x...`** — se o SP estiver fora da região de stack esperada (`ZMEM_STACK_BASE`..
   `+ZMEM_STACK_SIZE`), o problema é corrupção/desalinhamento de pilha, não só de PC (foi o
   caso observado no Pac-Mania: o SP avança para dentro da VRAM antes do fetch inválido).
4. **Dump de trace** (`ztrace_dump`, em `src/debug/trace.c`) — os últimos 64 PCs executados,
   em ordem cronológica (mais recente por último). Use para reconstruir o caminho de código
   que levou ao descarrilamento; combine com o binário do `.mod` (ex. Ghidra) para identificar
   a instrução exata.

O mesmo mecanismo de log dispara também no primeiro acesso de memória inválido (ver
`src/memory/memory.c`), então um "descarrilamento" pode aparecer primeiro como um acesso de
leitura/escrita ruim antes mesmo do fetch de PC falhar — o dump de trace ajuda a diferenciar
os dois casos.

---

## Troubleshooting

### "Core não aparece no menu"

```
1. Verificar se a DLL foi copiada:
   dir C:\RetroArch\cores\zeebo_libretro.dll

2. Se não existe, copiar manualmente (ver Passo 1 acima)

3. Verificar permissões:
   - Pasta cores\ deve ser acessível
   - Usuário deve ter permissão de leitura

4. Reiniciar o RetroArch
```

### "Erro ao carregar o core"

```
Motivo: pode estar faltando dependência de runtime

Solução:
  1. Instalar Visual C++ Runtime:
     https://support.microsoft.com/pt-br/help/2977003
```

### "RetroArch trava/fecha ao carregar a ROM"

```
Isso NÃO é esperado mesmo em fase de bug ativo — a CPU emulada deve parar sozinha
("CPU descarrilou") sem derrubar o processo do RetroArch.

Se o RetroArch travar/fechar de verdade:
  1. É um bug fora da CPU emulada (ex.: acesso indevido em C, overflow de buffer real).
  2. Rodar via tests/libretro_smoke.c primeiro para isolar do frontend.
  3. Revisar o código nativo em src/ (não a ROM emulada).
```

### "Preciso ver mais detalhes"

```
1. Habilitar logging verbose (RetroArch → Settings → Logging → Verbose = ON)
2. Ver logs em:
   ~/.retroarch/retroarch.log (Linux/Mac)
   C:\Users\[user]\AppData\Roaming\RetroArch\logs\ (Windows)
3. Procurar por "[Zeebo]" nos logs
```

---

## Relatório de teste

Se testar, registre o resultado (e considere colar no `docs/PROGRESS.md` se for um avanço):

```
Core: Zeebo <versão de retro_get_system_info> (ex.: 0.2-cpu)
ROM testada: <nome / CLSID>
Data: [data de teste]
Método: [libretro_smoke / RetroArch manual]

Progresso alcançado:
- [ ] AEEMod_Load OK
- [ ] IModule_CreateInstance entra
- [ ] CreateInstance completa sem descarrilar
- [ ] EVT_APP_START tratado, estado "rodando"
- [ ] Frames de vídeo com conteúdo (não só framebuffer vazio)

Se descarrilou:
- PC / LR / SP do log
- Trecho relevante do dump de trace

Próximos passos sugeridos:
- [descrever]
```

---

## Próximos passos de teste

Uma vez que o boot de um jogo chegue a "rodando" sem descarrilar (já é o caso do Family
Pack), o próximo alvo de teste deixa de ser "o boot avança?" e passa a ser:

1. **Frames com conteúdo real** — hoje bloqueado pelo stub de `AEECLSID_EGL`/`AEECLSID_GL`
   no Family Pack; ver `docs/PROGRESS.md`.
2. **Corrigir o descarrilamento do Pac-Mania** durante a saída de `CreateInstance` (ver
   tabela acima e `docs/PROGRESS.md`).
3. **Validar Double Dragon e Zeeboids**, que ainda não têm CLSID/fluxo de boot confirmados.
4. **Performance**, uma vez que um jogo rode de fato: FPS via
   `RetroArch → Information → Framerate` (alvo 60 FPS) e uso de CPU no Task Manager.

Para o roadmap completo e o estado módulo a módulo, ver `docs/PROGRESS.md` e
`docs/PROGRESS.md`. Para dúvidas sobre build, ver `README.md`.
