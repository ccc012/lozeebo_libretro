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

static uint32_t estrsize(uint32_t s) {
    return estrlen(s) + 1;
}

static uint32_t eutf8towstr(uint32_t src, uint32_t src_bytes,
                            uint32_t dst, uint32_t dst_bytes) {
    uint32_t si = 0, di = 0, capacity = dst_bytes / 2;
    while (si < src_bytes) {
        uint32_t cp;
        uint8_t c = zmem_read8(src + si++);
        if (!c) break;
        if (c < 0x80) cp = c;
        else if (c >= 0xC2 && c <= 0xDF && si < src_bytes) {
            uint8_t c1 = zmem_read8(src + si);
            if ((c1 & 0xC0) != 0x80) cp = 0xFFFD;
            else { si++; cp = ((uint32_t)(c & 0x1F) << 6) | (c1 & 0x3F); }
        } else if (c >= 0xE0 && c <= 0xEF && si + 1 < src_bytes) {
            uint8_t c1 = zmem_read8(src + si), c2 = zmem_read8(src + si + 1);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 ||
                (c == 0xE0 && c1 < 0xA0) || (c == 0xED && c1 >= 0xA0)) cp = 0xFFFD;
            else { si += 2; cp = ((uint32_t)(c & 0x0F) << 12) |
                                    ((uint32_t)(c1 & 0x3F) << 6) | (c2 & 0x3F); }
        } else if (c >= 0xF0 && c <= 0xF4 && si + 2 < src_bytes) {
            uint8_t c1 = zmem_read8(src + si), c2 = zmem_read8(src + si + 1);
            uint8_t c3 = zmem_read8(src + si + 2);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 ||
                (c3 & 0xC0) != 0x80 || (c == 0xF0 && c1 < 0x90) ||
                (c == 0xF4 && c1 >= 0x90)) cp = 0xFFFD;
            else { si += 3; cp = ((uint32_t)(c & 7) << 18) |
                                    ((uint32_t)(c1 & 0x3F) << 12) |
                                    ((uint32_t)(c2 & 0x3F) << 6) | (c3 & 0x3F); }
        } else cp = 0xFFFD;
        if (cp <= 0xFFFF) {
            if (di + 1 >= capacity) break;
            zmem_write16(dst + di++ * 2, (uint16_t)cp);
        } else {
            if (di + 2 >= capacity) break;
            cp -= 0x10000;
            zmem_write16(dst + di++ * 2, (uint16_t)(0xD800 | (cp >> 10)));
            zmem_write16(dst + di++ * 2, (uint16_t)(0xDC00 | (cp & 0x3FF)));
        }
    }
    if (capacity) zmem_write16(dst + di * 2, 0);
    return di;
}

static void estrlower(uint32_t s) {
    uint32_t i = 0;
    for (;;) {
        uint8_t c = zmem_read8(s + i);
        if (!c) break;
        if (c >= 'A' && c <= 'Z') c = (uint8_t)(c + 32);
        zmem_write8(s + i, c);
        i++;
    }
}

static void estrupper(uint32_t s) {
    uint32_t i = 0;
    for (;;) {
        uint8_t c = zmem_read8(s + i);
        if (!c) break;
        if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
        zmem_write8(s + i, c);
        i++;
    }
}

static uint32_t ewstrsize(uint32_t s) {
    return (ewstrlen(s) + 1) * 2;
}

static uint32_t guest_basename(uint32_t path) {
    uint32_t i = estrlen(path);
    while (i > 0) {
        uint8_t c = zmem_read8(path + i - 1);
        if (c == '/' || c == '\\') return path + i;
        i--;
    }
    return path;
}

static uint32_t guest_stribegins(uint32_t a, uint32_t b) {
    uint32_t n = estrlen(b);
    return estrncasecmp(a, b, n) == 0 ? 1u : 0u;
}

static void guest_strtowstr(uint32_t dst, uint32_t src, uint32_t cap_chars) {
    uint32_t i = 0;
    if (!dst || !cap_chars) return;
    for (; i + 1 < cap_chars; i++) {
        uint8_t c = zmem_read8(src + i);
        zmem_write16(dst + i * 2, c);
        if (!c) return;
    }
    zmem_write16(dst + i * 2, 0);
}

static void guest_wstrtostr(uint32_t dst, uint32_t src, uint32_t cap_bytes) {
    uint32_t i = 0;
    if (!dst || !cap_bytes) return;
    for (; i + 1 < cap_bytes; i++) {
        uint16_t c = zmem_read16(src + i * 2);
        zmem_write8(dst + i, (uint8_t)(c & 0xFF));
        if (!c) return;
    }
    zmem_write8(dst + i, 0);
}

static uint32_t guest_atoi(uint32_t s) {
    char buf[64];
    zmem_read_cstr(s, buf, sizeof(buf));
    return (uint32_t)atoi(buf);
}

static uint32_t guest_strtod_bits(uint32_t s, uint32_t endptr) {
    char buf[128];
    char *end = NULL;
    double d;
    uint64_t raw;
    zmem_read_cstr(s, buf, sizeof(buf));
    d = strtod(buf, &end);
    memcpy(&raw, &d, sizeof(raw));
    if (endptr && endptr < 0xFF000000u) {
        zmem_write32(endptr, s + (uint32_t)(end ? (end - buf) : 0));
    }
    return (uint32_t)raw;
}

static uint32_t guest_get_julian_date(void) {
    return zbrew_uptime_ms() / 1000u;
}

static uint32_t setup_native_image(uint32_t clsid, uint32_t buffer,
                                   uint32_t info, uint32_t realloced) {
    static uint32_t bitmap_vtbl;
    uint32_t pixel_offset, dib_size, width, abs_height;
    uint32_t stride, pixel_bytes, pixels, object, x, y;
    uint16_t planes, bpp;
    int32_t signed_height;

    if (realloced) zmem_write8(realloced, 0);
    if (clsid != 0x01004001u || !buffer ||
        zmem_read8(buffer) != 'B' || zmem_read8(buffer + 1) != 'M')
        return 0;

    pixel_offset = zmem_read32(buffer + 10);
    dib_size = zmem_read32(buffer + 14);
    width = zmem_read32(buffer + 18);
    signed_height = (int32_t)zmem_read32(buffer + 22);
    planes = zmem_read16(buffer + 26);
    bpp = zmem_read16(buffer + 28);
    if (dib_size < 40 || !width || width > 2048 || !signed_height ||
        signed_height == INT32_MIN || planes != 1 || zmem_read32(buffer + 30) != 0 ||
        (bpp != 8 && bpp != 24 && bpp != 32))
        return 0;
    abs_height = signed_height < 0 ? (uint32_t)-signed_height : (uint32_t)signed_height;
    if (abs_height > 2048 || width > (UINT32_MAX - 31u) / bpp)
        return 0;
    stride = ((width * bpp + 31u) / 32u) * 4u;
    if (abs_height > UINT32_MAX / stride ||
        !zmem_host_ptr(buffer + pixel_offset, stride * abs_height) ||
        width > UINT32_MAX / 4u || abs_height > UINT32_MAX / (width * 4u))
        return 0;

    pixel_bytes = width * abs_height * 4u;
    pixels = zheap_alloc(pixel_bytes);
    object = zheap_alloc(16);
    if (!pixels || !object) return 0;
    if (!bitmap_vtbl) {
        bitmap_vtbl = zheap_alloc(16);
        if (!bitmap_vtbl) return 0;
        zmem_write32(bitmap_vtbl + 0, ZTRAP_ADDR(ZT_BMP_ADDREF));
        zmem_write32(bitmap_vtbl + 4, ZTRAP_ADDR(ZT_BMP_RELEASE));
        zmem_write32(bitmap_vtbl + 8, ZTRAP_ADDR(ZT_BMP_GETINFO));
        zmem_write32(bitmap_vtbl + 12, ZTRAP_ADDR(ZT_BMP_BLT));
    }
    zmem_write32(object, bitmap_vtbl);
    zmem_write32(object + 4, width);
    zmem_write32(object + 8, abs_height);
    zmem_write32(object + 12, pixels);

    for (y = 0; y < abs_height; y++) {
        uint32_t src_y = signed_height < 0 ? y : abs_height - 1u - y;
        uint32_t row = buffer + pixel_offset + src_y * stride;
        for (x = 0; x < width; x++) {
            uint8_t r, g, b;
            if (bpp == 8) {
                uint32_t palette = buffer + 14u + dib_size +
                                   (uint32_t)zmem_read8(row + x) * 4u;
                b = zmem_read8(palette);
                g = zmem_read8(palette + 1);
                r = zmem_read8(palette + 2);
            } else {
                uint32_t source = row + x * (bpp / 8u);
                b = zmem_read8(source);
                g = zmem_read8(source + 1);
                r = zmem_read8(source + 2);
            }
            zmem_write32(pixels + (y * width + x) * 4u,
                         0xFF000000u | ((uint32_t)r << 16) |
                         ((uint32_t)g << 8) | b);
        }
    }
    if (info) {
        zmem_write16(info, (uint16_t)width);
        zmem_write16(info + 2, (uint16_t)abs_height);
        zmem_write16(info + 4, 0);
        zmem_write8(info + 6, 0);
        zmem_write16(info + 8, (uint16_t)width);
    }
    if (realloced) zmem_write8(realloced, 1);
    LOGI("SetupNativeImage WINBMP %ux%u %ubpp -> 0x%08X pixels=0x%08X",
         width, abs_height, bpp, object, pixels);
    return object;
}

/* mini vsnprintf sobre memoria emulada.
 * fmt em fmt_addr; argumentos comecam no indice arg_start da sequencia
 * R1,R2,R3,[SP],[SP+4]... Escreve em dst (se dst!=0) limitado a cap.
 * Retorna o tamanho gerado. Se dst==0, escreve em host_out (para log). */
static uint32_t eformat(uint32_t dst, uint32_t cap, uint32_t fmt_addr,
                         int arg_start, uint32_t va_addr,
                         char *host_out, uint32_t host_cap) {
    uint32_t out = 0;
    int argn = arg_start;
    uint32_t va_index = 0;
    uint32_t va_base = va_addr;
    uint32_t fi = 0;

    /* va_list BREW: o primeiro word pode ser um ponteiro para a lista real
     * (wrappers variadic dos modulos montam [va]=&args). Segue uma vez. */
    if (va_addr) {
        uint32_t candidate = zmem_read32(va_addr);
        if (candidate && candidate < 0xFF000000u)
            va_base = candidate;
    }

#define NEXT_ARG() (va_addr ? zmem_read32(va_base + va_index++ * 4) \
                              : (argn <= 3 ? g_cpu.r[argn++] \
                                           : zbrew_stack_arg((argn++) - 4)))
#define EMIT(c) do { \
        if (dst && out < cap - 1) zmem_write8(dst + out, (uint8_t)(c)); \
        if (host_out && out < host_cap - 1) host_out[out] = (char)(c); \
        out++; \
    } while (0)

    for (;;) {
        uint8_t c = zmem_read8(fmt_addr + fi++);
        if (!c) break;
        if (c != '%') { EMIT(c); continue; }

        /* Largura decimal e zero-padding usados por RE4: "%02d" -> "00". */
        {
            int width = 0;
            int zero_pad = 0;
            c = zmem_read8(fmt_addr + fi++);
            if (c == '0') zero_pad = 1;
            while (c >= '0' && c <= '9') {
                width = width * 10 + (c - '0');
                c = zmem_read8(fmt_addr + fi++);
            }
            while (c == '-' || c == '+' || c == '.' || c == 'l' || c == 'h')
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
            char buf[32];
            int len = snprintf(buf, sizeof(buf), zero_pad ? "%0*d" : "%*d",
                               width, (int)NEXT_ARG());
            int k;
            for (k = 0; k < len; k++) EMIT(buf[k]);
            break;
        }
        case 'u': {
            char buf[32];
            int len = snprintf(buf, sizeof(buf), zero_pad ? "%0*u" : "%*u",
                               width, NEXT_ARG());
            int k;
            for (k = 0; k < len; k++) EMIT(buf[k]);
            break;
        }
        case 'x': case 'X': case 'p': {
            char buf[32];
            int len = snprintf(buf, sizeof(buf), zero_pad ? "%0*x" : "%*x",
                               width, NEXT_ARG());
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
    }
    if (dst) zmem_write8(dst + (out < cap ? out : cap - 1), 0);
    if (host_out) host_out[out < host_cap ? out : host_cap - 1] = '\0';
    return out;
#undef NEXT_ARG
#undef EMIT
}

/* copia bloco na memoria emulada (enderecos podem se sobrepor) */
static void ememmove(uint32_t dst, uint32_t src, uint32_t n) {
    /* Nao transformar ponteiro/tamanho guest corrompido em loop de bilhoes. */
    if (n > ZMEM_HEAP_SIZE || !zmem_host_ptr(src, n) ||
        (!zmem_host_ptr(dst, n) && !zbrew_ifile_block_write(dst, n))) {
        static uint32_t bad_logs;
        if (bad_logs++ < 8)
            LOGW("memmove ignorado: dst=0x%08X src=0x%08X n=%u LR=0x%08X",
                 dst, src, n, g_cpu.r[REG_LR]);
        return;
    }
    /* byte path se cruza IFile protegido (evita host_ptr bypass) */
    if (zbrew_ifile_block_write(dst, n)) {
        uint32_t i;
        if (dst < src)
            for (i = 0; i < n; i++) zmem_write8(dst + i, zmem_read8(src + i));
        else
            for (i = n; i > 0; i--)
                zmem_write8(dst + i - 1, zmem_read8(src + i - 1));
        return;
    }
    {
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
}

void zbrew_handle_helper(uint32_t id) {
    uint32_t slot = id - ZT_HELPER_BASE;
    uint32_t r0 = g_cpu.r[0], r1 = g_cpu.r[1], r2 = g_cpu.r[2];
    uint32_t r3 = g_cpu.r[3];
    const char *name = slot < ZAEE_HELPER_COUNT ? ZAEE_HELPER_NAMES[slot] : "?";

    {
        static uint32_t seen[16];
        if (slot < ZAEE_HELPER_COUNT && !(seen[slot >> 5] & (1u << (slot & 31)))) {
            seen[slot >> 5] |= 1u << (slot & 31);
            LOGI("helper #%u (0x%03X) '%s' R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X LR=0x%08X",
                 slot, (id - ZT_HELPER_BASE) * 4, name, r0, r1, r2, r3,
                 g_cpu.r[REG_LR]);
        }
    }

    switch (slot * 4) {
    case 0x000: /* memmove(dst,src,n) */
        ememmove(r0, r1, r2);
        break;
    case 0x004: { /* memset(dst,val,n) */
        uint32_t i;
        if (zbrew_ifile_block_write(r0, r2)) {
            for (i = 0; i < r2; i++) zmem_write8(r0 + i, (uint8_t)r1);
            break;
        }
        {
            void *p = zmem_host_ptr(r0, r2);
            if (p) memset(p, (int)(r1 & 0xFF), r2);
            else for (i = 0; i < r2; i++) zmem_write8(r0 + i, (uint8_t)r1);
        }
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
        g_cpu.r[0] = eformat(r0, 0x10000, r1, 2, 0, NULL, 0);
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
    case 0x030: /* wstrlen */
        g_cpu.r[0] = ewstrlen(r0);
        break;
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
        g_cpu.r[0] = eformat(r0, 0x10000, r1, 2, 0, NULL, 0);
        break;
    case 0x040: /* STRTOWSTR(cs, wcs, wcsBufferSize) - AEEStdLib: src primeiro */
        guest_strtowstr(r1, r0, r2);
        break;
    case 0x044: /* WSTRTOSTR(ws, s, sizeInBytes) - AEEStdLib: src primeiro */
        guest_wstrtostr(r1, r0, r2);
        break;
    case 0x050: /* utf8towstr(src, srcBytes, dst, dstBytes) */
        g_cpu.r[0] = eutf8towstr(r0, r1, r2, r3);
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
    case 0x064: /* SetupNativeImage(cls,pBuffer,pInfo,pbRealloc) */
        g_cpu.r[0] = setup_native_image(r0, r1, r2, r3);
        break;
    case 0x068: /* malloc */
    {
        uint32_t size = r0 & 0x7FFFFFFFu;
        /* O heap guest tem 32 MiB; rejeitar cedo evita lixo virar alocacao. */
        g_cpu.r[0] = size <= ZMEM_HEAP_SIZE ? zheap_alloc(r0) : 0;
        {
            static uint32_t mlog = 0;
            if (mlog < 200) {
                LOGI("MALLOC size=%u -> 0x%08X (usado=%u)", size,
                     g_cpu.r[0], (unsigned)zheap_used());
                mlog++;
            }
        }
        if (!g_cpu.r[0]) {
            static uint32_t malloc_fail_logs = 0;
            if (malloc_fail_logs < 10) {
                LOGW("AEEHelper_malloc FALHOU para %u bytes (heap usado=%u)", size,
                     (unsigned)zheap_used());
                malloc_fail_logs++;
            }
        }
        break;
    }
    case 0x06C: case 0x0BC: /* free / sysfree */
        /* Guest às vezes chama free() em IFile; destruir aqui causa UAF.
         * Lifetime COM só via Release. */
        if (zbrew_ifile_is_live(r0))
            break;
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
        g_cpu.r[0] = ewstrsize(r0);
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
        eformat(0, 0, r0, 1, 0, line, sizeof(line));
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
    case 0x0B8: /* GetJulianDate */
        g_cpu.r[0] = guest_get_julian_date();
        break;
    case 0x0C0: /* GetAppInstance */
        g_cpu.r[0] = zboot_get_applet_object();
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
        estrlower(r0);
        g_cpu.r[0] = r0;
        break;
    }
    case 0x118: { /* strupper */
        estrupper(r0);
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
    case 0x138: { /* GetRAMFree(out_total?, out_max_block?) */
        g_cpu.r[0] = ZMEM_HEAP_SIZE - zheap_used();
        static uint32_t ramfree_logs = 0;
        if (ramfree_logs < 8) {
            LOGW("AEEHelper_GetRAMFree -> %u bytes livres (heap total=%u usado=%u)",
                 g_cpu.r[0], (unsigned)ZMEM_HEAP_SIZE, (unsigned)zheap_used());
            ramfree_logs++;
        }
        break;
    }
    case 0x13C: /* vsprintf(dst,fmt,va_list) - R2 e ponteiro para a lista */
        {
            char fbuf[96];
            int k;
            zmem_read_cstr(r1, fbuf, sizeof(fbuf));
            if (strstr(fbuf, "%s") || strstr(fbuf, "%d")) {
                uint32_t va[8];
                for (k = 0; k < 8; k++) va[k] = zmem_read32(r2 + k * 4);
                LOGI("vsprintf fmt='%s' dst=0x%08X va[0..7]=0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X LR=0x%08X",
                     fbuf, r0, va[0], va[1], va[2], va[3], va[4], va[5], va[6], va[7], g_cpu.r[REG_LR]);
                for (k = 0; k < 8; k++) {
                    if (va[k] >= 0x100000 && va[k] < 0x1800000) {
                        char cbuf[80];
                        zmem_read_cstr(va[k], cbuf, sizeof(cbuf));
                        LOGI("  va[%d]=0x%08X -> '%s'", k, va[k], cbuf);
                    }
                }
            }
            g_cpu.r[0] = eformat(r0, 0x10000, r1, 0, r2, NULL, 0);
        }
        break;
    case 0x140: /* vsnprintf(dst,cap,fmt,va_list) - R3 e ponteiro para a lista */
    {
        uint32_t res = eformat(r0, r1 ? r1 : 1, r2, 0, r3, NULL, 0);
        if (getenv("ZEEMU_TRACE_FMT")) {
            static int vlog = 0;
            if (vlog++ < 60) {
                char fb[96], vb[160];
                zmem_read_cstr(r2, fb, sizeof(fb));
                zmem_read_cstr(r0, vb, sizeof(vb));
                LOGI("vsnprintf cap=%u fmt='%s' -> '%s' (ret=%u)",
                     r1, fb, vb, res);
            }
        }
        g_cpu.r[0] = res;
        break;
    }
    case 0x144: /* snprintf(dst,cap,fmt,...) - varargs normais a partir de R3 */
        g_cpu.r[0] = eformat(r0, r1 ? r1 : 1, r2, 3, 0, NULL, 0);
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
    case 0x184: /* sleep(ms) - guest usa isso em busy-wait tipo
                 * "while (GetUpTimeMS()-t0 <= N) sleep(1);"; se nao
                 * avancarmos o relogio aqui o loop nunca sai (trava
                 * o frame inteiro girando GetUpTimeMS/sleep). */
        zbrew_tick_ms(r0 ? r0 : 1);
        break;
    case 0x188: /* getlasterror */
        g_cpu.r[0] = 0;
        break;
    case 0x190: /* dbgevent */
        break;
    case 0x194: /* IsBadPtr -> 0 (ponteiro ok) */
        g_cpu.r[0] = 0;
        break;
    case 0x198: { /* basename */
        g_cpu.r[0] = guest_basename(r0);
        break;
    }
    case 0x0A0: /* wstrcompress */
    case 0x0A4: /* LocalTimeOffset */
        g_cpu.r[0] = 0;
        break;
    case 0x0E4: { /* strexpand(src, srcLen, dst, dstLen) */
        uint32_t src = g_cpu.r[0], dst = g_cpu.r[2];
        uint32_t src_len = g_cpu.r[1], dst_cap = g_cpu.r[3];
        static uint32_t expand_logs = 0;
        uint32_t n = src_len;
        if (n >= dst_cap) n = dst_cap - 1;
        if (src >= 0xFF000000u || dst >= 0xFF000000u || dst == 0) {
            g_cpu.r[0] = 0;
            break;
        }
        if (expand_logs < 6 && src && src < 0xFF000000u) {
            char s[128];
            s[0] = '\0';
            zmem_read_cstr(src, s, sizeof(s));
            LOGW("AEEHelper_strexpand '%s' -> dst=0x%08X (n=%u) LR=0x%08X",
                 s, dst, n, g_cpu.r[REG_LR]);
            expand_logs++;
        }
        if (src && src < 0xFF000000u) {
            char s[16];
            s[0] = '\0';
            zmem_read_cstr(src, s, sizeof(s));
            if (!strncmp(s, "LOAD", 4) || !strncmp(s, "ERROR", 5) ||
                !strncmp(s, "LIST", 4)) {
                static uint32_t ld_hits = 0;
                if (ld_hits < 10) {
                    uint32_t sp = g_cpu.r[REG_SP];
                    LOGW("LOADSCREEN '%s' LR=0x%08X cLR=0x%08X cLR2=0x%08X "
                         "r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X",
                         s, g_cpu.r[REG_LR], zmem_read32(sp + 0x224),
                         zmem_read32(sp + 0x228), src, src_len, dst, dst_cap);
                    ld_hits++;
                }
            }
        }
        {
            uint32_t i;
            for (i = 0; i < n; i++) {
                uint8_t b = zmem_read8(src + i);
                if (b == 0) break;
                zmem_write8(dst + i, b);
            }
            zmem_write8(dst + i, 0);
        }
        g_cpu.r[0] = dst;
        break;
    }
    case 0x0EC: /* memstr */
        g_cpu.r[0] = 0;
        break;
    case 0x128: /* inet_aton */
        g_cpu.r[0] = 0;
        break;
    case 0x134: /* GetFSFree */
        g_cpu.r[0] = 64u * 1024u * 1024u / 2u;
        break;
    case 0x148: /* JulianToSeconds */
        g_cpu.r[0] = r0 * 86400u;
        break;
    case 0x15C: /* setstaticptr */
    case 0x160: /* f_assignstr */
    case 0x164: /* f_assignint */
    case 0x168: /* wwritelong */
    case 0x16C: /* dbgheapmark */
    case 0x170: /* lockmem */
    case 0x174: /* unlockmem */
    case 0x178: /* dumpheap */
        g_cpu.r[0] = 0;
        break;
    case 0x17C: /* strtod */
        g_cpu.r[0] = guest_strtod_bits(r0, r1);
        break;
    case 0x180: /* f_calc */
        g_cpu.r[0] = 0;
        break;
    case 0x18C: /* wgs84_to_degrees */
        g_cpu.r[0] = 0;
        break;
    case 0x19C: /* makepath */
    case 0x1A0: /* splitpath */
    case 0x1B0: /* f_get */
    case 0x1B4: /* qsort */
    case 0x1B8: /* trunc */
    case 0x1BC: /* utrunc */
    case 0x1C0: /* err_realloc */
    case 0x1C4: /* err_strdup */
    case 0x1C8: /* inet_pton */
    case 0x1CC: /* inet_ntop */
    case 0x1D0: /* GetALSContext */
        g_cpu.r[0] = 0;
        break;
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
