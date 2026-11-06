#ifndef LOADER_BAR_PARSER_H
#define LOADER_BAR_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* =========================================================================
 * BAR (BREW Archive) resource parser
 * Portado da logica do zeemu BrewShellResources.cpp (GPLv3).
 *
 * Formato BAR (resumido):
 *   0x06: range_count (u16 LE)
 *   0x08: range_table offset (u32 LE)
 *   0x10: offset_table offset (u32 LE)
 *   0x18: first_blob offset (u32 LE)
 *
 * Cada range descriptor (8 bytes em range_table):
 *   +0: type (u16 LE), +2: base_id (u16 LE), +4: count (u16 LE), +6: first_index (u16 LE)
 *
 * Cada blob em offset_table começa com:
 *   [0]: data_offset (u8), [1]: 0x00, [2..data_offset]: MIME string (null-term),
 *   [data_offset..end]: dados reais do recurso
 *
 * Cada recurso tem ID = base_id + i.
 * ========================================================================= */

#define BAR_MAX_ENTRIES 256
#define BAR_MAX_RANGES 256

typedef struct {
    uint16_t id;
    uint16_t type;
    char mime[64];
    uint32_t data_offset;   /* offset dentro do BAR para o dado real */
    uint32_t data_size;     /* tamanho do dado */
    uint32_t raw_offset;    /* offset para o dado raw (inclui header MIME) */
    uint32_t raw_size;      /* tamanho raw */
    const uint8_t *data_ptr; /* ponteiro para os dados no buffer BAR */
} bar_entry_t;

typedef struct {
    uint16_t range_count;
    uint16_t base_id;
    uint16_t type;
    uint32_t entry_count;
    bar_entry_t entries[BAR_MAX_ENTRIES];
    bool parsed;
    /* Buffer dos dados do BAR (mantido vivo enquanto o bar_resource estiver em uso) */
    uint8_t *raw_data;
    uint32_t raw_data_size;
} bar_resource_t;

/* Carrega e parseia um arquivo BAR do disco. Retorna true se sucesso. */
bool zbar_parse_file(const char *path, bar_resource_t *bar);

/* Stub de compatibilidade para mod_loader.c */
bool zbar_probe(const char *bar_path);

/* Procura uma entrada por resource ID. Retorna ponteiro ou NULL. */
bar_entry_t *zbar_find_entry(bar_resource_t *bar, uint16_t res_id);

/* Libera recursos alocados pelo parser. */
void zbar_free(bar_resource_t *bar);

#endif /* LOADER_BAR_PARSER_H */
