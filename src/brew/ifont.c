/* ifont.c - IFont (AEEFont.h) HLE de fontes do sistema Zeebo
 *
 * Jogos criam fontes via IShell_CreateInstance com os AEECLSID_FONT_*
 * (ex.: Double Dragon pede AEECLSID_FONT_STANDARD11/15/36) e recebem um
 * objeto IFont com vtable de 6 slots (AddRef, Release, QueryInterface,
 * DrawText, GetFontMetrics, MeasureText). As metricas vem da tabela do
 * zeemu (BrewFont.cpp) - mesmo resolve_clsid usado la.
 */
#include <string.h>
#include "brew.h"
#include "aee_ids.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../debug/log.h"

#define ZFONT_MAX 24

/* objeto: +0 vtable +4 clsid +8..+16 metricas +20 refs */
#define ZFONT_OBJ_BYTES 24u

typedef struct {
    uint32_t clsid;
    uint32_t obj;
    uint16_t ascent;
    uint16_t descent;
    uint16_t leading;
    uint16_t max_char_width;
    uint16_t height;
    uint16_t refs;
} zfont_state_t;

static zfont_state_t g_fonts[ZFONT_MAX];
static uint32_t      g_font_vtbl = 0;
static int           g_font_count = 0;

bool zbrew_is_font_clsid(uint32_t clsid) {
    switch (clsid) {
    case AEECLSID_FONT_FIXED4X6:
    case AEECLSID_FONT_BASIC6:
    case AEECLSID_FONT_BASIC9:
    case AEECLSID_FONT_BASIC10:
    case AEECLSID_FONT_BASIC11:
    case AEECLSID_FONT_BASIC11B:
    case AEECLSID_FONT_BASIC14:
    case AEECLSID_FONT_BASIC12:
    case AEECLSID_FONT_BASIC12B:
    case AEECLSID_FONT_BASIC15:
    case AEECLSID_BITFONTFOUNDRY:
    case AEECLSID_FONT:
    case AEECLSID_FONTSYS_NORMAL:
    case AEECLSID_FONTSYS_LARGE:
    case AEECLSID_FONTSYS_BOLD:
    case AEECLSID_FONTSYS_ITALIC:
    case AEECLSID_FONTSYS_BOLDITALIC:
    case AEECLSID_FONTSYS_LARGEITALLIC:
    case AEECLSID_FONT_STANDARD11:
    case AEECLSID_FONT_STANDARD11B:
    case AEECLSID_FONT_STANDARD15:
    case AEECLSID_FONT_STANDARD15B:
    case AEECLSID_FONT_STANDARD18:
    case AEECLSID_FONT_STANDARD18B:
    case AEECLSID_FONT_STANDARD23:
    case AEECLSID_FONT_STANDARD23B:
    case AEECLSID_FONT_STANDARD26:
    case AEECLSID_FONT_STANDARD26B:
    case AEECLSID_FONT_STANDARD36:
        return true;
    default:
        return false;
    }
}

/* Metricas por CLSID - fonte: zeemu BrewFont.cpp (mesma tabela). */
static void font_metrics_for_clsid(uint32_t clsid, zfont_state_t *m) {
    memset(m, 0, sizeof(*m));
    m->clsid = clsid;
    switch (clsid) {
    case AEECLSID_FONT_FIXED4X6:
        m->ascent = 4;  m->descent = 2; m->leading = 0;
        m->max_char_width = 4; m->height = 6;
        break;
    case AEECLSID_FONT_BASIC6:
        m->ascent = 6;  m->descent = 2; m->leading = 1;
        m->max_char_width = 5; m->height = 9;
        break;
    case AEECLSID_FONT_BASIC9:
        m->ascent = 9;  m->descent = 3; m->leading = 1;
        m->max_char_width = 6; m->height = 13;
        break;
    case AEECLSID_FONT_BASIC10:
        m->ascent = 10; m->descent = 3; m->leading = 1;
        m->max_char_width = 7; m->height = 14;
        break;
    case AEECLSID_FONT_BASIC11:
    case AEECLSID_FONTSYS_NORMAL:
    case AEECLSID_FONTSYS_ITALIC:
    case AEECLSID_FONT_STANDARD11:
    case AEECLSID_FONT:
        m->ascent = 13; m->descent = 3; m->leading = 2;
        m->max_char_width = 8; m->height = 18;
        break;
    case AEECLSID_FONT_BASIC11B:
    case AEECLSID_FONTSYS_BOLD:
    case AEECLSID_FONTSYS_BOLDITALIC:
    case AEECLSID_FONT_STANDARD11B:
        m->ascent = 13; m->descent = 3; m->leading = 2;
        m->max_char_width = 8; m->height = 18;
        break;
    case AEECLSID_FONT_BASIC14:
        m->ascent = 14; m->descent = 4; m->leading = 2;
        m->max_char_width = 9; m->height = 20;
        break;
    case AEECLSID_FONT_BASIC12:
        m->ascent = 12; m->descent = 4; m->leading = 2;
        m->max_char_width = 8; m->height = 18;
        break;
    case AEECLSID_FONT_BASIC12B:
        m->ascent = 12; m->descent = 4; m->leading = 2;
        m->max_char_width = 8; m->height = 18;
        break;
    case AEECLSID_FONT_BASIC15:
        m->ascent = 15; m->descent = 4; m->leading = 2;
        m->max_char_width = 10; m->height = 21;
        break;
    case AEECLSID_FONTSYS_LARGE:
    case AEECLSID_FONTSYS_LARGEITALLIC:
        m->ascent = 21; m->descent = 5; m->leading = 2;
        m->max_char_width = 10; m->height = 28;
        break;
    case AEECLSID_FONT_STANDARD18:
        m->ascent = 17; m->descent = 4; m->leading = 2;
        m->max_char_width = 9; m->height = 23;
        break;
    case AEECLSID_FONT_STANDARD15:
        m->ascent = 15; m->descent = 3; m->leading = 2;
        m->max_char_width = 8; m->height = 20;
        break;
    case AEECLSID_FONT_STANDARD18B:
    case AEECLSID_FONT_STANDARD15B:
        m->ascent = 17; m->descent = 4; m->leading = 2;
        m->max_char_width = 9; m->height = 23;
        break;
    case AEECLSID_FONT_STANDARD23:
        m->ascent = 21; m->descent = 5; m->leading = 2;
        m->max_char_width = 10; m->height = 28;
        break;
    case AEECLSID_FONT_STANDARD23B:
        m->ascent = 21; m->descent = 5; m->leading = 2;
        m->max_char_width = 10; m->height = 28;
        break;
    case AEECLSID_FONT_STANDARD26:
        m->ascent = 23; m->descent = 6; m->leading = 2;
        m->max_char_width = 10; m->height = 31;
        break;
    case AEECLSID_FONT_STANDARD26B:
        m->ascent = 23; m->descent = 6; m->leading = 2;
        m->max_char_width = 10; m->height = 31;
        break;
    case AEECLSID_FONT_STANDARD36:
        m->ascent = 38; m->descent = 10; m->leading = 2;
        m->max_char_width = 12; m->height = 50;
        break;
    default: /* fonte desconhecida: metricas minimas de seguranca */
        m->ascent = 12; m->descent = 4; m->leading = 2;
        m->max_char_width = 8; m->height = 16;
        break;
    }
}

static zfont_state_t *font_state_by_obj(uint32_t obj) {
    int i;
    for (i = 0; i < g_font_count; i++)
        if (g_fonts[i].obj == obj)
            return &g_fonts[i];
    return NULL;
}

static zfont_state_t *font_state_by_clsid(uint32_t clsid) {
    int i;
    for (i = 0; i < g_font_count; i++)
        if (g_fonts[i].clsid == clsid)
            return &g_fonts[i];
    return NULL;
}

uint32_t zbrew_font_create(uint32_t clsid) {
    zfont_state_t *st;
    int i;
    if (!zbrew_is_font_clsid(clsid)) return 0;
    st = font_state_by_clsid(clsid);
    if (st) return st->obj; /* cache por CLSID (APIs do sistema) */

    if (!g_font_vtbl) {
        g_font_vtbl = zheap_alloc((uint16_t)((ZT_FONT_END - ZT_FONT_BASE + 1) * 4));
        if (!g_font_vtbl) return 0;
        for (i = (int)ZT_FONT_BASE; i <= (int)ZT_FONT_END; i++)
            zmem_write32(g_font_vtbl + (uint32_t)(i - (int)ZT_FONT_BASE) * 4,
                         ZTRAP_ADDR((uint32_t)i));
        LOGI("font vtable alocada em 0x%08X", g_font_vtbl);
    }

    if (g_font_count >= ZFONT_MAX) {
        LOGW("zbrew_font_create: tabela full (%d)", g_font_count);
        return 0;
    }
    st = &g_fonts[g_font_count];
    /* objeto: [vtable][clsid][refs] + metricas para debug */
    st->obj = zheap_alloc(ZFONT_OBJ_BYTES);
    if (!st->obj) return 0;
    memset(zmem_host_ptr(st->obj, ZFONT_OBJ_BYTES), 0, ZFONT_OBJ_BYTES);
    {
        uint32_t keep = st->obj; /* font_metrics_for_clsid zera a struct */
        font_metrics_for_clsid(clsid, st);
        st->obj = keep;
    }
    zmem_write32(st->obj, g_font_vtbl);
    zmem_write32(st->obj + 4, clsid);
    zmem_write16(st->obj + 8,  st->ascent);
    zmem_write16(st->obj + 10, st->descent);
    zmem_write16(st->obj + 12, st->leading);
    zmem_write16(st->obj + 14, st->max_char_width);
    zmem_write16(st->obj + 16, st->height);
    zmem_write32(st->obj + 20, 1); /* refs */
    st->refs = 1;
    g_font_count++;
    LOGI("font obj=0x%08X clsid=0x%08X %dx%d+%d asc=%d desc=%d lead=%d",
         st->obj, clsid, st->max_char_width, st->height,
         st->ascent - st->descent, st->ascent, st->descent, st->leading);
    return st->obj;
}

bool zbrew_font_get_metrics(uint32_t obj, uint16_t *ascent, uint16_t *descent,
                            uint16_t *leading, uint16_t *max_char_width) {
    zfont_state_t *st = font_state_by_obj(obj);
    if (!st) return false;
    if (ascent)         *ascent = st->ascent;
    if (descent)        *descent = st->descent;
    if (leading)        *leading = st->leading;
    if (max_char_width) *max_char_width = st->max_char_width;
    return true;
}

static const char *font_name_for_clsid(uint32_t clsid) {
    switch (clsid) {
    case AEECLSID_FONT_FIXED4X6:   return "FIXED4X6";
    case AEECLSID_FONT_BASIC6:      return "BASIC6";
    case AEECLSID_FONT_BASIC9:      return "BASIC9";
    case AEECLSID_FONT_BASIC10:     return "BASIC10";
    case AEECLSID_FONT_BASIC11:     return "BASIC11";
    case AEECLSID_FONT_BASIC11B:    return "BASIC11B";
    case AEECLSID_FONT_BASIC14:     return "BASIC14";
    case AEECLSID_FONT_BASIC12:     return "BASIC12";
    case AEECLSID_FONT_BASIC12B:    return "BASIC12B";
    case AEECLSID_FONT_BASIC15:     return "BASIC15";
    case AEECLSID_BITFONTFOUNDRY:   return "BITFONTFOUNDRY";
    case AEECLSID_FONT:             return "FONT";
    case AEECLSID_FONTSYS_NORMAL:   return "FONTSYSNORMAL";
    case AEECLSID_FONTSYS_LARGE:    return "FONTSYSLARGE";
    case AEECLSID_FONTSYS_BOLD:     return "FONTSYSBOLD";
    case AEECLSID_FONTSYS_ITALIC:   return "FONTSYSITALIC";
    case AEECLSID_FONTSYS_BOLDITALIC: return "FONTSYSBOLDITALIC";
    case AEECLSID_FONTSYS_LARGEITALLIC: return "FONTSYSLARGEITALIC";
    case AEECLSID_FONT_STANDARD11:  return "STANDARD11";
    case AEECLSID_FONT_STANDARD11B: return "STANDARD11B";
    case AEECLSID_FONT_STANDARD15:  return "STANDARD15";
    case AEECLSID_FONT_STANDARD15B: return "STANDARD15B";
    case AEECLSID_FONT_STANDARD18:  return "STANDARD18";
    case AEECLSID_FONT_STANDARD18B: return "STANDARD18B";
    case AEECLSID_FONT_STANDARD23:  return "STANDARD23";
    case AEECLSID_FONT_STANDARD23B: return "STANDARD23B";
    case AEECLSID_FONT_STANDARD26:  return "STANDARD26";
    case AEECLSID_FONT_STANDARD26B: return "STANDARD26B";
    case AEECLSID_FONT_STANDARD36:  return "STANDARD36";
    default:                        return "UNKNOWN";
    }
}

void zbrew_handle_font(uint32_t id) {
    uint32_t slot = id - ZT_FONT_BASE;
    uint32_t obj = g_cpu.r[0];
    zfont_state_t *st = font_state_by_obj(obj);

    switch (slot) {
    case 0: /* AddRef */
        g_cpu.r[0] = 1;
        break;
    case 1: /* Release */
        if (st && st->refs > 0)
            st->refs--;
        g_cpu.r[0] = st ? st->refs : 1;
        break;
    case 2: { /* QueryInterface */
        uint32_t pp = g_cpu.r[2];
        if (pp && pp < 0xFF000000)
            zmem_write32(pp, obj);
        g_cpu.r[0] = 0;
        break;
    }
    case 3: { /* DrawText(po, font, pch, n, ...) */
        /* R1=pch R2=nChars R3=x [SP]=y */
        uint32_t pch = g_cpu.r[1];
        int32_t nChars = (int32_t)g_cpu.r[2];
        uint32_t x = g_cpu.r[3];
        uint32_t y = zbrew_stack_arg(0);
        char text[128];
        int i, n = 0;
        if (!st) { g_cpu.r[0] = 0; break; }
        if (pch && pch < 0xFF000000) {
            int limit = nChars >= 0 ? nChars : 64;
            if (limit > 63) limit = 63;
            for (i = 0; i < limit; i++) {
                uint16_t c = (uint16_t)zmem_read16(pch + (uint32_t)i * 2u);
                if (!c) break;
                text[n++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
            }
        }
        text[n] = '\0';
        LOGI("IFont_DrawText[%s]: '%s' em (%u,%u) width_px=%d",
             font_name_for_clsid(st->clsid), text, x, y,
             n * st->max_char_width);
        g_cpu.r[0] = 0;
        break;
    }
    case 4: /* GetFontMetrics */
        /* R1=AEEFontMetrics* -> nAscent/nDescent/nLeading/nMaxCharWidth */
        {
            uint32_t pmet = g_cpu.r[1];
            if (st && pmet && pmet < 0xFF000000) {
                zmem_write16(pmet + 0, st->ascent);
                zmem_write16(pmet + 2, st->descent);
                zmem_write16(pmet + 4, st->leading);
                zmem_write16(pmet + 6, st->max_char_width);
            }
            g_cpu.r[0] = st ? st->height : 16;
            break;
        }
    case 5: { /* MeasureText -> R0=largura (local + pontos de nMaxWidth? ) */
        /* AEEFont: MeasureText(pch, n, pnFits) -> R0=largura */
        uint32_t pch = g_cpu.r[1];
        int32_t nChars = (int32_t)g_cpu.r[2];
        uint32_t pnFits = g_cpu.r[3];
        int n = 0, limit;
        if (!st) { g_cpu.r[0] = 0; break; }
        limit = nChars >= 0 ? nChars : 1024;
        if (pch && pch < 0xFF000000) {
            for (; n < limit; n++) {
                uint16_t c = (uint16_t)zmem_read16(pch + (uint32_t)n * 2u);
                if (!c) break;
            }
        }
        if (pnFits && pnFits < 0xFF000000)
            zmem_write32(pnFits, (uint32_t)n);
        g_cpu.r[0] = (uint32_t)(n * st->max_char_width);
        break;
    }
    default:
        LOGW("IFont: slot %u nao tratado (obj=0x%08X)", slot, obj);
        g_cpu.r[0] = 1;
        break;
    }
}
