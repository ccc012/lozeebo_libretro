# 🗺️ Mapeamento de Memória - Referência Técnica

> Como organizar a memória emulada do Zeebo

---

## 📌 Conceito: Espaço de Endereçamento

```
A CPU ARM acessa memória por endereços de 32 bits.
Isso dá um espaço de 4GB (0x00000000 a 0xFFFFFFFF).

Você organiza esse espaço em regiões:
├─ Onde fica o código
├─ Onde ficam os dados
├─ Onde fica a stack
├─ Onde fica o heap
└─ Regiões especiais (HLE)
```

**IMPORTANTE:** Este é um mapa de DESIGN para HLE. O hardware real do Zeebo tem seu próprio mapa físico, mas em HLE você tem liberdade para organizar como fizer sentido.

---

## 🏗️ Mapa de Memória Proposto (HLE)

```
┌─────────────┬─────────────┬──────────────────────────────┐
│ Início      │ Fim         │ Uso                          │
├─────────────┼─────────────┼──────────────────────────────┤
│ 0x00000000  │ 0x00000FFF  │ Vetores/reservado (4KB)      │
│ 0x00001000  │ 0x00FFFFFF  │ Código do jogo (~16MB)       │
│ 0x01000000  │ 0x0FFFFFFF  │ Dados do jogo (~240MB)       │
│ 0x10000000  │ 0x1FFFFFFF  │ Heap (256MB)                 │
│ 0x20000000  │ 0x2FFFFFFF  │ Stack (256MB, cresce ↓)      │
│ 0x30000000  │ 0x3FFFFFFF  │ VRAM/Framebuffer             │
│ ...         │ ...         │ (livre)                      │
│ 0xF0000000  │ 0xFFFFFFFF  │ Região HLE (trap de APIs)    │
└─────────────┴─────────────┴──────────────────────────────┘

Total alocado fisicamente: você não precisa alocar 4GB!
Aloca só o que usa (ex: 256MB) e mapeia as regiões.
```

---

## 📦 Detalhamento de Cada Região

### Vetores (0x00000000)

```
No ARM real, aqui ficam os "vetores de exceção":
├─ Reset
├─ Undefined instruction
├─ Software interrupt (SWI)
├─ Prefetch abort
├─ Data abort
├─ IRQ
└─ FIQ

Para HLE, você pode:
├─ Deixar vazio (não usar exceções reais)
└─ Ou implementar handlers simples
```

### Código do Jogo (0x00001000)

```
Aqui vai o executável do jogo (arquivo MOD).

Entry point típico: 0x1000
(onde a CPU começa a executar)

O loader:
├─ Lê o MOD
├─ Copia código para cá
└─ Seta PC = 0x1000
```

### Dados do Jogo

```
Dados que o jogo usa:
├─ Constantes (.rodata)
├─ Variáveis globais (.data)
└─ Variáveis zeradas (.bss)

Geralmente logo após o código.
```

### Heap (0x10000000)

```
Memória dinâmica (MALLOC/FREE).

Quando jogo chama MALLOC:
├─ Você pega um pedaço livre do heap
├─ Retorna o endereço
└─ Marca como usado

Precisa de um "alocador" (heap manager).
```

### Stack (0x20000000)

```
Pilha de execução.

IMPORTANTE: Stack cresce PARA BAIXO!
├─ SP começa no TOPO (ex: 0x2FFFFFFF)
├─ PUSH: SP diminui
└─ POP: SP aumenta

Usado para:
├─ Chamadas de função (retorno)
├─ Variáveis locais
└─ Salvar registradores
```

### VRAM/Framebuffer (0x30000000)

```
Onde ficam os pixels da tela.

Framebuffer 640x480:
├─ 640 * 480 = 307200 pixels
├─ 4 bytes por pixel (RGBA)
└─ Total: ~1.2MB

O jogo desenha aqui (via APIs).
Você envia para o RetroArch.
```

### Região HLE (0xF0000000)

```
Endereços "mágicos" para interceptar APIs.

Quando jogo chama uma API:
├─ Ele "pula" para um endereço aqui (ex: 0xF0000001)
├─ Você detecta: PC >= 0xF0000000
├─ Identifica qual API pelo endereço
└─ Emula o comportamento

Estes endereços NÃO têm código real.
São sinais para o emulador.
```

---

## 🔧 Como Implementar (Conceitual)

### Alocação Física

```
Você não aloca 4GB!

Aloca só o necessário:
memoria = malloc(256 * 1024 * 1024);  // 256MB físico

E mapeia endereços virtuais para esse buffer.
```

### Tradução de Endereços

```
Endereço virtual → posição no buffer

Se código está em 0x1000 e você alocou buffer:
posição_real = endereço_virtual - base

Exemplo:
├─ Jogo acessa 0x1004
├─ Base do código é 0x1000
├─ Posição no buffer: 0x1004 - 0x1000 = 4
└─ Acessa memoria[4]
```

### Detecção de Região

```
Ao acessar memória, verificar a região:

função acessar_memoria(endereco) {
    if (endereco >= 0xF0000000) {
        // Região HLE - interceptar!
        return tratar_api(endereco);
    }
    else if (endereco >= 0x20000000) {
        // Stack
        return stack[endereco - 0x20000000];
    }
    else if (endereco >= 0x10000000) {
        // Heap
        return heap[endereco - 0x10000000];
    }
    else {
        // Código/Dados
        return memoria[endereco];
    }
}
```

---

## 📊 Heap Manager (Alocador)

### O Que Faz

```
Gerencia MALLOC/FREE do jogo.

Quando jogo pede memória:
├─ MALLOC(1000) → "me dê 1000 bytes"
├─ Você acha espaço livre
├─ Retorna endereço
└─ Marca como ocupado

Quando jogo libera:
├─ FREE(endereço)
└─ Marca como livre
```

### Estratégia Simples

```
Alocador linear básico:

heap_atual = 0x10000000;  // início do heap

MALLOC(tamanho) {
    endereco = heap_atual;
    heap_atual += tamanho;
    return endereco;
}

FREE(endereco) {
    // Versão simples: não faz nada
    // (memória "vaza" mas funciona para começar)
}

Depois, implementar FREE real (lista de blocos livres).
```

### Estratégia Avançada (Depois)

```
Alocador com lista de blocos:
├─ Rastrear blocos livres e ocupados
├─ MALLOC procura bloco livre adequado
├─ FREE devolve à lista de livres
├─ Merge de blocos adjacentes
└─ Mais eficiente
```

---

## 🎯 Stack Management

### Como Funciona

```
Stack cresce para baixo.

Inicialização:
SP = 0x2FFFFFFF  // topo da stack

PUSH R0:
SP = SP - 4
memoria[SP] = R0

POP R0:
R0 = memoria[SP]
SP = SP + 4
```

### Uso em Funções

```
Quando função é chamada (BL):
├─ LR = endereço de retorno
├─ Função salva registradores na stack (PUSH)
├─ Função faz seu trabalho
├─ Função restaura registradores (POP)
└─ Retorna (BX LR ou POP PC)
```

### Exemplo

```
Chamada de função:

BL minha_funcao        ; LR = retorno

minha_funcao:
    PUSH {R4, R5, LR}  ; salvar registradores
    ; ... trabalho ...
    POP {R4, R5, PC}   ; restaurar e retornar
```

---

## ⚠️ Cuidados com Memória

### Bounds Checking

```
DECISÃO PARA ESTE PROJETO (consistente com 04_EMULACAO_CPU_DETALHADA.md):
Nunca travar o núcleo por causa de acesso inválido.

função ler_memoria(endereco) {
    if (endereco >= tamanho_total) {
        log_warn("Acesso de leitura fora dos limites: 0x%X (PC=0x%X)",
                 endereco, cpu.pc);
        return 0;  // sempre retorna 0, nunca trava
    }
    return memoria[endereco];
}

função escrever_memoria(endereco, valor) {
    if (endereco >= tamanho_total) {
        log_warn("Acesso de escrita fora dos limites: 0x%X (PC=0x%X)",
                 endereco, cpu.pc);
        return;  // ignora a escrita, não trava
    }
    memoria[endereco] = valor;
}

Isso pega bugs (o log mostra onde) sem derrubar o emulador.
```

### Alinhamento

```
DECISÃO PARA ESTE PROJETO (consistente com 04_EMULACAO_CPU_DETALHADA.md):
Acesso desalinhado (ex: LDR de 32 bits em endereço não múltiplo
de 4) é PERMITIDO, não rejeitado.

├─ O acesso é feito normalmente, byte a byte, mesmo desalinhado
├─ Loga um aviso na primeira ocorrência de cada endereço
    (para não inundar o log se acontecer toda hora)
└─ NÃO implementa o comportamento de "rotação de valor" que o
    hardware ARM real faz em alguns modos - é um caso raro e
    não esperado nos 3 jogos-alvo (Crash Nitro Kart, Double
    Dragon, Zeebo Family Pack)

Motivo: simplicidade agora, sem perder visibilidade via log
caso isso realmente aconteça e cause bugs visuais/lógicos.
```

### Endianness

```
ARM é little-endian.

Valor 0x12345678 na memória:
├─ byte[0] = 0x78 (menos significativo)
├─ byte[1] = 0x56
├─ byte[2] = 0x34
└─ byte[3] = 0x12 (mais significativo)

Read32 deve montar corretamente.
```

---

## 📋 Regiões Especiais BREW

### Class IDs

```
Cada interface BREW tem um "class ID" (número único):

AEECLSID_SHELL    = 0x01001000 (exemplo)
AEECLSID_DISPLAY  = 0x01001001
AEECLSID_GRAPHICS = 0x01001002
...

(Os valores reais vêm da documentação BREW)

Quando jogo pede uma interface por class ID,
você identifica qual e retorna sua implementação.
```

### Ponteiros de Interface

```
Quando você cria uma interface, retorna um ponteiro.

Esse ponteiro aponta para uma struct na memória emulada:

Interface {
    vtable_ptr  → aponta para tabela de métodos
    dados...
}

O jogo usa esse ponteiro para chamar métodos.
```

---

## 🗺️ Mapa Visual Completo

```
0xFFFFFFFF ┌──────────────────────┐
           │   Região HLE          │ ← APIs interceptadas
0xF0000000 ├──────────────────────┤
           │   (livre)             │
0x40000000 ├──────────────────────┤
           │   VRAM/Framebuffer    │ ← Pixels da tela
0x30000000 ├──────────────────────┤
           │                       │
           │   Stack (cresce ↓)    │ ← Pilha
           │        ↓              │
0x20000000 ├──────────────────────┤
           │        ↑              │
           │   Heap (cresce ↑)     │ ← MALLOC/FREE
           │                       │
0x10000000 ├──────────────────────┤
           │   Dados do jogo       │ ← Variáveis
0x01000000 ├──────────────────────┤
           │   Código do jogo      │ ← Instruções
0x00001000 ├──────────────────────┤
           │   Vetores/reservado   │
0x00000000 └──────────────────────┘
```

---

## 🎯 Checklist de Memória

```
Estrutura:
[ ] Definir mapa de memória
[ ] Alocar buffer físico
[ ] Tradução endereço virtual → real

Acesso:
[ ] Read8/16/32
[ ] Write8/16/32
[ ] Little-endian correto
[ ] Bounds checking

Heap:
[ ] MALLOC básico
[ ] FREE básico
[ ] (depois) FREE com lista de blocos

Stack:
[ ] SP inicializado no topo
[ ] PUSH/POP
[ ] Cresce para baixo

Regiões:
[ ] Detecção de região HLE
[ ] Framebuffer mapeado
```

---

## 🎯 Próximo Passo

Último documento de referência:

→ **10_INSTRUCOES_ARM.md** - Lista completa de instruções ARM priorizadas

Você entende a memória. O último doc lista todas instruções a implementar.
