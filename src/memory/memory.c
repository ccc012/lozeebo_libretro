/* memory.c - Gerenciamento de memoria emulada
 * Regioes alocadas separadamente; traducao endereco virtual -> buffer.
 * Politica: acesso invalido loga e retorna 0 / ignora escrita (nunca trava).
 */
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "../debug/log.h"

static uint8_t *g_ram   = NULL;   /* codigo + dados  */
static uint8_t *g_heap  = NULL;
static uint8_t *g_stack = NULL;
static uint8_t *g_vram  = NULL;

/* Limita spam de log de acesso invalido */
static uint32_t g_bad_access_count = 0;
#define BAD_ACCESS_LOG_LIMIT 32

bool zmem_init(void) {
    g_ram   = (uint8_t *)calloc(1, ZMEM_RAM_SIZE);
    g_heap  = (uint8_t *)calloc(1, ZMEM_HEAP_SIZE);
    g_stack = (uint8_t *)calloc(1, ZMEM_STACK_SIZE);
    g_vram  = (uint8_t *)calloc(1, ZMEM_VRAM_SIZE);
    if (!g_ram || !g_heap || !g_stack || !g_vram) {
        LOGE("zmem_init: falha ao alocar memoria emulada");
        zmem_shutdown();
        return false;
    }
    zheap_reset();
    LOGI("zmem_init: RAM=%uMB heap=%uMB stack=%uMB vram=%uKB",
         ZMEM_RAM_SIZE >> 20, ZMEM_HEAP_SIZE >> 20,
         ZMEM_STACK_SIZE >> 20, ZMEM_VRAM_SIZE >> 10);
    return true;
}

void zmem_shutdown(void) {
    free(g_ram);   g_ram = NULL;
    free(g_heap);  g_heap = NULL;
    free(g_stack); g_stack = NULL;
    free(g_vram);  g_vram = NULL;
}

void zmem_reset(void) {
    if (g_ram)   memset(g_ram,   0, ZMEM_RAM_SIZE);
    if (g_heap)  memset(g_heap,  0, ZMEM_HEAP_SIZE);
    if (g_stack) memset(g_stack, 0, ZMEM_STACK_SIZE);
    if (g_vram)  memset(g_vram,  0, ZMEM_VRAM_SIZE);
    g_bad_access_count = 0;
    zheap_reset();
}

/* Traducao: devolve ponteiro host para 1 byte, ou NULL */
static uint8_t *translate(uint32_t addr) {
    if (addr < ZMEM_RAM_SIZE)
        return g_ram ? &g_ram[addr] : NULL;
    if (addr >= ZMEM_HEAP_BASE && addr < ZMEM_HEAP_BASE + ZMEM_HEAP_SIZE)
        return g_heap ? &g_heap[addr - ZMEM_HEAP_BASE] : NULL;
    if (addr >= ZMEM_STACK_BASE && addr < ZMEM_STACK_BASE + ZMEM_STACK_SIZE)
        return g_stack ? &g_stack[addr - ZMEM_STACK_BASE] : NULL;
    if (addr >= ZMEM_VRAM_BASE && addr < ZMEM_VRAM_BASE + ZMEM_VRAM_SIZE)
        return g_vram ? &g_vram[addr - ZMEM_VRAM_BASE] : NULL;
    return NULL;
}

static void log_bad_access(const char *what, uint32_t addr) {
    if (g_bad_access_count < BAD_ACCESS_LOG_LIMIT) {
        LOGW("memoria: %s invalido em 0x%08X", what, addr);
        g_bad_access_count++;
        if (g_bad_access_count == BAD_ACCESS_LOG_LIMIT)
            LOGW("memoria: (limite de logs de acesso invalido atingido)");
    }
}

uint8_t zmem_read8(uint32_t addr) {
    uint8_t *p = translate(addr);
    if (!p) { log_bad_access("read8", addr); return 0; }
    return *p;
}

uint16_t zmem_read16(uint32_t addr) {
    /* little-endian, tolera desalinhamento (le byte a byte) */
    uint8_t *p0 = translate(addr);
    uint8_t *p1 = translate(addr + 1);
    if (!p0 || !p1) { log_bad_access("read16", addr); return 0; }
    return (uint16_t)(p0[0] | (p1[0] << 8));
}

uint32_t zmem_read32(uint32_t addr) {
    uint8_t *p = translate(addr);
    if (p && translate(addr + 3) == p + 3) {
        /* regiao contigua: monta little-endian */
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    if (!p) { log_bad_access("read32", addr); return 0; }
    return (uint32_t)zmem_read8(addr) |
           ((uint32_t)zmem_read8(addr + 1) << 8) |
           ((uint32_t)zmem_read8(addr + 2) << 16) |
           ((uint32_t)zmem_read8(addr + 3) << 24);
}

void zmem_write8(uint32_t addr, uint8_t val) {
    uint8_t *p = translate(addr);
    if (!p) { log_bad_access("write8", addr); return; }
    *p = val;
}

void zmem_write16(uint32_t addr, uint16_t val) {
    zmem_write8(addr, (uint8_t)(val & 0xFF));
    zmem_write8(addr + 1, (uint8_t)(val >> 8));
}

void zmem_write32(uint32_t addr, uint32_t val) {
    uint8_t *p = translate(addr);
    if (p && translate(addr + 3) == p + 3) {
        p[0] = (uint8_t)(val);
        p[1] = (uint8_t)(val >> 8);
        p[2] = (uint8_t)(val >> 16);
        p[3] = (uint8_t)(val >> 24);
        return;
    }
    if (!p) { log_bad_access("write32", addr); return; }
    zmem_write8(addr,     (uint8_t)(val));
    zmem_write8(addr + 1, (uint8_t)(val >> 8));
    zmem_write8(addr + 2, (uint8_t)(val >> 16));
    zmem_write8(addr + 3, (uint8_t)(val >> 24));
}

bool zmem_read_block(uint32_t addr, void *dst, uint32_t len) {
    uint8_t *p = translate(addr);
    if (p && len > 0 && translate(addr + len - 1) == p + len - 1) {
        memcpy(dst, p, len);
        return true;
    }
    log_bad_access("read_block", addr);
    memset(dst, 0, len);
    return false;
}

bool zmem_write_block(uint32_t addr, const void *src, uint32_t len) {
    uint8_t *p = translate(addr);
    if (p && len > 0 && translate(addr + len - 1) == p + len - 1) {
        memcpy(p, src, len);
        return true;
    }
    log_bad_access("write_block", addr);
    return false;
}

void zmem_read_cstr(uint32_t addr, char *dst, uint32_t maxlen) {
    uint32_t i;
    if (maxlen == 0) return;
    for (i = 0; i < maxlen - 1; i++) {
        uint8_t c = zmem_read8(addr + i);
        dst[i] = (char)c;
        if (c == 0) return;
    }
    dst[maxlen - 1] = '\0';
}

void *zmem_host_ptr(uint32_t addr, uint32_t len) {
    uint8_t *p = translate(addr);
    if (p && len > 0 && translate(addr + len - 1) == p + len - 1)
        return p;
    return NULL;
}
