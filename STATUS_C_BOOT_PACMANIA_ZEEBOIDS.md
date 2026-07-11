# STATUS C — Boot travado: Pac-Mania e Zeeboids

## Resumo (8 linhas)

- **Zeeboids**: CLSID real confirmado (`0x0108FF1A`, bytes literais no `.mif`). Com o fix já
  pendente em `mif_parser.c` + `aee_ids.h`, o jogo passa de "trava dentro do próprio
  `AEEMod_Load`" para rodar **milhões de instruções reais** antes de descarrilar. Causa raiz
  identificada com precisão de registrador: o stub de relocação PIC (ROPI/RWPI) do `armcc`
  calcula `r1=0` porque a tabela de scatter-load que ele espera encontrar não está onde nosso
  loader assume. Ainda não corrigido — precisa entender o layout real do `.mod`.
- **Pac-Mania**: fix do `case 5` (já commitado antes desta rodada) **não resolve** o
  descarrilamento. Causa raiz agora identificada com matemática de stack exata: `CreateInstance`
  é chamado direto num endereço (`0x10DC`) que é meio de função, sem prólogo — o epílogo
  compartilhado estoura o SP em exatos `0x58` bytes, direto para dentro da VRAM.
  Não corrigido — precisa entender a convenção de chamada real do binário.
- Nenhuma mudança de código de fix foi commitada nesta branch além do CLSID do Zeeboids
  (`mif_parser.c` + `aee_ids.h`, já testado e funcionando).
- **Aviso de infraestrutura**: as 3 IAs desta rodada compartilham o MESMO working directory
  físico (não são clones/worktrees separados). Durante esta sessão, outro agente trocou de
  branch (`git checkout -b rodada2/family-pack-render`) enquanto eu tinha
  `rodada2/pacmania-zeeboids-boot` como HEAD — isso trocou o branch ativo silenciosamente sob
  meus pés. Recomendo fortemente que a próxima rodada use `git worktree add` por pessoa, não
  apenas branches, para isolar de verdade.

---

## PAC-MANIA

### Diff aplicado

Nenhum na branch final (a mudança do `case 5` já estava commitada em `master` antes desta
rodada, ver comentário em `boot.c:930-962`). Toda a investigação abaixo foi feita com
instrumentação temporária (logs `DEBUG`/`WATCH`) que foi **revertida** antes deste relatório —
não sobrou no working tree.

### Log real do smoke test (trecho relevante)

```
[INFO] [Zeebo] boot: IModule_CreateInstance(clsid=0x01087B72) via 0x000010DC
[INFO] [Zeebo] stub obj=0x100014E8 vtbl=0x10001538 vtbl[0]=0xF0000C00 clsid=0x0100101C
[INFO] [Zeebo] IShell_CreateInstance(0x0100101C) -> 0x100014E8
[INFO] [Zeebo] stub obj=0x10001648 vtbl=0x10001538 vtbl[0]=0xF0000C00 clsid=0x0100101C
[INFO] [Zeebo] stub obj=0x10001698 vtbl=0x10001538 vtbl[0]=0xF0000C00 clsid=0x0100101C
[INFO] [Zeebo] CreateInstance case 5 (0x0100101C) OK -> obj1=0x10001648 obj2=0x10001698
[ERROR] [Zeebo] CPU descarrilou: fetch em 0xFF000000 (LR=0x0000115C SP=0x30000048)
[INFO] [Zeebo] trace: ultimos 64 PCs (mais recente por ultimo):
  ... 0x0000114C 0x00001150 0x00001154 0x00001158 0x0000178C 0x00001790
      0x00001794 0xF0000C04 0x0000115C 0x00001160 0x00001164 0x00000EB4 0x00000EB8 0xFF000000
```

### Causa raiz (confirmada por matemática de stack, não suposição)

`SP` no crash = `0x30000048`. `VRAM_BASE` = `0x30000000` (`memory.h`). Ou seja o SP passou a
apontar **48 bytes dentro da VRAM**, e como a VRAM tinha acabado de ser limpa para
`0xFF000000` (cor de clear), o `pop {..,pc}` final leu esse valor como se fosse o endereço de
retorno — daí o "fetch em 0xFF000000".

Reconstruindo o cálculo do epílogo (desmontagem via capstone do `pacmania.mod`, endereço de
arquivo = endereço virtual direto, confirmado pelo próprio log `entry=0x00000040`):

```
0x00000EB4: add  sp, sp, #0x34        ; libera 0x34 (52) bytes de locais
0x00000EB8: pop  {r4,r5,r6,r7,r8,sb,sl,fp,pc}  ; 9 regs = 0x24 (36) bytes
```

Total reclamado: `0x34 + 0x24 = 0x58` bytes. E:

```
0x30000048 - 0x58 = 0x2FFFFFF0
```

que é **exatamente** o SP inicial de `cpu_reset` (`cpu_reset: PC=0x00000040 SP=0x2FFFFFF0
LR=0xF0000000 ARM`). Ou seja: **o SP nunca se moveu durante toda a chamada de
`CreateInstance`** — não existe nenhum `push`/`sub sp` correspondente antes desse epílogo.

Isso significa que `IModule_CreateInstance` está sendo invocado (via `boot.c`:
`create = zmem_read32(vtbl + 8)`, resultando em `0x10DC`) num endereço que é **meio de
função**, sem prólogo próprio — e a função, ao terminar, executa um epílogo desenhado para uma
função que *teria* empurrado 9 registradores (0x58 bytes), que nunca foram empurrados nesta
invocação.

### Evidência adicional: nenhum dos 4 slots da vtable do módulo aponta para um prólogo real

Dump ao vivo da vtable do `AEEMod` (instrumentação temporária, já removida):

```
DEBUG vtbl slots: +0=0x000010C8 +4=0x00001108 +8=0x000010DC +0xC=0x00001104
                   +0x10=0x00000000 +0x14=0x5A484150(=ASCII "PAHZ", já é dado, não vtable)
```

Ou seja a vtable tem só 4 entradas úteis (`AddRef`, `Release`, `CreateInstance`,
`FreeResources`, presumivelmente). Desmontando os 4 endereços:

- `+0` (`0x10C8`): `ldr r0,[pc,#0x1e4]; b #0xeb4` — 2 instruções, salta pro mesmo epílogo
  compartilhado. Não tem prólogo.
- `+4` (`0x1108`): cai em cima de um `bne #0x1114` — **instrução condicional dependente de
  flags que ninguém setou antes**, o que só faz sentido se alcançada por fallthrough de
  `0x1104` (`cmp r0,#0`), não como entrada externa de função.
- `+0xC` (`0x1104`): é literalmente 4 bytes antes de `+4` (`0x1108`) — as duas "entradas da
  vtable" caem na MESMA sequência linear de instruções, 4 bytes uma da outra. Isso não é
  compatível com serem duas funções C++ distintas.
- `+8` (`0x10DC`, a que `boot.c` usa como `CreateInstance`): meio de um loop grande
  (`cmp fp,r0 / bge #0x121c`) que varre um array — plausível para "procurar CLSID numa
  tabela", mas **sem `push` nem `sub sp` correspondente em nenhum lugar do arquivo entre
  `0x50` (fim do prólogo do `AEEMod_Load`) e `0x1440`** (busquei push/pop/stmdb/stmfd em todo
  esse intervalo e não achei nenhum além do próprio `AEEMod_Load`).

### O que ainda falta

Não cheguei a uma correção segura. Duas hipóteses concretas para a próxima pessoa investigar,
em ordem de probabilidade:

1. **Convenção de chamada diferente da assumida por `boot.c`**: o comentário no topo de
   `boot.c` diz "IModule vtable+8 = CreateInstance" como fato confirmado (linha 5), mas o
   padrão de código em `0x10DC` (busca em array sem inicializar frame) e o fato de `+4`/`+0xC`
   caírem na mesma sequência sugerem que essas 4 "vtable slots" podem não ser 4 métodos
   simples — pode ser um único dispatcher fatiado pelo compilador (RVCT/armcc) com
   "identical code folding" agressivo, e a convenção de chamada real pode exigir que o
   *chamador* já tenha um frame de pilha montado (os 9 registradores) antes de saltar para
   dentro, o que só aconteceria organicamente se a chamada viesse de dentro de outra função
   do próprio módulo, não de fora via `guest_call()`.
2. **Mitigação defensiva em vez de correção real**: dar um "colchão" de `0x58` bytes de folga
   entre `cpu_reset`'s SP inicial e o topo da região de stack, para qualquer overrun deste tipo
   aterrissar em stack real (não em VRAM) — não resolve a causa, mas evita o descarrilamento
   duro e permite ver até onde o jogo consegue ir com estado parcialmente incorreto. Não
   tentei isso porque pode mascarar bugs reais (é exatamente o tipo de coisa que o comentário
   em `cpu.c:73-79` diz que já causou confusão numa sessão anterior).

Ferramentas usadas: `capstone` (Python) para desmontagem ARM, instrumentação temporária em
`zcpu_step()`/`zboot_on_guest_return()` com `LOGE` (revertida).

---

## ZEEBOIDS

### CLSID real encontrado

`0x0108FF1A`. Já estava como suposição em `FIXES_TO_IMPLEMENT.md` de uma sessão anterior, mas
**agora verificado byte a byte**: os bytes `1A FF 08 01` (little-endian de `0x0108FF1A`)
aparecem literalmente no arquivo
`.../mif/279382.mif`, no offset `8116` (arquivo tem 8156 bytes — está perto do fim).

```python
target = bytes([0x1A,0xFF,0x08,0x01])
data.find(target)  # -> 8116
```

O fix pendente em `src/loader/mif_parser.c` (`clsid_from_path_hint`, adiciona `"Zeeboids"` ->
`0x0108FF1Au`) e em `src/brew/aee_ids.h` (adiciona `{ 0x0108FF1Au, "Zeeboids" }` à tabela de
applets conhecidos) está **correto** e eu o mantive nesta branch.

### Log real do smoke test (com o CLSID correto)

```
[INFO] [Zeebo] mif: clsid inferido por nome de caminho 0x0108FF1A em ...zeeboids.mod
[INFO] [Zeebo] mod1: hdr=0x0 code=755536 data=4336 bss=326140 entry=0x00000040
[INFO] [Zeebo] cpu_reset: PC=0x00000040 SP=0x2FFFFFF0 LR=0xF0000000 ARM
[INFO] [Zeebo] boot: AEEMod_Load(shell=0x10001270, ph=0, ppMod=0x10001480) entry=0x00000040
[INFO] [Zeebo] frame 1: boot=AEEMod_Load instrucoes=1000000 PC=0x0048EE74 halted=0 fb[0]=0xFF000000
[ERROR] [Zeebo] CPU descarrilou: fetch em 0x04000000 (LR=0x000000CC SP=0x2FFFFFDC)
[INFO] [Zeebo] trace: ultimos 64 PCs (mais recente por ultimo):
  0x03FFFF04 0x03FFFF08 0x03FFFF0C ... 0x03FFFFFC 0x04000000   (incrementando de 4 em 4)
```

**Antes** deste fix, o log da rodada anterior mostrava travamento imediato dentro do próprio
`AEEMod_Load` sem sequer o CLSID ser resolvido. **Agora** o jogo executa mais de 1 milhão de
instruções reais (chega a `PC=0x0048EE74`, bem além do próprio módulo carregado) antes de
descarrilar batendo no teto de RAM (`0x04000000` = exatamente `ZMEM_RAM_SIZE`, 64MB).

### Causa raiz (confirmada por instrumentação, não suposição)

O PC não faz um salto corrompido para `0x04000000` — ele **incrementa de 4 em 4 bytes**
continuamente (`0x03FFFF04 -> 0x03FFFF08 -> ... -> 0x04000000`), o que é o padrão de executar
`ANDEQ r0,r0,r0` (bytes zero decodificados como instrução ARM) repetidamente, ou seja: a CPU
está "andando" por RAM não inicializada/zerada como se fossem NOPs, até estourar o limite de
64MB.

Com um watchpoint temporário (`pc >= 0x00200000`, primeira ocorrência), encontrei o ponto exato
onde a CPU sai da área real do módulo (~1MB de código+dados) rumo ao vazio:

```
WATCH: primeira fuga para PC=0x00200000 (LR=0x000000CC SP=0x2FFFFFDC
        R0=0x000B9A40 R1=0x00000200 R4=0x00000200)
```

`LR=0xCC` aponta para dentro do próprio prólogo de `AEEMod_Load` (`0x40-0x188`), que é
**idêntico byte a byte ao stub de relocação PIC do Pac-Mania** nos mesmos offsets (ambos usam
`push {r0,r1,r2,r4,lr}` em `0x40`/`0x48` — é o boilerplate padrão de startup ROPI/RWPI gerado
pelo `armcc`/RVCT). Desmontagem do trecho relevante:

```
0x0000005C: add  r0, pc, #0x100     ; r0 = "self" (endereco PIC deste ponto no binario)
0x00000060: add  r0, r0, #0xA0
0x00000064: ldr  r1, [r0]           ; r1 = delta armazenado no binario
0x00000068: add  r1, r1, r0         ; r1 = delta + self  (deveria ser o endereco real da tabela)
    ...
0x000000B8: ldr  ip, [r1, #0x28]    ; le campo da "tabela" apontada por r1
0x000000BC: add  ip, ip, r0
0x000000C8: bl   #0x188             ; trampolim -> bx ip  (LR fica = 0xCC)
0x00000188: bx   ip
```

Watchpoints capturaram os valores reais:

```
WATCH 0x64: r0(self)=0x0000018C  [r0]=0xFFFFFE74  esperado_delta=-r0=0xFFFFFE74   <- BATE EXATO
WATCH 0xB8: r0=0x000B9A40 r1=0x00000000 [r1+0x28]=0x00004BB0 r2=0x000B8950 r3=0x00000200 r4=0x00000200
```

`0xFFFFFE74` é exatamente `-0x18C` (complemento de dois). Ou seja o binário armazena um delta
que, somado ao `self` calculado via PC-relative, dá **exatamente zero** — `r1` vira
`0x00000000` **por design do compilador**, não por bug de leitura de memória nossa.

Isso só faz sentido se, no binário real (não emulado), a "tabela" de regiões de relocação
(`Region$$Table`, um padrão clássico do scatter-loader `armcc`/`__scatterload_rt2`) estivesse
posicionada no endereço absoluto `0x00000000` do espaço de memória do módulo — um endereço que,
no NOSSO loader (`mod_loader.c`), é onde colocamos o **próprio código/vetor de entrada** do
módulo (assumimos `VA == offset do arquivo`, começando em 0). Como resultado, o código lê os
bytes do vetor de entrada/código do próprio módulo como se fossem campos de uma struct de
relocação (`[r1+0x15]`, `[r1+0x17]`, `[r1+0x18]`, `[r1+0x28]`, etc.), pega lixo, e calcula um
`ip` sem sentido (`0x00004BB0 + 0x000B9A40` na leitura direta, mas o valor real observado no
salto foi `0x00200000` — a aritmética completa do trecho tem mais somas depois de `0xB8` que
não recalculei manualmente com precisão de bit, mas o ponto crítico — `r1==0` sendo usado como
base de uma struct que não é a struct real — já está 100% confirmado pelos dois watchpoints).

### Por que o Pac-Mania (código idêntico no mesmo offset) não sofre disso

Pac-Mania é 5x menor (146KB de código vs 755KB) e tem `data=16 bytes, bss=484 bytes` (quase
nada), contra `data=4336, bss=326140` do Zeeboids. É plausível que o Pac-Mania nunca *precise*
de relocação real (nenhuma seção RW/ZI significativa para inicializar), então mesmo que o
mesmo cálculo dê um `r1` "errado", os branches condicionais em `0x78`/`0x84`/`0x8C` (que fazem
early-exit se certas flags não baterem) por acaso saem cedo o bastante para nunca chegar no
`bx ip` problemático — enquanto o Zeeboids, com dados reais para realocar, entra no caminho que
de fato monta e usa a tabela.

### O que ainda falta

Não cheguei a uma correção. O que precisa ser resolvido é: **onde realmente mora a
`Region$$Table` (dados de scatter-load) dentro do arquivo `.mod` do Zeeboids**, já que não é
no offset 0 como nosso loader assume. Isso provavelmente exige:

1. Entender o formato real do `.mod`/`.bar` da Zeebo além do que `mod_loader.c` já faz hoje
   (hoje ele só separa `code/data/bss` de um blob único — pode haver uma seção adicional,
   possivelmente dentro do `.bar` companheiro de 7MB+ que hoje é ignorado com "magic
   desconhecido", ver log `bar: '...' presente (... bytes) - magic desconhecido`).
2. Alternativa mais barata: como esse é código de bootstrap GENÉRICO do compilador (não lógica
   do jogo), pode ser mais produtivo **pular esse trecho inteiro via HLE** (detectar o padrão
   de entrada em `0x40`/`0x48` e simular o efeito do scatter-load — zerar BSS, que já fazemos
   via `mod1: BSS zerado em ...` no loader — em vez de deixar a CPU executar o stub PIC de
   verdade). Não implementei isso porque está fora do escopo estrito desta tarefa (mexe em
   `mod_loader.c`/`boot.c` de um jeito mais amplo do que "case 5"), mas é o caminho mais
   promissor que vejo.

Ferramentas: mesmas de Pac-Mania (capstone + watchpoints temporários revertidos), mais
verificação binária direta do `.mif` via Python (`bytes.find`).

---

## Arquivos tocados nesta branch

- `src/loader/mif_parser.c` — CLSID do Zeeboids (já pendente antes desta rodada, mantido e
  **verificado** com evidência binária nova).
- `src/brew/aee_ids.h` — nome do Zeeboids na tabela de applets conhecidos (idem).
- Nenhuma mudança em `src/brew/boot.c` (case 5) nem `src/cpu/cpu.c` — toda instrumentação de
  debug usada na investigação foi revertida antes deste commit.

## Aviso operacional para a sincronização

Descobri no meio da sessão que meu branch (`rodada2/pacmania-zeeboids-boot`) tinha sido trocado
por outro agente (`git branch --show-current` retornou `rodada2/family-pack-render`) sem eu ter
feito o checkout — sinal de que o working directory físico é compartilhado entre as 3 IAs, não
clones separados. Não commitei nada na branch errada (só rodei builds/testes), mas isso é um
risco real de perda de trabalho se alguém commitar no branch errado sem perceber. Recomendo
`git worktree add ../zeebo_libretro-<pessoa> rodada2/<slug>` na próxima rodada.
