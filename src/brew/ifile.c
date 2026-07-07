/* ifile.c - IFileMgr/IFile: acesso a arquivos (assets do jogo)
 *
 * Mapeia nomes BREW para arquivos reais no diretorio da ROM.
 * Objeto IFile: [vtable][indice do handle host]
 * Seguranca: nomes com ".." ou caminho absoluto sao rejeitados.
 */
#include <stdio.h>
#include <string.h>
#include "brew.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../debug/log.h"

#define ZFILE_MAX 16
#define ZPATH_MAX 512

static FILE *g_handles[ZFILE_MAX];
static char  g_base_dir[ZPATH_MAX] = ".";

void zbrew_set_file_base(const char *dir) {
    if (dir && dir[0]) {
        strncpy(g_base_dir, dir, sizeof(g_base_dir) - 1);
        g_base_dir[sizeof(g_base_dir) - 1] = '\0';
    }
}

static bool name_ok(const char *name) {
    if (!name[0]) return false;
    if (strstr(name, "..")) return false;
    if (name[0] == '/' || name[0] == '\\') return false;
    if (strchr(name, ':')) return false;
    return true;
}

static void build_path(char *out, size_t outsz, const char *name) {
    snprintf(out, outsz, "%s/%s", g_base_dir, name);
}

void zbrew_handle_file(uint32_t id) {
    switch (id) {
    case ZT_FMGR_ADDREF:
    case ZT_FMGR_RELEASE:
    case ZT_FILE_ADDREF:
        g_cpu.r[0] = 1;
        break;

    case ZT_FMGR_OPENFILE: {
        char name[256], path[ZPATH_MAX + 260];
        uint32_t mode = g_cpu.r[2]; /* 0 = leitura, 1 = escrita */
        int slot;
        zmem_read_cstr(g_cpu.r[1], name, sizeof(name));

        if (!name_ok(name)) {
            LOGW("IFileMgr_OpenFile: nome invalido '%s'", name);
            g_cpu.r[0] = 0;
            break;
        }
        for (slot = 0; slot < ZFILE_MAX; slot++)
            if (!g_handles[slot]) break;
        if (slot == ZFILE_MAX) {
            LOGW("IFileMgr_OpenFile: sem handles livres");
            g_cpu.r[0] = 0;
            break;
        }
        build_path(path, sizeof(path), name);
        g_handles[slot] = fopen(path, mode ? "r+b" : "rb");
        if (!g_handles[slot] && mode)
            g_handles[slot] = fopen(path, "w+b");
        if (!g_handles[slot]) {
            LOGD("IFileMgr_OpenFile: '%s' nao encontrado", name);
            g_cpu.r[0] = 0;
            break;
        }
        {
            static const uint32_t vtbl[] = {
                ZT_FILE_ADDREF, ZT_FILE_RELEASE, ZT_FILE_READ,
                ZT_FILE_WRITE, ZT_FILE_SEEK
            };
            uint32_t obj = zbrew_make_interface(vtbl, 5, 1);
            if (!obj) {
                fclose(g_handles[slot]);
                g_handles[slot] = NULL;
                g_cpu.r[0] = 0;
                break;
            }
            zmem_write32(obj + 4, (uint32_t)slot);
            LOGD("IFileMgr_OpenFile('%s') -> IFile 0x%08X", name, obj);
            g_cpu.r[0] = obj;
        }
        break;
    }

    case ZT_FMGR_TEST: {
        char name[256], path[ZPATH_MAX + 260];
        zmem_read_cstr(g_cpu.r[1], name, sizeof(name));
        if (!name_ok(name)) { g_cpu.r[0] = ZBREW_EFAILED; break; }
        build_path(path, sizeof(path), name);
        {
            FILE *f = fopen(path, "rb");
            if (f) { fclose(f); g_cpu.r[0] = ZBREW_SUCCESS; }
            else   g_cpu.r[0] = ZBREW_EFAILED;
        }
        break;
    }

    case ZT_FMGR_REMOVE:
        /* Delecao de arquivos desabilitada por seguranca (HLE) */
        LOGW("IFileMgr_Remove ignorado");
        g_cpu.r[0] = ZBREW_EFAILED;
        break;

    case ZT_FILE_RELEASE: {
        uint32_t self = g_cpu.r[0];
        uint32_t slot = zmem_read32(self + 4);
        if (slot < ZFILE_MAX && g_handles[slot]) {
            fclose(g_handles[slot]);
            g_handles[slot] = NULL;
        }
        g_cpu.r[0] = 0;
        break;
    }

    case ZT_FILE_READ: {
        uint32_t self = g_cpu.r[0];
        uint32_t buf  = g_cpu.r[1];
        uint32_t len  = g_cpu.r[2];
        uint32_t slot = zmem_read32(self + 4);
        if (slot >= ZFILE_MAX || !g_handles[slot] || len == 0) {
            g_cpu.r[0] = 0;
            break;
        }
        {
            void *host = zmem_host_ptr(buf, len);
            size_t got = 0;
            if (host) {
                got = fread(host, 1, len, g_handles[slot]);
            } else {
                uint8_t tmp[4096];
                uint32_t off = 0;
                while (off < len) {
                    size_t chunk = len - off < sizeof(tmp) ? len - off : sizeof(tmp);
                    size_t n = fread(tmp, 1, chunk, g_handles[slot]);
                    if (n == 0) break;
                    zmem_write_block(buf + off, tmp, (uint32_t)n);
                    off += (uint32_t)n;
                    got += n;
                    if (n < chunk) break;
                }
            }
            g_cpu.r[0] = (uint32_t)got;
        }
        break;
    }

    case ZT_FILE_WRITE: {
        uint32_t self = g_cpu.r[0];
        uint32_t buf  = g_cpu.r[1];
        uint32_t len  = g_cpu.r[2];
        uint32_t slot = zmem_read32(self + 4);
        if (slot >= ZFILE_MAX || !g_handles[slot] || len == 0) {
            g_cpu.r[0] = 0;
            break;
        }
        {
            const void *host = zmem_host_ptr(buf, len);
            size_t put = 0;
            if (host) put = fwrite(host, 1, len, g_handles[slot]);
            g_cpu.r[0] = (uint32_t)put;
        }
        break;
    }

    case ZT_FILE_SEEK: {
        uint32_t self   = g_cpu.r[0];
        uint32_t origin = g_cpu.r[1]; /* 0=inicio 1=atual 2=fim */
        int32_t  offset = (int32_t)g_cpu.r[2];
        uint32_t slot = zmem_read32(self + 4);
        if (slot >= ZFILE_MAX || !g_handles[slot]) {
            g_cpu.r[0] = ZBREW_EFAILED;
            break;
        }
        {
            int o = origin == 0 ? SEEK_SET : origin == 1 ? SEEK_CUR : SEEK_END;
            g_cpu.r[0] = fseek(g_handles[slot], offset, o) == 0
                       ? ZBREW_SUCCESS : ZBREW_EFAILED;
        }
        break;
    }
    }
}
