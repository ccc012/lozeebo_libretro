# 🔌 Emulação de APIs BREW - Detalhada

> Como emular o "sistema operacional" BREW que os jogos usam

---

## 📌 Por Que Isso é Necessário

```
A CPU executa o código do jogo.
Mas o jogo não desenha pixels diretamente.
O jogo CHAMA APIs do BREW para tudo:

├─ "BREW, desenhe um retângulo aqui"
├─ "BREW, toque este som"
├─ "BREW, leia este arquivo"
├─ "BREW, me dê memória"
└─ "BREW, qual botão foi pressionado?"

Você precisa RESPONDER a essas chamadas.
Isso é HLE (High-Level Emulation).
```

---

## 🎭 HLE vs LLE (Relembrando)

```
LLE (Low-Level Emulation):
├─ Emular o BREW real, instrução por instrução
├─ Precisa da ROM do BREW da Qualcomm
├─ Não disponível/legal
└─ ❌ NÃO É O CAMINHO

HLE (High-Level Emulation):
├─ Você REIMPLEMENTA as APIs BREW
├─ Quando jogo chama "desenhar", você desenha
├─ Não precisa da ROM do BREW
├─ É o que Infuse faz
└─ ✅ ESTE É O CAMINHO
```

---

## 🧩 Modelo de Interface do BREW

### Como o BREW Organiza APIs

```
BREW usa um modelo tipo COM (Component Object Model).

Cada "serviço" é uma INTERFACE:
├─ IShell    : gerenciador central
├─ IDisplay  : tela
├─ IGraphics : desenho 2D
├─ ISound    : áudio
├─ IFile     : arquivos
└─ ... muitas outras

Cada interface tem MÉTODOS (funções).
```

### Como o Jogo Usa uma Interface

```
Passo 1: Jogo pede a interface ao Shell
   IShell_CreateInstance(shell, AEECLSID_DISPLAY, &pDisplay);
   
Passo 2: BREW retorna um ponteiro
   pDisplay agora aponta para a interface IDisplay

Passo 3: Jogo chama métodos via vtable
   IDISPLAY_Update(pDisplay);
   // Internamente: pDisplay->vtable->Update(pDisplay);
```

### O Que é uma vtable

```
vtable (virtual table) = tabela de ponteiros de função

Uma interface é essencialmente:

Interface {
    vtable*  → [ ponteiro para Método1 ]
                [ ponteiro para Método2 ]
                [ ponteiro para Método3 ]
                ...
    dados internos
}

Quando jogo chama "Método2", ele:
1. Pega o ponteiro da vtable[1]
2. Chama essa função
```

---

## 🎯 Como Interceptar Chamadas (O Truque do HLE)

### A Estratégia

```
Você cria vtables FALSAS que apontam para SEU código.

Quando o jogo chama IDISPLAY_Update:
├─ Ele segue o ponteiro na vtable
├─ Mas esse ponteiro aponta para SEU código
└─ SEU código emula o comportamento
```

### Mecanismo de Trap

```
Método 1: Endereços Mágicos
├─ Você coloca endereços "impossíveis" na vtable (ex: 0xF0000001)
├─ Quando CPU tenta executar nesse endereço, você intercepta
├─ Você identifica qual API pelo endereço
└─ Executa a emulação

Método 2: Instrução Especial
├─ Você coloca uma instrução SWI (software interrupt) 
├─ Cada API tem um número SWI diferente
├─ Quando CPU executa SWI, você intercepta
└─ Executa a emulação
```

### Decisão de Escopo: Qual Método Usar

```
DECISÃO PARA ESTE PROJETO: Método 1 (Endereços Mágicos).

Motivos:
├─ Mais simples de implementar no loop principal do fetch
│  (é só um "if PC >= faixa_reservada", sem precisar decodificar
│  mais um tipo de instrução)
├─ Não depende de reconhecer corretamente o encoding de SWI/SVC
│  em ambos os modos ARM e Thumb (que têm encodings diferentes)
├─ Já é o modelo mais comum usado por outros emuladores HLE de
│  plataformas com vtables (ex: emuladores de consoles com BIOS
│  baseada em COM/interfaces)

Isso significa:
[ ] Reservar uma faixa de endereços que NUNCA existe na RAM real
    do Zeebo para servir de "endereço mágico" (ver
    09_MAPEAMENTO_MEMORIAS.md para a faixa exata reservada)
[ ] SWI/SVC continua sendo decodificado (ver 04 e 10), mas seu
    uso na prática deve ser raro - se aparecer, logar e tratar
    como não implementado, não como parte do trap principal
```

### Exemplo Conceitual (Endereços Mágicos)

```
Setup:
├─ Criar vtable do IDisplay
├─ vtable[Update] = 0xF0000001  (endereço mágico)
├─ vtable[DrawRect] = 0xF0000002
└─ etc

No loop da CPU:
if (PC >= 0xF0000000) {
    // É uma chamada de API!
    api_id = PC - 0xF0000000;
    
    switch (api_id) {
        case 1: emular_IDisplay_Update(); break;
        case 2: emular_IDisplay_DrawRect(); break;
        // ...
    }
    
    // Retornar da "função"
    PC = LR;  // volta pro jogo
}
```

---

## 📋 APIs Essenciais (Para os 3 Primeiros Jogos)

### 1. IShell (O Mais Importante)

```
IShell é o gerenciador central. Tudo passa por ele.

Métodos essenciais:
├─ CreateInstance : criar outras interfaces
├─ GetDeviceInfo  : informações do dispositivo
├─ SetTimer       : agendar callbacks
└─ GetPosition    : posição/tempo

CreateInstance é o mais crítico:
Quando jogo pede uma interface, você:
1. Identifica qual (pelo class ID)
2. Cria sua implementação HLE
3. Retorna ponteiro
```

### 2. Memória (MALLOC/FREE)

```
Jogos alocam memória constantemente.

Métodos:
├─ MALLOC  : alocar N bytes
├─ FREE    : liberar
├─ REALLOC : redimensionar
├─ MEMSET  : preencher
└─ MEMCPY  : copiar

Emulação:
├─ Você mantém um heap na memória emulada
├─ MALLOC retorna endereço livre
├─ FREE marca como livre
└─ Simples gerenciador de heap
```

### 3. IDisplay / IGraphics (Desenho)

```
Para o jogo aparecer na tela.

IDisplay:
├─ Update      : atualizar tela (mostrar o que foi desenhado)
├─ FillRect    : preencher retângulo
├─ SetColor    : cor atual
└─ ClearScreen : limpar

IGraphics:
├─ DrawRect    : desenhar retângulo
├─ DrawLine    : desenhar linha
├─ DrawPixel   : desenhar pixel
└─ DrawBitmap  : desenhar imagem

Emulação:
├─ Você mantém um framebuffer (640x480)
├─ Cada método desenha no framebuffer
└─ Update envia framebuffer ao RetroArch
```

### 4. IBitmap (Imagens)

```
Para sprites e imagens.

Métodos:
├─ CreateBitmap : criar bitmap
├─ LoadBitmap   : carregar de arquivo
├─ BltIn        : desenhar (blit)
└─ GetInfo      : dimensões

Emulação:
├─ Carregar dados de imagem
├─ Blit = copiar pixels para framebuffer
└─ Suportar transparência
```

### 5. IFile / IFileMgr (Arquivos)

```
Para carregar assets (imagens, sons, dados).

IFileMgr:
├─ OpenFile   : abrir arquivo
├─ Remove     : deletar
└─ Test       : existe?

IFile:
├─ Read       : ler dados
├─ Write      : escrever
├─ Seek       : mover cursor
└─ Release    : fechar

Emulação:
├─ Mapear "arquivos BREW" para arquivos reais
├─ Ou para dados dentro do BAR
└─ Read/Write em buffers
```

### 6. ISound (Áudio)

```
Para efeitos sonoros.

Métodos:
├─ Play        : tocar som
├─ Stop        : parar
├─ SetVolume   : volume
└─ RegisterNotify : callback

Emulação:
├─ Decodificar formato (PCM primeiro)
├─ Adicionar ao mixer
└─ Enviar ao RetroArch
```

---

## 🎯 Como Descobrir Quais APIs um Jogo Usa

### Método 1: Logging de "Não Implementado"

```
Quando jogo chama API que você não tem:

log_warn("API não implementada: class=0x%X, method=%d", class_id, method);

Rode o jogo, colете os logs, veja o que falta.
```

### Método 2: Análise Estática (Reverse Engineering)

```
Usar Ghidra/IDA no arquivo MOD:
├─ Procurar chamadas a IShell_CreateInstance
├─ Ver quais class IDs são pedidos
└─ Saber antecipadamente quais APIs implementar
```

### Método 3: Estudar o MIF

```
O arquivo MIF lista "extensões requeridas":
├─ Quais interfaces o jogo precisa
└─ Dica de quais APIs implementar
```

---

## 📊 APIs por Complexidade

```
┌─────────────────┬─────────────┬──────────────┐
│ API             │ Dificuldade │ Quando       │
├─────────────────┼─────────────┼──────────────┤
│ IShell          │ Média       │ Fase 3       │
│ MALLOC/FREE     │ Baixa       │ Fase 3       │
│ IFile           │ Baixa       │ Fase 3       │
│ IDisplay        │ Média       │ Fase 4       │
│ IGraphics       │ Média       │ Fase 4       │
│ IBitmap         │ Média       │ Fase 4       │
│ ISound (PCM)    │ Média       │ Fase 4       │
│ ISound (MIDI)   │ Alta        │ Fase 6+      │
│ OpenGL ES       │ Muito Alta  │ Fase 6+      │
│ INet            │ Alta        │ Fase 7+      │
└─────────────────┴─────────────┴──────────────┘
```

---

## 🔧 Estrutura de Implementação de uma API

### Modelo Conceitual

```
Para cada API, você precisa:

1. DEFINIR a interface (quais métodos)
2. CRIAR a vtable falsa (aponta pro seu código)
3. IMPLEMENTAR cada método (a emulação)
4. REGISTRAR no CreateInstance (para o jogo pegar)
```

### Exemplo Conceitual: IDisplay

```
// Definir métodos
enum IDisplay_Methods {
    IDISPLAY_Update = 0,
    IDISPLAY_FillRect = 1,
    IDISPLAY_SetColor = 2,
};

// Implementar
void emular_IDisplay_Update() {
    // Enviar framebuffer para RetroArch
    video_cb(framebuffer, 640, 480, pitch);
}

void emular_IDisplay_FillRect() {
    // Ler argumentos (x, y, w, h) dos registradores
    x = cpu.R[0];
    y = cpu.R[1];
    w = cpu.R[2];
    h = cpu.R[3];
    
    // Preencher no framebuffer
    for cada pixel em (x,y,w,h):
        framebuffer[pixel] = cor_atual;
}

// Registrar
void IShell_CreateInstance(class_id) {
    if (class_id == AEECLSID_DISPLAY) {
        criar_vtable_IDisplay();
        return ponteiro_para_IDisplay;
    }
}
```

---

## 🎯 Convenção de Chamada (Como Ler Argumentos)

### ARM Calling Convention

```
Quando jogo chama uma função/API:

Argumentos vão nos registradores:
├─ R0 = primeiro argumento
├─ R1 = segundo argumento
├─ R2 = terceiro argumento
├─ R3 = quarto argumento
└─ (mais argumentos vão na stack)

Valor de retorno:
└─ R0 = resultado

Então, ao emular uma API:
├─ Ler argumentos de R0, R1, R2, R3
├─ Fazer a emulação
└─ Colocar resultado em R0
```

### Exemplo

```
Jogo chama: IDISPLAY_FillRect(pDisplay, x, y, w, h)

Nos registradores:
├─ R0 = pDisplay (this pointer)
├─ R1 = x
├─ R2 = y
├─ R3 = w
└─ [stack] = h

Sua emulação:
x = cpu.R[1];
y = cpu.R[2];
w = cpu.R[3];
h = ler_da_stack();

fill_rect(x, y, w, h);
```

---

## 🐛 Debugando APIs

### Log Detalhado

```
Para cada chamada de API:

log_debug("IDisplay_FillRect(x=%d, y=%d, w=%d, h=%d)",
          x, y, w, h);

Assim você vê exatamente o que o jogo está pedindo.
```

### Comparar com Comportamento Esperado

```
Se a tela está errada:
├─ Ver quais FillRect foram chamados
├─ Comparar com o que deveria aparecer
└─ Encontrar a discrepância
```

---

## 📋 Roadmap de Implementação de APIs

### Fase 3 (BREW Básico)
```
[ ] Sistema de trap (interceptar chamadas)
[ ] IShell_CreateInstance
[ ] MALLOC/FREE
[ ] IFile (Open/Read/Close)
[ ] Applet lifecycle (load, event loop)
```

### Fase 4 (Gráficos/Áudio)
```
[ ] IDisplay (Update, FillRect)
[ ] IGraphics (DrawRect, DrawLine)
[ ] IBitmap (Load, Blit)
[ ] ISound (Play PCM)
```

### Fase 5-6 (Refinamento)
```
[ ] APIs específicas dos 3 jogos
[ ] Corrigir edge cases
[ ] Quirks por jogo
```

### Fase 7+ (Expansão)
```
[ ] OpenGL ES (3D)
[ ] MIDI synthesis
[ ] INet
[ ] APIs raras
```

---

## ⚠️ Desafios Específicos

```
1. DESCOBRIR ASSINATURAS
   └─ Nem sempre é claro quais argumentos uma API recebe
   └─ Precisa de reverse engineering ou documentação

2. COMPORTAMENTO EXATO
   └─ Uma API pode ter comportamento sutil
   └─ Testar e comparar com hardware/Infuse

3. ESTADO INTERNO
   └─ Algumas APIs mantêm estado
   └─ Você precisa rastrear isso

4. CALLBACKS
   └─ Jogo registra callbacks (ex: timer)
   └─ Você precisa chamá-los na hora certa

5. PONTEIROS
   └─ Argumentos podem ser ponteiros para structs
   └─ Você precisa ler a memória emulada
```

---

## 🎯 Checklist de APIs

```
Sistema:
[ ] Trap mechanism
[ ] IShell_CreateInstance
[ ] Class ID routing

Memória:
[ ] MALLOC
[ ] FREE
[ ] REALLOC
[ ] MEMSET/MEMCPY

Arquivos:
[ ] IFileMgr_OpenFile
[ ] IFile_Read
[ ] IFile_Write
[ ] IFile_Close

Display:
[ ] IDisplay_Update
[ ] IDisplay_FillRect
[ ] IGraphics_DrawRect
[ ] IGraphics_DrawLine

Bitmap:
[ ] IBitmap_Create
[ ] IBitmap_Load
[ ] IBitmap_Blt

Som:
[ ] ISound_Play (PCM)
[ ] ISound_Stop
[ ] Mixer

Applet:
[ ] AEEMod_Load
[ ] Event handler
[ ] Event loop
```

---

## 🎯 Próximo Passo

Agora você entende como emular as APIs BREW. Próximo:

→ **06_TOOLCHAIN_SETUP.md** - Ferramentas e ambiente de desenvolvimento

Você sabe O QUE fazer (CPU + APIs). Agora precisa das ferramentas para FAZER.
