/* ifile.c - IFileMgr/IFile: acesso a arquivos (assets do jogo)
 *
 * Mapeia nomes BREW para arquivos reais no diretorio da ROM.
 * Objeto IFile: [vtable][indice do handle host]
 * Seguranca: sandbox no diretorio base, sem escapar via "..".
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "brew.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../debug/log.h"

#define ZFILE_MAX 16
#define ZPATH_MAX 512
#define ZVCOMP_MAX 64
#define ZVCOMP_LEN 96

static FILE *g_handles[ZFILE_MAX];
static char  g_base_dir[ZPATH_MAX] = ".";

/* Layout real do IFileMgr (AEEFile.h, confirmado no BrewFileMgr do zeemu):
 * [0]AddRef [1]Release [2]OpenFile [3]GetInfo [4]Remove [5]MkDir [6]RmDir
 * [7]Test [8]GetFreeSpace [9]GetLastError [10]EnumInit [11]EnumNext
 * [12]Rename [13]EnumNextEx [14]SetDescription [15]GetInfoEx [16]Use
 * [17]GetFileUseInfo [18]ResolvePath [19]CheckPathAccess [20]GetFreeSpaceEx */
uint32_t zbrew_create_filemgr(void) {
    static const uint32_t vtbl[] = {
        ZT_FMGR_ADDREF, ZT_FMGR_RELEASE, ZT_FMGR_OPENFILE,
        ZT_FMGR_GETINFO, ZT_FMGR_REMOVE, ZT_FMGR_MKDIR,
        ZT_FMGR_STUB /* RmDir */, ZT_FMGR_TEST,
        ZT_FMGR_STUB /* GetFreeSpace */, ZT_FMGR_STUB /* GetLastError */,
        ZT_FMGR_STUB, ZT_FMGR_STUB, ZT_FMGR_STUB, ZT_FMGR_STUB,
        ZT_FMGR_STUB, ZT_FMGR_STUB, ZT_FMGR_STUB, ZT_FMGR_STUB,
        ZT_FMGR_STUB, ZT_FMGR_STUB, ZT_FMGR_STUB
    };
    return zbrew_make_interface(vtbl, 21, 0);
}

void zbrew_set_file_base(const char *dir) {
    if (dir && dir[0]) {
        strncpy(g_base_dir, dir, sizeof(g_base_dir) - 1);
        g_base_dir[sizeof(g_base_dir) - 1] = '\0';
    }
}

static int ci_starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        unsigned char a, b;
        if (!*s) return 0;
        a = (unsigned char)*s++;
        b = (unsigned char)*prefix++;
        if (tolower(a) != tolower(b)) return 0;
    }
    return 1;
}

static int name_ok(const char *name) {
    if (!name || !name[0]) return 0;
    if (name[0] == '/' || name[0] == '\\') return 0;
    if (strchr(name, ':') && !ci_starts_with(name, "fs:/")) return 0;
    return 1;
}

static const char *skip_fs_prefix(const char *name) {
    if (ci_starts_with(name, "fs:/")) {
        const char *p = name + 4;
        while (*p == '/') p++;
        return p;
    }
    return name;
}

static void normalize_slashes(char *s) {
    while (*s) {
        if (*s == '\\') *s = '/';
        s++;
    }
}

static int split_component(const char **pp, char *out, size_t outsz) {
    const char *p = *pp;
    size_t n = 0;

    while (*p == '/') p++;
    while (*p && *p != '/') {
        if (n + 1 < outsz) out[n++] = *p;
        p++;
    }
    out[n] = '\0';
    *pp = p;
    return (int)n;
}

static int resolve_brew_path(char *out, size_t outsz, const char *name) {
    char in[ZPATH_MAX + 260];
    char virt[ZPATH_MAX + 260];
    char comps[ZVCOMP_MAX][ZVCOMP_LEN];
    int ncomp = 0;
    const char *p;

    if (!name_ok(name)) return 0;

    strncpy(in, name, sizeof(in) - 1);
    in[sizeof(in) - 1] = '\0';
    normalize_slashes(in);

    if (ci_starts_with(in, "fs:/")) {
        strncpy(virt, in, sizeof(virt) - 1);
        virt[sizeof(virt) - 1] = '\0';
    } else {
        const char *rel = in;
        if (rel[0] == '.' && rel[1] == '/') rel += 2;
        snprintf(virt, sizeof(virt), "fs:/%s", rel);
    }

    p = skip_fs_prefix(virt);

    while (*p) {
        char c[ZVCOMP_LEN];
        int len = split_component(&p, c, sizeof(c));
        if (*p == '/') p++;
        if (len <= 0 || (len == 1 && c[0] == '.'))
            continue;
        if (len == 2 && c[0] == '.' && c[1] == '.') {
            if (ncomp <= 0)
                return 0; /* tentaria escapar da sandbox */
            ncomp--;
            continue;
        }
        if (ncomp >= ZVCOMP_MAX)
            return 0;
        strncpy(comps[ncomp], c, ZVCOMP_LEN - 1);
        comps[ncomp][ZVCOMP_LEN - 1] = '\0';
        ncomp++;
    }

    /* Alias estilo Zeemu: fs:/~ e fs:/~0xXXXXXXXX -> raiz do app */
    if (ncomp > 0 && comps[0][0] == '~') {
        int i;
        for (i = 1; i < ncomp; i++)
            strncpy(comps[i - 1], comps[i], ZVCOMP_LEN);
        ncomp--;
    }

    snprintf(out, outsz, "%s", g_base_dir);
    {
        int i;
        for (i = 0; i < ncomp; i++) {
            size_t cur = strlen(out);
            size_t need = cur + 1 + strlen(comps[i]) + 1;
            if (need > outsz)
                return 0;
            out[cur] = '/';
            out[cur + 1] = '\0';
            strncat(out, comps[i], outsz - strlen(out) - 1);
        }
    }

    return 1;
}

void zbrew_handle_file(uint32_t id) {
    switch (id) {
    case ZT_FMGR_ADDREF:
    case ZT_FMGR_RELEASE:
    case ZT_FILE_ADDREF:
        g_cpu.r[0] = 1;
        break;

    case ZT_FMGR_OPENFILE: {
        /* R2 = AEEOpenFileMode (bitmask): _OFM_READ=1 _OFM_READWRITE=2
         * _OFM_CREATE=4 _OFM_APPEND=8. So abre gravavel com flags de
         * escrita explicitas - _OFM_READ sozinho e SO-LEITURA (era o bug
         * que corrompia data.vfs: read-only aberto como "r+b"). */
        char name[256], path[ZPATH_MAX + 260];
        uint32_t mode = g_cpu.r[2];
        int writable = (mode & 0x0Eu) != 0; /* READWRITE|CREATE|APPEND */
        int slot;
        zmem_read_cstr(g_cpu.r[1], name, sizeof(name));

        if (!resolve_brew_path(path, sizeof(path), name)) {
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
        g_handles[slot] = fopen(path, writable ? "r+b" : "rb");
        if (!g_handles[slot] && (mode & 0x04u)) /* _OFM_CREATE */
            g_handles[slot] = fopen(path, "w+b");
        if (g_handles[slot] && (mode & 0x08u)) /* _OFM_APPEND */
            fseek(g_handles[slot], 0, SEEK_END);
        if (!g_handles[slot]) {
            LOGD("IFileMgr_OpenFile: '%s' (modo 0x%X) nao encontrado", name, mode);
            g_cpu.r[0] = 0;
            break;
        }
        {
            /* Layout real do IFile (AEEFile.h, herda IAStream):
             * [0]AddRef [1]Release [2]Readable [3]Read [4]Cancel [5]Write
             * [6]GetInfo [7]Seek [8]Truncate [9]GetInfoEx [10]SetCacheSize
             * [11]Map */
            static const uint32_t vtbl[] = {
                ZT_FILE_ADDREF, ZT_FILE_RELEASE, ZT_FILE_READABLE,
                ZT_FILE_READ, ZT_FILE_CANCEL, ZT_FILE_WRITE,
                ZT_FILE_GETINFO, ZT_FILE_SEEK, ZT_FILE_STUB,
                ZT_FILE_STUB, ZT_FILE_STUB, ZT_FILE_STUB
            };
            uint32_t obj = zbrew_make_interface(vtbl, 12, 1);
            if (!obj) {
                fclose(g_handles[slot]);
                g_handles[slot] = NULL;
                g_cpu.r[0] = 0;
                break;
            }
            zmem_write32(obj + 4, (uint32_t)slot);
            LOGD("IFileMgr_OpenFile('%s' modo 0x%X) -> IFile 0x%08X", name, mode, obj);
            g_cpu.r[0] = obj;
        }
        break;
    }

    case ZT_FMGR_TEST: {
        char name[256], path[ZPATH_MAX + 260];
        zmem_read_cstr(g_cpu.r[1], name, sizeof(name));
        if (!resolve_brew_path(path, sizeof(path), name)) {
            g_cpu.r[0] = ZBREW_EFAILED;
            break;
        }
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
        {
            static uint32_t read_logs = 0;
            if (read_logs < 16) {
                LOGD("IFile_Read slot=%u buf=0x%08X len=%u pos=%ld",
                     slot, buf, len,
                     (slot < ZFILE_MAX && g_handles[slot])
                         ? ftell(g_handles[slot]) : -1L);
                read_logs++;
            }
        }
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
        /* AEEFileSeekType real (AEEFile.h): 0=_SEEK_START 1=_SEEK_END
         * 2=_SEEK_CURRENT (a ordem END/CURRENT e invertida vs stdio).
         * Idioma comum dos jogos: Seek(_SEEK_CURRENT, 0) = "GetPos",
         * espera a posicao atual como retorno (como no zeemu). */
        uint32_t self   = g_cpu.r[0];
        uint32_t origin = g_cpu.r[1];
        int32_t  offset = (int32_t)g_cpu.r[2];
        uint32_t slot = zmem_read32(self + 4);
        if (slot >= ZFILE_MAX || !g_handles[slot]) {
            g_cpu.r[0] = ZBREW_EFAILED;
            break;
        }
        {
            static uint32_t seek_logs = 0;
            if (seek_logs < 16) {
                LOGD("IFile_Seek slot=%u tipo=%u offset=%d", slot, origin,
                     (int)offset);
                seek_logs++;
            }
        }
        if (origin == 2 && offset == 0) {
            g_cpu.r[0] = (uint32_t)ftell(g_handles[slot]);
            break;
        }
        {
            int o = origin == 0 ? SEEK_SET : origin == 1 ? SEEK_END : SEEK_CUR;
            g_cpu.r[0] = fseek(g_handles[slot], offset, o) == 0
                       ? ZBREW_SUCCESS : ZBREW_EFAILED;
        }
        break;
    }

    case ZT_FILE_READABLE:
        /* Readable(po, AEECallback*): sinalizaria via callback quando ha
         * dados; arquivos locais estao sempre prontos. */
        g_cpu.r[0] = 1;
        break;

    case ZT_FILE_CANCEL:
        g_cpu.r[0] = 0;
        break;

    case ZT_FILE_GETINFO: {
        /* FileInfo (AEEFile.h): +0 attrib, +4 dwCreationDate, +8 dwSize,
         * +12 szName[64] - total 76 bytes. */
        uint32_t self  = g_cpu.r[0];
        uint32_t pinfo = g_cpu.r[1];
        uint32_t slot  = zmem_read32(self + 4);
        long cur, size;
        if (slot >= ZFILE_MAX || !g_handles[slot] || !pinfo) {
            g_cpu.r[0] = ZBREW_EFAILED;
            break;
        }
        cur = ftell(g_handles[slot]);
        fseek(g_handles[slot], 0, SEEK_END);
        size = ftell(g_handles[slot]);
        fseek(g_handles[slot], cur, SEEK_SET);
        zmem_write32(pinfo + 0, 0);
        zmem_write32(pinfo + 4, 0);
        zmem_write32(pinfo + 8, (uint32_t)size);
        zmem_write8(pinfo + 12, 0);
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;
    }

    case ZT_FILE_STUB:
        LOGW("IFile: metodo nao implementado (Truncate/GetInfoEx/"
             "SetCacheSize/Map) R0=0x%08X R1=0x%08X", g_cpu.r[0], g_cpu.r[1]);
        g_cpu.r[0] = ZBREW_EFAILED;
        break;

    case ZT_FMGR_GETINFO: {
        /* IFILEMGR_GetInfo(pfm, nome, FileInfo*) */
        char name[256], path[ZPATH_MAX + 260];
        uint32_t pinfo = g_cpu.r[2];
        FILE *f;
        long size;
        zmem_read_cstr(g_cpu.r[1], name, sizeof(name));
        if (!pinfo || !resolve_brew_path(path, sizeof(path), name)) {
            g_cpu.r[0] = ZBREW_EFAILED;
            break;
        }
        f = fopen(path, "rb");
        if (!f) {
            g_cpu.r[0] = ZBREW_EFAILED;
            break;
        }
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fclose(f);
        zmem_write32(pinfo + 0, 0);
        zmem_write32(pinfo + 4, 0);
        zmem_write32(pinfo + 8, (uint32_t)size);
        zmem_write8(pinfo + 12, 0);
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;
    }

    case ZT_FMGR_MKDIR:
        /* Criacao de diretorio no host desabilitada por enquanto (mesma
         * politica do Remove); jogos que salvam em subpasta nova falham
         * o save, nao o boot. */
        LOGW("IFileMgr_MkDir ignorado");
        g_cpu.r[0] = ZBREW_EFAILED;
        break;

    case ZT_FMGR_STUB:
        LOGW("IFileMgr: metodo nao implementado R0=0x%08X R1=0x%08X R2=0x%08X",
             g_cpu.r[0], g_cpu.r[1], g_cpu.r[2]);
        g_cpu.r[0] = ZBREW_EFAILED;
        break;
    }
}
