/* idisplay_real.c - IDisplay BREW real
 * Layout e semantica minima portados de Zeemu/BrewDisplay.cpp (GPL-3.0).
 */
#include <string.h>
#include "brew.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../gpu/framebuffer.h"
#include "../debug/log.h"

#define IDISPLAY_TRAP_BASE (ZT_STUB_BASE + 200u)
#define IDISPLAY_SLOTS 48
#define RGB_NONE 0xFFFFFFFFu
#define IDF_RECT_FRAME 1u
#define IDF_RECT_FILL  2u
#define ADDR_MAX (ZMEM_VRAM_BASE + ZMEM_VRAM_SIZE)
#define FONT_FIRST 0x20
#define FONT_LAST  0x5A

static uint32_t s_text_color = 0xFFFFFF00u;
static uint32_t s_background_color;
static uint32_t s_line_color = 0xFFFFFF00u;
static int16_t s_clip_x, s_clip_y, s_clip_dx = 640, s_clip_dy = 480;
static uint32_t s_dest_bitmap; /* SetDestination; 0 = device */
static uint32_t s_font_normal;

/* Fonte 5x7 (ASCII 0x20..0x5A) — mesma tabela do stub em boot.c. */
static const uint8_t s_font5x7[91][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},
    {0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},
    {0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
    {0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},
    {0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},
    {0x61,0x51,0x49,0x45,0x43},
};

static uint32_t rgbval_to_xrgb(uint32_t rgb)
{
    return 0xFF000000u | ((rgb >> 8) & 0xFFu) << 16 |
           ((rgb >> 16) & 0xFFu) << 8 | ((rgb >> 24) & 0xFFu);
}

static void read_rect(uint32_t p, int *x, int *y, int *w, int *h)
{
    if (!p) {
        *x = 0; *y = 0; *w = 640; *h = 480;
        return;
    }
    *x = (int16_t)zmem_read16(p);
    *y = (int16_t)zmem_read16(p + 2);
    *w = (int16_t)zmem_read16(p + 4);
    *h = (int16_t)zmem_read16(p + 6);
}

/* Thunks Zeebo duplicam R0/R1/R2 na pilha — mesma heuristica do stub. */
static uint32_t find_arg_base(void)
{
    uint32_t sp = g_cpu.r[REG_SP];
    uint32_t r0 = g_cpu.r[0], r1 = g_cpu.r[1], r2 = g_cpu.r[2];
    int depth;
    if (sp == 0 || sp >= ADDR_MAX) return sp;
    for (depth = 0; depth < 4; depth++) {
        if (zmem_read32(sp + 0) != r0 || zmem_read32(sp + 4) != r1 ||
            zmem_read32(sp + 8) != r2)
            break;
        sp += 20;
    }
    if (zmem_read32(sp + 12) == r0)
        sp += 20;
    return sp;
}

static int read_text(uint32_t pch, int n_chars, int force_byte, char *out, int cap)
{
    int limit = n_chars, i, n = 0;
    if (limit < 0 || limit > 4096) limit = 4096;
    if (!pch || pch >= ADDR_MAX) { out[0] = '\0'; return 0; }
    for (i = 0; i < limit && n < cap - 1; i++) {
        uint32_t c = force_byte ? zmem_read8(pch + (uint32_t)i)
                                : zmem_read16(pch + (uint32_t)i * 2u);
        if (!c) break;
        out[n++] = (c == '\n' || c == '\r' || c == '\t' || (c >= 0x20 && c < 0x7F))
                       ? (char)c : '?';
    }
    out[n] = '\0';
    return n;
}

static int resolve_string(uint32_t pch, int n_chars, char *out, int cap)
{
    char ae[160], by[160];
    int ae_n, by_n, ae_s = 0, by_s = 0, i, cp;
    uint32_t object, text = pch;
    int force_byte = 0;
    if (pch && pch < ADDR_MAX) {
        object = zmem_read32(pch);
        if (object && object < ADDR_MAX) {
            text = zmem_read32(object + 8);
            if (text && text < ADDR_MAX) {
                uint8_t c0 = zmem_read8(text);
                if (c0 >= 0x20 && c0 < 0x7F) {
                    force_byte = 1;
                    pch = text;
                }
            }
        }
    }
    if (force_byte)
        return read_text(pch, n_chars, 1, out, cap);
    ae_n = read_text(pch, n_chars, 0, ae, sizeof(ae));
    by_n = read_text(pch, n_chars, 1, by, sizeof(by));
    for (i = 0; i < ae_n; i++) if (ae[i] != '?') ae_s++;
    for (i = 0; i < by_n; i++) if (by[i] != '?') by_s++;
    if (by_n > 0 && by_s > ae_s + 2 && by_n > ae_n) {
        cp = by_n < cap - 1 ? by_n : cap - 1;
        memcpy(out, by, (size_t)cp);
        out[cp] = '\0';
        return cp;
    }
    cp = ae_n < cap - 1 ? ae_n : cap - 1;
    memcpy(out, ae, (size_t)cp);
    out[cp] = '\0';
    return cp;
}

/* IDIB layout: +8 pBmp, +20/22 cx/cy, +24 pitch, +28 depth.
 * Aceita stub QI (ZT_STUB_BASE) OU qualquer vtbl com depth/bits validos —
 * CreateCompatible/zimage podem usar vtbl BMP distinta. */
static int is_idib(uint32_t obj)
{
    uint32_t bits, vtbl;
    uint8_t depth;
    int cx, cy, pitch;
    if (!obj || obj >= ADDR_MAX) return 0;
    vtbl = zmem_read32(obj);
    if (!vtbl || vtbl >= ADDR_MAX) return 0;
    bits = zmem_read32(obj + 8);
    depth = zmem_read8(obj + 28);
    cx = (int)zmem_read16(obj + 20);
    cy = (int)zmem_read16(obj + 22);
    pitch = (int)zmem_read16(obj + 24);
    if (!(depth == 2 || depth == 4 || depth == 16 || depth == 32))
        return 0;
    if (cx <= 0 || cy <= 0 || cx > 2048 || cy > 2048 || pitch <= 0)
        return 0;
    return bits != 0 &&
           (bits < ADDR_MAX ||
            (bits >= ZMEM_VRAM_BASE &&
             bits < ZMEM_VRAM_BASE + ZMEM_VRAM_SIZE));
}

static int idib_bpp(uint32_t obj)
{
    uint8_t d = zmem_read8(obj + 28);
    if (d == 2 || d == 16) return 2;
    if (d == 4 || d == 32) return 4;
    return 4;
}

static uint32_t rgb565_to_xrgb(uint16_t p16)
{
    uint32_t r = ((p16 >> 11) & 0x1Fu) * 255u / 31u;
    uint32_t g = ((p16 >> 5) & 0x3Fu) * 255u / 63u;
    uint32_t b = (p16 & 0x1Fu) * 255u / 31u;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* M2 self-check: 2x2 RGB565 → XRGB. Roda 1x. */
static void rgb565_selfcheck(void)
{
    static int done;
    uint16_t s[4] = {0xF800u, 0x07E0u, 0x001Fu, 0xFFFFu};
    uint32_t e[4] = {0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFFFFFFFFu};
    int i;
    if (done) return;
    done = 1;
    for (i = 0; i < 4; i++) {
        uint32_t got = rgb565_to_xrgb(s[i]);
        if (got != e[i])
            LOGW("rgb565_selfcheck fail i=%d got=0x%08X want=0x%08X",
                 i, got, e[i]);
    }
}

/* M1: dest offscreen DIBM; device/NULL → zfb (VRAM XRGB). */
static int dest_use_zfb(void)
{
    uint32_t bits;
    if (!s_dest_bitmap) return 1;
    if (!is_idib(s_dest_bitmap)) return 1;
    bits = zmem_read32(s_dest_bitmap + 8);
    return bits == ZMEM_VRAM_BASE;
}

static void dest_put_xrgb(int x, int y, uint32_t xrgb)
{
    uint32_t bits;
    int dw, dh, pitch, bpp;
    if (dest_use_zfb()) {
        zfb_set_pixel(x, y, xrgb);
        return;
    }
    bits = zmem_read32(s_dest_bitmap + 8);
    dw = (int)zmem_read16(s_dest_bitmap + 20);
    dh = (int)zmem_read16(s_dest_bitmap + 22);
    pitch = (int)zmem_read16(s_dest_bitmap + 24);
    bpp = idib_bpp(s_dest_bitmap);
    if (x < 0 || y < 0 || x >= dw || y >= dh || !bits || pitch <= 0)
        return;
    if (bpp == 2) {
        uint16_t p16 = (uint16_t)(((xrgb >> 19) & 0x1Fu) << 11 |
                                  ((xrgb >> 10) & 0x3Fu) << 5 |
                                  ((xrgb >> 3) & 0x1Fu));
        zmem_write16(bits + (uint32_t)y * (uint32_t)pitch +
                     (uint32_t)x * 2u, p16);
    } else {
        zmem_write32(bits + (uint32_t)y * (uint32_t)pitch +
                     (uint32_t)x * 4u, xrgb);
    }
}

static void dest_fill_xrgb(int x, int y, int w, int h, uint32_t xrgb)
{
    int xx, yy;
    if (dest_use_zfb()) {
        zfb_fill_rect(x, y, w, h, xrgb);
        return;
    }
    for (yy = y; yy < y + h; yy++)
        for (xx = x; xx < x + w; xx++)
            dest_put_xrgb(xx, yy, xrgb);
}

static void dest_frame_xrgb(int x, int y, int w, int h, uint32_t xrgb)
{
    if (dest_use_zfb()) {
        zfb_draw_rect(x, y, w, h, xrgb);
        return;
    }
    if (w <= 0 || h <= 0) return;
    dest_fill_xrgb(x, y, w, 1, xrgb);
    dest_fill_xrgb(x, y + h - 1, w, 1, xrgb);
    dest_fill_xrgb(x, y, 1, h, xrgb);
    dest_fill_xrgb(x + w - 1, y, 1, h, xrgb);
}

static void draw_char(int x, int y, char ch, uint32_t xrgb)
{
    uint8_t c = (uint8_t)ch;
    int col, row;
    const uint8_t *glyph;
    if (c >= 'a' && c <= 'z') c = (uint8_t)(c & 0xDFu);
    if (c < FONT_FIRST || c > FONT_LAST) c = '?';
    glyph = s_font5x7[c - FONT_FIRST];
    for (col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (row = 0; row < 7; row++) {
            if (bits & (uint8_t)(1u << row))
                dest_put_xrgb(x + col, y + row, xrgb);
        }
    }
}

static void draw_text(int x, int y, const char *s, uint32_t xrgb, int char_w)
{
    int cx = x;
    for (; *s; s++) {
        if (*s == '\n') { y += 8; cx = x; continue; }
        draw_char(cx, y, *s, xrgb);
        cx += char_w;
    }
}

void zbrew_handle_idisplay_real(uint32_t id)
{
    uint32_t slot = id - IDISPLAY_TRAP_BASE;
    static uint32_t s_slot_hits[IDISPLAY_SLOTS];
    static uint32_t s_slot_log;

    if (slot < IDISPLAY_SLOTS) s_slot_hits[slot]++;
    if (s_slot_log < 24) {
        uint32_t sp = g_cpu.r[REG_SP];
        LOGI("IDisplay slot %u hit#%u LR=0x%08X R0=0x%08X R1=0x%08X R2=0x%08X "
             "R3=0x%08X R4=0x%08X R5=0x%08X R6=0x%08X R7=0x%08X SP=0x%08X "
             "stk={%08X %08X %08X %08X %08X %08X %08X %08X}",
             slot, s_slot_hits[slot], g_cpu.r[REG_LR],
             g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[3],
             g_cpu.r[4], g_cpu.r[5], g_cpu.r[6], g_cpu.r[7], sp,
             zmem_read32(sp), zmem_read32(sp + 4), zmem_read32(sp + 8),
             zmem_read32(sp + 12), zmem_read32(sp + 16), zmem_read32(sp + 20),
             zmem_read32(sp + 24), zmem_read32(sp + 28));
        s_slot_log++;
    }

    switch (slot) {
    case 0: /* AddRef */
        g_cpu.r[0] = g_cpu.r[0];
        break;
    case 1: /* Release */
        g_cpu.r[0] = 0;
        break;
    case 2: { /* GetFontMetrics(po, font, AEEFontMetrics*, u16* ascent) */
        uint16_t asc = 12, desc = 4, lead = 2, mw = 8;
        uint32_t pmet = g_cpu.r[2];
        if (!zbrew_font_get_metrics(g_cpu.r[1], &asc, &desc, &lead, &mw)) {
            asc = 12; desc = 4; lead = 2; mw = 8;
        }
        if (pmet) {
            zmem_write16(pmet + 0, asc);
            zmem_write16(pmet + 2, desc);
            zmem_write16(pmet + 4, lead);
            zmem_write16(pmet + 6, mw);
        }
        if (g_cpu.r[3]) zmem_write16(g_cpu.r[3], asc);
        g_cpu.r[0] = (uint32_t)(asc + desc);
        break;
    }
    case 3: { /* MeasureTextEx — thunk so se R2 e trap (nl=-1 != thunk). */
        uint16_t mw = 6, asc = 12, desc = 4, lead = 2;
        uint32_t base = find_arg_base();
        uint32_t r2 = g_cpu.r[2], r3 = g_cpu.r[3];
        /* nl=-1 (0xFFFFFFFF) e RGB_NONE: NAO e endereco de trap. */
        int thunk = (r2 >= ZTRAP_BASE && r2 < ZTRAP_BASE + 0x100000u);
        uint32_t psz = thunk ? g_cpu.r[5] : r2;
        int32_t nl = thunk ? (int32_t)g_cpu.r[6] : (int32_t)r3;
        uint32_t pn_fits = zmem_read32(base + 4);
        char text[160];
        int len;
        if (!zbrew_font_get_metrics(g_cpu.r[1], &asc, &desc, &lead, &mw))
            mw = 6;
        (void)asc; (void)desc; (void)lead;
        len = resolve_string(psz, nl, text, sizeof(text));
        if (pn_fits && pn_fits < ADDR_MAX)
            zmem_write32(pn_fits, (uint32_t)len);
        g_cpu.r[0] = (uint32_t)(len > 0 ? (int)mw * len : 16);
        break;
    }
    case 4: { /* DrawText(po, font, psz, nl, x, y, prcBackground, dwFlags)
               * ARMCC thunk: so quando R2 e trap de vtable — nl=-1 NAO e thunk. */
        uint32_t base = find_arg_base();
        uint32_t r2 = g_cpu.r[2], r3 = g_cpu.r[3];
        int thunk = (r2 >= ZTRAP_BASE && r2 < ZTRAP_BASE + 0x100000u);
        uint32_t psz = thunk ? g_cpu.r[5] : r2;
        int32_t nl = thunk ? -1 : (int32_t)r3;
        int x, y;
        uint32_t prc;
        char text[160];
        int len;
        static uint32_t draw_logs;
        if (thunk) {
            x = (int32_t)zmem_read32(base + 0);
            y = (int32_t)zmem_read32(base + 4);
            prc = zmem_read32(base + 8);
        } else {
            x = (int32_t)zmem_read32(base + 0);
            y = (int32_t)zmem_read32(base + 4);
            prc = zmem_read32(base + 8);
        }
        if (prc && prc < ADDR_MAX &&
            (x < -1024 || x > 4096 || y < -1024 || y > 4096)) {
            x = (int16_t)zmem_read16(prc + 0);
            y = (int16_t)zmem_read16(prc + 2);
        }
        len = resolve_string(psz, nl, text, sizeof(text));
        if (len > 0) {
            uint16_t font_w = 6, asc = 12, desc = 4, lead = 2;
            if (!zbrew_font_get_metrics(g_cpu.r[1], &asc, &desc, &lead, &font_w) ||
                font_w < 4)
                font_w = 6;
            (void)asc; (void)desc; (void)lead;
            draw_text(x, y, text, rgbval_to_xrgb(s_text_color), (int)font_w);
        }
        if (draw_logs < 16) {
            LOGI("IDisplay_DrawText: '%s' em (%d,%d) len=%d psz=0x%08X nl=%d "
                 "thunk=%d r2=0x%08X r5=0x%08X r6=0x%08X base=0x%08X",
                 text, x, y, len, psz, nl, thunk, r2, g_cpu.r[5], g_cpu.r[6],
                 base);
            draw_logs++;
        }
        zbrew_mark_frame();
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;
    }
    case 5: { /* DrawRect(pRect, frame, fill, flags) — M1 honra dest */
        int x, y, w, h;
        uint32_t p_rect = g_cpu.r[1];
        uint32_t frame = g_cpu.r[2], fill = g_cpu.r[3];
        uint32_t flags = zbrew_stack_arg(0);
        static uint32_t draw_rect_logs;
        read_rect(p_rect, &x, &y, &w, &h);
        /* ClearScreen via DrawRect(NULL, NONE, NONE, 0) — usa background. */
        if (!p_rect && frame == RGB_NONE && fill == RGB_NONE) {
            fill = s_background_color;
            flags = IDF_RECT_FILL;
        }
        if ((flags & IDF_RECT_FILL) && fill != RGB_NONE)
            dest_fill_xrgb(x, y, w, h, rgbval_to_xrgb(fill));
        if ((flags & IDF_RECT_FRAME) && frame != RGB_NONE)
            dest_frame_xrgb(x, y, w, h, rgbval_to_xrgb(frame));
        if (draw_rect_logs < 16) {
            LOGI("IDisplay_DrawRect: rect=%d,%d %dx%d frame=0x%08X "
                 "fill=0x%08X flags=0x%X dest=0x%08X",
                 x, y, w, h, frame, fill, flags, s_dest_bitmap);
            draw_rect_logs++;
        }
        zbrew_mark_frame();
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;
    }
    case 6: { /* BitBlt — M2 RGB565→XRGB; M1 dest */
        uint32_t base = find_arg_base();
        int x_dest = (int32_t)g_cpu.r[1];
        int y_dest = (int32_t)g_cpu.r[2];
        int cx = (int32_t)g_cpu.r[3];
        int cy = (int32_t)zmem_read32(base + 0);
        uint32_t src = zmem_read32(base + 4);
        int x_src = (int32_t)zmem_read32(base + 8);
        int y_src = (int32_t)zmem_read32(base + 12);
        uint32_t rop = zmem_read32(base + 16);
        rgb565_selfcheck();
        if (is_idib(src) && cx > 0 && cy > 0) {
            uint32_t bits = zmem_read32(src + 8);
            int sw = (int)zmem_read16(src + 20);
            int sh = (int)zmem_read16(src + 22);
            int pitch = (int)zmem_read16(src + 24);
            int sbpp = idib_bpp(src);
            int xx, yy;
            for (yy = 0; yy < cy; yy++) {
                int sy = y_src + yy;
                if (sy < 0 || sy >= sh) continue;
                for (xx = 0; xx < cx; xx++) {
                    int sx = x_src + xx;
                    uint32_t px;
                    if (sx < 0 || sx >= sw) continue;
                    if (sbpp == 2) {
                        uint16_t p16 = zmem_read16(
                            bits + (uint32_t)sy * (uint32_t)pitch +
                            (uint32_t)sx * 2u);
                        if (rop == 7u && p16 == 0) continue;
                        px = rgb565_to_xrgb(p16);
                    } else {
                        px = zmem_read32(
                            bits + (uint32_t)sy * (uint32_t)pitch +
                            (uint32_t)sx * 4u);
                        if (rop == 7u && px == 0xFF000000u) continue;
                    }
                    dest_put_xrgb(x_dest + xx, y_dest + yy, px);
                }
            }
            zbrew_mark_frame();
        }
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;
    }
    case 7: /* Update */
        zbrew_mark_frame();
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;
    case 13: { /* CreateDIBitmap(po, ppIDIB, depth, width, height) */
        uint32_t base = find_arg_base();
        uint32_t pp = g_cpu.r[1];
        int depth = (int)(g_cpu.r[2] & 0xFFFFu);
        int width = (int)(g_cpu.r[3] & 0xFFFFu);
        int height = (int)(zmem_read32(base + 0) & 0xFFFFu);
        uint32_t dib;
        if (depth != 2 && depth != 16 && depth != 4 && depth != 32)
            depth = 32;
        dib = zbrew_create_device_bitmap(width, height, depth);
        if (pp && pp < ADDR_MAX) zmem_write32(pp, dib);
        LOGI("IDisplay_CreateDIBitmap: %dx%d d=%d -> 0x%08X",
             width, height, depth, dib);
        g_cpu.r[0] = dib ? ZBREW_SUCCESS : 8;
        break;
    }
    case 14: /* SetDestination(po, pBitmap); NULL = device — M1.
              * Guarda qualquer ptr (GetDestination roundtrip; Zuma passa
              * IImage wrapper). Draw/BitBlt so redirecionam se is_idib. */
        s_dest_bitmap = g_cpu.r[1];
        {
            static uint32_t sd_logs;
            if (sd_logs++ < 8)
                LOGI("IDisplay_SetDestination -> 0x%08X idib=%d",
                     s_dest_bitmap,
                     s_dest_bitmap ? is_idib(s_dest_bitmap) : 0);
        }
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;
    case 15: /* GetDestination */
        g_cpu.r[0] = s_dest_bitmap ? s_dest_bitmap : zbrew_device_bitmap();
        break;
    case 0x27: { /* M5: dispatcher estendido RE4 (metodo em R3) */
        uint32_t method = g_cpu.r[3];
        static uint32_t disp_logs;
        if (disp_logs++ < 16)
            LOGI("IDisplay_DISPATCH metodo=%u obj R1=0x%08X R2=0x%08X",
                 method, g_cpu.r[1], g_cpu.r[2]);
        switch (method) {
        case 0x4B: /* ClearScreen */
            dest_fill_xrgb(0, 0, 640, 480, rgbval_to_xrgb(s_background_color));
            zbrew_mark_frame();
            break;
        case 0x54: { /* FillRect via rect@R2, cor=line */
            int x, y, w, h;
            read_rect(g_cpu.r[2], &x, &y, &w, &h);
            dest_fill_xrgb(x, y, w, h, rgbval_to_xrgb(s_line_color));
            zbrew_mark_frame();
            break;
        }
        case 0x66: { /* DrawRect frame via rect@R2 */
            int x, y, w, h;
            read_rect(g_cpu.r[2], &x, &y, &w, &h);
            dest_frame_xrgb(x, y, w, h, rgbval_to_xrgb(s_line_color));
            zbrew_mark_frame();
            break;
        }
        default:
            break;
        }
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;
    }
    case 9: /* Backlight */
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;
    case 10: { /* SetColor(index, RGBVAL), retorna cor anterior */
        uint32_t *color = 0, previous = 0;
        if (g_cpu.r[1] == 1) color = &s_text_color;
        else if (g_cpu.r[1] == 2) color = &s_background_color;
        else if (g_cpu.r[1] == 3) color = &s_line_color;
        if (color) {
            previous = *color;
            if (g_cpu.r[2] != RGB_NONE) *color = g_cpu.r[2];
        }
        g_cpu.r[0] = previous;
        break;
    }
    case 18: /* SetClipRect */
        if (g_cpu.r[1]) {
            int x, y, w, h;
            read_rect(g_cpu.r[1], &x, &y, &w, &h);
            s_clip_x = (int16_t)x; s_clip_y = (int16_t)y;
            s_clip_dx = (int16_t)w; s_clip_dy = (int16_t)h;
        } else {
            s_clip_x = s_clip_y = 0; s_clip_dx = 640; s_clip_dy = 480;
        }
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;
    case 19: /* GetClipRect */
        if (!g_cpu.r[1]) { g_cpu.r[0] = 4; break; }
        zmem_write16(g_cpu.r[1], (uint16_t)s_clip_x);
        zmem_write16(g_cpu.r[1] + 2, (uint16_t)s_clip_y);
        zmem_write16(g_cpu.r[1] + 4, (uint16_t)s_clip_dx);
        zmem_write16(g_cpu.r[1] + 6, (uint16_t)s_clip_dy);
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;
    case 17: { /* SetFont(nFont, pFont), retorna fonte anterior */
        uint32_t previous = s_font_normal;
        if (g_cpu.r[1] == 0x8000u && g_cpu.r[2] && g_cpu.r[2] < ADDR_MAX)
            s_font_normal = g_cpu.r[2];
        g_cpu.r[0] = previous;
        break;
    }
    case 16: { /* GetDeviceBitmap — mesmo IDIB do DISPLAY1/QEGL (VRAM). */
        uint32_t dib = zbrew_device_bitmap();
        rgb565_selfcheck();
        if (g_cpu.r[1])
            zmem_write32(g_cpu.r[1], dib);
        LOGI("IDisplay_GetDeviceBitmap -> 0x%08X", dib);
        g_cpu.r[0] = dib ? ZBREW_SUCCESS : ZBREW_EFAILED;
        break;
    }
    case 22: /* IsEnabled */
        g_cpu.r[0] = 1;
        break;
    default:
        /* SUCCESS: EFAILED em slot critico derruba CreateInstance do FP. */
        LOGW("IDisplay real slot %u nao implementado", slot);
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;
    }
}

uint32_t zbrew_make_idisplay_real(int relative)
{
    uint32_t vtbl = zheap_alloc(IDISPLAY_SLOTS * 4u);
    /* AH (0xAAD0) faz ldr r8,[display,#0x14]; GetDeviceBitmap via r8.
     * Objeto precisa cobrir +0x14; self-ptr la faz o slot 16 cair neste IDisplay. */
    uint32_t obj = zheap_alloc(64);
    int i;
    if (!vtbl || !obj) return 0;
    memset((void *)zmem_host_ptr(obj, 64), 0, 64);
    s_dest_bitmap = 0;
    s_font_normal = 0;
    /* Zeemu grava enderecos absolutos em todos os 48 slots. RE4 chama
     * DrawRect (5), SetColor (10) e SetFont (17) via ldr/bx direto. */
    (void)relative;
    for (i = 0; i < IDISPLAY_SLOTS; ++i)
        zmem_write32(vtbl + (uint32_t)i * 4u,
                     ZTRAP_ADDR(IDISPLAY_TRAP_BASE + (uint32_t)i));
    zmem_write32(obj, vtbl);
    zmem_write32(obj + 4, AEECLSID_DISPLAY);
    zmem_write32(obj + 0x14, obj); /* self: AH GetDeviceBitmap via +0x14 */
    LOGI("IDisplay real (48 slots) criado em 0x%08X rel=%d", obj, relative);
    return obj;
}
