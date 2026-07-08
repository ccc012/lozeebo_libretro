/* bar_parser.c - Recursos BAR (fase 2.3)
 *
 * O formato BAR (BREW Archive) requer reverse engineering.
 * Por enquanto apenas verifica se o arquivo existe e loga o tamanho,
 * para o jogo poder acessa-lo via IFile normalmente.
 */
#include <stdio.h>
#include <string.h>
#include "mod_loader.h"
#include "../debug/log.h"

bool zbar_probe(const char *bar_path) {
    FILE *f = fopen(bar_path, "rb");
    long size;
    unsigned char hdr[16];
    if (!f) return false;
    if (fread(hdr, 1, sizeof(hdr), f) < 8) {
        fclose(f);
        return false;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fclose(f);
    if (memcmp(hdr, "BAR", 3) == 0 || memcmp(hdr, "BREW", 4) == 0) {
        LOGI("bar: '%s' parece valido (%ld bytes)", bar_path, size);
        return true;
    }
    LOGI("bar: '%s' presente (%ld bytes) - magic desconhecido", bar_path, size);
    return true;
}
