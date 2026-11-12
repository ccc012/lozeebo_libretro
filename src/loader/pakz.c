/* pakz.c - leitor do container "PACK" (.pakz). Ver pakz.h. */
#include "pakz.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define PAKZ_NAME_MAX 56
#define PAKZ_MAX_MEMBERS 65536

typedef struct {
    char name[PAKZ_NAME_MAX];
    uint32_t off;
    uint32_t size;
} pakz_ent_t;

struct pakz {
    FILE *host;
    pakz_ent_t *ent;
    int count;
    int cap;
};

static uint32_t rd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Normaliza um nome de membro para comparacao: minusculas, '/' como
 * separador, sem prefixo "fs:/", sem "./" ou '/' inicial. */
static void pakz_norm(const char *in, char *out, size_t outsz) {
    const char *p = in;
    size_t n = 0;
    if (!in) { out[0] = '\0'; return; }
    if ((p[0] == 'f' || p[0] == 'F') && (p[1] == 's' || p[1] == 'S') &&
        p[2] == ':' && (p[3] == '/' || p[3] == '\\'))
        p += 4;
    for (;;) {
        if (p[0] == '.' && (p[1] == '/' || p[1] == '\\')) { p += 2; continue; }
        if (p[0] == '/' || p[0] == '\\') { p++; continue; }
        break;
    }
    for (; *p && n + 1 < outsz; ++p) {
        char c = (*p == '\\') ? '/' : *p;
        if (c >= 'A' && c <= 'Z') c += 0x20;
        out[n++] = c;
    }
    out[n] = '\0';
}

pakz_t *pakz_open(const char *path) {
    FILE *f;
    uint8_t hdr[12];
    uint8_t *toc;
    uint32_t toc_off, toc_size;
    long fsz;
    int n, i;
    pakz_t *p;

    if (!path || !path[0]) return NULL;
    f = fopen(path, "rb");
    if (!f) return NULL;
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "PACK", 4) != 0) {
        fclose(f);
        return NULL;
    }
    toc_off = rd_le32(hdr + 4);
    toc_size = rd_le32(hdr + 8);
    if (toc_off < 12 || toc_size == 0 || (toc_size % 64) != 0) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    fsz = ftell(f);
    if ((long)toc_off + (long)toc_size > fsz) {
        fclose(f);
        return NULL;
    }
    n = (int)(toc_size / 64);
    if (n <= 0 || n > PAKZ_MAX_MEMBERS) {
        fclose(f);
        return NULL;
    }
    toc = (uint8_t *)malloc((size_t)n * 64);
    if (!toc) {
        fclose(f);
        return NULL;
    }
    fseek(f, (long)toc_off, SEEK_SET);
    if (fread(toc, 64, (size_t)n, f) != (size_t)n) {
        free(toc);
        fclose(f);
        return NULL;
    }
    p = (pakz_t *)calloc(1, sizeof(*p));
    if (!p) {
        free(toc);
        fclose(f);
        return NULL;
    }
    p->ent = (pakz_ent_t *)calloc((size_t)n, sizeof(*p->ent));
    if (!p->ent) {
        free(p);
        free(toc);
        fclose(f);
        return NULL;
    }
    p->host = f;
    p->cap = n;
    for (i = 0; i < n; i++) {
        const uint8_t *e = toc + (size_t)i * 64;
        char raw[PAKZ_NAME_MAX + 1];
        uint32_t off, size;
        int k = 0;
        while (k < PAKZ_NAME_MAX && e[k]) { raw[k] = (char)e[k]; k++; }
        raw[k] = '\0';
        off = rd_le32(e + PAKZ_NAME_MAX);
        size = rd_le32(e + PAKZ_NAME_MAX + 4);
        if (!raw[0] || size == 0) continue;
        if (off < 12 || (long)off + (long)size > fsz) continue;
        pakz_norm(raw, p->ent[p->count].name, PAKZ_NAME_MAX);
        if (!p->ent[p->count].name[0]) continue;
        p->ent[p->count].off = off;
        p->ent[p->count].size = size;
        p->count++;
    }
    free(toc);
    if (p->count == 0) {
        pakz_close(p);
        return NULL;
    }
    return p;
}

void pakz_close(pakz_t *p) {
    if (!p) return;
    if (p->host) fclose(p->host);
    free(p->ent);
    free(p);
}

int pakz_count(const pakz_t *p) { return p ? p->count : 0; }
FILE *pakz_host(const pakz_t *p) { return p ? p->host : NULL; }

int pakz_find(const pakz_t *p, const char *name) {
    char norm[PAKZ_NAME_MAX * 2];
    int i;
    if (!p || p->count <= 0 || !name || !name[0]) return -1;
    pakz_norm(name, norm, sizeof(norm));
    if (!norm[0]) return -1;
    for (i = 0; i < p->count; i++)
        if (strcmp(p->ent[i].name, norm) == 0) return i;
    return -1;
}

int pakz_member(const pakz_t *p, int idx, uint32_t *off, uint32_t *size) {
    if (!p || idx < 0 || idx >= p->count) return 0;
    if (off) *off = p->ent[idx].off;
    if (size) *size = p->ent[idx].size;
    return 1;
}

uint32_t pakz_read(const pakz_t *p, int idx, uint32_t member_off,
                   void *dst, uint32_t len) {
    uint32_t size, got = 0;
    if (!p || !dst || idx < 0 || idx >= p->count) return 0;
    size = p->ent[idx].size;
    if (member_off > size) return 0;
    if (len > size - member_off) len = size - member_off;
    if (len == 0) return 0;
    if (fseek(p->host, (long)(p->ent[idx].off + member_off), SEEK_SET) != 0)
        return 0;
    got = (uint32_t)fread(dst, 1, len, p->host);
    return got;
}
