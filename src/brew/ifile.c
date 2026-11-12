/* ifile.c - IFileMgr/IFile: acesso a arquivos (assets do jogo)
 *
 * Mapeia nomes BREW para arquivos reais no diretorio da ROM.
 * Objeto IFile: [vtable][indice do handle host]
 * Seguranca: sandbox no diretorio base, sem escapar via "..".
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#endif
#include "brew.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../debug/log.h"
#include "../audio/miniz_tinfl.h"
#include "../loader/pakz.h"

#define ZFILE_MAX 16
#define ZIFILE_POOL 64
#define ZIFILE_OBJ_WORDS 64 /* padding: guest writes past COM header */
#define ZPATH_MAX 512
#define ZVCOMP_MAX 64
#define ZVCOMP_LEN 96

static FILE *g_handles[ZFILE_MAX];
/* Cache host RO: zip/pkg byte-a-byte (CUNG inflate) sem ftell/fread. */
typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t pos;
    int active;
} ifile_mem_t;
static ifile_mem_t g_mem[ZFILE_MAX];
static uint32_t g_ifile_vtbl;
static uint32_t g_ifile_pool[ZIFILE_POOL];
static uint8_t g_ifile_used[ZIFILE_POOL];
static int g_ifile_slot[ZIFILE_POOL];
static uint32_t g_ifile_refs[ZIFILE_POOL]; /* refcount só no host */
char g_base_dir[512] = ".";

static void ifile_mem_free(int slot) {
    if (slot < 0 || slot >= ZFILE_MAX) return;
    if (g_mem[slot].data) free(g_mem[slot].data);
    g_mem[slot].data = NULL;
    g_mem[slot].size = 0;
    g_mem[slot].pos = 0;
    g_mem[slot].active = 0;
}

/* Carrega arquivo RO inteiro no host (zip/pkg <= 32MB). 1 = ok. */
static int ifile_mem_load(int slot, FILE *fp) {
    long sz;
    uint8_t *buf;
    if (slot < 0 || slot >= ZFILE_MAX || !fp) return 0;
    ifile_mem_free(slot);
    if (fseek(fp, 0, SEEK_END) != 0) return 0;
    sz = ftell(fp);
    if (sz <= 0 || sz > (32L * 1024L * 1024L)) {
        fseek(fp, 0, SEEK_SET);
        return 0;
    }
    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fseek(fp, 0, SEEK_SET);
        return 0;
    }
    fseek(fp, 0, SEEK_SET);
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fseek(fp, 0, SEEK_SET);
        return 0;
    }
    g_mem[slot].data = buf;
    g_mem[slot].size = (uint32_t)sz;
    g_mem[slot].pos = 0;
    g_mem[slot].active = 1;
    return 1;
}

static void ifile_raw_write32(uint32_t addr, uint32_t val) {
    uint8_t *p = (uint8_t *)zmem_host_ptr(addr, 4);
    if (!p) return;
    p[0] = (uint8_t)(val);
    p[1] = (uint8_t)(val >> 8);
    p[2] = (uint8_t)(val >> 16);
    p[3] = (uint8_t)(val >> 24);
}

static uint32_t ifile_shared_vtbl(void) {
    /* AEEFile.h / zeemu: [8]Truncate [9]GetInfoEx [10]SetCacheSize [11]Map */
    static const uint32_t traps[] = {
        ZT_FILE_ADDREF, ZT_FILE_RELEASE, ZT_FILE_READABLE,
        ZT_FILE_READ, ZT_FILE_CANCEL, ZT_FILE_WRITE,
        ZT_FILE_GETINFO, ZT_FILE_SEEK, ZT_FILE_STUB,
        ZT_FILE_STUB, ZT_FILE_SETCACHE, ZT_FILE_MAP
    };
    int i;
    if (g_ifile_vtbl) return g_ifile_vtbl;
    g_ifile_vtbl = zheap_alloc(12 * 4);
    if (!g_ifile_vtbl) return 0;
    for (i = 0; i < 12; i++)
        ifile_raw_write32(g_ifile_vtbl + (uint32_t)i * 4, ZTRAP_ADDR(traps[i]));
    return g_ifile_vtbl;
}

static int ifile_pool_index(uint32_t obj) {
    int i;
    if (!obj) return -1;
    for (i = 0; i < ZIFILE_POOL; i++)
        if (g_ifile_pool[i] == obj) return i;
    return -1;
}

static uint32_t ifile_pool_alloc(int slot) {
    int i;
    uint32_t vtbl = ifile_shared_vtbl();
    if (!vtbl) return 0;
    for (i = 0; i < ZIFILE_POOL; i++) {
        if (g_ifile_used[i]) continue;
        if (!g_ifile_pool[i]) {
            g_ifile_pool[i] = zheap_alloc(ZIFILE_OBJ_WORDS * 4);
            if (!g_ifile_pool[i]) return 0;
        }
        g_ifile_used[i] = 1;
        g_ifile_slot[i] = slot;
        g_ifile_refs[i] = 1;
        /* Guest: [0]=vtbl; +4.. livres (stream). Slot/refs só no host. */
        ifile_raw_write32(g_ifile_pool[i], vtbl);
        return g_ifile_pool[i];
    }
    return 0;
}

static int ifile_slot_of(uint32_t obj) {
    int i = ifile_pool_index(obj);
    return i >= 0 ? g_ifile_slot[i] : -1;
}

int zbrew_ifile_is_live(uint32_t obj) {
    int i;
    if (!obj) return 0;
    /* Qualquer endereço do pool de IFile (usado ou liberado) — free() guest
     * nunca deve reciclar esses blocos. */
    for (i = 0; i < ZIFILE_POOL; i++)
        if (g_ifile_pool[i] == obj) return 1;
    return 0;
}

/* Bloqueia write na palavra da vtable. */
int zbrew_ifile_block_write(uint32_t addr, uint32_t len) {
    int i;
    if (!len) return 0;
    for (i = 0; i < ZIFILE_POOL; i++) {
        uint32_t base = g_ifile_pool[i];
        if (!base) continue;
        if (addr < base + 4u && addr + len > base)
            return 1;
    }
    return 0;
}

/* Se guest lê [IFile+0], devolve vtable HLE. Não aloca (evita recursão com heap). */
uint32_t zbrew_ifile_force_vtbl_read(uint32_t addr) {
    int i;
    if (!g_ifile_vtbl) return 0;
    for (i = 0; i < ZIFILE_POOL; i++) {
        if (g_ifile_pool[i] && g_ifile_pool[i] == addr)
            return g_ifile_vtbl;
    }
    return 0;
}

void zbrew_ifile_release_addr(uint32_t obj) {
    uint32_t old0;
    int pi;
    if (!zbrew_ifile_is_live(obj)) return;
    pi = ifile_pool_index(obj);
    if (pi >= 0) g_ifile_refs[pi] = 1;
    old0 = g_cpu.r[0];
    g_cpu.r[0] = obj;
    zbrew_handle_file(ZT_FILE_RELEASE);
    g_cpu.r[0] = old0;
}

static int ci_path_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static void ci_mkdir_recursive(const char *path) {
    char tmp[ZPATH_MAX + 260];
    size_t i;
    if (!path || !path[0]) return;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (i = 1; i < strlen(tmp); i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            tmp[i] = '\0';
#if defined(_WIN32)
            _mkdir(tmp);
#else
            mkdir(tmp, 0755);
#endif
            tmp[i] = '/';
        }
    }
#if defined(_WIN32)
    _mkdir(tmp);
#else
    mkdir(tmp, 0755);
#endif
}

/* ------------------------------------------------------------------ *
 * Janela virtual para arquivos .ggz (Double Dragon et al.)
 *
 * Formato real confirmado na ROM: um TOC em 0 com entradas de 8 bytes
 * big-endian {offset, size} (size = tamanho DESCOMPRIMIDO do membro),
 * seguido pelos membros como streams gzip puros. A tabela ocupa
 * [0, off0) onde off0 = offset do primeiro membro (e multiplo de 8);
 * cada entrada em [i*8, i*8+8) descreve um membro - a entrada i=0
 * aponta para o proprio off0. O jogo le o TOC cru em [0, off0),
 * faz Seek(offset_i) e le size_i bytes - entao o espaco virtual de cada
 * membro coincide com os offsets/sizes do TOC. Aqui descompact os
 * membros na abertura e servimos os bytes finais, para que o inflater
 * ZEIS do jogo (substituido por um stub de copia) e o driver de
 * IFile_Mirror usem os dados reais.
 * ------------------------------------------------------------------ */
typedef struct {
    uint32_t off;    /* offset virtual no .ggz (= off do TOC) */
    uint32_t size;   /* tamanho final (tamanho do TOC) */
    uint8_t *data;   /* conteudo descomprimido (ou bruto) */
} ggz_mem_t;

typedef struct {
    int active;      /* 1 = arquivo e .ggz servido por esta janela */
    int vfs;         /* 1 = janela de membro do container VFS FUFS  */
    uint32_t off;    /* VFS: offset do membro dentro do data.vfs    */
    FILE *host;      /* arquivo host (VFS: o proprio data.vfs)      */
    uint8_t *toc;    /* bytes crus [0, toc_len)                       */
    uint32_t toc_len;
    uint32_t total;  /* tamanho virtual total do arquivo              */
    ggz_mem_t *m;    /* membros                                       */
    uint32_t nm;
    int active_member;
    long pos;        /* posicao virtual corrente                      */
} ggz_win_t;

static ggz_win_t g_ggz[ZFILE_MAX];

/* ---- IFileMgr EnumInit/EnumNext state ---- */
#define ZENUM_MAX 256
#define ZENUM_NAME_MAX 128
static char   g_enum_names[ZENUM_MAX][ZENUM_NAME_MAX];
static int    g_enum_count = 0;
static int    g_enum_idx   = 0;

static void enum_scan_dir(const char *pattern) {
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    HANDLE hFind;
    char mask[ZPATH_MAX + 260];
    g_enum_count = 0;
    g_enum_idx   = 0;
    snprintf(mask, sizeof(mask), "%s/%s", g_base_dir,
             pattern && pattern[0] ? pattern : "*");
    hFind = FindFirstFileA(mask, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (g_enum_count >= ZENUM_MAX) break;
        strncpy(g_enum_names[g_enum_count], fd.cFileName, ZENUM_NAME_MAX - 1);
        g_enum_names[g_enum_count][ZENUM_NAME_MAX - 1] = '\0';
        g_enum_count++;
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    DIR *d;
    struct dirent *de;
    g_enum_count = 0;
    g_enum_idx   = 0;
    d = opendir(g_base_dir);
    if (!d) return;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (g_enum_count >= ZENUM_MAX) break;
        strncpy(g_enum_names[g_enum_count], de->d_name, ZENUM_NAME_MAX - 1);
        g_enum_names[g_enum_count][ZENUM_NAME_MAX - 1] = '\0';
        g_enum_count++;
    }
    closedir(d);
#endif
}

/* ------------------------------------------------------------------ *
 * Container VFS do Zeebo (magic "FUFS"): o data.vfs e um filesystem
 * virtual; o TOC em 0x0C tem entradas de 12 bytes little-endian
 * {offset(4), namehash(4), size(4)} e o hash do nome e
 * h = h*67 + c - 0x71 com '\\'->'/' e minusculas->maiusculas.
 * ------------------------------------------------------------------ */
typedef struct {
    uint32_t off;
    uint32_t hash;
    uint32_t size;
} vfs_entry_t;

static vfs_entry_t *g_vfs_toc = NULL;
static int g_vfs_nm = 0;
static FILE *g_vfs_file = NULL;

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

static uint32_t vfs_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t vfs_hash(const char *name) {
    uint32_t h = 0;
    for (; *name; ++name) {
        uint32_t c = (unsigned char)*name;
        if (c == '\\') c = '/';
        if (c >= 'a' && c <= 'z') c -= 0x20;
        h = h * 67u + c - 0x71u;
    }
    return h;
}

static void vfs_load(void) {
    char path[ZPATH_MAX + 260];
    uint8_t hdr[16], *buf;
    uint32_t cnt, toc_len, i;
    long fsz;
    FILE *f;
    if (g_vfs_toc) return;
    snprintf(path, sizeof(path), "%s/data.vfs", g_base_dir);
    f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz < 16 || fread(hdr, 1, 16, f) != 16 ||
        memcmp(hdr, "FUFS", 4) != 0) {
        fclose(f);
        return;
    }
    cnt = vfs_le32(hdr + 8);
    toc_len = vfs_le32(hdr + 12);
    if (cnt == 0 || cnt > 0x10000u ||
        12u + (uint64_t)cnt * 12 > (uint64_t)toc_len ||
        (long)toc_len > fsz) {
        fclose(f);
        return;
    }
    buf = (uint8_t *)malloc((size_t)cnt * 12);
    if (!buf) {
        fclose(f);
        return;
    }
    fseek(f, 12, SEEK_SET);
    if (fread(buf, 12, cnt, f) != cnt) {
        free(buf);
        fclose(f);
        return;
    }
    g_vfs_toc = (vfs_entry_t *)buf;
    g_vfs_nm = (int)cnt;
    g_vfs_file = f;
    (void)i;
    LOGI("VFS: data.vfs carregado (%d membros, %ld bytes)", g_vfs_nm, fsz);
}

static const char *vfs_leaf(const char *name) {
    const char *p, *slash;
    if (!name) return name;
    p = name;
    if (ci_starts_with(p, "fs:/")) {
        p += 4;
        while (*p == '/') p++;
    }
    if (p[0] == '~') {
        p++;
        while (*p && *p != '/') p++;
        if (*p == '/') p++;
    }
    slash = strrchr(p, '/');
    if (slash) p = slash + 1;
    return p;
}

static int vfs_find(const char *name) {
    const char *leaf;
    uint32_t h;
    int i;
    vfs_load();
    if (!name || !name[0]) return -1;
    h = vfs_hash(name);
    for (i = 0; i < g_vfs_nm; i++)
        if (g_vfs_toc[i].hash == h) return i;
    leaf = vfs_leaf(name);
    if (leaf && leaf != name && leaf[0]) {
        h = vfs_hash(leaf);
        for (i = 0; i < g_vfs_nm; i++)
            if (g_vfs_toc[i].hash == h) return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ *
 * Container "PACK" (.pakz): arquivos do Zeebo Extreme etc.
 * O parsing do formato fica em src/loader/pakz.c; aqui apenas montamos
 * a lista de arquivos .pakz do jogo e resolvemos membros por nome.
 * ------------------------------------------------------------------ */
#define ZPAKZ_MAX_FILES 16
static pakz_t *g_pakz_arch[ZPAKZ_MAX_FILES];
static int g_pakz_narch = 0;
static int g_pakz_loaded = 0;

static void pakz_add_file(const char *path) {
    pakz_t *p;
    if (!path || !path[0]) return;
    if (g_pakz_narch >= ZPAKZ_MAX_FILES) return;
    p = pakz_open(path);
    if (!p) return;
    g_pakz_arch[g_pakz_narch++] = p;
    LOGI("PAKZ: %s -> %d membros", path, pakz_count(p));
}

static void pakz_scan_dir(const char *dir) {
    if (!dir || !dir[0]) return;
#if defined(_WIN32)
    {
        WIN32_FIND_DATAA fd;
        HANDLE h;
        char mask[ZPATH_MAX + 64];
        snprintf(mask, sizeof(mask), "%s/*.pakz", dir);
        h = FindFirstFileA(mask, &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            char full[ZPATH_MAX + 300];
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            snprintf(full, sizeof(full), "%s/%s", dir, fd.cFileName);
            pakz_add_file(full);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    {
        DIR *d = opendir(dir);
        struct dirent *de;
        if (!d) return;
        while ((de = readdir(d)) != NULL) {
            size_t len = strlen(de->d_name);
            char full[ZPATH_MAX + 300];
            if (len < 5) continue;
            if (tolower((unsigned char)de->d_name[len - 5]) != '.' ||
                tolower((unsigned char)de->d_name[len - 4]) != 'p' ||
                tolower((unsigned char)de->d_name[len - 3]) != 'a' ||
                tolower((unsigned char)de->d_name[len - 2]) != 'k' ||
                tolower((unsigned char)de->d_name[len - 1]) != 'z')
                continue;
            snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
            pakz_add_file(full);
        }
        closedir(d);
    }
#endif
}

static void pakz_load(void) {
    const char *md;
    if (g_pakz_loaded) return;
    g_pakz_loaded = 1;
    md = zbrew_get_mod_dir();
    pakz_scan_dir(md);
    if (!md || strcmp(md, g_base_dir) != 0)
        pakz_scan_dir(g_base_dir);
}

static pakz_t *pakz_lookup(const char *name, int *member) {
    int i, idx;
    if (member) *member = -1;
    if (!name || !name[0]) return NULL;
    pakz_load();
    for (i = 0; i < g_pakz_narch; i++) {
        idx = pakz_find(g_pakz_arch[i], name);
        if (idx >= 0) {
            if (member) *member = idx;
            return g_pakz_arch[i];
        }
    }
    return NULL;
}

static void pakz_reset(void) {
    int i;
    for (i = 0; i < g_pakz_narch; i++)
        if (g_pakz_arch[i]) pakz_close(g_pakz_arch[i]);
    g_pakz_narch = 0;
    g_pakz_loaded = 0;
}

static uint32_t ggz_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int ggz_has_ext(const char *name) {
    size_t n = strlen(name);
    return n >= 4 &&
           tolower((unsigned char)name[n - 4]) == '.' &&
           tolower((unsigned char)name[n - 3]) == 'g' &&
           tolower((unsigned char)name[n - 2]) == 'g' &&
           tolower((unsigned char)name[n - 1]) == 'z';
}

/* Descomprime um membro gzip (raw deflate) para out (out_len bytes max).
 * Retorna o numero de bytes escritos ou -1 se nao e gzip/inflavel. */
static int ggz_inflate(const uint8_t *in, uint32_t in_len,
                       uint8_t *out, uint32_t out_len) {
    uint8_t flg;
    uint32_t pos;
    size_t out_sz, deflate_len;

    if (!in || in_len < 18 || in[0] != 0x1F || in[1] != 0x8B ||
        (in[2] & 0x0F) != 8)
        return -1;

    flg = in[3];
    pos = 10;
    if (flg & 0x04) { /* FEXTRA */
        uint32_t xlen;
        if (pos + 2 > in_len) return -1;
        xlen = in[pos] | ((uint32_t)in[pos + 1] << 8);
        pos += 2 + xlen;
    }
    if (flg & 0x08) { /* FNAME */
        while (pos < in_len && in[pos]) pos++;
        if (pos >= in_len) return -1;
        pos++;
    }
    if (flg & 0x10) { /* FCOMMENT */
        while (pos < in_len && in[pos]) pos++;
        if (pos >= in_len) return -1;
        pos++;
    }
    if (flg & 0x02) pos += 2; /* FHCRC */
    if (pos + 8 > in_len) return -1;
    deflate_len = in_len - pos - 8; /* exclui CRC32 + ISIZE */

    out_len = out_len ? out_len : 1;
    out_sz = tinfl_decompress_mem_to_mem(out, out_len,
                                         in + pos, deflate_len, 0);
    if (out_sz == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED)
        return -1;
    return (int)out_sz;
}

static void ggz_free(ggz_win_t *w) {
    uint32_t i;
    if (w->toc) { free(w->toc); w->toc = NULL; }
    if (w->m) {
        for (i = 0; i < w->nm; i++)
            if (w->m[i].data) free(w->m[i].data);
        free(w->m);
    }
    w->m = NULL;
    w->nm = 0;
    w->toc_len = 0;
    w->total = 0;
    w->active_member = -1;
    w->pos = 0;
    w->active = 0;
}

/* Constroi a janela; devolve 1 se o arquivo e um .ggz valido. */
static int ggz_build(ggz_win_t *w, const char *path) {
    FILE *f;
    long fsz;
    uint8_t *raw;
    uint32_t off0, n, total;
    int served = 0;

    if (!path || !path[0]) return 0;
    f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz < 16) { fclose(f); return 0; }

    raw = (uint8_t *)malloc((size_t)fsz);
    if (!raw) { fclose(f); return 0; }
    if (fread(raw, 1, (size_t)fsz, f) != (size_t)fsz) {
        free(raw); fclose(f); return 0;
    }
    fclose(f);

    /* A tabela inteira fica em [0, off0), com uma entrada de 8 bytes por
     * membro; a entrada i=0 descreve o membro cujo gzip começa em off0. */
    off0 = ggz_be32(raw);
    if (off0 < 16 || (off0 & 7) || off0 > (uint32_t)fsz)
        return 0;
    n = off0 / 8;
    if (n < 1 || n > 4096) { free(raw); return 0; }

    w->m = (ggz_mem_t *)calloc(n, sizeof(ggz_mem_t));
    if (!w->m) { free(raw); return 0; }
    w->nm = n;

    {
        uint32_t i;
        for (i = 0; i < n; i++) {
            uint32_t o  = ggz_be32(raw + i * 8);
            uint32_t s  = ggz_be32(raw + i * 8 + 4);
            uint8_t *data;
            uint32_t avail;

            if (o >= off0 && o <= (uint32_t)fsz)
                avail = (uint32_t)fsz - o;
            else
                avail = 0;

            w->m[i].off  = o;
            w->m[i].size = s;
            data = (uint8_t *)malloc(s ? s : 1);
            if (!data)
                continue;

            if (s && avail >= 2 && raw[o] == 0x1F && raw[o + 1] == 0x8B) {
                int got = ggz_inflate(raw + o, avail, data, s);
                if (got > 0) {
                    w->m[i].data = data;
                    served++;
                    continue;
                }
            }
            /* Nao era.gzip (ou falhou): serve bruto + zero-pad. */
            {
                uint32_t cp = s < avail ? s : avail;
                memcpy(data, raw + o, cp);
                if (cp < s) memset(data + cp, 0, s - cp);
            }
            w->m[i].data = data;
            served++;
        }
    }

    if (!served) {
        ggz_free(w);
        free(raw);
        return 0;
    }

    /* Tamanho virtual total = max(off + size), >= TOC */
    total = off0;
    {
        uint32_t i;
        for (i = 0; i < n; i++) {
            uint64_t end = (uint64_t)w->m[i].off + w->m[i].size;
            if (end > total) total = (uint32_t)end;
        }
    }
    w->toc     = raw;
    w->toc_len = off0;
    w->total   = total;
    w->active_member = -1;
    w->pos     = 0;
    w->active  = 1;
    return 1;
}

/* Le `len` bytes a partir da posicao virtual; retorna quantos escreveu. */
static size_t ggz_read(ggz_win_t *w, void *dst, uint32_t len) {
    long pos = w->pos;
    size_t got = 0;
    if (w->vfs) {
        /* membro do container VFS: ler do arquivo host em off+pos */
        long left = (long)w->total - pos;
        if (left < 0) left = 0;
        if ((long)len > left) len = (uint32_t)left;
        if (len) {
            fseek(w->host, (long)w->off + pos, SEEK_SET);
            got = fread(dst, 1, len, w->host);
            pos += (long)got;
        }
        w->pos = pos;
        return got;
    }
    while (len) {
        uint32_t o = (uint32_t)pos;
        if (o < w->toc_len) {
            size_t c = w->toc_len - o;
            if (c > len) c = len;
            memcpy((uint8_t *)dst + got, w->toc + o, c);
            pos += (long)c;
            len -= (uint32_t)c;
            got += c;
        } else {
            ggz_mem_t *m = NULL;
            if (w->active_member >= 0 &&
                (uint32_t)w->active_member < w->nm) {
                ggz_mem_t *selected = &w->m[w->active_member];
                if (o >= selected->off &&
                    o < selected->off + selected->size)
                    m = selected;
            }
            if (!m) {
                /* regiao desconhecida: zeros */
                memset((uint8_t *)dst + got, 0, len);
                pos += (long)len;
                got += len;
                len = 0;
                break;
            }
            {
                size_t c = (uint32_t)(m->size - (o - m->off));
                if (c > len) c = len;
                memcpy((uint8_t *)dst + got, m->data + (o - m->off), c);
                pos += (long)c;
                len -= (uint32_t)c;
                got += c;
            }
        }
    }
    w->pos = pos;
    return got;
}

static int ggz_seek(ggz_win_t *w, uint32_t origin, int32_t offset) {
    long base = origin == 0 ? 0 : origin == 1 ? (long)w->total : w->pos;
    long pos = base + offset;
    uint32_t i;
    if (pos < 0) pos = 0;
    if (pos > (long)w->total) pos = (long)w->total;
    w->active_member = -1;
    for (i = 0; i < w->nm; i++) {
        if ((uint32_t)pos == w->m[i].off) {
            w->active_member = (int)i;
            break;
        }
    }
    w->pos = pos;
    return 0;
}

/* Layout real do IFileMgr (AEEFile.h, confirmado no BrewFileMgr do zeemu):
 * [0]AddRef [1]Release [2]OpenFile [3]GetInfo [4]Remove [5]MkDir [6]RmDir
 * [7]Test [8]GetFreeSpace [9]GetLastError [10]EnumInit [11]EnumNext
 * [12]Rename [13]EnumNextEx [14]SetDescription [15]GetInfoEx [16]Use
 * [17]GetFileUseInfo [18]ResolvePath [19]CheckPathAccess [20]GetFreeSpaceEx */
/* Cria um objeto IFile valido ligado ao slot `slot` (g_handles[slot]).
 * Usado pelo IFileMgr_OpenFile e pelo CreateInstance de classes que o
 * jogo trata como stream de dados (ex: 0x01001014 no Double Dragon). */
uint32_t zbrew_create_file_object(int slot) {
    uint32_t obj = ifile_pool_alloc(slot);
    if (!obj) {
        LOGW("ifile_pool_alloc failed slot=%d", slot);
        return 0;
    }
    return obj;
}

uint32_t zbrew_create_filemgr(void) {
    /* Slots stub com IDs unicos (ZT_FMGR_STUB2+n) para que o log
     * identifique qual metodo real do IFileMgr o jogo chamou. */
    static const uint32_t vtbl[] = {
        ZT_FMGR_ADDREF, ZT_FMGR_RELEASE, ZT_FMGR_OPENFILE,
        ZT_FMGR_GETINFO, ZT_FMGR_REMOVE, ZT_FMGR_MKDIR,
        ZT_FMGR_STUB2 + 0, /* 6 RmDir        */
        ZT_FMGR_TEST,
        ZT_FMGR_STUB2 + 1, /* 8 GetFreeSpace */
        ZT_FMGR_STUB2 + 2, /* 9 GetLastError */
        ZT_FMGR_STUB2 + 3, /* 10 EnumInit    */
        ZT_FMGR_STUB2 + 4, /* 11 EnumNext    */
        ZT_FMGR_STUB2 + 5, /* 12 Rename      */
        ZT_FMGR_STUB2 + 6, /* 13 EnumNextEx  */
        ZT_FMGR_STUB2 + 7, /* 14 SetDescription */
        ZT_FMGR_STUB2 + 8, /* 15 GetInfoEx   */
        ZT_FMGR_STUB2 + 9, /* 16 Use         */
        ZT_FMGR_STUB2 + 10, /* 17 GetFileUseInfo */
        ZT_FMGR_STUB2 + 11, /* 18 ResolvePath */
        ZT_FMGR_STUB2 + 12, /* 19 CheckPathAccess */
        ZT_FMGR_STUB2 + 13  /* 20 GetFreeSpaceEx */
    };
    return zbrew_make_interface(vtbl, 21, 0);
}

void zbrew_ifile_reset(void) {
    int i;
    for (i = 0; i < ZFILE_MAX; i++) {
        if (g_handles[i]) {
            fclose(g_handles[i]);
            g_handles[i] = NULL;
        }
        ifile_mem_free(i);
        if (g_ggz[i].active)
            ggz_free(&g_ggz[i]);
    }
    memset(g_ifile_pool, 0, sizeof(g_ifile_pool));
    memset(g_ifile_used, 0, sizeof(g_ifile_used));
    memset(g_ifile_slot, 0, sizeof(g_ifile_slot));
    memset(g_ifile_refs, 0, sizeof(g_ifile_refs));
    g_ifile_vtbl = 0;
    pakz_reset();
}

void zbrew_set_file_base(const char *dir) {
    zbrew_ifile_reset();
    if (dir && dir[0]) {
        strncpy(g_base_dir, dir, sizeof(g_base_dir) - 1);
        g_base_dir[sizeof(g_base_dir) - 1] = '\0';
    }
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

    /* Alias estilo Zeemu: fs:/~ e fs:/~0xXXXXXXXX -> pasta do .mod (app root).
     * g_base_dir e a raiz do pacote (antes de /mod/); resources.dat / a3dres.res
     * vivem ao lado do .mod. Paths bare (sem ~) tambem preferem mod_dir — Action
     * Hero abre "a3dres.res" sem prefixo. Fallback: g_base_dir. */
    {
        const char *root = g_base_dir;
        const char *md = zbrew_get_mod_dir();
        int used_tilde = 0;
        if (ncomp > 0 && comps[0][0] == '~') {
            int i;
            if (md && md[0]) root = md;
            for (i = 1; i < ncomp; i++)
                strncpy(comps[i - 1], comps[i], ZVCOMP_LEN);
            ncomp--;
            used_tilde = 1;
        } else if (md && md[0]) {
            root = md;
        }

        snprintf(out, outsz, "%s", root);
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

        /* Bare path sob mod_dir: se nao existe, cai em g_base_dir (udata/, data.vfs). */
        if (!used_tilde && md && md[0] && root == md &&
            strcmp(md, g_base_dir) != 0) {
            FILE *probe = fopen(out, "rb");
            if (probe) {
                fclose(probe);
            } else {
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
            }
        }
    }

    return 1;
}

/* Le um arquivo inteiro por nome: primeiro a pasta da ROM (host), depois
 * o container VFS FUFS (data.vfs). Retorna buffer mallocado em *out. */
int zbrew_read_asset(const char *name, uint8_t **out, uint32_t *out_size) {
    char path[ZPATH_MAX + 260];
    FILE *f;
    long fsz;
    uint8_t *buf = NULL;
    uint32_t size = 0;
    int vi;
    int pi;
    pakz_t *pa;

    if (!out || !out_size) return 0;
    *out = NULL;
    *out_size = 0;
    if (!name || !name[0]) return 0;

    if (resolve_brew_path(path, sizeof(path), name)) {
        f = fopen(path, "rb");
        if (f) {
            if (fseek(f, 0, SEEK_END) == 0 && (fsz = ftell(f)) > 0 &&
                fseek(f, 0, SEEK_SET) == 0) {
                buf = (uint8_t *)malloc((size_t)fsz);
                if (buf && fread(buf, 1, (size_t)fsz, f) == (size_t)fsz) {
                    size = (uint32_t)fsz;
                    LOGI("asset: '%s' -> %ld bytes (host)", name, fsz);
                } else {
                    free(buf); buf = NULL;
                }
            }
            fclose(f);
        }
    }

    if (!buf) {
        vi = vfs_find(name);
        if (vi >= 0 && g_vfs_file) {
            size = g_vfs_toc[vi].size;
            if (size) {
                buf = (uint8_t *)malloc(size);
                if (buf) {
                    if (fseek(g_vfs_file, (long)g_vfs_toc[vi].off, SEEK_SET) != 0 ||
                        fread(buf, 1, size, g_vfs_file) != size) {
                        free(buf); buf = NULL; size = 0;
                    } else {
                        LOGI("asset: '%s' -> %u bytes (VFS)", name, size);
                    }
                } else {
                    size = 0;
                }
            }
        } else if ((pa = pakz_lookup(name, &pi)) != NULL) {
            uint32_t poff = 0;
            pakz_member(pa, pi, &poff, &size);
            if (size) {
                buf = (uint8_t *)malloc(size);
                if (buf) {
                    if (pakz_read(pa, pi, 0, buf, size) != size) {
                        free(buf); buf = NULL; size = 0;
                    } else {
                        LOGI("asset: '%s' -> %u bytes (PAKZ)", name, size);
                    }
                } else {
                    size = 0;
                }
            }
            (void)poff;
        }
    }

    if (!buf || !size) return 0;

    /* PLZP: container Zeebo (magic PLZP + original_size + compressed_size + zlib) */
    if (size >= 12 && buf[0]=='P' && buf[1]=='L' && buf[2]=='Z' && buf[3]=='P') {
        uint32_t orig_sz, comp_sz;
        memcpy(&orig_sz, buf + 4, 4);
        memcpy(&comp_sz, buf + 8, 4);
        if (orig_sz > 0 && orig_sz < (32u*1024u*1024u) && comp_sz > 0 &&
            12u + comp_sz <= size) {
            uint8_t *dec = (uint8_t *)malloc(orig_sz ? orig_sz : 1);
            if (dec) {
                size_t got = tinfl_decompress_mem_to_mem(dec, orig_sz,
                    buf + 12, comp_sz, TINFL_FLAG_PARSE_ZLIB_HEADER);
                if (got != TINFL_DECOMPRESS_MEM_TO_MEM_FAILED && got == orig_sz) {
                    free(buf);
                    buf = dec;
                    size = orig_sz;
                    LOGI("asset: '%s' PLZP decodificado -> %u bytes", name, size);
                } else {
                    free(dec);
                    LOGW("asset: '%s' PLZP falhou (got=%u esperado=%u)",
                         name, (unsigned)got, orig_sz);
                }
            }
        }
    }

    *out = buf;
    *out_size = size;
    return 1;
}

void zbrew_handle_file(uint32_t id) {
    if (id == ZT_UNZIP_SETSTREAM) {
        ifile_raw_write32(g_cpu.r[0] + 4, g_cpu.r[1]);
        g_cpu.r[0] = 0;
        return;
    }
    if (id == ZT_UNZIP_READ) {
        g_cpu.r[0] = zmem_read32(g_cpu.r[0] + 4);
        id = ZT_FILE_READ;
    }

    switch (id) {
    case ZT_FMGR_ADDREF:
    case ZT_FMGR_RELEASE:
        g_cpu.r[0] = 1;
        break;

    case ZT_FILE_ADDREF: {
        int pi = ifile_pool_index(g_cpu.r[0]);
        uint32_t refs = 1;
        if (pi >= 0) {
            g_ifile_refs[pi]++;
            refs = g_ifile_refs[pi];
        }
        g_cpu.r[0] = refs;
        break;
    }

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
        LOGI("IFileMgr_OpenFile: '%s' R1=0x%08X modo=0x%X R0=0x%08X R3=0x%08X PC=0x%08X LR=0x%08X",
             name, g_cpu.r[1], mode, g_cpu.r[0], g_cpu.r[3], g_cpu.r[15], g_cpu.r[14]);
        if (name[0] == (char)0xFD || name[0] == (char)0xE8 || strchr(name, '%')) {
            uint8_t *d = (uint8_t *)zmem_host_ptr(g_cpu.r[1], 64);
            char hex[200];
            int i;
            if (d) {
                hex[0] = 0;
                for (i = 0; i < 64; i++) {
                    char t[8];
                    sprintf(t, "%02X ", d[i]);
                    strcat(hex, t);
                }
                LOGI("  name hex: %s", hex);
            }
            d = (uint8_t *)zmem_host_ptr(g_cpu.r[1] - 16, 32);
            if (d) {
                hex[0] = 0;
                for (i = 0; i < 32; i++) {
                    char t[8];
                    sprintf(t, "%02X ", d[i]);
                    strcat(hex, t);
                }
                LOGI("  @buf-16: %s", hex);
            }
            {
                uint32_t p = 0;
                uint8_t *n = (uint8_t *)zmem_host_ptr(g_cpu.r[1], 4);
                if (n) p = n[0] | (n[1] << 8) | (n[2] << 16) | (n[3] << 24);
                d = (uint8_t *)zmem_host_ptr(p, 64);
                if (d) {
                    hex[0] = 0;
                    for (i = 0; i < 64; i++) {
                        char t[8];
                        sprintf(t, "%02X ", d[i]);
                        strcat(hex, t);
                    }
                    LOGI("  @*name(0x%08X): %s", p, hex);
                }
            }
        }

        if (strcmp(name, "AEEAppletNewE.txt") == 0) {
            g_cpu.r[0] = 0;
            break;
        }
        if (!resolve_brew_path(path, sizeof(path), name)) {
            LOGW("IFileMgr_OpenFile: nome invalido '%s'", name);
            g_cpu.r[0] = 0;
            break;
        }
        for (slot = 0; slot < ZFILE_MAX; slot++)
            if (!g_handles[slot] && !g_ggz[slot].active && !g_mem[slot].active)
                break;
        if (slot == ZFILE_MAX) {
            LOGW("IFileMgr_OpenFile: sem handles livres");
            g_cpu.r[0] = 0;
            break;
        }
        ifile_mem_free(slot);
        g_handles[slot] = fopen(path, writable ? "r+b" : "rb");
        if (!g_handles[slot] && (mode & 0x04u)) { /* _OFM_CREATE */
            /* Garante que a pasta pai existe (ex: udata/) antes de criar */
            char tmp[ZPATH_MAX + 260], *slash;
            snprintf(tmp, sizeof(tmp), "%s", path);
            slash = strrchr(tmp, '/');
            if (slash) {
                *slash = '\0';
                if (!ci_path_exists(tmp))
                    ci_mkdir_recursive(tmp);
            }
            g_handles[slot] = fopen(path, "w+b");
        }
        if (g_handles[slot] && (mode & 0x08u)) /* _OFM_APPEND */
            fseek(g_handles[slot], 0, SEEK_END);
        if (!g_handles[slot]) {
            /* fallback: membro do container VFS do Zeebo (data.vfs) */
            int vi = vfs_find(name);
            int pi;
            pakz_t *pa;
            if (vi >= 0 && g_vfs_file) {
                g_ggz[slot].active = 1;
                g_ggz[slot].vfs = 1;
                g_ggz[slot].host = g_vfs_file;
                g_ggz[slot].off = g_vfs_toc[vi].off;
                g_ggz[slot].total = g_vfs_toc[vi].size;
                g_ggz[slot].pos = 0;
                g_ggz[slot].toc = NULL;
                g_ggz[slot].m = NULL;
                g_ggz[slot].nm = 0;
                LOGD("IFileMgr_OpenFile('%s') -> membro VFS 0x%08X "
                     "(off=%u size=%u)", name, g_vfs_toc[vi].hash,
                     g_vfs_toc[vi].off, g_vfs_toc[vi].size);
            } else if ((pa = pakz_lookup(name, &pi)) != NULL) {
                uint32_t poff = 0, psize = 0;
                pakz_member(pa, pi, &poff, &psize);
                g_ggz[slot].active = 1;
                g_ggz[slot].vfs = 1;
                g_ggz[slot].host = pakz_host(pa);
                g_ggz[slot].off = poff;
                g_ggz[slot].total = psize;
                g_ggz[slot].pos = 0;
                g_ggz[slot].toc = NULL;
                g_ggz[slot].m = NULL;
                g_ggz[slot].nm = 0;
                LOGD("IFileMgr_OpenFile('%s') -> membro PAKZ (off=%u size=%u)",
                     name, poff, psize);
            } else {
                LOGD("IFileMgr_OpenFile: '%s' (modo 0x%X) nao encontrado",
                     name, mode);
                /* Fallback para arquivos .fnz (fonte) ausentes:
                 * criamos um arquivo vazio para o jogo continuar sem travar.
                 * Isso permite jogos como Super BurgerTime carregar mesmo
                 * quando a fonte nao esta no data.vfs. */
                const char *ext = strrchr(name, '.');
                if (ext) {
                    int is_fnz = 0;
#if defined(_WIN32)
                    is_fnz = (_stricmp(ext, ".fnz") == 0);
#else
                    is_fnz = (strcasecmp(ext, ".fnz") == 0);
#endif
                    if (is_fnz) {
                    uint32_t obj = zbrew_create_file_object(slot);
                    if (obj) {
                        g_handles[slot] = NULL; /* sem arquivo host, vazia */
                        LOGI("IFileMgr_OpenFile: '%s' -> arquivo vazio (fallback)", name);
                        g_cpu.r[0] = obj;
                        break;
                    }
                }
                }
                g_cpu.r[0] = 0;
                break;
            }
        }
        if (ggz_has_ext(name) && ggz_build(&g_ggz[slot], path)) {
            fclose(g_handles[slot]);
            g_handles[slot] = NULL;
            LOGD("IFileMgr_OpenFile('%s') -> janela GGZ (toc=%u membros=%u "
                 "total=%u)", name, g_ggz[slot].toc_len, g_ggz[slot].nm,
                 g_ggz[slot].total);
        } else if (g_handles[slot] && !writable) {
            /* RO: mmap host — zip/pkg CUNG deixa de ser 200k fread(1). */
            if (ifile_mem_load(slot, g_handles[slot])) {
                fclose(g_handles[slot]);
                g_handles[slot] = NULL;
                LOGD("IFileMgr_OpenFile('%s') -> mem cache %u bytes",
                     name, g_mem[slot].size);
            }
        }
        {
            uint32_t obj = zbrew_create_file_object(slot);
            if (!obj) {
                if (g_handles[slot]) {
                    fclose(g_handles[slot]);
                    g_handles[slot] = NULL;
                }
                ifile_mem_free(slot);
                g_cpu.r[0] = 0;
                break;
            }
            LOGD("IFileMgr_OpenFile('%s' modo 0x%X) -> IFile 0x%08X", name, mode, obj);
            g_cpu.r[0] = obj;
        }
        break;
    }

    case ZT_FMGR_TEST: {
        char name[256], path[ZPATH_MAX + 260];
        zmem_read_cstr(g_cpu.r[1], name, sizeof(name));
        LOGI("IFileMgr_Test: '%s'", name);
        if (!resolve_brew_path(path, sizeof(path), name)) {
            g_cpu.r[0] = ZBREW_EFAILED;
            break;
        }
        {
            FILE *f = fopen(path, "rb");
            if (f) {
                fclose(f);
                g_cpu.r[0] = ZBREW_SUCCESS;
            } else if (vfs_find(name) >= 0 || pakz_lookup(name, NULL) != NULL) {
                g_cpu.r[0] = ZBREW_SUCCESS;
            } else {
                g_cpu.r[0] = ZBREW_EFAILED;
            }
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
        int pi = ifile_pool_index(self);
        int slot = ifile_slot_of(self);
        uint32_t refs = pi >= 0 ? g_ifile_refs[pi] : 0;
        uint32_t vtbl = ifile_shared_vtbl();
        if (refs > 0) refs--;
        if (pi >= 0) g_ifile_refs[pi] = refs;
        if (refs == 0) {
            if (slot >= 0 && slot < ZFILE_MAX && g_ggz[slot].active)
                ggz_free(&g_ggz[slot]);
            if (slot >= 0 && slot < ZFILE_MAX)
                ifile_mem_free(slot);
            if (slot >= 0 && slot < ZFILE_MAX && g_handles[slot]) {
                fclose(g_handles[slot]);
                g_handles[slot] = NULL;
            }
            if (vtbl) ifile_raw_write32(self, vtbl);
            if (pi >= 0) {
                g_ifile_slot[pi] = -1;
                g_ifile_used[pi] = 0;
            }
        }
        g_cpu.r[0] = refs;
        break;
    }

    case ZT_FILE_READ: {
        uint32_t self = g_cpu.r[0];
        uint32_t buf  = g_cpu.r[1];
        uint32_t len  = g_cpu.r[2];
        int slot = ifile_slot_of(self);
        {
            long rpos = -1L;
            if (slot >= 0 && slot < ZFILE_MAX && g_ggz[slot].active)
                rpos = g_ggz[slot].pos;
            else if (slot >= 0 && slot < ZFILE_MAX && g_mem[slot].active)
                rpos = (long)g_mem[slot].pos;
            else if (slot >= 0 && slot < ZFILE_MAX && g_handles[slot] &&
                     len >= 256)
                rpos = ftell(g_handles[slot]);
            /* So leituras grandes / TOC. Nao logar len=1 em pos alta
             * (zip byte-a-byte: flood stderr → host OOM/kill no DE). */
            if (len >= 256 || (len > 1 && rpos >= 0 && rpos < 32))
                LOGD("IFile_Read slot=%u buf=0x%08X len=%u pos=%ld LR=0x%08X",
                     slot, buf, len, rpos, g_cpu.r[REG_LR]);
        }
        if (len == 0) {
            g_cpu.r[0] = 0;
            break;
        }
        if (slot >= 0 && slot < ZFILE_MAX && g_mem[slot].active) {
            ifile_mem_t *m = &g_mem[slot];
            uint32_t avail = m->pos < m->size ? m->size - m->pos : 0;
            uint32_t n = len < avail ? len : avail;
            if (n)
                zmem_write_block(buf, m->data + m->pos, n);
            m->pos += n;
            /* Zeebo sector driver: pad zeros past EOF. */
            if (n < len) {
                uint8_t z[256];
                uint32_t off = n;
                memset(z, 0, sizeof(z));
                while (off < len) {
                    uint32_t c = len - off < sizeof(z) ? len - off : (uint32_t)sizeof(z);
                    zmem_write_block(buf + off, z, c);
                    off += c;
                }
                n = len;
            }
            g_cpu.r[0] = n;
            break;
        }
        if (slot >= 0 && slot < ZFILE_MAX && g_ggz[slot].active) {
            ggz_win_t *w = &g_ggz[slot];
            uint8_t tmp[4096];
            uint32_t off = 0;
            size_t got = 0;
            while (off < len) {
                size_t chunk = len - off < sizeof(tmp) ? len - off : sizeof(tmp);
                size_t n = ggz_read(w, tmp, (uint32_t)chunk);
                if (n == 0)
                    break; /* fim do arquivo virtual */
                zmem_write_block(buf + off, tmp, (uint32_t)n);
                off += (uint32_t)n;
                got += n;
                if (n < chunk)
                    break;
            }
            if (got < len) {
                memset(tmp, 0, sizeof(tmp));
                while (off < len) {
                    size_t z = len - off < sizeof(tmp) ? len - off : sizeof(tmp);
                    zmem_write_block(buf + off, tmp, (uint32_t)z);
                    off += (uint32_t)z;
                    got += z;
                }
            }
            g_cpu.r[0] = (uint32_t)got;
            break;
        }
        if (slot < 0 || slot >= ZFILE_MAX || !g_handles[slot]) {
            g_cpu.r[0] = 0;
            break;
        }
        {
            void *host = zmem_host_ptr(buf, len);
            size_t got = 0;
            if (host) {
                got = fread(host, 1, len, g_handles[slot]);
                if (got < len && feof(g_handles[slot])) {
                    /* Driver de setor do Zeebo devolve zeros quando o
                     * pedido cruza o fim do arquivo (dump truncado). */
                    memset((uint8_t *)host + got, 0, len - got);
                    got = len;
                }
            } else {
                uint8_t tmp[4096];
                uint32_t off = 0;
                while (off < len) {
                    size_t chunk = len - off < sizeof(tmp) ? len - off : sizeof(tmp);
                    size_t n = fread(tmp, 1, chunk, g_handles[slot]);
                    if (n == 0) {
                        if (feof(g_handles[slot])) {
                            memset(tmp, 0, sizeof(tmp));
                            while (off < len) {
                                size_t z = len - off < sizeof(tmp) ? len - off : sizeof(tmp);
                                zmem_write_block(buf + off, tmp, (uint32_t)z);
                                off += (uint32_t)z;
                                got += z;
                            }
                        }
                        break;
                    }
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
        int slot = ifile_slot_of(self);
        if (slot >= 0 && slot < ZFILE_MAX && g_mem[slot].active) {
            g_cpu.r[0] = 0; /* RO mem cache */
            break;
        }
        if (slot >= 0 && slot < ZFILE_MAX && g_ggz[slot].active) {
            {
                static uint32_t wl = 0;
                if (wl < 6) {
                    LOGI("IFile_Write GGZ negada (slot=%u) buf=0x%08X len=0x%08X "
                         "PC=0x%08X LR=0x%08X", slot, g_cpu.r[1], g_cpu.r[2],
                         g_cpu.r[REG_PC], g_cpu.r[REG_LR]);
                    wl++;
                }
            }
            g_cpu.r[0] = 0;
            break;
        }
        {
        uint32_t buf  = g_cpu.r[1];
        uint32_t len  = g_cpu.r[2];
        if (slot < 0 || slot >= ZFILE_MAX || !g_handles[slot] || len == 0) {
            g_cpu.r[0] = 0;
            break;
        }
        {
            const void *host = zmem_host_ptr(buf, len);
            size_t put = 0;
            if (host) put = fwrite(host, 1, len, g_handles[slot]);
            g_cpu.r[0] = (uint32_t)put;
        }
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
        int slot = ifile_slot_of(self);
        if (slot >= 0 && slot < ZFILE_MAX && g_mem[slot].active) {
            ifile_mem_t *m = &g_mem[slot];
            int64_t np;
            if (origin == 2 && offset == 0) {
                g_cpu.r[0] = m->pos;
                break;
            }
            if (origin == 0) np = offset;
            else if (origin == 1) np = (int64_t)m->size + offset;
            else np = (int64_t)m->pos + offset;
            if (np < 0 || np > (int64_t)m->size) {
                g_cpu.r[0] = ZBREW_EFAILED;
                break;
            }
            m->pos = (uint32_t)np;
            g_cpu.r[0] = ZBREW_SUCCESS;
            break;
        }
        if (slot >= 0 && slot < ZFILE_MAX && g_ggz[slot].active) {
            ggz_win_t *w = &g_ggz[slot];
            {
                static uint32_t seek_logs = 0;
                if (seek_logs < 200) {
                    LOGD("IFile_Seek(GGZ) slot=%u tipo=%u offset=%d "
                         "size=%u pos=%ld", slot, origin, (int)offset,
                         w->total, w->pos);
                    seek_logs++;
                }
            }
            if (origin == 2 && offset == 0)
                g_cpu.r[0] = (uint32_t)w->pos;
            else
                g_cpu.r[0] = ggz_seek(w, origin, offset) == 0
                                 ? ZBREW_SUCCESS : ZBREW_EFAILED;
            break;
        }
        if (slot < 0 || slot >= ZFILE_MAX || !g_handles[slot]) {
            g_cpu.r[0] = ZBREW_EFAILED;
            break;
        }
        {
            static uint32_t seek_logs = 0;
            if (seek_logs < 200) {
                long sz = -1, pos = -1;
                if (slot < ZFILE_MAX && g_handles[slot]) {
                    long cur = ftell(g_handles[slot]);
                    fseek(g_handles[slot], 0, SEEK_END);
                    sz = ftell(g_handles[slot]);
                    fseek(g_handles[slot], cur, SEEK_SET);
                }
                LOGD("IFile_Seek slot=%u tipo=%u offset=%d size=%ld pos=%ld",
                     slot, origin, (int)offset, sz, pos);
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
        int slot = ifile_slot_of(self);
        long cur, size;
        if (slot >= 0 && slot < ZFILE_MAX && g_mem[slot].active) {
            if (!pinfo) {
                g_cpu.r[0] = ZBREW_EFAILED;
                break;
            }
            zmem_write32(pinfo + 0, 0);
            zmem_write32(pinfo + 4, 0);
            zmem_write32(pinfo + 8, g_mem[slot].size);
            zmem_write8(pinfo + 12, 0);
            g_cpu.r[0] = ZBREW_SUCCESS;
            break;
        }
        if (slot >= 0 && slot < ZFILE_MAX && g_ggz[slot].active) {
            if (!pinfo) {
                g_cpu.r[0] = ZBREW_EFAILED;
                break;
            }
            zmem_write32(pinfo + 0, 0);
            zmem_write32(pinfo + 4, 0);
            zmem_write32(pinfo + 8, g_ggz[slot].total);
            zmem_write8(pinfo + 12, 0);
            g_cpu.r[0] = ZBREW_SUCCESS;
            break;
        }
        if (slot < 0 || slot >= ZFILE_MAX || !g_handles[slot] || !pinfo) {
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

    case ZT_FILE_SETCACHE:
        /* zeemu: SetCacheSize -> SUCCESS. Com mem cache ja carregado. */
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;

    case ZT_FILE_MAP: {
        /* Map(pBuf, size, offset): copia range para guest; R0=dst.
         * offset em [SP+4] (ARMCC AAPCS: 4o arg). */
        uint32_t self = g_cpu.r[0];
        uint32_t requested = g_cpu.r[1];
        uint32_t size = g_cpu.r[2];
        uint32_t offset = zmem_read32(g_cpu.r[REG_SP] + 4);
        int slot = ifile_slot_of(self);
        uint32_t avail, to_map, dst;
        const uint8_t *src = NULL;
        long cur = -1;
        if (slot < 0 || slot >= ZFILE_MAX) {
            g_cpu.r[0] = 0;
            break;
        }
        if (g_mem[slot].active) {
            if (offset > g_mem[slot].size) {
                g_cpu.r[0] = 0;
                break;
            }
            avail = g_mem[slot].size - offset;
            src = g_mem[slot].data + offset;
        } else if (g_handles[slot]) {
            long fsz;
            cur = ftell(g_handles[slot]);
            fseek(g_handles[slot], 0, SEEK_END);
            fsz = ftell(g_handles[slot]);
            if (fsz < 0 || offset > (uint32_t)fsz) {
                fseek(g_handles[slot], cur, SEEK_SET);
                g_cpu.r[0] = 0;
                break;
            }
            avail = (uint32_t)fsz - offset;
        } else if (g_ggz[slot].active) {
            if (offset > g_ggz[slot].total) {
                g_cpu.r[0] = 0;
                break;
            }
            avail = g_ggz[slot].total - offset;
        } else {
            g_cpu.r[0] = 0;
            break;
        }
        to_map = size ? (size < avail ? size : avail) : avail;
        dst = (requested && requested < 0xFF000000u)
                  ? requested
                  : zheap_alloc(to_map ? to_map : 1);
        if (!dst) {
            if (cur >= 0 && g_handles[slot])
                fseek(g_handles[slot], cur, SEEK_SET);
            g_cpu.r[0] = 0;
            break;
        }
        if (src) {
            if (to_map) zmem_write_block(dst, src, to_map);
        } else if (g_handles[slot]) {
            uint8_t tmp[4096];
            uint32_t off = 0;
            fseek(g_handles[slot], (long)offset, SEEK_SET);
            while (off < to_map) {
                size_t chunk = to_map - off < sizeof(tmp) ? to_map - off : sizeof(tmp);
                size_t n = fread(tmp, 1, chunk, g_handles[slot]);
                if (!n) break;
                zmem_write_block(dst + off, tmp, (uint32_t)n);
                off += (uint32_t)n;
                if (n < chunk) break;
            }
            to_map = off;
            fseek(g_handles[slot], cur, SEEK_SET);
        } else {
            /* GGZ: le via ggz_read a partir de offset */
            ggz_win_t *w = &g_ggz[slot];
            long saved = w->pos;
            uint8_t tmp[4096];
            uint32_t off = 0;
            w->pos = (long)offset;
            while (off < to_map) {
                size_t chunk = to_map - off < sizeof(tmp) ? to_map - off : sizeof(tmp);
                size_t n = ggz_read(w, tmp, (uint32_t)chunk);
                if (!n) break;
                zmem_write_block(dst + off, tmp, (uint32_t)n);
                off += (uint32_t)n;
                if (n < chunk) break;
            }
            to_map = off;
            w->pos = saved;
        }
        {
            static uint32_t map_logs = 0;
            if (map_logs < 8) {
                LOGI("IFile_Map slot=%u size=%u offset=%u -> 0x%08X (%u)",
                     slot, size, offset, dst, to_map);
                map_logs++;
            }
        }
        g_cpu.r[0] = dst;
        break;
    }

    case ZT_FILE_STUB:
        LOGW("IFile: metodo nao implementado (Truncate/GetInfoEx) "
             "R0=0x%08X R1=0x%08X", g_cpu.r[0], g_cpu.r[1]);
        g_cpu.r[0] = ZBREW_EFAILED;
        break;

case ZT_FMGR_GETINFO: {
        /* IFILEMGR_GetInfo(pfm, nome, FileInfo*) */
        char name[256], path[ZPATH_MAX + 260];
        uint32_t pinfo = g_cpu.r[2];
        FILE *f;
        long size;
        zmem_read_cstr(g_cpu.r[1], name, sizeof(name));
        LOGI("IFileMgr_GetInfo: '%s' pinfo=0x%08X", name, pinfo);
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

    case ZT_FMGR_STUB2 + 1: { /* 8 GetFreeSpace(pfm, pnFreeBytes, pfsFree) */
        if (g_cpu.r[1])
            zmem_write32(g_cpu.r[1], 0x40000000u); /* ~1GB livre */
        if (g_cpu.r[2])
            zmem_write8(g_cpu.r[2], 0); /* _FT_FILESYSTEM */
        /* O DDG trata R0 como BYTES LIVRES (retorna 0 = sem espaco ->
         * "Memory is insufficient"). Devolvemos bastante espaco em R0. */
        g_cpu.r[0] = 0x40000000u;
        break;
    }

    case ZT_FMGR_STUB2 + 2: /* 9 GetLastError -> 0 (sem erro) */
        g_cpu.r[0] = 0;
        break;

    case ZT_FMGR_STUB2 + 13: /* 20 GetFreeSpaceEx(pfm, pnFreeBytes, pnTotalBytes, pfsFree) */
        if (g_cpu.r[1])
            zmem_write32(g_cpu.r[1], 0x40000000u);
        if (g_cpu.r[2])
            zmem_write32(g_cpu.r[2], 0x40000000u);
        if (g_cpu.r[3])
            zmem_write8(g_cpu.r[3], 0);
        LOGI("IFileMgr_GetFreeSpaceEx -> 1GB/1GB, fs=0");
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;

    case ZT_FMGR_STUB2 + 3: { /* 10 EnumInit(pfm, psz) */
        char pattern[128];
        zmem_read_cstr(g_cpu.r[1], pattern, sizeof(pattern));
        LOGI("IFileMgr_EnumInit: pattern='%s' R1=0x%08X", pattern, g_cpu.r[1]);
        enum_scan_dir(pattern[0] ? pattern : NULL);
        LOGI("IFileMgr_EnumInit: %d arquivos encontrados", g_enum_count);
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;
    }

    case ZT_FMGR_STUB2 + 4: { /* 11 EnumNext(pfm, piLen, pbuf) */
        uint32_t piLen = g_cpu.r[1];
        uint32_t pbuf  = g_cpu.r[2];
        if (g_enum_idx < g_enum_count) {
            const char *name = g_enum_names[g_enum_idx];
            uint32_t len = (uint32_t)strlen(name);
            zmem_write_block(pbuf, name, len + 1);
            if (piLen) zmem_write32(piLen, len);
            LOGI("IFileMgr_EnumNext: '%s' (len=%u)", name, len);
            g_enum_idx++;
            g_cpu.r[0] = ZBREW_SUCCESS;
        } else {
            LOGI("IFileMgr_EnumNext: fim da enumeracao");
            g_cpu.r[0] = 10; /* AEE_ENOMORE */
        }
        break;
    }

    case ZT_FMGR_STUB2 + 0:   /* RmDir */
    case ZT_FMGR_STUB2 + 5:   /* Rename */
    case ZT_FMGR_STUB2 + 6:   /* EnumNextEx */
    case ZT_FMGR_STUB2 + 7:   /* SetDescription */
    case ZT_FMGR_STUB2 + 8:   /* GetInfoEx */
    case ZT_FMGR_STUB2 + 9:   /* Use */
    case ZT_FMGR_STUB2 + 10:  /* GetFileUseInfo */
    case ZT_FMGR_STUB2 + 11:  /* ResolvePath */
    case ZT_FMGR_STUB2 + 12: { /* CheckPathAccess */
        static const char *const names[] = {
            "RmDir", "GetFreeSpace", "GetLastError", "EnumInit", "EnumNext",
            "Rename", "EnumNextEx", "SetDescription", "GetInfoEx", "Use",
            "GetFileUseInfo", "ResolvePath", "CheckPathAccess",
            "GetFreeSpaceEx"
        };
        unsigned n = (unsigned)(id - ZT_FMGR_STUB2);
        const char *nm = n < 14 ? names[n] : "?";
        LOGW("IFileMgr_%s nao implementado R1=0x%08X R2=0x%08X LR=0x%08X",
             nm, g_cpu.r[1], g_cpu.r[2], g_cpu.r[14]);
        g_cpu.r[0] = ZBREW_EFAILED;
        break;
    }
    }
}
