# 🏗️ Protótipo / Skeleton - Guia Passo a Passo

> O primeiro código real do projeto: um núcleo vazio que compila e o RetroArch reconhece

---

## 📌 Objetivo Deste Documento

```
Todos os outros documentos explicam TEORIA.
Este documento é PRÁTICO: comando por comando.

No final dele, você terá:
├─ Uma pasta de projeto criada
├─ Um Makefile funcional
├─ Um núcleo "vazio" (todas funções retro_* existem, mas não fazem nada)
├─ O núcleo compilado (.so no Linux, .dll no Windows)
└─ RetroArch mostrando "Zeebo" na lista de núcleos

Isso é o marco final da SEMANA 1-2 do plano de desenvolvimento.
```

---

## 🗂️ Passo 1: Criar a Estrutura de Pastas

### Comandos (Linux/Mac - terminal)

```bash
mkdir zeebo_libretro
cd zeebo_libretro

mkdir -p src/core
mkdir -p src/cpu
mkdir -p src/memory
mkdir -p src/loader
mkdir -p src/brew
mkdir -p src/gpu
mkdir -p src/audio
mkdir -p src/input
mkdir -p src/debug
mkdir -p include
mkdir -p tests/roms
mkdir -p docs
mkdir -p build
```

### Comandos (Windows - MSYS2 ou WSL2)

```bash
# Mesmos comandos acima funcionam no MSYS2/WSL2
# Se estiver no PowerShell puro, use:
mkdir zeebo_libretro
cd zeebo_libretro
mkdir src\core, src\cpu, src\memory, src\loader, src\brew, src\gpu, src\audio, src\input, src\debug, include, tests\roms, docs, build
```

### Resultado Esperado

```
zeebo_libretro/
├── src/
│   ├── core/
│   ├── cpu/
│   ├── memory/
│   ├── loader/
│   ├── brew/
│   ├── gpu/
│   ├── audio/
│   ├── input/
│   └── debug/
├── include/
├── tests/roms/
├── docs/
└── build/
```

Isso corresponde exatamente à estrutura descrita em `07_ESTRUTURA_PASTA.md`.

---

## 📥 Passo 2: Baixar o Header libretro.h

### O Que É

```
libretro.h é o "contrato" entre seu núcleo e o RetroArch.
Sem ele, você não consegue compilar nada.
```

### Como Baixar

```bash
cd include

# Baixar direto do repositório oficial
curl -O https://raw.githubusercontent.com/libretro/libretro-common/master/include/libretro.h

# Ou, se não tiver curl:
wget https://raw.githubusercontent.com/libretro/libretro-common/master/include/libretro.h
```

### Verificar

```bash
ls -la libretro.h
# Deve mostrar um arquivo de ~100KB+
```

Se você não tiver acesso à internet no ambiente onde vai compilar, baixe manualmente pelo navegador em:
`https://github.com/libretro/libretro-common/blob/master/include/libretro.h`
e salve como `include/libretro.h`.

---

## 📝 Passo 3: Criar o Núcleo Vazio (Skeleton)

### Criar `src/core/libretro_core.c`

```c
// libretro_core.c
// Núcleo Zeebo - Skeleton inicial
// Todas as funções existem, mas ainda não fazem nada.
// Objetivo: compilar e o RetroArch reconhecer o núcleo.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"

// =====================================================
// CALLBACKS (RetroArch vai nos dar essas funções)
// =====================================================
static retro_video_refresh_t video_cb = NULL;
static retro_audio_sample_t audio_cb = NULL;
static retro_audio_sample_batch_t audio_batch_cb = NULL;
static retro_environment_t environ_cb = NULL;
static retro_input_poll_t input_poll_cb = NULL;
static retro_input_state_t input_state_cb = NULL;

// =====================================================
// VERSÃO DA API
// =====================================================
unsigned retro_api_version(void) {
    return RETRO_API_VERSION;
}

// =====================================================
// INIT / DEINIT
// =====================================================
void retro_init(void) {
    printf("[Zeebo] retro_init() chamado\n");
}

void retro_deinit(void) {
    printf("[Zeebo] retro_deinit() chamado\n");
}

// =====================================================
// INFO DO NÚCLEO
// =====================================================
void retro_get_system_info(struct retro_system_info *info) {
    memset(info, 0, sizeof(*info));
    info->library_name     = "Zeebo";
    info->library_version  = "0.1-skeleton";
    info->valid_extensions = "mod"; // só .mod é carregável; .mif é metadado
    info->need_fullpath    = false;
    info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
    memset(info, 0, sizeof(*info));
    info->geometry.base_width   = 640;
    info->geometry.base_height  = 480;
    info->geometry.max_width    = 640;
    info->geometry.max_height   = 480;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps            = 60.0;
    info->timing.sample_rate    = 44100.0;
}

// =====================================================
// CALLBACKS - RetroArch registra essas funções
// =====================================================
void retro_set_video_refresh(retro_video_refresh_t cb) {
    video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb) {
    audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
    audio_batch_cb = cb;
}

void retro_set_environment(retro_environment_t cb) {
    environ_cb = cb;

    bool no_content = false;
    environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);
}

void retro_set_input_poll(retro_input_poll_t cb) {
    input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb) {
    input_state_cb = cb;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
    (void)port;
    (void)device;
}

// =====================================================
// LOAD / UNLOAD GAME
// =====================================================
bool retro_load_game(const struct retro_game_info *info) {
    if (!info) {
        printf("[Zeebo] retro_load_game: sem info\n");
        return false;
    }
    printf("[Zeebo] retro_load_game: %s\n", info->path ? info->path : "(sem path)");

    // TODO (Fase 2): carregar ROM MOD de verdade aqui
    return true;
}

void retro_unload_game(void) {
    printf("[Zeebo] retro_unload_game()\n");
}

unsigned retro_get_region(void) {
    return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) {
    (void)type;
    (void)info;
    (void)num;
    return false;
}

// =====================================================
// RESET
// =====================================================
void retro_reset(void) {
    printf("[Zeebo] retro_reset()\n");
}

// =====================================================
// RUN - FUNÇÃO PRINCIPAL (ainda vazia)
// =====================================================
void retro_run(void) {
    // TODO (Fase 1+): rodar CPU, gráficos, áudio de verdade
    if (input_poll_cb) {
        input_poll_cb();
    }

    // Por enquanto, só envia uma tela preta (para não travar o RetroArch)
    static uint32_t framebuffer[640 * 480];
    if (video_cb) {
        video_cb(framebuffer, 640, 480, 640 * sizeof(uint32_t));
    }
}

// =====================================================
// SAVE STATES (stub por enquanto)
// =====================================================
size_t retro_serialize_size(void) {
    return 0;
}

bool retro_serialize(void *data, size_t size) {
    (void)data;
    (void)size;
    return false;
}

bool retro_unserialize(const void *data, size_t size) {
    (void)data;
    (void)size;
    return false;
}

// =====================================================
// CHEATS (não usado, mas precisa existir)
// =====================================================
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {
    (void)index;
    (void)enabled;
    (void)code;
}

// =====================================================
// MEMORY (stub por enquanto)
// =====================================================
void *retro_get_memory_data(unsigned id) {
    (void)id;
    return NULL;
}

size_t retro_get_memory_size(unsigned id) {
    (void)id;
    return 0;
}
```

### Por Que Cada Parte Existe

```
O RetroArch espera encontrar TODAS essas funções na biblioteca,
mesmo que várias não façam nada ainda (save states, cheats, etc).

Se faltar uma função, a compilação vai falhar com erro
"undefined reference" - por isso o skeleton já inclui tudo.
```

---

## 🔨 Passo 4: Criar o Makefile

### Criar `Makefile` (na raiz do projeto)

```makefile
# Makefile - Núcleo Zeebo LibRetro

TARGET_NAME := zeebo

# Detectar sistema operacional
ifeq ($(shell uname -s),Linux)
    TARGET := $(TARGET_NAME)_libretro.so
    SHARED := -shared -Wl,--no-undefined
    FPIC := -fPIC
endif

ifeq ($(shell uname -s),Darwin)
    TARGET := $(TARGET_NAME)_libretro.dylib
    SHARED := -dynamiclib
    FPIC := -fPIC
endif

ifdef windir
    TARGET := $(TARGET_NAME)_libretro.dll
    SHARED := -shared -static-libgcc -static-libstdc++
    FPIC :=
endif

CC := gcc
CFLAGS := -Wall -Wextra -O2 -g $(FPIC) -Iinclude
LDFLAGS := $(SHARED)

# Por enquanto, só o skeleton. Outros arquivos .c serão
# adicionados aqui conforme forem criados (cpu.c, memory.c, etc)
SOURCES := src/core/libretro_core.c

OBJECTS := $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $(TARGET)
	@echo ""
	@echo "✅ Build concluído: $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
	@echo "🧹 Limpo"

.PHONY: all clean
```

### Compilar

```bash
make
```

### Saída Esperada

```
gcc -Wall -Wextra -O2 -g -fPIC -Iinclude -c src/core/libretro_core.c -o src/core/libretro_core.o
gcc src/core/libretro_core.o -shared -Wl,--no-undefined -o zeebo_libretro.so

✅ Build concluído: zeebo_libretro.so
```

Se aparecer isso, **parabéns — você acabou de compilar seu primeiro núcleo!**

---

## 🎮 Passo 5: Testar no RetroArch

### Copiar o núcleo para a pasta de cores

```bash
# Linux
cp zeebo_libretro.so ~/.config/retroarch/cores/

# Mac
cp zeebo_libretro.dylib ~/Library/Application\ Support/RetroArch/cores/

# Windows
copy zeebo_libretro.dll "C:\RetroArch\cores\"
```

### Verificar no RetroArch

```
1. Abrir RetroArch
2. Ir em "Load Core"
3. Procurar "Zeebo" na lista
4. Se aparecer: ✅ SUCESSO!
```

### Testar Carregar um "Jogo" (arquivo qualquer .mod)

```
1. Load Core → Zeebo
2. Load Content → escolher qualquer arquivo .mod (pode ser vazio/fake por enquanto)
3. Deve mostrar uma tela preta (sem travar)
4. Ver terminal/log: deve aparecer "[Zeebo] retro_load_game: ..."
```

Se a tela preta aparece sem crash, o skeleton está funcionando perfeitamente.

---

## ✅ Critérios de Sucesso do Protótipo

```
[ ] Pasta do projeto criada com estrutura correta
[ ] libretro.h baixado em include/
[ ] libretro_core.c criado e compila sem erros
[ ] Makefile funciona (make gera o .so/.dll)
[ ] RetroArch reconhece e lista "Zeebo" como núcleo
[ ] Núcleo carrega um "jogo" fake sem crashar
[ ] Tela preta aparece (prova que video_cb funciona)
```

Quando todos os itens acima estiverem ✅, a **Fase 0** (Setup & Estrutura) do `03_PLANO_DESENVOLVIMENTO.md` está oficialmente completa.

---

## 🎯 Próximo Passo

Com o skeleton compilando e rodando:

→ Comece a **Fase 1** do plano: implementar a CPU de verdade (`04_EMULACAO_CPU_DETALHADA.md`)

O próximo código real a escrever é `src/cpu/cpu.c` com a struct da CPU e o primeiro `cpu_step()`.
