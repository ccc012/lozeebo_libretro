/* iimage.c - IImage: decodificador e renderer de imagens BREW
 *
 * Portado da logica do zeemu BrewImage.cpp (GPLv3).
 * Reimplementado em C99 para o core LOZeebo (GPLv3).
 *
 * Suporta: PNG, JPEG, BMP, GIF, TGA via stb_image (vendado em reference/).
 * ROP: Copy=2, Transparent=7 (magenta 0xFF00FF), Blend=9 (alpha).
 * Cache: fnv1a64 por conteudo para nao redecodificar a cada frame.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "brew.h"
#include "aee_ids.h"
#include "iimage.h"
#ifndef AEECLSID_PNGDECODER_R
#define AEECLSID_PNGDECODER_R  0x01030766u
#define AEECLSID_JPEGDECODER_R 0x0102FD92u
#define AEEIID_IIMAGEDECODER   0x01026E20u
#define AEEIID_IFORCEFEED      0x0101EB0Bu
#endif
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../gpu/framebuffer.h"
#include "../debug/log.h"

/* stb_image - decodificador de imagem (public domain/MIT) */
#define STB_IMAGE_IMPLEMENTATION
#include "../../reference/zeemu-main/zeemu-main/third_party/stb_image.h"

/* =========================================================================
 * Cache de imagens decodificadas (fnv1a64)
 * ========================================================================= */

#define ZIMAGE_CACHE_MAX 256

typedef struct {
    uint64_t key;
    int width;
    int height;
    int channels;
    uint32_t rop;
    uint32_t *pixels;   /* RGBA8888, w*h entries */
    bool used;
} zimage_cache_entry_t;

static zimage_cache_entry_t g_image_cache[ZIMAGE_CACHE_MAX];
static int g_image_cache_count = 0;

static uint64_t zimage_fnv1a64(const uint8_t *data, size_t size) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < size; i++) {
        h ^= data[i];
        h *= 1099511628211ull;
    }
    return h ^ ((uint64_t)size * 0x9e3779b97f4a7c15ull);
}

static zimage_cache_entry_t *zimage_cache_find(uint64_t key) {
    for (int i = 0; i < g_image_cache_count; i++) {
        if (g_image_cache[i].used && g_image_cache[i].key == key)
            return &g_image_cache[i];
    }
    return NULL;
}

static zimage_cache_entry_t *zimage_cache_alloc(uint64_t key) {
    if (g_image_cache_count >= ZIMAGE_CACHE_MAX) {
        LOGW("zimage: cache cheio (%d entries), descartando mais antigo", ZIMAGE_CACHE_MAX);
        /* descarta o primeiro */
        if (g_image_cache[0].pixels) free(g_image_cache[0].pixels);
        memmove(&g_image_cache[0], &g_image_cache[1],
                sizeof(zimage_cache_entry_t) * (ZIMAGE_CACHE_MAX - 1));
        g_image_cache_count--;
    }
    zimage_cache_entry_t *e = &g_image_cache[g_image_cache_count++];
    memset(e, 0, sizeof(*e));
    e->key = key;
    e->used = true;
    return e;
}

/* =========================================================================
 * Objeto IImage (layout de memoria guest)
 * ========================================================================= */

typedef struct {
    uint32_t obj;       /* ponteiro do objeto na memoria guest */
    uint32_t vtbl;      /* ponteiro da vtable na memoria guest */
    uint32_t refs;      /* reference count */

    /* Dados da imagem decodificada */
    int width;
    int height;
    int channels;
    uint32_t rop;       /* ROP atual (default COPY) */
    uint32_t *pixels;   /* RGBA8888 decodificado (host) */
    bool loaded;

    /* Parametros de desenho */
    int draw_w;
    int draw_h;
    int offset_x;
    int offset_y;
    int frame_w;
    int frame_count;

    /* Notify */
    uint32_t notify_pfn;
    uint32_t notify_user;
    bool drawn;
    bool offscreen;

    char label[64];
} zimage_t;

/* Tabela de objetos IImage ativos.
 * Zuma cria dezenas de IImage no boot; 64 estourava e cortava assets. */
#define ZIMAGE_MAX 512
static zimage_t g_images[ZIMAGE_MAX];
static int g_image_count = 0;

static zimage_t *zimage_find_by_obj(uint32_t obj) {
    for (int i = 0; i < g_image_count; i++) {
        if (g_images[i].obj == obj)
            return &g_images[i];
    }
    return NULL;
}

static zimage_t *zimage_alloc_obj(void) {
    if (g_image_count >= ZIMAGE_MAX) {
        LOGE("zimage: limite de objetos atingido (%d)", ZIMAGE_MAX);
        return NULL;
    }
    zimage_t *img = &g_images[g_image_count++];
    memset(img, 0, sizeof(*img));
    img->refs = 1;
    img->rop = IIMAGE_ROP_COPY;
    img->draw_w = 1;
    img->draw_h = 1;
    img->frame_w = 0;
    img->frame_count = 1;
    return img;
}

/* =========================================================================
 * Decodificacao via stb_image
 * ========================================================================= */

static bool zimage_decode_buffer(zimage_t *img, const uint8_t *data, size_t size) {
    if (!data || size == 0)
        return false;

    uint64_t key = zimage_fnv1a64(data, size);
    zimage_cache_entry_t *cached = zimage_cache_find(key);
    if (cached) {
        img->width = cached->width;
        img->height = cached->height;
        img->channels = cached->channels;
        img->rop = cached->rop;
        img->draw_w = cached->width;
        img->draw_h = cached->height;
        img->frame_w = cached->width;
        img->frame_count = 1;
        img->pixels = malloc((size_t)cached->width * cached->height * sizeof(uint32_t));
        if (!img->pixels) return false;
        memcpy(img->pixels, cached->pixels,
               (size_t)cached->width * cached->height * sizeof(uint32_t));
        img->loaded = true;
        LOGI("zimage: cache hit %zu bytes -> %dx%d ch=%d rop=%u",
             size, img->width, img->height, img->channels, img->rop);
        return true;
    }

    int w = 0, h = 0, ch = 0;
    stbi_uc *px = stbi_load_from_memory(data, (int)size, &w, &h, &ch, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        LOGW("zimage: stb_image nao conseguiu decodificar %zu bytes", size);
        return false;
    }

    img->width = w;
    img->height = h;
    img->channels = ch;
    img->draw_w = w;
    img->draw_h = h;
    img->frame_w = w;
    img->frame_count = 1;
    img->pixels = malloc((size_t)w * h * sizeof(uint32_t));
    if (!img->pixels) {
        stbi_image_free(px);
        return false;
    }

    bool has_alpha = false;
    for (int i = 0; i < w * h; i++) {
        has_alpha = has_alpha || px[i * 4 + 3] < 255;
        img->pixels[i] =
            ((uint32_t)px[i * 4 + 0] << 24) |   /* R */
            ((uint32_t)px[i * 4 + 1] << 16) |   /* G */
            ((uint32_t)px[i * 4 + 2] << 8)  |   /* B */
            ((uint32_t)px[i * 4 + 3]);          /* A */
    }
    img->rop = has_alpha ? IIMAGE_ROP_BLEND : IIMAGE_ROP_COPY;
    img->loaded = true;

    /* Guarda no cache */
    zimage_cache_entry_t *e = zimage_cache_alloc(key);
    e->width = w;
    e->height = h;
    e->channels = ch;
    e->rop = img->rop;
    e->pixels = malloc((size_t)w * h * sizeof(uint32_t));
    if (e->pixels) memcpy(e->pixels, img->pixels, (size_t)w * h * sizeof(uint32_t));

    stbi_image_free(px);
    LOGI("zimage: decodificado %zu bytes -> %dx%d ch=%d rop=%u (cache=%d)",
         size, w, h, ch, img->rop, g_image_cache_count);
    return true;
}

/* =========================================================================
 * Vtable / trap handlers
 * ========================================================================= */

/* Escreve o info da imagem em pInfo (guest memory) */
static void zimage_write_info(zimage_t *img, uint32_t pInfo) {
    if (!pInfo || pInfo >= 0xFF000000u)
        return;
    zmem_write16(pInfo + 0, (uint16_t)img->width);      /* width */
    zmem_write16(pInfo + 2, (uint16_t)img->height);     /* height */
    zmem_write16(pInfo + 4, 0);                         /* nColors */
    zmem_write8(pInfo + 6, (uint8_t)(img->frame_count > 1 ? 1 : 0)); /* frames */
    zmem_write16(pInfo + 8, (uint16_t)img->frame_w);    /* frame width */
}

/* PFNNOTIFY(pUser, IImage*, AEEImageInfo*) — fila async (nao reentra). */
static void zimage_fire_notify(zimage_t *img, uint32_t pfn, uint32_t puser) {
    uint32_t info_ptr;
    if (!img || !pfn || pfn >= 0xFF000000u)
        return;
    info_ptr = zheap_alloc(10);
    if (!info_ptr) return;
    zimage_write_info(img, info_ptr);
    LOGI("zimage: notify queue pfn=0x%08X puser=0x%08X obj=0x%08X info=0x%08X",
         pfn, puser, img->obj, info_ptr);
    zbrew_queue_signal_callback(pfn, puser, img->obj, info_ptr);
}

/* Blit da imagem para o framebuffer (device) */
static void zimage_blit_to_device(zimage_t *img, int dst_x, int dst_y, int frame_index) {
    if (!img->loaded || !img->pixels)
        return;

    int src_w = img->width;
    int src_h = img->height;
    int clamped_fc = img->frame_count > 0 ? img->frame_count : 1;
    int eff_fw = img->frame_w > 0 ? img->frame_w : (src_w / clamped_fc);
    if (eff_fw <= 0) eff_fw = src_w;
    int sel_frame = frame_index;
    if (sel_frame < 0) sel_frame = 0;
    if (sel_frame >= clamped_fc) sel_frame = clamped_fc - 1;
    int src_base_x = img->offset_x + sel_frame * eff_fw;
    int src_base_y = img->offset_y;
    if (src_base_x < 0) src_base_x = 0;
    if (src_base_y < 0) src_base_y = 0;
    int max_src_w = src_w - src_base_x;
    int max_src_h = src_h - src_base_y;
    if (max_src_w <= 0 || max_src_h <= 0) return;
    int out_w = img->draw_w > 0 ? img->draw_w : eff_fw;
    int out_h = img->draw_h > 0 ? img->draw_h : src_h;
    if (out_w > max_src_w) out_w = max_src_w;
    if (out_h > max_src_h) out_h = max_src_h;
    if (out_w <= 0 || out_h <= 0) return;

    static int blit_logs = 0;
    if (blit_logs++ < 8) {
        LOGI("zimage: blit '%s' %dx%d@(%d,%d) -> (%d,%d) rop=%u",
             img->label, src_base_x, src_base_y, out_w, out_h,
             dst_x, dst_y, img->rop);
    }

    /* Clip contra o framebuffer */
    int fb_w = ZFB_WIDTH;
    int fb_h = ZFB_HEIGHT;
    int clip_left = 0, clip_top = 0, clip_right = fb_w, clip_bottom = fb_h;

    uint32_t *fb = zfb_pixels();
    if (!fb) return;

    for (int y = 0; y < out_h; y++) {
        int sy = src_base_y + y;
        if (sy >= src_h) sy = src_h - 1;
        int dy = dst_y + y;
        if (dy < clip_top || dy >= clip_bottom) continue;
        for (int x = 0; x < out_w; x++) {
            int sx = src_base_x + x;
            if (sx >= src_w) sx = src_w - 1;
            int dx = dst_x + x;
            if (dx < clip_left || dx >= clip_right) continue;

            uint32_t rgba = img->pixels[(size_t)sy * src_w + sx];
            uint8_t r = (uint8_t)(rgba >> 24);
            uint8_t g = (uint8_t)(rgba >> 16);
            uint8_t b = (uint8_t)(rgba >> 8);
            uint8_t a = (uint8_t)(rgba & 0xFFu);

            if (a == 0) continue;
            if (img->rop == IIMAGE_ROP_TRANSPARENT &&
                r == 255 && g == 0 && b == 255) continue;  /* magenta */

            if (img->rop == IIMAGE_ROP_BLEND && a < 255) {
                uint32_t dst = fb[dy * fb_w + dx];
                uint8_t dr = (uint8_t)(dst >> 24);
                uint8_t dg = (uint8_t)(dst >> 16);
                uint8_t db = (uint8_t)(dst >> 8);
                uint8_t br = (uint8_t)((r * a + dr * (255 - a)) / 255);
                uint8_t bg = (uint8_t)((g * a + dg * (255 - a)) / 255);
                uint8_t bb = (uint8_t)((b * a + db * (255 - a)) / 255);
                fb[dy * fb_w + dx] = (br << 24) | (bg << 16) | (bb << 8) | 0xFFu;
            } else {
                fb[dy * fb_w + dx] = (r << 24) | (g << 16) | (b << 8) | 0xFFu;
            }
        }
    }
    zbrew_mark_frame();
}

/* =========================================================================
 * Trap dispatch
 * ========================================================================= */

void zimage_handle(uint32_t id) {
    uint32_t slot = id - ZT_IMAGE_BASE;
    uint32_t obj = g_cpu.r[0];
    zimage_t *img = zimage_find_by_obj(obj);

    if (!img && slot != 0) {  /* AddRef pode ser chamado em obj nulo? improvável */
        LOGW("zimage: slot=%u obj=0x%08X nao encontrado", slot, obj);
        g_cpu.r[0] = 1;
        return;
    }

    uint32_t r0 = g_cpu.r[0], r1 = g_cpu.r[1], r2 = g_cpu.r[2], r3 = g_cpu.r[3];

    switch (slot) {
    case 0: /* AddRef */
        if (img) img->refs++;
        g_cpu.r[0] = img ? img->refs : 1;
        break;

    case 1: /* Release */
        if (img && img->refs > 0) img->refs--;
        g_cpu.r[0] = img ? img->refs : 0;
        break;

    case 2: /* Draw(x=R1, y=R2) */
        if (img) {
            zimage_blit_to_device(img, (int)r1, (int)r2, 0);
            img->drawn = true;
        }
        g_cpu.r[0] = 0;
        break;

    case 3: /* DrawFrame(frame=R1, x=R2, y=R3) */
        if (img) {
            zimage_blit_to_device(img, (int)r2, (int)r3, (int)r1);
            img->drawn = true;
        }
        g_cpu.r[0] = 0;
        break;

    case 4: /* GetInfo(pInfo=R1) */
        if (img) zimage_write_info(img, r1);
        g_cpu.r[0] = 0;
        break;

    case 5: /* SetParm(parm=R1, v1=R2, v2=R3) */
        if (img) {
            uint32_t parm = r1;
            int32_t v1 = (int32_t)r2;
            int32_t v2 = (int32_t)r3;
            if (parm == IIMAGE_PARM_SIZE) {
                img->draw_w = v1 > 0 ? v1 : 1;
                img->draw_h = v2 > 0 ? v2 : 1;
            } else if (parm == IIMAGE_PARM_OFFSET) {
                img->offset_x = v1;
                img->offset_y = v2;
            } else if (parm == IIMAGE_PARM_CXFRAME) {
                img->frame_w = v1 > 0 ? v1 : img->height;
                img->frame_count = img->frame_w > 0 ? (img->width / img->frame_w) : 1;
                if (img->frame_count < 1) img->frame_count = 1;
                img->draw_w = img->frame_w > 0 ? img->frame_w : img->draw_w;
            } else if (parm == IIMAGE_PARM_ROP) {
                img->rop = (uint32_t)v1;
            } else if (parm == IIMAGE_PARM_NFRAMES) {
                img->frame_count = v1 > 0 ? v1 : 1;
                img->frame_w = img->frame_count > 0 ? (img->width / img->frame_count) : img->width;
                if (img->frame_w < 1) img->frame_w = 1;
                img->draw_w = img->frame_w > 0 ? img->frame_w : img->draw_w;
            } else if (parm == IIMAGE_PARM_OFFSCREEN) {
                img->offscreen = (v1 != 0);
            } else if (parm == IIMAGE_PARM_SCALE) {
                img->draw_w = v1 > 0 ? v1 : 1;
                img->draw_h = v2 > 0 ? v2 : 1;
            }
        }
        g_cpu.r[0] = 0;
        break;

    case 6: /* Start */
        if (img && !img->drawn && !img->offscreen) {
            zimage_blit_to_device(img, img->offset_x, img->offset_y, 0);
            img->drawn = true;
        }
        g_cpu.r[0] = 0;
        break;

    case 7: /* Stop */
        g_cpu.r[0] = 0;
        break;

    case 8: { /* SetStream(stream_obj=R1) — zeemu: le IMemAStream restante */
        size_t n = 0;
        uint8_t *buf = zmemastream_read_remaining(r1, &n);
        if (img && buf && n > 0 && zimage_decode_buffer(img, buf, n)) {
            LOGI("zimage: SetStream obj=0x%08X stream=0x%08X bytes=%zu -> %dx%d",
                 obj, r1, n, img->width, img->height);
            if (img->notify_pfn)
                zimage_fire_notify(img, img->notify_pfn, img->notify_user);
        } else {
            LOGW("zimage: SetStream falhou obj=0x%08X stream=0x%08X bytes=%zu",
                 obj, r1, n);
        }
        free(buf);
        g_cpu.r[0] = 0;
        break;
    }

    case 9: /* HandleEvent */
        /* Mesmo que Start: desenha se ainda nao desenhou */
        if (img && !img->drawn && !img->offscreen) {
            zimage_blit_to_device(img, img->offset_x, img->offset_y, 0);
            img->drawn = true;
        }
        g_cpu.r[0] = 0;
        break;

    case 10: /* Notify(pfn=R1, puser=R2) */
        if (img) {
            img->notify_pfn = r1;
            img->notify_user = r2;
            LOGI("zimage: Notify pfn=0x%08X puser=0x%08X loaded=%d",
                 r1, r2, img->loaded ? 1 : 0);
            if (img->loaded)
                zimage_fire_notify(img, r1, r2);
        }
        g_cpu.r[0] = 0;
        break;

    default:
        LOGW("zimage: slot %u nao implementado", slot);
        g_cpu.r[0] = 1;
        break;
    }
}

/* =========================================================================
 * Factory
 * ========================================================================= */

static bool zimage_vtbl_installed = false;
static uint32_t g_image_vtbl = 0;

static void zimage_install_vtbl(void) {
    if (zimage_vtbl_installed) return;
    g_image_vtbl = zheap_alloc(11 * 4);
    if (!g_image_vtbl) return;
    for (int i = 0; i < 11; i++)
        zmem_write32(g_image_vtbl + (uint32_t)i * 4, ZTRAP_ADDR(ZT_IMAGE_BASE + i));
    zimage_vtbl_installed = true;
    LOGI("zimage: vtable instalada em 0x%08X", g_image_vtbl);
}

uint32_t zimage_create_from_buffer(uint32_t clsid, const uint8_t *data,
                                   size_t size, const char *label) {
    zimage_install_vtbl();
    zimage_t *img = zimage_alloc_obj();
    if (!img) return 0;
    uint32_t obj = zheap_alloc(4);
    if (!obj) return 0;
    zmem_write32(obj, g_image_vtbl);
    zmem_write32(obj + 4, clsid);  /* CLSID no offset 4 */
    img->obj = obj;
    img->vtbl = g_image_vtbl;
    strncpy(img->label, label ? label : "image", sizeof(img->label) - 1);
    img->label[sizeof(img->label) - 1] = 0;
    if (data && size > 0)
        zimage_decode_buffer(img, data, size);
    LOGI("zimage: criado obj=0x%08X clsid=0x%08X label='%s'", obj, clsid, img->label);
    return obj;
}

uint32_t zimage_create_from_file(uint32_t clsid, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOGW("zimage: nao abriu arquivo '%s'", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return 0; }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return 0; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    uint32_t obj = zimage_create_from_buffer(clsid, buf, rd, path);
    free(buf);
    return obj;
}

uint32_t zimage_create(uint32_t clsid) {
    /* Criacao sem dados: objeto vazio, preenchido depois via SetStream */
    zimage_install_vtbl();
    zimage_t *img = zimage_alloc_obj();
    if (!img) return 0;
    uint32_t obj = zheap_alloc(4);
    if (!obj) return 0;
    zmem_write32(obj, g_image_vtbl);
    zmem_write32(obj + 4, clsid);
    img->obj = obj;
    img->vtbl = g_image_vtbl;
    strncpy(img->label, "image(empty)", sizeof(img->label) - 1);
    img->label[sizeof(img->label) - 1] = 0;
    LOGI("zimage: criado vazio obj=0x%08X clsid=0x%08X", obj, clsid);
    return obj;
}

/* Limpeza (chamado no shutdown do boot) */
void zimage_shutdown(void) {
    for (int i = 0; i < g_image_count; i++) {
        if (g_images[i].pixels) free(g_images[i].pixels);
        g_images[i].pixels = NULL;
    }
    g_image_count = 0;
    for (int i = 0; i < g_image_cache_count; i++) {
        if (g_image_cache[i].pixels) free(g_image_cache[i].pixels);
    }
    g_image_cache_count = 0;
    memset(g_image_cache, 0, sizeof(g_image_cache));
    if (g_image_vtbl) {
        /* nao liberamos a vtable (esta na heap guest) */
    }
    zimage_vtbl_installed = false;
    g_image_vtbl = 0;
}

/* =========================================================================
 * IImageDecoder + IForceFeed (PNGDecoderBREW / JPEGDecoder)
 * Semantica: zeemu BrewImageDecoder.cpp (GPLv3).
 * ========================================================================= */

#define ZIMGDEC_MAX 8
#define ZIMGDEC_ADDR_MAX 0xFF000000u
#define AEE_RO_COPY 0x00CC0020u

typedef struct {
    bool active;
    uint32_t clsid;
    uint32_t refs;
    uint32_t dec_obj;
    uint32_t dec_vtbl;
    uint32_t ff_obj;
    uint32_t ff_vtbl;
    uint32_t bitmap;       /* IDIB guest (ZCLSID_DEVICE_BITMAP layout) */
    uint8_t *encoded;
    size_t encoded_len;
    size_t encoded_cap;
    bool dirty;
} zimgdec_t;

static zimgdec_t g_decoders[ZIMGDEC_MAX];
static uint32_t g_dec_vtbl_shared = 0;
static uint32_t g_ff_vtbl_shared = 0;

static zimgdec_t *imgdec_by_obj(uint32_t obj)
{
    int i;
    for (i = 0; i < ZIMGDEC_MAX; i++) {
        if (g_decoders[i].active &&
            (g_decoders[i].dec_obj == obj || g_decoders[i].ff_obj == obj))
            return &g_decoders[i];
    }
    return NULL;
}

static zimgdec_t *imgdec_alloc(void)
{
    int i;
    for (i = 0; i < ZIMGDEC_MAX; i++) {
        if (!g_decoders[i].active) {
            memset(&g_decoders[i], 0, sizeof(g_decoders[i]));
            g_decoders[i].active = true;
            g_decoders[i].refs = 1;
            return &g_decoders[i];
        }
    }
    return NULL;
}

static void imgdec_install_vtbls(void)
{
    int i;
    if (!g_dec_vtbl_shared) {
        g_dec_vtbl_shared = zheap_alloc(32 * 4);
        if (g_dec_vtbl_shared) {
            for (i = 0; i < 32; i++)
                zmem_write32(g_dec_vtbl_shared + (uint32_t)i * 4,
                             ZTRAP_ADDR(ZT_IMGDEC_BASE + (i < 5 ? i : 5)));
        }
    }
    if (!g_ff_vtbl_shared) {
        g_ff_vtbl_shared = zheap_alloc(5 * 4);
        if (g_ff_vtbl_shared) {
            for (i = 0; i < 5; i++)
                zmem_write32(g_ff_vtbl_shared + (uint32_t)i * 4,
                             ZTRAP_ADDR(ZT_FORCEFEED_BASE + i));
        }
    }
}

/* IDIB RGB565 via zbrew_create_device_bitmap (vtbl stub DIBM real). */
static uint32_t imgdec_make_bitmap(int width, int height, const uint8_t *rgba)
{
    uint32_t obj, bits, pitch;
    int x, y;
    if (width <= 0 || height <= 0 || width > 2048 || height > 2048 || !rgba)
        return 0;
    obj = zbrew_create_device_bitmap(width, height, 16);
    if (!obj) return 0;
    bits = zmem_read32(obj + 8);
    pitch = zmem_read16(obj + 24);
    if (!bits || !pitch) return 0;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            const uint8_t *p = rgba + ((size_t)y * width + x) * 4;
            uint16_t rgb565 = (p[3] == 0)
                ? 0
                : (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
            zmem_write16(bits + ((uint32_t)y * pitch + (uint32_t)x * 2u), rgb565);
        }
    }
    return obj;
}

static bool imgdec_decode(zimgdec_t *d)
{
    int w = 0, h = 0, ch = 0;
    stbi_uc *px;
    if (!d) return false;
    if (!d->dirty && d->bitmap)
        return true;
    if (!d->encoded || d->encoded_len == 0)
        return false;
    px = stbi_load_from_memory(d->encoded, (int)d->encoded_len, &w, &h, &ch, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        LOGW("imgdec: decode falhou clsid=0x%08X bytes=%zu", d->clsid, d->encoded_len);
        return false;
    }
    d->bitmap = imgdec_make_bitmap(w, h, px);
    stbi_image_free(px);
    d->dirty = false;
    LOGI("imgdec: decoded clsid=0x%08X %dx%d -> bmp=0x%08X",
         d->clsid, w, h, d->bitmap);
    return d->bitmap != 0;
}

static void imgdec_qi(zimgdec_t *d, uint32_t iid, uint32_t pp)
{
    uint32_t out = 0;
    if (!d) {
        if (pp && pp < ZIMGDEC_ADDR_MAX) zmem_write32(pp, 0);
        g_cpu.r[0] = 4; /* ECLASSNOTSUPPORT */
        return;
    }
    if (iid == AEEIID_IIMAGEDECODER || iid == d->clsid ||
        iid == AEECLSID_PNGDECODER_R || iid == AEECLSID_JPEGDECODER_R)
        out = d->dec_obj;
    else if (iid == AEEIID_IFORCEFEED)
        out = d->ff_obj;
    if (pp && pp < ZIMGDEC_ADDR_MAX)
        zmem_write32(pp, out);
    LOGI("imgdec: QI iid=0x%08X -> 0x%08X", iid, out);
    g_cpu.r[0] = out ? 0 : 4;
}

uint32_t zimage_decoder_create(uint32_t clsid)
{
    zimgdec_t *d = imgdec_alloc();
    if (!d) return 0;
    imgdec_install_vtbls();
    if (!g_dec_vtbl_shared || !g_ff_vtbl_shared) {
        d->active = false;
        return 0;
    }
    d->clsid = clsid;
    d->dec_vtbl = g_dec_vtbl_shared;
    d->ff_vtbl = g_ff_vtbl_shared;
    d->dec_obj = zheap_alloc(8);
    d->ff_obj = zheap_alloc(8);
    if (!d->dec_obj || !d->ff_obj) {
        d->active = false;
        return 0;
    }
    zmem_write32(d->dec_obj, d->dec_vtbl);
    zmem_write32(d->dec_obj + 4, clsid);
    zmem_write32(d->ff_obj, d->ff_vtbl);
    zmem_write32(d->ff_obj + 4, clsid);
    LOGI("imgdec: create clsid=0x%08X dec=0x%08X ff=0x%08X",
         clsid, d->dec_obj, d->ff_obj);
    return d->dec_obj;
}

void zimage_decoder_handle(uint32_t id)
{
    uint32_t slot = id - ZT_IMGDEC_BASE;
    zimgdec_t *d = imgdec_by_obj(g_cpu.r[0]);
    uint32_t r1 = g_cpu.r[1], r2 = g_cpu.r[2];

    switch (slot) {
    case 0: /* AddRef */
        if (d) d->refs++;
        g_cpu.r[0] = d ? d->refs : 1;
        return;
    case 1: /* Release */
        if (d && d->refs > 0) d->refs--;
        g_cpu.r[0] = d ? d->refs : 0;
        return;
    case 2: /* QueryInterface */
        imgdec_qi(d, r1, r2);
        return;
    case 3: { /* GetBitmap(ppBitmap) */
        uint32_t bmp = 0;
        if (d && imgdec_decode(d))
            bmp = d->bitmap;
        if (r1 && r1 < ZIMGDEC_ADDR_MAX)
            zmem_write32(r1, bmp);
        LOGI("imgdec: GetBitmap -> 0x%08X", bmp);
        g_cpu.r[0] = bmp ? 0 : 1;
        return;
    }
    case 4: /* GetRop */
        g_cpu.r[0] = AEE_RO_COPY;
        return;
    default:
        g_cpu.r[0] = 0;
        return;
    }
}

void zimage_forcefeed_handle(uint32_t id)
{
    uint32_t slot = id - ZT_FORCEFEED_BASE;
    zimgdec_t *d = imgdec_by_obj(g_cpu.r[0]);
    uint32_t r1 = g_cpu.r[1], r2 = g_cpu.r[2];

    switch (slot) {
    case 0:
        if (d) d->refs++;
        g_cpu.r[0] = d ? d->refs : 1;
        return;
    case 1:
        if (d && d->refs > 0) d->refs--;
        g_cpu.r[0] = d ? d->refs : 0;
        return;
    case 2:
        imgdec_qi(d, r1, r2);
        return;
    case 3: { /* Write(pBuf, cb) — pBuf==0 = fim do stream */
        int32_t cb = (int32_t)r2;
        if (!d) { g_cpu.r[0] = 1; return; }
        if (r1 && r1 < ZIMGDEC_ADDR_MAX && cb > 0) {
            size_t need = d->encoded_len + (size_t)cb;
            if (need > d->encoded_cap) {
                size_t ncap = d->encoded_cap ? d->encoded_cap * 2 : 4096;
                uint8_t *nbuf;
                while (ncap < need) ncap *= 2;
                nbuf = (uint8_t *)realloc(d->encoded, ncap);
                if (!nbuf) { g_cpu.r[0] = 1; return; }
                d->encoded = nbuf;
                d->encoded_cap = ncap;
            }
            if (!zmem_read_block(r1, d->encoded + d->encoded_len, (uint32_t)cb)) {
                g_cpu.r[0] = 1;
                return;
            }
            d->encoded_len += (size_t)cb;
            d->dirty = true;
            LOGI("imgdec: Write chunk=%d total=%zu", cb, d->encoded_len);
        } else if (r1 == 0) {
            LOGI("imgdec: Write end total=%zu", d->encoded_len);
            imgdec_decode(d);
        }
        g_cpu.r[0] = 0;
        return;
    }
    case 4: /* Reset */
        if (d) {
            d->encoded_len = 0;
            d->dirty = false;
            d->bitmap = 0;
            LOGI("imgdec: Reset");
        }
        g_cpu.r[0] = 0;
        return;
    default:
        g_cpu.r[0] = 0;
        return;
    }
}

/* =========================================================================
 * IMemAStream (AEECLSID_MEMASTREAM = 0x0100100C) — zeemu BrewMemAStream
 * ========================================================================= */

#define ZMEMASTREAM_MAX 256

typedef struct {
    bool active;
    uint32_t obj;
    uint32_t refs;
    uint8_t *data;
    size_t len;
    size_t pos;
} zmemastream_t;

static zmemastream_t g_memastreams[ZMEMASTREAM_MAX];
static uint32_t g_memastream_vtbl = 0;

static zmemastream_t *memastream_by_obj(uint32_t obj)
{
    int i;
    if (!obj) return NULL;
    for (i = 0; i < ZMEMASTREAM_MAX; i++) {
        if (g_memastreams[i].active && g_memastreams[i].obj == obj)
            return &g_memastreams[i];
    }
    return NULL;
}

static zmemastream_t *memastream_alloc(void)
{
    int i;
    for (i = 0; i < ZMEMASTREAM_MAX; i++) {
        if (!g_memastreams[i].active) {
            memset(&g_memastreams[i], 0, sizeof(g_memastreams[i]));
            g_memastreams[i].active = true;
            g_memastreams[i].refs = 1;
            return &g_memastreams[i];
        }
    }
    LOGE("memastream: limite (%d)", ZMEMASTREAM_MAX);
    return NULL;
}

uint32_t zmemastream_create(void)
{
    zmemastream_t *s = memastream_alloc();
    uint32_t obj;
    int i;
    if (!s) return 0;
    if (!g_memastream_vtbl) {
        g_memastream_vtbl = zheap_alloc(7 * 4);
        if (!g_memastream_vtbl) {
            s->active = false;
            return 0;
        }
        for (i = 0; i < 7; i++)
            zmem_write32(g_memastream_vtbl + (uint32_t)i * 4,
                         ZTRAP_ADDR(ZT_MEMASTREAM_BASE + i));
    }
    obj = zheap_alloc(8);
    if (!obj) {
        s->active = false;
        return 0;
    }
    zmem_write32(obj, g_memastream_vtbl);
    zmem_write32(obj + 4, AEECLSID_MEMASTREAM);
    s->obj = obj;
    LOGI("memastream: create obj=0x%08X", obj);
    return obj;
}

uint8_t *zmemastream_read_remaining(uint32_t stream_obj, size_t *out_len)
{
    zmemastream_t *s = memastream_by_obj(stream_obj);
    size_t rem;
    uint8_t *out;
    if (out_len) *out_len = 0;
    if (!s || !s->data || s->pos > s->len) return NULL;
    rem = s->len - s->pos;
    out = (uint8_t *)malloc(rem ? rem : 1);
    if (!out) return NULL;
    if (rem) memcpy(out, s->data + s->pos, rem);
    s->pos = s->len;
    if (out_len) *out_len = rem;
    return out;
}

void zmemastream_handle(uint32_t id)
{
    uint32_t slot = id - ZT_MEMASTREAM_BASE;
    zmemastream_t *s = memastream_by_obj(g_cpu.r[0]);
    uint32_t r1 = g_cpu.r[1], r2 = g_cpu.r[2], r3 = g_cpu.r[3];

    switch (slot) {
    case 0: /* AddRef */
        if (s) s->refs++;
        g_cpu.r[0] = s ? s->refs : 1;
        return;
    case 1: /* Release */
        if (s && s->refs > 0) s->refs--;
        if (s && s->refs == 0) {
            free(s->data);
            s->data = NULL;
            s->len = s->pos = 0;
            s->active = false;
        }
        g_cpu.r[0] = s ? s->refs : 0;
        return;
    case 2: /* Readable — sync; nothing to arm */
        g_cpu.r[0] = 0;
        return;
    case 3: { /* Read(buf, len) */
        uint32_t want, n;
        if (!s || !s->data || s->pos >= s->len || !r1 || r1 >= 0xFF000000u) {
            g_cpu.r[0] = 0;
            return;
        }
        want = r2;
        n = (uint32_t)(s->len - s->pos);
        if (n > want) n = want;
        if (n && !zmem_write_block(r1, s->data + s->pos, n)) {
            g_cpu.r[0] = 0;
            return;
        }
        s->pos += n;
        g_cpu.r[0] = n;
        return;
    }
    case 4: /* Cancel */
        g_cpu.r[0] = 0;
        return;
    case 5: /* Set(buf, size, offset) */
    case 6: { /* SetEx — same args */
        uint32_t buf = r1, size = r2, offset = r3;
        if (!s) { g_cpu.r[0] = 1; return; }
        free(s->data);
        s->data = NULL;
        s->len = s->pos = 0;
        if (buf && buf < 0xFF000000u && size > offset) {
            size_t avail = (size_t)(size - offset);
            s->data = (uint8_t *)malloc(avail);
            if (!s->data) { g_cpu.r[0] = 1; return; }
            if (!zmem_read_block(buf + offset, s->data, (uint32_t)avail)) {
                free(s->data);
                s->data = NULL;
                g_cpu.r[0] = 1;
                return;
            }
            s->len = avail;
            LOGI("memastream: Set obj=0x%08X buf=0x%08X size=%u off=%u stored=%zu",
                 s->obj, buf, size, offset, avail);
        } else {
            LOGW("memastream: Set invalid buf=0x%08X size=%u off=%u", buf, size, offset);
        }
        g_cpu.r[0] = 0;
        return;
    }
    default:
        g_cpu.r[0] = 1;
        return;
    }
}
