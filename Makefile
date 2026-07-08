# Makefile - Núcleo Zeebo LibRetro
# Suporta: Linux, macOS, Windows (MSYS2/MinGW)

TARGET_NAME := zeebo
CORE_NAME := $(TARGET_NAME)_libretro

# =====================================================
# Detectar Sistema Operacional
# =====================================================

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
    TARGET := $(CORE_NAME).so
    SHARED := -shared -Wl,--no-undefined
    FPIC := -fPIC
    PLATFORM := linux
endif

ifeq ($(UNAME_S),Darwin)
    TARGET := $(CORE_NAME).dylib
    SHARED := -dynamiclib
    FPIC := -fPIC
    PLATFORM := osx
endif

ifdef WINDIR
    TARGET := $(CORE_NAME).dll
    SHARED := -shared -static-libgcc
    FPIC :=
    PLATFORM := windows
endif

# Se não detectou nada
ifeq ($(TARGET),)
    $(error Plataforma nao detectada. Use Linux, macOS ou Windows/MSYS2)
endif

ifneq ($(wildcard src/core/link.T),)
ifeq ($(PLATFORM),linux)
SHARED += -Wl,--version-script=src/core/link.T
endif
endif

# =====================================================
# Compilador e Flags
# =====================================================

CC := gcc
CFLAGS := -Wall -Wextra -O2 -g $(FPIC) -Isrc -Isrc/core
LDFLAGS := $(SHARED)

# =====================================================
# Arquivos Fonte
# =====================================================

SOURCES := \
	src/audio/audio.c \
	src/audio/mixer.c \
	src/audio/pcm.c \
	src/brew/boot.c \
	src/brew/brew.c \
	src/brew/helpers.c \
	src/brew/ibitmap.c \
	src/brew/idisplay.c \
	src/brew/idisplay_real.c \
	src/brew/ifile.c \
	src/brew/imemory.c \
	src/brew/ishell.c \
	src/brew/isound.c \
	src/core/libretro_core.c \
	src/cpu/cpu.c \
	src/cpu/decode.c \
	src/cpu/execute_arm.c \
	src/cpu/execute_thumb.c \
	src/cpu/flags.c \
	src/debug/disasm.c \
	src/debug/log.c \
	src/debug/trace.c \
	src/gpu/draw.c \
	src/gpu/egl_gl.c \
	src/gpu/framebuffer.c \
	src/input/input.c \
	src/loader/bar_parser.c \
	src/loader/mif_parser.c \
	src/loader/mod_loader.c \
	src/memory/heap.c \
	src/memory/memory.c

OBJECTS := $(SOURCES:.c=.o)

# =====================================================
# Targets
# =====================================================

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@echo "[$(PLATFORM)] Linking $(TARGET)..."
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

%.o: %.c
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS)

fclean: clean
	rm -f $(TARGET)

rebuild: fclean all

info:
	@echo "Plataforma: $(PLATFORM)"
	@echo "Target: $(TARGET)"
	@echo "Compilador: $(CC)"
	@echo "Sources: $(words $(SOURCES)) arquivos"

.PHONY: all clean fclean rebuild info

