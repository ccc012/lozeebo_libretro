# 🧠 Emulação de CPU ARM - Detalhada

> Como emular o processador ARM do Zeebo, passo a passo

---

## 📌 Conceito Fundamental: O Ciclo Fetch-Decode-Execute

Todo emulador de CPU segue o mesmo ciclo básico:

```
LOOP INFINITO:
  1. FETCH   - Ler instrução da memória (no endereço do PC)
  2. DECODE  - Descobrir o que a instrução faz
  3. EXECUTE - Fazer o que a instrução manda
  4. Avançar PC para próxima instrução
  5. Repetir
```

### Em Pseudocódigo

```
while (cpu_running) {
    // FETCH
    instrucao = memoria[PC];
    
    // DECODE
    tipo = decodificar(instrucao);
    
    // EXECUTE
    switch (tipo) {
        case MOV: executar_mov(); break;
        case ADD: executar_add(); break;
        // ...
    }
    
    // AVANÇAR
    PC = PC + tamanho_instrucao;  // 4 bytes ARM, 2 bytes Thumb
}
```

Isto é o CORAÇÃO do emulador. Tudo mais gira em torno disso.

---

## 🔍 FETCH - Ler a Instrução

### ARM (32-bit)

```
Instruções ARM têm 32 bits (4 bytes).

Ler:
opcode = memoria[PC]           (byte 0)
       | memoria[PC+1] << 8    (byte 1)
       | memoria[PC+2] << 16   (byte 2)
       | memoria[PC+3] << 24   (byte 3)

Little-endian: byte menos significativo primeiro
```

### Thumb (16-bit)

```
Instruções Thumb têm 16 bits (2 bytes).

Ler:
opcode = memoria[PC]           (byte 0)
       | memoria[PC+1] << 8    (byte 1)
```

### Como Saber ARM ou Thumb?

```
O bit T (bit 5) do CPSR indica:
├─ T = 0 → modo ARM (ler 32-bit)
└─ T = 1 → modo Thumb (ler 16-bit)

A troca acontece via instrução BX (Branch Exchange).
```

---

## 🔬 DECODE - Entender a Instrução

### Anatomia de uma Instrução ARM

Cada instrução ARM tem campos codificados nos 32 bits:

```
Exemplo: ADD R0, R1, R2
Binário: 1110 00 0 0100 0 0001 0000 00000000 0010

Campos:
├─ Bits 31-28: Condição (1110 = AL, sempre executa)
├─ Bits 27-26: 00 (tipo de instrução)
├─ Bits 25:    0 (immediate ou registrador)
├─ Bits 24-21: 0100 (opcode: ADD)
├─ Bits 20:    0 (atualiza flags?)
├─ Bits 19-16: 0001 (Rn = R1, primeiro operando)
├─ Bits 15-12: 0000 (Rd = R0, destino)
└─ Bits 11-0:  operando 2 (R2)
```

### Processo de Decodificação

```
Para decodificar, você usa MÁSCARAS E COMPARAÇÕES:

// Exemplo: detectar se é MOV
if ((opcode & MASK_MOV) == PATTERN_MOV) {
    // É MOV!
    dest = (opcode >> 12) & 0xF;   // Extrair Rd
    src  = opcode & 0xFFF;          // Extrair operando
}

Máscara: isola os bits que importam
Pattern: o valor que identifica a instrução
```

### Tabela de Decodificação (simplificada)

```
┌──────────────┬─────────────┬──────────────────────┐
│ Instrução    │ Máscara     │ Pattern              │
├──────────────┼─────────────┼──────────────────────┤
│ Data Proc    │ 0x0C000000  │ 0x00000000           │
│ Load/Store   │ 0x0C000000  │ 0x04000000           │
│ Branch       │ 0x0E000000  │ 0x0A000000           │
│ SW Interrupt │ 0x0F000000  │ 0x0F000000           │
└──────────────┴─────────────┴──────────────────────┘

(Estes são exemplos ilustrativos - os valores exatos
vêm do ARM Architecture Reference Manual)
```

---

## ⚡ EXECUTE - Fazer a Ação

### Categorias de Instruções

```
1. DATA PROCESSING (aritmética/lógica)
   └─ MOV, ADD, SUB, AND, ORR, EOR, CMP...

2. LOAD/STORE (memória)
   └─ LDR, STR, LDRB, STRB, LDM, STM...

3. BRANCH (controle de fluxo)
   └─ B, BL, BX...

4. MULTIPLY
   └─ MUL, MLA...

5. SPECIAL
   └─ SWI (software interrupt), etc
```

### Exemplos de Execução

#### MOV (mover valor)
```
MOV R0, #5   → R0 = 5

Execução:
registrador[dest] = valor_imediato;
```

#### ADD (somar)
```
ADD R0, R1, R2   → R0 = R1 + R2

Execução:
registrador[dest] = registrador[src1] + registrador[src2];
```

#### LDR (carregar da memória)
```
LDR R0, [R1]   → R0 = memoria[R1]

Execução:
endereco = registrador[base];
registrador[dest] = memoria_read32(endereco);
```

#### STR (guardar na memória)
```
STR R0, [R1]   → memoria[R1] = R0

Execução:
endereco = registrador[base];
memoria_write32(endereco, registrador[src]);
```

#### B (branch/pulo)
```
B label   → PC = endereço de label

Execução:
PC = PC + offset;   // Pula para outro lugar
```

#### BL (branch com link/chamada de função)
```
BL funcao   → chama função (salva retorno)

Execução:
LR = PC + 4;        // Salva endereço de retorno
PC = endereco_funcao;  // Pula para função
```

---

## 🚩 Flags e Execução Condicional

### O Que São Flags

```
Após operações, a CPU seta "flags" no CPSR:

N (Negative): resultado foi negativo?
Z (Zero):     resultado foi zero?
C (Carry):    houve "vai um"?
V (Overflow): estourou o limite?
```

### Como Setar Flags (exemplo: após SUB)

```
resultado = a - b;

Z = (resultado == 0) ? 1 : 0;
N = (resultado < 0)  ? 1 : 0;
C = (a >= b)         ? 1 : 0;  // sem borrow
V = detectar_overflow(a, b, resultado);
```

### Execução Condicional

```
No ARM, QUALQUER instrução pode ser condicional!

Os 4 bits superiores (31-28) definem a condição:

ADDEQ R0, R1, R2   → só executa ADD se Z=1 (equal)
MOVNE R0, #5       → só executa MOV se Z=0 (not equal)

Códigos de condição:
├─ EQ (0000): igual (Z=1)
├─ NE (0001): diferente (Z=0)
├─ GE (1010): maior ou igual
├─ LT (1011): menor
├─ AL (1110): sempre (padrão)
└─ ... 16 códigos no total
```

### Verificar Condição (pseudocódigo)

```
bool deve_executar(condicao, cpsr) {
    switch (condicao) {
        case EQ: return Z == 1;
        case NE: return Z == 0;
        case GE: return N == V;
        case LT: return N != V;
        case AL: return true;
        // ...
    }
}

// No loop:
if (deve_executar(instr.condicao, cpu.cpsr)) {
    executar(instr);
}
// Se não, pula a instrução (só avança PC)
```

### Tabela Completa das 16 Condições

```
Esta é a tabela EXATA e COMPLETA. Usar esta, não improvisar.

┌────────┬──────┬─────────────────────────┬──────────────────────┐
│ Código │ Mnem.│ Significado             │ Condição lógica      │
├────────┼──────┼─────────────────────────┼──────────────────────┤
│ 0000   │ EQ   │ Equal                   │ Z == 1                │
│ 0001   │ NE   │ Not Equal               │ Z == 0                │
│ 0010   │ CS/HS│ Carry Set / Unsigned ≥  │ C == 1                │
│ 0011   │ CC/LO│ Carry Clear / Unsigned <│ C == 0                │
│ 0100   │ MI   │ Minus / Negative        │ N == 1                │
│ 0101   │ PL   │ Plus / Positive or zero │ N == 0                │
│ 0110   │ VS   │ Overflow Set            │ V == 1                │
│ 0111   │ VC   │ Overflow Clear          │ V == 0                │
│ 1000   │ HI   │ Unsigned >              │ C == 1 AND Z == 0     │
│ 1001   │ LS   │ Unsigned ≤              │ C == 0 OR Z == 1      │
│ 1010   │ GE   │ Signed ≥                │ N == V                │
│ 1011   │ LT   │ Signed <                │ N != V                │
│ 1100   │ GT   │ Signed >                │ Z == 0 AND N == V     │
│ 1101   │ LE   │ Signed ≤                │ Z == 1 OR N != V      │
│ 1110   │ AL   │ Always                  │ sempre true           │
│ 1111   │ NV   │ Never (reservado)       │ nunca executa         │
└────────┴──────┴─────────────────────────┴──────────────────────┘

Nota sobre NV: em CPUs ARM modernas este código é "unpredictable"
(reservado para uso futuro), mas para HLE de um console antigo
como o Zeebo, o comportamento seguro é tratá-lo como "nunca executa".
```

---

## 🔄 Barrel Shifter (Operando 2 com Shift Embutido)

```
Este é um recurso do ARM MUITO usado e FÁCIL de esquecer:

QUALQUER instrução de Data Processing pode ter seu segundo
operando deslocado ("shiftado") ANTES de ser usado, sem gastar
uma instrução extra:

ADD R0, R1, R2, LSL #2   ; R0 = R1 + (R2 << 2)
MOV R0, R1, LSR #4       ; R0 = R1 >> 4
SUB R0, R1, R2, ASR R3   ; R0 = R1 - (R2 >> R3, com sinal)

DECISÃO PARA ESTE PROJETO:
└─ Implementar o barrel shifter DESDE A FASE 1 (não deixar para
   depois), porque é usado por praticamente todo jogo real -
   compiladores C geram isso o tempo todo (ex: acesso a arrays,
   multiplicação por potência de 2).

Formas do Operando 2 (bits 11-0 da instrução):
┌──────────────────────────┬────────────────────────────────────┐
│ Bit 25 = 1 (immediate)   │ Rotate(imm8, rotate*2)              │
│ Bit 25 = 0, bit 4 = 0    │ Registrador com shift por IMEDIATO  │
│ Bit 25 = 0, bit 4 = 1    │ Registrador com shift por REGISTRADOR│
└──────────────────────────┴────────────────────────────────────┘

Tipos de shift (bits 6-5, quando bit 25 = 0):
├─ 00 = LSL (Logical Shift Left)
├─ 01 = LSR (Logical Shift Right)
├─ 10 = ASR (Arithmetic Shift Right, preserva sinal)
└─ 11 = ROR (Rotate Right) — ou RRX se shift=0 e usa registrador

### Carry Flag e Barrel Shifter (detalhe fácil de esquecer)

```
Quando a instrução tem o bit S setado (atualiza flags) E usa
shift no operando 2, o ÚLTIMO BIT DESLOCADO PARA FORA alimenta
a flag Carry (C) — mesmo que a instrução não seja um shift
"puro" como LSL/LSR.

Exemplo:
MOVS R0, R1, LSL #1
├─ R0 = R1 << 1
└─ C = bit 31 de R1 (o bit que "saiu" no shift)

Isso é DIFERENTE do carry setado por ADD/SUB (que vem da soma).
Se a instrução for tipo MOV/MVN/AND/ORR/EOR/BIC com shift e bit S,
o carry vem do shifter, NÃO de uma soma.

DECISÃO PARA ESTE PROJETO:
└─ Implementar esse comportamento desde o início, junto com o
   barrel shifter, já que os dois andam juntos no hardware.
```

---

## 🔢 Constantes Imediatas Rotacionadas

```
ARM reserva só 12 bits para representar uma constante imediata,
mas os jogos usam valores de 32 bits o tempo todo. A solução do
hardware é: 8 bits de valor + 4 bits de "quantidade de rotação".

Encoding do immediate (quando bit 25 = 1):
┌──────────────┬──────────────┐
│ Bits 11-8    │ Bits 7-0     │
│ rotate (0-15)│ imm8 (0-255) │
└──────────────┴──────────────┘

Fórmula de decodificação:
valor_final = ROR(imm8, rotate * 2)

Exemplo:
MOV R0, #0xFF        ; imm8=0xFF, rotate=0  → valor = 0xFF
MOV R0, #0xFF000000  ; imm8=0xFF, rotate=4  → valor = 0xFF000000
                        (rotate*2 = 8, então 0xFF rotacionado
                         8 bits para direita = 0xFF000000)

DECISÃO PARA ESTE PROJETO:
└─ Implementar essa fórmula exata desde a Fase 1, no decode de
   Data Processing. Não simplificar para "ler 12 bits direto",
   pois isso quebra qualquer MOV/CMP/etc com constante grande.
```

---

## 🏛️ Decisão de Escopo: Modos de CPU

```
O ARM real tem VÁRIOS modos de operação, cada um com seu
próprio banco de registradores (User, IRQ, FIQ, Supervisor, etc).

DECISÃO PARA ESTE PROJETO:
└─ Emular APENAS o modo USER.
   ├─ Motivo: HLE não precisa simular interrupções de hardware
   │  reais (são o sistema BREW original que não existe mais).
   ├─ Um único banco de 16 registradores é suficiente.
   └─ Simplifica MUITO a struct da CPU e a implementação.

Isso significa:
[ ] Não implementar troca de modo via CPSR
[ ] Não implementar bancos de registrador separados (R8_fiq, etc)
[ ] Se o jogo tentar entrar em modo privilegiado, isso é um
    "não deveria acontecer" e deve ser logado, não implementado
```

---

## ⏱️ Decisão de Escopo: Pipeline

```
No ARM real, o PC (Program Counter) sempre aponta 8 bytes à
frente da instrução sendo executada (modo ARM) ou 4 bytes
(modo Thumb), por causa do pipeline de 3 estágios do
hardware real.

DECISÃO PARA ESTE PROJETO:
└─ NÃO simular um pipeline de verdade (fetch/decode/execute
   sobrepostos). Isso seria complexo e desnecessário para HLE.

└─ MAS simular o EFEITO do pipeline sobre o valor do PC quando
   ele é LIDO como operando (ex: "MOV R0, PC" ou "ADD R0, PC, R1"):
   
   valor_de_PC_como_operando = PC_real + 8   // modo ARM
   valor_de_PC_como_operando = PC_real + 4   // modo Thumb

Isso é essencial para branches relativos calculados corretamente,
mesmo sem simular o pipeline de hardware de verdade.
```

---

## 💥 Decisão de Escopo: Exceções e Erros

```
Situações de erro que PODEM ocorrer durante a execução:
├─ Instrução desconhecida/não implementada
├─ SWI/SVC (software interrupt)
├─ Acesso de memória fora dos limites
├─ Acesso de memória desalinhado

DECISÃO PARA ESTE PROJETO:
└─ NUNCA travar o emulador silenciosamente.
└─ Para instrução desconhecida:
   ├─ Logar: "Unknown instruction: 0x%08X at PC=0x%X"
   ├─ Tratar como NOP (só avança PC)
   └─ Continuar execução (não implementar handler de exceção real)

└─ Para SWI/SVC:
   ├─ Usado como possível mecanismo alternativo de trap para BREW
   ├─ Logar o número da interrupção
   └─ Ver 05_EMULACAO_APIS_BREW.md para o mecanismo de trap escolhido

└─ Para acesso de memória fora dos limites:
   ├─ Logar aviso com endereço e PC
   ├─ Retornar 0 (leitura) ou ignorar (escrita)
   └─ Não travar o processo

└─ Para acesso desalinhado (ex: LDR em endereço não múltiplo de 4):
   ├─ Fazer o acesso mesmo assim (não rejeitar)
   ├─ Logar aviso na primeira ocorrência de cada endereço
   └─ Não implementar rotação de valor como hardware real faz
       (comportamento raro de ser explorado por jogos)

Justificativa: erros devem ser visíveis nos logs para facilitar
debug, mas NUNCA devem derrubar o núcleo — travar o RetroArch
por causa de uma instrução não suportada é pior do que continuar
com comportamento levemente incorreto e permitir investigar depois.
```

---

## 🎯 Estratégia: Interpretador vs JIT

### Interpretador (COMECE POR ESTE)

```
Como funciona:
├─ Lê uma instrução
├─ Decodifica
├─ Executa
└─ Repete

Vantagens:
✅ Simples de implementar
✅ Fácil de debugar
✅ Fácil de adicionar instruções
✅ Portável

Desvantagens:
❌ Mais lento (mas OK para começar)
```

### JIT (Just-In-Time) - DEPOIS

```
Como funciona:
├─ Traduz blocos de ARM para código nativo (x86)
├─ Executa código nativo (rápido)
└─ Cacheia traduções

Vantagens:
✅ Muito mais rápido

Desvantagens:
❌ Complexo de implementar
❌ Difícil de debugar
❌ Deixe para depois (Fase 7)
```

### Recomendação

```
FASE 1-6: Interpretador
├─ Foco em compatibilidade
├─ Foco em correção
└─ Performance vem depois

FASE 7+: Considerar JIT
├─ Se performance for problema
├─ Se jogos estiverem lentos
└─ Otimização
```

---

## 📊 Quais Instruções Implementar Primeiro

### Prioridade 1 (Essenciais - Semana 4-5)

```
MOV   - mover valores
ADD   - somar
SUB   - subtrair
LDR   - ler memória
STR   - escrever memória
B     - branch
BL    - chamar função
CMP   - comparar
```

### Prioridade 2 (Muito comuns - Semana 6-7)

```
AND   - E lógico
ORR   - OU lógico
EOR   - XOR
LSL   - shift esquerda
LSR   - shift direita
PUSH  - empilhar
POP   - desempilhar
LDM   - load multiple
STM   - store multiple
```

### Prioridade 3 (Comuns - Semana 8)

```
MUL   - multiplicar
TST   - testar bits
BX    - branch exchange
LDRB  - load byte
STRB  - store byte
ASR   - shift aritmético
ROR   - rotação
```

### Prioridade 4 (Menos comuns - conforme necessário)

```
MLA   - multiply accumulate
SWP   - swap
SWI   - software interrupt
Coprocessor instructions
... (implementar quando jogo precisar)
```

---

## 🔧 Estrutura de Dados da CPU (Conceitual)

```
Estrutura mínima que você precisa:

CPU {
    registradores[16]    // R0-R15
    cpsr                 // flags e modo
    
    modo_thumb           // ARM ou Thumb?
    
    memoria*             // ponteiro para RAM
    
    // Estatísticas (opcional)
    instrucoes_executadas
    ciclos
}
```

---

## 🐛 Como Debugar a CPU

### Técnica 1: Trace de Execução

```
A cada instrução, logar:
├─ PC (onde está)
├─ Instrução (o que vai fazer)
├─ Registradores antes
└─ Registradores depois

Exemplo de log:
[0x1000] MOV R0, #5    | R0: 0 → 5
[0x1004] ADD R1, R0, #3 | R1: 0 → 8
[0x1008] STR R1, [R2]   | mem[0x2000] = 8
```

### Técnica 2: Comparar com Emulador de Referência

```
Rodar mesma ROM em:
├─ Seu emulador
└─ QEMU ou Infuse

Comparar estado dos registradores.
Onde divergir = seu bug.
```

### Técnica 3: Breakpoints

```
Parar execução em endereço específico:

if (PC == endereco_suspeito) {
    imprimir_estado_completo();
    esperar_comando_usuario();
}
```

---

## 📖 Exemplo Completo: Executar um Programa Simples

```
Programa ARM (pseudocódigo):
  MOV R0, #10      ; R0 = 10
  MOV R1, #20      ; R1 = 20
  ADD R2, R0, R1   ; R2 = R0 + R1 = 30
  STR R2, [R3]     ; memoria[R3] = 30

Execução no emulador:

Passo 1: PC=0x1000
  FETCH: opcode = 0xE3A0000A (MOV R0, #10)
  DECODE: tipo=MOV, dest=R0, valor=10
  EXECUTE: R0 = 10
  PC += 4 → PC=0x1004

Passo 2: PC=0x1004
  FETCH: opcode = 0xE3A01014 (MOV R1, #20)
  DECODE: tipo=MOV, dest=R1, valor=20
  EXECUTE: R1 = 20
  PC += 4 → PC=0x1008

Passo 3: PC=0x1008
  FETCH: opcode = 0xE0802001 (ADD R2, R0, R1)
  DECODE: tipo=ADD, dest=R2, src1=R0, src2=R1
  EXECUTE: R2 = R0 + R1 = 30
  PC += 4 → PC=0x100C

Passo 4: PC=0x100C
  FETCH: opcode = 0xE5832000 (STR R2, [R3])
  DECODE: tipo=STR, src=R2, base=R3
  EXECUTE: memoria[R3] = 30
  PC += 4 → PC=0x1010

Resultado: R2 = 30, memória atualizada. ✅
```

---

## ⚠️ Armadilhas Comuns

```
1. ENDIANNESS
   └─ ARM é little-endian, cuidado ao ler/escrever

2. PC OFFSET
   └─ No ARM, PC aponta 2 instruções à frente (pipeline)
   └─ PC = endereço_atual + 8 (ARM) ou +4 (Thumb)
   └─ Importante para branches relativos

3. FLAGS
   └─ Nem toda instrução atualiza flags
   └─ Só se o bit S estiver setado (ADDS vs ADD)

4. THUMB
   └─ Instruções diferentes, decodificação diferente
   └─ Não misturar com ARM

5. CONDIÇÃO
   └─ Sempre checar condição antes de executar
   └─ Instrução com condição falsa = NOP (só avança PC)
```

---

## 🎯 Checklist de Implementação da CPU

```
Estrutura:
[ ] Struct da CPU definida
[ ] Init/reset/destroy

Fetch:
[ ] Ler ARM (32-bit)
[ ] Ler Thumb (16-bit)
[ ] Detectar modo (ARM/Thumb)

Decode:
[ ] Framework de decodificação
[ ] Extração de campos (dest, src, imm)

Execute - Data Processing:
[ ] MOV, ADD, SUB
[ ] AND, ORR, EOR
[ ] CMP, TST
[ ] Shifts (LSL, LSR, ASR, ROR)

Execute - Load/Store:
[ ] LDR, STR
[ ] LDRB, STRB
[ ] LDM, STM (PUSH/POP)

Execute - Branch:
[ ] B, BL
[ ] BX (troca ARM/Thumb)

Flags:
[ ] Atualização de N, Z, C, V
[ ] Verificação de condição (16 condições, tabela completa)
[ ] Carry setado corretamente pelo barrel shifter (não só por soma)

Barrel Shifter e Immediates:
[ ] Decode de operando 2 com shift por imediato
[ ] Decode de operando 2 com shift por registrador
[ ] Decode de immediate rotacionado (imm8 + rotate*2)
[ ] LSL, LSR, ASR, ROR aplicados como parte do operando (não só
    como instruções separadas)

Escopo e Robustez (decisões já tomadas, ver seções acima):
[ ] Emular apenas modo User (sem bancos de registrador IRQ/FIQ)
[ ] PC simula efeito de pipeline (+8 ARM / +4 Thumb) quando lido
    como operando, sem pipeline real
[ ] Instrução desconhecida: loga e trata como NOP (não trava)
[ ] Acesso de memória fora dos limites: loga e retorna 0/ignora
[ ] Acesso desalinhado: permite mesmo assim, loga aviso

Thumb:
[ ] Decodificação Thumb
[ ] Instruções Thumb principais
[ ] Suporte a troca ARM ↔ Thumb via BX desde a Fase 1 (não deixar
    Thumb totalmente para depois - jogos podem misturar os dois)

Debug:
[ ] Trace de execução
[ ] Print de estado
[ ] Breakpoints
```

---

## 🎯 Próximo Passo

Agora você entende como emular a CPU. Próximo:

→ **05_EMULACAO_APIS_BREW.md** - Como emular as APIs BREW (o "sistema operacional")

A CPU executa o código, mas o código chama APIs BREW para desenhar, tocar som, etc. O próximo doc explica isso.
