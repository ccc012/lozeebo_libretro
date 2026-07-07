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
#include "brew.h"
#include "aee_ids.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
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
    BOOT_FAILED
};

static enum zboot_state g_state = BOOT_IDLE;
static uint32_t g_ppmod = 0;         /* IModule** passado ao AEEMod_Load */
static uint32_t g_module_obj = 0;
static uint32_t g_applet_out = 0;    /* void** para o applet */
static uint32_t g_applet_obj = 0;
static uint32_t g_applet_clsid = 0;
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
static uint32_t make_stub_interface(uint32_t clsid) {
    uint32_t obj = zheap_alloc(16);
    if (!g_stub_vtbl) {
        int i;
        g_stub_vtbl = zheap_alloc(64 * 4);
        if (!g_stub_vtbl) return 0;
        for (i = 0; i < 64; i++)
            zmem_write32(g_stub_vtbl + (uint32_t)i * 4,
                         ZTRAP_ADDR(ZT_STUB_BASE + i));
    }
    if (!obj) return 0;
    zmem_write32(obj, g_stub_vtbl);
    zmem_write32(obj + 4, clsid);
    LOGI("stub de interface para clsid 0x%08X em 0x%08X", clsid, obj);
    return obj;
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
    /* AEEMod_Load(IShell*, void* ph, IModule**) - R2 e R3 = ppMod
     * (alguns AEEModGen leem de R2, outros de R3) */
    guest_call(entry, g_shell_obj, 0, g_ppmod, g_ppmod);
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
        if (result != 0 || !g_applet_obj) {
            LOGE("boot: applet nao foi criado (clsid errado?)");
            g_state = BOOT_FAILED;
            g_cpu.halted = true;
            return;
        }
        uint32_t vtbl = zmem_read32(g_applet_obj);
        uint32_t handle_evt = zmem_read32(vtbl + 8);
        LOGI("boot: HandleEvent(EVT_APP_START) via 0x%08X", handle_evt);
        g_state = BOOT_APP_START;
        /* EVT_APP_START = 0 no Zeebo/BREW 1.1 */
        guest_call(handle_evt, g_applet_obj, 0, 0, 0);
        return;
    }

    case BOOT_APP_START:
        LOGI("boot: EVT_APP_START tratado (ret=0x%08X) - jogo RODANDO", result);
        g_state = BOOT_RUNNING;
        g_cpu.halted = true; /* espera timers/eventos */
        return;

    case BOOT_TIMER_CALL:
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

/* ifile.c/idisplay.c legados usam nossa convencao propria; para modulos
 * reais criamos stubs logados por enquanto e migramos um a um. */
static uint32_t real_create_instance(uint32_t clsid) {
    switch (clsid) {
        case AEECLSID_DISPLAY_REAL:
        case AEECLSID_GRAPHICS_REAL:
        case AEECLSID_FILEMGR_REAL:
        case AEECLSID_SOUNDPLAYER_R:
        case AEECLSID_MEDIA_REAL:
        case AEECLSID_MEDIAPCM_REAL:
        default:
            return make_stub_interface(clsid);
    }
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

    case 2: { /* CreateInstance(shell, clsid, ppObj) */
        uint32_t iface = 0;
        if (r1 == g_applet_clsid && g_applet_obj) {
            iface = g_applet_obj; /* auto-criacao do proprio applet */
        } else {
            iface = real_create_instance(r1);
        }
        if (r2) zmem_write32(r2, iface);
        LOGI("IShell_CreateInstance(0x%08X) -> 0x%08X", r1, iface);
        g_cpu.r[0] = iface ? 0 : 12 /* ECLASSNOTSUPPORT */;
        break;
    }

    case 4: { /* GetDeviceInfo(shell, pInfo) - layout AEEDeviceInfo */
        uint32_t p = r1;
        if (p) {
            zmem_write16(p + 0, 640);      /* cxScreen */
            zmem_write16(p + 2, 480);      /* cyScreen */
            zmem_write16(p + 4, 0);        /* cxAltScreen */
            zmem_write16(p + 6, 0);        /* cyAltScreen */
            zmem_write16(p + 8, 0);        /* cxScrollBar */
            zmem_write16(p + 10, 2);       /* wEncoding = UNICODE */
            zmem_write16(p + 12, 0);
            zmem_write16(p + 14, 32);      /* nColorDepth */
            zmem_write32(p + 16, 4);
            zmem_write32(p + 20, 0);
            zmem_write32(p + 24, ZMEM_HEAP_SIZE); /* dwRAM */
            zmem_write32(p + 28, 0);       /* flags */
            zmem_write32(p + 32, 0);
            zmem_write16(p + 36, 0);
            zmem_write16(p + 38, 0);
            zmem_write32(p + 40, 0x0409);  /* dwLang (en) */
            zmem_write16(p + 44, 64);      /* wStructSize */
            zmem_write32(p + 48, 0);
            zmem_write32(p + 52, 0);
            zmem_write16(p + 56, 128);     /* wMaxPath */
            zmem_write32(p + 60, 0x5A454542u); /* dwPlatformID 'ZEEB' */
        }
        g_cpu.r[0] = 0;
        break;
    }

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
        g_cpu.r[0] = 0;
        break;
    }

    case 21: /* SendEvent - devolve FALSE (nao tratado) */
        g_cpu.r[0] = 0;
        break;

    case 23: case 24: /* GetPrefs/SetPrefs */
        g_cpu.r[0] = 1; /* EFAILED */
        break;

    case 38: /* GetPosition */
        g_cpu.r[0] = 1;
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
    static uint32_t warn_count = 0;
    if (warn_count < 128) {
        LOGW("stub: metodo %u de interface 0x%08X chamado "
             "(R1=0x%08X R2=0x%08X)", slot,
             g_cpu.r[0] ? zmem_read32(g_cpu.r[0] + 4) : 0,
             g_cpu.r[1], g_cpu.r[2]);
        warn_count++;
    }
    /* AddRef/Release respondem 1; resto EFAILED */
    g_cpu.r[0] = (slot <= 1) ? 1 : 1;
}
