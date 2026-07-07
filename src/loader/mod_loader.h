/* mod_loader.h - Carregador de ROMs (arquivos MOD) */
#ifndef ZEEBO_MOD_LOADER_H
#define ZEEBO_MOD_LOADER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint32_t entry;      /* endereco de entrada (PC inicial)     */
    uint32_t code_base;  /* onde o codigo foi carregado          */
    uint32_t code_size;  /* tamanho carregado                    */
    char     name[64];   /* nome (do MIF, se disponivel)         */
} zmod_info_t;

/* Carrega os dados da ROM na memoria emulada e prepara a CPU.
 *
 * Formato atual: binario ARM "flat" carregado em 0x1000 com entry
 * no proprio 0x1000 (docs 09). O parse do header MOD real da BREW
 * requer reverse engineering (fase 2.2) - quando o formato for
 * documentado, detectar aqui e ajustar o entry point.
 *
 * data/size: conteudo do arquivo (do RetroArch)
 * path: caminho do arquivo (para achar assets e o .mif ao lado)
 */
bool zmod_load(const void *data, size_t size, const char *path,
               zmod_info_t *out);

/* mif_parser.c: extrai o nome do jogo de um .mif ao lado da ROM */
bool zmif_parse_name(const char *mod_path, char *name_out, size_t maxlen);

/* bar_parser.c: enumeracao basica de recursos BAR (fase 2.3) */
bool zbar_probe(const char *bar_path);

#endif /* ZEEBO_MOD_LOADER_H */
