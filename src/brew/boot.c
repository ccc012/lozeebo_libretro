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
static uint32_t g_device_bitmap = 0;

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
    zmem_write32(obj, g_stub_vtbl);
    uint32_t vtbl_read = zmem_read32(obj);
    uint32_t vtbl0 = zmem_read32(vtbl_read);
    LOGI("stub obj=0x%08X vtbl=0x%08X vtbl[0]=0x%08X clsid=0x%08X",
         obj, vtbl_read, vtbl0, clsid);
    zmem_write32(obj + 4, clsid);
    return obj;
}

static bool is_known_applet(uint32_t clsid) {
    unsigned i;
    for (i = 0; i < ZAEE_KNOWN_APPLET_COUNT; i++) {
        if (ZAEE_KNOWN_APPLETS[i].clsid == clsid)
            return true;
    }
    return false;
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
    g_hid_button_signal = 0;
    g_hid_position_signal = 0;
    g_hid_event_pending = false;
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
        /* Diagnostico: struct AEEMod = {vtbl, IShell*, ph, pfnModCrInst} */
        LOGI("boot: AEEMod +0=0x%08X +4=0x%08X +8=0x%08X +12=0x%08X +16=0x%08X",
             zmem_read32(g_module_obj), zmem_read32(g_module_obj + 4),
             zmem_read32(g_module_obj + 8), zmem_read32(g_module_obj + 12),
             zmem_read32(g_module_obj + 16));
        /* Se o slot do IShell nao aponta para nosso objeto, corrige
         * (bootstraps ROPI variam a ordem dos args do AEEMod_Load) */
        if (zmem_read32(g_module_obj + 4) != g_shell_obj) {
            LOGW("boot: corrigindo AEEMod.m_pIShell (era 0x%08X)",
                 zmem_read32(g_module_obj + 4));
            zmem_write32(g_module_obj + 4, g_shell_obj);
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
        if (clsid == AEECLSID_DISPLAY_REAL) {
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
        if (clsid == AEECLSID_DISPLAY_REAL) {
            /* MeasureTextEx(po, font, AECHAR* psz, int nl, ...) -> R0=largura.
             * Aproximacao 8px/char ate a fonte real existir. */
            uint32_t psz = g_cpu.r[2];
            int32_t nl = (int32_t)g_cpu.r[3];
            uint32_t chars = 0;
            if (nl > 0) {
                chars = (uint32_t)nl;
            } else if (psz) {
                while (chars < 256 && zmem_read16(psz + chars * 2))
                    chars++;
            }
            g_cpu.r[0] = chars * 8;
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
        /* A classe 0x0100101C usada no bootstrap do Pac-Mania cria
         * dois sub-objetos de stub reais (como SignalCBFactory em case 3).
         * Diferente de simplesmente escrever NULL, aloca objetos stub
         * para evitar que o modulo fique preso em fallback nunca testados. */
        if (clsid == 0x0100101Cu) {
            uint32_t obj1 = make_stub_interface(0x0100101Cu);
            uint32_t obj2 = make_stub_interface(0x0100101Cu);
            if (g_cpu.r[2]) zmem_write32(g_cpu.r[2], obj1);
            if (g_cpu.r[3]) zmem_write32(g_cpu.r[3], obj2);
            LOGI("CreateInstance case 5 (0x0100101C) -> obj1=0x%08X obj2=0x%08X",
                 obj1, obj2);
        }
        g_cpu.r[0] = 0;
        break;
    case 7:
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
    case 14:
        if (clsid == ZCLSID_HID_DEVICE) {
            g_hid_position_signal = g_cpu.r[1];
            LOGI("IHIDDevice_RegisterForPositionChange signal=0x%08X",
                 g_hid_position_signal);
        }
        g_cpu.r[0] = 0;
        break;
    case 16:
        if (clsid == AEECLSID_DISPLAY_REAL) { /* GetDeviceBitmap */
            if (!g_device_bitmap) {
                g_device_bitmap = make_stub_interface(ZCLSID_DEVICE_BITMAP);
                if (g_device_bitmap) {
                    zmem_write32(g_device_bitmap + 8, ZMEM_VRAM_BASE);
                    zmem_write16(g_device_bitmap + 20, 640);
                    zmem_write16(g_device_bitmap + 22, 480);
                    zmem_write16(g_device_bitmap + 24, 640 * 2);
                    zmem_write8(g_device_bitmap + 28, 16);
                    zmem_write8(g_device_bitmap + 29, 16);
                }
            }
            if (g_cpu.r[1]) zmem_write32(g_cpu.r[1], g_device_bitmap);
            LOGI("IDisplay_GetDeviceBitmap -> 0x%08X", g_device_bitmap);
            g_cpu.r[0] = g_device_bitmap ? 0 : 1;
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
