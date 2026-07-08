# 🧪 Estratégia de Testes - Como Validar o Núcleo

> Como testar sistematicamente e quais jogos usar

---

## 📌 Por Que Testar é Crucial

```
Emuladores são complexos.
Um bug pequeno pode quebrar tudo.
Testar sistematicamente = achar bugs cedo.

Sem testes:
├─ Bugs se acumulam
├─ Difícil saber o que quebrou
└─ Frustração

Com testes:
├─ Cada peça validada
├─ Sabe onde está o problema
└─ Confiança no código
```

---

## 🎯 Níveis de Teste

```
NÍVEL 1: Testes Unitários
└─ Testar cada instrução/função isoladamente

NÍVEL 2: Testes de Integração
└─ Testar componentes juntos

NÍVEL 3: Testes com ROMs de Teste
└─ Programas ARM simples que você controla

NÍVEL 4: Testes com Jogos Reais
└─ Os jogos alvo (Family Pack, etc)
```

---

## 🔬 NÍVEL 1: Testes Unitários

### Testar Instruções da CPU

```
Para cada instrução, criar um teste:

TESTE: MOV R0, #5
├─ Setup: CPU zerada
├─ Ação: executar MOV R0, #5
├─ Verificar: R0 == 5? ✅ ou ❌
└─ Resultado: PASS ou FAIL
```

### Exemplo Conceitual de Teste

```
função testar_MOV() {
    // Setup
    CPU cpu;
    cpu_init(&cpu);
    
    // Colocar instrução MOV R0, #5 na memória
    escrever_instrucao(0x1000, MOV_R0_5);
    cpu.PC = 0x1000;
    
    // Executar
    cpu_step(&cpu);
    
    // Verificar
    if (cpu.R[0] == 5) {
        print("MOV: PASS");
    } else {
        print("MOV: FAIL (R0=%d, esperado 5)", cpu.R[0]);
    }
}
```

### Testes Essenciais da CPU

```
[ ] MOV: mover valor
[ ] ADD: 5 + 3 = 8
[ ] SUB: 10 - 4 = 6
[ ] LDR: ler da memória
[ ] STR: escrever na memória
[ ] B: pular para endereço
[ ] BL: chamar função (LR correto?)
[ ] CMP: comparar (flags corretas?)
[ ] AND: 0xFF & 0x0F = 0x0F
[ ] ORR: 0xF0 | 0x0F = 0xFF
[ ] Condições: TODAS as 16 (não só EQ/NE - ver tabela completa
    em 04_EMULACAO_CPU_DETALHADA.md, incluindo HI/LS/GT/LE que
    são fáceis de implementar errado)
[ ] Barrel shifter: MOV R0, R1, LSL #2 (shift embutido no operando)
[ ] Carry via shifter: MOVS R0, R1, LSL #1 seta C corretamente?
[ ] Immediate rotacionado: MOV R0, #0xFF000000 (rotate != 0)
[ ] PC como operando: MOV R0, PC retorna PC+8 (ARM) ou PC+4 (Thumb)?
```

Estes 4 últimos itens são os que mais geram bugs silenciosos -
o jogo roda, mas com valores sutilmente errados que só aparecem
como glitches visuais ou travamentos aleatórios muito depois.

### Testar Memória

```
[ ] Write32 + Read32 = mesmo valor?
[ ] Write16 + Read16 = mesmo valor?
[ ] Write8 + Read8 = mesmo valor?
[ ] Little-endian correto?
[ ] Bounds checking funciona?
```

---

## 🔗 NÍVEL 2: Testes de Integração

### Testar Componentes Juntos

```
Exemplo: CPU + Memória

Programa que:
1. MOV R0, #42
2. STR R0, [R1]     (guarda na memória)
3. LDR R2, [R1]     (lê da memória)
4. Verificar: R2 == 42?

Testa: CPU executa + Memória funciona + juntos OK
```

### Testar Loader + CPU

```
1. Carregar uma ROM de teste
2. CPU executa do entry point
3. Verificar que executou corretamente
```

---

## 💾 NÍVEL 3: ROMs de Teste

### O Que São

```
Programas ARM SIMPLES que VOCÊ escreve.
Você sabe exatamente o que devem fazer.
Fácil de verificar se emulador está certo.
```

### Como Criar

```
Escrever em assembly ARM:

.global _start
_start:
    MOV R0, #10
    MOV R1, #20
    ADD R2, R0, R1    ; R2 deve ser 30
    ; ... loop infinito para parar

Compilar com:
arm-none-eabi-as teste.s -o teste.o
arm-none-eabi-ld teste.o -o teste.elf
arm-none-eabi-objcopy -O binary teste.elf teste.bin
```

### Progressão de ROMs de Teste

```
teste_01_mov.bin     → só MOV
teste_02_add.bin     → aritmética
teste_03_memory.bin  → LDR/STR
teste_04_loop.bin    → branches e loop
teste_05_function.bin → BL/BX (funções)
teste_06_thumb.bin   → modo Thumb
```

### Vantagem

```
Se teste_04 (loop) falha:
├─ Você sabe que branches têm bug
├─ Não precisa debugar jogo inteiro
└─ Isola o problema
```

### ROMs de Teste Prontas (Comunidade)

```
Existem test suites para ARM:
├─ Procurar "ARM CPU test ROM"
├─ Alguns emuladores compartilham
└─ Validam correção da emulação
```

---

## 🎮 NÍVEL 4: Jogos Reais

### Os 3 Jogos Alvo

```
Ordem de dificuldade (comece pelo mais fácil):

1. ZEEBO FAMILY PACK (mais simples)
   ├─ Mini-games básicos
   ├─ Gráficos 2D simples
   ├─ Pouco áudio
   └─ Bom primeiro alvo

2. DOUBLE DRAGON (médio)
   ├─ Fighting game
   ├─ Sprites 2D
   ├─ Áudio MIDI
   └─ Segundo alvo

3. CRASH NITRO KART 3D (difícil)
   ├─ Jogo 3D
   ├─ OpenGL ES
   ├─ Mais complexo
   └─ Último alvo
```

### Metodologia por Jogo

```
Para cada jogo:

1. CARREGAR
   └─ ROM carrega? Ou erro?

2. INICIAR
   └─ CPU começa a executar?

3. BOOT
   └─ Passa da inicialização?

4. TELA
   └─ Mostra alguma imagem?

5. INPUT
   └─ Responde ao controle?

6. GAMEPLAY
   └─ É jogável?

7. COMPLETO
   └─ Funciona do início ao fim?

Cada etapa = progresso mensurável
```

---

## 🐛 Processo de Debug com Jogo

### Quando o Jogo Trava

```
Passo 1: Ver o log
└─ O que apareceu antes de travar?

Passo 2: Identificar a causa
├─ "Unimplemented API: X" → implementar X
├─ "Unknown instruction: Y" → implementar Y
├─ "Memory error at Z" → bug de memória
└─ Crash → usar GDB

Passo 3: Corrigir
└─ Implementar o que falta / corrigir bug

Passo 4: Testar novamente
└─ Progrediu? Ou trava em outro lugar?

Passo 5: Repetir
└─ Cada correção = mais progresso
```

### Exemplo de Sessão de Debug

```
Rodar Family Pack:
[LOG] Loading ROM... OK
[LOG] CPU starting at 0x1000
[LOG] Executing...
[WARN] Unimplemented API: IShell_CreateInstance
[ERROR] Crash!

Diagnóstico: falta IShell_CreateInstance

Ação: implementar IShell_CreateInstance

Rodar novamente:
[LOG] IShell_CreateInstance(DISPLAY) OK
[WARN] Unimplemented API: IDisplay_Update
...

Progresso! Agora falta IDisplay_Update.
Continue até rodar.
```

---

## 📊 Métricas de Progresso

### Tabela de Compatibilidade

```
Manter uma tabela:

┌──────────────────┬────────┬───────┬──────┬─────────┐
│ Jogo             │ Carrega│ Boota │ Tela │ Jogável │
├──────────────────┼────────┼───────┼──────┼─────────┤
│ Family Pack      │ ✅     │ ✅    │ ✅   │ ✅      │
│ Double Dragon    │ ✅     │ ✅    │ ⚠️   │ ❌      │
│ Crash Nitro Kart │ ✅     │ ❌    │ ❌   │ ❌      │
└──────────────────┴────────┴───────┴──────┴─────────┘

✅ = funciona
⚠️ = parcial/buggy
❌ = não funciona
```

### Estatísticas da CPU

```
Instruções:
├─ Implementadas: 45/150
├─ Testadas: 40/45
└─ Com bugs: 2

APIs BREW:
├─ Implementadas: 12
├─ Faltando (Family Pack): 3
├─ Faltando (Double Dragon): 8
└─ Faltando (Crash): 15
```

---

## 🎯 Estratégia de Testes por Fase

### Fase 1-2 (CPU)

```
Foco: Testes unitários
├─ Cada instrução testada
├─ ROMs de teste simples
└─ Validar correção
```

### Fase 3-4 (BREW/Gráficos)

```
Foco: Testes de integração
├─ APIs funcionam?
├─ Framebuffer atualiza?
└─ Áudio toca?
```

### Fase 5-6 (Jogos)

```
Foco: Jogos reais
├─ Rodar os 3 jogos
├─ Debugar cada trava
└─ Iterar até jogável
```

### Fase 7+ (Expansão)

```
Foco: Regression + novos jogos
├─ Novos jogos não quebram antigos?
├─ Testar lista de jogos
└─ Manter tabela de compatibilidade
```

---

## 🔄 Testes de Regressão

### O Que É

```
Garantir que código novo não quebra o antigo.

Cenário:
├─ Family Pack funcionava
├─ Você implementa algo para Double Dragon
├─ Family Pack ainda funciona?

Se quebrou = regressão (bug introduzido)
```

### Como Fazer

```
Antes de cada mudança grande:
1. Testar jogos que funcionam
2. Anotar estado (funciona)

Depois da mudança:
3. Testar os mesmos jogos
4. Ainda funcionam?

Se não: sua mudança quebrou algo.
```

---

## 🧰 Ferramentas de Teste

### Log Comparativo

```
Rodar jogo, salvar log:
./teste > log_antes.txt

Fazer mudança.

Rodar de novo:
./teste > log_depois.txt

Comparar:
diff log_antes.txt log_depois.txt
```

### Trace de Execução

```
Comparar com emulador de referência:

Seu emulador:  PC=0x1000, R0=5
QEMU:          PC=0x1000, R0=5   ✅ igual

Seu emulador:  PC=0x1008, R0=8
QEMU:          PC=0x1008, R0=7   ❌ DIFERENTE!

Achou o bug: na instrução em 0x1008
```

---

## 🎯 Checklist de Testes

```
Testes Unitários:
[ ] Todas instruções testadas
[ ] Memória testada
[ ] Flags testadas

ROMs de Teste:
[ ] teste_mov
[ ] teste_arithmetic
[ ] teste_memory
[ ] teste_loop
[ ] teste_function

Jogos:
[ ] Family Pack: carrega
[ ] Family Pack: boota
[ ] Family Pack: tela
[ ] Family Pack: jogável
[ ] Double Dragon: (progressão)
[ ] Crash: (progressão)

Regressão:
[ ] Jogos antigos ainda funcionam após mudanças
```

---

## 💡 Dicas de Teste

```
1. TESTE CEDO, TESTE SEMPRE
   └─ Não acumule código sem testar

2. ISOLE PROBLEMAS
   └─ Teste unitário antes de jogo completo

3. AUTOMATIZE
   └─ Script que roda todos os testes

4. DOCUMENTE FALHAS
   └─ Anote o que não funciona

5. COMPARE COM REFERÊNCIA
   └─ QEMU/Infuse para validar
```

---

## 🎯 Próximo Passo

Você sabe como testar! Últimos documentos:

→ **09_MAPEAMENTO_MEMORIAS.md** - Detalhes de endereços de memória
→ **10_INSTRUCOES_ARM.md** - Lista completa de instruções priorizadas

Estes são referências técnicas para consultar durante a implementação.
