/* zip_extract.c - Extracao de ZIP via miniz (sem dependencia externa) */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "mod_loader.h"
#include "../debug/log.h"
#include "../audio/miniz_tinfl.h"

/* ZIP structures (little-endian) */
#pragma pack(push, 1)
typedef struct {
    uint32_t sig;       /* 0x04034b50 */
    uint16_t version;
    uint16_t flags;
    uint16_t method;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t name_len;
    uint16_t extra_len;
} zip_local_file_t;

typedef struct {
    uint32_t sig;       /* 0x02014b50 */
    uint16_t version_made;
    uint16_t version_needed;
    uint16_t flags;
    uint16_t method;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t name_len;
    uint16_t extra_len;
    uint16_t comment_len;
    uint16_t disk_start;
    uint16_t internal_attr;
    uint32_t external_attr;
    uint32_t local_header_offset;
} zip_central_dir_t;
#pragma pack(pop)

static uint32_t zip_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t zip_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int has_mod_ext(const char *name) {
    size_t n = strlen(name);
    if (n < 4) return 0;
    const char *ext = name + n - 4;
    if ((ext[0] == '.') &&
        ((ext[1]=='m'||ext[1]=='M') && (ext[2]=='o'||ext[2]=='O') && (ext[3]=='d'||ext[3]=='D')))
        return 3;
    if ((ext[0] == '.') &&
        ((ext[1]=='b'||ext[1]=='B') && (ext[2]=='a'||ext[2]=='A') && (ext[3]=='r'||ext[3]=='R')))
        return 2;
    if ((ext[0] == '.') &&
        ((ext[1]=='m'||ext[1]=='M') && (ext[2]=='i'||ext[2]=='I') && (ext[3]=='f'||ext[3]=='F')))
        return 1;
    return 0;
}

bool zmod_extract_from_zip(const void *zip_data, size_t zip_size,
                           void **out_data, size_t *out_size, char **out_path) {
    const uint8_t *bytes = (const uint8_t *)zip_data;
    uint32_t eocd_offset = 0;
    uint32_t cd_offset = 0;
    uint32_t cd_size = 0;
    uint32_t cd_entries = 0;
    int best_priority = 0;
    uint32_t best_offset = 0, best_comp = 0, best_uncomp = 0;
    uint16_t best_method = 0;
    char best_name[256] = {0};

    if (zip_size < 22) return false;

    /* Find End of Central Directory record */
    for (uint32_t i = (uint32_t)zip_size - 22; i > (zip_size > 0xFFFF ? zip_size - 0x10016 : 0); i--) {
        if (bytes[i] == 'P' && bytes[i+1] == 'K' && bytes[i+2] == 0x05 && bytes[i+3] == 0x06) {
            eocd_offset = i;
            break;
        }
    }
    if (!eocd_offset) return false;

    cd_entries = zip_le16(bytes + eocd_offset + 10);
    cd_size = zip_le32(bytes + eocd_offset + 12);
    cd_offset = zip_le32(bytes + eocd_offset + 16);
    if (cd_offset == 0xFFFFFFFF || cd_size == 0xFFFFFFFF) return false;
    if (cd_offset + cd_size > zip_size) return false;

    /* Scan central directory */
    uint32_t pos = cd_offset;
    for (uint32_t i = 0; i < cd_entries && pos + 46 <= cd_offset + cd_size; i++) {
        if (bytes[pos] != 'P' || bytes[pos+1] != 'K' || bytes[pos+2] != 0x01 || bytes[pos+3] != 0x02)
            break;
        uint16_t method   = zip_le16(bytes + pos + 10);
        uint32_t comp_sz  = zip_le32(bytes + pos + 20);
        uint32_t uncomp_sz = zip_le32(bytes + pos + 24);
        uint16_t name_len = zip_le16(bytes + pos + 28);
        uint16_t extra_len = zip_le16(bytes + pos + 30);
        uint16_t comment_len = zip_le16(bytes + pos + 32);
        uint32_t local_offset = zip_le32(bytes + pos + 42);

        char name[256] = {0};
        uint32_t copy_len = name_len < 255 ? name_len : 255;
        memcpy(name, bytes + pos + 46, copy_len);

        int priority = has_mod_ext(name);
        if (priority > best_priority ||
            (priority == best_priority && priority > 0 && comp_sz < best_comp)) {
            best_priority = priority;
            best_offset = local_offset;
            best_comp = comp_sz;
            best_uncomp = uncomp_sz;
            best_method = method;
            strncpy(best_name, name, sizeof(best_name) - 1);
        }
        pos += 46 + name_len + extra_len + comment_len;
    }

    if (best_priority == 0) {
        LOGW("zip: nenhum .mod/.bar/.mif encontrado no ZIP (%u entries)", cd_entries);
        return false;
    }

    /* Read local file header */
    if (best_offset + 30 > zip_size) return false;
    if (bytes[best_offset] != 'P' || bytes[best_offset+1] != 'K' ||
        bytes[best_offset+2] != 0x03 || bytes[best_offset+3] != 0x04)
        return false;

    uint16_t l_name_len = zip_le16(bytes + best_offset + 26);
    uint16_t l_extra_len = zip_le16(bytes + best_offset + 28);
    uint32_t data_start = best_offset + 30 + l_name_len + l_extra_len;
    if (data_start + best_comp > zip_size) return false;

    /* Decompress */
    uint8_t *out_buf = (uint8_t *)malloc(best_uncomp ? best_uncomp : 1);
    if (!out_buf) return false;

    if (best_method == 0) {
        /* Stored */
        memcpy(out_buf, bytes + data_start, best_comp);
    } else if (best_method == 8) {
        /* Deflate */
        size_t out_pos = 0;
        tinfl_status st = tinfl_decompress_mem_to_mem(
            out_buf, best_uncomp, bytes + data_start, best_comp, 0);
        if (st != TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
            out_pos = best_uncomp;
        } else {
            free(out_buf);
            LOGW("zip: falha ao descompactar '%s'", best_name);
            return false;
        }
    } else {
        free(out_buf);
        LOGW("zip: metodo %d nao suportado para '%s'", best_method, best_name);
        return false;
    }

    *out_data = out_buf;
    *out_size = (size_t)best_uncomp;
    *out_path = _strdup(best_name);
    LOGI("zip: extraido '%s' (%u bytes, method=%d)", best_name, best_uncomp, best_method);
    return true;
}
