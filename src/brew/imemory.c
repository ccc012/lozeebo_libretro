/* imemory.c - MALLOC/FREE/REALLOC/MEMSET/MEMCPY do BREW */
#include <string.h>
#include "brew.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../debug/log.h"

void zbrew_handle_memory(uint32_t id) {
    switch (id) {
    case ZT_MALLOC:
        g_cpu.r[0] = zheap_alloc(g_cpu.r[0]);
        break;

    case ZT_FREE:
        zheap_free(g_cpu.r[0]);
        break;

    case ZT_REALLOC:
        g_cpu.r[0] = zheap_realloc(g_cpu.r[0], g_cpu.r[1]);
        break;

    case ZT_MEMSET: {
        uint32_t dst = g_cpu.r[0];
        uint8_t  val = (uint8_t)g_cpu.r[1];
        uint32_t len = g_cpu.r[2];
        void *p = zmem_host_ptr(dst, len);
        if (p) {
            memset(p, val, len);
        } else {
            uint32_t i;
            for (i = 0; i < len; i++)
                zmem_write8(dst + i, val);
        }
        break;
    }

    case ZT_MEMCPY: {
        uint32_t dst = g_cpu.r[0];
        uint32_t src = g_cpu.r[1];
        uint32_t len = g_cpu.r[2];
        void *pd = zmem_host_ptr(dst, len);
        const void *ps = zmem_host_ptr(src, len);
        if (pd && ps) {
            memmove(pd, ps, len);
        } else {
            uint32_t i;
            for (i = 0; i < len; i++)
                zmem_write8(dst + i, zmem_read8(src + i));
        }
        break;
    }
    }
}
