/* memory.c - Gerenciamento de memoria emulada
 * Regioes alocadas separadamente; traducao endereco virtual -> buffer.
 * Politica: acesso invalido loga e retorna 0 / ignora escrita (nunca trava).
 */
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "../cpu/cpu.h"
#include "../debug/log.h"
#include "../brew/brew.h"

static uint8_t *g_ram   = NULL;   /* codigo + dados  */
static uint8_t *g_heap  = NULL;
static uint8_t *g_guard = NULL;   /* janela extra apos o heap */
static uint8_t *g_stack = NULL;
static uint8_t *g_vram  = NULL;

/* Pagina alta 0xFFFFFF00-0xFFFFFFFF: o bootstrap de modulos BREW le a
 * tabela de helpers de [-4] (=0xFFFFFFFC) e a versao de [-8]. */
static uint8_t g_highpage[256];

/* Limita spam de log de acesso invalido */
static uint32_t g_bad_access_count = 0;
#define BAD_ACCESS_LOG_LIMIT 32

bool zmem_init(void) {
    g_ram   = (uint8_t *)calloc(1, ZMEM_RAM_SIZE);
    g_heap  = (uint8_t *)calloc(1, ZMEM_HEAP_SIZE);
    g_guard = (uint8_t *)calloc(1, ZMEM_GUARD_SIZE);
    g_stack = (uint8_t *)calloc(1, ZMEM_STACK_SIZE);
    g_vram  = (uint8_t *)calloc(1, ZMEM_VRAM_SIZE);
    if (!g_ram || !g_heap || !g_guard || !g_stack || !g_vram) {
        LOGE("zmem_init: falha ao alocar memoria emulada");
        zmem_shutdown();
        return false;
    }
    zheap_reset();
    LOGI("zmem_init: RAM=%uMB heap=%uMB guard=%uMB stack=%uMB vram=%uKB",
         ZMEM_RAM_SIZE >> 20, ZMEM_HEAP_SIZE >> 20, ZMEM_GUARD_SIZE >> 20,
         ZMEM_STACK_SIZE >> 20, ZMEM_VRAM_SIZE >> 10);
    return true;
}

void zmem_shutdown(void) {
    free(g_ram);   g_ram = NULL;
    free(g_heap);  g_heap = NULL;
    free(g_guard); g_guard = NULL;
    free(g_stack); g_stack = NULL;
    free(g_vram);  g_vram = NULL;
}

void zmem_reset(void) {
    if (g_ram)   memset(g_ram,   0, ZMEM_RAM_SIZE);
    if (g_heap)  memset(g_heap,  0, ZMEM_HEAP_SIZE);
    if (g_guard) memset(g_guard, 0, ZMEM_GUARD_SIZE);
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
    if (addr >= ZMEM_GUARD_BASE && addr < ZMEM_GUARD_BASE + ZMEM_GUARD_SIZE)
        return g_guard ? &g_guard[addr - ZMEM_GUARD_BASE] : NULL;
    if (addr >= ZMEM_STACK_BASE && addr < ZMEM_STACK_BASE + ZMEM_STACK_SIZE)
        return g_stack ? &g_stack[addr - ZMEM_STACK_BASE] : NULL;
    if (addr >= ZMEM_VRAM_BASE && addr < ZMEM_VRAM_BASE + ZMEM_VRAM_SIZE)
        return g_vram ? &g_vram[addr - ZMEM_VRAM_BASE] : NULL;
    if (addr >= 0xFFFFFF00u)
        return &g_highpage[addr - 0xFFFFFF00u];
    return NULL;
}

void ztrace_dump(void);

static void log_bad_access(const char *what, uint32_t addr) {
    if (g_bad_access_count < BAD_ACCESS_LOG_LIMIT) {
        LOGW("memoria: %s invalido em 0x%08X (PC=0x%08X LR=0x%08X SP=0x%08X R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X)",
             what, addr, g_cpu.r[REG_PC], g_cpu.r[REG_LR], g_cpu.r[REG_SP],
             g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[3]);
        if (g_bad_access_count == 0)
            ztrace_dump(); /* primeiro acesso ruim: mostra o caminho ate aqui */
        g_bad_access_count++;
        if (g_bad_access_count == BAD_ACCESS_LOG_LIMIT)
            LOGW("memoria: (limite de logs de acesso invalido atingido)");
    }
}

uint8_t zmem_read8(uint32_t addr) {
    uint8_t *p;
    /* Regiao HLE: retornar 0 em vez de fault (jogos leem registradores
     * de hardware mapeados aqui como BADA8/SD card status). */
    if (addr >= ZMEM_HLE_BASE && addr < ZMEM_HLE_END)
        return 0;
    p = translate(addr);
    if (!p) {
        /* Buracos do mapa (0x04000000..0x0FFFFFFF e alem da guard ate a
         * stack): retorno silencioso de 0 — o scan linear de
         * Alice/TTDMM le zeros atraves desses buracos byte a byte;
         * logar geraria spam infinito e travava o smoke. */
        if (addr >= ZMEM_GUARD_BASE && addr < ZMEM_STACK_BASE)
            return 0;
        if (addr >= ZMEM_RAM_SIZE && addr < ZMEM_HEAP_BASE)
            return 0;
        log_bad_access("read8", addr); return 0;
    }
    return *p;
}

/* Cross-boundary reads/writes (ex: 4 bytes em ZMEM_GUARD_SIZE-2..) sao
 * decompostos em bytes; writes apos a guard (< ZMEM_STACK_BASE) sao
 * descartados sem log para nao floodar o log do scan de Alice/TTDMM. */

uint16_t zmem_read16(uint32_t addr) {
    /* little-endian, tolera desalinhamento (le byte a byte) */
    uint8_t *p0 = translate(addr);
    uint8_t *p1 = translate(addr + 1);
    if (!p0 || !p1) { log_bad_access("read16", addr); return 0; }
    return (uint16_t)(p0[0] | (p1[0] << 8));
}

uint32_t zmem_read32(uint32_t addr) {
    uint8_t *p;
    uint32_t forced;
    /* Regiao de traps HLE: retornar o proprio endereco (vtable auto-descritiva)
     * para que jogos que copiem vtables stubs como dados recebam ponteiros
     * validos que despacham handlers quando chamados. */
    if (addr >= ZMEM_HLE_BASE && addr < ZMEM_HLE_END)
        return addr;
    /* IFile pool: virtual call sempre vê vtable HLE mesmo se guest sujou [obj]. */
    forced = zbrew_ifile_force_vtbl_read(addr);
    if (forced) return forced;
    p = translate(addr);
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
    uint8_t *p;
    if (zbrew_ifile_block_write(addr, 1))
        return;
    p = translate(addr);
    if (!p) {
        /* Writes em buracos do mapa (0x04000000..0x0FFFFFFF, e alem da
         * guard ate a stack) sao ignorados sem log: o scan linear de
         * Alice/TTDMM atravessa esses buracos byte a byte, e logar
         * geraria spam infinito. */
        if (addr >= ZMEM_GUARD_BASE && addr < ZMEM_STACK_BASE)
            return;
        if (addr < ZMEM_HEAP_BASE && addr >= ZMEM_RAM_SIZE)
            return;
        if (addr < ZMEM_GUARD_BASE && addr >= ZMEM_RAM_SIZE)
            return;
        log_bad_access("write8", addr); return;
    }
    *p = val;
}

void zmem_write16(uint32_t addr, uint16_t val) {
    zmem_write8(addr, (uint8_t)(val & 0xFF));
    zmem_write8(addr + 1, (uint8_t)(val >> 8));
}

void zmem_write32(uint32_t addr, uint32_t val) {
    if (zbrew_ifile_block_write(addr, 4))
        return;
    if (addr >= 0x10001500u && addr <= 0x10001510u) {
        static uint32_t app24_hits = 0;
        if (app24_hits < 30) {
            LOGI("WATCH app24 write32: addr=0x%08X value=0x%08X PC=0x%08X LR=0x%08X",
                 addr, val, g_cpu.r[REG_PC] - 8, g_cpu.r[REG_LR]);
            app24_hits++;
        }
    }
    /* tabptr watch removido: flood no init DE (Bad Dudes) apos RomLoaded. */
    if (addr == 0x1000161Cu || addr == 0x10005F28u) {
        static uint32_t ctxc_hits = 0;
        if (ctxc_hits < 200) {
            LOGI("WATCH ctxC write32: addr=0x%08X value=0x%08X PC=0x%08X LR=0x%08X "
                 "vtable=[0x%08X]",
                 addr, val, g_cpu.r[REG_PC] - 8, g_cpu.r[REG_LR], zmem_read32(val));
            ctxc_hits++;
        }
    }
    /* errfield watch removido: mesmo flood no decode de tabelas DE. */
    if (addr == 0x10004BACu) {
        static uint32_t st_hits = 0;
        if (st_hits < 60) {
            LOGI("WATCH loadstate write32: addr=0x%08X value=0x%08X PC=0x%08X LR=0x%08X",
                 addr, val, g_cpu.r[REG_PC] - 8, g_cpu.r[REG_LR]);
            st_hits++;
        }
    }
    if (addr == 0x100124A8u && val == 0) {
        static uint32_t hits = 0;
        if (hits < 3) {
            LOGI("WATCH devvt ZERO: addr=0x%08X PC=0x%08X LR=0x%08X "
                 "R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X",
                 addr, g_cpu.r[REG_PC], g_cpu.r[REG_LR],
                 g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[3]);
            ztrace_dump();
            hits++;
        }
    }
    if (addr == 0x100124A8u) {
        static uint32_t hits = 0;
        if (hits < 30) {
            LOGI("WATCH devvt write32: addr=0x%08X value=0x%08X PC=0x%08X LR=0x%08X",
                 addr, val, g_cpu.r[REG_PC] - 8, g_cpu.r[REG_LR]);
            hits++;
        }
    }
    if (addr == 0x100120F0u) {
        static uint32_t hits = 0;
        if (hits < 30) {
            LOGI("WATCH devslot write32: addr=0x%08X value=0x%08X PC=0x%08X LR=0x%08X",
                 addr, val, g_cpu.r[REG_PC] - 8, g_cpu.r[REG_LR]);
            hits++;
        }
    }
    if (addr >= 0x10011FE0u && addr <= 0x10012014u) {
        static uint32_t devzone_hits = 0;
        if (devzone_hits < 120) {
            LOGI("WATCH devzone write32: addr=0x%08X value=0x%08X PC=0x%08X LR=0x%08X",
                 addr, val, g_cpu.r[REG_PC] - 8, g_cpu.r[REG_LR]);
            devzone_hits++;
        }
    }
    if (addr >= 0x100059A0u && addr <= 0x100059B0u) {
        static uint32_t mgr_hits = 0;
        if (mgr_hits < 60) {
            LOGI("WATCH mgrstate write32: addr=0x%08X value=0x%08X PC=0x%08X LR=0x%08X",
                 addr, val, g_cpu.r[REG_PC] - 8, g_cpu.r[REG_LR]);
            mgr_hits++;
        }
    }
    if (addr == 0x1CBB8u || addr == 0x1CBBCu) {
        static uint32_t q_hits = 0;
        if (q_hits < 60) {
            LOGI("WATCH queuehead write32: addr=0x%08X value=0x%08X PC=0x%08X LR=0x%08X",
                 addr, val, g_cpu.r[REG_PC] - 8, g_cpu.r[REG_LR]);
            q_hits++;
        }
    }
    if (addr == 0x10005BB8u || addr == 0x10005BBCu || addr == 0x10005BC0u) {
        static uint32_t tip_hits = 0;
        if (tip_hits < 60) {
            LOGI("WATCH tipstate write32: addr=0x%08X value=0x%08X PC=0x%08X LR=0x%08X",
                 addr, val, g_cpu.r[REG_PC] - 8, g_cpu.r[REG_LR]);
            tip_hits++;
        }
    }
    if (addr == 0x100D955Au || addr == 0x100D9552u || addr == 0x100D9556u) {
        static uint32_t list_hits = 0;
        if (list_hits < 40) {
            LOGI("WATCH listfield write16/32: addr=0x%08X value=0x%08X PC=0x%08X LR=0x%08X",
                 addr, val, g_cpu.r[REG_PC] - 8, g_cpu.r[REG_LR]);
            list_hits++;
        }
    }
    if (addr >= 0x100059F0u && addr <= 0x10005A20u) {
        static uint32_t rl_hits = 0;
        if (rl_hits < 120) {
            LOGI("WATCH readobj write32: addr=0x%08X value=0x%08X PC=0x%08X LR=0x%08X",
                 addr, val, g_cpu.r[REG_PC] - 8, g_cpu.r[REG_LR]);
            rl_hits++;
        }
    }
    uint8_t *p = translate(addr);
    if (p && translate(addr + 3) == p + 3) {
        p[0] = (uint8_t)(val);
        p[1] = (uint8_t)(val >> 8);
        p[2] = (uint8_t)(val >> 16);
        p[3] = (uint8_t)(val >> 24);
        return;
    }
    if (!p) {
        if (addr >= ZMEM_GUARD_BASE && addr < ZMEM_STACK_BASE)
            return;
        if (addr < ZMEM_HEAP_BASE && addr >= ZMEM_RAM_SIZE)
            return;
        log_bad_access("write32", addr); return;
    }
    zmem_write8(addr,     (uint8_t)(val));
    zmem_write8(addr + 1, (uint8_t)(val >> 8));
    zmem_write8(addr + 2, (uint8_t)(val >> 16));
    zmem_write8(addr + 3, (uint8_t)(val >> 24));
}
/* Writes em buracos do mapa sao descartados sem log (ver zmem_write8).
 * Reads em buracos retornam 0 sem log (ver zmem_read8). Cross-boundary
 * via bytes decompostos cai nesses mesmos caminhos. */

bool zmem_read_block(uint32_t addr, void *dst, uint32_t len) {
    uint8_t *p = translate(addr);
    uint8_t *out = (uint8_t *)dst;

    if (len == 0) return true;

    if (p && translate(addr + len - 1) == p + len - 1) {
        memcpy(dst, p, len);
        return true;
    }

    {
        uint32_t i;
        for (i = 0; i < len; i++) {
            uint8_t *b = translate(addr + i);
            if (!b) {
                memset(out + i, 0, len - i);
                log_bad_access("read_block", addr + i);
                return false;
            }
            out[i] = *b;
        }
    }
    return true;
}

bool zmem_write_block(uint32_t addr, const void *src, uint32_t len) {
    uint8_t *p = translate(addr);
    const uint8_t *in = (const uint8_t *)src;

    if (addr <= 0x10004BACu && addr + len > 0x10004BACu) {
        static uint32_t blk_hits = 0;
        if (blk_hits < 30) {
            LOGI("WATCH block covers 0x10004BAC: addr=0x%08X len=%u PC=0x%08X LR=0x%08X",
                 addr, len, g_cpu.r[REG_PC] - 8, g_cpu.r[REG_LR]);
            blk_hits++;
        }
    }

    if (len == 0) return true;

    if (p && translate(addr + len - 1) == p + len - 1) {
        memcpy(p, src, len);
        return true;
    }

    {
        uint32_t i;
        for (i = 0; i < len; i++) {
            uint8_t *b = translate(addr + i);
            if (!b) {
                log_bad_access("write_block", addr + i);
                return false;
            }
            *b = in[i];
        }
    }
    return true;
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

uint8_t *zmem_ram_ptr(void) { return g_ram; }
uint8_t *zmem_heap_ptr(void) { return g_heap; }
uint8_t *zmem_stack_ptr(void) { return g_stack; }
uint8_t *zmem_guard_ptr(void) { return g_guard; }
uint8_t *zmem_vram_ptr(void) { return g_vram; }
const uint8_t *zmem_ram_ptr_const(void) { return g_ram; }
const uint8_t *zmem_heap_ptr_const(void) { return g_heap; }
const uint8_t *zmem_stack_ptr_const(void) { return g_stack; }
const uint8_t *zmem_guard_ptr_const(void) { return g_guard; }
const uint8_t *zmem_vram_ptr_const(void) { return g_vram; }
const uint8_t *zmem_highpage_ptr_const(void) { return g_highpage; }
uint8_t *zmem_highpage_ptr(void) { return g_highpage; }
