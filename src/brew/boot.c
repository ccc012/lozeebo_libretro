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
#include <stdio.h>
#include <assert.h>
#include "brew.h"
#include "aee_ids.h"
#include "imedia.h"
#include "iimage.h"
#include "../loader/bar_parser.h"
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
static uint32_t g_boot_clsid = 0;  /* CLSID original do zboot_start, imune a unserialize */
static uint32_t g_hid_button_signal = 0;
static uint32_t g_hid_position_signal = 0;
/* Fila de button-events (zeemu: device.button_events). Slot unico
 * perdia press+release / multi-botao no mesmo tick — Ovos rachava e
 * voltava ao ovo intacto. */
#define ZHID_EVT_QUEUE_MAX 16
static struct {
    uint32_t id;
    uint32_t uid;
    bool down;
} g_hid_evt_q[ZHID_EVT_QUEUE_MAX];
static int g_hid_evt_head = 0;
static int g_hid_evt_tail = 0;
static int g_hid_evt_count = 0;

static void hid_evt_push(uint32_t id, uint32_t uid, bool down) {
    if (g_hid_evt_count >= ZHID_EVT_QUEUE_MAX) return;
    g_hid_evt_q[g_hid_evt_tail].id = id;
    g_hid_evt_q[g_hid_evt_tail].uid = uid;
    g_hid_evt_q[g_hid_evt_tail].down = down;
    g_hid_evt_tail = (g_hid_evt_tail + 1) % ZHID_EVT_QUEUE_MAX;
    g_hid_evt_count++;
}

static bool hid_evt_pop(uint32_t *id, uint32_t *uid, bool *down) {
    if (g_hid_evt_count <= 0) return false;
    *id = g_hid_evt_q[g_hid_evt_head].id;
    *uid = g_hid_evt_q[g_hid_evt_head].uid;
    *down = g_hid_evt_q[g_hid_evt_head].down;
    g_hid_evt_head = (g_hid_evt_head + 1) % ZHID_EVT_QUEUE_MAX;
    g_hid_evt_count--;
    return true;
}
/* Posicao atual do stick analogico esquerdo (0..0xffff, centro 0x8000).
 * Os jogos leem via IHIDDevice_GetPositionState e sao avisados de mudanca
 * via o signal de RegisterForPositionChange. Espelhamos o D-pad (ver
 * zinput_left_stick_*) para que o menu do Family Pack possa ser navegado
 * com o direcional sem precisar de joystick analogico real. */
static uint32_t g_hid_axis_lx = 0x8000u;
static uint32_t g_hid_axis_ly = 0x8000u;
static uint32_t g_hid_axis_rx = 0x8000u;
static uint32_t g_hid_axis_ry = 0x8000u;
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
/* Alice/Foot pollam IHID_GetNextConnectEvent sem RegisterForConnectEvents.
 * FP/AH registram sinal e CreateDevice antes; so entregamos 1 evento
 * sintetico quando o sinal nunca foi registrado. */
static bool g_hid_connect_event_avail = true;
/* Watchdog: alguns jogos (Double Dragon confirmado por smoke test) entram
 * num loop degenerado dentro do callback de conexao HID e nunca retornam
 * (nunca disparam ZT_GUEST_RETURN) - sem isso a CPU queima o orcamento de
 * 1M instrucoes/frame para sempre, travando o framebuffer que ja avancava
 * bem so com timers. Se o callback nao voltar depois de um numero razoavel
 * de instrucoes, forca o estado de volta para RUNNING/halted. */
#define ZSIGNAL_CALL_INSTR_BUDGET (2u * 1000u * 1000u)
/* Slice cooperativa da thread guest (zeemu run_slices ~20k-50k). Sem
 * isso Alice/Foot Camp ficam em BOOT_SIGNAL_CALL ate o smoke TIMEOUT:
 * timers/signals nao drenam e IMedia_Play nunca chega. */
#define ZTHREAD_SLICE_INSTR ZBOOT_THREAD_SLICE_INSTR
static uint64_t g_signal_call_started_at = 0;
static zcpu_t g_signal_saved_cpu;
static bool g_signal_context_saved = false;
/* Thread guest (ex: loader de romset) roda em loop longo de CPU puro
 * (decompress CUNG): yield por slice, nao abortar pelo watchdog. */
static bool g_in_thread_guest = false;

uint32_t zboot_get_applet_object(void) {
    if (g_applet_obj)
        return g_applet_obj;
    if (g_applet_out)
        return zmem_read32(g_applet_out);
    return 0;
}

uint32_t zboot_get_applet_clsid(void) {
    return g_boot_clsid ? g_boot_clsid : g_applet_clsid;
}
static uint32_t g_shell_obj = 0;     /* objeto IShell real */
static uint32_t g_display_obj = 0;   /* objeto IDISPLAY (0x01001001) */
static uint32_t g_graphics_obj = 0;  /* objeto IGRAPHICS (0x01001002) */

/* ---- timers (IShell_SetTimer) ---- */
#define ZTIMER_MAX 16
typedef struct {
    bool     active;
    uint32_t expires_ms;
    uint32_t pfn;
    uint32_t puser;
} ztimer_t;
static ztimer_t g_timers[ZTIMER_MAX];
/* Thread guest minima (AEECLSID_THREAD_REAL): jogos criam uma thread com
 * IThread_Start(stack, pfn, user) e aguardam o corpo rodar; sem ele
 * congelam no boot. Zeemu aloca stack dedicada (nao reusa a do applet) e
 * trata Exit/Suspend/GetResumeCBK. Sem Exit->GUEST_RETURN o guest continua
 * apos ITHREAD_Exit e cai em SP=0 (ARMCC Data East: LR 0xF4E4/0x111F4). */
static uint32_t g_thread_obj = 0;
static uint32_t g_thread_fn = 0;
static uint32_t g_thread_user = 0;
static uint32_t g_thread_stack_size = 0;
static uint32_t g_thread_stack_base = 0; /* heap block; SP = base+size */
static bool g_thread_started = false;
static bool g_thread_alive = false;
static bool g_thread_ready = false; /* false apos Suspend; Resume seta true */
static bool g_thread_resume_pending = false; /* Resume antes do Suspend (zeemu queue) */
static unsigned g_thread_slice_credit = 1;   /* 1 slice / frame; 0 = pump applet */
static zcpu_t g_thread_saved_cpu;
static bool g_thread_context_saved = false;
static uint32_t g_thread_resume_cbk = 0; /* AEECallback: +16 pfn, +20 pNotifyData */

void zboot_thread_resume_callback(void) {
    uint32_t thread = g_cpu.r[0];
    if (thread && thread == g_thread_obj)
        g_thread_resume_pending = true;
    g_cpu.r[0] = 0;
}

/* ---- ISignal / ISignalCtl (AEESignal.h) ----
 * Layout do objeto (mesmo do zeemu BrewShell.cpp): +0 vtable,
 * +4 PFNNOTIFY, +8 pUser, +12 detached/disabled, +16 pendente.
 * ISignal_Set enfileira o callback; a fila e drenada por frame em
 * zboot_process_timers (um callback por vez, como os timers). */
#define ZSIGNAL_QUEUE_MAX 32
typedef struct {
    uint32_t signal_obj;
    uint32_t callback;
    uint32_t user;
    uint32_t arg1;
    uint32_t arg2;
    const char *label;
} zsignal_pending_t;
static uint32_t g_signal_vtbl = 0;
static zsignal_pending_t g_signal_queue[ZSIGNAL_QUEUE_MAX];
static int g_signal_queue_count = 0;

/* IShell_SendEvent ASYNC (zeemu BrewShell::queue_app_event): guest posts
 * evt+wp+dwp; drain chama applet HandleEvent real — nao sintetiza evt. */
#define ZAPP_EVT_QUEUE_MAX 16
#define ZEVT_FLAG_UNIQUE 0x0001u
#define ZEVT_FLAG_ASYNC  0x0002u
typedef struct {
    uint32_t event;
    uint32_t wparam;
    uint32_t dwp;
} zapp_evt_pending_t;
static zapp_evt_pending_t g_app_evt_queue[ZAPP_EVT_QUEUE_MAX];
static int g_app_evt_queue_count = 0;

bool zboot_timer_active(int i) {
    return i >= 0 && i < ZTIMER_MAX && g_timers[i].active;
}

void zboot_timer_set(int i, uint32_t ms, uint32_t pfn, uint32_t puser) {
    int n;
    int free_slot = -1;
    for (n = 0; n < ZTIMER_MAX; n++) {
        if (g_timers[n].active && g_timers[n].pfn == pfn &&
            g_timers[n].puser == puser) {
            i = n;
            break;
        }
        if (!g_timers[n].active && free_slot < 0) free_slot = n;
    }
    if (i == ZTIMER_MAX) i = free_slot; /* not found, use free slot */
    if (i < 0) i = free_slot;           /* i was -1, use free slot */
    if (zlog_trace_timer())
        LOGI("zboot_timer_set: i=%d free_slot=%d ms=%u pfn=0x%08X puser=0x%08X", i, free_slot, ms, pfn, puser);
    if (i >= 0 && i < ZTIMER_MAX) {
        g_timers[i].active = true;
        g_timers[i].expires_ms = zbrew_uptime_ms() + (ms < 16 ? 16 : ms);
        g_timers[i].pfn = pfn;
        g_timers[i].puser = puser;
        if (zlog_trace_timer())
            LOGI("zboot_timer_set: timer[%d] SET active=1 expires=%u", i, g_timers[i].expires_ms);
    } else {
        LOGW("zboot_timer_set: FALHOU - sem slot livre");
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

bool zboot_in_thread_guest(void) {
    return g_in_thread_guest;
}

void zboot_thread_slice_yield_if_needed(void) {
    if (g_state != BOOT_SIGNAL_CALL || !g_in_thread_guest) return;
    if (g_cpu.executed - g_signal_call_started_at <= ZTHREAD_SLICE_INSTR)
        return;
    {
        uint64_t executed = g_cpu.executed;
        g_thread_saved_cpu = g_cpu;
        g_thread_context_saved = true;
        g_in_thread_guest = false;
        g_thread_slice_credit = 0;
        if (g_signal_context_saved) {
            g_cpu = g_signal_saved_cpu;
            g_cpu.executed = executed;
            g_signal_context_saved = false;
        }
        g_state = BOOT_RUNNING;
        g_cpu.halted = true;
        if (getenv("ZEEMU_TRACE_THREADS"))
            LOGI("boot: thread slice yield PC=0x%08X after %llu instr",
                 g_thread_saved_cpu.r[REG_PC],
                 (unsigned long long)(executed - g_signal_call_started_at));
    }
}

int zboot_state(void) {
    return (int)g_state;
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
    /* Savestate: so head da fila (compat blob). Fila completa e
     * runtime-only — ok, input nao precisa sobreviver savestate. */
    if (g_hid_evt_count > 0) {
        blob.hid_event_id = g_hid_evt_q[g_hid_evt_head].id;
        blob.hid_event_uid = g_hid_evt_q[g_hid_evt_head].uid;
        blob.hid_event_down = g_hid_evt_q[g_hid_evt_head].down ? 1u : 0u;
        blob.hid_event_pending = 1u;
    } else {
        blob.hid_event_id = 0;
        blob.hid_event_uid = 0;
        blob.hid_event_down = 0;
        blob.hid_event_pending = 0;
    }
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
    /* Restaura CLSID apenas se o blob tem um válido (não-zero).
     * Isso evita sobrescrever o CLSID correto setado por zboot_start
     * com um valor 0 de savestate antigo (salvo antes do applet existir). */
    if (blob->applet_clsid)
        g_applet_clsid = blob->applet_clsid;
    g_hid_button_signal = blob->hid_button_signal;
    g_hid_position_signal = blob->hid_position_signal;
    g_hid_evt_head = g_hid_evt_tail = g_hid_evt_count = 0;
    if (blob->hid_event_pending)
        hid_evt_push(blob->hid_event_id, blob->hid_event_uid,
                     blob->hid_event_down != 0);
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
static uint32_t g_qegl_surface = 0;

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

/* Desenha a string e devolve a largura total em pixels. `char_w` e o
 * avanco horizontal por caractere (6px na fonte 5x7 de fallback; largo
 * da fonte real via nMaxCharWidth quando disponivel). */
static int display_draw_text(int x, int y, const char *s, uint32_t xrgb,
                             bool actually_draw, int char_w) {
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
        cx += char_w;
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
/* IDIB real: +4 = pPaletteMap (deve ser 0). NAO gravar CLSID ali —
 * Zuma faz ldr r3,[bmp,#4] e itera como ponteiro (crash 0x4449424D).
 * Detecta DIBM por vtbl stub + bits + depth 16/32. */
/* IDIB nDepth: Zuma trata como BYTES/pixel (2=RGB565, 4=XRGB).
 * Guest faz (nDepth>>1)==1 no upload pos-GetBitmap; bits=16 cai no
 * ramo A42C (eor pBmp, free) e smash heap. colorScheme@+29 = 16/0.
 * idisplay_real aloca vtbl propria com os mesmos traps — aceitar. */
static bool is_device_bitmap_obj(uint32_t obj) {
    uint32_t bits, vtbl, slot0;
    uint8_t depth;
    if (!obj || obj >= ZDISPLAY_ADDR_MAX)
        return false;
    vtbl = zmem_read32(obj);
    if (!vtbl || vtbl >= ZDISPLAY_ADDR_MAX)
        return false;
    slot0 = zmem_read32(vtbl);
    if (slot0 != ZTRAP_ADDR(ZT_STUB_BASE))
        return false;
    bits = zmem_read32(obj + 8);
    depth = zmem_read8(obj + 28);
    return bits != 0 &&
           (bits < ZDISPLAY_ADDR_MAX ||
            (bits >= ZMEM_VRAM_BASE &&
             bits < ZMEM_VRAM_BASE + ZMEM_VRAM_SIZE)) &&
           (depth == 2 || depth == 4 || depth == 16 || depth == 32);
}

static int idib_depth_bytes(uint32_t obj) {
    uint8_t d = zmem_read8(obj + 28);
    if (d == 2 || d == 16) return 2; /* bytes ou bits RGB565 */
    if (d == 4 || d == 32) return 4; /* bytes ou bits XRGB */
    return 4;
}

static uint32_t make_stub_interface(uint32_t clsid) {
    if (clsid == 0x01001014u) {
        static const uint32_t vtbl[] = {
            ZT_FILE_ADDREF, ZT_FILE_ADDREF, ZT_FILE_READABLE,
            ZT_UNZIP_READ, ZT_FILE_CANCEL, ZT_UNZIP_SETSTREAM
        };
        return zbrew_make_interface(vtbl, 6, 1);
    }
    uint32_t obj = zheap_alloc(64);
    if (!g_stub_vtbl) {
        int i;
        g_stub_vtbl = zheap_alloc(128 * 4);
        if (!g_stub_vtbl) return 0;
        LOGI("stub vtable alocada em 0x%08X", g_stub_vtbl);
        for (i = 0; i < 128; i++)
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
    /* DIBM/IDIB: +4 = pPaletteMap = 0 (zeemu BrewBitmap). Demais stubs:
     * CLSID em +4 pra dispatch. */
    if (clsid != ZCLSID_DEVICE_BITMAP)
        zmem_write32(obj + 4, clsid);
    return obj;
}

/* Singleton: IDisplay e DISPLAY1/QEGL compartilham o mesmo IDIB (pBmp=VRAM). */
uint32_t zbrew_device_bitmap(void) {
    if (!g_device_bitmap) {
        g_device_bitmap = make_stub_interface(ZCLSID_DEVICE_BITMAP);
        if (g_device_bitmap) {
            zmem_write32(g_device_bitmap + 8, ZMEM_VRAM_BASE);
            zmem_write16(g_device_bitmap + 20, 640);
            zmem_write16(g_device_bitmap + 22, 480);
            /* Zeemu device = RGB565: nDepth=16 scheme=16.
             * Zuma 0x1ABA0 rejeita scheme 0 pos-InitGl. */
            zmem_write16(g_device_bitmap + 24, 640 * 2);
            zmem_write8(g_device_bitmap + 28, 16);
            zmem_write8(g_device_bitmap + 29, 16);
        }
    }
    return g_device_bitmap;
}

/* Cria um bitmap offscreen (IDIB) real: objeto no layout ZCLSID_DEVICE_BITMAP
 * (mesmo usado por GetDeviceBitmap) com pixels alocados no heap.
 * depth=16 -> RGB565; depth=32 -> XRGB8888. */
uint32_t zbrew_create_device_bitmap(int width, int height, int depth) {
    uint32_t obj, bits, pitch, bpp;
    uint8_t idib_depth, scheme;
    if (width <= 0 || height <= 0 || width > 2048 || height > 2048)
        return 0;
    /* Assimetrico (Zuma):
     * - RGB565 soft (imgdec): +28 = BYTES (2) — upload faz nDepth>>1.
     * - Device GetDeviceBitmap: BITS 16 + scheme 16 (idisplay_real / boot).
     * - XRGB offscreen: +28 = BITS (32). */
    if (depth == 2 || depth == 16) {
        bpp = 2; idib_depth = 2; scheme = 16;
    } else {
        bpp = 4; idib_depth = 32; scheme = 0;
    }
    obj = make_stub_interface(ZCLSID_DEVICE_BITMAP);
    if (!obj) return 0;
    pitch = (uint32_t)width * bpp;
    bits = zheap_alloc(pitch * (uint32_t)height);
    if (!bits) return 0;
    zmem_write32(obj + 8, bits);
    zmem_write16(obj + 20, (uint16_t)width);
    zmem_write16(obj + 22, (uint16_t)height);
    zmem_write16(obj + 24, (uint16_t)pitch);
    zmem_write8(obj + 28, idib_depth);
    zmem_write8(obj + 29, scheme);
    return obj;
}

static uint32_t display_create_dib(int width, int height) {
    return zbrew_create_device_bitmap(width, height, 32);
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
    g_cpu.r[4] = r0;
    g_cpu.r[REG_LR] = ZTRAP_ADDR(ZT_GUEST_RETURN);
    g_cpu.halted = false;
    static uint32_t gc_log = 0;
    if (gc_log++ < 128)
        LOGI("guest_call: func=0x%08X r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X", func, r0, r1, r2, r3);
    zcpu_bx(func | (func & 1));
}

/* ---- ISignal: objeto, disparo e fila ---- */

/* Cria o objeto de sinal no layout do zeemu (0x18 bytes). A vtable e
 * compartilhada e criada sob demanda (o heap e recriado a cada boot). */
static uint32_t make_signal_object(uint32_t callback, uint32_t user) {
    uint32_t obj;
    int i;
    if (!g_signal_vtbl) {
        g_signal_vtbl = zheap_alloc(8 * 4);
        if (!g_signal_vtbl) return 0;
        for (i = 0; i < 8; i++)
            zmem_write32(g_signal_vtbl + (uint32_t)i * 4,
                         ZTRAP_ADDR(ZT_SIGNAL_BASE + (uint32_t)i));
        LOGI("signal vtable alocada em 0x%08X", g_signal_vtbl);
    }
    obj = zheap_alloc(0x1c);
    if (!obj) return 0;
    memset((void *)zmem_host_ptr(obj, 0x1c), 0, 0x1c);
    zmem_write32(obj + 0, g_signal_vtbl);
    zmem_write32(obj + 4, callback);
    zmem_write32(obj + 8, user);
    zmem_write32(obj + 12, 0); /* nao detached */
    zmem_write32(obj + 16, 0); /* sem callback pendente */
    /* CreateSignal entrega ISignal e ISignalCtl, cada interface com uma ref. */
    zmem_write32(obj + 20, 2);
    return obj;
}

static bool zboot_signal_valid(uint32_t obj) {
    return obj && obj < ZDISPLAY_ADDR_MAX && g_signal_vtbl &&
           zmem_read32(obj) == g_signal_vtbl && zmem_read32(obj + 20) != 0;
}

static uint32_t zboot_signal_addref(uint32_t obj) {
    uint32_t refs;
    if (!zboot_signal_valid(obj)) return 0;
    refs = zmem_read32(obj + 20) + 1;
    zmem_write32(obj + 20, refs);
    return refs;
}

static uint32_t zboot_signal_release(uint32_t obj) {
    uint32_t refs;
    if (!zboot_signal_valid(obj)) return 0;
    refs = zmem_read32(obj + 20) - 1;
    zmem_write32(obj + 20, refs);
    if (!refs) {
        zmem_write32(obj + 4, 0);
        zmem_write32(obj + 8, 0);
        zmem_write32(obj + 12, 1);
        zmem_write32(obj + 16, 0);
    }
    return refs;
}

static void zboot_signal_register(uint32_t *registered, uint32_t signal_obj) {
    uint32_t old = *registered;
    if (old == signal_obj) return;
    if (signal_obj && !zboot_signal_addref(signal_obj)) signal_obj = 0;
    *registered = signal_obj;
    if (old) zboot_signal_release(old);
}

/* Enfileira um callback de sinal (nao chama o guest diretamente). */
static void zboot_signal_queue_object(uint32_t signal_obj, uint32_t callback,
                                      uint32_t user, uint32_t arg1,
                                      uint32_t arg2, const char *label) {
    if (!callback || callback >= ZDISPLAY_ADDR_MAX) return;
    if (g_signal_queue_count >= ZSIGNAL_QUEUE_MAX) {
        LOGW("boot: fila de sinais cheia (%d) - callback 0x%08X descartado",
             g_signal_queue_count, callback);
        return;
    }
    if (signal_obj && !zboot_signal_addref(signal_obj)) return;
    g_signal_queue[g_signal_queue_count].signal_obj = signal_obj;
    g_signal_queue[g_signal_queue_count].callback = callback;
    g_signal_queue[g_signal_queue_count].user = user;
    g_signal_queue[g_signal_queue_count].arg1 = arg1;
    g_signal_queue[g_signal_queue_count].arg2 = arg2;
    g_signal_queue[g_signal_queue_count].label = label;
    g_signal_queue_count++;
}

/* API publica para outras camadas do HLE (IMedia, etc.) agendarem um
 * callback no guest sem chamar reentrantemente. */
void zbrew_queue_signal_callback(uint32_t callback, uint32_t user,
                                 uint32_t arg1, uint32_t arg2) {
    zboot_signal_queue_object(0, callback, user, arg1, arg2, "async");
}

/* ISignal_Set: se o sinal esta detached, ignora; se nao ha callback
 * valido, marca pendente (ISignalCtl_Enable re-dispara depois); senao
 * enfileira a notificacao PFNNOTIFY(pUser). */
static void zboot_signal_fire(uint32_t signal_obj, const char *label) {
    uint32_t callback, user;
    if (!zboot_signal_valid(signal_obj)) return;
    if (zmem_read32(signal_obj + 12) != 0) {
        if (getenv("ZEEMU_TRACE_HID"))
            LOGI("signal: %s ignorado (detached) obj=0x%08X", label, signal_obj);
        return;
    }
    callback = zmem_read32(signal_obj + 4);
    user = zmem_read32(signal_obj + 8);
    if (!callback || callback >= ZDISPLAY_ADDR_MAX) {
        zmem_write32(signal_obj + 16, 1);
        if (getenv("ZEEMU_TRACE_HID"))
            LOGI("signal: %s pendente obj=0x%08X (callback=0x%08X)",
                 label, signal_obj, callback);
        return;
    }
    zmem_write32(signal_obj + 16, 0);
    zboot_signal_queue_object(signal_obj, callback, user, 0, 0, label);
}

/* Drain um SendEvent ASYNC: HandleEvent(applet, evt, wp, dwp). */
static bool zboot_app_evt_drain_one(void) {
    zapp_evt_pending_t e;
    int i;
    uint32_t applet, vtbl, handle_evt;
    if (g_app_evt_queue_count <= 0) return false;
    applet = zboot_get_applet_object();
    if (!applet) return false;
    vtbl = zmem_read32(applet);
    handle_evt = vtbl ? zmem_read32(vtbl + 8) : 0;
    if (!handle_evt) return false;
    e = g_app_evt_queue[0];
    for (i = 1; i < g_app_evt_queue_count; i++)
        g_app_evt_queue[i - 1] = g_app_evt_queue[i];
    g_app_evt_queue_count--;
    LOGI("boot: SendEvent drain evt=0x%08X wp=0x%04X dwp=0x%08X",
         e.event, e.wparam & 0xFFFFu, e.dwp);
    g_state = BOOT_SIGNAL_CALL;
    g_signal_call_started_at = g_cpu.executed;
    g_signal_saved_cpu = g_cpu;
    g_signal_context_saved = true;
    g_cpu.r[REG_SP] = ZMEM_STACK_TOP;
    guest_call(handle_evt, applet, e.event, e.wparam, e.dwp);
    return true;
}

/* Tira um callback da fila e executa como guest call. Retorna true se
 * iniciou uma chamada (a maquina de estados espera ZT_GUEST_RETURN). */
static bool zboot_signal_drain_one(void) {
    zsignal_pending_t p;
    int i;
    if (g_signal_queue_count <= 0) return false;
    p = g_signal_queue[0];
    for (i = 1; i < g_signal_queue_count; i++)
        g_signal_queue[i - 1] = g_signal_queue[i];
    g_signal_queue_count--;
    if (p.signal_obj) {
        bool live = zboot_signal_valid(p.signal_obj) &&
                    zmem_read32(p.signal_obj + 12) == 0 &&
                    zmem_read32(p.signal_obj + 4) == p.callback &&
                    zmem_read32(p.signal_obj + 8) == p.user;
        zboot_signal_release(p.signal_obj);
        if (!live) {
            LOGI("boot: sinal (%s) cancelado antes do callback",
                 p.label ? p.label : "?");
            return false;
        }
    }
    LOGI("boot: sinal (%s) callback=0x%08X user=0x%08X",
         p.label ? p.label : "?", p.callback, p.user);
    g_state = BOOT_SIGNAL_CALL;
    g_signal_call_started_at = g_cpu.executed;
    g_signal_saved_cpu = g_cpu;
    g_signal_context_saved = true;
    guest_call(p.callback, p.user, p.arg1, p.arg2, 0);
    return true;
}

static void zboot_signal_lifetime_selftest(void) {
    uint32_t signal_obj = make_signal_object(0x1000u, 0x2000u);
    uint32_t registered = 0;
    assert(signal_obj && zmem_read32(signal_obj + 20) == 2);
    zboot_signal_register(&registered, signal_obj); /* attach */
    assert(zmem_read32(signal_obj + 20) == 3);
    assert(zboot_signal_release(signal_obj) == 2);  /* ISignal */
    assert(zboot_signal_release(signal_obj) == 1);  /* ISignalCtl */
    zboot_signal_fire(signal_obj, "selftest");       /* queued ref */
    assert(g_signal_queue_count == 1 && zmem_read32(signal_obj + 20) == 2);
    zmem_write32(signal_obj + 12, 1);                /* Detach before fire */
    assert(!zboot_signal_drain_one());
    assert(zmem_read32(signal_obj + 20) == 1);       /* registration only */
    zboot_signal_register(&registered, 0);           /* owner Release */
    assert(!zboot_signal_valid(signal_obj));
    LOGI("signal lifetime selftest: attach/detach/fire/release OK");
}

void zboot_start(uint32_t entry, uint32_t applet_clsid) {
    memset(g_timers, 0, sizeof(g_timers));
    g_thread_obj = 0;
    g_thread_fn = 0;
    g_thread_user = 0;
    g_thread_stack_size = 0;
    g_thread_stack_base = 0;
    g_thread_started = false;
    g_thread_alive = false;
    g_thread_ready = false;
    g_thread_resume_pending = false;
    g_thread_slice_credit = 1;
    g_thread_context_saved = false;
    g_thread_resume_cbk = 0;
    g_in_thread_guest = false;
    g_stub_vtbl = 0;
    g_signal_vtbl = 0;
    g_signal_queue_count = 0;
    g_app_evt_queue_count = 0;
    g_signal_context_saved = false;
    if (getenv("ZEEMU_SIGNAL_SELFTEST"))
        zboot_signal_lifetime_selftest();
    g_device_bitmap = 0;
    g_display_obj = 0;
    g_qegl_surface = 0;
    g_display_text_color = 0xFFFFFF00u;
    g_display_background_color = 0x00000000u;
    g_display_line_color = 0xFFFFFF00u;
    g_display_clip_x = 0;
    g_display_clip_y = 0;
    g_display_clip_w = 640;
    g_display_clip_h = 480;
    g_hid_button_signal = 0;
    g_hid_position_signal = 0;
    g_hid_evt_head = g_hid_evt_tail = g_hid_evt_count = 0;
    g_hid_connect_signal = 0;
    g_hid_connect_pending = false;
    g_hid_connect_event_avail = true;
    g_hid_axis_lx = 0x8000u;
    g_hid_axis_ly = 0x8000u;
    g_hid_axis_rx = 0x8000u;
    g_hid_axis_ry = 0x8000u;
    memset(g_shell_prefs, 0, sizeof(g_shell_prefs));
    memset(g_shell_events, 0, sizeof(g_shell_events));
    if (applet_clsid) {
        g_applet_clsid = applet_clsid;
        g_boot_clsid = applet_clsid;
    }

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

/* --- IApplet stub funcional --- */
static void applet_addref_stub(uint32_t addr) {
    (void)addr;
    uint32_t obj = g_cpu.r[0];
    uint32_t ref = zmem_read32(obj + 4);
    zmem_write32(obj + 4, ref + 1);
    g_cpu.r[0] = 1;
}

static void applet_release_stub(uint32_t addr) {
    (void)addr;
    uint32_t obj = g_cpu.r[0];
    uint32_t ref = zmem_read32(obj + 4);
    if (ref <= 1) {
        g_cpu.r[0] = 1;
    } else {
        zmem_write32(obj + 4, ref - 1);
        g_cpu.r[0] = 0;
    }
}

static void applet_queryinterface_stub(uint32_t addr) {
    (void)addr;
    /* Stub: nao suporta interfaces adicionais */
    g_cpu.r[0] = 0x80004002u; /* E_NOINTERFACE */
}

static void applet_handle_event_stub(uint32_t addr) {
    (void)addr;
    uint32_t obj = g_cpu.r[0];
    uint32_t evt = g_cpu.r[1];
    uint32_t wparam = g_cpu.r[2];
    uint32_t lparam = g_cpu.r[3];
    uint32_t dwParam = g_cpu.r[4]; /* 5o arg na pilha */

    /* EVT_APP_START = 0, EVT_KEY = 1, EVT_TIMER = 2, EVT_COMMAND = 3 */
    LOGI("Applet::HandleEvent obj=0x%08X evt=0x%08X wparam=0x%08X lparam=0x%08X",
         obj, evt, wparam, lparam);

    if (evt == 0) { /* EVT_APP_START */
        /* Registra timer para game loop (~60 FPS = 16ms) */
        if (g_applet_obj) {
            uint32_t shell = g_shell_obj;
            if (shell) {
                uint32_t vtbl = zmem_read32(shell);
                uint32_t set_timer = zmem_read32(vtbl + 4 * 11); /* SetTimer slot 11 */
                if (set_timer) {
                    guest_call(set_timer, shell, 16, g_applet_obj, 0);
                }
            }
            /* Registra para receber eventos de tecla */
            if (g_applet_obj && g_shell_obj) {
                uint32_t shell = g_shell_obj;
                uint32_t vtbl = zmem_read32(shell);
                uint32_t reg_notify = zmem_read32(vtbl + 4 * 8); /* RegisterNotify slot 8 */
                if (reg_notify) {
                    guest_call(reg_notify, shell, 0, g_applet_obj, 0);
                }
            }
        }
        /* EVT_KEY = 1, EVT_TIMER = 2 - repassa para o jogo */
        g_cpu.r[0] = 0; /* AEE_SUCCESS */
        return;
    }

    g_cpu.r[0] = 0x80004001u; /* E_NOTIMPL */
}

/* Tabela de funcoes IApplet (slots 0-63) */
static void build_applet_vtable(uint32_t obj) {
    uint32_t vtbl = zheap_alloc(64 * 4);
    if (!vtbl) return;
    for (int i = 0; i < 64; i++)
        zmem_write32(vtbl + i * 4, ZTRAP_ADDR(ZT_STUB_BASE));

    /* Override slots conhecidos: 0=AddRef, 1=Release, 2=QueryInterface, 8=HandleEvent */
    zmem_write32(vtbl + 0 * 4, ZTRAP_ADDR(ZT_STUB_BASE + 0x1000)); /* AddRef stub */
    zmem_write32(vtbl + 1 * 4, ZTRAP_ADDR(ZT_STUB_BASE + 0x1004)); /* Release stub */
    zmem_write32(vtbl + 2 * 4, ZTRAP_ADDR(ZT_STUB_BASE + 0x1008)); /* QueryInterface stub */
    zmem_write32(vtbl + 8 * 4, ZTRAP_ADDR(ZT_STUB_BASE + 0x1020)); /* HandleEvent stub */

    /* Instala handlers customizados nos traps */
    static bool installed = false;
    if (!installed) {
        installed = true;
        /* Registra os handlers customizados na tabela de traps personalizada */
        /* Usamos offsets 0x1000+ para nossos handlers customizados */
        zcpu_set_custom_trap(ZT_STUB_BASE + 0x1000, applet_addref_stub);
        zcpu_set_custom_trap(ZT_STUB_BASE + 0x1004, applet_release_stub);
        zcpu_set_custom_trap(ZT_STUB_BASE + 0x1008, applet_queryinterface_stub);
        zcpu_set_custom_trap(ZT_STUB_BASE + 0x1020, applet_handle_event_stub);
    }

    obj = g_applet_obj;
    zmem_write32(obj, vtbl);
    zmem_write32(obj + 4, 1); /* refcount = 1 */
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
        LOGI("boot: IModule_CreateInstance(clsid=0x%08X, boot_clsid=0x%08X) via 0x%08X",
             g_applet_clsid, g_boot_clsid, create);
        g_state = BOOT_CREATE_APPLET;
        guest_call(create, g_module_obj, g_shell_obj,
                   g_boot_clsid, g_applet_out);
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
        /* Loga todos os campos do applet para entender seu layout */
        {
            int f;
            LOGI("boot: applet dump (obj=0x%08X):", g_applet_obj);
            for (f = 0; f < 0x80; f += 4) {
                uint32_t v = zmem_read32(g_applet_obj + f);
                if (v != 0)
                    LOGI("boot:   applet+0x%02X = 0x%08X", f, v);
            }
        }
        /* RE4: Bio4_InitApp1 (0x44B8) preenche +0x40 (0x2F49C/size 0x14) e
         * +0x3C (0x54E60). NAO sobrescrever — injecao RE4H anterior clobberava
         * o objeto real e matava o draw path. So restaura +0x6C (lixo stack).
         * Se InitApp falhar e +0x3C==0, ai sim injeta helper minimo. */
        if (g_applet_clsid == 0x0108AF6Cu) {
            uint32_t o3c = zmem_read32(g_applet_obj + 0x3C);
            uint32_t o40 = zmem_read32(g_applet_obj + 0x40);
            if (!o3c) {
                uint32_t vtbl = zheap_alloc(16 * 4u);
                uint32_t helper = zheap_alloc(0x40);
                int hi;
                if (vtbl && helper) {
                    memset((void *)zmem_host_ptr(helper, 0x40), 0, 0x40);
                    for (hi = 0; hi < 16; ++hi)
                        zmem_write32(vtbl + (uint32_t)hi * 4u,
                                     ZTRAP_ADDR(ZT_STUB_BASE + (uint32_t)hi) - vtbl);
                    zmem_write32(helper, vtbl);
                    zmem_write32(helper + 4, 0x52453448u);
                    zmem_write32(g_applet_obj + 0x3C, helper);
                    LOGI("boot: applet+0x3C fallback RE4H 0x%08X", helper);
                }
            } else {
                LOGI("boot: RE4 keep +0x3C=0x%08X +0x40=0x%08X", o3c, o40);
            }
        }
        if (g_graphics_obj && g_applet_clsid != 0x0108AF6Cu) {
            LOGI("boot: applet+0x6C deixado sem injecao (clsid=0x%08X)", g_applet_clsid);
        }
        uint32_t vtbl = zmem_read32(g_applet_obj);
        uint32_t handle_evt = zmem_read32(vtbl + 8);
        LOGI("boot: HandleEvent(EVT_APP_START) via 0x%08X", handle_evt);
        g_state = BOOT_APP_START;
        /* CreateInstance deixa SP residual (GRAPHICS Init etc). Sem reset,
         * frames grandes do EVT_APP_START (ex: RE4 sub sp,#0x16c) estouram
         * o topo e disparam STACK OVERFLOW — mesma politica dos timers. */
        g_cpu.r[REG_SP] = ZMEM_STACK_TOP;
        /* O SDK usado pelo Family Pack codifica EVT_APP_START como 0. */
        guest_call(handle_evt, g_applet_obj, 0, 0, 0);
        return;
    }

    case BOOT_APP_START:
        LOGI("boot: EVT_APP_START tratado (ret=0x%08X) - jogo RODANDO", result);
        /* Pac-Mania: o literal global em 0xA3F4 (carregado por
         * ldr r6,[pc,#0x124] em 0xA2C8) vem 0 no arquivo; a tabela de
         * fixups do .mod (na cauda do arquivo) lista 0xA3F4, mas o
         * scatter HLE nao aplica relocacoes. GMAIN (0xA2D4) entao faz
         * r0=[[g-4]] com g=0 e chama vt[0xB0]=0 -> blx 0. Experimental:
         * aponta o literal para um pseudo-objeto cuja vtable stub em
         * [vt+0xB0] e um trap que retorna 0 sem crash. */
        if (zmem_read32(0xA3F4u) == 0 && g_stub_vtbl) {
            uint32_t objp = zheap_alloc(8);
            if (objp) {
                zmem_write32(objp, g_stub_vtbl);
                zmem_write32(0xA3F4u, objp + 4);
                LOGI("boot: PATCH literal 0xA3F4 = 0x%08X (obj+4, vt=0x%08X)",
                     objp + 4, g_stub_vtbl);
            }
        }
        /* Fake game-loop: HandleEvent(evt=2). Em BREW AEEEvent, 2 =
         * EVT_APP_SUSPEND — Family Pack (0x010903C6) suspende o applet.
         * FP ja registra IShell_SetTimer. Data East (IThread_Start no
         * APP_START): evt=2 = "suspend enter" + crash — so thread.
         * Zuma (0x0109AF55): ja tem IShell_SetTimer(0x4814); o fake
         * HandleEvent cancela esse timer e deixa so clears pretos.
         * Ovos (0x010963A5): SetTimer(0x1870) no CreateInstance; evt=2
         * cancela HID+timer e mata o draw GL.
          * Double Dragon (0x0102F789): SetTimer(33ms, 0x239DC) no APP_START;
          * fake evt=2 cancela timer+HID e entra em retry GRAPHICS CreateInstance. */
        if (g_applet_obj && g_shell_obj && g_applet_clsid != 0x010903C6u &&
            g_applet_clsid != 0x0108AF6Cu && g_applet_clsid != 0x0109AF55u &&
            g_applet_clsid != 0x010963A5u &&
            g_applet_clsid != 0x0102F789u &&
            !g_thread_fn) {
            uint32_t applet_vtbl = zmem_read32(g_applet_obj);
            uint32_t handle_event = zmem_read32(applet_vtbl + 8);
            if (handle_event) {
                /* Registra timer diretamente (evita guest_call dentro de trap handler) */
                zboot_timer_set(-1, 16, handle_event, g_applet_obj);
                LOGI("boot: timer de game loop (16ms) registrado para applet=0x%08X cb=0x%08X", g_applet_obj, handle_event);
            }
            /* Registra para receber eventos de tecla via RegisterNotify */
            uint32_t shell = g_shell_obj;
            uint32_t vtbl = zmem_read32(shell);
            uint32_t reg_notify = zmem_read32(vtbl + 4 * 8); /* RegisterNotify slot 8 */
            if (reg_notify) {
                guest_call(reg_notify, shell, 0, g_applet_obj, 0);
            }
        }
        g_state = BOOT_RUNNING;
        g_cpu.halted = true; /* espera timers/eventos */
        return;

    case BOOT_TIMER_CALL:
    case BOOT_SIGNAL_CALL:
        if (g_in_thread_guest) {
            /* Suspend: contexto da thread ja em g_thread_saved_cpu; so
             * restaura o applet. Exit: marca morta. */
            g_in_thread_guest = false;
            if (g_signal_context_saved) {
                uint64_t executed = g_cpu.executed;
                g_cpu = g_signal_saved_cpu;
                g_cpu.executed = executed;
                g_signal_context_saved = false;
            }
            if (!g_thread_alive)
                g_thread_fn = 0; /* Exit: nao reinicia */
            g_state = BOOT_RUNNING;
            g_cpu.halted = true;
            return;
        }
        g_in_thread_guest = false;
        if (g_state == BOOT_SIGNAL_CALL && g_signal_context_saved) {
            uint64_t executed = g_cpu.executed;
            g_cpu = g_signal_saved_cpu;
            g_cpu.executed = executed;
            g_signal_context_saved = false;
        }
        g_state = BOOT_RUNNING;
        g_cpu.halted = true;
        /* RE4/Quake/NFS: re-registra fake loop. FP/DE/Zuma/Ovos/DD: nao. */
        if (g_applet_obj && g_state == BOOT_RUNNING &&
            g_applet_clsid != 0x010903C6u && g_applet_clsid != 0x0108AF6Cu &&
            g_applet_clsid != 0x0109AF55u && g_applet_clsid != 0x010963A5u &&
            g_applet_clsid != 0x0102F789u &&
            !g_thread_fn) {
            uint32_t applet_vtbl = zmem_read32(g_applet_obj);
            uint32_t handle_event = zmem_read32(applet_vtbl + 8);
            uint32_t real_handler = zmem_read32(g_applet_obj + 0x18);
            uint32_t gfx_handler = zmem_read32(g_applet_obj + 0x6C);
            LOGI("boot: applet=0x%08X vtbl=0x%08X HandleEvent=0x%08X obj+0x18=0x%08X obj+0x6C=0x%08X",
                 g_applet_obj, applet_vtbl, handle_event, real_handler, gfx_handler);
            if (handle_event) {
                int i;
                for (i = 0; i < ZTIMER_MAX; i++) {
                    if (g_timers[i].active && g_timers[i].pfn == handle_event &&
                        g_timers[i].puser == g_applet_obj) {
                        g_timers[i].expires_ms = zbrew_uptime_ms() + 16;
                        break;
                    }
                }
                if (i == ZTIMER_MAX) {
                    zboot_timer_set(-1, 16, handle_event, g_applet_obj);
                    LOGI("boot: timer de game loop re-registrado (applet=0x%08X)", g_applet_obj);
                }
            }
        }
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
    if (zlog_trace_timer()) { /* probe 1x/60 — so sob pedido */
        static unsigned tprobe = 0;
        if ((tprobe++ % 60) == 0) {
            int act = 0;
            for (i = 0; i < ZTIMER_MAX; i++)
                if (g_timers[i].active) act++;
            LOGI("TIMERPROBE: state=%s now=%u halted=%d active=%d first.exp=%u first.pfn=0x%08X PC=0x%08X exec=%llu",
                 zboot_state_name(), now, g_cpu.halted ? 1 : 0, act,
                 act ? g_timers[0].expires_ms : 0,
                 act ? g_timers[0].pfn : 0, g_cpu.r[15],
                 (unsigned long long)g_cpu.executed);
        }
    }
    if (zlog_trace_timer()) {
        static int trace_n = 0;
        int active = 0;
        for (i = 0; i < ZTIMER_MAX; i++)
            if (g_timers[i].active) active++;
        if (trace_n < 30) { trace_n++;
            LOGI("TIMERTRACE: now=%u state=%s halted=%d timers=%d/%d",
                 now, zboot_state_name(), g_cpu.halted ? 1 : 0, active, ZTIMER_MAX);
        }
    }

    zboot_thread_slice_yield_if_needed();

    /* Watchdog do callback de sinal (ver comentario em g_signal_call_started_at):
     * roda mesmo com o jogo tecnicamente "parado" nesse estado, porque a CPU
     * pode estar halted=false rodando o callback ha varios frames. */
    if (g_state == BOOT_SIGNAL_CALL && !g_in_thread_guest &&
        g_cpu.executed - g_signal_call_started_at > ZSIGNAL_CALL_INSTR_BUDGET) {
        LOGW("boot: callback de sinal nao retornou apos %llu instrucoes - "
             "forcando volta para RUNNING (provavel loop degenerado no guest)",
             (unsigned long long)(g_cpu.executed - g_signal_call_started_at));
        if (g_signal_context_saved) {
            uint64_t executed = g_cpu.executed;
            g_cpu = g_signal_saved_cpu;
            g_cpu.executed = executed;
            g_signal_context_saved = false;
        }
        g_state = BOOT_RUNNING;
        g_cpu.halted = true;
    }

    /* Sinal de conexao de HID (IHID_RegisterForConnectEvents, case 6):
     * dispara uma unica vez como um timer de disparo imediato. Sem isso
     * jogos que esperam esse callback antes de iniciar o loop de jogo
     * (ex: Family Pack, Super BurgerTime) ficam parados para sempre.
     * Atrasamos alguns frames para dar tempo ao jogo de entrar em
     * EVT_APP_SUSPEND antes de receber o sinal (o Supbtime exige). */
    if (g_state != BOOT_RUNNING)
        return;

    if (g_hid_connect_pending && g_hid_connect_signal) {
        uint32_t cb = zmem_read32(g_hid_connect_signal + 4);
        uint32_t user = zmem_read32(g_hid_connect_signal + 8);
        LOGI("boot: sinal de conexao HID disparado, callback=0x%08X user=0x%08X",
             cb, user);
        g_hid_connect_pending = false;
        zboot_signal_fire(g_hid_connect_signal, "hid-connect");
        if (zboot_signal_drain_one())
            return;
    }

    if (!g_thread_started && g_thread_fn && g_thread_ready &&
        !getenv("ZEEMU_STUB_THREAD")) {
        uint32_t sp = ZMEM_STACK_TOP;
        g_thread_started = true;
        g_thread_alive = true;
        g_in_thread_guest = true;
        g_state = BOOT_SIGNAL_CALL;
        g_signal_call_started_at = g_cpu.executed;
        g_signal_saved_cpu = g_cpu;
        g_signal_context_saved = true;
        if (g_thread_stack_size) {
            if (!g_thread_stack_base)
                g_thread_stack_base = zheap_alloc(g_thread_stack_size + 16u);
            if (g_thread_stack_base)
                sp = (g_thread_stack_base + g_thread_stack_size) & ~7u;
        }
        LOGI("boot: thread guest iniciada pfn=0x%08X user=0x%08X sp=0x%08X "
             "stack=0x%08X",
             g_thread_fn, g_thread_user, sp, g_thread_stack_size);
        g_cpu.r[REG_SP] = sp;
        guest_call(g_thread_fn, g_thread_obj, g_thread_user, 0, 0);
        return;
    }

    /* Resume enfileirado antes do Suspend (zeemu collect_resumes). */
    if (g_thread_resume_pending) {
        g_thread_ready = true;
        g_thread_resume_pending = false;
    }

    /* App events (SendEvent ASYNC) / sinais / timers ANTES do resume:
     * DE Resume+Suspend starve o applet (zeemu drain_app_events). */
    if (zboot_app_evt_drain_one())
        return;
    if (zboot_signal_drain_one())
        return;

    for (i = 0; i < ZTIMER_MAX; i++) {
        if (!g_timers[i].active) {
            if (zlog_trace_timer() && (i == 0))
                LOGI("zboot_process_timers: timer[0] inactive now=%u", now);
            continue;
        }

        if (now >= g_timers[i].expires_ms) {
            if (zlog_trace_timer())
                LOGI("boot: timer %d expirou, callback=0x%08X user=0x%08X now=%u expires=%u",
                     i, g_timers[i].pfn, g_timers[i].puser, now, g_timers[i].expires_ms);
            g_state = BOOT_TIMER_CALL;
            g_timers[i].active = false;
            g_cpu.r[REG_SP] = ZMEM_STACK_TOP;
            /* RE4: evt=1 (draw). DE fake-HandleEvent: evt=1 (2=SUSPEND).
             * FP/outros: evt=2. */
            {
                uint32_t evt = 2u;
                if (g_applet_clsid == 0x0108AF6Cu ||
                    g_applet_clsid == 0x0109AF55u || g_thread_fn)
                    evt = 1u;
                guest_call(g_timers[i].pfn, g_timers[i].puser, evt, 0, 0);
            }
            return;
        }
    }

    /* A thread guest e o game loop. Nao sintetizar HandleEvent(evt=1): nos
     * ARMCC Data East isso e EVT_KEY/STOP e desmonta o estado a cada yield.
     * credit==0 apos slice yield: restaura e retoma nesta mesma chamada
     * (timers/signals acima ja rodaram). */
    if (g_thread_slice_credit == 0)
        g_thread_slice_credit = 1;

    if (g_thread_alive && g_thread_ready && g_thread_context_saved &&
        !getenv("ZEEMU_STUB_THREAD")) {
        uint64_t executed = g_cpu.executed;
        g_in_thread_guest = true;
        g_thread_slice_credit = 0;
        g_state = BOOT_SIGNAL_CALL;
        g_signal_call_started_at = g_cpu.executed;
        g_signal_saved_cpu = g_cpu;
        g_signal_context_saved = true;
        g_cpu = g_thread_saved_cpu;
        g_cpu.executed = executed;
        g_cpu.halted = false;
        g_thread_context_saved = false;
        if (getenv("ZEEMU_TRACE_THREADS"))
            LOGI("boot: thread guest retomada PC=0x%08X SP=0x%08X",
                 g_cpu.r[REG_PC], g_cpu.r[REG_SP]);
        return;
    }
}

void zboot_tick(uint32_t elapsed_ms) {
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
            /* Botões reais: A/B/C/MENU/BACK. O D-pad fica EXCLUSIVAMENTE
             * nos eixos analogicos (position, mais abaixo): o Family Pack
             * navega o menu por GetPositionState/GetMin/GetMax (slots 10-12)
             * e descarrila se receber button-event do D-pad (crash em
             * 0xE5053008 no release). O zeemu tambem nao espelha D-pad em
             * eixos nem os trata como botoes. */
            static const struct {
                uint32_t bit, id, uid;
            } map[] = {
                { ZKEY_A,     0,  0x0106C40Au },
                { ZKEY_B,     1,  0x0106C40Bu },
                { ZKEY_C,     2,  0x0106C40Cu },
                { ZKEY_MENU,  9,  0x0106C402u },
                { ZKEY_BACK,  8,  0x0106C403u },
            };
            unsigned n;
            bool any = false;
            for (n = 0; n < sizeof(map) / sizeof(map[0]); n++) {
                if (!(changed & map[n].bit)) continue;
                bit = map[n].bit;
                id = map[n].id;
                uid = map[n].uid;
                hid_evt_push(id, uid, down);
                any = true;
                if (getenv("ZEEMU_TRACE_HID")) {
                    LOGI("HID: uid=0x%08X down=%u q=%d",
                         uid, down ? 1u : 0u, g_hid_evt_count);
                }
            }
            if (any && g_hid_button_signal) {
                uint32_t user = zmem_read32(g_hid_button_signal + 8);
                /* Convencao BREW: ISignalCtl e so um "acorde-me"
                 * (PFNNOTIFY(pUser)); jogo drena via GetNextButtonEvent. */
                zboot_signal_fire(g_hid_button_signal, "hid-button");
                if (getenv("ZEEMU_CB_R4_AS_USER"))
                    g_cpu.r[4] = user;
                if (zboot_signal_drain_one())
                    return;
            }
        }
    }

    /* Posicao do stick analogico (IHIDDevice position): quando o D-pad
     * (espelhado nos eixos) muda, acorda o callback de RegisterForPositionChange
     * para jogos que navegam menus por posicao (ex: Family Pack) em vez de
     * por eventos de botao. GetPositionState (case 10) devolve os eixos. */
    {
        uint32_t lx = zinput_left_stick_x();
        uint32_t ly = zinput_left_stick_y();
        if (lx != g_hid_axis_lx || ly != g_hid_axis_ly) {
            g_hid_axis_lx = lx;
            g_hid_axis_ly = ly;
            /* Voltou ao centro (release): NAO acorda o signal. O signal de
             * position so leva o cursor; ao soltar, o jogo chama um handler
             * de menu via ponteiro nao preparado (crash 0xE5053004). Soltar
             * o stick so "libera" - nenhum callback espera esse estado. */
            if (lx == 0x8000u && ly == 0x8000u)
                return;
            if (g_hid_position_signal) {
                uint32_t callback = zmem_read32(g_hid_position_signal + 4);
                uint32_t user = zmem_read32(g_hid_position_signal + 8);
                if (getenv("ZEEMU_TRACE_HID")) {
                    LOGI("HID: position lx=0x%04X ly=0x%04X callback=0x%08X "
                         "user=0x%08X", lx, ly, callback, user);
                }
                zboot_signal_fire(g_hid_position_signal, "hid-position");
                if (zboot_signal_drain_one())
                    return;
            }
        }
    }

    /* Timers ficam so em zboot_process_timers (depois desta funcao).
     * Disparar aqui monopolizava BOOT_RUNNING e a thread guest (Start
     * durante EVT_APP_START nos Data East) nunca arrancava. */
}

/* ---- IShell real (traps 0x200+) ---- */

/* Diretorio do .mod carregado (app root: resources.dat, .bar, etc.) */
static char g_mod_dir[512] = {0};
void zbrew_set_mod_dir(const char *path) {
    if (!path || !path[0]) return;
    char full[512];
    strncpy(full, path, sizeof(full) - 1);
    full[sizeof(full) - 1] = 0;
    for (char *p = full; *p; ++p) {
        if (*p == '\\') *p = '/';
    }
    /* Remove o nome do arquivo, deixando apenas o diretorio */
    char *last_sep = strrchr(full, '/');
    if (last_sep) *last_sep = '\0';
    else { full[0] = 0; }
    if (full[0]) {
        strncpy(g_mod_dir, full, sizeof(g_mod_dir) - 1);
        g_mod_dir[sizeof(g_mod_dir) - 1] = 0;
    }
}
const char *zbrew_get_mod_dir(void) {
    return g_mod_dir[0] ? g_mod_dir : NULL;
}

/* Servicos recebem stubs COM permissivos para deixar jogos reais avancarem.
 * A lista conhecida ajuda QueryClass, mas CreateInstance tambem tolera
 * CLSIDs desconhecidos para evitar travar cedo no boot. */
static uint32_t real_create_instance(uint32_t clsid) {
    LOGI("real_create_instance: clsid=0x%08X", clsid);
    if (zbrew_is_font_clsid(clsid))
        return zbrew_font_create(clsid);
    if (clsid == AEECLSID_FILEMGR_REAL)
        return zbrew_create_filemgr();
    if (clsid == AEECLSID_EGL_REAL)
        return zegl_create_interface();
    if (clsid == AEECLSID_GL_REAL)
        return zgl_create_interface();
    if (clsid == 0x0103D8EAu || clsid == 0x0103D8DDu)
        return zgl_create_igles_interface();
    if (clsid == AEECLSID_MEDIAPCM_REAL)
        return zbrew_create_media_pcm();
    if (clsid == AEECLSID_MEDIAADPCM_REAL)
        return zbrew_create_media_adpcm();
    if (clsid == AEECLSID_MEDIAMIDI_REAL)
        return zbrew_create_media_midi();
    if (clsid == AEECLSID_MEDIAMP3_REAL)
        return zbrew_create_media_mp3();
    if (clsid == AEECLSID_MEDIAUTIL_REAL)
        return zbrew_create_media_util();
    if (clsid == AEECLSID_SOUNDPLAYER_R)
        return zbrew_create_media_util();
    if (clsid == AEECLSID_DISPLAY_REAL || clsid == AEECLSID_DISPLAY1_REAL) {
        /* DISPLAY e DISPLAY1 = mesmo IDisplay real + mesmo device bitmap.
         * Ovos/QEGL pega DIBM via DISPLAY1; IDisplay via 0x01001001. */
        if (g_display_obj) {
            LOGI("real_create_instance: DISPLAY reuse obj=0x%08X clsid=0x%08X",
                 g_display_obj, clsid);
            return g_display_obj;
        }
        /* RE4 (0x0108AF6C) usa vtbl relativa; FIFA e demais BLX absoluto. */
        int rel = (g_boot_clsid == 0x0108AF6Cu);
        uint32_t obj = zbrew_make_idisplay_real(rel);
        g_display_obj = obj;
        LOGI("real_create_instance: DISPLAY real obj=0x%08X rel=%d clsid=0x%08X",
             obj, rel, clsid);
        return obj;
    }
    if (clsid == 0x01001002u) { /* GRAPHICS — stub generico */
        uint32_t obj = make_stub_interface(clsid);
        g_graphics_obj = obj;
        LOGI("real_create_instance: GRAPHICS obj=0x%08X", obj);
        return obj;
    }
    if (clsid == AEECLSID_MEMASTREAM) {
        uint32_t obj = zmemastream_create();
        LOGI("real_create_instance: MemAStream -> obj=0x%08X", obj);
        return obj;
    }
    if (clsid == AEECLSID_CPNG || clsid == AEECLSID_WINBMP ||
        clsid == AEECLSID_NATIVEBMP || clsid == AEECLSID_CPNGBREW) {
        return zimage_create(clsid);
    }
    if (clsid == AEECLSID_PNGDECODER_R || clsid == AEECLSID_JPEGDECODER_R) {
        uint32_t obj = zimage_decoder_create(clsid);
        LOGI("real_create_instance: ImageDecoder clsid=0x%08X -> 0x%08X", clsid, obj);
        return obj;
    }
    if (clsid == AEECLSID_MEDIA_REAL || clsid == 0x01005500u) {
        /* CLSID generico de mídia - retornar stub IMedia que o jogo
         * pode usar para tocar áudio (ex: Pac-Mania via GetHandler). */
        LOGI("real_create_instance: MEDIA_REAL -> stub IMedia obj=0x%08X", clsid);
        return zbrew_create_media_util(); /* reutiliza IMedia util como stub */
    }
    return make_stub_interface(clsid);
}

void zbrew_handle_ishell_real(uint32_t id) {
    uint32_t slot = id - ZT_ISHELL_BASE;
    const char *name = slot < ZAEE_ISHELL_NAMED_COUNT
                     ? ZAEE_ISHELL_NAMES[slot] : "?";
    uint32_t r1 = g_cpu.r[1], r2 = g_cpu.r[2], r3 = g_cpu.r[3];

    if (getenv("ZEEMU_TRACE_HID")) {
        LOGI("ISHELL: slot=%u (%s) R0=0x%08X R1=0x%08X R2=0x%08X "
             "R3=0x%08X PC=0x%08X",
             slot, name, g_cpu.r[0], r1, r2, r3, g_cpu.r[REG_PC]);
    }

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
            LOGI("ISHELL CreateInstance calling real_create_instance(0x%08X)", r1);
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
        uint32_t pfn = r2, puser = r3;
        int i, free_slot = -1;
        /* BREW 1.x aceita AEECallback* nos dois argumentos. */
        if (pfn == puser && pfn >= ZMEM_HEAP_BASE &&
            pfn < ZMEM_HEAP_BASE + ZMEM_HEAP_SIZE) {
            uint32_t real_pfn = zmem_read32(pfn + 16);
            uint32_t real_user = zmem_read32(pfn + 20);
            if ((real_pfn < ZMEM_RAM_SIZE) ||
                (real_pfn >= ZMEM_HEAP_BASE &&
                 real_pfn < ZMEM_HEAP_BASE + ZMEM_HEAP_SIZE)) {
                LOGI("IShell_SetTimer: AEECallback 0x%08X -> pfn=0x%08X user=0x%08X",
                     pfn, real_pfn, real_user);
                pfn = real_pfn;
                puser = real_user;
            }
        }
        for (i = 0; i < ZTIMER_MAX; i++) {
            if (g_timers[i].active && g_timers[i].pfn == pfn &&
                g_timers[i].puser == puser)
                break;
            if (!g_timers[i].active && free_slot < 0) free_slot = i;
        }
        if (i == ZTIMER_MAX) i = free_slot;
        if (i < 0) LOGW("IShell_SetTimer: fila cheia");
        else {
            static uint32_t timer_logs = 0;
            zboot_timer_set(i, r1 < 16 ? 16 : r1, pfn, puser);
            if (timer_logs++ < 64)
                LOGI("IShell_SetTimer: %u ms callback=0x%08X user=0x%08X",
                     r1, pfn, puser);
        }
        g_cpu.r[0] = 0;
        break;
    }

    case 12: { /* CancelTimer(shell, pfn, puser) */
        uint32_t pfn = r1, puser = r2;
        int i;
        if (pfn == puser && pfn >= ZMEM_HEAP_BASE &&
            pfn < ZMEM_HEAP_BASE + ZMEM_HEAP_SIZE) {
            pfn = zmem_read32(r1 + 16);
            puser = zmem_read32(r1 + 20);
        }
        for (i = 0; i < ZTIMER_MAX; i++) {
            if (g_timers[i].active &&
                (pfn == 0 || g_timers[i].pfn == pfn) &&
                (puser == 0 || g_timers[i].puser == puser))
                g_timers[i].active = false;
        }
        LOGI("IShell_CancelTimer: callback=0x%08X user=0x%08X", pfn, puser);
        g_cpu.r[0] = 0;
        break;
    }

    case 21: { /* SendEvent(shell, flags, cls, evt, wp, dwp) — zeemu ASYNC */
        uint32_t flags = r1;
        uint32_t cls = r2;
        uint32_t evt = r3;
        uint32_t wp = zbrew_stack_arg(0);
        uint32_t dwp = zbrew_stack_arg(1);
        (void)cls;
        if ((flags & ZEVT_FLAG_ASYNC) && zboot_get_applet_object()) {
            int i;
            bool skip = false;
            if (flags & ZEVT_FLAG_UNIQUE) {
                for (i = 0; i < g_app_evt_queue_count; i++) {
                    if (g_app_evt_queue[i].event == evt) {
                        skip = true;
                        break;
                    }
                }
            }
            if (!skip) {
                if (g_app_evt_queue_count >= ZAPP_EVT_QUEUE_MAX) {
                    LOGW("IShell_SendEvent: fila cheia, drop evt=0x%08X", evt);
                } else {
                    g_app_evt_queue[g_app_evt_queue_count].event = evt;
                    g_app_evt_queue[g_app_evt_queue_count].wparam = wp;
                    g_app_evt_queue[g_app_evt_queue_count].dwp = dwp;
                    g_app_evt_queue_count++;
                    LOGI("IShell_SendEvent: ASYNC queue evt=0x%08X wp=0x%04X "
                         "dwp=0x%08X flags=0x%04X (n=%d)",
                         evt, wp & 0xFFFFu, dwp, flags, g_app_evt_queue_count);
                }
            }
        } else {
            LOGI("IShell_SendEvent: flags=0x%04X cls=0x%08X evt=0x%08X "
                 "wp=0x%04X dwp=0x%08X (no queue)",
                 flags, cls, evt, wp & 0xFFFFu, dwp);
        }
        g_cpu.r[0] = 1; /* TRUE — aceito (zeemu) */
        break;
    }

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

    case 32: /* GetHandler(shell, handler_type, mime) -> returns CLSID of handler */
        /* Pac-Mania chama GetHandler(0x01005500=MEDIA_REAL, NULL) para obter
         * um CLSID de mídia para tocar áudio. Retornamos AEECLSID_MEDIAPCM_REAL
         * para PCM/ADPCM (o que o Double Dragon usa com sucesso). */
        if (r1 == AEECLSID_MEDIA_REAL || r1 == 0x01005500u) {
            uint32_t handler_clsid = AEECLSID_MEDIAPCM_REAL; /* 0x01005511 */
            if (r2) zmem_write32(r2, handler_clsid);
            g_cpu.r[0] = handler_clsid; /* retorna o CLSID em R0 */
            LOGI("IShell_GetHandler: MEDIA_REAL -> MEDIAPCM_REAL (0x%08X)", handler_clsid);
        } else {
            g_cpu.r[0] = 1; /* EFAILED */
        }
        break;

    case 33: /* RegisterHandler */
    case 34: /* RegisterNotify */
    case 35: /* Notify */
        g_cpu.r[0] = 0;
        break;

    case 36: { /* Resume(shell, pcb) — zeemu queue; aplica no process_timers */
        uint32_t pcb = r1;
        if (pcb && g_thread_resume_cbk && pcb == g_thread_resume_cbk) {
            uint32_t thr = zmem_read32(g_thread_resume_cbk + 20);
            if (thr) g_thread_obj = thr;
            g_thread_resume_pending = true;
            LOGI("IShell_Resume: thread resume cbk=0x%08X thr=0x%08X",
                 pcb, thr);
        }
        g_cpu.r[0] = 0;
        break;
    }
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
        if (getenv("ZEEMU_TRACE_HID")) {
            char nm[64];
            int i;
            for (i = 0; i < 63; i++) {
                uint8_t c = zmem_read8(r1 + i);
                if (c < 0x20 || c > 0x7e) break;
                nm[i] = (char)c;
            }
            nm[i] = 0;
            LOGI("ISHELL_LoadResDataEx(name=\"%s\" id=0x%04X type=%u "
                 "ppData=0x%08X)", nm, r2, r3, zbrew_stack_arg(0));
        }
        {
            char nm[64];
            int i;
            for (i = 0; i < 63; i++) {
                uint8_t c = zmem_read8(r1 + i);
                if (c < 0x20 || c > 0x7e) break;
                nm[i] = (char)c;
            }
            nm[i] = 0;

            /* Pac-Mania (0x01087B72): recursos embutidos no .mod em 0x23128.
             * Ridge (0x01087B73) e outros: mesmo padrao de nome .bar, mas o
             * fallback embutido devolvia size errado e dwSize virava ponteiro. */
            if (zmem_read8(r1) && (strstr(nm, ".bar") || strstr(nm, ".data"))) {
                uint32_t pBuf = zbrew_stack_arg(0);
                uint32_t pnSize = zbrew_stack_arg(1);
                if (pnSize) {
                    if (g_applet_clsid == 0x01087B72u) {
                        uint32_t base = 0x23128;
                        if (r2 >= 0x1414u && r2 <= 0x1416u)
                            base += (r2 - 0x1414u) * 0x3Eu;
                        uint32_t entry[3];
                        uint32_t n = 0;
                        for (i = 0; i < 0x2000; i++) {
                            uint32_t v = zmem_read32(0x1A48 + i);
                            if ((v & 0x00FFFFFFu) == 0x00023128u) {
                                entry[n] = v;
                                if (++n == 3) break;
                            }
                        }
                        if (n == 3) base = entry[0];
                        uint32_t size = 0x243B0 - base;
                        if (pBuf == 0xFFFFFFFFu) {
                            zmem_write32(pnSize, size);
                            g_cpu.r[0] = 0xFFFFFFFFu;
                        } else if (pBuf) {
                            zmem_write32(pBuf, base);
                            zmem_write32(pnSize, size);
                            g_cpu.r[0] = base;
                        } else {
                            g_cpu.r[0] = base;
                        }
                        LOGI("LoadResDataEx: bar embutido -> 0x%08X size=%u",
                             g_cpu.r[0], size);
                        break;
                    }
                    /* BAR externo (Ridge etc.): carrega entrada real. */
                    {
                        char barpath[560];
                        const char *md = zbrew_get_mod_dir();
                        bar_resource_t bar;
                        bar_entry_t *ent;
                        uint32_t guest = 0;
                        if (md && md[0])
                            snprintf(barpath, sizeof(barpath), "%s/%s", md, nm);
                        else
                            snprintf(barpath, sizeof(barpath), "%s", nm);
                        memset(&bar, 0, sizeof(bar));
                        if (zbar_parse_file(barpath, &bar) &&
                            (ent = zbar_find_entry(&bar, (uint16_t)r2)) &&
                            ent->data_ptr && ent->data_size) {
                            if (pBuf == 0xFFFFFFFFu) {
                                zmem_write32(pnSize, ent->data_size);
                                g_cpu.r[0] = 0xFFFFFFFFu;
                            } else {
                                guest = zheap_alloc(ent->data_size);
                                if (guest &&
                                    zmem_write_block(guest, ent->data_ptr,
                                                     ent->data_size)) {
                                    if (pBuf) zmem_write32(pBuf, guest);
                                    zmem_write32(pnSize, ent->data_size);
                                    g_cpu.r[0] = guest;
                                } else {
                                    g_cpu.r[0] = 0;
                                }
                            }
                            LOGI("LoadResDataEx: '%s' id=0x%04X -> 0x%08X "
                                 "size=%u", nm, (unsigned)r2, g_cpu.r[0],
                                 ent->data_size);
                            zbar_free(&bar);
                            break;
                        }
                        zbar_free(&bar);
                        LOGW("LoadResDataEx: '%s' id=0x%04X nao encontrado",
                             nm, (unsigned)r2);
                    }
                }
            }
        }
        if (r3)
            zmem_write32(r3, 0);
        g_cpu.r[0] = 0;
        break;

    case 42: /* RegisterSystemCallback */
        g_cpu.r[0] = 0;
        break;

case 43: /* DetectType */
        /* Pac-Mania (loader 0xC54): DUAS chamadas com
         * semantica oposta:
         *  r1 == NULL -> quer a deteccao de tipo: retorna 0x23, [r2]=tamanho
         *  r1 != NULL (ex: 0x23227 = payload) -> quer ok: retorna 0
         */
{ static int dt = 0;
          if (dt < 4) { LOGI("DetectType r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X -> %s", g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[3], g_cpu.r[1] ? "ok(0)" : "tipo 0x23"); dt++; }
          if (g_cpu.r[1]) {
              g_cpu.r[0] = 0;
              if (g_cpu.r[2]) zmem_write32(g_cpu.r[2], 0);
          } else {
              g_cpu.r[0] = 0x23;
              if (g_cpu.r[2]) zmem_write32(g_cpu.r[2], 0x23);
          }
        }
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

/* ---- ISignal / ISignalCtl real (trap 0x6A0+) ---- */
void zbrew_handle_signal(uint32_t id) {
    uint32_t slot = id - ZT_SIGNAL_BASE;
    uint32_t obj = g_cpu.r[0];

    if (getenv("ZEEMU_TRACE_HID")) {
        LOGI("SIGNAL: slot=%u obj=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X PC=0x%08X",
             slot, obj, g_cpu.r[1], g_cpu.r[2], g_cpu.r[3], g_cpu.r[REG_PC]);
    }

    switch (slot) {
    case 0: /* AddRef */
        g_cpu.r[0] = zboot_signal_addref(obj);
        break;

    case 1: /* Release */
        g_cpu.r[0] = zboot_signal_release(obj);
        break;

    case 2: { /* QueryInterface: ISignal e ISignalCtl sao o mesmo objeto */
        uint32_t iid = g_cpu.r[1];
        uint32_t out = g_cpu.r[2];
        if (iid && obj && out && out < ZDISPLAY_ADDR_MAX &&
            zboot_signal_addref(obj)) {
            zmem_write32(out, obj);
            g_cpu.r[0] = 0; /* SUCCESS */
        } else {
            g_cpu.r[0] = 4; /* ECLASSNOTSUPPORT */
        }
        break;
    }

    case 3: /* Set */
        zboot_signal_fire(obj, "signal");
        g_cpu.r[0] = 0;
        break;

    case 4: /* Detach */
        if (zboot_signal_valid(obj)) {
            zmem_write32(obj + 12, 1);
            zmem_write32(obj + 16, 0);
        }
        g_cpu.r[0] = 0;
        break;

    case 5: /* Enable: re-dispara se havia ficado pendente */
        if (obj && obj < ZDISPLAY_ADDR_MAX && zmem_read32(obj + 16) != 0) {
            zmem_write32(obj + 16, 0);
            zboot_signal_fire(obj, "signal-enable");
        }
        g_cpu.r[0] = 0;
        break;

    default:
        g_cpu.r[0] = 0;
        break;
    }
}

void zbrew_handle_stub(uint32_t id) {
    uint32_t slot = id - ZT_STUB_BASE;
    uint32_t obj = g_cpu.r[0];
    uint32_t clsid = zmem_read32(obj + 4);
    static uint32_t warn_count = 0;
    /* IDIB: +4=pPaletteMap=0; detecta por vtbl+bits+depth. */
    if (is_device_bitmap_obj(obj))
        clsid = ZCLSID_DEVICE_BITMAP;

    /* RE4 event helper (applet+0x3C): slot4=evt dispatch, slot5 le obj+0x14.
     * Nao e CreateInstance/DrawRect — SUCCESS puro. */
    if (clsid == 0x52453448u) {
        static uint32_t re4h_log;
        if (re4h_log++ < 8)
            LOGI("RE4H stub slot=%u R1=0x%08X R2=0x%08X R3=0x%08X",
                 slot, g_cpu.r[1], g_cpu.r[2], g_cpu.r[3]);
        g_cpu.r[0] = 0;
        return;
    }

    static uint32_t stub_log = 0;
    if (stub_log++ < 256) {
        LOGI("STUB: id=0x%03X slot=%u clsid=0x%08X obj=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X",
             id, slot, clsid, obj, g_cpu.r[1], g_cpu.r[2], g_cpu.r[3]);
    }

    if (getenv("ZEEMU_TRACE_HID")) {
        LOGI("HIDSTUB: slot=%u clsid=0x%08X R0=0x%08X R1=0x%08X "
             "R2=0x%08X R3=0x%08X PC=0x%08X",
             slot, clsid, g_cpu.r[0], g_cpu.r[1], g_cpu.r[2],
             g_cpu.r[3], g_cpu.r[REG_PC]);
    }
    /* Stub COM generico retorna sucesso para operacoes basicas */
    switch (slot) {
    case 38: /* IGraphics_SetDestination */
        if (clsid == AEECLSID_GRAPHICS_REAL || clsid == AEECLSID_GRAPHICS_BREW) {
            g_display_dest_obj = g_cpu.r[1];
            LOGI("IGraphics_SetDestination -> 0x%08X", g_display_dest_obj);
            g_cpu.r[0] = 0;
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 0x27: { /* IGraphics_GetDestination; no IDisplay, dispatcher estendido.
                  * RE4 chama metodos estendidos via r3: 0x4B(75)=ClearScreen,
                  * 0x54(84)=FillRect, 0x66(102)=DrawRect.
                  * Para interfaces nao-IDisplay, mantem o STUBEV do Pac-Mania. */
        if (clsid == AEECLSID_GRAPHICS_REAL || clsid == AEECLSID_GRAPHICS_BREW) {
            g_cpu.r[0] = g_display_dest_obj ? g_display_dest_obj : g_device_bitmap;
            break;
        }
        if (is_display_interface(clsid)) {
            uint32_t method = g_cpu.r[3]; /* indice do metodo estendido */
            static uint32_t disp_logs = 0;
            if (disp_logs++ < 32) {
                LOGI("IDISP_DISPATCH: metodo=%u (0x%02X) obj=0x%08X r1=0x%08X r2=0x%08X "
                     "sp0=0x%08X PC=0x%08X",
                     method, method, obj, g_cpu.r[1], g_cpu.r[2],
                     zbrew_stack_arg(0), g_cpu.r[REG_PC]);
            }
            switch (method) {
            case 0x4B: /* 75 - ClearScreen: limpa framebuffer com cor de fundo */
                zfb_clear(display_rgbval_to_xrgb(g_display_background_color));
                zbrew_mark_frame();
                g_cpu.r[0] = 0;
                break;
            case 0x54: /* 84 - FillRect: preenche retangulo apontado por r2 */
                {
                    int x, y, w, h;
                    display_read_rect(g_cpu.r[2], &x, &y, &w, &h);
                    display_fill_rect(x, y, w, h, g_display_line_color);
                    zbrew_mark_frame();
                }
                g_cpu.r[0] = 0;
                break;
            case 0x66: /* 102 - DrawRect: desenha contorno do retangulo apontado por r2 */
                {
                    int x, y, w, h;
                    display_read_rect(g_cpu.r[2], &x, &y, &w, &h);
                    if (w > 0 && h > 0) {
                        display_fill_rect(x, y, w, 1, g_display_line_color);
                        display_fill_rect(x, y + h - 1, w, 1, g_display_line_color);
                        display_fill_rect(x, y, 1, h, g_display_line_color);
                        display_fill_rect(x + w - 1, y, 1, h, g_display_line_color);
                    }
                    zbrew_mark_frame();
                }
                g_cpu.r[0] = 0;
                break;
            default:
                g_cpu.r[0] = 0;
                break;
            }
            break;
        }
        /* Pac-Mania: STUBEV ('posta evento' na fila do loader) */
        static uint32_t ev = 0;
        if (ev < 40) {
            LOGI("STUBEV slot=0x27 r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X LR=0x%08X PC=0x%08X",
                 g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[3],
                 g_cpu.r[REG_LR], g_cpu.r[REG_PC] - 8);
            ev++;
        }
        g_cpu.r[0] = 0;
        break;
    }
    case 0: /* AddRef */
        g_cpu.r[0] = 1; /* refcount */
        break;
    case 1: { /* Release */
        if (clsid == AEECLSID_THREAD_REAL) {
            /* IBase::Release retorna a contagem; zeemu mantem o singleton. */
            g_cpu.r[0] = 0;
            break;
        }
        /* O registrador HID possui uma referencia ao signal. Solta-la junto
         * com a interface dona; Release do ISignal nao invalida o registro. */
        if (clsid == AEECLSID_HID_REAL) {
            zboot_signal_register(&g_hid_connect_signal, 0);
            g_hid_connect_pending = false;
            LOGI("IHID Release: connect signal liberado");
        } else if (clsid == ZCLSID_HID_DEVICE) {
            zboot_signal_register(&g_hid_button_signal, 0);
            zboot_signal_register(&g_hid_position_signal, 0);
            LOGI("IHIDDevice Release: button/position signals liberados");
        }
        g_cpu.r[0] = 0; /* stubs nao possuem outra referencia */
        break;
    }
    case 2: /* QueryInterface (exceto IDisplay, que nao tem QI no slot 2) */
        if (clsid == AEECLSID_QEGL_REAL ||
            clsid == 0x0103D8EDu || clsid == 0x0103D8EEu ||
            clsid == 0x0103D8EFu || clsid == 0x0103D8F0u ||
            clsid == 0x010426E3u || clsid == 0x0109B10Eu ||
            clsid == 0x010434CCu || clsid == 0x01051834u ||
            clsid == 0x01058546u || clsid == 0x010459B1u ||
            clsid == 0x0103D8DEu || clsid == 0x0103D8EBu ||
            clsid == 0x0103DEF1u) {
            /* IIDs do zeemu BrewEGL::QueryInterface — Data East ports
             * pedem GLES + SurfaceManip + ImageonExt no InitGLExtensions. */
            uint32_t iid = g_cpu.r[1];
            uint32_t iface = 0;
            if (iid == 0x0103D8DDu || iid == 0x0103D8EAu)
                iface = zgl_create_igles_interface();
            else if (iid == 0x01014BC4u)
                iface = zegl_create_interface();
            else if (iid == clsid ||
                     iid == AEECLSID_QEGL_REAL || iid == 0x0103D8EDu ||
                     iid == 0x0103D8EEu || iid == 0x0103D8EFu ||
                     iid == 0x0103D8F0u || iid == 0x010426E3u ||
                     iid == 0x0109B10Eu)
                iface = g_cpu.r[0]; /* self */
            else if (iid == 0x0103D8DEu || /* GLES10EXT */
                     iid == 0x0103D8EBu || /* GLES11EXT */
                     iid == 0x0103DEF1u || /* GLES11EXTPAK */
                     iid == 0x01058546u || /* GLESIMAGEONEXT */
                     iid == 0x010459B1u || /* GLESIMAGEONEXT_V1 */
                     iid == 0x010434CCu || /* EGLSurfaceManip */
                     iid == 0x01051834u)   /* EGLSurfaceManip alt */
                iface = make_stub_interface(iid);
            if (g_cpu.r[2] && iface)
                zmem_write32(g_cpu.r[2], iface);
            LOGI("IQEGL_QueryInterface(iid=0x%08X from=0x%08X) -> 0x%08X",
                 iid, clsid, iface);
            g_cpu.r[0] = iface ? 0 : 4; /* SUCCESS / ECLASSNOTSUPPORT */
            break;
        }
        if (is_display_interface(clsid)) {
            /* GetFontMetrics(po, font, AEEFontMetrics*, u16* ascent).
             * Usa as metricas da fonte real (do IFont criado via
             * IShell_CreateInstance) quando o handle for uma dessas;
             * caso contrario mantem os valores de seguranca antigos. */
            uint16_t asc = 12, desc = 4, lead = 2, mw = 8;
            if (zbrew_font_get_metrics(g_cpu.r[1], &asc, &desc, &lead, &mw)) {
                /* metricas da fonte real */
            } else {
                asc = 12; desc = 4; lead = 2; mw = 8;
            }
            {
                uint32_t pmet = g_cpu.r[2];
                if (pmet) {
                    zmem_write16(pmet + 0, asc);  /* nAscent */
                    zmem_write16(pmet + 2, desc); /* nDescent */
                    zmem_write16(pmet + 4, lead); /* nLeading */
                    zmem_write16(pmet + 6, mw);   /* nMaxCharWidth */
                }
                if (g_cpu.r[3]) zmem_write16(g_cpu.r[3], asc);
                g_cpu.r[0] = (uint32_t)(asc + desc); /* altura da fonte */
            }
            break;
        }
        if (clsid == ZCLSID_DEVICE_BITMAP) {
            /* QI: IDIB/Bitmap = self; ITransform (0x01001029) = stub. */
            uint32_t iid = g_cpu.r[1];
            uint32_t iface = 0;
            if (iid == 0x01001021u || iid == 0x01001045u ||
                iid == 0x0100102Cu || iid == ZCLSID_DEVICE_BITMAP)
                iface = obj;
            else if (iid == 0x01001029u)
                iface = make_stub_interface(0x01001029u);
            if (g_cpu.r[2]) zmem_write32(g_cpu.r[2], iface);
            LOGI("IDIB QI obj=0x%08X d=%u sch=%u pitch=%u pBmp=0x%08X",
                 obj, zmem_read8(obj + 28), zmem_read8(obj + 29),
                 zmem_read16(obj + 24), zmem_read32(obj + 8));
            g_cpu.r[0] = iface ? 0 : 4;
            break;
        }
        zmem_write32(g_cpu.r[2], 0); /* ppInterface = 0 */
        g_cpu.r[0] = 1; /* EFAILED */
        break;
    case 3:
        if (clsid == ZCLSID_DEVICE_BITMAP) {
            /* IBitmap_RGBToNative — RGBVAL -> native; RGB565 via scheme. */
            uint32_t rgb = g_cpu.r[1];
            uint8_t scheme = zmem_read8(obj + 29);
            if (rgb == 0xFFFFFFFFu)
                g_cpu.r[0] = 0;
            else if (scheme == 16 || idib_depth_bytes(obj) == 2) {
                uint32_t r = (rgb >> 8) & 0xFFu;
                uint32_t g = (rgb >> 16) & 0xFFu;
                uint32_t b = (rgb >> 24) & 0xFFu;
                g_cpu.r[0] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            } else {
                g_cpu.r[0] = rgb;
            }
            break;
        }
        if (clsid == AEECLSID_THREAD_REAL) {
            /* Malloc(po, size) — heap do thread (zeemu Thread_Malloc). */
            g_cpu.r[0] = zheap_alloc(g_cpu.r[1]);
            break;
        }
        if (is_display_interface(clsid)) {
            /* MeasureTextEx(po, font, AECHAR* psz, int nl, nMaxWidth, pnFits)
             * -> R0=largura em pixels, usa a mesma resolucao de texto (AECHAR/
             * byte/descritor) e as metricas da fonte (nMaxCharWidth) quando a
             * fonte eh uma das do sistema; senao a fonte 5x7 de fallback. */
            uint16_t mw = 6, asc = 12, desc = 4, lead = 2;
            if (zbrew_font_get_metrics(g_cpu.r[1], &asc, &desc, &lead, &mw))
                ; /* fonte real: usa mw=largura por caractere */
            else
                mw = 6; /* fallback 5x7 + 1px gap */
            {
                uint32_t base = display_find_arg_base();
                uint32_t psz = g_cpu.r[2];
                int32_t nl = (int32_t)g_cpu.r[3];
                uint32_t pn_fits = zmem_read32(base + 4);
                char text[160];
                int len = display_resolve_string(psz, nl, text, sizeof(text));
                int width = len > 0 ? (int)mw * len : 16;
                if (pn_fits && pn_fits < ZDISPLAY_ADDR_MAX)
                    zmem_write32(pn_fits, (uint32_t)len);
                g_cpu.r[0] = (uint32_t)width;
            }
            break;
        }
        if (clsid == AEECLSID_SIGNALCBFACT_R) {
            uint32_t signal = make_signal_object(g_cpu.r[1], g_cpu.r[2]);
            uint32_t pp_signal = g_cpu.r[3];
            uint32_t pp_signal_ctl = zbrew_stack_arg(0);
            if (pp_signal && pp_signal < ZDISPLAY_ADDR_MAX)
                zmem_write32(pp_signal, signal);
            if (pp_signal_ctl && pp_signal_ctl < ZDISPLAY_ADDR_MAX)
                zmem_write32(pp_signal_ctl, signal);
            LOGI("SignalCBFactory_CreateSignal(cb=0x%08X user=0x%08X) -> 0x%08X",
                 g_cpu.r[1], g_cpu.r[2], signal);
            LOGI("SignalCBFactory outputs: signal=0x%08X ctl=0x%08X",
                 pp_signal, pp_signal_ctl);
            g_cpu.r[0] = signal ? 0 : 1;
            break;
        }
        if (clsid == AEECLSID_QEGL_REAL) {
            /* Zeemu QEGL slot3 = GetError(po, pError): *R1 = EGL_SUCCESS.
             * Data East: InitGLSurface(po, bitmap, ppSurface) com R1=IDIB.
             * Distingue: R1 IDIB => DE; senao GetError (RE4). */
            if (is_device_bitmap_obj(g_cpu.r[1])) {
                uint32_t surface = zegl_create_qegl_surface();
                if (g_cpu.r[2] && g_cpu.r[2] < ZDISPLAY_ADDR_MAX)
                    zmem_write32(g_cpu.r[2], surface);
                g_qegl_surface = surface;
                LOGI("IQEGL_InitGLSurface: bitmap=0x%08X out=0x%08X -> 0x%08X",
                     g_cpu.r[1], g_cpu.r[2], surface);
                g_cpu.r[0] = surface ? 0 : 1;
            } else {
                if (g_cpu.r[1] && g_cpu.r[1] < ZDISPLAY_ADDR_MAX)
                    zmem_write32(g_cpu.r[1], 0x3000u); /* EGL_SUCCESS */
                LOGI("IQEGL_GetError: out=0x%08X -> EGL_SUCCESS", g_cpu.r[1]);
                g_cpu.r[0] = 0;
            }
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
        if (clsid == ZCLSID_HID_DEVICE) { /* IHIDDevice_GetDeviceInfo (slot 3) */
            uint32_t info = g_cpu.r[1];
            if (info) {
                zmem_write32(info + 0, ZHID_JOYSTICK_UID);
                zmem_write16(info + 4, 0x1234);
                zmem_write16(info + 6, 0x5678);
                zmem_write8(info + 8, 0);
            }
            g_cpu.r[0] = info ? 0 : 14;
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 4:
        if (clsid == ZCLSID_DEVICE_BITMAP) {
            /* IBitmap_NativeToRGB */
            uint32_t native = g_cpu.r[1];
            if (idib_depth_bytes(obj) == 2) {
                uint32_t r5 = (native >> 11) & 0x1Fu;
                uint32_t g6 = (native >> 5) & 0x3Fu;
                uint32_t b5 = native & 0x1Fu;
                uint32_t r = (r5 << 3) | (r5 >> 2);
                uint32_t g = (g6 << 2) | (g6 >> 4);
                uint32_t b = (b5 << 3) | (b5 >> 2);
                g_cpu.r[0] = (r << 8) | (g << 16) | (b << 24);
            } else {
                g_cpu.r[0] = native;
            }
            break;
        }
        if (clsid == AEECLSID_THREAD_REAL) {
            /* Free(po, ptr) — zeemu Thread_Free noop SUCCESS. */
            if (g_cpu.r[1]) zheap_free(g_cpu.r[1]);
            g_cpu.r[0] = 0;
            break;
        }
        if (clsid == AEECLSID_QEGL_REAL) { /* alias: alguns layouts usam slot 4 */
            uint32_t surface = zegl_create_qegl_surface();
            uint32_t out = g_cpu.r[2] ? g_cpu.r[2] : zbrew_stack_arg(0);
            if (out) zmem_write32(out, surface);
            g_qegl_surface = surface;
            LOGI("IQEGL_InitGLSurface(slot4): bitmap=0x%08X out=0x%08X -> 0x%08X",
                 g_cpu.r[1], out, surface);
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

            if (draw_text_logs < 14) {
                LOGI("DT: r1=0x%08X r2=0x%08X r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X "
                     "r7=0x%08X base=0x%08X sp0=0x%08X sp4=0x%08X sp8=0x%08X spC=0x%08X "
                     "LR=0x%08X "
                     "r5d={%08X %08X %08X %08X} sp8d={%08X %08X %08X %08X}",
                     g_cpu.r[1], g_cpu.r[2], g_cpu.r[3], g_cpu.r[4], g_cpu.r[5],
                     g_cpu.r[6], g_cpu.r[7], base,
                     zmem_read32(base + 0), zmem_read32(base + 4),
                     zmem_read32(base + 8), zmem_read32(base + 12),
                     g_cpu.r[14],
                     zmem_read32(g_cpu.r[5]), zmem_read32(g_cpu.r[5] + 4),
                     zmem_read32(g_cpu.r[5] + 8), zmem_read32(g_cpu.r[5] + 12),
                     zmem_read32(g_cpu.r[7]), zmem_read32(g_cpu.r[7] + 4),
                     zmem_read32(g_cpu.r[7] + 8), zmem_read32(g_cpu.r[7] + 12));
            }

            if (prc && prc < ZDISPLAY_ADDR_MAX &&
                (x < -1024 || x > 4096 || y < -1024 || y > 4096)) {
                x = (int16_t)zmem_read16(prc + 0);
                y = (int16_t)zmem_read16(prc + 2);
            }
            len = display_resolve_string(psz, nl, text, sizeof(text));
            if (len > 0) {
                uint16_t font_w = 6, asc = 12, desc = 4, lead = 2;
                if (zbrew_font_get_metrics(g_cpu.r[1], &asc, &desc, &lead, &font_w)) {
                    if (font_w < 4) font_w = 6; /* largura minima segura */
                } else {
                    font_w = 6;
                }
                (void)asc; (void)desc; (void)lead;
                uint32_t tcol = display_rgbval_to_xrgb(g_display_text_color);
                display_draw_text(x, y, text, tcol, true, font_w);
                if (draw_text_logs < 40 && getenv("ZEEMU_TRACE_TEXT")) {
                    int py = y, px0 = x, px = -1;
                    if (py >= 0 && py < 480 && x >= 0 && x < 640) {
                        for (px = x; px < x + 200 && px < 640; px++)
                            if (zfb_get_pixel(px, py) == tcol) break;
                    }
                    LOGI("TXT: dest=0x%08X cor=0x%08X pixel(%d,%d)=%s", g_display_dest_obj,
                         tcol, x, y, px >= 0 ? "S" : "N");
                }
            }
            if (draw_text_logs < 32) {
                LOGI("IDisplay_DrawText: '%s' em (%d,%d) len=%d psz=0x%08X nl=%d "
                     "dados={%08X %08X %08X %08X %08X %08X}",
                     text, x, y, len, psz, nl,
                     zmem_read32(psz), zmem_read32(psz + 4),
                     zmem_read32(psz + 8), zmem_read32(psz + 12),
                     zmem_read32(psz + 16), zmem_read32(psz + 20));
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
        if (clsid == ZCLSID_HID_DEVICE) { /* IHIDDevice_GetDeviceStatus (slot 4) */
            if (g_cpu.r[1]) zmem_write32(g_cpu.r[1], 1);
            g_cpu.r[0] = 0;
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 5:
        if (clsid == ZCLSID_DEVICE_BITMAP) {
            /* IBitmap_DrawPixel(po, x, y, native [, rop]) */
            int x = (int)g_cpu.r[1], y = (int)g_cpu.r[2];
            uint32_t color = g_cpu.r[3];
            uint32_t bits = zmem_read32(obj + 8);
            int w = (int)zmem_read16(obj + 20);
            int h = (int)zmem_read16(obj + 22);
            int pitch = (int)zmem_read16(obj + 24);
            int bpp = idib_depth_bytes(obj);
            if (color != 0xFFFFFFFFu && bits && x >= 0 && y >= 0 &&
                x < w && y < h) {
                uint32_t rop = zmem_read32(g_cpu.r[REG_SP]);
                if (bpp == 2) {
                    uint16_t c16 = (uint16_t)color;
                    uint32_t a = bits + (uint32_t)y * (uint32_t)pitch +
                                 (uint32_t)x * 2u;
                    if (rop == 1u)
                        c16 ^= zmem_read16(a);
                    zmem_write16(a, c16);
                } else {
                    uint32_t a = bits + (uint32_t)y * (uint32_t)pitch +
                                 (uint32_t)x * 4u;
                    if (rop == 1u)
                        color ^= zmem_read32(a);
                    zmem_write32(a, color);
                }
            }
            g_cpu.r[0] = 0;
            break;
        }
        if (clsid == AEECLSID_QEGL_REAL) {
            /* IQEGL_Initialize / DE wrapper: out em SP0, *out==1
             * Bad Dudes (0xE534): espera R0==1 em vez de 0. */
            uint32_t out = zbrew_stack_arg(0);
            if (g_cpu.r[2]) zmem_write32(g_cpu.r[2], 1); /* major */
            if (g_cpu.r[3]) zmem_write32(g_cpu.r[3], 1); /* minor */
            if (out) zmem_write32(out, 1);
            LOGI("IQEGL_Initialize: out=0x%08X -> 1", out);
            g_cpu.r[0] = 1;
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
        } else if (clsid == AEECLSID_HID_REAL) {
            /* IHID_GetNextConnectEvent (slot 5).
             * Alice nunca chama RegisterForConnectEvents: precisa de 1
             * evento connect com handle do joystick pra chegar em
             * CreateDevice. Jogos com sinal (FP/AH/DD) ja CreateDevice
             * via callback — nao entregar evento extra (loop DD). */
            if (g_hid_connect_event_avail && !g_hid_connect_signal) {
                /* Alice: *phDevice=handle, *ppdevInfo=NULL — CreateDevice
                 * so dispara se ppdevInfo fica 0 (ver .mod @0x599F4). */
                g_hid_connect_event_avail = false;
                if (g_cpu.r[1]) zmem_write32(g_cpu.r[1], ZHID_HANDLE);
                if (g_cpu.r[2]) zmem_write32(g_cpu.r[2], 0);
                if (g_cpu.r[3]) zmem_write8(g_cpu.r[3], 1); /* connected */
                LOGI("IHID_GetNextConnectEvent -> handle=0x%08X (synthetic)",
                     ZHID_HANDLE);
                g_cpu.r[0] = 0;
            } else {
                if (g_cpu.r[1]) zmem_write32(g_cpu.r[1], 0);
                if (g_cpu.r[2]) zmem_write32(g_cpu.r[2], 0);
                if (g_cpu.r[3]) zmem_write8(g_cpu.r[3], 0);
                g_cpu.r[0] = 5; /* empty queue (zeemu) */
            }
        } else if (clsid == ZCLSID_HID_DEVICE) {
            /* IHIDDevice_RegisterForStatusChange (slot 5): noop SUCCESS.
             * Antes caia no stub generico e criava objetos fantasmas. */
            g_cpu.r[0] = 0;
        } else {
            /* CLSID desconhecido em method 5: criar stub generico e preencher
             * saidas para evitar lixo em R1/R2/R3 (ex: Z-Wheel 0x0100104F). */
            uint32_t obj1 = make_stub_interface(clsid);
            if (obj1) {
                if (g_cpu.r[1]) zmem_write32(g_cpu.r[1], obj1);
                if (g_cpu.r[2]) zmem_write32(g_cpu.r[2], obj1);
                if (g_cpu.r[3]) zmem_write32(g_cpu.r[3], obj1);
                zmem_write32(obj1 + 8, g_shell_obj);
                zmem_write32(obj1 + 12, g_module_obj);
            }
            LOGW("CreateInstance case 5: CLSID desconhecido 0x%08X -> stub 0x%08X",
                 clsid, obj1);
            g_cpu.r[0] = obj1 ? 0 : 1;
        }
        break;
    case 6:
        if (clsid == AEECLSID_QEGL_REAL) {
            /* IQEGL_Terminate(po, dpy, pRet) — zeemu: *pRet=1 SUCCESS */
            uint32_t out = g_cpu.r[2] ? g_cpu.r[2] : zbrew_stack_arg(0);
            if (out && out < ZDISPLAY_ADDR_MAX)
                zmem_write32(out, 1);
            LOGI("IQEGL_Terminate: dpy=0x%08X out=0x%08X", g_cpu.r[1], out);
            g_cpu.r[0] = 0;
            break;
        }
        if (clsid == AEECLSID_HID_REAL) { /* IHID_RegisterForConnectEvents */
            zboot_signal_register(&g_hid_connect_signal, g_cpu.r[1]);
            g_hid_connect_pending = g_hid_connect_signal != 0;
            LOGI("IHID_RegisterForConnectEvents signal=0x%08X",
                 g_hid_connect_signal);
            g_cpu.r[0] = 0;
            break;
        }
        if (clsid == ZCLSID_HID_DEVICE) { /* IHIDDevice_GetButtonInfo (slot 6) */
            /* R1=id R2=pInfo. Layout AEEHIDButtonInfo = zeemu write_button_info. */
            uint32_t id = g_cpu.r[1];
            uint32_t info = g_cpu.r[2];
            static const uint32_t k_uid[16] = {
                0x0106C40Au, 0x0106C40Bu, 0x0106C40Cu, 0x0106C40Du,
                0x0106C406u, 0x0106C408u, 0x0106C407u, 0x0106C409u,
                0x0106C403u, 0x0106C402u, 0x0106C404u, 0x0106C405u,
                0x0106C3FEu, 0x0106C3FFu, 0x0106C400u, 0x0106C401u,
            };
            if (!info || id >= 16u) {
                g_cpu.r[0] = 14;
                break;
            }
            zmem_write32(info + 0, id);
            zmem_write32(info + 4, 0);
            zmem_write32(info + 8, k_uid[id]);
            zmem_write32(info + 12, 0);
            zmem_write32(info + 16, 1);
            g_cpu.r[0] = 0;
            break;
        }
        if (clsid == 0x01001002u) { /* AEECLSID_GRAPHICS Init/SetRenderCallback */
            LOGI("GRAPHICS slot 6 (Init): obj=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X",
                 obj, g_cpu.r[1], g_cpu.r[2], g_cpu.r[3]);
            /* NAO injetar applet+0x6C = 0x2F440 para RE4: causa recursao
             * infinita no chamador 0x2F440 (ldr r1,[r0,#0x6C]; bx r1).
             * Renderizacao vem via IDisplay em applet+0x3C. */
            if (g_cpu.r[2]) zmem_write32(g_cpu.r[2], obj);
            /* RE4 Bio4_InitApp1 (0x4344) trata Init como bool: 0 = falha e
             * aborta antes de preencher applet+0x40. Outros jogos: AEE_SUCCESS. */
            g_cpu.r[0] = (g_applet_clsid == 0x0108AF6Cu) ? 1u : 0u;
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
            static uint32_t bitblt_logs = 0;

            if (is_device_bitmap_obj(src_ptr) && cx_dest > 0 && cy_dest > 0) {
                uint32_t bits = zmem_read32(src_ptr + 8);
                int sw = (int)zmem_read16(src_ptr + 20);
                int sh = (int)zmem_read16(src_ptr + 22);
                int spitch = (int)zmem_read16(src_ptr + 24);
                int sbpp = idib_depth_bytes(src_ptr);
                int xx, yy;
                for (yy = 0; yy < cy_dest; yy++) {
                    int sy = y_src + yy;
                    if (sy < 0 || sy >= sh) continue;
                    for (xx = 0; xx < cx_dest; xx++) {
                        int sx = x_src + xx;
                        uint32_t px;
                        if (sx < 0 || sx >= sw) continue;
                        if (sbpp == 2) {
                            uint16_t p16 = zmem_read16(
                                bits + (uint32_t)sy * (uint32_t)spitch +
                                (uint32_t)sx * 2u);
                            uint32_t r = ((p16 >> 11) & 0x1Fu) * 255u / 31u;
                            uint32_t g = ((p16 >> 5) & 0x3Fu) * 255u / 63u;
                            uint32_t b = (p16 & 0x1Fu) * 255u / 31u;
                            if (rop == 7u && p16 == 0)
                                continue;
                            px = 0xFF000000u | (r << 16) | (g << 8) | b;
                        } else {
                            px = zmem_read32(
                                bits + (uint32_t)sy * (uint32_t)spitch +
                                (uint32_t)sx * 4u);
                            if (rop == 7u /* AEE_RO_TRANSPARENT */ &&
                                px == 0xFF000000u)
                                continue;
                        }
                        display_fill_rect_xrgb(x_dest + xx, y_dest + yy, 1, 1, px);
                    }
                }
                zbrew_mark_frame();
                g_cpu.r[0] = 0;
            } else {
                if (cx_dest > 0 && cy_dest > 0 && bitblt_logs < 8) {
                    LOGW("IDisplay_BitBlt: fonte nao suportada src=0x%08X",
                          src_ptr);
                    bitblt_logs++;
                }
                g_cpu.r[0] = 0;
            }
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 7:
        if (clsid == AEECLSID_QEGL_REAL) {
            /* IQEGL_QueryString(po, dpy, name, pRet) — zeemu + DE.
             * R2=name (0x3055=EGL_EXTENSIONS), R3=out. */
            uint32_t name = g_cpu.r[2];
            uint32_t out = g_cpu.r[3];
            uint32_t ret = 0;
            uint32_t old0 = g_cpu.r[0], old1 = g_cpu.r[1];
            g_cpu.r[1] = name;
            zegl_handle(7); /* IEGL_QueryString */
            ret = g_cpu.r[0];
            g_cpu.r[0] = old0;
            g_cpu.r[1] = old1;
            if (out >= 0x10000000u && out < 0x40000000u)
                zmem_write32(out, ret);
            LOGI("IQEGL_QueryString name=0x%04X out=0x%08X -> 0x%08X",
                 name, out, ret);
            g_cpu.r[0] = 0;
            break;
        }
        if (clsid == AEECLSID_THREAD_REAL) {
            /* Slot 7 = Start(po, dwStackSize, pfn, pUser) — zeemu. */
            g_thread_obj = g_cpu.r[0];
            g_thread_stack_size = g_cpu.r[1];
            g_thread_fn = g_cpu.r[2];
            g_thread_user = g_cpu.r[3];
            g_thread_stack_base = 0;
            g_thread_started = false;
            g_thread_alive = false;
            g_thread_ready = true;
            g_thread_resume_pending = false;
            g_thread_context_saved = false;
            /* ARMCC Data East: o entry testa a flag de execucao via
             *   ldr r6,[pc,#imm] ... ldrb r3,[r6]
             * Native APP_START zera a flag (strb) antes do scheduler; HLE
             * salta isso. Literal no .mod (ex. 0x212D54) ja vem relocado
             * pelo PIC (ex. 0x212DF0). Sem CLSID/endereco fixo. */
            {
                uint32_t op = zmem_read32(g_thread_fn + 0x14u);
                if ((op & 0xFFFFF000u) == 0xE59F6000u) {
                    uint32_t literal = g_thread_fn + 0x1Cu + (op & 0xFFFu);
                    uint32_t flag = zmem_read32(literal);
                    uint32_t ldrb = zmem_read32(g_thread_fn + 0x70u);
                    if ((ldrb & 0xFFFFF000u) == 0xE5D63000u &&
                        flag < ZMEM_RAM_SIZE) {
                        /* flag!=0 = idle GetResumeCBK→Resume→Suspend;
                         * flag==0 = bl work. APP_START ja zera via strb. */
                        zmem_write8(flag, 0);
                        LOGI("IThread_Start: ARMCC work flag 0x%08X = 0", flag);
                    }
                }
            }
            LOGI("IThread_Start(stack=0x%08X pfn=0x%08X user=0x%08X) -> SUCCESS",
                 g_thread_stack_size, g_thread_fn, g_thread_user);
            g_cpu.r[0] = 0;
            break;
        }
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
        if (clsid == AEECLSID_THREAD_REAL) {
            /* Exit: so mata o slice se chamado DE DENTRO da thread guest
             * (zeemu PC=DEADBEE0). Ports Data East chamam slot 8 tambem
             * do HandleEvent ("suspend enter") — la e so SUCCESS. */
            LOGI("IThread_Exit obj=0x%08X in_thread=%d", obj,
                 g_in_thread_guest ? 1 : 0);
            g_cpu.r[0] = 0;
            if (g_in_thread_guest) {
                g_thread_alive = false;
                g_thread_ready = false;
                g_thread_resume_pending = false;
                g_thread_context_saved = false;
                g_cpu.r[REG_PC] = ZTRAP_ADDR(ZT_GUEST_RETURN);
                zbrew_trap_skip_return();
            }
            break;
        }
        if (clsid == AEECLSID_QEGL_REAL) {
            /* GetConfigs / DE: *out=1 */
            uint32_t out = zbrew_stack_arg(0);
            if (g_cpu.r[1]) zmem_write32(g_cpu.r[1], 1);
            if (out) zmem_write32(out, 1);
            g_cpu.r[0] = 0;
            break;
        }
        if (clsid == ZCLSID_HID_DEVICE) {
            zboot_signal_register(&g_hid_button_signal, g_cpu.r[1]);
            LOGI("IHIDDevice_RegisterForButtonEvent signal=0x%08X",
                 g_hid_button_signal);
        }
        g_cpu.r[0] = 0;
        break;
    case 9:
        if (clsid == AEECLSID_THREAD_REAL) {
            /* Join: noop SUCCESS (single-slice HLE). */
            g_cpu.r[0] = 0;
            break;
        }
        if (clsid == AEECLSID_QEGL_REAL) {
            /* IQEGL_ChooseConfig(po, dpy, attribs, pConfigs, config_size,
             *                   pNumConfig, pRet) — zeemu BrewEGL.cpp.
             * R2=attribs (NAO gravar). R3=pConfigs. SP1=pNum SP2=pRet. */
            uint32_t p_configs = g_cpu.r[3];
            uint32_t p_num = zbrew_stack_arg(1);
            uint32_t p_ret = zbrew_stack_arg(2);
            if (p_configs && p_configs < ZDISPLAY_ADDR_MAX)
                zmem_write32(p_configs, 1);
            if (p_num && p_num < ZDISPLAY_ADDR_MAX)
                zmem_write32(p_num, 1);
            if (p_ret && p_ret < ZDISPLAY_ADDR_MAX)
                zmem_write32(p_ret, 1);
            LOGI("IQEGL_ChooseConfig: configs=0x%08X num=0x%08X ret=0x%08X",
                 p_configs, p_num, p_ret);
            g_cpu.r[0] = 0;
            break;
        }
        if (clsid == ZCLSID_HID_DEVICE) {
            uint32_t info = g_cpu.r[1];
            uint32_t eid = 0, euid = 0;
            bool edown = false;
            if (!hid_evt_pop(&eid, &euid, &edown)) {
                g_cpu.r[0] = 5;
                break;
            }
            if (info) {
                zmem_write32(info + 0, eid);
                zmem_write32(info + 4, edown ? 1 : 0);
                zmem_write32(info + 8, euid);
                zmem_write32(info + 12, 0);
                zmem_write32(info + 16, 1);
            }
            if (g_cpu.r[2]) zmem_write32(g_cpu.r[2], 0);
            if (g_cpu.r[3]) zmem_write8(g_cpu.r[3], 0);
            g_cpu.r[0] = 0;
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 10:
        if (clsid == AEECLSID_THREAD_REAL) {
            /* Suspend: yield so de dentro da thread. Fora = SUCCESS. */
            LOGI("IThread_Suspend obj=0x%08X in_thread=%d SP=0x%08X",
                 obj, g_in_thread_guest ? 1 : 0, g_cpu.r[REG_SP]);
            g_cpu.r[0] = 0;
            if (g_in_thread_guest) {
                g_thread_saved_cpu = g_cpu;
                g_thread_saved_cpu.r[REG_PC] = g_cpu.r[REG_LR];
                g_thread_context_saved = true;
                g_thread_ready = false;
                g_cpu.r[REG_PC] = ZTRAP_ADDR(ZT_GUEST_RETURN);
                zbrew_trap_skip_return();
            }
            break;
        }
        if (clsid == ZCLSID_HID_DEVICE) { /* GetPositionState(po, pInfo, pNumAxes) */
            uint32_t p_pos = g_cpu.r[1];
            if (p_pos && p_pos < ZDISPLAY_ADDR_MAX && p_pos != g_cpu.r[0])
                zinput_hid_fill_position(p_pos, g_hid_axis_lx, g_hid_axis_ly,
                                         g_hid_axis_rx, g_hid_axis_ry);
            if (g_cpu.r[2] && g_cpu.r[2] < ZDISPLAY_ADDR_MAX &&
                g_cpu.r[2] != g_cpu.r[0])
                zmem_write32(g_cpu.r[2], 4);
            g_cpu.r[0] = 0;
            break;
        }
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
        if (clsid == ZCLSID_DEVICE_BITMAP) {
            /* IBitmap_BltIn(po, xDst, yDst, cx, cy, pSrc, xSrc, ySrc [, rop])
             * zeemu BrewBitmapBlit: R1/R2/R3 + stack cy/pSrc/xSrc/ySrc. */
            int dst_x = (int32_t)g_cpu.r[1];
            int dst_y = (int32_t)g_cpu.r[2];
            int copy_w = (int32_t)g_cpu.r[3];
            uint32_t base = display_find_arg_base();
            int copy_h = (int32_t)zmem_read32(base + 0);
            uint32_t src_obj = zmem_read32(base + 4);
            int src_x = (int32_t)zmem_read32(base + 8);
            int src_y = (int32_t)zmem_read32(base + 12);
            uint32_t dbits = zmem_read32(obj + 8);
            int dw = (int)zmem_read16(obj + 20);
            int dh = (int)zmem_read16(obj + 22);
            int dpitch = (int)zmem_read16(obj + 24);
            int ddepth = idib_depth_bytes(obj);
            static uint32_t bltin_logs = 0;
            if (is_device_bitmap_obj(src_obj) && src_obj && dbits &&
                copy_w > 0 && copy_h > 0) {
                uint32_t sbits = zmem_read32(src_obj + 8);
                int sw = (int)zmem_read16(src_obj + 20);
                int sh = (int)zmem_read16(src_obj + 22);
                int spitch = (int)zmem_read16(src_obj + 24);
                int sdepth = idib_depth_bytes(src_obj);
                int yy, xx;
                if (bltin_logs++ < 16)
                    LOGI("IBitmap_BltIn dst=0x%08X (%d,%d %dx%d d=%d) "
                         "src=0x%08X (%d,%d s=%d)",
                         obj, dst_x, dst_y, copy_w, copy_h, ddepth,
                         src_obj, src_x, src_y, sdepth);
                if (sbits && spitch > 0 && dpitch > 0) {
                    for (yy = 0; yy < copy_h; yy++) {
                        int sy = src_y + yy, dy = dst_y + yy;
                        if (sy < 0 || dy < 0 || sy >= sh || dy >= dh)
                            continue;
                        for (xx = 0; xx < copy_w; xx++) {
                            int sx = src_x + xx, dx = dst_x + xx;
                            if (sx < 0 || dx < 0 || sx >= sw || dx >= dw)
                                continue;
                            if (ddepth == 2 && sdepth == 2) {
                                uint16_t px = zmem_read16(
                                    sbits + (uint32_t)sy * (uint32_t)spitch +
                                    (uint32_t)sx * 2u);
                                zmem_write16(
                                    dbits + (uint32_t)dy * (uint32_t)dpitch +
                                    (uint32_t)dx * 2u, px);
                            } else if (ddepth == 4 && sdepth == 4) {
                                uint32_t px = zmem_read32(
                                    sbits + (uint32_t)sy * (uint32_t)spitch +
                                    (uint32_t)sx * 4u);
                                zmem_write32(
                                    dbits + (uint32_t)dy * (uint32_t)dpitch +
                                    (uint32_t)dx * 4u, px);
                            } else if (ddepth == 4 && sdepth == 2) {
                                uint16_t p16 = zmem_read16(
                                    sbits + (uint32_t)sy * (uint32_t)spitch +
                                    (uint32_t)sx * 2u);
                                uint32_t r = ((p16 >> 11) & 0x1Fu) * 255u / 31u;
                                uint32_t g = ((p16 >> 5) & 0x3Fu) * 255u / 63u;
                                uint32_t b = (p16 & 0x1Fu) * 255u / 31u;
                                zmem_write32(
                                    dbits + (uint32_t)dy * (uint32_t)dpitch +
                                    (uint32_t)dx * 4u,
                                    0xFF000000u | (r << 16) | (g << 8) | b);
                            }
                        }
                    }
                    g_cpu.r[0] = 0;
                } else {
                    g_cpu.r[0] = 4;
                }
            } else {
                if (bltin_logs++ < 8)
                    LOGW("IBitmap_BltIn unsupported dst=0x%08X src=0x%08X "
                         "%dx%d",
                         obj, src_obj, copy_w, copy_h);
                g_cpu.r[0] = 4;
            }
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 11: /* GetMinPositionInfo(po, pPos) - todos os eixos zerados */
        if (clsid == AEECLSID_THREAD_REAL) {
            /* GetResumeCBK: AEECallback com pfnNotify@+16, pNotifyData@+20.
             * IShell_Resume(cbk) reativa a thread (zeemu). */
            if (!g_thread_resume_cbk) {
                g_thread_resume_cbk = zheap_alloc(24);
                if (g_thread_resume_cbk)
                    memset((void *)zmem_host_ptr(g_thread_resume_cbk, 24), 0, 24);
            }
            if (g_thread_resume_cbk) {
                zmem_write32(g_thread_resume_cbk + 16,
                             ZTRAP_ADDR(ZT_THREAD_RESUME_CALLBACK));
                zmem_write32(g_thread_resume_cbk + 20, obj);
            }
            LOGI("IThread_GetResumeCBK -> 0x%08X (thread=0x%08X)",
                 g_thread_resume_cbk, obj);
            g_cpu.r[0] = g_thread_resume_cbk;
            break;
        }
        if (clsid == AEECLSID_QEGL_REAL) {
            /* Zeemu: CreateWindowSurface(dpy, cfg, win, …, pSurf@SP1).
             * DE: CreateGL — R3 nao e IDIB. */
            uint32_t out = zbrew_stack_arg(1);
            if (is_device_bitmap_obj(g_cpu.r[3]) ||
                is_device_bitmap_obj(g_cpu.r[2])) {
                uint32_t surf = g_qegl_surface ? g_qegl_surface
                                               : zegl_create_qegl_surface();
                g_qegl_surface = surf;
                if (out && out < ZDISPLAY_ADDR_MAX)
                    zmem_write32(out, surf);
                LOGI("IQEGL_CreateWindowSurface: win=0x%08X out=0x%08X -> 0x%08X",
                     g_cpu.r[3], out, surf);
            } else {
                uint32_t gl = zgl_create_igles_interface();
                if (out && out < ZDISPLAY_ADDR_MAX)
                    zmem_write32(out, gl);
                LOGI("IQEGL_slot11(CreateGL): out=0x%08X gl=0x%08X", out, gl);
            }
            g_cpu.r[0] = 0;
            break;
        }
        if (clsid == ZCLSID_HID_DEVICE) {
            uint32_t p_pos = g_cpu.r[1];
            if (p_pos && p_pos < ZDISPLAY_ADDR_MAX)
                zinput_hid_fill_min_position(p_pos);
            g_cpu.r[0] = 0;
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 12: /* GetMaxPositionInfo(po, pInfo) - eixos em 0xFFFF */
        if (clsid == ZCLSID_HID_DEVICE) {
            uint32_t p_pos = g_cpu.r[1];
            if (p_pos && p_pos < ZDISPLAY_ADDR_MAX)
                zinput_hid_fill_max_position(p_pos);
            g_cpu.r[0] = 0;
            break;
        }
        if (clsid == ZCLSID_DEVICE_BITMAP) {
            /* IBitmap_GetInfo(po, pInfo, nSize) — zeemu BrewBitmapInfo:
             * AEEBitmapInfo = { cx, cy, nDepth } (3x uint32). nDepth em
             * BITS (API); IDIB+28 guarda BYTES. */
            uint32_t pinfo = g_cpu.r[1];
            uint32_t nsize = g_cpu.r[2];
            uint32_t cx = zmem_read16(obj + 20);
            uint32_t cy = zmem_read16(obj + 22);
            uint32_t depth = (uint32_t)idib_depth_bytes(obj) * 8u;
            if (!cx) cx = 640;
            if (!cy) cy = 480;
            if (!depth) depth = 32;
            if (pinfo && pinfo < ZDISPLAY_ADDR_MAX && nsize >= 12) {
                zmem_write32(pinfo + 0, cx);
                zmem_write32(pinfo + 4, cy);
                zmem_write32(pinfo + 8, depth);
                g_cpu.r[0] = 0;
            } else {
                g_cpu.r[0] = 4; /* EBADPARM */
            }
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 13:
        if (clsid == ZCLSID_HID_DEVICE) { /* GetAxesInfo(po, pAxes) */
            uint32_t p_axes = g_cpu.r[1];
            if (p_axes && p_axes < ZDISPLAY_ADDR_MAX)
                zinput_hid_fill_axes_info(p_axes);
            g_cpu.r[0] = 0;
            break;
        }
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
        if (clsid == ZCLSID_DEVICE_BITMAP) {
            /* IBitmap_CreateCompatibleBitmap(po, ppBmp, w, h) — AH boot.
             * Depth herda do this (bytes 2/4) — zeemu BrewBitmap. */
            uint32_t pp = g_cpu.r[1];
            int w = (int)(g_cpu.r[2] & 0xFFFFu);
            int h = (int)(g_cpu.r[3] & 0xFFFFu);
            int depth = idib_depth_bytes(obj);
            uint32_t dib;
            dib = zbrew_create_device_bitmap(w, h, depth);
            if (pp && pp < ZDISPLAY_ADDR_MAX) zmem_write32(pp, dib);
            LOGI("IBitmap_CreateCompatibleBitmap: %dx%d d=%d -> 0x%08X",
                 w, h, depth, dib);
            g_cpu.r[0] = dib ? 0 : 8;
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 14:
        if (clsid == ZCLSID_HID_DEVICE) {
            zboot_signal_register(&g_hid_position_signal, g_cpu.r[1]);
            LOGI("IHIDDevice_RegisterForPositionChange signal=0x%08X",
                 g_hid_position_signal);
            g_cpu.r[0] = 0;
            break;
        }
        if (is_display_interface(clsid)) { /* SetDestination(po, pIDIB) */
            uint32_t dest = g_cpu.r[1];
            if (!dest) {
                g_display_dest_obj = 0; /* volta para o dispositivo (VRAM) */
            } else if (is_device_bitmap_obj(dest)) {
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
        if (clsid == AEECLSID_QEGL_REAL) {
            /* Zeemu: CreateContext(dpy, cfg, share, …, pCtx@SP1) -> 3.
             * DE wrapper: surface em SP1. Heuristica: cfg pequeno + share=0. */
            uint32_t out = zbrew_stack_arg(1);
            if (g_cpu.r[2] <= 16u && g_cpu.r[3] == 0) {
                uint32_t ctx = 3;
                if (out && out < ZDISPLAY_ADDR_MAX)
                    zmem_write32(out, ctx);
                LOGI("IQEGL_CreateContext: cfg=%u out=0x%08X -> %u",
                     g_cpu.r[2], out, ctx);
            } else {
                uint32_t surf = g_qegl_surface ? g_qegl_surface
                                               : zegl_create_qegl_surface();
                if (out && out < ZDISPLAY_ADDR_MAX)
                    zmem_write32(out, surf);
                LOGI("IQEGL_slot16: out=0x%08X surf=0x%08X", out, surf);
            }
            g_cpu.r[0] = 0;
            break;
        }
        if (is_display_interface(clsid)) { /* GetDeviceBitmap */
            uint32_t dib = zbrew_device_bitmap();
            if (g_cpu.r[1]) zmem_write32(g_cpu.r[1], dib);
            LOGI("IDisplay_GetDeviceBitmap -> 0x%08X", dib);
            g_cpu.r[0] = dib ? 0 : 1;
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 17:
        if (clsid == AEECLSID_QEGL_REAL) {
            /* Zeemu DestroyContext(dpy, ctx, pRet@SP0): ctx tipicamente
             * pequeno (CreateContext -> 3). DE GetNativeContext: R0=handle. */
            if (g_cpu.r[2] != 0 && g_cpu.r[2] < 0x1000u) {
                uint32_t p_ret = zbrew_stack_arg(0);
                if (p_ret && p_ret < ZDISPLAY_ADDR_MAX)
                    zmem_write32(p_ret, 1);
                LOGI("IQEGL_DestroyContext: ctx=0x%08X ret=0x%08X",
                     g_cpu.r[2], p_ret);
                g_cpu.r[0] = 0;
            } else {
                g_cpu.r[0] = 0x1001F000;
                LOGI("IQEGL_GetNativeContext -> 0x%08X", g_cpu.r[0]);
            }
            break;
        }
        g_cpu.r[0] = 0;
        break;
    case 18:
        if (clsid == AEECLSID_QEGL_REAL) {
            /* Zeemu MakeCurrent: pRet@SP1 = 1 */
            uint32_t out = zbrew_stack_arg(1);
            if (out && out < ZDISPLAY_ADDR_MAX)
                zmem_write32(out, 1);
            LOGI("IQEGL_MakeCurrent: dpy=0x%08X draw=0x%08X read=0x%08X out=0x%08X",
                 g_cpu.r[1], g_cpu.r[2], g_cpu.r[3], out);
            g_cpu.r[0] = 0;
            break;
        }
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
    case 25:
        if (clsid == AEECLSID_QEGL_REAL) {
            /* IQEGL_SwapBuffers(po, surf, gl [, out]) — Zuma apos o
             * clear fullscreen. Marca frame; nao limpa o FB. */
            uint32_t out = g_cpu.r[3];
            if (!(out >= 0x2F000000u && out < 0x30000000u))
                out = zbrew_stack_arg(0);
            if (out >= 0x2F000000u && out < 0x30000000u)
                zmem_write32(out, 1);
            zbrew_mark_frame();
            {
                static uint32_t sw = 0;
                if (sw++ < 8)
                    LOGI("IQEGL_SwapBuffers surf=0x%08X gl=0x%08X",
                         g_cpu.r[1], g_cpu.r[2]);
            }
            g_cpu.r[0] = 0;
            break;
        }
        g_cpu.r[0] = 0;
        break;
    /* ---- IDisplay metodos estendidos (slots >= 20) ----
     * RE4 chama metodos 0x4B (75), 0x54 (84), 0x66 (102) na vtable do
     * IDisplay (applet+0x3C). Na convencao do stub: R0=this, R1/R2/R3 =
     * args 1-3, stack = args 4+. */
    case 0x4B: /* 75 - provavelmente ClearScreen */
        if (is_display_interface(clsid)) {
            zfb_clear(0xFF000000u | g_display_background_color);
            zbrew_mark_frame();
            if (warn_count < 16)
                LOGI("IDisplay_EXT[75] ClearScreen obj=0x%08X bg=0x%08X",
                     obj, g_display_background_color);
            g_cpu.r[0] = 0;
        } else {
            g_cpu.r[0] = 0;
        }
        break;
    case 0x54: /* 84 - provavelmente FillRect/DrawRect: R1=x R2=y R3=w [SP]=h */
        if (is_display_interface(clsid)) {
            int x = (int)g_cpu.r[1];
            int y = (int)g_cpu.r[2];
            int w = (int)g_cpu.r[3];
            int h = (int)zbrew_stack_arg(0);
            display_fill_rect(x, y, w, h, g_display_background_color);
            zbrew_mark_frame();
            if (warn_count < 16)
                LOGI("IDisplay_EXT[84] FillRect obj=0x%08X x=%d y=%d w=%d h=%d bg=0x%08X",
                     obj, x, y, w, h, g_display_background_color);
            g_cpu.r[0] = 0;
        } else {
            g_cpu.r[0] = 0;
        }
        break;
    case 0x66: /* 102 - provavelmente BitBlt: R1=src R2=w R3=h [SP]=x [SP+4]=y */
        if (is_display_interface(clsid)) {
            uint32_t src = g_cpu.r[1];
            int w = (int)g_cpu.r[2];
            int h = (int)g_cpu.r[3];
            int x_dest = (int)zbrew_stack_arg(0);
            int y_dest = (int)zbrew_stack_arg(1);
            if (is_device_bitmap_obj(src) && w > 0 && h > 0) {
                uint32_t bits = zmem_read32(src + 8);
                int sw = (int)zmem_read16(src + 20);
                int sh = (int)zmem_read16(src + 22);
                int spitch = (int)zmem_read16(src + 24);
                int sbpp = idib_depth_bytes(src);
                int xx, yy;
                for (yy = 0; yy < h; yy++) {
                    int sy = yy;
                    if (sy < 0 || sy >= sh) continue;
                    for (xx = 0; xx < w; xx++) {
                        int sx = xx;
                        uint32_t px;
                        if (sx < 0 || sx >= sw) continue;
                        if (sbpp == 2) {
                            uint16_t p16 = zmem_read16(
                                bits + (uint32_t)sy * (uint32_t)spitch +
                                (uint32_t)sx * 2u);
                            uint32_t r = ((p16 >> 11) & 0x1Fu) * 255u / 31u;
                            uint32_t g = ((p16 >> 5) & 0x3Fu) * 255u / 63u;
                            uint32_t b = (p16 & 0x1Fu) * 255u / 31u;
                            px = 0xFF000000u | (r << 16) | (g << 8) | b;
                        } else {
                            px = zmem_read32(
                                bits + (uint32_t)sy * (uint32_t)spitch +
                                (uint32_t)sx * 4u);
                        }
                        display_fill_rect_xrgb(x_dest + xx, y_dest + yy, 1, 1, px);
                    }
                }
                zbrew_mark_frame();
            }
            if (warn_count < 16)
                LOGI("IDisplay_EXT[102] BitBlt obj=0x%08X src=0x%08X "
                     "w=%d h=%d dst=(%d,%d)", obj, src, w, h, x_dest, y_dest);
            g_cpu.r[0] = 0;
        } else {
            g_cpu.r[0] = 0;
        }
        break;
    default: {
        /* Extensoes EGL/GLES (SurfaceManip, Imageon, GLES1xEXT…): zeemu
         * grava *out=1 (bool) no ultimo arg e retorna 0. Sem isso os ports
         * Data East caem em InitGLExtensions failed apos o QI. */
        int is_gl_ext =
            clsid == AEECLSID_QEGL_REAL ||
            clsid == 0x010434CCu || clsid == 0x01051834u || /* SurfaceManip */
            clsid == 0x01058546u || clsid == 0x010459B1u || /* Imageon */
            clsid == 0x0103D8DEu || clsid == 0x0103D8EBu || /* GLES10/11EXT */
            clsid == 0x0103DEF1u ||                         /* GLES11EXTPAK */
            clsid == 0x0103D8EDu || clsid == 0x0103D8EEu ||
            clsid == 0x0103D8EFu || clsid == 0x0103D8F0u ||
            clsid == 0x010426E3u || clsid == 0x0109B10Eu;
        if (is_gl_ext) {
            /* So escreve *out=1 em ponteiros da STACK guest (bool ret do
             * zeemu). Escrever no heap corrompe objetos (visto: SP=1 e
             * PC=malloc). */
            int i;
            for (i = 3; i >= 0; i--) {
                uint32_t p = zbrew_stack_arg((uint32_t)i);
                if (p >= 0x2F000000u && p < 0x30000000u) {
                    zmem_write32(p, 1);
                    if (warn_count < 32) {
                        LOGI("GLext clsid=0x%08X slot=%u out@SP%d=0x%08X <- 1",
                             clsid, slot, i, p);
                        warn_count++;
                    }
                    break;
                }
            }
            g_cpu.r[0] = 0;
            break;
        }
        g_cpu.r[0] = 0;
        if (warn_count < 32) {
            LOGW("stub: clsid=0x%08X metodo=%u r0=0x%08X r1=0x%08X "
                 "r2=0x%08X r3=0x%08X sp0=0x%08X PC=0x%08X LR=0x%08X",
                 clsid, slot, obj, g_cpu.r[1], g_cpu.r[2], g_cpu.r[3],
                 zbrew_stack_arg(0), g_cpu.r[REG_PC], g_cpu.r[REG_LR]);
            warn_count++;
        }
        break;
    }
    }
}
