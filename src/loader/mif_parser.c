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
#include "../brew/aee_ids.h"
#include "../debug/log.h"

/* Procura CLSIDs de applets conhecidos dentro de um arquivo */
static uint32_t scan_file_for_clsid(const char *path) {
    FILE *f = fopen(path, "rb");
    unsigned char buf[32768];
    size_t n, i, k;
    if (!f) return 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (i = 0; i + 4 <= n; i++) {
            uint32_t v = (uint32_t)buf[i] | ((uint32_t)buf[i+1] << 8) |
                         ((uint32_t)buf[i+2] << 16) | ((uint32_t)buf[i+3] << 24);
            for (k = 0; k < ZAEE_KNOWN_APPLET_COUNT; k++) {
                if (v == ZAEE_KNOWN_APPLETS[k].clsid) {
                    LOGI("mif: applet '%s' (0x%08X) em %s",
                         ZAEE_KNOWN_APPLETS[k].name, v, path);
                    fclose(f);
                    return v;
                }
            }
        }
    }
    fclose(f);
    return 0;
}

static uint32_t clsid_from_path_hint(const char *mod_path) {
    if (!mod_path) return 0;
    if (strstr(mod_path, "Zeebo Family Pack") || strstr(mod_path, "family_pack") ||
        strstr(mod_path, "family-pack") || strstr(mod_path, "FAMILYPACK")) {
        return 0x010903C6u;
    }
    if (strstr(mod_path, "Pac-Mania") || strstr(mod_path, "pacmania") ||
        strstr(mod_path, "PAC-MANIA") || strstr(mod_path, "PACMANIA")) {
        return 0x01087B72u;
    }
    if (strstr(mod_path, "Double Dragon") || strstr(mod_path, "doubledragon") ||
        strstr(mod_path, "DOUBLE DRAGON")) {
        return 0x0102F789u;
    }
    if (strstr(mod_path, "Quake II") || strstr(mod_path, "quake2") ||
        strstr(mod_path, "QUAKE2") || strstr(mod_path, "QUAKE II")) {
        return 0x01087C1Cu;
    }
    if (strstr(mod_path, "Quake") || strstr(mod_path, "quake") ||
        strstr(mod_path, "QUAKE")) {
        return 0x01087A3Cu;
    }
    if (strstr(mod_path, "Z-Wheel") || strstr(mod_path, "zwheel") ||
        strstr(mod_path, "Z_WHEEL")) {
        return 0x01070798u;
    }
    if (strstr(mod_path, "Zeeboids") || strstr(mod_path, "zeeboids") ||
        strstr(mod_path, "ZEEBOIDS")) {
        return 0x0108FF1Au;
    }
    return 0;
}

uint32_t zmif_find_applet_clsid(const char *mod_path) {
    char candidate[1024];
    uint32_t clsid;
    const char *sep, *s1, *s2;

    if (!mod_path) return 0;

    {
        uint32_t hinted = clsid_from_path_hint(mod_path);
        if (hinted) {
            LOGI("mif: clsid inferido por nome de caminho 0x%08X em %s",
                 hinted, mod_path);
            return hinted;
        }
    }

    /* 1. O proprio MOD costuma conter o CLSID (AEEAppInfo estatico) */
    clsid = scan_file_for_clsid(mod_path);
    if (clsid) return clsid;

    /* 2. jogo.mif ao lado do MOD */
    {
        size_t len = strlen(mod_path);
        const char *dot = strrchr(mod_path, '.');
        if (dot && len < sizeof(candidate)) {
            memcpy(candidate, mod_path, dot - mod_path);
            strcpy(candidate + (dot - mod_path), ".mif");
            clsid = scan_file_for_clsid(candidate);
            if (clsid) return clsid;
        }
    }

    /* 2b. Se houver BAR companheiro, ele pode conter o nome do applet */
    {
        size_t len = strlen(mod_path);
        const char *dot = strrchr(mod_path, '.');
        if (dot && len < sizeof(candidate)) {
            memcpy(candidate, mod_path, dot - mod_path);
            strcpy(candidate + (dot - mod_path), ".bar");
            clsid = scan_file_for_clsid(candidate);
            if (clsid) return clsid;
        }
    }

    /* 3. Convencao do dump: <raiz>/mod/<id>/x.mod -> <raiz>/mif/<id>.mif */
    s1 = strrchr(mod_path, '/');
    s2 = strrchr(mod_path, '\\');
    sep = s1 > s2 ? s1 : s2;
    if (sep) {
        char dir[768];
        size_t dlen = (size_t)(sep - mod_path);
        if (dlen < sizeof(dir)) {
            const char *id;
            memcpy(dir, mod_path, dlen);
            dir[dlen] = '\0';
            s1 = strrchr(dir, '/');
            s2 = strrchr(dir, '\\');
            sep = s1 > s2 ? s1 : s2;
            id = sep ? sep + 1 : dir;
            if (sep) {
                size_t plen = (size_t)(sep - dir);
                char root[768];
                if (plen < sizeof(root)) {
                    /* remove o "mod" final do caminho */
                    memcpy(root, dir, plen);
                    root[plen] = '\0';
                    s1 = strrchr(root, '/');
                    s2 = strrchr(root, '\\');
                    sep = s1 > s2 ? s1 : s2;
                    if (sep) {
                        snprintf(candidate, sizeof(candidate),
                                 "%.*s%cmif%c%s.mif",
                                 (int)(sep - root), root,
                                 sep[0] == '/' ? '/' : '\\',
                                 sep[0] == '/' ? '/' : '\\', id);
                        clsid = scan_file_for_clsid(candidate);
                        if (clsid) return clsid;
                    }
                }
            }
        }
    }
    return 0;
}

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
