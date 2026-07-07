/* bar_parser.c - Recursos BAR (fase 2.3)
 *
 * O formato BAR (BREW Archive) requer reverse engineering.
 * Por enquanto apenas verifica se o arquivo existe e loga o tamanho,
 * para o jogo poder acessa-lo via IFile normalmente.
 */
#include <stdio.h>
#include "mod_loader.h"
#include "../debug/log.h"

bool zbar_probe(const char *bar_path) {
    FILE *f = fopen(bar_path, "rb");
    long size;
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fclose(f);
    LOGI("bar: '%s' presente (%ld bytes) - parse na fase 2.3", bar_path, size);
    return true;
}
