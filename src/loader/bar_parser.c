/* bar_parser.c - Parser de arquivos BAR (BREW Archive)
 *
 * Portado da logica do zeemu BrewShellResources.cpp (GPLv3).
 * Reimplementado em C99 para o core LOZeebo.
 *
 * Formato BAR:
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
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "bar_parser.h"
#include "../debug/log.h"

static uint16_t read_u16_le(const uint8_t *data, size_t off) {
    if (off + 1 > 0xFFFFFFFFu) return 0;
    return (uint16_t)(data[off] | ((uint16_t)data[off + 1] << 8));
}

static uint32_t read_u32_le(const uint8_t *data, size_t off) {
    return (uint32_t)(data[off] | ((uint32_t)data[off + 1] << 8) |
                      ((uint32_t)data[off + 2] << 16) |
                      ((uint32_t)data[off + 3] << 24));
}

/* Stub de compatibilidade: mod_loader.chama zbar_probe para verificar se
 * um arquivo .bar existe. */
bool zbar_probe(const char *bar_path) {
    FILE *f = fopen(bar_path, "rb");
    if (!f) return false;
    fclose(f);
    LOGI("bar: probe '%s' -> existe", bar_path);
    return true;
}

bool zbar_parse_file(const char *path, bar_resource_t *bar) {
    if (!path || !bar) return false;

    FILE *f = fopen(path, "rb");
    if (!f) {
        LOGW("bar: nao abriu '%s'", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size < 0x3C) {
        fclose(f);
        LOGW("bar: '%s' muito pequeno (%ld bytes)", path, file_size);
        return false;
    }

    uint8_t *data = (uint8_t *)malloc((size_t)file_size);
    if (!data) {
        fclose(f);
        return false;
    }
    size_t rd = fread(data, 1, (size_t)file_size, f);
    fclose(f);
    if (rd != (size_t)file_size) {
        free(data);
        return false;
    }

    memset(bar, 0, sizeof(*bar));
    bar->raw_data = data;  /* guarda para liberar depois em zbar_free */

    uint16_t range_count = read_u16_le(data, 0x06);
    uint32_t range_table = read_u32_le(data, 0x08);
    uint32_t offset_table = read_u32_le(data, 0x10);
    uint32_t first_blob = read_u32_le(data, 0x18);

    if (range_count == 0 || range_table < 0x20 ||
        range_table + (uint32_t)range_count * 8u > (uint32_t)file_size ||
        offset_table <= range_table ||
        first_blob <= offset_table || first_blob > (uint32_t)file_size) {
        LOGW("bar: '%s' cabecalho invalido (ranges=%u rt=0x%X ot=0x%X fb=0x%X)",
             path, range_count, range_table, offset_table, first_blob);
        free(data);
        return false;
    }

    /* Coleta offsets de blobs da offset_table */
    uint32_t blob_offsets[BAR_MAX_ENTRIES + 1];
    int blob_count = 0;
    for (uint32_t off = offset_table; off + 4 <= first_blob && blob_count < BAR_MAX_ENTRIES; off += 4) {
        uint32_t blob_off = read_u32_le(data, off);
        if (blob_off >= first_blob && blob_off <= (uint32_t)file_size) {
            blob_offsets[blob_count++] = blob_off;
        }
    }
    if (blob_count == 0 || blob_offsets[blob_count - 1] != (uint32_t)file_size) {
        blob_offsets[blob_count++] = (uint32_t)file_size;
    }

    /* Processa cada range */
    bar->range_count = range_count;
    uint32_t entry_idx = 0;

    for (uint16_t range = 0; range < range_count && entry_idx < BAR_MAX_ENTRIES; range++) {
        uint32_t desc = range_table + (uint32_t)range * 8u;
        uint16_t type = read_u16_le(data, desc);
        uint16_t base_id = read_u16_le(data, desc + 2);
        uint16_t count = read_u16_le(data, desc + 4);
        uint16_t first_index = read_u16_le(data, desc + 6);

        if (count == 0 || first_index >= (uint16_t)blob_count) continue;

        for (uint16_t i = 0; i < count && entry_idx < BAR_MAX_ENTRIES; i++) {
            size_t index = (size_t)first_index + i;
            if (index + 1 >= (size_t)blob_count) break;

            uint32_t start = blob_offsets[index];
            uint32_t end = blob_offsets[index + 1];
            if (start + 2 >= end || end > (uint32_t)file_size) continue;

            uint8_t data_offset = data[start];
            if (data[start + 1] != 0 || data_offset < 3 || start + data_offset > end) continue;

            size_t mime_start = start + 2;
            size_t mime_end = start + data_offset;
            /* Procura null terminator no MIME */
            for (size_t m = mime_start; m < mime_end; m++) {
                if (data[m] == 0) { mime_end = m; break; }
            }

            bar_entry_t *e = &bar->entries[entry_idx++];
            e->id = (uint16_t)(base_id + i);
            e->type = type;
            size_t mime_len = mime_end - mime_start;
            if (mime_len >= sizeof(e->mime)) mime_len = sizeof(e->mime) - 1;
            memcpy(e->mime, data + mime_start, mime_len);
            e->mime[mime_len] = 0;
            e->data_offset = start + data_offset;
            e->data_size = end - (start + data_offset);
            e->raw_offset = start;
            e->raw_size = end - start;
            e->data_ptr = data + e->data_offset;
        }
    }

    bar->entry_count = entry_idx;
    bar->parsed = true;

    /* Guarda o ponteiro para os dados (precisamos manter data alocado) */
    /* Infelizmente o bar_resource_t nao tem campo para o buffer raw.
     * O caller deve manter o buffer BAR vivo enquanto usar as entradas. */
    /* Para simplificar, vamos alocar e guardar o buffer no bar_resource_t.
     * Vamos adicionar um campo para isso... mas nao temos no header.
     * Vamos usar um truque: as entradas apontam para data, e o caller
     * deve garantir que o buffer BAR viva enquanto usar o parser. */

    LOGI("bar: '%s' parseado OK: %u ranges, %u entries",
         path, range_count, entry_idx);
    /* Nao liberamos data aqui - fica em bar->raw_data para as entradas
     * poderem acessar. Liberado em zbar_free(). */
    return true;
}

bar_entry_t *zbar_find_entry(bar_resource_t *bar, uint16_t res_id) {
    if (!bar || !bar->parsed) return NULL;
    for (uint32_t i = 0; i < bar->entry_count; i++) {
        if (bar->entries[i].id == res_id)
            return &bar->entries[i];
    }
    return NULL;
}

void zbar_free(bar_resource_t *bar) {
    if (!bar) return;
    if (bar->raw_data) {
        free(bar->raw_data);
        bar->raw_data = NULL;
    }
    memset(bar, 0, sizeof(*bar));
}
