# 📋 Instruções ARM Priorizadas - Referência Completa

> Lista de todas as instruções a implementar, em ordem de prioridade

---

## 📌 Como Usar Este Documento

```
Esta é sua REFERÊNCIA durante a implementação da CPU.

Para cada instrução:
├─ O que faz
├─ Sintaxe
├─ Prioridade (quando implementar)
└─ Exemplo

Implemente na ordem das prioridades.
```

---

## 🎯 Sistema de Prioridades

```
P1 (CRÍTICA): Sem isso, nada funciona
P2 (ALTA):    Muito comum, implemente logo
P3 (MÉDIA):   Comum, implemente na fase 2
P4 (BAIXA):   Menos comum, quando precisar
P5 (RARA):    Edge cases, implementar por último
```

---

## 📦 CATEGORIA 1: Data Processing (Aritmética/Lógica)

```
⚠️ NOTA IMPORTANTE - lida em conjunto com 04_EMULACAO_CPU_DETALHADA.md:

Todas as instruções desta categoria compartilham o mesmo mecanismo
de "Operando 2", que pode ser:
├─ Uma constante imediata rotacionada (8 bits + rotação)
├─ Um registrador puro
├─ Um registrador com shift embutido (LSL/LSR/ASR/ROR por
    imediato ou por outro registrador)

Isso NÃO é uma instrução separada - é parte do encoding de CADA
instrução de Data Processing (MOV, ADD, SUB, AND, etc).

Ver seção "Barrel Shifter" e "Constantes Imediatas Rotacionadas"
em 04_EMULACAO_CPU_DETALHADA.md para os detalhes exatos de bits
e a decisão de implementar isso desde a Fase 1.
```

### MOV - Move (P1)

```
O que faz: Coloca um valor em um registrador

Sintaxe:
MOV Rd, #imm      ; Rd = valor imediato
MOV Rd, Rn        ; Rd = Rn

Exemplo:
MOV R0, #5        ; R0 = 5
MOV R1, R0        ; R1 = R0

Prioridade: P1 (essencial)
```

### ADD - Add (P1)

```
O que faz: Soma

Sintaxe:
ADD Rd, Rn, #imm  ; Rd = Rn + imm
ADD Rd, Rn, Rm    ; Rd = Rn + Rm

Exemplo:
ADD R0, R1, #10   ; R0 = R1 + 10
ADD R2, R0, R1    ; R2 = R0 + R1

Prioridade: P1
```

### SUB - Subtract (P1)

```
O que faz: Subtrai

Sintaxe:
SUB Rd, Rn, #imm  ; Rd = Rn - imm
SUB Rd, Rn, Rm    ; Rd = Rn - Rm

Exemplo:
SUB R0, R1, #5    ; R0 = R1 - 5

Prioridade: P1
```

### CMP - Compare (P1)

```
O que faz: Compara (subtrai mas só seta flags)

Sintaxe:
CMP Rn, #imm      ; flags = Rn - imm
CMP Rn, Rm        ; flags = Rn - Rm

Exemplo:
CMP R0, #10       ; se R0==10, Z=1

Prioridade: P1 (necessário para branches condicionais)
```

### AND - Bitwise AND (P2)

```
O que faz: E lógico bit a bit

Sintaxe:
AND Rd, Rn, #imm  ; Rd = Rn & imm
AND Rd, Rn, Rm    ; Rd = Rn & Rm

Exemplo:
AND R0, R1, #0xFF ; R0 = R1 & 0xFF (pega byte baixo)

Prioridade: P2
```

### ORR - Bitwise OR (P2)

```
O que faz: OU lógico bit a bit

Sintaxe:
ORR Rd, Rn, #imm  ; Rd = Rn | imm
ORR Rd, Rn, Rm    ; Rd = Rn | Rm

Exemplo:
ORR R0, R1, #0x80 ; R0 = R1 | 0x80 (seta bit 7)

Prioridade: P2
```

### EOR - Bitwise XOR (P2)

```
O que faz: XOR bit a bit

Sintaxe:
EOR Rd, Rn, Rm    ; Rd = Rn ^ Rm

Exemplo:
EOR R0, R0, R0    ; R0 = 0 (truque comum)

Prioridade: P2
```

### TST - Test (P3)

```
O que faz: AND mas só seta flags

Sintaxe:
TST Rn, #imm      ; flags = Rn & imm

Exemplo:
TST R0, #1        ; testa se bit 0 está setado

Prioridade: P3
```

### MVN - Move Not (P3)

```
O que faz: Move o inverso (NOT)

Sintaxe:
MVN Rd, Rn        ; Rd = ~Rn

Exemplo:
MVN R0, R1        ; R0 = NOT R1

Prioridade: P3
```

### MUL - Multiply (P3)

```
O que faz: Multiplica

Sintaxe:
MUL Rd, Rm, Rs    ; Rd = Rm * Rs

Exemplo:
MUL R0, R1, R2    ; R0 = R1 * R2

Prioridade: P3
```

### Outras Data Processing (P4-P5)

```
RSB  - Reverse Subtract (Rd = imm - Rn)
ADC  - Add with Carry
SBC  - Subtract with Carry
RSC  - Reverse Subtract with Carry
BIC  - Bit Clear (Rd = Rn & ~Rm)
CMN  - Compare Negative
TEQ  - Test Equivalence
MLA  - Multiply Accumulate
UMULL/SMULL - Long multiply
```

---

## 📦 CATEGORIA 2: Load/Store (Memória)

### LDR - Load Register (P1)

```
O que faz: Lê 32-bit da memória

Sintaxe:
LDR Rd, [Rn]           ; Rd = mem[Rn]
LDR Rd, [Rn, #offset]  ; Rd = mem[Rn + offset]

Exemplo:
LDR R0, [R1]           ; R0 = memória em R1
LDR R0, [R1, #4]       ; R0 = memória em R1+4

Prioridade: P1
```

### STR - Store Register (P1)

```
O que faz: Escreve 32-bit na memória

Sintaxe:
STR Rd, [Rn]           ; mem[Rn] = Rd
STR Rd, [Rn, #offset]  ; mem[Rn+offset] = Rd

Exemplo:
STR R0, [R1]           ; memória em R1 = R0

Prioridade: P1
```

### LDRB - Load Byte (P2)

```
O que faz: Lê 8-bit (1 byte)

Sintaxe:
LDRB Rd, [Rn]     ; Rd = mem[Rn] (só 1 byte)

Prioridade: P2
```

### STRB - Store Byte (P2)

```
O que faz: Escreve 8-bit (1 byte)

Sintaxe:
STRB Rd, [Rn]     ; mem[Rn] = Rd (só 1 byte)

Prioridade: P2
```

### LDM - Load Multiple (P2)

```
O que faz: Carrega vários registradores de uma vez

Sintaxe:
LDM Rn, {R0-R3}   ; carrega R0,R1,R2,R3 da memória

Usado para POP:
LDMFD SP!, {R4-R7, PC}  ; POP (restaurar registradores)

Prioridade: P2 (importante para funções)
```

### STM - Store Multiple (P2)

```
O que faz: Guarda vários registradores de uma vez

Sintaxe:
STM Rn, {R0-R3}   ; guarda R0,R1,R2,R3 na memória

Usado para PUSH:
STMFD SP!, {R4-R7, LR}  ; PUSH (salvar registradores)

Prioridade: P2
```

### LDRH/STRH - Load/Store Half (P3)

```
O que faz: Lê/escreve 16-bit (2 bytes)

Sintaxe:
LDRH Rd, [Rn]     ; Rd = mem[Rn] (2 bytes)
STRH Rd, [Rn]     ; mem[Rn] = Rd (2 bytes)

Prioridade: P3
```

---

## 📦 CATEGORIA 3: Branch (Controle de Fluxo)

### B - Branch (P1)

```
O que faz: Pula para outro endereço

Sintaxe:
B label           ; PC = endereço de label

Com condição:
BEQ label         ; pula se Z=1 (equal)
BNE label         ; pula se Z=0 (not equal)
BLT label         ; pula se menor
BGT label         ; pula se maior

Exemplo:
B loop            ; volta para loop
BEQ fim           ; se igual, vai para fim

Prioridade: P1 (essencial para qualquer lógica)
```

### BL - Branch with Link (P1)

```
O que faz: Chama função (salva retorno)

Sintaxe:
BL funcao         ; LR = próxima instrução; PC = funcao

Exemplo:
BL calcular       ; chama função calcular

Prioridade: P1 (essencial para funções)
```

### BX - Branch Exchange (P2)

```
O que faz: Pula e pode trocar ARM/Thumb

Sintaxe:
BX Rn             ; PC = Rn (troca modo se bit 0)

Uso comum:
BX LR             ; retornar de função

Prioridade: P2 (necessário para retorno e Thumb)
```

### BLX - Branch Link Exchange (P3)

```
O que faz: BL + troca de modo

Sintaxe:
BLX Rn            ; chama função e troca modo

Prioridade: P3
```

---

## 📦 CATEGORIA 4: Shifts (Deslocamentos)

### LSL - Logical Shift Left (P2)

```
O que faz: Desloca bits para esquerda

Sintaxe:
LSL Rd, Rn, #imm  ; Rd = Rn << imm

Exemplo:
LSL R0, R1, #2    ; R0 = R1 * 4 (shift 2 = *4)

Prioridade: P2
```

### LSR - Logical Shift Right (P2)

```
O que faz: Desloca bits para direita

Sintaxe:
LSR Rd, Rn, #imm  ; Rd = Rn >> imm

Exemplo:
LSR R0, R1, #1    ; R0 = R1 / 2

Prioridade: P2
```

### ASR - Arithmetic Shift Right (P3)

```
O que faz: Shift direita preservando sinal

Sintaxe:
ASR Rd, Rn, #imm  ; Rd = Rn >> imm (com sinal)

Prioridade: P3
```

### ROR - Rotate Right (P3)

```
O que faz: Rotaciona bits para direita

Sintaxe:
ROR Rd, Rn, #imm  ; rotaciona

Prioridade: P3
```

---

## 📦 CATEGORIA 5: Especiais

### SWI/SVC - Software Interrupt (P3)

```
O que faz: Chama o "sistema operacional"

Sintaxe:
SWI #numero       ; interrupção de software

Uso em BREW:
├─ Pode ser usado para chamar APIs
└─ Método alternativo ao trap de endereços

Prioridade: P3 (depende de como você faz HLE)
```

### NOP - No Operation (P4)

```
O que faz: Nada (passa)

Sintaxe:
NOP               ; não faz nada

Prioridade: P4
```

### MRS/MSR - Status Register Access (P4)

```
O que faz: Ler/escrever CPSR

Sintaxe:
MRS Rd, CPSR      ; Rd = CPSR
MSR CPSR, Rn      ; CPSR = Rn

Prioridade: P4
```

---

## 🚫 Fora de Escopo (Decisões Já Tomadas)

```
COPROCESSADOR (CDP, MCR, MRC):
└─ Zeebo (ARM11/Cortex-A8 como referência) provavelmente não usa
   coprocessador customizado em jogos comuns.
└─ DECISÃO: não implementar. Se encontrado, tratar como instrução
   desconhecida (loga e faz NOP), igual a qualquer opcode não
   suportado. Não é necessário um handler dedicado.

THUMB-2 (instruções Thumb de 32-bit):
└─ Zeebo usa ARM11 (ARMv6), que suporta Thumb original (16-bit)
   mas não o conjunto estendido Thumb-2 (que é ARMv6T2+).
└─ DECISÃO: implementar apenas Thumb clássico de 16-bit.
   Se aparecer um encoding que pareça Thumb-2, tratar como
   instrução desconhecida e logar - não se espera que ocorra
   com os jogos-alvo (Crash Nitro Kart, Double Dragon, Family Pack).
```

---

## 🎯 THUMB - Instruções 16-bit

```
Thumb é uma versão compacta.
Menos registradores acessíveis, mas menor.

Instruções Thumb essenciais (P2 quando implementar Thumb):

Thumb MOV, ADD, SUB    - versões 16-bit
Thumb LDR, STR         - load/store
Thumb B, BL            - branches
Thumb PUSH, POP        - stack

Decodificação é DIFERENTE do ARM.
Implementar separadamente.
```

---

## 📊 Ordem de Implementação Recomendada

### Semana 4 (P1 básico)
```
1. MOV
2. ADD
3. SUB
4. CMP
5. B (branch)
6. BL (call)
```

### Semana 5 (P1 memória)
```
7. LDR
8. STR
9. BX (return)
```

### Semana 6 (P2 comum)
```
10. AND
11. ORR
12. EOR
13. LSL
14. LSR
15. LDRB
16. STRB
```

### Semana 7 (P2 stack)
```
17. LDM (POP)
18. STM (PUSH)
19. MUL
```

### Semana 8 (P3 e Thumb)
```
20. TST
21. ASR
22. ROR
23. Instruções Thumb
```

### Depois (P4-P5)
```
Implementar conforme jogos precisam.
Log "Unknown instruction" mostra o que falta.
```

---

## 📋 Tabela Resumo de Prioridades

```
┌────────────┬───────────┬────────────────────────┐
│ Prioridade │ Instrução │ Quando                 │
├────────────┼───────────┼────────────────────────┤
│ P1         │ MOV       │ Semana 4               │
│ P1         │ ADD/SUB   │ Semana 4               │
│ P1         │ CMP       │ Semana 4               │
│ P1         │ B/BL      │ Semana 4               │
│ P1         │ LDR/STR   │ Semana 5               │
│ P1         │ BX        │ Semana 5               │
│ P2         │ AND/ORR   │ Semana 6               │
│ P2         │ EOR       │ Semana 6               │
│ P2         │ LSL/LSR   │ Semana 6               │
│ P2         │ LDRB/STRB │ Semana 6               │
│ P2         │ LDM/STM   │ Semana 7               │
│ P2         │ MUL       │ Semana 7               │
│ P3         │ TST/ASR   │ Semana 8               │
│ P3         │ ROR       │ Semana 8               │
│ P3         │ Thumb     │ Semana 8               │
│ P4-P5      │ Resto     │ Conforme necessário    │
└────────────┴───────────┴────────────────────────┘
```

---

## 🔍 Como Descobrir Instruções Faltando

```
Quando rodar um jogo:

if (instrução desconhecida) {
    log_warn("Unknown instruction: 0x%08X at PC=0x%X",
             opcode, PC);
}

Colете esses logs.
Veja quais opcodes aparecem.
Decodifique e implemente.
```

---

## 📚 Referência Completa

```
Para detalhes EXATOS de encoding:

ARM Architecture Reference Manual
├─ Cada instrução tem seu encoding preciso
├─ Bits exatos de cada campo
└─ Comportamento detalhado

Disponível em:
https://developer.arm.com/documentation/

Procurar: "ARMv6 Architecture Reference Manual"
(Zeebo usa ARM11 = ARMv6)
```

---

## 🎯 Conclusão da Documentação

Você agora tem **TODA a documentação de planejamento**:

```
00_INDICE.md                    ✅ Visão geral
01_ARQUITETURA_LIBRETRO.md      ✅ Interface RetroArch
02_ARQUITETURA_ZEEBO.md         ✅ Hardware Zeebo
03_PLANO_DESENVOLVIMENTO.md     ✅ Fases e timeline
04_EMULACAO_CPU_DETALHADA.md    ✅ Como emular CPU
05_EMULACAO_APIS_BREW.md        ✅ Como emular APIs
06_TOOLCHAIN_SETUP.md           ✅ Ferramentas
07_ESTRUTURA_PASTA.md           ✅ Organização
08_TESTES_ESTRATEGIA.md         ✅ Como testar
09_MAPEAMENTO_MEMORIAS.md       ✅ Memória
10_INSTRUCOES_ARM.md            ✅ Instruções (este)
```

**Quando estiver pronto para começar a implementar, é só avisar!**

Você tem o plano completo. O próximo passo (quando quiser) é começar a Fase 0: setup e estrutura.
