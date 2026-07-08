/* heap.c - Alocador de blocos para o heap emulado (MALLOC/FREE do BREW)
 *
 * Estrategia: lista encadeada de blocos dentro do proprio heap emulado.
 * Cada bloco tem um header de 16 bytes:
 *   [0] uint32_t magic  (ZHEAP_MAGIC_USED ou ZHEAP_MAGIC_FREE)
 *   [4] uint32_t size   (tamanho utilizavel, sem o header)
 *   [8] uint32_t prev   (endereco emulado do bloco anterior, 0 se primeiro)
 *   [12] uint32_t pad
 * Free faz merge com vizinhos adjacentes livres.
 */
#include <string.h>
#include "memory.h"
#include "../debug/log.h"

#define ZHEAP_MAGIC_USED 0x5A484150u  /* "ZHAP" */
#define ZHEAP_MAGIC_FREE 0x5A465245u  /* "ZFRE" */
#define ZHEAP_HDR_SIZE   16u
#define ZHEAP_ALIGN      8u

static uint32_t g_used_bytes = 0;

static uint32_t align_up(uint32_t v, uint32_t a) {
    return (v + a - 1) & ~(a - 1);
}

static uint32_t hdr_magic(uint32_t blk) { return zmem_read32(blk); }
static uint32_t hdr_size(uint32_t blk)  { return zmem_read32(blk + 4); }
static uint32_t hdr_prev(uint32_t blk)  { return zmem_read32(blk + 8); }
static void set_hdr(uint32_t blk, uint32_t magic, uint32_t size, uint32_t prev) {
    zmem_write32(blk, magic);
    zmem_write32(blk + 4, size);
    zmem_write32(blk + 8, prev);
    zmem_write32(blk + 12, 0);
}

static uint32_t next_block(uint32_t blk) {
    uint32_t n = blk + ZHEAP_HDR_SIZE + hdr_size(blk);
    if (n >= ZMEM_HEAP_BASE + ZMEM_HEAP_SIZE) return 0;
    return n;
}

void zheap_reset(void) {
    /* Um unico bloco livre gigante cobrindo o heap inteiro */
    set_hdr(ZMEM_HEAP_BASE, ZHEAP_MAGIC_FREE,
            ZMEM_HEAP_SIZE - ZHEAP_HDR_SIZE, 0);
    g_used_bytes = 0;
}

uint32_t zheap_alloc(uint32_t size) {
    uint32_t blk = ZMEM_HEAP_BASE;
    if (size == 0) size = 1;
    size = align_up(size, ZHEAP_ALIGN);

    while (blk) {
        uint32_t magic = hdr_magic(blk);
        uint32_t bsize = hdr_size(blk);
        if (magic != ZHEAP_MAGIC_USED && magic != ZHEAP_MAGIC_FREE) {
            LOGE("heap corrompido em 0x%08X (magic=0x%08X)", blk, magic);
            return 0;
        }
        if (magic == ZHEAP_MAGIC_FREE && bsize >= size) {
            /* divide o bloco se sobrar espaco para outro header + minimo */
            if (bsize >= size + ZHEAP_HDR_SIZE + ZHEAP_ALIGN) {
                uint32_t rest = blk + ZHEAP_HDR_SIZE + size;
                set_hdr(rest, ZHEAP_MAGIC_FREE,
                        bsize - size - ZHEAP_HDR_SIZE, blk);
                uint32_t after = next_block(rest);
                if (after) zmem_write32(after + 8, rest); /* prev do proximo */
                set_hdr(blk, ZHEAP_MAGIC_USED, size, hdr_prev(blk));
            } else {
                set_hdr(blk, ZHEAP_MAGIC_USED, bsize, hdr_prev(blk));
            }
            g_used_bytes += hdr_size(blk);
            if (blk + ZHEAP_HDR_SIZE < 0x100AF500u &&
                blk + ZHEAP_HDR_SIZE + size > 0x100AF4C0u) {
                LOGI("watch heap alloc: user=0x%08X size=%u blk=0x%08X",
                     blk + ZHEAP_HDR_SIZE, size, blk);
            }
            return blk + ZHEAP_HDR_SIZE;
        }
        blk = next_block(blk);
    }
    LOGW("zheap_alloc: sem memoria para %u bytes", size);
    return 0;
}

void zheap_free(uint32_t addr) {
    if (addr == 0) return;
    uint32_t blk = addr - ZHEAP_HDR_SIZE;
    if (blk < ZMEM_HEAP_BASE || blk >= ZMEM_HEAP_BASE + ZMEM_HEAP_SIZE ||
        hdr_magic(blk) != ZHEAP_MAGIC_USED) {
        LOGW("zheap_free: endereco invalido 0x%08X", addr);
        return;
    }
    uint32_t size = hdr_size(blk);
    if (addr < 0x100AF500u && addr + size > 0x100AF4C0u)
        LOGI("watch heap free: user=0x%08X size=%u", addr, size);
    if (size <= g_used_bytes) g_used_bytes -= size; else g_used_bytes = 0;
    set_hdr(blk, ZHEAP_MAGIC_FREE, size, hdr_prev(blk));

    /* merge com o proximo se livre */
    uint32_t nxt = next_block(blk);
    if (nxt && hdr_magic(nxt) == ZHEAP_MAGIC_FREE) {
        uint32_t merged = hdr_size(blk) + ZHEAP_HDR_SIZE + hdr_size(nxt);
        set_hdr(blk, ZHEAP_MAGIC_FREE, merged, hdr_prev(blk));
        uint32_t after = next_block(blk);
        if (after) zmem_write32(after + 8, blk);
    }
    /* merge com o anterior se livre */
    uint32_t prv = hdr_prev(blk);
    if (prv && hdr_magic(prv) == ZHEAP_MAGIC_FREE) {
        uint32_t merged = hdr_size(prv) + ZHEAP_HDR_SIZE + hdr_size(blk);
        set_hdr(prv, ZHEAP_MAGIC_FREE, merged, hdr_prev(prv));
        uint32_t after = next_block(prv);
        if (after) zmem_write32(after + 8, prv);
    }
}

uint32_t zheap_block_size(uint32_t addr) {
    if (addr == 0) return 0;
    uint32_t blk = addr - ZHEAP_HDR_SIZE;
    if (blk < ZMEM_HEAP_BASE || blk >= ZMEM_HEAP_BASE + ZMEM_HEAP_SIZE)
        return 0;
    if (hdr_magic(blk) != ZHEAP_MAGIC_USED) return 0;
    return hdr_size(blk);
}

uint32_t zheap_realloc(uint32_t addr, uint32_t new_size) {
    if (addr == 0) return zheap_alloc(new_size);
    if (new_size == 0) { zheap_free(addr); return 0; }

    uint32_t old_size = zheap_block_size(addr);
    if (old_size == 0) {
        LOGW("zheap_realloc: endereco invalido 0x%08X", addr);
        return 0;
    }
    if (new_size <= old_size) return addr; /* ja cabe */

    uint32_t novo = zheap_alloc(new_size);
    if (!novo) return 0;

    /* copia dentro da memoria emulada */
    void *src = zmem_host_ptr(addr, old_size);
    void *dst = zmem_host_ptr(novo, old_size);
    if (src && dst) memcpy(dst, src, old_size);
    zheap_free(addr);
    return novo;
}

uint32_t zheap_used(void) {
    return g_used_bytes;
}
