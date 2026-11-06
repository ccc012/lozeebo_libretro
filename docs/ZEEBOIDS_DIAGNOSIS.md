# Diagnostico: falha de startup do Zeeboids (Zeebo / ARM BREW)

Data da analise: 2026-11-04  
Repositorio: `C:\Users\Lucas\source\repos\zeebo_libretro`  
Logs analisados: `C:\Users\Lucas\AppData\Local\Temp\zb_fmt.log`, `zb_sig1.log`  
Referencias: `docs/RE_INFUSE_NOTES.md`, `docs/PROGRESS.md`, `STATUS_C_BOOT_PACMANIA_ZEEBOIDS.md`

## 1. Resumo

O Zeeboids (CLSID `0x0108FF1A`) ja superou o problema antigo de bootstrap PIC e chega a
`EVT_APP_START` com estado "jogo RODANDO". O novo bloqueio ocorre **depois** da inicializacao do
applet, quando a thread de guest e disparada por `IThread_Start`:

- a thread inicializa EGL/GLES, tenta abrir `xui/title.xui` e `xui/warning.xui` (ambos ausentes no VFS);
- em seguida o jogo imprime `TTDMM ASSERT: Error 62 @ source\TTDMemoryPool.cpp:1661`;
- nenhuma chamada `SignalCBFactory_CreateSignal` aparece nos logs;
- o jogo nao registra sinais e nao entra no loop de render real.

O heap do core nao esta esgotado no momento do assert (restam ~9 MB contiguos), portanto a causa
mais provavel nao e "falta de memoria total", mas sim um problema de **inicializacao/contrato do
pool de memoria TTD** (alinhamento, tamanho util apos header, ou estado do MemoryManager) ou um
caminho de recurso ausente disparado pela thread. A causa exata ainda precisa ser confirmada por
experimentos.

## 2. Configuracao de memoria do core

Definida em `src/memory/memory.h` / `src/memory/memory.c`:

| Regiao | Base | Tamanho |
|---|---:|---:|
| RAM principal | `0x00000000` | 64 MB |
| Heap (MALLOC/FREE BREW) | `0x10000000` | 32 MB (`0x10000000`..`0x11FFFFFF`) |
| Stack | `0x2FC00000` | 4 MB |
| VRAM | `0x30000000` | 2 MB |

O alocador (`src/memory/heap.c`) e uma lista encadeada de blocos dentro do proprio heap emulado,
com header de 16 bytes e alinhamento de **8 bytes** (`ZHEAP_ALIGN = 8u`).

## 3. Sequencia exata de alocacoes observada

Fonte: `zb_fmt.log` / `zb_sig1.log`.

| # | Momento / gatilho | Chamada | Tamanho | Endereco retornado | `usado` apos |
|---|---|---|---:|---:|---:|
| 0 | `brew_init` (core) | objeto IShell no heap | (nao logado como MALLOC) | `0x10000010` | — |
| 1 | `AEEMod_Load` | `MALLOC` (helper `0x068`) | 36 B | `0x10001498` | 5200 |
| 2 | `IModule_CreateInstance` | `MALLOC` | 120 B | `0x100014E8` | 5328 |
| 3 | `IModule_CreateInstance`, `ZeeboApplication.cpp:532` (imprime "Zeeboids v 1.1.1402") | `MALLOC` | **24.117.248 B (23 MiB, `0x01700000`)** | **`0x100017D0`** | 24.123.152 |
| — | apos CreateInstance | `IThread_Start(stack=0x00080000, pfn=0x0000B6CC, user=0x100014E8)` | — | SUCCESS | — |

Estado do heap apos a alocacao #3:

- bloco #3 ocupa `0x100017D0` .. `0x117017D0` (23 MiB);
- heap total: `0x10000000` .. `0x12000000` (32 MiB);
- espaco livre contiguo apos o bloco: `0x117017D0` .. `0x12000000` = **9.431.280 B (~8,99 MiB)**;
- uso acumulado: 24.123.152 / 33.554.432 = ~71,9%;
- antes do bloco #3 existem os objetos pequenos de #1/#2 e o IShell do core, formando um fragmento
  inicial de ~6 KB (`0x10000000`..`0x100017D0`).

Caminho executado pela thread (`pfn=0x0000B6CC`) antes do assert:

1. `vsprintf` / `strncpy` / `GetAppInstance`;
2. `IShell_CreateInstance(0x01001003)` -> `0x11701830`;
3. `IFileMgr_Test/OpenFile: resources.pakz` -> VFS carregado (448 membros, 21.730.345 bytes), mas `resources.pakz` nao encontrado;
4. `IFileMgr_Test/OpenFile: resources.pak` -> nao encontrado;
5. `IShell_CreateInstance(0x0106C411)` e `0x0103D8EC`;
6. QEGL: `InitGLSurface`, `Initialize`, `ChooseConfig`, `CreateGL`;
7. GLES: `glClearColorx`, `glClearStencil`, `glClearDepthx`, `glDisable`, `glTexEnvx`, `glLightModelxv`, `glEnableClientState`, `glClear`;
8. `IFileMgr_Test/OpenFile: xui/title.xui` -> **nao encontrado**;
9. `IFileMgr_Test/OpenFile: xui/warning.xui` -> **nao encontrado**;
10. `GetUpTimeMS`, `sprintf`/`vsnprintf`;
11. **`TTDMM ASSERT: Error 62 @ source\TTDMemoryPool.cpp:1661`** (impresso via `TTDMemoryManager.cpp:916`);
12. depois disso o thread continua e repete `glViewport` + assert em loop, sem `CreateSignal`.

## 4. Detalhes do assert

Log (resumido):

```
TTDMM ASSERT: [Condition | Line @ File]
  Error 62
  1661 @ source\TTDMemoryPool.cpp
```

O binario do jogo contem, entre outras, as seguintes mensagens associadas ao TTDMM:

- `MemoryManager not initialized`
- `Invalid MemoryPool id`
- `Invalid block size`
- `MemoryPool has not been initialized`

O codigo 62 nao pode ser mapeado com certeza a uma dessas mensagens sem o fonte do jogo, mas as
strings indicam que o assert esta relacionado a **estado/contrato do pool**, nao a um erro generico
de renderizacao.

## 5. Hipoteses

### H1 - Tamanho util do pool nao multiplo do bloco (mais provavel)

A alocacao #3 retorna `0x100017D0`. O jogo aparenta usar um header de `0x70` bytes antes da area
util do pool, porque:

```
0x100017D0 + 0x70 = 0x10001840
```

e `0x10001840` fica alinhado a 64 bytes. Assim, a area util do pool seria:

```
0x01700000 - 0x70 = 0x016FFFF90
```

Esse valor nao e multiplo de 32 (resto 16). Se o `TTDMemoryPool` exigir que o tamanho util seja
multiplo do tamanho de bloco (16/32/64), isso dispararia exatamente um assert do tipo
"Invalid block size".

### H2 - Alinhamento/base do pool

O endereco base retornado (`0x100017D0`) e alinhado a 16 bytes, mas nao a 32/64/4096. O core
usa alinhamento de 8 bytes e mantem objetos pequenos no inicio do heap, deslocando a alocacao
grande em `0x17D0`. Um pool que exija base alinhada a pagina (4 KB) ou a um limite maior falharia.

Observacao: a transformacao `base + 0x70` produz uma area util alinhada a 64 bytes, o que sugere
que o jogo espera que o **base bruto** tenha um alinhamento especifico (16 bytes mod 64), e nao
necessariamente 64. Portanto H1 e mais forte que H2, mas ambos devem ser testados.

### H3 - Recursos ausentes (`xui/title.xui`, `xui/warning.xui`)

A thread tenta abrir esses dois arquivos e o VFS responde "nao encontrado". O caminho seguinte
chama o assert. Pode ser que o carregador de UI/XUI passe um buffer nulo/vazio para o
TTDMemoryPool quando o arquivo falta, disparando "MemoryPool has not been initialized" ou
"Invalid MemoryPool id".

### H4 - Execucao da thread em si

O assert so aparece apos `IThread_Start` e durante a execucao do corpo da thread. Pode ser um
problema de ordem de inicializacao (sinais/timers ainda nao registrados quando a thread roda),
nao do pool.

### H5 - Tamanho total do heap

Com 23 MiB alocados, restam ~9 MiB. E provavel que seja suficiente para o bootstrap, mas nao
ha evidencia de que o jogo nao precise de mais. Heap maior e um experimento barato e de baixo
risco.

### H6 - Fragmentacao

A fragmentacao atual e baixa: ha um unico bloco livre contiguo de ~9 MiB apos o pool. Os unicos
fragmentos sao os ~6 KB iniciais com IShell e os objetos pequenos. Fragmentacao severa nao
explica o assert imediato, mas o deslocamento inicial pode afetar alinhamento (H2).

## 6. Heap size / alinhamento / fragmentacao podem causar isso?

- **Tamanho do heap**: improvavel como causa primaria. O `MALLOC` de 23 MiB ja cabe em 32 MB e
  restam ~9 MiB. Porem, o jogo pode assumir um heap maior ou alocar mais depois; aumentar o heap
  e um experimento valido para descartar H5.
- **Alinhamento**: **candidato forte**. O alocador atual tem 8 bytes, e o bloco grande comeca em
  `0x100017D0`. O header de `0x70` produz area util alinhada a 64 bytes, mas o tamanho util
  deixa resto 16 para blocos de 32/64. Testar alinhamentos maiores e padding do tamanho e
  recomendado.
- **Fragmentacao**: nao parece ser a causa direta (bloco livre contiguo de ~9 MiB), mas o
  fragmento inicial de 6 KB afeta o endereco base do pool e pode interagir com H2.

## 7. Experimentos recomendados (sem alterar codigo de producao final; usar builds de teste)

1. **Stub de `IThread_Start`** (mais barato e decisivo): fazer o handler retornar `SUCCESS`
   sem registrar `g_thread_fn` e sem chamar `guest_call`.  
   - Se o assert desaparecer: o problema esta no caminho executado pela thread (H3/H4).  
   - Se o assert permanecer: o problema esta na inicializacao do pool durante `CreateInstance` (H1/H2/H5).

2. **Heap maior**: alterar `ZMEM_HEAP_SIZE` para 64 MB e depois 128 MB, rebuild do smoke.  
   Verificar se o assert muda de codigo/linha ou se `CreateSignal` aparece.

3. **Alinhamento diferente**: alterar `ZHEAP_ALIGN` para 16, 32 e 64; opcionalmente alinhar
   apenas blocos grandes (>= 1 MB) a 4096 bytes. Verificar se `0x100017D0` muda e se o assert
   desaparece.

4. **Padding/tamanho do bloco**: ao alocar, arredondar o tamanho solicitado para multiplo de 32
   ou 64 antes de reservar o bloco. Testar se "Invalid block size" deixa de ocorrer.

5. **Base alinhada / alocacao no topo**: alocar o bloco grande a partir do topo do heap ou em
   um endereco fixo alinhado, evitando os ~6 KB iniciais. Testar H2/H6.

6. **Recursos ausentes**: fornecer `xui/title.xui` e `xui/warning.xui` validos no VFS (ou
   stubar `IFileMgr_OpenFile` para retornar um arquivo vazio valido). Verificar se o assert
   desaparece (H3).

7. **Comparacao com Family Pack**: verificar se outro jogo que usa o mesmo motor TTD tambem faz
   `MALLOC` de ~23 MiB e se dispara o mesmo assert. Isso ajudaria a distinguir problema de
   emulacao de problema especifico do Zeeboids.

8. **Instrumentacao adicional (somente logs)**: registrar o endereco/tamanho exatos do pool quando
   o jogo chamar `MALLOC` e, se possivel, o primeiro endereco de assert. Nao modificar logica.

## 8. Proximos passos sugeridos

1. Rodar o experimento 1 (stub de IThread) e o experimento 2 (heap maior) primeiro.
2. Se o stub de thread remover o assert, focar em `xui/title.xui`, `xui/warning.xui` e no init
   de UI/XUI.
3. Se o assert permanecer, focar em alinhamento/padding do heap (experimentos 3 e 4) e na base
   do pool.
4. Registrar se `SignalCBFactory_CreateSignal` aparece apos qualquer experimento bem-sucedido.

## 9. Referencias

- Logs: `zb_fmt.log`, `zb_sig1.log`
- Codigo: `src/memory/memory.h`, `src/memory/memory.c`, `src/memory/heap.c`,
  `src/brew/helpers.c`, `src/brew/boot.c`, `src/loader/mod_loader.c`
- Docs: `docs/RE_INFUSE_NOTES.md`, `docs/PROGRESS.md`,
  `STATUS_C_BOOT_PACMANIA_ZEEBOIDS.md`
- CLSID do Zeeboids: `0x0108FF1A` (confirmado byte a byte no `.mif`)
- Thread: `pfn=0x0000B6CC`, `user=0x100014E8`, `stack=0x00080000`
- Assert: `TTDMM ASSERT`, `Error 62`, `source\TTDMemoryPool.cpp:1661`,
  impresso via `source\TTDMemoryManager.cpp:916`
