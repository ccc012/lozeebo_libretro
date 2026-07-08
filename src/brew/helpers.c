/* helpers.c - AEEHelperFuncs: a "stdlib" que modulos BREW usam
 *
 * Modulos BREW obtem esta tabela via GET_HELPER() == *((void**)AEEMod_Load - 4).
 * Cada slot aponta para um trap; aqui implementamos as funcoes sobre a
 * memoria emulada. Layout de 117 slots conforme aee_ids.h.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "brew.h"
#include "aee_ids.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../debug/log.h"

uint32_t zbrew_uptime_ms(void);

static uint32_t g_rand_state = 0x12345678;

uint32_t zbrew_build_helper_table(void) {
    uint32_t table = zheap_alloc(0x1000);
    uint32_t i;
    if (!table) return 0;
    for (i = 0; i < 0x1000; i += 4)
        zmem_write32(table + i, 0);
    for (i = 0; i < ZAEE_HELPER_COUNT; i++)
        zmem_write32(table + i * 4, ZTRAP_ADDR(ZT_HELPER_BASE + i));
    LOGI("helper table AEE em 0x%08X (%u slots)", table,
         (unsigned)ZAEE_HELPER_COUNT);
    return table;
}

/* ---- utilidades sobre memoria emulada ---- */

static uint32_t estrlen(uint32_t s) {
    uint32_t n = 0;
    while (zmem_read8(s + n)) n++;
    return n;
}

static uint32_t ewstrlen(uint32_t s) {
    uint32_t n = 0;
    while (zmem_read16(s + n * 2)) n++;
    return n;
}

static int estrncasecmp(uint32_t a, uint32_t b, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        uint8_t ca = zmem_read8(a + i), cb = zmem_read8(b + i);
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (int)ca - (int)cb;
        if (!ca) return 0;
    }
    return 0;
}

static int ewstrncasecmp(uint32_t a, uint32_t b, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        uint16_t ca = zmem_read16(a + i * 2), cb = zmem_read16(b + i * 2);
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (int)ca - (int)cb;
        if (!ca) return 0;
    }
    return 0;
}

/* mini vsnprintf sobre memoria emulada.
 * fmt em fmt_addr; argumentos comecam no indice arg_start da sequencia
 * R1,R2,R3,[SP],[SP+4]... Escreve em dst (se dst!=0) limitado a cap.
 * Retorna o tamanho gerado. Se dst==0, escreve em host_out (para log). */
static uint32_t eformat(uint32_t dst, uint32_t cap, uint32_t fmt_addr,
                        int arg_start, char *host_out, uint32_t host_cap) {
    uint32_t out = 0;
    int argn = arg_start;
    uint32_t fi = 0;

#define NEXT_ARG() (argn <= 3 ? g_cpu.r[argn++] \
                              : zbrew_stack_arg((argn++) - 4))
#define EMIT(c) do { \
        if (dst && out < cap - 1) zmem_write8(dst + out, (uint8_t)(c)); \
        if (host_out && out < host_cap - 1) host_out[out] = (char)(c); \
        out++; \
    } while (0)

    for (;;) {
        uint8_t c = zmem_read8(fmt_addr + fi++);
        if (!c) break;
        if (c != '%') { EMIT(c); continue; }

        /* pula flags/largura/precisao (nao suportadas, so consome) */
        c = zmem_read8(fmt_addr + fi++);
        while ((c >= '0' && c <= '9') || c == '-' || c == '+' ||
               c == '.' || c == 'l' || c == 'h')
            c = zmem_read8(fmt_addr + fi++);

        switch (c) {
        case '%': EMIT('%'); break;
        case 'c': EMIT((char)NEXT_ARG()); break;
        case 's': {
            uint32_t sp = NEXT_ARG();
            if (sp) {
                uint8_t sc;
                while ((sc = zmem_read8(sp++)) != 0) EMIT(sc);
            } else {
                const char *nul = "(null)";
                while (*nul) EMIT(*nul++);
            }
            break;
        }
        case 'd': case 'i': {
            char buf[16];
            int len = snprintf(buf, sizeof(buf), "%d", (int)NEXT_ARG());
            int k;
            for (k = 0; k < len; k++) EMIT(buf[k]);
            break;
        }
        case 'u': {
            char buf[16];
            int len = snprintf(buf, sizeof(buf), "%u", NEXT_ARG());
            int k;
            for (k = 0; k < len; k++) EMIT(buf[k]);
            break;
        }
        case 'x': case 'X': case 'p': {
            char buf[16];
            int len = snprintf(buf, sizeof(buf), "%x", NEXT_ARG());
            int k;
            for (k = 0; k < len; k++) EMIT(buf[k]);
            break;
        }
        default:
            EMIT('%');
            EMIT(c);
            break;
        }
    }
    if (dst) zmem_write8(dst + (out < cap ? out : cap - 1), 0);
    if (host_out) host_out[out < host_cap ? out : host_cap - 1] = '\0';
    return out;
#undef NEXT_ARG
#undef EMIT
}

/* copia bloco na memoria emulada (enderecos podem se sobrepor) */
static void ememmove(uint32_t dst, uint32_t src, uint32_t n) {
    void *pd = zmem_host_ptr(dst, n);
    void *ps = zmem_host_ptr(src, n);
    if (pd && ps) {
        memmove(pd, ps, n);
    } else if (dst < src) {
        uint32_t i;
        for (i = 0; i < n; i++) zmem_write8(dst + i, zmem_read8(src + i));
    } else {
        uint32_t i;
        for (i = n; i > 0; i--)
            zmem_write8(dst + i - 1, zmem_read8(src + i - 1));
    }
}

void zbrew_handle_helper(uint32_t id) {
    uint32_t slot = id - ZT_HELPER_BASE;
    uint32_t r0 = g_cpu.r[0], r1 = g_cpu.r[1], r2 = g_cpu.r[2];
    const char *name = slot < ZAEE_HELPER_COUNT ? ZAEE_HELPER_NAMES[slot] : "?";

    switch (slot * 4) {
    case 0x000: /* memmove(dst,src,n) */
        ememmove(r0, r1, r2);
        break;
    case 0x004: { /* memset(dst,val,n) */
        uint32_t i;
        void *p = zmem_host_ptr(r0, r2);
        if (p) memset(p, (int)(r1 & 0xFF), r2);
        else for (i = 0; i < r2; i++) zmem_write8(r0 + i, (uint8_t)r1);
        break;
    }
    case 0x008: { /* strcpy */
        uint32_t i = 0; uint8_t c;
        do { c = zmem_read8(r1 + i); zmem_write8(r0 + i, c); i++; } while (c);
        break;
    }
    case 0x00C: { /* strcat */
        uint32_t d = r0 + estrlen(r0), i = 0; uint8_t c;
        do { c = zmem_read8(r1 + i); zmem_write8(d + i, c); i++; } while (c);
        break;
    }
    case 0x010: { /* strcmp */
        uint32_t i = 0;
        for (;;) {
            uint8_t a = zmem_read8(r0 + i), b = zmem_read8(r1 + i);
            if (a != b) { g_cpu.r[0] = (uint32_t)(int32_t)((int)a - (int)b); break; }
            if (!a) { g_cpu.r[0] = 0; break; }
            i++;
        }
        break;
    }
    case 0x014: /* strlen */
        g_cpu.r[0] = estrlen(r0);
        break;
    case 0x018: { /* strchr */
        uint32_t i = 0; uint8_t c;
        g_cpu.r[0] = 0;
        do {
            c = zmem_read8(r0 + i);
            if (c == (uint8_t)r1) { g_cpu.r[0] = r0 + i; break; }
            i++;
        } while (c);
        break;
    }
    case 0x01C: { /* strrchr */
        uint32_t i = 0, found = 0; uint8_t c;
        do {
            c = zmem_read8(r0 + i);
            if (c == (uint8_t)r1) found = r0 + i;
            i++;
        } while (c);
        g_cpu.r[0] = found;
        break;
    }
    case 0x020: /* sprintf(dst,fmt,...) */
        g_cpu.r[0] = eformat(r0, 0x10000, r1, 2, NULL, 0);
        break;
    case 0x024: { /* wstrcpy (UTF-16) */
        uint32_t i = 0; uint16_t c;
        do { c = zmem_read16(r1 + i * 2); zmem_write16(r0 + i * 2, c); i++; } while (c);
        g_cpu.r[0] = r0;
        break;
    }
    case 0x028: { /* wstrcat */
        uint32_t d = ewstrlen(r0), i = 0;
        uint16_t c;
        do {
            c = zmem_read16(r1 + i * 2);
            zmem_write16(r0 + (d + i) * 2, c);
            i++;
        } while (c);
        g_cpu.r[0] = r0;
        break;
    }
    case 0x02C: { /* wstrcmp */
        uint32_t i = 0;
        for (;;) {
            uint16_t a = zmem_read16(r0 + i * 2), b = zmem_read16(r1 + i * 2);
            if (a != b) { g_cpu.r[0] = (uint32_t)(int32_t)((int)a - (int)b); break; }
            if (!a) { g_cpu.r[0] = 0; break; }
            i++;
        }
        break;
    }
    case 0x034: { /* wstrchr */
        uint32_t i = 0; uint16_t c;
        uint16_t needle = (uint16_t)r1;
        g_cpu.r[0] = 0;
        do {
            c = zmem_read16(r0 + i * 2);
            if (c == needle) { g_cpu.r[0] = r0 + i * 2; break; }
            i++;
        } while (c);
        break;
    }
    case 0x038: { /* wstrrchr */
        uint32_t i = 0, found = 0; uint16_t c;
        uint16_t needle = (uint16_t)r1;
        do {
            c = zmem_read16(r0 + i * 2);
            if (c == needle) found = r0 + i * 2;
            i++;
        } while (c);
        g_cpu.r[0] = found;
        break;
    }
    case 0x03C: /* wsprintf(dst,fmt,...) */
        g_cpu.r[0] = eformat(r0, 0x10000, r1, 2, NULL, 0);
        break;
    case 0x058: { /* wstrlower */
        uint32_t i = 0;
        for (;;) {
            uint16_t c = zmem_read16(r0 + i * 2);
            if (!c) break;
            if (c >= 'A' && c <= 'Z') c = (uint16_t)(c + 32);
            zmem_write16(r0 + i * 2, c);
            i++;
        }
        g_cpu.r[0] = r0;
        break;
    }
    case 0x05C: { /* wstrupper */
        uint32_t i = 0;
        for (;;) {
            uint16_t c = zmem_read16(r0 + i * 2);
            if (!c) break;
            if (c >= 'a' && c <= 'z') c = (uint16_t)(c - 32);
            zmem_write16(r0 + i * 2, c);
            i++;
        }
        g_cpu.r[0] = r0;
        break;
    }
    case 0x068: /* malloc */
        g_cpu.r[0] = zheap_alloc(r0);
        break;
    case 0x06C: case 0x0BC: /* free / sysfree */
        zheap_free(r0);
        break;
    case 0x070: { /* wstrdup */
        uint32_t n = (ewstrlen(r0) + 1) * 2;
        uint32_t d = zheap_alloc(n);
        if (d) ememmove(d, r0, n);
        g_cpu.r[0] = d;
        break;
    }
    case 0x074: /* realloc */
        g_cpu.r[0] = zheap_realloc(r0, r1);
        break;
    case 0x07C: /* wstrsize = bytes incluindo terminador */
        g_cpu.r[0] = (ewstrlen(r0) + 1) * 2;
        break;
    case 0x08C: /* GetAEEVersion -> BREW 3.1 */
        g_cpu.r[0] = 0x03010000u;
        break;
    case 0x090: { /* atoi */
        char buf[32];
        zmem_read_cstr(r0, buf, sizeof(buf));
        g_cpu.r[0] = (uint32_t)atoi(buf);
        break;
    }
    case 0x09C: { /* dbgprintf(fmt,...) */
        char line[512];
        eformat(0, 0, r0, 1, line, sizeof(line));
        LOGI("DBGPRINTF: %s", line);
        break;
    }
    case 0x0A8: { /* GetRand(dst, size) */
        uint32_t i;
        for (i = 0; i < r1; i++) {
            g_rand_state = g_rand_state * 1103515245u + 12345u;
            zmem_write8(r0 + i, (uint8_t)(g_rand_state >> 16));
        }
        break;
    }
    case 0x0AC: /* GetTimeMS (desde meia-noite; usamos uptime) */
    case 0x0B0: /* GetUpTimeMS */
        g_cpu.r[0] = zbrew_uptime_ms();
        break;
    case 0x0B4: /* GetSeconds */
        g_cpu.r[0] = zbrew_uptime_ms() / 1000;
        break;
    case 0x0C0: /* GetAppInstance */
        g_cpu.r[0] = zboot_get_applet_object();
        if (g_cpu.r[0]) {
            LOGI("GetAppInstance -> 0x%08X fields: +0=0x%08X +4=0x%08X "
                 "+8=0x%08X +C=0x%08X +10=0x%08X +14=0x%08X +18=0x%08X",
                 g_cpu.r[0], zmem_read32(g_cpu.r[0]),
                 zmem_read32(g_cpu.r[0] + 4), zmem_read32(g_cpu.r[0] + 8),
                 zmem_read32(g_cpu.r[0] + 12), zmem_read32(g_cpu.r[0] + 16),
                 zmem_read32(g_cpu.r[0] + 20), zmem_read32(g_cpu.r[0] + 24));
        }
        break;
    case 0x0C4: { /* strtoul */
        char buf[64];
        char *end = NULL;
        zmem_read_cstr(r0, buf, sizeof(buf));
        g_cpu.r[0] = (uint32_t)strtoul(buf, &end, (int)r2);
        if (r1) {
            uint32_t consumed = (uint32_t)(end ? (end - buf) : 0);
            zmem_write32(r1, r0 + consumed);
        }
        break;
    }
    case 0x0C8: { /* strncpy */
        uint32_t i;
        uint8_t c = 1;
        for (i = 0; i < r2; i++) {
            if (c) c = zmem_read8(r1 + i);
            zmem_write8(r0 + i, c);
        }
        g_cpu.r[0] = r0;
        break;
    }
    case 0x0CC: { /* strncmp */
        uint32_t i;
        g_cpu.r[0] = 0;
        for (i = 0; i < r2; i++) {
            uint8_t a = zmem_read8(r0 + i), b = zmem_read8(r1 + i);
            if (a != b) { g_cpu.r[0] = (uint32_t)(int32_t)((int)a - (int)b); break; }
            if (!a) break;
        }
        break;
    }
    case 0x0D0: { /* stricmp */
        uint32_t i = 0;
        for (;;) {
            uint8_t a = zmem_read8(r0 + i), b = zmem_read8(r1 + i);
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { g_cpu.r[0] = (uint32_t)(int32_t)((int)a - (int)b); break; }
            if (!a) { g_cpu.r[0] = 0; break; }
            i++;
        }
        break;
    }
    case 0x0D4: /* strnicmp */
        g_cpu.r[0] = (uint32_t)(int32_t)estrncasecmp(r0, r1, r2);
        break;
    case 0x0D8: { /* strstr */
        uint32_t nlen = estrlen(r1), i = 0;
        g_cpu.r[0] = 0;
        if (nlen == 0) { g_cpu.r[0] = r0; break; }
        for (;;) {
            uint8_t c = zmem_read8(r0 + i);
            if (!c) break;
            uint32_t j;
            for (j = 0; j < nlen; j++)
                if (zmem_read8(r0 + i + j) != zmem_read8(r1 + j)) break;
            if (j == nlen) { g_cpu.r[0] = r0 + i; break; }
            i++;
        }
        break;
    }
    case 0x0DC: { /* memcmp */
        uint32_t i;
        g_cpu.r[0] = 0;
        for (i = 0; i < r2; i++) {
            uint8_t a = zmem_read8(r0 + i), b = zmem_read8(r1 + i);
            if (a != b) { g_cpu.r[0] = (uint32_t)(int32_t)((int)a - (int)b); break; }
        }
        break;
    }
    case 0x0E0: { /* memchr */
        uint32_t i;
        g_cpu.r[0] = 0;
        for (i = 0; i < r2; i++)
            if (zmem_read8(r0 + i) == (uint8_t)r1) { g_cpu.r[0] = r0 + i; break; }
        break;
    }
    case 0x0E8: { /* stristr */
        uint32_t nlen = estrlen(r1), i = 0;
        g_cpu.r[0] = 0;
        if (nlen == 0) { g_cpu.r[0] = r0; break; }
        for (;;) {
            uint8_t c = zmem_read8(r0 + i);
            uint32_t j;
            if (!c) break;
            for (j = 0; j < nlen; j++) {
                uint8_t a = zmem_read8(r0 + i + j);
                uint8_t b = zmem_read8(r1 + j);
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) break;
                if (!a) break;
            }
            if (j == nlen) { g_cpu.r[0] = r0 + i; break; }
            i++;
        }
        break;
    }
    case 0x0F0: { /* wstrncmp */
        uint32_t i;
        g_cpu.r[0] = 0;
        for (i = 0; i < r2; i++) {
            uint16_t a = zmem_read16(r0 + i * 2), b = zmem_read16(r1 + i * 2);
            if (a != b) { g_cpu.r[0] = (uint32_t)(int32_t)((int)a - (int)b); break; }
            if (!a) break;
        }
        break;
    }
    case 0x0F4: { /* strdup */
        uint32_t n = estrlen(r0) + 1;
        uint32_t d = zheap_alloc(n);
        if (d) ememmove(d, r0, n);
        g_cpu.r[0] = d;
        break;
    }
    case 0x0F8: { /* strbegins */
        uint32_t n = estrlen(r1);
        g_cpu.r[0] = (estrncasecmp(r0, r1, n) == 0) ? 1u : 0u;
        break;
    }
    case 0x0FC: { /* strends */
        uint32_t sl = estrlen(r0), tl = estrlen(r1);
        if (tl > sl) g_cpu.r[0] = 0;
        else g_cpu.r[0] = (estrncasecmp(r0 + (sl - tl), r1, tl) == 0) ? 1u : 0u;
        break;
    }
    case 0x100: { /* strchrend */
        uint32_t i = 0;
        g_cpu.r[0] = r0 + estrlen(r0);
        for (;;) {
            uint8_t c = zmem_read8(r0 + i);
            if (c == (uint8_t)r1) g_cpu.r[0] = r0 + i;
            if (!c) break;
            i++;
        }
        break;
    }
    case 0x104: { /* strchrsend */
        uint32_t i = 0;
        g_cpu.r[0] = r0 + estrlen(r0);
        for (;;) {
            uint8_t c = zmem_read8(r0 + i);
            uint32_t j = 0;
            if (!c) break;
            while (zmem_read8(r1 + j)) {
                if (c == zmem_read8(r1 + j)) g_cpu.r[0] = r0 + i;
                j++;
            }
            i++;
        }
        break;
    }
    case 0x108: { /* memrchr */
        uint32_t i;
        g_cpu.r[0] = 0;
        for (i = r2; i > 0; i--)
            if (zmem_read8(r0 + i - 1) == (uint8_t)r1) { g_cpu.r[0] = r0 + i - 1; break; }
        break;
    }
    case 0x10C: { /* memchrend */
        uint32_t i;
        g_cpu.r[0] = r0 + r2;
        for (i = 0; i < r2; i++)
            if (zmem_read8(r0 + i) == (uint8_t)r1) { g_cpu.r[0] = r0 + i; break; }
        break;
    }
    case 0x110: { /* memrchrbegin */
        uint32_t i;
        g_cpu.r[0] = r0;
        for (i = r2; i > 0; i--)
            if (zmem_read8(r0 + i - 1) == (uint8_t)r1) { g_cpu.r[0] = r0 + i - 1; break; }
        break;
    }
    case 0x114: { /* strlower */
        uint32_t i = 0;
        for (;;) {
            uint8_t c = zmem_read8(r0 + i);
            if (!c) break;
            if (c >= 'A' && c <= 'Z') c = (uint8_t)(c + 32);
            zmem_write8(r0 + i, c);
            i++;
        }
        g_cpu.r[0] = r0;
        break;
    }
    case 0x118: { /* strupper */
        uint32_t i = 0;
        for (;;) {
            uint8_t c = zmem_read8(r0 + i);
            if (!c) break;
            if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
            zmem_write8(r0 + i, c);
            i++;
        }
        g_cpu.r[0] = r0;
        break;
    }
    case 0x11C: /* wstricmp */
        g_cpu.r[0] = (uint32_t)(int32_t)ewstrncasecmp(r0, r1, ewstrlen(r0) + 1);
        break;
    case 0x120: /* wstrnicmp */
        g_cpu.r[0] = (uint32_t)(int32_t)ewstrncasecmp(r0, r1, r2);
        break;
    case 0x12C: /* swapl */
        g_cpu.r[0] = ((r0 & 0xFF) << 24) | ((r0 & 0xFF00) << 8) |
                     ((r0 >> 8) & 0xFF00) | (r0 >> 24);
        break;
    case 0x130: /* swaps */
        g_cpu.r[0] = ((r0 & 0xFF) << 8) | ((r0 >> 8) & 0xFF);
        break;
    case 0x138: /* GetRAMFree(out_total?, out_max_block?) */
        g_cpu.r[0] = ZMEM_HEAP_SIZE - zheap_used();
        break;
    case 0x13C: /* vsprintf */
        g_cpu.r[0] = eformat(r0, 0x10000, r1, 2, NULL, 0);
        break;
    case 0x140: /* vsnprintf(dst,cap,fmt,va) - va simplificado */
    case 0x144: /* snprintf(dst,cap,fmt,...) */
        g_cpu.r[0] = eformat(r0, r1 ? r1 : 1, r2, 3, NULL, 0);
        break;
    case 0x14C: { /* strlcpy(dst,src,cap) */
        uint32_t i;
        for (i = 0; i + 1 < r2; i++) {
            uint8_t c = zmem_read8(r1 + i);
            zmem_write8(r0 + i, c);
            if (!c) break;
        }
        if (r2) zmem_write8(r0 + i, 0);
        g_cpu.r[0] = estrlen(r1);
        break;
    }
    case 0x150: { /* strlcat */
        uint32_t dlen = estrlen(r0), slen = estrlen(r1), i = 0;
        if (r2 <= dlen) { g_cpu.r[0] = r2 + slen; break; }
        while (dlen + i + 1 < r2) {
            uint8_t c = zmem_read8(r1 + i);
            zmem_write8(r0 + dlen + i, c);
            if (!c) break;
            i++;
        }
        zmem_write8(r0 + dlen + i, 0);
        g_cpu.r[0] = dlen + slen;
        break;
    }
    case 0x154: { /* wstrlcpy */
        uint32_t slen = ewstrlen(r1), i = 0;
        if (r2 > 0) {
            uint32_t capw = r2 / 2;
            uint32_t max = capw > 0 ? capw - 1 : 0;
            while (i < max) {
                uint16_t c = zmem_read16(r1 + i * 2);
                zmem_write16(r0 + i * 2, c);
                if (!c) break;
                i++;
            }
            zmem_write16(r0 + i * 2, 0);
        }
        g_cpu.r[0] = slen;
        break;
    }
    case 0x158: { /* wstrlcat */
        uint32_t dlen = ewstrlen(r0), slen = ewstrlen(r1), i = 0;
        uint32_t capw = r2 / 2;
        if (capw <= dlen) { g_cpu.r[0] = dlen + slen; break; }
        while (dlen + i + 1 < capw) {
            uint16_t c = zmem_read16(r1 + i * 2);
            zmem_write16(r0 + (dlen + i) * 2, c);
            if (!c) break;
            i++;
        }
        zmem_write16(r0 + (dlen + i) * 2, 0);
        g_cpu.r[0] = dlen + slen;
        break;
    }
    case 0x184: /* sleep(ms) - nop no emulador */
        break;
    case 0x188: /* getlasterror */
        g_cpu.r[0] = 0;
        break;
    case 0x194: /* IsBadPtr -> 0 (ponteiro ok) */
        g_cpu.r[0] = 0;
        break;
    case 0x198: { /* basename */
        uint32_t i = estrlen(r0);
        g_cpu.r[0] = r0;
        while (i > 0) {
            uint8_t c = zmem_read8(r0 + i - 1);
            if (c == '/' || c == '\\') { g_cpu.r[0] = r0 + i; break; }
            i--;
        }
        break;
    }
    default: {
        static uint32_t warn_count = 0;
        if (warn_count < 64) {
            LOGW("AEEHelper_%s (slot 0x%03X) nao implementado "
                 "(R0=0x%08X R1=0x%08X LR=0x%08X)",
                 name, slot * 4, r0, r1, g_cpu.r[REG_LR]);
            warn_count++;
        }
        g_cpu.r[0] = 0;
        break;
    }
    }
}
