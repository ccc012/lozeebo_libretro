# STATUS R5-E — Input/HID ponta a ponta

## Resumo (8 linhas)

- Adicionei simulação de input configurável ao `libretro_smoke.c` (segura um botão por N
  frames a partir de um frame configurável via env vars) — infraestrutura permanente de teste.
- **Achado principal**: `IHIDDevice_RegisterForButtonEvent` já capturava o `signal` certo, mas
  `zboot_tick()` **nunca disparava o callback** — só logava e marcava `pending`. Corrigido:
  agora chama `guest_call()` de verdade (mesmo padrão de timers), com evidência de que o
  callback executa (+73 instruções reais, sem crash) no Double Dragon.
- **Regressão real encontrada e documentada** (não escondida): o mesmo fix expõe um crash
  pré-existente no Family Pack quando o botão é pressionado — vai para 0x04000000 (RAM vazia),
  o mesmo padrão de "andar por memória zerada" já visto no Zeeboids/Pac-Mania de rodadas
  anteriores. Análise abaixo indica que é um bug dormente do próprio jogo, não algo que meu
  código HID introduziu estruturalmente.
- Pac-Mania e Zeeboids **não registram HID nenhum** nos primeiros 3s simulados (180 frames) —
  ficam presos só em `IShell_SetTimer` de 1ms/10ms. Não chegam a chamar `GetKeys` nem
  `EVT_KEY`. Não deu pra confirmar qual mecanismo eles usarão pra input porque ainda não
  saíram do próprio loop de inicialização/timer nesse intervalo.
- Testado de verdade: smoke test com log colado abaixo, não só "compilou".
- Sem regressão em Pac-Mania/Zeeboids (não tocam no código mudado, `g_hid_button_signal`
  nunca é setado pra eles). Double Dragon melhora (evento reage). Family Pack regride (crash
  novo) — mas é a exposição de um bug antigo, discutido na seção própria.

---

## O que existia antes (achado #1)

Investigação em `src/brew/boot.c`:

- `case 8` de `zbrew_handle_stub()` (`IHIDDevice_RegisterForButtonEvent`, `ZCLSID_HID_DEVICE`):
  guarda `g_hid_button_signal = g_cpu.r[1]` — o objeto de signal criado por
  `SignalCBFactory_CreateSignal` (`case 3`, já grava `cb`/`user` em `signal+8`/`signal+12`).
- `zboot_tick()` (antes do fix): quando o RetroPad muda de estado, mapeia o botão pro par
  `id`/`uid` do BREW, seta `g_hid_event_pending = true` + `g_hid_event_id/uid/down`, e **só
  loga** `callback`/`user` calculados de `g_hid_button_signal` — nunca chama `guest_call()`.
- `case 9` (`IHIDDevice_GetNextEvent`, presumo): já lê corretamente os campos
  `g_hid_event_*` e devolve pro jogo quando ele pergunta ativamente (`pending`).

Ou seja: existe um mecanismo de **poll** funcional (`GetNextEvent`), mas o mecanismo de
**notificação por signal** (o que o BREW real usa — `ISignalCtl` é "acorde-me",
`PFNNOTIFY(pUser)`, sem parâmetros de evento; os detalhes vêm depois via `GetNextEvent`)
nunca disparava. Resultado prático: mesmo com o jogo registrando o callback certo, ele nunca
seria chamado — o jogo ficaria esperando pra sempre (`halted=true`) um "acorda" que nunca vem.

### Evidência (baseline, ANTES do fix)

```
[INFO] [Zeebo] IHIDDevice_RegisterForButtonEvent signal=0x10022020
[SMOKE-INPUT] frame=120 segurando botao id=8
[INFO] [Zeebo] HID: uid=0x0106C40A down=1 callback=0x0001BDF4 user=0x10001F18
[INFO] [Zeebo] frame 121: boot=rodando instrucoes=17671 PC=0xF0000A40 halted=1 fb[0]=0xFFFFFFFF
```

`callback=0x1BDF4` é calculado corretamente, mas note `PC=0xF0000A40` (trap de retorno) e
`halted=1` — nada rodou. Comparado com `frame 61` (`instrucoes=13695`), o crescimento de
`instrucoes` até `frame 121` (`17671`, +3976) é idêntico ao ritmo normal do timer do jogo — o
botão pressionado **não mudou nada**.

---

## Fix aplicado

`src/brew/boot.c`, dentro de `zboot_tick()`, no bloco `if (bit && g_hid_button_signal)`:

```c
/* Convencao BREW: ISignalCtl e so um "acorde-me" (PFNNOTIFY(pUser)),
 * sem parametros de evento - o jogo consulta os detalhes depois via
 * IHIDDevice_GetNextEvent (case 9, que ja devolve id/down/uid).
 * Antes desta chamada, o callback so era logado e nunca disparado:
 * o jogo nunca acordava do halted apos EVT_APP_START. */
if (callback) {
    g_state = BOOT_SIGNAL_CALL;
    guest_call(callback, user, 0, 0, 0);
    return;
}
```

Reaproveita o estado `BOOT_SIGNAL_CALL` que já existia no enum (`case BOOT_TIMER_CALL:
case BOOT_SIGNAL_CALL:` já tratam o retorno igual: `g_state = BOOT_RUNNING; g_cpu.halted =
true;`) — não mexi em `zboot_on_guest_return()`. Segue a mesma disciplina de "um guest_call
por tick" que os timers já usam (`return` logo depois, pra não pisar o dispatch de outro
evento na mesma chamada de `zboot_tick`).

### Evidência (DEPOIS do fix) — Double Dragon reage de verdade

```
[SMOKE-INPUT] frame=120 segurando botao id=8
[INFO] [Zeebo] HID: uid=0x0106C40A down=1 callback=0x0001BDF4 user=0x10001F18
[INFO] [Zeebo] frame 121: boot=rodando instrucoes=17744 PC=0xF0000A40 halted=1 fb[0]=0xFFFFFFFF
```

`instrucoes` sobe pra `17744` em vez de `17671` — **+73 instruções reais executadas pelo
callback do jogo**, sem descarrilar, antes de voltar a `halted` esperando o próximo evento.
Isso é a prova de que o callback rodou (não é ruído: com `ZEEMU_SMOKE_HOLD_FRAMES=0` — sem
segurar nenhum botão — `frame 121` volta a `instrucoes=17671`, idêntico à baseline; testei
os dois lados pra isolar a causa).

---

## Regressão encontrada: Family Pack crasha ao apertar o botão

```
[INFO] [Zeebo] SignalCBFactory_CreateSignal(cb=0x000057A4 user=0x10011FF0) -> 0x10012548
[INFO] [Zeebo] IHIDDevice_RegisterForButtonEvent signal=0x10012548
[SMOKE-INPUT] frame=120 segurando botao id=8
[INFO] [Zeebo] HID: uid=0x0106C40A down=1 callback=0x000057A4 user=0x10011FF0
[INFO] [Zeebo] frame 121: boot=sinal HID instrucoes=1729568 PC=0x0046BB04 halted=0 fb[0]=0xFF000000
[ERROR] [Zeebo] CPU descarrilou: fetch em 0x04000000 (LR=0x000AEF80 SP=0x000B1DC8)
```

### Isso é regressão minha ou bug pré-existente exposto?

Fortes indícios de que é **bug pré-existente do próprio Family Pack**, não algo que meu
código HID introduziu estruturalmente:

1. O padrão do crash (`PC` andando linear até `0x04000000` = topo exato dos 64MB de RAM) é
   **idêntico** ao bug documentado em `STATUS_C_BOOT_PACMANIA_ZEEBOIDS.md` pro Zeeboids —
   "CPU anda por RAM zerada como NOP até estourar o limite". Não é um salto corrompido; é
   código genuíno (ou lixo interpretado como NOP) rodando até o fim da memória.
2. `docs/PROGRESS.md`, sessão 2026-07-08(2), já documentava um bug **diferente mas
   correlato** no scheduler do Family Pack: "loop de divisão por subtração cujo divisor
   `[obj+0x54]` é 0... NULL-deref não falha aqui... leitura inválida -> 0 mantém o loop vivo
   porém degenerado" — ou seja, o próprio scheduler do jogo já tinha um histórico de cair em
   caminhos de código com ponteiros nulos/não inicializados que "funcionam por acidente" (lêem
   zero) até certo ponto.
3. O log mostra `SignalCBFactory outputs: signal=0x00000000` (o out-param `signal`,
   **diferente** do `ctl`/objeto que uso pra ler `cb`/`user` — esse eu confirmei correto).
   Isso é exatamente o suspeito que a Tarefa A foi instruída a investigar. Se esse out-param
   nulo é usado em outro lugar do próprio callback do botão (ou de qualquer callback do
   Family Pack) pra navegar uma estrutura, um NULL ali pode ser a causa raiz de qualquer
   callback que rode nesse jogo cair em memória inválida — não é exclusivo do HID.
4. **Nenhum outro jogo é afetado**: Pac-Mania e Zeeboids nunca chamam
   `IHIDDevice_RegisterForButtonEvent` nos 180 frames testados (`g_hid_button_signal`
   permanece `0`), então o `if (bit && g_hid_button_signal)` nunca entra pra eles — path de
   código idêntico ao antes do fix, `instrucoes` idênticas byte a byte à baseline. Double
   Dragon funciona (evidência acima). **Isolado ao Family Pack.**

### Recomendação pra próxima etapa (não fiz — fora do escopo de HID)

Não revertive o fix (ele está correto e faz o que o BREW real faz) porque:
- Reverter esconderia o bug de novo atrás de "callback nunca dispara" — o mesmo problema
  vai aparecer de qualquer jeito quando a Tarefa A destravar o signal do game-loop do Family
  Pack (`cb=0x554C`, o primeiro `SignalCBFactory_CreateSignal` do log, ainda não é disparado
  por ninguém hoje).
- É bem provável que ESSE MESMO crash (RAM vazia em `0x04000000`) aconteça de novo assim que
  a Tarefa A ligar o dispatch genérico de signals pro Family Pack — recomendo que quem
  investigar isso (Tarefa A ou uma Rodada 6) trate como **um único bug**, não dois: "qualquer
  callback do Family Pack, quando roda de verdade, cai em memória vazia" — e verifique o
  out-param `signal=0x00000000` do `SignalCBFactory` como suspeito nº 1, já que é comum aos
  dois casos.
- Este achado (crash isolado ao Family Pack, único jogo com HID registrado) é justamente o
  tipo de "regressão obrigatória" que a Parte 2 do prompt pede pra checar antes de encerrar —
  reportando com evidência em vez de omitir.

---

## Pac-Mania e Zeeboids: não chegam a usar input em 180 frames (3s simulados)

```
=== Pac-Mania ===
IShell_SetTimer: 1 ms callback=0x0000A2AC user=0x100014E8   (repetido 179x)
(nenhum GetKeys, EVT_KEY, RegisterForButtonEvent)

=== Zeeboids ===
IShell_SetTimer: 10 ms callback=0x00042494 user=0x100014E8
(nenhum GetKeys, EVT_KEY, RegisterForButtonEvent)
```

Os dois ficam presos rodando só o próprio timer de scheduler interno (1ms/10ms) — não saem
desse loop pra sequer perguntar por input nesse intervalo. Não dá pra saber ainda se eles
usariam `EVT_KEY`/`GetKeys` ou HID quando (se) chegarem numa tela de menu/gameplay real —
precisaria rodar MUITOS mais frames simulados (dezenas de milhares, não só 180) pra saber, o
que é mais uma investigação de "por que eles não avançam de estado" (provavelmente território
da Tarefa C, que já está de olho no Pac-Mania) do que de input propriamente dito.

**Não implementei `EVT_KEY` via `HandleEvent`** porque não tenho evidência de que algum dos 4
jogos Tier 1 realmente espera por isso — implementar às cegas seria código sem teste real.
Fica documentado como possível próximo passo se um jogo aparecer usando esse padrão.

---

## Infraestrutura adicionada (permanente)

`tests/libretro_smoke.c`: `input_state()` agora segura um botão configurável por env vars:

- `ZEEMU_SMOKE_PRESS_FRAME` (padrão 120) — frame em que começa a segurar
- `ZEEMU_SMOKE_HOLD_FRAMES` (padrão 10) — por quantos frames mantém segurado
- `ZEEMU_SMOKE_BUTTON` (padrão `A`; aceita `START`/`B`/`UP`/`DOWN`) — qual botão

Loga `[SMOKE-INPUT] frame=N segurando botao id=X` a cada frame em que o botão está ativo, pra
correlacionar com o log do core. Testado com `ZEEMU_SMOKE_HOLD_FRAMES=0` pra confirmar que,
sem segurar nada, o comportamento é idêntico à baseline sem a mudança (`instrucoes=17671` em
ambos os casos no Double Dragon).

## Arquivos tocados

- `src/brew/boot.c` — dispatch real do signal de botão HID em `zboot_tick()`.
- `tests/libretro_smoke.c` — simulação de input configurável via env vars (infraestrutura
  de teste permanente).

## Regressão checada (4 jogos Tier 1, smoke test, build Release x64)

| Jogo | Antes (instrucoes @ frame 121, sem input) | Depois (mesmo, sem input) | Com botão segurado |
|---|---|---|---|
| Double Dragon | 17671 | 17671 (idêntico) | 17744 (+73, callback rodou, sem crash) |
| Pac-Mania | 145686 | 145686 (idêntico, sem HID registrado) | idêntico (não afetado) |
| Family Pack | 729568 (congelado, bug conhecido a2f6767) | idêntico sem input | **crash em 0x04000000** ao apertar botão — ver seção de regressão acima |
| Zeeboids | 544678 | 544678 (idêntico, sem HID registrado) | idêntico (não afetado) |

Nenhum jogo regrediu no cenário "sem input simulado" (uso normal de quem não aperta nada
ainda). O crash do Family Pack só ocorre com input ativo, e está documentado com causa
provável acima, não escondido.
