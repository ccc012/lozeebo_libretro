/* ishell.c - IShell: gerenciador central do BREW (CreateInstance etc) */
#include "brew.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../debug/log.h"

uint32_t zbrew_uptime_ms(void);

/* Cria a interface pedida pelo class ID e retorna o endereco emulado */
static uint32_t create_by_clsid(uint32_t clsid) {
    switch (clsid) {
    case AEECLSID_SHELL:
        return zbrew_shell_ptr();

    case AEECLSID_DISPLAY:
    case AEECLSID_GRAPHICS: {
        static const uint32_t vtbl[] = {
            ZT_DISP_ADDREF, ZT_DISP_RELEASE, ZT_DISP_UPDATE,
            ZT_DISP_CLEARSCREEN, ZT_DISP_SETCOLOR, ZT_DISP_FILLRECT,
            ZT_DISP_DRAWRECT, ZT_DISP_DRAWLINE, ZT_DISP_DRAWPIXEL,
            ZT_DISP_BITBLT
        };
        return zbrew_make_interface(vtbl, 10, 0);
    }

    case AEECLSID_BITMAP: {
        static const uint32_t vtbl[] = {
            ZT_BMP_ADDREF, ZT_BMP_RELEASE, ZT_BMP_GETINFO, ZT_BMP_BLT
        };
        /* dados: [w][h][addr_pixels] */
        return zbrew_make_interface(vtbl, 4, 3);
    }

    case AEECLSID_FILEMGR: {
        static const uint32_t vtbl[] = {
            ZT_FMGR_ADDREF, ZT_FMGR_RELEASE, ZT_FMGR_OPENFILE,
            ZT_FMGR_TEST, ZT_FMGR_REMOVE
        };
        return zbrew_make_interface(vtbl, 5, 0);
    }

    case AEECLSID_SOUND: {
        static const uint32_t vtbl[] = {
            ZT_SND_ADDREF, ZT_SND_RELEASE, ZT_SND_PLAY,
            ZT_SND_STOP, ZT_SND_SETVOLUME
        };
        return zbrew_make_interface(vtbl, 5, 0);
    }
    }
    return 0;
}

void zbrew_handle_shell(uint32_t id) {
    switch (id) {
    case ZT_SHELL_ADDREF:
    case ZT_SHELL_RELEASE:
        g_cpu.r[0] = 1; /* refcount ficticio */
        break;

    case ZT_SHELL_CREATEINSTANCE: {
        uint32_t clsid   = g_cpu.r[1];
        uint32_t ptr_out = g_cpu.r[2];
        uint32_t iface = create_by_clsid(clsid);
        if (iface) {
            zmem_write32(ptr_out, iface);
            g_cpu.r[0] = ZBREW_SUCCESS;
            LOGD("IShell_CreateInstance(0x%08X) -> 0x%08X", clsid, iface);
        } else {
            zmem_write32(ptr_out, 0);
            g_cpu.r[0] = ZBREW_ECLASSNOTSUPPORT;
            LOGW("IShell_CreateInstance: classe 0x%08X nao suportada", clsid);
        }
        break;
    }

    case ZT_SHELL_GETDEVICEINFO: {
        /* struct AEEDeviceInfo simplificada:
         * [0]=tamanho [4]=largura [8]=altura [12]=profundidade de cor */
        uint32_t info = g_cpu.r[1];
        zmem_write32(info, 16);
        zmem_write32(info + 4, 640);
        zmem_write32(info + 8, 480);
        zmem_write32(info + 12, 32);
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;
    }

    case ZT_SHELL_SETTIMER:
        LOGW("IShell_SetTimer nao implementado (callbacks: fase 3.5)");
        g_cpu.r[0] = ZBREW_EFAILED;
        break;

    case ZT_SHELL_GETUPTIMEMS:
        g_cpu.r[0] = zbrew_uptime_ms();
        break;
    }
}
