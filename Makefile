# Makefile - Núcleo Zeebo LibRetro
# Suporta: Linux, macOS, Windows (MSYS2/MinGW)

TARGET_NAME := zeebo

# =====================================================
# Detectar Sistema Operacional
# =====================================================

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
    TARGET := $(TARGET_NAME)_libretro.so
    SHARED := -shared -Wl,--no-undefined -Wl,--version-script=src/core/link.T
    FPIC := -fPIC
    PLATFORM := linux
endif

ifeq ($(UNAME_S),Darwin)
    TARGET := $(TARGET_NAME)_libretro.dylib
    SHARED := -dynamiclib
    FPIC := -fPIC
    PLATFORM := osx
endif

ifdef WINDIR
    TARGET := $(TARGET_NAME)_libretro.dll
    SHARED := -shared -static-libgcc -static-libstdc++
    FPIC :=
    PLATFORM := windows
endif

# Se não detectou nada
ifeq ($(TARGET),)
    $(error Plataforma não detectada. Use: make PLATFORM=linux/osx/windows)
endif

# =====================================================
# Compilador e Flags
# =====================================================

CC := gcc
CFLAGS := -Wall -Wextra -O2 -g $(FPIC) -Iinclude -Isrc/core
LDFLAGS := $(SHARED)

# =====================================================
# Arquivos Fonte
# =====================================================

# Skeleton (Fase 0)
SKELETON_SRC := src/core/libretro_core.c

# CPU (Fase 1 - comentado por enquanto)
# CPU_SRC := src/cpu/cpu.c src/cpu/decode.c src/cpu/execute_arm.c src/cpu/execute_thumb.c src/cpu/flags.c

# Memory (Fase 1 - comentado por enquanto)
# MEMORY_SRC := src/memory/memory.c src/memory/heap.c

# Todos os arquivos fonte
SOURCES := $(SKELETON_SRC)

OBJECTS := $(SOURCES:.c=.o)

# =====================================================
# Targets
# =====================================================

all: $(TARGET)

$(TARGET): $(OBJECTS)
    @echo "[$(PLATFORM)] Linking $(TARGET)..."
    $(CC) $(OBJECTS) $(LDFLAGS) -o $(TARGET)
    @echo ""
    @echo "? Build concluído: $(TARGET)"
    @ls -lh $(TARGET)

%.o: %.c
    @echo "[CC] Compilando $<..."
    $(CC) $(CFLAGS) -c $< -o $@

clean:
    @echo "?? Limpando objetos..."
    rm -f $(OBJECTS)
    @echo "? Limpo"

fclean: clean
    @echo "???  Deletando binário..."
    rm -f $(TARGET)
    @echo "? Completamente limpo"

rebuild: fclean all

install: $(TARGET)
    @echo "?? Instalando em cores do RetroArch..."
ifeq ($(PLATFORM),linux)
    @mkdir -p ~/.config/retroarch/cores
    @cp $(TARGET) ~/.config/retroarch/cores/
    @echo "? Instalado em ~/.config/retroarch/cores/"
endif
ifeq ($(PLATFORM),osx)
    @mkdir -p ~/Library/Application\ Support/RetroArch/cores
    @cp $(TARGET) ~/Library/Application\ Support/RetroArch/cores/
    @echo "? Instalado em ~/Library/Application Support/RetroArch/cores/"
endif
ifdef WINDIR
    @echo "??  Para Windows, copie manualmente: $(TARGET) para C:\RetroArch\cores\"
endif

info:
    @echo "=== Zeebo LibRetro - Build Info ==="
    @echo "Plataforma: $(PLATFORM)"
    @echo "Compilador: $(CC)"
    @echo "Target: $(TARGET)"
    @echo "Arquivos: $(SOURCES)"
    @echo "Flags: $(CFLAGS)"
    @echo ""

help:
    @echo "Targets disponíveis:"
    @echo "  make           - Compilar"
    @echo "  make clean     - Limpar objetos (.o)"
    @echo "  make fclean    - Limpar tudo (objetos + binário)"
    @echo "  make rebuild   - Recompilar (fclean + all)"
    @echo "  make install   - Instalar em RetroArch (Linux/Mac)"
    @echo "  make info      - Mostrar informações de build"
    @echo "  make help      - Mostrar esta mensagem"
    @echo ""

.PHONY: all clean fclean rebuild install info help

