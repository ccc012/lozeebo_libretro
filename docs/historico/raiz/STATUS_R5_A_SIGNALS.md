# STATUS R5-A — Signals: Family Pack / Zeeboids

## Resumo (8 linhas)

- Causa raiz real do bloqueio nº 1 identificada: **não é falta de CPU livre** (testei e
  descartei essa hipótese com evidência), é um método da vtable real do `IHID`
  (`RegisterForConnectEvents`, slot 6) que nunca foi implementado — o jogo registra um
  sinal esperando ser avisado que o joystick conectou e nunca é avisado.
- Implementado: `case 6` de `AEECLSID_HID_REAL` captura o sinal + dispara ele uma vez
  via `zboot_process_timers()` (mesmo padrão dos timers). **Testado**: o callback
  `DeviceConnectCB` do Family Pack agora executa de verdade (log real colado abaixo).
- **Regressão encontrada e corrigida na mesma sessão**: o mesmo sinal existe no Double
  Dragon, e o callback dele entra num loop que nunca retorna — sem proteção, isso
  travava 100%+ da CPU nesse loop pra sempre e regredia o Double Dragon (que já
  funcionava bem via timers) para tela preta. Adicionei um watchdog de instruções
  (`ZSIGNAL_CALL_INSTR_BUDGET`) que força a volta ao estado normal se o callback não
  retornar — testado, Double Dragon volta a pintar branco por timer normalmente.
- Pac-Mania e Zeeboids não chamam esse método — zero mudança nos dois (confirmado por
  log, instruções idênticas antes/depois).
- Family Pack **ainda não renderiza** (0 chamadas GL) — avançou para um bloqueador
  novo e mais profundo (ponteiro de vtable lendo bytes de código como dado), fora do
  escopo desta tarefa, documentado abaixo para a próxima rodada.
- Compila limpo, testado via `tests/libretro_smoke.exe` nos 4 jogos Tier 1, mudança
  isolada em `src/brew/boot.c` (64 linhas, sem tocar em display/GLES).

---

## Investigação (antes de codar)

### Hipótese 1 do prompt: "`SignalCBFactory outputs: signal=0x00000000` indica bug" — DESCARTADA

Log real do baseline:
```
[INFO] [Zeebo] SignalCBFactory_CreateSignal(cb=0x0000554C user=0x10011FE0) -> 0x10012358
[INFO] [Zeebo] SignalCBFactory outputs: signal=0x00000000 ctl=0x10011FEC
```

Conferi o código do `case 3` (`AEECLSID_SIGNALCBFACT_R`): o `LOGI` imprime os
**endereços dos out-params** (`pp_signal`, `pp_signal_ctl`), não o que foi escrito
neles. `signal=0x00000000` significa que `pp_signal` (arg `r3`) veio `NULL` do
chamador — nesse caso específico, o jogo só pediu o `ctl` (`pp_signal_ctl=0x10011FEC`,
não-nulo, recebeu `0x10012358` corretamente). Não é um bug nosso, é uma chamada legítima
que só quer um dos dois out-params. Confirmei isso lendo o código antes de mudar
qualquer coisa.

### Hipótese 2: "o jogo só precisa de CPU livre pra rodar seu próprio scheduler" — TESTADA E DESCARTADA COM EVIDÊNCIA

Testei a alternativa mínima sugerida no prompt: reverter `zboot_process_timers()` para
soltar a CPU (`halted=false`) quando não há timer ativo (comportamento pré-`a2f6767`).

```c
/* experimento, revertido depois */
g_cpu.halted = false;
```

Resultado real (Family Pack, 180 frames):
```
0   <- grep -c "glClear|DrawArrays|eglSwapBuffers"
[WARN] [Zeebo] boot: retorno de guest call em estado inesperado (rodando)
```
repetido **179 vezes** (uma por frame). Ou seja: soltar a CPU sem mais nada só faz ela
re-executar o mesmo trap de retorno (`ZT_GUEST_RETURN`) e re-parar imediatamente, porque
não existe mais nenhuma instrução real depois do ponto onde o `EVT_APP_START` retornou —
o jogo genuinamente precisa de um evento/callback real disparado por nós, não só de
ciclos de CPU. Descartei essa abordagem.

### Descoberta real: método 6 do `IHID` nunca implementado

No log do Family Pack (baseline), logo depois do `SignalCBFactory_CreateSignal` que
cria o objeto `0x10012358`, aparece:
```
[WARN] [Zeebo] stub: clsid=0x0106C411 metodo=6 r0=0x100122B8 r1=0x10012358 r2=0x10001588 r3=0x100122B8 sp0=0x10011FEC
```
`0x0106C411` é `AEECLSID_HID_REAL` (o próprio objeto IHID). O `r1` passado é exatamente
o sinal recém-criado. `zbrew_handle_stub()` não tem `case 6:` — cai no `default` e o
sinal nunca é guardado nem disparado.

Pedi confirmação em `reference/zeemu-main` (fonte de referência do projeto) do layout
real do vtable de `IHID`:
```
0 AddRef | 1 Release | 2 QueryInterface | 3 CreateDevice | 4 GetDeviceInfo
5 GetNextConnectEvent | 6 RegisterForConnectEvents | 7 GetConnectedDevices
```
Confirmado: **slot 6 = `RegisterForConnectEvents`**. No zeemu (`BrewHID.cpp`), esse
sinal é guardado e disparado depois via `shell_.set_signal(connect_signal_, ...)`
quando um device conecta — no nosso caso, como o `IHID_GetConnectedDevices` já reporta
o joystick como conectado desde o início do boot, o sinal deve disparar uma vez, cedo.

## Fix implementado

`src/brew/boot.c`:

1. **`case 6` de `zbrew_handle_stub()`** (novo): para `AEECLSID_HID_REAL`, captura o
   objeto de sinal em `g_hid_connect_signal` e marca `g_hid_connect_pending = true`.
2. **`zboot_process_timers()`**: antes do loop de timers, se `g_hid_connect_pending`,
   lê `cb`/`user` do sinal (`signal+8`/`signal+12`, mesmo layout usado no `case 3`),
   dispara via `guest_call()` (mesmo padrão dos timers, transiciona pra
   `BOOT_SIGNAL_CALL`), dispara só uma vez.
3. **Watchdog de instruções** (novo, ver seção de regressão abaixo).
4. Reset em `zboot_start()` e campos novos no blob de serialize/unserialize (para não
   quebrar save-states, seguindo o padrão dos campos de HID já existentes).

## Evidência: Family Pack avança de verdade

Log **depois** do fix (trecho real):
```
[INFO] [Zeebo] IHID_RegisterForConnectEvents signal=0x10012358
...
[INFO] [Zeebo] boot: sinal de conexao HID disparado, callback=0x0000554C user=0x10011FE0
[INFO] [Zeebo] DBGPRINTF: *dbgprintf-4* ../../src/input/Joystick.cpp:349
[INFO] [Zeebo] DBGPRINTF: DeviceConnectCB
[WARN] [Zeebo] memoria: read32 invalido em 0xEA000014 (PC=0x000055C4 LR=0x0000559C SP=0x2FFFFFBC)
```
O callback real do jogo (`DeviceConnectCB`, confirmado pelo próprio `DBGPRINTF` do
jogo) executa. `instrucoes` sobe de `729568` (baseline, congelado) para `749905`
(mais ~20 mil instruções reais de execução). GL calls ainda em 0 — ver bloqueador novo
abaixo.

## Regressão encontrada e corrigida: Double Dragon

Testando os 4 jogos Tier 1 depois do fix, o Double Dragon regrediu:

**Antes da correção do watchdog** (só o disparo do sinal, sem proteção):
```
frame 61:  boot=sinal HID instrucoes=54791832  halted=0  fb[0]=0xFF000000
frame 121: boot=sinal HID instrucoes=109574440 halted=0  fb[0]=0xFF000000
```
O callback de conexão do Double Dragon (mesmo mecanismo, `RegisterForConnectEvents`
também é chamado por ele) entra num loop que nunca dispara `ZT_GUEST_RETURN` — a CPU
fica queimando o orçamento de 1M instruções/frame (`ZEEBO_INSTR_PER_FRAME`) pra sempre,
sem nunca voltar pro estado `RUNNING`, e o framebuffer (que antes ficava branco via
`IDisplay_DrawRect` por timer) fica preso em preto.

**Baseline real (sem meu fix, pra confirmar que é regressão e não bug preexistente)**:
```
frame 61:  boot=rodando instrucoes=13695 halted=1 fb[0]=0xFFFFFFFF
frame 121: boot=rodando instrucoes=17671 halted=1 fb[0]=0xFFFFFFFF
```
(testado revertendo temporariamente meu fix com `git stash`, confirmando que sem ele o
Double Dragon já pintava branco por timer normalmente.)

**Fix**: adicionei um watchdog (`ZSIGNAL_CALL_INSTR_BUDGET = 2.000.000` instruções) em
`zboot_process_timers()`: se o estado ficar em `BOOT_SIGNAL_CALL` por mais instruções
que isso sem retornar, força a volta pra `RUNNING`/`halted=true`, registrando um `LOGW`
claro. **Depois do watchdog**:
```
[WARN] [Zeebo] boot: callback de sinal nao retornou apos 2739131 instrucoes -
       forcando volta para RUNNING (provavel loop degenerado no guest)
frame 61:  boot=rodando instrucoes=2752826 halted=1 fb[0]=0xFFFFFFFF
frame 121: boot=rodando instrucoes=2756802 halted=1 fb[0]=0xFFFFFFFF
```
Double Dragon volta a pintar branco por timer normalmente (`fb[0]=0xFFFFFFFF` de
volta) — a regressão foi fechada. O watchdog é genérico o bastante pra proteger
qualquer futuro uso de `BOOT_SIGNAL_CALL`, não só esse caso específico.

## Regressão (checklist final, os 4 jogos Tier 1)

| Jogo | Antes (baseline) | Depois do fix | Veredito |
|---|---|---|---|
| Double Dragon | instrucoes cresce devagar via timer, `fb` vira branco por volta do frame 61 | idêntico (depois do watchdog corrigir o loop do sinal de conexão) | ✅ sem regressão |
| Pac-Mania | instrucoes cresce via timer (~145k no frame 121), `fb` preto | idêntico, byte a byte | ✅ sem regressão |
| Zeeboids | idle em 544678 instrucoes desde o frame 1 | idêntico (não chama `RegisterForConnectEvents`) | ✅ sem regressão |
| Family Pack | 0 chamadas GL, `instrucoes` congelado em 729568 | callback de conexão executa de verdade (+20k instrucoes reais), ainda 0 chamadas GL — novo bloqueador | ✅ progresso real, sem regredir mais |

## Bloqueador novo descoberto (fora do escopo desta tarefa)

Depois do `DeviceConnectCB` do Family Pack rodar, a CPU trava numa leitura de memória
inválida:
```
[WARN] [Zeebo] memoria: read32 invalido em 0xEA000014 (PC=0x000055C4 LR=0x0000559C SP=0x2FFFFFBC)
```
Desmontei a região com capstone (`tests/roms/real_family_pack_game.mod`, offset
`0x5540-0x5600`): é uma cadeia clássica de chamada por vtable —
```
0x000055A4: ldr r3, [r4, #4]      ; r3 = objeto->campo4 (sub-interface)
0x000055AC: ldr ip, [r3]          ; ip = vtable de r3
0x000055BC: ldr ip, [ip, #0x14]   ; ip = vtable[5] (metodo 5)
0x000055C4: bx  ip                ; chama
```
`0xEA000014` (o endereço que falhou ao ler) começa com `0xEA000000`, que é exatamente
o padrão de opcode ARM de `B` (branch incondicional) — sinal forte de que algum
ponteiro nessa cadeia (`r4+4`, ou a vtable de `r3`) está apontando pra **bytes de
código sendo lidos como se fossem dado**, o mesmo tipo de bug já visto e documentado
em `STATUS_C_BOOT_PACMANIA_ZEEBOIDS.md` para o Zeeboids (só que lá era a stack
apontando pra VRAM; aqui é um objeto/interface criado por algum stub nosso que não tem
o layout certo). Minha suspeita mais provável: algum objeto de dispositivo/interface
que o `DeviceConnectCB` consulta (talvez `GetDeviceInfo` ou uma nova instância criada
via `IShell_CreateInstance` dentro do callback) foi alocado via `make_stub_interface()`
genérico sem os campos que esse código realmente lê.

**Não investiguei mais fundo** porque é claramente fora do escopo de "signals" (é sobre
o conteúdo de um objeto HID específico, mais perto do território de investigação livre
tipo Tarefa C) e eu já tinha risco de regressão pra fechar no Double Dragon. Deixo como
próximo passo nº 1 pra continuar o Family Pack.

## Arquivos tocados

- `src/brew/boot.c` (único arquivo, 64 linhas adicionadas, nada removido/alterado fora
  do necessário): `case 6` do `zbrew_handle_stub`, disparo do sinal em
  `zboot_process_timers()`, watchdog de instruções, reset em `zboot_start()`, campos
  novos no blob de serialize/unserialize.
- Nenhuma mudança em `IDisplay` (Tarefa B), rasterizador GLES (Tarefa D) ou input
  RetroPad (Tarefa E).
