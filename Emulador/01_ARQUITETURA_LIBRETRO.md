# 🎮 Arquitetura LibRetro - Núcleo Zeebo

> Como funciona a interface entre seu núcleo e o RetroArch

---

## 📌 O Que é LibRetro?

LibRetro é uma **interface padronizada** que permite que qualquer emulador rode em RetroArch.

```
Antes de LibRetro:
├─ Emulador SNES rodava só em SNES9X
├─ Emulador Genesis rodava só em Gens
├─ Emulador NES rodava só em FCEUX
└─ Cada um com interface diferente

Com LibRetro:
├─ Qualquer emulador ("núcleo") roda em RetroArch
├─ Mesma interface para todos
├─ RetroArch gerencia: menu, salvar, config, etc
└─ Núcleo só emula!
```

### Analogia

```
LibRetro é como um "conector USB universal"

SEM LibRetro:
├─ Você tem um "emulador standalone" (programa independente)
└─ Funciona sozinho, mas só você controla tudo

COM LibRetro:
├─ Seu "núcleo" é uma biblioteca (.dll/.so)
├─ RetroArch carrega essa biblioteca
├─ RetroArch controla (UI, menu, config)
└─ Você só emula!
```

---

## 🔌 Interface LibRetro - Funções Principais

Seu núcleo PRECISA implementar estas funções:

### 1. **Funções Obrigatórias**

```
retro_api_version()
  └─ Retorna versão da API LibRetro
  └─ Usado para compatibilidade
  └─ Seu núcleo diz: "eu sou v1.0 compatible"

retro_init()
  └─ Inicializar seu emulador
  └─ Alocar memória
  └─ Preparar tudo
  └─ Chamado 1x ao startup

retro_deinit()
  └─ Desligar seu emulador
  └─ Liberar memória
  └─ Salvar estado (se necessário)
  └─ Chamado 1x ao shutdown

retro_load_game(game_info)
  └─ Carregar ROM (arquivo .mod)
  └─ Parse do arquivo
  └─ Preparar execução
  └─ Retorna 1=sucesso, 0=erro

retro_unload_game()
  └─ Descarregar jogo atual
  └─ Limpar recursos
  └─ Preparar para próximo jogo

retro_run()
  └─ PRINCIPAL: Executar 1 frame
  └─ Chamado ~60x por segundo
  └─ Lê input
  └─ Executa CPU
  └─ Renderiza gráficos
  └─ Retorna resultado ao RetroArch

retro_reset()
  └─ Reset do jogo (como apertar botão reset)
  └─ Resetar CPU, memória, tudo
  └─ Manter ROM carregada
```

### 2. **Funções de Informação**

```
retro_get_system_info()
  └─ Retorna info do seu núcleo
  └─ Nome: "Zeebo"
  └─ Versão: "0.1"
  └─ Extensões: ".mod" (arquivo executável carregável -
      ".mif" é só metadado e NÃO deve estar nesta lista,
      ver nota abaixo)
  └─ Precisa full path? Sim/Não

retro_get_system_av_info()
  └─ Retorna informações audiovisuais
  └─ Resolução: 640x480
  └─ Aspect ratio: 4:3
  └─ FPS: 60
  └─ Sample rate áudio: 44100 Hz
```

### 3. **Callbacks - Como Enviar Dados ao RetroArch**

RetroArch passa funções para seu núcleo chamar:

```
retro_video_refresh_t
  └─ Seu núcleo chama: video_cb(framebuffer, width, height, pitch)
  └─ Envia imagem para RetroArch renderizar

retro_audio_sample_batch_t
  └─ Seu núcleo chama: audio_batch_cb(samples, frames)
  └─ Envia áudio para RetroArch tocar

retro_input_poll_t
  └─ Seu núcleo chama: input_poll_cb()
  └─ Diz ao RetroArch: "leia o input"

retro_input_state_t
  └─ Seu núcleo chama: input_state_cb(port, device, index, id)
  └─ Pergunta: "qual é o estado do botão X?"

retro_environment_t
  └─ Seu núcleo chama: environ_cb(cmd, data)
  └─ Comunica com RetroArch (comandos especiais)
```

---

## 🏗️ Fluxo de Execução

### Initialization (1x)

```
RetroArch inicia
  ↓
RetroArch carrega seu núcleo (.dll/.so)
  ↓
RetroArch chama retro_init()
  ├─ Você aloca memória
  ├─ Você inicializa CPU
  ├─ Você prepara estruturas
  └─ Você retorna OK

RetroArch mostra menu
  ↓
Usuário seleciona jogo
  ↓
RetroArch chama retro_load_game(path)
  ├─ Você carrega ROM do arquivo
  ├─ Você parser o formato MOD
  ├─ Você coloca código em memória
  └─ Você retorna sucesso/erro
```

### Main Loop (60x por segundo)

```
RetroArch while(running):
  ├─ Chama input_poll_cb()      (ler controle)
  │
  ├─ Chama retro_run()          (SUA FUNÇÃO PRINCIPAL)
  │  ├─ Executa ~700k instruções ARM (1/60 de segundo)
  │  ├─ Lê input (via input_state_cb)
  │  ├─ Atualiza memória
  │  ├─ Renderiza gráficos
  │  └─ Gera áudio
  │
  ├─ Você chama video_cb()      (envia frame)
  ├─ Você chama audio_batch_cb() (envia som)
  │
  └─ RetroArch mostra frame, toca som
```

### Shutdown (1x)

```
RetroArch fecha
  ↓
RetroArch chama retro_unload_game()
  ├─ Você descarrega ROM
  └─ Você limpa recursos
  
  ↓
RetroArch chama retro_deinit()
  ├─ Você libera memória
  └─ Você salva estado (opcional)
  
  ↓
Núcleo descarregado
```

---

## 📝 Exemplo: Estrutura Mínima do Núcleo

```c
// zeebo_core.c - Exemplo de estrutura

#include <libretro.h>

// Variáveis globais
static retro_video_refresh_t video_cb = NULL;
static retro_input_poll_t input_poll_cb = NULL;
static retro_input_state_t input_state_cb = NULL;
static retro_audio_sample_batch_t audio_batch_cb = NULL;
static retro_environment_t environ_cb = NULL;

// Seu emulador
static zeebo_emulator_t emulator;

// =====================================================
// RETRO_API_VERSION
// =====================================================
unsigned retro_api_version(void) {
    return RETRO_API_VERSION;  // Valor definido em libretro.h
}

// =====================================================
// RETRO_INIT
// =====================================================
void retro_init(void) {
    // 1. Alocar memória do emulador
    emulator.memory = malloc(256 * 1024 * 1024);  // 256MB
    
    // 2. Inicializar CPU
    cpu_init(&emulator.cpu, emulator.memory);
    
    // 3. Inicializar outros componentes
    gpu_init(&emulator.gpu);
    audio_init(&emulator.audio);
    
    log_info("Zeebo core initialized");
}

// =====================================================
// RETRO_DEINIT
// =====================================================
void retro_deinit(void) {
    if (emulator.memory) {
        free(emulator.memory);
        emulator.memory = NULL;
    }
    log_info("Zeebo core deinitalized");
}

// =====================================================
// RETRO_GET_SYSTEM_INFO
// =====================================================
void retro_get_system_info(struct retro_system_info *info) {
    info->library_name = "Zeebo";
    info->library_version = "0.1";
    info->valid_extensions = "mod";      // Só o executável BREW (.mif é metadado, não vai aqui)
    info->need_fullpath = false;         // Pode aceitar ROMs em memória
    info->block_extract = false;
}

// =====================================================
// RETRO_GET_SYSTEM_AV_INFO
// =====================================================
void retro_get_system_av_info(struct retro_system_av_info *info) {
    // Resolução
    info->geometry.base_width = 640;
    info->geometry.base_height = 480;
    info->geometry.max_width = 640;
    info->geometry.max_height = 480;
    info->geometry.aspect_ratio = 4.0f / 3.0f;  // 4:3
    
    // Timing
    info->timing.fps = 60.0;          // 60 frames por segundo
    info->timing.sample_rate = 44100.0;  // 44.1 kHz áudio
}

// =====================================================
// RETRO_LOAD_GAME
// =====================================================
bool retro_load_game(const struct retro_game_info *info) {
    if (!info || !info->path) {
        log_error("No game path provided");
        return false;
    }
    
    log_info("Loading game: %s", info->path);
    
    // 1. Carregar ROM do arquivo
    if (rom_loader_load(&emulator, info->path) != 0) {
        log_error("Failed to load ROM");
        return false;
    }
    
    // 2. Preparar execução
    emulator.cpu.pc = emulator.rom.entry_point;
    
    log_info("Game loaded successfully");
    return true;
}

// =====================================================
// RETRO_RUN - FUNÇÃO PRINCIPAL!
// =====================================================
void retro_run(void) {
    // 1. Ler input
    input_poll_cb();  // Diz ao RetroArch: leia o controle
    
    uint16_t joypad = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
    if (joypad) {
        log_debug("Button A pressed");
    }
    
    // 2. Executar CPU por 1 frame (~700k instruções)
    cpu_run(&emulator.cpu, CYCLES_PER_FRAME);
    
    // 3. Renderizar gráficos
    gpu_render(&emulator.gpu, emulator.memory);
    
    // 4. Gerar áudio
    audio_generate(&emulator.audio);
    
    // 5. Enviar resultado ao RetroArch
    
    // Vídeo
    video_cb(emulator.gpu.framebuffer, 640, 480, 640 * 4);
    
    // Áudio
    audio_batch_cb(emulator.audio.buffer, emulator.audio.samples);
}

// =====================================================
// CALLBACKS - RetroArch passa essas funções
// =====================================================
void retro_set_video_refresh(retro_video_refresh_t cb) {
    video_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb) {
    input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb) {
    input_state_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
    audio_batch_cb = cb;
}

void retro_set_environment(retro_environment_t cb) {
    environ_cb = cb;
}

// ... mais funções padrão (reset, save state, etc)
```

---

## 🎯 Resumo: O Que Seu Núcleo Precisa Fazer

```
RETRO_INIT
  └─ Prepare-se para receber um jogo

RETRO_LOAD_GAME
  └─ Carregue a ROM (arquivo .mod)

RETRO_RUN (chamado 60x/segundo)
  └─ Execute 1 frame:
     ├─ Leia input
     ├─ Execute CPU
     ├─ Renderize gráficos
     ├─ Gere áudio
     └─ Envie tudo ao RetroArch

RETRO_UNLOAD_GAME
  └─ Descarregue a ROM

RETRO_DEINIT
  └─ Limpe e feche
```

---

## 🔗 Conexão com RetroArch

```
RetroArch (processo)
  │
  ├─ Carrega seu núcleo (zeebo_core.dll)
  │
  ├─ Chama retro_init()
  │
  ├─ Pede para usuário escolher jogo
  │
  ├─ Chama retro_load_game("jogo.mod")
  │
  ├─ Loop principal:
  │  ├─ Chama retro_run()
  │  │  └─ Seu núcleo executa 1 frame
  │  │
  │  └─ Você chama video_cb() (envia imagem)
  │  └─ Você chama audio_batch_cb() (envia som)
  │
  ├─ RetroArch mostra imagem e toca som
  │
  └─ Loop continua até fechar
```

---

## 💾 Estrutura de Dados Esperada

LibRetro passa informações así:

```c
struct retro_game_info {
    const char *path;          // "/caminho/para/jogo.mod"
    const void *data;          // Dados em memória (ou NULL)
    size_t size;               // Tamanho dos dados
    const char *meta;          // Metadata (opcional)
};

struct retro_system_info {
    const char *library_name;     // "Zeebo"
    const char *library_version;  // "0.1"
    const char *valid_extensions; // "mod"
    bool need_fullpath;           // Precisa do path completo?
    bool block_extract;           // Não extrair archives?
};

struct retro_system_av_info {
    retro_game_geometry geometry;
    retro_system_timing timing;
};
```

---

## 🎮 Fluxo Completo com Timings

```
┌─────────────────────────────────────────────────────────┐
│ RetroArch Initializes                                   │
│ └─ Loads zeebo_core.dll                                 │
│    └─ retro_api_version() → 1                           │
│    └─ retro_init() → allocates memory                   │
│    └─ retro_get_system_info() → "Zeebo 0.1"           │
└─────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────┐
│ User Selects Game (Menu)                                │
│ └─ Clicks "Load ROM"                                    │
│    └─ Selects "game.mod"                                │
└─────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────┐
│ retro_load_game("game.mod")                             │
│ └─ Your core:                                           │
│    ├─ Opens file                                        │
│    ├─ Parses MOD format                                 │
│    ├─ Loads code to memory                              │
│    ├─ Sets PC to entry point                            │
│    └─ Returns true                                      │
│                                                          │
│ retro_get_system_av_info()                              │
│ └─ Returns: 640x480, 60fps, 44100Hz                    │
└─────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────┐
│ Main Loop (RUNS FOREVER UNTIL QUIT)                     │
│                                                          │
│ for each frame (60x per second):                        │
│   ├─ retro_run() called                                 │
│   │  ├─ input_poll_cb()       (poll input)              │
│   │  ├─ Your CPU executes ~700k instructions           │
│   │  ├─ Your GPU renders to framebuffer                 │
│   │  ├─ Your audio generates samples                    │
│   │  ├─ video_cb(framebuffer)  (display frame)          │
│   │  └─ audio_batch_cb(audio)  (play sound)             │
│   │                                                      │
│   └─ RetroArch:                                         │
│      ├─ Displays frame on screen                        │
│      ├─ Plays audio through speakers                    │
│      └─ Handles menu, savestates, etc                   │
└─────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────┐
│ User Quits Game                                          │
│                                                          │
│ retro_unload_game()                                     │
│ └─ Your core cleans up ROM                              │
│                                                          │
│ retro_deinit()                                          │
│ └─ Your core:                                           │
│    ├─ Frees memory                                      │
│    ├─ Closes files                                      │
│    └─ Cleans up                                         │
│                                                          │
│ zeebo_core.dll unloaded from memory                     │
└─────────────────────────────────────────────────────────┘
```

---

## ⚙️ O Que RetroArch Gerencia

Você NÃO precisa implementar:

```
✅ Menu (RetroArch faz)
✅ Salvar/Carregar Estados (RetroArch pode fazer)
✅ Configurações (RetroArch gerencia)
✅ Screenshots (RetroArch captura)
✅ Rewind/Fast-forward (RetroArch controla)
✅ Mapping de controle (RetroArch mapeia)
✅ Renderização final (RetroArch mostra na tela)
✅ Reprodução de áudio (RetroArch toca)

❌ Você SÓ precisa fazer:
   ├─ Emular CPU
   ├─ Renderizar para framebuffer
   ├─ Gerar amostras de áudio
   ├─ Ler estado de input
   └─ Comunicar via callbacks
```

---

## 🎯 Próximos Passos

Agora que você entende LibRetro:

1. Leia **ARQUITETURA_ZEEBO.md** (próximo)
   - Entender o hardware que estamos emulando

2. Depois **PLANO_DESENVOLVIMENTO.md**
   - Timeline de como implementar

3. Depois **Documentos técnicos**
   - Como implementar CPU, APIs, etc

Quer que crie esses docs? 🚀
