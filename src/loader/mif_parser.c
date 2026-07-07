/* mif_parser.c - Extracao basica de metadados do MIF
 *
 * O formato MIF completo requer reverse engineering (fase 2.3).
 * Por enquanto: procura um .mif com o mesmo nome da ROM e extrai
 * a primeira string ASCII imprimivel longa como nome do jogo.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "mod_loader.h"
#include "../debug/log.h"

bool zmif_parse_name(const char *mod_path, char *name_out, size_t maxlen) {
    char mif_path[512];
    size_t len = strlen(mod_path);
    FILE *f;

    if (len + 1 > sizeof(mif_path)) return false;
    strcpy(mif_path, mod_path);

    /* troca a extensao por .mif */
    {
        char *dot = strrchr(mif_path, '.');
        if (!dot || (size_t)(dot - mif_path) + 5 > sizeof(mif_path))
            return false;
        strcpy(dot, ".mif");
    }

    f = fopen(mif_path, "rb");
    if (!f) return false;

    {
        unsigned char buf[4096];
        size_t n = fread(buf, 1, sizeof(buf), f);
        size_t i, run_start = 0, run_len = 0;
        size_t best_start = 0, best_len = 0;

        for (i = 0; i <= n; i++) {
            if (i < n && isprint(buf[i]) && buf[i] < 0x7F) {
                if (run_len == 0) run_start = i;
                run_len++;
            } else {
                if (run_len > best_len) {
                    best_len = run_len;
                    best_start = run_start;
                }
                run_len = 0;
            }
        }
        fclose(f);

        if (best_len >= 4) {
            size_t copy = best_len < maxlen - 1 ? best_len : maxlen - 1;
            memcpy(name_out, buf + best_start, copy);
            name_out[copy] = '\0';
            LOGI("mif: nome extraido '%s'", name_out);
            return true;
        }
    }
    return false;
}
