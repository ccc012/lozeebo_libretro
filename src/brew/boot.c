/* boot.c - Ciclo de vida de applet BREW real
 *
 * Sequencia (interoperabilidade confirmada via zeemu/BREW SDK):
 *   1. AEEMod_Load(piShell, ph, ppMod)     <- entry do modulo
 *   2. mod = *ppMod; IModule vtable+8 = CreateInstance
 *   3. IMODULE_CreateInstance(mod, shell, clsid, &applet)
 *   4. applet vtable+8 = HandleEvent; enviar EVT_APP_START (=0 no Zeebo)
 *   5. Loop: timers do IShell_SetTimer + eventos de tecla
 *
 * "Guest call": seta R0-R3, LR = trap ZT_GUEST_RETURN, PC = funcao;
 * quando o trap dispara, a maquina de estados avanca.
 */
#include <string.h>
#include <stdlib.h>
#include "brew.h"
#include "aee_ids.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../input/input.h"
#include "../gpu/egl_gl.h"
#include "../gpu/framebuffer.h"
#include "../debug/log.h"

uint32_t zbrew_uptime_ms(void);
void zbrew_mark_frame(void);

/* ---- estados ---- */
enum zboot_state {
    BOOT_IDLE = 0,
    BOOT_MOD_LOAD,        /* executando AEEMod_Load            */
    BOOT_CREATE_APPLET,   /* executando IModule_CreateInstance */
    BOOT_APP_START,       /* executando HandleEvent(EVT_APP_START) */
    BOOT_RUNNING,         /* jogo rodando (timers/eventos)     */
    BOOT_TIMER_CALL,      /* executando callback de timer      */
    BOOT_SIGNAL_CALL,     /* executando callback de sinal HID  */
    BOOT_FAILED
};

static enum zboot_state g_state = BOOT_IDLE;
static uint32_t g_ppmod = 0;         /* IModule** passado ao AEEMod_Load */
static uint32_t g_module_obj = 0;
static uint32_t g_applet_out = 0;    /* void** para o applet */
static uint32_t g_applet_obj = 0;
static uint32_t g_applet_clsid = 0;
static uint32_t g_hid_button_signal = 0;
static uint32_t g_hid_position_signal = 0;
static uint32_t g_hid_event_id = 0;
static uint32_t g_hid_event_uid = 0;
static bool g_hid_event_down = false;
static bool g_hid_event_pending = false;
static uint32_t g_shell_prefs[8];
static uint32_t g_shell_events[8];

/* IHID_RegisterForConnectEvents (slot 6 do IHID real, ver BrewHID.cpp do
 * zeemu): o jogo registra um sinal e espera ser avisado quando um device
 * HID conecta. Nosso stub ja reporta o joystick como conectado desde o
 * IHID_GetConnectedDevices inicial, entao disparamos esse sinal uma unica
 * vez, no primeiro tick apos o registro (ver zboot_process_timers). Sem
 * isso o Family Pack fica parado para sempre esperando esse callback. */
static uint32_t g_hid_connect_signal = 0;
static bool g_hid_connect_pending = false;
/* Watchdog: alguns jogos (Double Dragon confirmado por smoke test) entram
 * num loop degenerado dentro do callback de conexao HID e nunca retornam
 * (nunca disparam ZT_GUEST_RETURN) - sem isso a CPU queima o orcamento de
 * 1M instrucoes/frame para sempre, travando o framebuffer que ja avancava
 * bem so com timers. Se o callback nao voltar depois de um numero razoavel
 * de instrucoes, forca o estado de volta para RUNNING/halted. */
#define ZSIGNAL_CALL_INSTR_BUDGET (2u * 1000u * 1000u)
static uint64_t g_signal_call_started_at = 0;

uint32_t zboot_get_applet_object(void) {
    if (g_applet_obj)
        return g_applet_obj;
    if (g_applet_out)
        return zmem_read32(g_applet_out);
    return 0;
}
static uint32_t g_shell_obj = 0;     /* objeto IShell real */

/* ---- timers (IShell_SetTimer) ---- */
#define ZTIMER_MAX 16
typedef struct {
    bool     active;
    uint32_t expires_ms;
    uint32_t pfn;
    uint32_t puser;
} ztimer_t;
static ztimer_t g_timers[ZTIMER_MAX];

bool zboot_timer_active(int i) {
    return i >= 0 && i < ZTIMER_MAX && g_timers[i].active;
}

void zboot_timer_set(int i, uint32_t ms, uint32_t pfn, uint32_t puser) {
    if (i >= 0 && i < ZTIMER_MAX) {
        g_timers[i].active = true;
        g_timers[i].expires_ms = zbrew_uptime_ms() + ms;
        g_timers[i].pfn = pfn;
        g_timers[i].puser = puser;
    }
}

const char *zboot_state_name(void) {
    switch (g_state) {
        case BOOT_IDLE: return "idle";
        case BOOT_MOD_LOAD: return "AEEMod_Load";
        case BOOT_CREATE_APPLET: return "CreateInstance";
        case BOOT_APP_START: return "EVT_APP_START";
        case BOOT_RUNNING: return "rodando";
        case BOOT_TIMER_CALL: return "timer";
        case BOOT_SIGNAL_CALL: return "sinal HID";
        case BOOT_FAILED: return "falhou";
    }
    return "?";
}

uint32_t zboot_shell_obj(void) { return g_shell_obj; }

typedef struct {
    uint32_t magic;
    uint32_t state;
    uint32_t ppmod;
    uint32_t module_obj;
    uint32_t applet_out;
    uint32_t applet_obj;
    uint32_t applet_clsid;
    uint32_t hid_button_signal;
    uint32_t hid_position_signal;
    uint32_t hid_event_id;
    uint32_t hid_event_uid;
    uint32_t hid_event_down;
    uint32_t hid_event_pending;
    uint32_t shell_obj;
    uint32_t shell_prefs[8];
    uint32_t shell_events[8];
    uint32_t timers[ZTIMER_MAX * 4];
    uint32_t hid_connect_signal;
    uint32_t hid_connect_pending;
} zboot_state_blob_t;

size_t zboot_serialize(void *dst, size_t max) {
    zboot_state_blob_t blob;
    size_t need = sizeof(blob);
    size_t i;
    if (!dst || max < need)
        return need;
    memset(&blob, 0, sizeof(blob));
    blob.magic = 0x5A424F54u; /* 'ZBOT' */
    blob.state = (uint32_t)g_state;
    blob.ppmod = g_ppmod;
    blob.module_obj = g_module_obj;
    blob.applet_out = g_applet_out;
    blob.applet_obj = g_applet_obj;
    blob.applet_clsid = g_applet_clsid;
    blob.hid_button_signal = g_hid_button_signal;
    blob.hid_position_signal = g_hid_position_signal;
    blob.hid_event_id = g_hid_event_id;
    blob.hid_event_uid = g_hid_event_uid;
    blob.hid_event_down = g_hid_event_down ? 1u : 0u;
    blob.hid_event_pending = g_hid_event_pending ? 1u : 0u;
    blob.shell_obj = g_shell_obj;
    memcpy(blob.shell_prefs, g_shell_prefs, sizeof(g_shell_prefs));
    memcpy(blob.shell_events, g_shell_events, sizeof(g_shell_events));
    for (i = 0; i < ZTIMER_MAX; i++) {
        blob.timers[i * 4 + 0] = g_timers[i].active ? 1u : 0u;
        blob.timers[i * 4 + 1] = g_timers[i].expires_ms;
        blob.timers[i * 4 + 2] = g_timers[i].pfn;
        blob.timers[i * 4 + 3] = g_timers[i].puser;
    }
    blob.hid_connect_signal = g_hid_connect_signal;
    blob.hid_connect_pending = g_hid_connect_pending ? 1u : 0u;
    memcpy(dst, &blob, sizeof(blob));
    return need;
}

bool zboot_unserialize(const void *src, size_t len) {
    const zboot_state_blob_t *blob = (const zboot_state_blob_t *)src;
    size_t i;
    if (!src || len < sizeof(*blob) || blob->magic != 0x5A424F54u)
        return false;
    g_state = (enum zboot_state)blob->state;
    g_ppmod = blob->ppmod;
    g_module_obj = blob->module_obj;
    g_applet_out = blob->applet_out;
    g_applet_obj = blob->applet_obj;
    g_applet_clsid = blob->applet_clsid;
    g_hid_button_signal = blob->hid_button_signal;
    g_hid_position_signal = blob->hid_position_signal;
    g_hid_event_id = blob->hid_event_id;
    g_hid_event_uid = blob->hid_event_uid;
    g_hid_event_down = blob->hid_event_down ? true : false;
    g_hid_event_pending = blob->hid_event_pending ? true : false;
    g_shell_obj = blob->shell_obj;
    memcpy(g_shell_prefs, blob->shell_prefs, sizeof(g_shell_prefs));
    memcpy(g_shell_events, blob->shell_events, sizeof(g_shell_events));
    for (i = 0; i < ZTIMER_MAX; i++) {
        g_timers[i].active = blob->timers[i * 4 + 0] ? true : false;
        g_timers[i].expires_ms = blob->timers[i * 4 + 1];
        g_timers[i].pfn = blob->timers[i * 4 + 2];
        g_timers[i].puser = blob->timers[i * 4 + 3];
    }
    g_hid_connect_signal = blob->hid_connect_signal;
    g_hid_connect_pending = blob->hid_connect_pending ? true : false;
    return true;
}

/* ---- IShell real: objeto + vtable na ordem do AEEShell.h ---- */
static uint32_t setup_real_shell(void) {
    uint32_t vtbl = zheap_alloc(128 * 4);
    uint32_t obj  = zheap_alloc(128 * 4);
    int i;
    if (!vtbl || !obj) return 0;
    for (i = 0; i < 128; i++) {
        uint32_t trap = ZTRAP_ADDR(ZT_ISHELL_BASE + i);
        zmem_write32(vtbl + (uint32_t)i * 4, trap);
        /* alguns binarios Zeebo acessam o objeto como vtable */
        if (i > 0) zmem_write32(obj + (uint32_t)i * 4, trap);
    }
    zmem_write32(obj, vtbl);
    return obj;
}

/* Cria interface stub logada: 64 slots -> traps ZT_STUB_BASE+ */
static uint32_t g_stub_vtbl = 0;
#define ZCLSID_HID_DEVICE 0x48494444u /* 'HIDD', marcador HLE interno */
#define ZCLSID_DEVICE_BITMAP 0x4449424Du /* 'DIBM', marcador HLE interno */
#define ZHID_HANDLE       0x00001234u
#define ZHID_JOYSTICK_UID 0x0106C3FDu
/* Limite superior de qualquer endereco emulado "real" (RAM/heap/stack/VRAM);
 * acima disso so existem traps HLE (0xF0000000+) ou lixo/ponteiro invalido.
 * NAO usar 0x04000000 aqui: heap comeca em 0x10000000 e stack em 0x2FC00000,
 * ambos endereços legitimos e muito usados. */
#define ZDISPLAY_ADDR_MAX (ZMEM_VRAM_BASE + ZMEM_VRAM_SIZE)
static uint32_t g_device_bitmap = 0;

/* Estado mínimo do IDisplay real (layout de 48 slots do SDK Zeebo). */
#define ZDISPLAY_RGB_NONE       0xFFFFFFFFu
#define ZDISPLAY_RECT_FRAME     0x00000001u
#define ZDISPLAY_RECT_FILL      0x00000002u
static uint32_t g_display_text_color = 0xFFFFFF00u;
static uint32_t g_display_background_color = 0x00000000u;
static uint32_t g_display_line_color = 0xFFFFFF00u;
static int g_display_clip_x = 0;
static int g_display_clip_y = 0;
static int g_display_clip_w = 640;
static int g_display_clip_h = 480;

/* RGBVAL do BREW usa r<<8 | g<<16 | b<<24; o framebuffer é XRGB8888. */
static uint32_t display_rgbval_to_xrgb(uint32_t rgb) {
    uint32_t r = (rgb >> 8) & 0xFFu;
    uint32_t g = (rgb >> 16) & 0xFFu;
    uint32_t b = (rgb >> 24) & 0xFFu;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static void display_read_rect(uint32_t p_rect, int *x, int *y, int *w, int *h) {
    *x = 0;
    *y = 0;
    *w = 640;
    *h = 480;
    if (p_rect > 1 && zmem_host_ptr(p_rect, 8)) {
        *x = (int16_t)zmem_read16(p_rect + 0);
        *y = (int16_t)zmem_read16(p_rect + 2);
        *w = (int16_t)zmem_read16(p_rect + 4);
        *h = (int16_t)zmem_read16(p_rect + 6);
    }
}

static bool display_clip_rect(int *x, int *y, int *w, int *h) {
    int x0 = *x > g_display_clip_x ? *x : g_display_clip_x;
    int y0 = *y > g_display_clip_y ? *y : g_display_clip_y;
    int x1 = (*x + *w) < (g_display_clip_x + g_display_clip_w)
               ? (*x + *w) : (g_display_clip_x + g_display_clip_w);
    int y1 = (*y + *h) < (g_display_clip_y + g_display_clip_h)
               ? (*y + *h) : (g_display_clip_y + g_display_clip_h);
    if (x1 <= x0 || y1 <= y0)
        return false;
    *x = x0;
    *y = y0;
    *w = x1 - x0;
    *h = y1 - y0;
    return true;
}

/* Destino ativo de desenho: 0 = dispositivo (VRAM); senao, objeto DIB
 * offscreen criado por IDisplay_CreateDIBitmap (layout ZCLSID_DEVICE_BITMAP:
 * +8 endereco dos pixels XRGB8888, +20 largura, +22 altura, +24 passo em
 * bytes, +28 profundidade). Espelha IDISPLAY_SetDestination/BitBlt reais. */
static uint32_t g_display_dest_obj = 0;

static void display_fill_rect_xrgb(int x, int y, int w, int h, uint32_t xrgb) {
    if (g_display_dest_obj == 0) {
        if (display_clip_rect(&x, &y, &w, &h))
            zfb_fill_rect(x, y, w, h, xrgb);
        return;
    }
    {
        uint32_t bits = zmem_read32(g_display_dest_obj + 8);
        int dw = (int)zmem_read16(g_display_dest_obj + 20);
        int dh = (int)zmem_read16(g_display_dest_obj + 22);
        int pitch = (int)zmem_read16(g_display_dest_obj + 24);
        int xx, yy;
        if (!bits) return;
        for (yy = y; yy < y + h; yy++) {
            if (yy < 0 || yy >= dh) continue;
            for (xx = x; xx < x + w; xx++) {
                if (xx < 0 || xx >= dw) continue;
                zmem_write32(bits + (uint32_t)yy * (uint32_t)pitch + (uint32_t)xx * 4u, xrgb);
            }
        }
    }
}

static void display_fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    display_fill_rect_xrgb(x, y, w, h, display_rgbval_to_xrgb(rgb));
}

/* ---- Texto: fonte 5x7 embutida (aproximacao do glcdfont classico) e
 * resolucao de ponteiro de texto (AECHAR UTF-16LE, byte ASCII, ou um nivel
 * de indirecao via descritor - padrao usado por alguns SDKs BREW/Zeebo,
 * confirmado no zeemu como resolve_display_text_descriptor). ---- */
static const uint8_t ZFONT5X7[91][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */
    {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */
    {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */
    {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x14,0x08,0x3E,0x08,0x14}, /* * */
    {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x08,0x14,0x22,0x41,0x00}, /* < */
    {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x00,0x41,0x22,0x14,0x08}, /* > */
    {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* @ */
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x3E,0x41,0x49,0x49,0x7A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x3F,0x40,0x38,0x40,0x3F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x07,0x08,0x70,0x08,0x07}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
};
#define ZFONT5X7_FIRST 0x20
#define ZFONT5X7_LAST  0x5A

static bool display_looks_ascii(uint32_t p) {
    int printable = 0, total = 0, i;
    if (!p || p >= ZDISPLAY_ADDR_MAX) return false;
    for (i = 0; i < 16; i++) {
        uint8_t c = zmem_read8(p + (uint32_t)i);
        if (!c) break;
        total++;
        if ((c >= 0x20 && c <= 0x7E) || c == '\n' || c == '\r' || c == '\t')
            printable++;
    }
    return total >= 2 && printable == total;
}

/* Um nivel de indirecao: alguns jogos passam um "descritor" cujo +0 e um
 * objeto e +8 desse objeto e o char* real (visto no zeemu como padrao
 * recorrente de SDKs BREW customizados). */
static uint32_t display_resolve_text_ptr(uint32_t pch, bool *force_byte) {
    uint32_t object, text;
    *force_byte = false;
    if (!pch || pch >= ZDISPLAY_ADDR_MAX) return pch;
    object = zmem_read32(pch);
    if (!object || object >= ZDISPLAY_ADDR_MAX) return pch;
    text = zmem_read32(object + 8);
    if (display_looks_ascii(text)) {
        *force_byte = true;
        return text;
    }
    return pch;
}

static int display_read_text(uint32_t pch, int n_chars, bool force_byte,
                              char *out, int out_cap) {
    int limit = n_chars, i, n = 0;
    if (limit < 0 || limit > 4096) limit = 4096;
    if (!pch || pch >= ZDISPLAY_ADDR_MAX) { out[0] = '\0'; return 0; }
    for (i = 0; i < limit && n < out_cap - 1; i++) {
        uint32_t c = force_byte ? zmem_read8(pch + (uint32_t)i)
                                 : zmem_read16(pch + (uint32_t)i * 2u);
        if (!c) break;
        out[n++] = (c == '\n' || c == '\r' || c == '\t' || (c >= 0x20 && c < 0x7F))
                       ? (char)c : '?';
    }
    out[n] = '\0';
    return n;
}

/* Resolve o texto tentando AECHAR (UTF-16LE) e byte (ASCII/OEM), preferindo
 * a interpretacao mais "imprimivel" - mesma heuristica do zeemu. */
static int display_resolve_string(uint32_t pch, int n_chars, char *out, int out_cap) {
    bool force_byte = false;
    uint32_t resolved = display_resolve_text_ptr(pch, &force_byte);
    if (force_byte)
        return display_read_text(resolved, n_chars, true, out, out_cap);
    {
        char ae_buf[160], byte_buf[160];
        int ae_n = display_read_text(resolved, n_chars, false, ae_buf, sizeof(ae_buf));
        int byte_n = display_read_text(resolved, n_chars, true, byte_buf, sizeof(byte_buf));
        int ae_score = 0, byte_score = 0, i;
        for (i = 0; i < ae_n; i++) if (ae_buf[i] != '?') ae_score++;
        for (i = 0; i < byte_n; i++) if (byte_buf[i] != '?') byte_score++;
        if (byte_n > 0 && byte_score > ae_score + 2 && byte_n > ae_n) {
            int cp = byte_n < out_cap - 1 ? byte_n : out_cap - 1;
            memcpy(out, byte_buf, (size_t)cp);
            out[cp] = '\0';
            return cp;
        }
        {
            int cp = ae_n < out_cap - 1 ? ae_n : out_cap - 1;
            memcpy(out, ae_buf, (size_t)cp);
            out[cp] = '\0';
            return cp;
        }
    }
}

static void display_draw_char(int x, int y, char ch, uint32_t xrgb) {
    uint8_t c = (uint8_t)ch;
    int col, row;
    const uint8_t *glyph;
    if (c >= 'a' && c <= 'z') c = (uint8_t)(c & 0xDFu); /* aproxima minusculas com maiusculas */
    if (c < ZFONT5X7_FIRST || c > ZFONT5X7_LAST) c = '?';
    glyph = ZFONT5X7[c - ZFONT5X7_FIRST];
    for (col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (row = 0; row < 7; row++) {
            if (bits & (uint8_t)(1u << row))
                display_fill_rect_xrgb(x + col, y + row, 1, 1, xrgb);
        }
    }
}

/* Desenha a string e devolve a largura total em pixels (5px + 1px de gap
 * por caractere), usada tambem por MeasureTextEx. */
static int display_draw_text(int x, int y, const char *s, uint32_t xrgb, bool actually_draw) {
    int cx = x, max_w = 0;
    for (; *s; s++) {
        if (*s == '\n') {
            if (cx - x > max_w) max_w = cx - x;
            y += 8;
            cx = x;
            continue;
        }
        if (actually_draw)
            display_draw_char(cx, y, *s, xrgb);
        cx += 6;
    }
    if (cx - x > max_w) max_w = cx - x;
    return max_w;
}

/* NOTA: nao rotear AEECLSID_DISPLAY_REAL para uma vtable propria com os
 * traps ZT_DISP_* legados: o layout real do IDisplay (AEEDisplay.h, ver
 * BrewDisplay do zeemu) tem 48 slots ([4]DrawText [6]BitBlt [7]Update
 * [16]GetDeviceBitmap...) com assinaturas proprias. Uma vtable curta em
 * ordem inventada faz o jogo ler alem do fim dela (lixo do heap virando
 * "funcao"), saltar para 0x0, re-executar o entry do modulo (que zera o
 * BSS inteiro) e descarrilhar com SP=0. O stub generico abaixo cobre o
 * display por enquanto (GetDeviceBitmap etc. tratados por clsid+slot). */
static uint32_t make_stub_interface(uint32_t clsid) {
    uint32_t obj = zheap_alloc(64);
    if (!g_stub_vtbl) {
        int i;
        g_stub_vtbl = zheap_alloc(64 * 4);
        if (!g_stub_vtbl) return 0;
        LOGI("stub vtable alocada em 0x%08X", g_stub_vtbl);
        for (i = 0; i < 64; i++)
            zmem_write32(g_stub_vtbl + (uint32_t)i * 4,
                         ZTRAP_ADDR(ZT_STUB_BASE + i));
        LOGI("stub vtable[0] = 0x%08X (deve ser 0x%08X)",
             zmem_read32(g_stub_vtbl), ZTRAP_ADDR(ZT_STUB_BASE));
    }
    if (!obj) return 0;
    memset((void *)zmem_host_ptr(obj, 64), 0, 64);
    zmem_write32(obj, g_stub_vtbl);
    uint32_t vtbl_read = zmem_read32(obj);
    uint32_t vtbl0 = zmem_read32(vtbl_read);
    LOGI("stub obj=0x%08X vtbl=0x%08X vtbl[0]=0x%08X clsid=0x%08X",
         obj, vtbl_read, vtbl0, clsid);
    zmem_write32(obj + 4, clsid);
    return obj;
}

/* Cria um bitmap offscreen (IDIB) real: objeto no layout ZCLSID_DEVICE_BITMAP
 * (mesmo usado por GetDeviceBitmap) com pixels XRGB8888 alocados no heap -
 * usado por IDisplay_CreateDIBitmap/CreateDIBitmapEx. */
static uint32_t display_create_dib(int width, int height) {
    uint32_t obj, bits;
    uint32_t pitch;
    if (width <= 0 || height <= 0 || width > 2048 || height > 2048)
        return 0;
    obj = make_stub_interface(ZCLSID_DEVICE_BITMAP);
    if (!obj) return 0;
    pitch = (uint32_t)width * 4u;
    bits = zheap_alloc(pitch * (uint32_t)height);
    if (!bits) return 0;
    zmem_write32(obj + 8, bits);
    zmem_write16(obj + 20, (uint16_t)width);
    zmem_write16(obj + 22, (uint16_t)height);
    zmem_write16(obj + 24, (uint16_t)pitch);
    zmem_write8(obj + 28, 32);
    zmem_write8(obj + 29, 32);
    return obj;
}

/* Algumas ROMs Zeebo usam um thunk de vtable que empurra copias extras de
 * R0/R1/R2 na pilha antes dos argumentos reais (confirmado no zeemu como
 * display_arg_stack_base) - detecta e pula essas copias. */
static uint32_t display_find_arg_base(void) {
    uint32_t sp = g_cpu.r[REG_SP];
    uint32_t r0 = g_cpu.r[0], r1 = g_cpu.r[1], r2 = g_cpu.r[2];
    int depth;
    if (sp == 0 || sp >= ZDISPLAY_ADDR_MAX) return sp;
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

static bool is_known_applet(uint32_t clsid) {
    unsigned i;
    for (i = 0; i < ZAEE_KNOWN_APPLET_COUNT; i++) {
        if (ZAEE_KNOWN_APPLETS[i].clsid == clsid)
            return true;
    }
    return false;
}

/* IDisplay1 mantem o prefixo de vtable de IDisplay usado pelos jogos
 * ARMCC (incluindo GetDeviceBitmap no slot 16). */
static bool is_display_interface(uint32_t clsid) {
    return clsid == AEECLSID_DISPLAY_REAL ||
           clsid == AEECLSID_DISPLAY1_REAL;
}

static bool is_stub_service(uint32_t clsid) {
    switch (clsid) {
    case AEECLSID_DISPLAY_REAL:
    case AEECLSID_DISPLAY1_REAL:
    case AEECLSID_HEAP_REAL:
    case AEECLSID_FILEMGR_REAL:
    case AEECLSID_DBMGR_REAL:
    case AEECLSID_THREAD_REAL:
    case AEECLSID_MEMCACHE1_REAL:
    case AEECLSID_RANDOM_REAL:
    case AEECLSID_SOUNDPLAYER_R:
    case AEECLSID_GRAPHICS_REAL:
    case AEECLSID_GRAPHICS_BREW:
    case AEECLSID_APPLETCTL_REAL:
    case AEECLSID_SIGNALCBFACT_R:
    case AEECLSID_MEDIA_REAL:
    case AEECLSID_MEDIAMIDI_REAL:
    case AEECLSID_MEDIAMP3_REAL:
    case AEECLSID_MEDIAQCP_REAL:
    case AEECLSID_MEDIAPMD_REAL:
    case AEECLSID_MEDIAMPEG4_REAL:
    case AEECLSID_MEDIAMMF_REAL:
    case AEECLSID_MEDIAADPCM_REAL:
    case AEECLSID_MEDIAAAC_REAL:
    case AEECLSID_MEDIAUTIL_REAL:
    case AEECLSID_MEDIAAMR_REAL:
    case AEECLSID_MEDIASAF_REAL:
    case AEECLSID_MEDIAPCM_REAL:
    case AEECLSID_MEDIAXMF_REAL:
    case AEECLSID_MEDIADLS_REAL:
    case AEECLSID_MEDIASVG_REAL:
    case AEECLSID_MEDIAWMA_RAW:
    case AEECLSID_GL_REAL:
    case AEECLSID_EGL_REAL:
    case AEECLSID_QEGL_REAL:
    case AEECLSID_HID_REAL:
    case AEECLSID_I3D_REAL:
    case AEECLSID_I3DUTIL_REAL:
    case AEECLSID_I3DMODEL_REAL:
    case AEECLSID_TEXTCTL_REAL:
    case AEECLSID_MENUCTL_REAL:
    case AEECLSID_LISTCTL_REAL:
    case AEECLSID_ICONVIEWCTL_R:
        return true;
    default:
        return false;
    }
}

/* ---- guest call ---- */
static void guest_call(uint32_t func, uint32_t r0, uint32_t r1,
                       uint32_t r2, uint32_t r3) {
    g_cpu.r[0] = r0;
    g_cpu.r[1] = r1;
    g_cpu.r[2] = r2;
    g_cpu.r[3] = r3;
    /* Codigo RVCT dos modulos reais le 'this' de R4 em labels internos
     * de vtable (ex: CreateInstance do Pac-Mania: LDR R0,[R4,#4]). */
    g_cpu.r[4] = r0;
    g_cpu.r[REG_LR] = ZTRAP_ADDR(ZT_GUEST_RETURN);
    g_cpu.halted = false;
    zcpu_bx(func | (func & 1));
}

void zboot_start(uint32_t entry, uint32_t applet_clsid) {
    memset(g_timers, 0, sizeof(g_timers));
    g_stub_vtbl = 0;
    g_device_bitmap = 0;
    g_display_text_color = 0xFFFFFF00u;
    g_display_background_color = 0x00000000u;
    g_display_line_color = 0xFFFFFF00u;
    g_display_clip_x = 0;
    g_display_clip_y = 0;
    g_display_clip_w = 640;
    g_display_clip_h = 480;
    g_hid_button_signal = 0;
    g_hid_position_signal = 0;
    g_hid_event_pending = false;
    g_hid_connect_signal = 0;
    g_hid_connect_pending = false;
    memset(g_shell_prefs, 0, sizeof(g_shell_prefs));
    memset(g_shell_events, 0, sizeof(g_shell_events));
    g_applet_clsid = applet_clsid;

    g_shell_obj = setup_real_shell();
    g_ppmod = zheap_alloc(4);
    zmem_write32(g_ppmod, 0);

    if (!g_shell_obj || !g_ppmod) {
        LOGE("boot: sem heap para objetos base");
        g_state = BOOT_FAILED;
        return;
    }

    LOGI("boot: AEEMod_Load(shell=0x%08X, ph=0, ppMod=0x%08X) entry=0x%08X",
         g_shell_obj, g_ppmod, entry);
    g_state = BOOT_MOD_LOAD;
    /* AEEMod_Load(IShell*, void* ph, IModule**). Bootstraps de modulos
     * reais variam a posicao dos args (o stub ROPI pode deslocar),
     * entao duplicamos: shell em R0 e R1, ppMod em R2 e R3. */
    guest_call(entry, g_shell_obj, g_shell_obj, g_ppmod, g_ppmod);
}

void zboot_on_guest_return(void) {
    uint32_t result = g_cpu.r[0];

    switch (g_state) {
    case BOOT_MOD_LOAD: {
        g_module_obj = zmem_read32(g_ppmod);
        LOGI("boot: AEEMod_Load retornou 0x%08X, IModule=0x%08X",
             result, g_module_obj);
        if (result != 0 || !g_module_obj) {
            LOGE("boot: AEEMod_Load falhou");
            g_state = BOOT_FAILED;
            g_cpu.halted = true;
            return;
        }
        /* Layout observado no SDK: {vtbl, refcount, pIShell,
         * pfnModCrInst, ...}. O campo +4 nao e o IShell. */
        LOGI("boot: AEEMod +0=0x%08X +4=0x%08X +8=0x%08X +12=0x%08X +16=0x%08X",
             zmem_read32(g_module_obj), zmem_read32(g_module_obj + 4),
             zmem_read32(g_module_obj + 8), zmem_read32(g_module_obj + 12),
             zmem_read32(g_module_obj + 16));
        /* Se pIShell nao aponta para nosso objeto, corrige apenas +8 e
         * preserva o refcount em +4. */
        if (zmem_read32(g_module_obj + 8) != g_shell_obj) {
            LOGW("boot: corrigindo AEEMod.m_pIShell (era 0x%08X)",
                 zmem_read32(g_module_obj + 8));
            zmem_write32(g_module_obj + 8, g_shell_obj);
        }
        /* pfnModCrInst invalido -> forca caminho dinamico (tecnica zeemu) */
        {
            uint32_t pfn = zmem_read32(g_module_obj + 12);
            if (pfn != 0 && pfn >= ZMEM_RAM_SIZE) {
                LOGW("boot: sanitizando pfnModCrInst=0x%08X -> 0", pfn);
                zmem_write32(g_module_obj + 12, 0);
            }
        }
        uint32_t vtbl = zmem_read32(g_module_obj);
        uint32_t create = zmem_read32(vtbl + 8);
        g_applet_out = zheap_alloc(4);
        zmem_write32(g_applet_out, 0);
        LOGI("boot: IModule_CreateInstance(clsid=0x%08X) via 0x%08X",
             g_applet_clsid, create);
        g_state = BOOT_CREATE_APPLET;
        guest_call(create, g_module_obj, g_shell_obj,
                   g_applet_clsid, g_applet_out);
        return;
    }

    case BOOT_CREATE_APPLET: {
        g_applet_obj = zmem_read32(g_applet_out);
        LOGI("boot: CreateInstance retornou 0x%08X, applet=0x%08X",
             result, g_applet_obj);
        /* AEEClsCreateInstance gerado pelo BREW pode retornar booleano TRUE
         * enquanto outros modulos usam AEE_SUCCESS (0). O ponteiro de saida
         * valido e a fonte autoritativa para a criacao do applet. */
        if (!g_applet_obj) {
            LOGE("boot: applet nao foi criado (clsid errado?)");
            g_state = BOOT_FAILED;
            g_cpu.halted = true;
            return;
        }
        uint32_t vtbl = zmem_read32(g_applet_obj);
        uint32_t handle_evt = zmem_read32(vtbl + 8);
        LOGI("boot: HandleEvent(EVT_APP_START) via 0x%08X", handle_evt);
        g_state = BOOT_APP_START;
        /* O SDK usado pelo Family Pack codifica EVT_APP_START como 0. */
        guest_call(handle_evt, g_applet_obj, 0, 0, 0);
        return;
    }

    case BOOT_APP_START:
        LOGI("boot: EVT_APP_START tratado (ret=0x%08X) - jogo RODANDO", result);
        g_state = BOOT_RUNNING;
        g_cpu.halted = true; /* espera timers/eventos */
        return;

    case BOOT_TIMER_CALL:
    case BOOT_SIGNAL_CALL:
        g_state = BOOT_RUNNING;
        g_cpu.halted = true;
        return;

    default:
        LOGW("boot: retorno de guest call em estado inesperado (%s)",
             zboot_state_name());
        g_cpu.halted = true;
        return;
    }
}

void zboot_process_timers(void) {
    int i;
    uint32_t now = zbrew_uptime_ms();

    /* Watchdog do callback de sinal (ver comentario em g_signal_call_started_at):
     * roda mesmo com o jogo tecnicamente "parado" nesse estado, porque a CPU
     * pode estar halted=false rodando o callback ha varios frames. */
    if (g_state == BOOT_SIGNAL_CALL &&
        g_cpu.executed - g_signal_call_started_at > ZSIGNAL_CALL_INSTR_BUDGET) {
        LOGW("boot: callback de sinal nao retornou apos %llu instrucoes - "
             "forcando volta para RUNNING (provavel loop degenerado no guest)",
             (unsigned long long)(g_cpu.executed - g_signal_call_started_at));
        g_state = BOOT_RUNNING;
        g_cpu.halted = true;
    }

    if (g_state != BOOT_RUNNING)
        return;

    /* Sinal de conexao de HID (IHID_RegisterForConnectEvents, case 6):
     * dispara uma unica vez, como um timer de disparo imediato. Sem isso
     * jogos que esperam esse callback antes de iniciar o loop de jogo
     * (ex: Family Pack) ficam parados para sempre depois do boot. */
    if (g_hid_connect_pending) {
        uint32_t cb = zmem_read32(g_hid_connect_signal + 8);
        uint32_t user = zmem_read32(g_hid_connect_signal + 12);
        LOGI("boot: sinal de conexao HID disparado, callback=0x%08X user=0x%08X",
             cb, user);
        g_hid_connect_pending = false;
        g_state = BOOT_SIGNAL_CALL;
        g_signal_call_started_at = g_cpu.executed;
        guest_call(cb, user, 0, 0, 0);
        return;
    }

    for (i = 0; i < ZTIMER_MAX; i++) {
        if (!g_timers[i].active)
            continue;

        if (now >= g_timers[i].expires_ms) {
            LOGI("boot: timer %d expirou, callback=0x%08X user=0x%08X",
                 i, g_timers[i].pfn, g_timers[i].puser);
            g_state = BOOT_TIMER_CALL;
            g_timers[i].active = false;
            guest_call(g_timers[i].pfn, g_timers[i].puser, 0, 0, 0);
            return;
        }
    }

    /* Sem trabalho pendente, o PC continua no trap de retorno do ultimo
     * guest call. A CPU deve ficar parada ate um timer ou evento real. */
}

void zboot_tick(uint32_t elapsed_ms) {
    int i;
    (void)elapsed_ms;
    if (g_state != BOOT_RUNNING) return;

    {
        uint32_t changed = zinput_pressed();
        bool down = true;
        uint32_t bit = 0;
        uint32_t id = 0;
        uint32_t uid = 0;
        if (!changed) {
            changed = zinput_released();
            down = false;
        }
        if (changed) {
            static const struct {
                uint32_t bit, id, uid;
            } map[] = {
                { ZKEY_A,     0,  0x0106C40Au },
                { ZKEY_B,     3,  0x0106C40Du },
                { ZKEY_C,     2,  0x0106C40Cu },
                { ZKEY_UP,    12, 0x0106C3FEu },
                { ZKEY_LEFT,  13, 0x0106C3FFu },
                { ZKEY_DOWN,  14, 0x0106C400u },
                { ZKEY_RIGHT, 15, 0x0106C401u },
                { ZKEY_MENU,  9,  0x0106C402u },
                { ZKEY_BACK,  8,  0x0106C403u },
            };
            unsigned n;
            for (n = 0; n < sizeof(map) / sizeof(map[0]); n++) {
                if (changed & map[n].bit) {
                    bit = map[n].bit;
                    id = map[n].id;
                    uid = map[n].uid;
                    break;
                }
            }
        }
        if (bit && g_hid_button_signal) {
            uint32_t callback = zmem_read32(g_hid_button_signal + 8);
            uint32_t user = zmem_read32(g_hid_button_signal + 12);
            g_hid_event_id = id;
            g_hid_event_uid = uid;
            g_hid_event_down = down;
            g_hid_event_pending = true;
            if (getenv("ZEEMU_TRACE_HID")) {
                LOGI("HID: uid=0x%08X down=%u callback=0x%08X user=0x%08X",
                     uid, down ? 1u : 0u, callback, user);
            }
            /* Convencao BREW: ISignalCtl e so um "acorde-me" (PFNNOTIFY(pUser)),
             * sem parametros de evento - o jogo consulta os detalhes depois via
             * IHIDDevice_GetNextEvent (case 9, que ja devolve id/down/uid).
             * Antes desta chamada, o callback so era logado e nunca disparado:
             * o jogo nunca acordava do halted apos EVT_APP_START. */
            if (callback) {
                g_state = BOOT_SIGNAL_CALL;
                guest_call(callback, user, 0, 0, 0);
                return;
            }
        }
    }

    uint32_t now = zbrew_uptime_ms();
    for (i = 0; i < ZTIMER_MAX; i++) {
        if (!g_timers[i].active) continue;
        if ((int32_t)(now - g_timers[i].expires_ms) < 0) continue;
        g_timers[i].active = false;
        g_state = BOOT_TIMER_CALL;
        /* PFNNOTIFY(pUser) */
        guest_call(g_timers[i].pfn, g_timers[i].puser, 0, 0, 0);
        return; /* um timer por vez; o resto dispara no proximo tick */
    }
}

/* ---- IShell real (traps 0x200+) ---- */

/* Servicos recebem stubs COM permissivos para deixar jogos reais avancarem.
 * A lista conhecida ajuda QueryClass, mas CreateInstance tambem tolera
 * CLSIDs desconhecidos para evitar travar cedo no boot. */
static uint32_t real_create_instance(uint32_t clsid) {
    if (clsid == AEECLSID_FILEMGR_REAL)
        return zbrew_create_filemgr();
    if (clsid == AEECLSID_EGL_REAL)
        return zegl_create_interface();
    if (clsid == AEECLSID_GL_REAL)
        return zgl_create_interface();
    return make_stub_interface(clsid);
}

void zbrew_handle_ishell_real(uint32_t id) {
    uint32_t slot = id - ZT_ISHELL_BASE;
    const char *name = slot < ZAEE_ISHELL_NAMED_COUNT
                     ? ZAEE_ISHELL_NAMES[slot] : "?";
    uint32_t r1 = g_cpu.r[1], r2 = g_cpu.r[2], r3 = g_cpu.r[3];

    switch (slot) {
    case 0: case 1: /* AddRef/Release */
        g_cpu.r[0] = 1;
        break;

    case 5: /* StartApplet */
        g_cpu.r[0] = 0;
        break;

    case 6: /* CloseApplet */
        g_cpu.r[0] = 0;
        break;

    case 7: /* CanStartApplet */
        g_cpu.r[0] = 1;
        break;

    case 8: /* ActiveApplet */
        g_cpu.r[0] = zboot_get_applet_object();
        break;

    case 9: /* EnumAppletInit */
        if (r1) zmem_write32(r1, 0);
        g_cpu.r[0] = 1;
        break;

    case 10: /* EnumNextApplet */
        g_cpu.r[0] = 0;
        break;

    case 2: { /* CreateInstance(shell, clsid, ppObj) */
        uint32_t iface = 0;
        if (r1 == g_applet_clsid && g_applet_obj) {
            iface = g_applet_obj; /* auto-criacao do proprio applet */
        } else {
            iface = real_create_instance(r1);
        }
        if (r2) zmem_write32(r2, iface);
        LOGI("IShell_CreateInstance(0x%08X) -> 0x%08X", r1, iface);
        g_cpu.r[0] = iface ? 0 : 1 /* EFAILED (como o zeemu) */;
        break;
    }

    case 3: { /* QueryClass(shell, clsid, pInfo) */
        bool applet = is_known_applet(r1);
        bool supported = applet || is_stub_service(r1);
        if (r2 && applet) {
            zmem_write32(r2 + 0x00, r1);      /* clsid */
            zmem_write32(r2 + 0x04, 0);       /* pszMIF */
            zmem_write16(r2 + 0x08, 1);       /* wIDBase */
            zmem_write16(r2 + 0x0A, 0);       /* wAppType */
            zmem_write16(r2 + 0x0C, (uint16_t)(r1 & 0xFFFF));
            zmem_write16(r2 + 0x0E, (uint16_t)(r1 >> 16));
            zmem_write16(r2 + 0x10, 0);
            zmem_write16(r2 + 0x12, 0);
        }
        g_cpu.r[0] = supported ? 1 : 0;       /* boolean */
        break;
    }

    case 4: { /* GetDeviceInfo(shell, pInfo) - layout AEEDeviceInfo */
        uint32_t p = r1;
        if (p) {
            zmem_write16(p + 0, 640);      /* cxScreen */
            zmem_write16(p + 2, 480);      /* cyScreen */
            zmem_write16(p + 4, 0);        /* cxAltScreen */
            zmem_write16(p + 6, 0);        /* cyAltScreen */
            zmem_write16(p + 8, 8);        /* cxScrollBar */
            zmem_write16(p + 10, 0x00FD);  /* wEncoding = S_JIS */
            zmem_write16(p + 12, 200);     /* wMenuTextScroll */
            zmem_write16(p + 14, 24);      /* nColorDepth */
            zmem_write32(p + 16, 0);
            zmem_write32(p + 20, 1000);
            zmem_write32(p + 24, 64u * 1024u * 1024u); /* dwRAM */
            zmem_write32(p + 28, 0x10C);   /* flags */
            zmem_write32(p + 32, 0);
            zmem_write16(p + 36, 0xE030);  /* close key */
            zmem_write16(p + 38, 0);
            zmem_write32(p + 40, 0x72627470u); /* 'ptbr' */
            zmem_write16(p + 44, 64);      /* wStructSize */
            zmem_write32(p + 48, 0);
            zmem_write32(p + 52, 0);
            zmem_write16(p + 56, 260);     /* wMaxPath */
            zmem_write32(p + 60, 0x01000000u);
        }
        g_cpu.r[0] = 0;
        break;
    }

    case 14: /* CreateDialog */
        if (r3) zmem_write32(r3, make_stub_interface(0));
        g_cpu.r[0] = 0;
        break;

    case 15: /* GetActiveDialog */
        g_cpu.r[0] = 0;
        break;

    case 16: /* EndDialog */
        g_cpu.r[0] = 0;
        break;

    case 13: /* GetTimerExpiration(shell, pfn, puser) */
        g_cpu.r[0] = 0;
        break;

    case 17: { /* LoadResString(shell, path, id, buffer, size) */
        uint32_t pbuf = r3;
        uint32_t size = zbrew_stack_arg(0);
        if (pbuf && size > 0)
            zmem_write8(pbuf, 0);
        g_cpu.r[0] = 0;
        break;
    }

    case 18: /* LoadResData(shell, path, id, type) */
    case 19: /* LoadResObject(shell, path, id, htype) */
        g_cpu.r[0] = make_stub_interface(0);
        break;

    case 20: /* FreeResData(shell, data) */
        g_cpu.r[0] = 0;
        break;

    case 11: { /* SetTimer(shell, ms, pfn, puser) */
        int i;
        for (i = 0; i < ZTIMER_MAX; i++) {
            if (g_timers[i].active) continue;
            g_timers[i].active = true;
            g_timers[i].expires_ms = zbrew_uptime_ms() + r1;
            g_timers[i].pfn = r2;
            g_timers[i].puser = r3;
            break;
        }
        if (i == ZTIMER_MAX) LOGW("IShell_SetTimer: fila cheia");
        else LOGI("IShell_SetTimer: %u ms callback=0x%08X user=0x%08X",
                  r1, r2, r3);
        g_cpu.r[0] = 0;
        break;
    }

    case 12: { /* CancelTimer(shell, pfn, puser) */
        int i;
        for (i = 0; i < ZTIMER_MAX; i++) {
            if (g_timers[i].active &&
                (r1 == 0 || g_timers[i].pfn == r1) &&
                (r2 == 0 || g_timers[i].puser == r2))
                g_timers[i].active = false;
        }
        LOGI("IShell_CancelTimer: callback=0x%08X user=0x%08X", r1, r2);
        g_cpu.r[0] = 0;
        break;
    }

    case 21: /* SendEvent - devolve FALSE (nao tratado) */
        g_cpu.r[0] = 0;
        break;

    case 22: /* Beep */
        g_cpu.r[0] = 0;
        break;

    case 23: case 24: /* GetPrefs/SetPrefs */
        g_cpu.r[0] = 1; /* EFAILED */
        break;

    case 25: /* GetItemStyle */
        g_cpu.r[0] = 0;
        break;

    case 26: /* Prompt */
    case 27: /* MessageBox */
    case 28: /* MessageBoxText */
        g_cpu.r[0] = 0;
        break;

    case 29: /* SetAlarm */
    case 30: /* CancelAlarm */
        g_cpu.r[0] = 0;
        break;

    case 31: /* AlarmsActive */
        g_cpu.r[0] = 0;
        break;

    case 32: /* GetHandler */
        g_cpu.r[0] = 0;
        break;

    case 33: /* RegisterHandler */
    case 34: /* RegisterNotify */
    case 35: /* Notify */
        g_cpu.r[0] = 0;
        break;

    case 36: /* Resume */
    case 37: /* ForceExit */
        g_cpu.r[0] = 0;
        break;

    case 38: /* GetPosition */
        if (r2) zmem_write32(r2, 0);
        if (r3) zmem_write32(r3, 0);
        g_cpu.r[0] = 0;
        break;

    case 39: /* CheckPrivLevel */
        g_cpu.r[0] = 1;
        break;

    case 40: /* IsValidResource */
        g_cpu.r[0] = 0;
        break;

    case 41: /* LoadResDataEx */
        if (r3)
            zmem_write32(r3, 0);
        g_cpu.r[0] = 0;
        break;

    case 42: /* RegisterSystemCallback */
        g_cpu.r[0] = 0;
        break;

    case 43: /* DetectType */
        g_cpu.r[0] = 1;
        break;

    case 44: /* GetDeviceInfoEx */
        if (r3)
            zmem_write32(r3, 0);
        g_cpu.r[0] = 0;
        break;

    case 45: /* GetClassItemID(shell, clsid) */
        g_cpu.r[0] = (r1 && r1 == g_applet_clsid) ? 1 : 0;
        break;

    case 47: /* GetProperty */
        if (r2 && (r1 & 7u) < 8u) zmem_write32(r2, g_shell_prefs[r1 & 7u]);
        g_cpu.r[0] = 0;
        break;
    case 48: /* SetProperty */
        if ((r1 & 7u) < 8u) g_shell_prefs[r1 & 7u] = r2;
        g_cpu.r[0] = 0;
        break;
    case 49: /* RegisterEvent */
        if ((r1 & 7u) < 8u) g_shell_events[r1 & 7u] = r2;
        g_cpu.r[0] = 0;
        break;
    case 50: /* Reset */
        memset(g_shell_prefs, 0, sizeof(g_shell_prefs));
        memset(g_shell_events, 0, sizeof(g_shell_events));
        g_cpu.r[0] = 0;
        break;

    case 51: /* AppIsInGroup */
        g_cpu.r[0] = (r1 == g_applet_clsid) ? 1 : 0;
        break;

    case 52: /* GetUpTimeMS */
        g_cpu.r[0] = zbrew_uptime_ms();
        break;

    default: {
        static uint32_t warn_count = 0;
        if (warn_count < 64) {
            LOGW("IShell_%s (slot %u) nao implementado "
                 "(R1=0x%08X R2=0x%08X)", name, slot, r1, r2);
            warn_count++;
        }
        g_cpu.r[0] = 1; /* EFAILED */
        break;
    }
    }
}

void zbrew_handle_stub(uint32_t id) {
    uint32_t slot = id - ZT_STUB_BASE;
    uint32_t obj = g_cpu.r[0];
    uint32_t clsid = zmem_read32(obj + 4);
    static uint32_t warn_count = 0;

    /* Stub COM generico retorna sucesso para operacoes basicas */
    switch (slot) {
    case 0: /* AddRef */
        g_cpu.r[0] = 1; /* refcount */
        break;
    case 1: /* Release */
        g_cpu.r[0] = 1; /* refcount */
        break;
    case 2: /* QueryInterface (exceto IDisplay, que nao tem QI no slot 2) */
        if (clsid == AEECLSID_QEGL_REAL &&
            (g_cpu.r[1] == 0x0103D8DDu || g_cpu.r[1] == 0x0103D8EAu)) {
            uint32_t iface = make_stub_interface(g_cpu.r[1]);
            if (g_cpu.r[2])
                zmem_write32(g_cpu.r[2], iface);
            LOGI("IQEGL_QueryInterface(0x%08X) -> 0x%08X",
                 g_cpu.r[1], iface);
            g_cpu.r[0] = iface ? 0 : 1;
            break;
        }
        if (is_display_interface(clsid)) {
            /* GetFontMetrics(po, font, AEEFontMetrics*, u16* ascent).
             * Zerar metricas trava jogos: alturas/larguras viram
             * divisores no rasterizador de texto deles. */
            uint32_t pmet = g_cpu.r[2];
            if (pmet) {
                zmem_write16(pmet + 0, 12); /* nAscent */
                zmem_write16(pmet + 2, 4);  /* nDescent */
                zmem_write16(pmet + 4, 2);  /* nLeading */
                zmem_write16(pmet + 6, 8);  /* nMaxCharWidth */
            }
            if (g_cpu.r[3]) zmem_write16(g_cpu.r[3], 12);
            g_cpu.r[0] = 16; /* altura da fonte (ascent+descent) */
            break;
        }
        if (clsid == ZCLSID_DEVICE_BITMAP) {
            if (g_cpu.r[2]) zmem_write32(g_cpu.r[2], obj);
            g_cpu.r[0] = 0;
            break;
        }
        zmem_write32(g_cpu.r[2], 0); /* ppInterface = 0 */
        g_cpu.r[0] = 1; /* EFAILED */
        break;
    case 3:
        if (is_display_interface(clsid)) {
            /* MeasureTextEx(po, font, AECHAR* psz, int nl, nMaxWidth, pnFits)
             * -> R0=largura em pixels, usa a mesma resolucao de texto (AECHAR/
             * byte/descritor) e a fonte 5x7 usadas por DrawText, para que a
             * largura reportada bata com o que sera desenhado de verdade. */
            uint32_t base = display_find_arg_base();
            uint32_t psz = g_cpu.r[2];
            int32_t nl = (int32_t)g_cpu.r[3];
            uint32_t pn_fits = zmem_read32(base + 4);
            char text[160];
            int len = display_resolve_string(psz, nl, text, sizeof(text));
            int width = len > 0 ? display_draw_text(0, 0, text, 0, false) : 16;
            if (pn_fits && pn_fits < ZDISPLAY_ADDR_MAX)
                zmem_write32(pn_fits, (uint32_t)len);
            g_cpu.r[0] = (uint32_t)width;
            break;
        }
        if (clsid == AEECLSID_SIGNALCBFACT_R) {
            uint32_t signal = make_stub_interface(AEECLSID_SIGNALCBFACT_R);
            uint32_t pp_signal = g_cpu.r[3];
            uint32_t pp_signal_ctl = zbrew_stack_arg(0);
            if (pp_signal) zmem_write32(pp_signal, signal);
            if (pp_signal_ctl) zmem_write32(pp_signal_ctl, signal);
            zmem_write32(signal + 8, g_cpu.r[1]);
            zmem_write32(signal + 12, g_cpu.r[2]);
            zmem_write32(signal + 16, 0);
            zmem_write32(signal + 20, 0);
            LOGI("SignalCBFactory_CreateSignal(cb=0x%08X user=0x%08X) -> 0x%08X",
                 g_cpu.r[1], g_cpu.r[2], signal);
            LOGI("SignalCBFactory outputs: signal=0x%08X ctl=0x%08X",
                 pp_signal, pp_signal_ctl);
            g_cpu.r[0] = 0;
            break;
        }
        if (clsid == AEECLSID_HID_REAL) { /* IHID_CreateDevice */
            uint32_t device = 0;
            if (g_cpu.r[1] == ZHID_HANDLE)
                device = make_stub_interface(ZCLSID_HID_DEVICE);
            if (g_cpu.r[2]) zmem_write32(g_cpu.r[2], device);
            LOGI("IHID_CreateDevice handle=0x%08X out=0x%08X -> 0x%08X",
                 g_cpu.r[1], g_cpu.r[2], device);
            g_cpu.r[0] = device ? 0 : 39; /* SUCCESS / ENOSUCH */
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 4:
        if (clsid == AEECLSID_QEGL_REAL) { /* IQEGL_InitGLSurface */
            uint32_t surface = zegl_create_qegl_surface();
            if (g_cpu.r[2])
                zmem_write32(g_cpu.r[2], surface);
            LOGI("IQEGL_InitGLSurface: bitmap=0x%08X out=0x%08X -> 0x%08X",
                 g_cpu.r[1], g_cpu.r[2], surface);
            g_cpu.r[0] = surface ? 0 : 1;
            break;
        }
        if (is_display_interface(clsid)) { /* DrawText */
            /* IDISPLAY_DrawText(po, font, psz, nl, x, y, prcBackground, dwFlags):
             * R0=po R1=font R2=psz R3=nl; x/y/prcb/flags na pilha (com possivel
             * ajuste de base para thunks que duplicam this/args - ver
             * display_find_arg_base). */
            uint32_t base = display_find_arg_base();
            uint32_t psz = g_cpu.r[2];
            int32_t nl = (int32_t)g_cpu.r[3];
            int x = (int32_t)zmem_read32(base + 0);
            int y = (int32_t)zmem_read32(base + 4);
            uint32_t prc = zmem_read32(base + 8);
            char text[160];
            int len;
            static uint32_t draw_text_logs = 0;

            if (prc && prc < ZDISPLAY_ADDR_MAX &&
                (x < -1024 || x > 4096 || y < -1024 || y > 4096)) {
                x = (int16_t)zmem_read16(prc + 0);
                y = (int16_t)zmem_read16(prc + 2);
            }
            len = display_resolve_string(psz, nl, text, sizeof(text));
            if (len > 0)
                display_draw_text(x, y, text, display_rgbval_to_xrgb(g_display_text_color), true);
            if (draw_text_logs < 32) {
                LOGI("IDisplay_DrawText: '%s' em (%d,%d) len=%d", text, x, y, len);
                draw_text_logs++;
            }
            zbrew_mark_frame();
            g_cpu.r[0] = 0;
            break;
        }
        if (clsid == AEECLSID_HID_REAL) { /* IHID_GetDeviceInfo */
            uint32_t info = g_cpu.r[2];
            if (g_cpu.r[1] != ZHID_HANDLE || !info) {
                g_cpu.r[0] = 39; /* ENOSUCH */
                break;
            }
            zmem_write32(info + 0, ZHID_JOYSTICK_UID);
            zmem_write16(info + 4, 0x1234);
            zmem_write16(info + 6, 0x5678);
            zmem_write8(info + 8, 0);
            g_cpu.r[0] = 0;
            break;
        }
        if (clsid == ZCLSID_HID_DEVICE) { /* IHIDDevice_GetDeviceInfo */
            uint32_t info = g_cpu.r[1];
            if (info) {
                zmem_write32(info + 0, ZHID_JOYSTICK_UID);
                zmem_write16(info + 4, 0x1234);
                zmem_write16(info + 6, 0x5678);
                zmem_write8(info + 8, 0);
            }
            g_cpu.r[0] = info ? 0 : 14; /* SUCCESS / EBADPARM */
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 5:
        if (clsid == AEECLSID_QEGL_REAL) {
            uint32_t pp_gl = zbrew_stack_arg(0);
            uint32_t gl = make_stub_interface(0x0103D8DDu);
            if (pp_gl)
                zmem_write32(pp_gl, gl);
            LOGI("IQEGL_CreateGL: surface=0x%08X out=0x%08X -> 0x%08X",
                 g_cpu.r[1], pp_gl, gl);
            g_cpu.r[0] = gl ? 0 : 1;
            break;
        }
        if (is_display_interface(clsid)) { /* DrawRect */
            uint32_t p_rect = g_cpu.r[1];
            uint32_t clr_frame = g_cpu.r[2];
            uint32_t clr_fill = g_cpu.r[3];
            uint32_t flags = zbrew_stack_arg(0);
            int x, y, w, h;
            static uint32_t draw_rect_logs = 0;

            display_read_rect(p_rect, &x, &y, &w, &h);
            if (!p_rect && clr_frame == ZDISPLAY_RGB_NONE &&
                clr_fill == ZDISPLAY_RGB_NONE) {
                clr_fill = g_display_background_color;
                flags = ZDISPLAY_RECT_FILL;
            }
            if ((flags & ZDISPLAY_RECT_FILL) && clr_fill != ZDISPLAY_RGB_NONE)
                display_fill_rect(x, y, w, h, clr_fill);
            if ((flags & ZDISPLAY_RECT_FRAME) && clr_frame != ZDISPLAY_RGB_NONE) {
                display_fill_rect(x, y, w, 1, clr_frame);
                display_fill_rect(x, y + h - 1, w, 1, clr_frame);
                display_fill_rect(x, y, 1, h, clr_frame);
                display_fill_rect(x + w - 1, y, 1, h, clr_frame);
            }
            if (draw_rect_logs < 16) {
                LOGI("IDisplay_DrawRect: rect=%d,%d %dx%d frame=0x%08X "
                     "fill=0x%08X flags=0x%X", x, y, w, h,
                     clr_frame, clr_fill, flags);
                draw_rect_logs++;
            }
            zbrew_mark_frame();
            g_cpu.r[0] = 0;
        } else if (clsid == 0x0100101Cu) {
            /* Esta classe usada no bootstrap do Pac-Mania cria dois
             * sub-objetos reais e recebe ponteiros de saída em R2/R3. */
            uint32_t obj1 = make_stub_interface(0x0100101Cu);
            uint32_t obj2 = make_stub_interface(0x0100101Cu);
            if (!obj1 || !obj2) {
                LOGE("CreateInstance case 5: heap alloc falhou obj1=0x%08X obj2=0x%08X",
                     obj1, obj2);
                g_cpu.r[0] = 1;  /* EFAILED */
                break;
            }
            if (g_cpu.r[2]) zmem_write32(g_cpu.r[2], obj1);
            if (g_cpu.r[3]) zmem_write32(g_cpu.r[3], obj2);
            if (obj1) {
                zmem_write32(obj1 + 8, g_shell_obj);
                zmem_write32(obj1 + 12, g_module_obj);
            }
            if (obj2) {
                zmem_write32(obj2 + 8, g_shell_obj);
                zmem_write32(obj2 + 12, g_module_obj);
            }
            LOGI("CreateInstance case 5 (0x0100101C) OK -> obj1=0x%08X obj2=0x%08X",
                 obj1, obj2);
            g_cpu.r[0] = 0;
        } else {
            LOGW("CreateInstance case 5: CLSID desconhecido 0x%08X (esperado 0x0100101C)",
                 clsid);
            g_cpu.r[0] = 0;
        }
        break;
    case 6:
        if (clsid == AEECLSID_HID_REAL) { /* IHID_RegisterForConnectEvents */
            g_hid_connect_signal = g_cpu.r[1];
            g_hid_connect_pending = g_hid_connect_signal != 0;
            LOGI("IHID_RegisterForConnectEvents signal=0x%08X",
                 g_hid_connect_signal);
            g_cpu.r[0] = 0;
            break;
        }
        if (is_display_interface(clsid)) { /* BitBlt */
            /* IDISPLAY_BitBlt(po, xDest, yDest, cxDest, cyDest, pSrc, xSrc,
             * ySrc, rop): R1=xDest R2=yDest R3=cxDest; cyDest/pSrc/xSrc/ySrc/
             * rop na pilha. pSrc precisa ser um bitmap ZCLSID_DEVICE_BITMAP
             * (device ou criado por CreateDIBitmap) - o unico formato que
             * este HLE sabe ler pixels de. */
            uint32_t base = display_find_arg_base();
            int x_dest = (int32_t)g_cpu.r[1];
            int y_dest = (int32_t)g_cpu.r[2];
            int cx_dest = (int32_t)g_cpu.r[3];
            int cy_dest = (int32_t)zmem_read32(base + 0);
            uint32_t src_ptr = zmem_read32(base + 4);
            int x_src = (int32_t)zmem_read32(base + 8);
            int y_src = (int32_t)zmem_read32(base + 12);
            uint32_t rop = zmem_read32(base + 16);
            uint32_t src_clsid = src_ptr ? zmem_read32(src_ptr + 4) : 0;
            static uint32_t bitblt_logs = 0;

            if (src_clsid == ZCLSID_DEVICE_BITMAP && cx_dest > 0 && cy_dest > 0) {
                uint32_t bits = zmem_read32(src_ptr + 8);
                int sw = (int)zmem_read16(src_ptr + 20);
                int sh = (int)zmem_read16(src_ptr + 22);
                int spitch = (int)zmem_read16(src_ptr + 24);
                int xx, yy;
                for (yy = 0; yy < cy_dest; yy++) {
                    int sy = y_src + yy;
                    if (sy < 0 || sy >= sh) continue;
                    for (xx = 0; xx < cx_dest; xx++) {
                        int sx = x_src + xx;
                        uint32_t px;
                        if (sx < 0 || sx >= sw) continue;
                        px = zmem_read32(bits + (uint32_t)sy * (uint32_t)spitch + (uint32_t)sx * 4u);
                        if (rop == 7u /* AEE_RO_TRANSPARENT */ && px == 0xFF000000u)
                            continue; /* preto puro tratado como transparente */
                        display_fill_rect_xrgb(x_dest + xx, y_dest + yy, 1, 1, px);
                    }
                }
                zbrew_mark_frame();
                g_cpu.r[0] = 0;
            } else {
                if (bitblt_logs < 8) {
                    LOGW("IDisplay_BitBlt: fonte nao suportada src=0x%08X clsid=0x%08X",
                         src_ptr, src_clsid);
                    bitblt_logs++;
                }
                g_cpu.r[0] = 0;
            }
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 7:
        if (is_display_interface(clsid)) { /* Update */
            zbrew_mark_frame();
            g_cpu.r[0] = 0;
            break;
        }
        if (clsid == AEECLSID_HID_REAL) {
            uint32_t handles = g_cpu.r[2];
            uint32_t max_handles = g_cpu.r[3];
            uint32_t required = zbrew_stack_arg(0);
            bool supported = g_cpu.r[1] == 0 ||
                             g_cpu.r[1] == ZHID_JOYSTICK_UID;
            if (required) zmem_write32(required, supported ? 1 : 0);
            if (supported && handles && max_handles)
                zmem_write32(handles, ZHID_HANDLE);
            LOGI("IHID_GetConnectedDevices(type=0x%08X) -> %u",
                 g_cpu.r[1], supported ? 1u : 0u);
            g_cpu.r[0] = supported ? 0 : 14;
            break;
        }
        if (clsid == ZCLSID_HID_DEVICE) { /* GetNumberOfButtons */
            if (g_cpu.r[1]) zmem_write32(g_cpu.r[1], 16);
            g_cpu.r[0] = 0;
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 8:
        if (clsid == ZCLSID_HID_DEVICE) {
            g_hid_button_signal = g_cpu.r[1];
            LOGI("IHIDDevice_RegisterForButtonEvent signal=0x%08X",
                 g_hid_button_signal);
        }
        g_cpu.r[0] = 0;
        break;
    case 9:
        if (clsid == ZCLSID_HID_DEVICE) {
            uint32_t info = g_cpu.r[1];
            if (!g_hid_event_pending) {
                g_cpu.r[0] = 5;
                break;
            }
            if (info) {
                zmem_write32(info + 0, g_hid_event_id);
                zmem_write32(info + 4, g_hid_event_down ? 1 : 0);
                zmem_write32(info + 8, g_hid_event_uid);
                zmem_write32(info + 12, 0);
                zmem_write32(info + 16, 1);
            }
            if (g_cpu.r[2]) zmem_write32(g_cpu.r[2], 0);
            if (g_cpu.r[3]) zmem_write8(g_cpu.r[3], 0);
            g_hid_event_pending = false;
            g_cpu.r[0] = 0;
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 10:
        if (is_display_interface(clsid)) { /* SetColor */
            uint32_t index = g_cpu.r[1];
            uint32_t color = g_cpu.r[2];
            uint32_t previous = 0;
            uint32_t *selected = NULL;
            if (index == 1)
                selected = &g_display_text_color;
            else if (index == 2)
                selected = &g_display_background_color;
            else if (index == 3)
                selected = &g_display_line_color;
            if (selected) {
                previous = *selected;
                if (color != ZDISPLAY_RGB_NONE)
                    *selected = color;
            }
            g_cpu.r[0] = previous;
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 13:
        if (is_display_interface(clsid)) { /* CreateDIBitmap(po, ppIDIB, depth, width, height) */
            uint32_t base = display_find_arg_base();
            uint32_t pp_idib = g_cpu.r[1];
            int width = (int)(g_cpu.r[3] & 0xFFFFu);
            int height = (int)(zmem_read32(base + 0) & 0xFFFFu);
            uint32_t dib = display_create_dib(width, height);
            if (pp_idib && pp_idib < ZDISPLAY_ADDR_MAX) zmem_write32(pp_idib, dib);
            LOGI("IDisplay_CreateDIBitmap: %dx%d -> 0x%08X", width, height, dib);
            g_cpu.r[0] = dib ? 0 : 8; /* SUCCESS / EUNSUPPORTED */
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 14:
        if (clsid == ZCLSID_HID_DEVICE) {
            g_hid_position_signal = g_cpu.r[1];
            LOGI("IHIDDevice_RegisterForPositionChange signal=0x%08X",
                 g_hid_position_signal);
            g_cpu.r[0] = 0;
            break;
        }
        if (is_display_interface(clsid)) { /* SetDestination(po, pIDIB) */
            uint32_t dest = g_cpu.r[1];
            uint32_t dest_clsid = dest ? zmem_read32(dest + 4) : 0;
            if (!dest) {
                g_display_dest_obj = 0; /* volta para o dispositivo (VRAM) */
            } else if (dest_clsid == ZCLSID_DEVICE_BITMAP) {
                g_display_dest_obj = dest;
            }
            LOGI("IDisplay_SetDestination: dest=0x%08X ativo=0x%08X", dest, g_display_dest_obj);
            g_cpu.r[0] = 0;
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 15:
        if (is_display_interface(clsid)) { /* GetDestination */
            g_cpu.r[0] = g_display_dest_obj ? g_display_dest_obj : g_device_bitmap;
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 16:
        if (is_display_interface(clsid)) { /* GetDeviceBitmap */
            if (!g_device_bitmap) {
                g_device_bitmap = make_stub_interface(ZCLSID_DEVICE_BITMAP);
                if (g_device_bitmap) {
                    zmem_write32(g_device_bitmap + 8, ZMEM_VRAM_BASE);
                    zmem_write16(g_device_bitmap + 20, 640);
                    zmem_write16(g_device_bitmap + 22, 480);
                    zmem_write16(g_device_bitmap + 24, 640 * 4);
                    zmem_write8(g_device_bitmap + 28, 32);
                    zmem_write8(g_device_bitmap + 29, 32);
                }
            }
            if (g_cpu.r[1]) zmem_write32(g_cpu.r[1], g_device_bitmap);
            LOGI("IDisplay_GetDeviceBitmap -> 0x%08X", g_device_bitmap);
            g_cpu.r[0] = g_device_bitmap ? 0 : 1;
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 18:
        if (is_display_interface(clsid)) { /* SetClipRect */
            uint32_t p_rect = g_cpu.r[1];
            if (p_rect && zmem_host_ptr(p_rect, 8)) {
                g_display_clip_x = (int16_t)zmem_read16(p_rect + 0);
                g_display_clip_y = (int16_t)zmem_read16(p_rect + 2);
                g_display_clip_w = (int16_t)zmem_read16(p_rect + 4);
                g_display_clip_h = (int16_t)zmem_read16(p_rect + 6);
            } else {
                g_display_clip_x = 0;
                g_display_clip_y = 0;
                g_display_clip_w = 640;
                g_display_clip_h = 480;
            }
            g_cpu.r[0] = 0;
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 19:
        if (is_display_interface(clsid)) { /* GetClipRect */
            uint32_t p_rect = g_cpu.r[1];
            if (p_rect && zmem_host_ptr(p_rect, 8)) {
                zmem_write16(p_rect + 0, (uint16_t)g_display_clip_x);
                zmem_write16(p_rect + 2, (uint16_t)g_display_clip_y);
                zmem_write16(p_rect + 4, (uint16_t)g_display_clip_w);
                zmem_write16(p_rect + 6, (uint16_t)g_display_clip_h);
                g_cpu.r[0] = 0;
            } else {
                g_cpu.r[0] = 14; /* EBADPARM */
            }
            break;
        }
        g_cpu.r[0] = 0;
        break;
    default:
        /* Qualquer outro metodo: retorna sucesso (0) */
        g_cpu.r[0] = 0;
        if (warn_count < 32) {
            LOGW("stub: clsid=0x%08X metodo=%u r0=0x%08X r1=0x%08X "
                 "r2=0x%08X r3=0x%08X sp0=0x%08X",
                 clsid, slot, obj, g_cpu.r[1], g_cpu.r[2], g_cpu.r[3],
                 zbrew_stack_arg(0));
            warn_count++;
        }
        break;
    }
}
