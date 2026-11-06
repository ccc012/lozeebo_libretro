/* isource.c - ISource/IAStream HLE para streaming de dados */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "brew.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../debug/log.h"

#define ZSOURCE_MAX 16

typedef struct {
    bool     active;
    uint32_t base_addr;
    uint32_t length;
    uint32_t position;
} zsource_t;

static zsource_t g_sources[ZSOURCE_MAX];

static zsource_t *source_alloc(void) {
    for (int i = 0; i < ZSOURCE_MAX; i++) {
        if (!g_sources[i].active) {
            memset(&g_sources[i], 0, sizeof(zsource_t));
            g_sources[i].active = true;
            return &g_sources[i];
        }
    }
    return NULL;
}

static zsource_t *source_from_obj(uint32_t obj) {
    if (obj == 0) return NULL;
    for (int i = 0; i < ZSOURCE_MAX; i++) {
        if (g_sources[i].active) {
            uint32_t source_obj = createinstance_cache_get(0x01001000u + i);
            if (source_obj == obj) return &g_sources[i];
        }
    }
    return NULL;
}

static uint32_t make_source_interface(void) {
    static const uint32_t vtbl[] = {
        ZT_SOURCE_ADDREF, ZT_SOURCE_RELEASE,
        ZT_SOURCE_READ, ZT_SOURCE_SEEK,
        ZT_SOURCE_GETLENGTH
    };
    uint32_t obj = zbrew_make_interface(vtbl, 5, 0);
    return obj;
}

void zbrew_handle_source(uint32_t id) {
    uint32_t slot = id - ZT_SOURCE_ADDREF;
    uint32_t obj = g_cpu.r[0];
    zsource_t *s = source_from_obj(obj);

    if (!s && slot != 0 && slot != 1) {
        g_cpu.r[0] = ZBREW_EFAILED;
        return;
    }

    switch (slot) {
    case 0: /* AddRef */
        g_cpu.r[0] = 1;
        break;

    case 1: /* Release */
        if (s) {
            s->active = false;
        }
        g_cpu.r[0] = 0;
        break;

    case 2: /* Read(buf, len) -> bytes read */
        if (s) {
            uint32_t buf = g_cpu.r[1];
            uint32_t len = g_cpu.r[2];
            uint32_t avail = s->length - s->position;
            uint32_t to_read = len < avail ? len : avail;

            if (to_read > 0 && buf && buf < 0xFF000000) {
                memcpy(zmem_host_ptr(buf, to_read),
                       zmem_host_ptr(s->base_addr + s->position, to_read),
                       to_read);
                s->position += to_read;
            }
            g_cpu.r[0] = to_read;
        } else {
            g_cpu.r[0] = 0;
        }
        break;

    case 3: /* Seek(whence, offset) */
        if (s) {
            int whence = (int)g_cpu.r[1];
            int32_t offset = (int32_t)g_cpu.r[2];
            switch (whence) {
            case 0: /* SEEK_SET */
                s->position = (offset >= 0 && (uint32_t)offset <= s->length) ? offset : 0;
                break;
            case 1: /* SEEK_CUR */
                if ((int32_t)s->position + offset >= 0 &&
                    (int32_t)s->position + offset <= (int32_t)s->length) {
                    s->position += offset;
                }
                break;
            case 2: /* SEEK_END */
                if (offset <= 0 && (uint32_t)(-offset) <= s->length) {
                    s->position = s->length + offset;
                }
                break;
            }
            g_cpu.r[0] = 0;
        } else {
            g_cpu.r[0] = 14;
        }
        break;

    case 4: /* GetLength */
        if (s) {
            g_cpu.r[0] = s->length;
        } else {
            g_cpu.r[0] = 0;
        }
        break;

    default:
        g_cpu.r[0] = ZBREW_EFAILED;
        break;
    }
}

uint32_t zbrew_create_source_util(void) {
    static const uint32_t vtbl[] = {
        ZT_SOURCE_ADDREF, ZT_SOURCE_RELEASE,
        ZT_SOURCE_READ, ZT_SOURCE_SEEK,
        ZT_SOURCE_GETLENGTH
    };
    return zbrew_make_interface(vtbl, 5, 0);
}

uint32_t zsource_create_from_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    fseek(fp, 0, SEEK_END);
    uint32_t len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    zsource_t *s = source_alloc();
    if (!s) {
        fclose(fp);
        return 0;
    }

    s->length = len;
    s->base_addr = zheap_alloc(len);
    if (!s->base_addr) {
        s->active = false;
        fclose(fp);
        return 0;
    }

    fread(zmem_host_ptr(s->base_addr, len), 1, len, fp);
    fclose(fp);

    return make_source_interface();
}

uint32_t zsource_create_from_buffer(uint32_t addr, uint32_t len) {
    zsource_t *s = source_alloc();
    if (!s) return 0;

    s->base_addr = addr;
    s->length = len;

    return make_source_interface();
}