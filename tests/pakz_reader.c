/* pakz_reader.c - teste do leitor de container "PACK" (.pakz).
 *
 * Monta um arquivo PACK sintetico (header + 2 membros + TOC de 64 bytes)
 * e valida: abertura, contagem, busca por nome (com ./, fs:/, \ e caixa),
 * leitura com offset e rejeicao de arquivo invalido.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loader/pakz.h"

static int failures = 0;

static void check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static void wr32(FILE *f, unsigned v) {
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
    fputc((int)((v >> 16) & 0xFF), f);
    fputc((int)((v >> 24) & 0xFF), f);
}

static void wr_entry(FILE *f, const char *name, unsigned off, unsigned size) {
    int i;
    for (i = 0; i < 56; i++) fputc(i < (int)strlen(name) ? name[i] : 0, f);
    wr32(f, off);
    wr32(f, size);
}

int main(void) {
    const char *path = "pakz_reader_selftest.pakz";
    const char *bad = "pakz_reader_selftest.bad";
    const char *data0 = "RIFF-WAVE-COUNT-1"; /* 18 bytes */
    const char *data1 = "TEXTURE-QXT";        /* 11 bytes */
    unsigned d0 = (unsigned)strlen(data0);
    unsigned d1 = (unsigned)strlen(data1);
    unsigned toc_off = 12 + d0 + d1;
    FILE *f = fopen(path, "wb");
    pakz_t *p;
    unsigned off = 0, size = 0;
    char buf[64];

    if (!f) { fprintf(stderr, "cannot create %s\n", path); return 2; }

    fwrite("PACK", 1, 4, f);
    wr32(f, toc_off);
    wr32(f, 2u * 64u);
    fwrite(data0, 1, d0, f);
    fwrite(data1, 1, d1, f);
    wr_entry(f, "AUDIO/XUI/COUNT_1.WAV", 12, d0);
    wr_entry(f, "texture/a.qxt", 12 + d0, d1);
    fclose(f);

    p = pakz_open(path);
    check(p != NULL, "pakz_open valid archive");
    if (!p) return 1;

    check(pakz_count(p) == 2, "pakz_count == 2");
    check(pakz_host(p) != NULL, "host file available");

    /* busca: nome exato armazenado em maiusculas */
    check(pakz_find(p, "audio/xui/count_1.wav") >= 0, "find lowercase");
    /* prefixos/separadores aceitos pelo normalizador */
    check(pakz_find(p, ".//audio/xui/count_1.wav") >= 0, "find with ./ prefix");
    check(pakz_find(p, "fs:/audio/xui/count_1.wav") >= 0, "find with fs:/ prefix");
    check(pakz_find(p, "audio\\xui\\count_1.wav") >= 0, "find with backslashes");
    check(pakz_find(p, "AUDIO/XUI/COUNT_1.WAV") >= 0, "find uppercase");
    check(pakz_find(p, "audio/xui/missing.wav") < 0, "missing member rejected");

    /* metadados e leitura completa */
    {
        int idx = pakz_find(p, "audio/xui/count_1.wav");
        check(pakz_member(p, idx, &off, &size) == 1, "member info");
        check(off == 12 && size == d0, "member offset/size");
        memset(buf, 0, sizeof(buf));
        check(pakz_read(p, idx, 0, buf, d0) == d0, "read full size");
        check(memcmp(buf, data0, d0) == 0, "read full bytes");
        /* leitura com offset e clamp no fim do membro */
        memset(buf, 0, sizeof(buf));
        check(pakz_read(p, idx, 5, buf, 4) == 4, "read with offset");
        check(memcmp(buf, data0 + 5, 4) == 0, "read offset bytes");
        memset(buf, 0, sizeof(buf));
        check(pakz_read(p, idx, 3, buf, 100) == d0 - 3, "read clamps at end");
        check(pakz_read(p, idx, d0 + 10, buf, 4) == 0, "read past end is 0");
    }

    pakz_close(p);

    /* arquivo sem magic PACK deve ser rejeitado */
    f = fopen(bad, "wb");
    if (f) {
        fwrite("NOPE", 1, 4, f);
        fclose(f);
        check(pakz_open(bad) == NULL, "invalid magic rejected");
        remove(bad);
    }

    remove(path);
    if (failures) {
        fprintf(stderr, "%d falha(s)\n", failures);
        return 1;
    }
    printf("pakz_reader: OK\n");
    return 0;
}
