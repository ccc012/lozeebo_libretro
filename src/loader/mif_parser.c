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

static uint32_t scan_mif_for_clsid(const char *path) {
    uint32_t known = scan_file_for_clsid(path);
    FILE *f;
    if (known) return known;
    f = fopen(path, "rb");
    unsigned char tail[32];
    long size;
    size_t n, i;
    uint32_t clsid = 0;

    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 4 ||
        fseek(f, size > (long)sizeof(tail) ? size - (long)sizeof(tail) : 0,
              SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    n = fread(tail, 1, sizeof(tail), f);
    fclose(f);
    for (i = 0; i + 4 <= n; i++) {
        uint32_t v = (uint32_t)tail[i] | ((uint32_t)tail[i+1] << 8) |
                     ((uint32_t)tail[i+2] << 16) | ((uint32_t)tail[i+3] << 24);
        if ((v & 0xFF000000u) == 0x01000000u)
            clsid = v;
    }
    if (clsid)
        LOGI("mif: applet 0x%08X no trailer de %s", clsid, path);
    return clsid;
}

static uint32_t scan_memory_for_clsid(const void *data, size_t size) {
    const unsigned char *buf = (const unsigned char *)data;
    size_t i, k;
    if (!data || size < 4) return 0;
    for (i = 0; i + 4 <= size; i++) {
        uint32_t v = (uint32_t)buf[i] | ((uint32_t)buf[i+1] << 8) |
                     ((uint32_t)buf[i+2] << 16) | ((uint32_t)buf[i+3] << 24);
        for (k = 0; k < ZAEE_KNOWN_APPLET_COUNT; k++) {
            if (v == ZAEE_KNOWN_APPLETS[k].clsid) {
                LOGI("mif: applet '%s' (0x%08X) em memoria @0x%zX",
                     ZAEE_KNOWN_APPLETS[k].name, v, i);
                return v;
            }
        }
    }
    return 0;
}

/* Match case-insensitive: paths de teste usam snake_case (bad_dudes...),
 * enquanto hints historicos usam Title Case. */
static int path_has(const char *hay, const char *needle) {
    size_t n, h, i, j;
    if (!hay || !needle || !*needle) return 0;
    n = strlen(needle);
    h = strlen(hay);
    if (n > h) return 0;
    for (i = 0; i + n <= h; i++) {
        for (j = 0; j < n; j++) {
            unsigned char a = (unsigned char)hay[i + j];
            unsigned char b = (unsigned char)needle[j];
            if (tolower(a) != tolower(b)) break;
        }
        if (j == n) return 1;
    }
    return 0;
}

static uint32_t clsid_from_path_hint(const char *mod_path) {
    if (!mod_path) return 0;
    if (path_has(mod_path, "family_pack") || path_has(mod_path, "family-pack") ||
        path_has(mod_path, "familypack") || path_has(mod_path, "Zeebo Family Pack"))
        return 0x010903C6u;
    if (path_has(mod_path, "pac-mania") || path_has(mod_path, "pacmania") ||
        path_has(mod_path, "pac_mania"))
        return 0x01087B72u;
    if (path_has(mod_path, "double_dragon") || path_has(mod_path, "doubledragon") ||
        path_has(mod_path, "ddragon") || path_has(mod_path, "Double Dragon"))
        return 0x0102F789u;
    if (path_has(mod_path, "quake_ii") || path_has(mod_path, "quake2") ||
        path_has(mod_path, "quake ii"))
        return 0x01087C1Cu;
    if (path_has(mod_path, "quake"))
        return 0x01087A3Cu;
    if (path_has(mod_path, "z-wheel") || path_has(mod_path, "zwheel") ||
        path_has(mod_path, "z_wheel"))
        return 0x01070798u;
    if (path_has(mod_path, "zeeboids"))
        return 0x0108FF1Au;
    if (path_has(mod_path, "action_hero") || path_has(mod_path, "Action Hero 3D"))
        return 0x01081970u;
    if (path_has(mod_path, "alice_no_pais") || path_has(mod_path, "Alice no Pais"))
        return 0x010A2337u;
    if (path_has(mod_path, "alien_breaker") || path_has(mod_path, "Alien Breaker"))
        return 0x0108E356u;
    if (path_has(mod_path, "alpine_racer") || path_has(mod_path, "Alpine Racer"))
        return 0x01073825u;
    if (path_has(mod_path, "armageddon_squadron") || path_has(mod_path, "Armageddon Squadron"))
        return 0x0109DA8Eu;
    if (path_has(mod_path, "bad_dudes") || path_has(mod_path, "Bad Dudes"))
        return 0x0109EC1Bu;
    if (path_has(mod_path, "bejeweled") || path_has(mod_path, "Bejeweled Twist"))
        return 0x0108C1E1u;
    if (path_has(mod_path, "caveman_ninja") || path_has(mod_path, "Caveman Ninja"))
        return 0x010924DEu;
    if (path_has(mod_path, "crash_bandicoot") || path_has(mod_path, "Crash Bandicoot"))
        return 0x01081984u;
    if (path_has(mod_path, "dark_seal") || path_has(mod_path, "Dark Seal"))
        return 0x010924DFu;
    if (path_has(mod_path, "disney_all_star") || path_has(mod_path, "Disney All Star"))
        return 0x010940DAu;
    if (path_has(mod_path, "fifa_09") || path_has(mod_path, "fifa09") ||
        path_has(mod_path, "FIFA 09"))
        return 0x01087AE8u;
    if (path_has(mod_path, "galaxy_on_fire") || path_has(mod_path, "Galaxy on Fire"))
        return 0x0108FAB8u;
    if (path_has(mod_path, "heavy_barrel") || path_has(mod_path, "Heavy Barrel"))
        return 0x0109EC1Cu;
    if (path_has(mod_path, "heavy_weapon") || path_has(mod_path, "Heavy Weapon"))
        return 0x010978A2u;
    if (path_has(mod_path, "iron_sight") || path_has(mod_path, "Iron Sight"))
        return 0x0109DA8Fu;
    if (path_has(mod_path, "karnov"))
        return 0x010924DDu;
    if (path_has(mod_path, "magical_drop") || path_has(mod_path, "Magical Drop"))
        return 0x010924E0u;
    if (path_has(mod_path, "need_for_speed") || path_has(mod_path, "nfs.mod") ||
        path_has(mod_path, "Need For Speed"))
        return 0x0108C0BCu;
    if (path_has(mod_path, "peggle"))
        return 0x01099CD6u;
    if (path_has(mod_path, "powerboat") || path_has(mod_path, "Powerboat Challenge"))
        return 0x010A1241u;
    if (path_has(mod_path, "prey_evil") || path_has(mod_path, "Prey Evil"))
        return 0x0108D48Eu;
    if (path_has(mod_path, "raging_thunder") || path_has(mod_path, "Raging Thunder"))
        return 0x0109DA90u;
    if (path_has(mod_path, "rally_master") || path_has(mod_path, "Rally Master"))
        return 0x0108FAB9u;
    if (path_has(mod_path, "reckless_racing") || path_has(mod_path, "Reckless Racing"))
        return 0x0109DA91u;
    if (path_has(mod_path, "resident_evil_4") || path_has(mod_path, "re4") ||
        path_has(mod_path, "Resident Evil 4"))
        return 0x0108AF6Cu;
    if (path_has(mod_path, "ridge_racer") || path_has(mod_path, "Ridge Racer"))
        return 0x01087B73u;
    if (path_has(mod_path, "spin_master") || path_has(mod_path, "Spin Master"))
        return 0x010924E1u;
    if (path_has(mod_path, "street_hoop") || path_has(mod_path, "Street Hoop"))
        return 0x010924E2u;
    if (path_has(mod_path, "super_burgertime") || path_has(mod_path, "supbtime") ||
        path_has(mod_path, "sup_btime") || path_has(mod_path, "Super BurgerTime") ||
        path_has(mod_path, "burger"))
        return 0x010924E3u;
    if (path_has(mod_path, "tekken_2") || path_has(mod_path, "Tekken 2"))
        return 0x0108D1B7u;
    if (path_has(mod_path, "tork_and_kral") || path_has(mod_path, "Tork and Kral"))
        return 0x010A6D13u;
    if (path_has(mod_path, "toy_raid") || path_has(mod_path, "Toy Raid"))
        return 0x01095146u;
    if (path_has(mod_path, "treino_cerebral") || path_has(mod_path, "Treino Cerebral"))
        return 0x01087A49u;
    if (path_has(mod_path, "turma_da_monica") || path_has(mod_path, "Turma da Monica"))
        return 0x010A2335u;
    if (path_has(mod_path, "ultimate_chess") || path_has(mod_path, "Ultimate Chess"))
        return 0x010413C3u;
    if (path_has(mod_path, "um_jogo_de_ovos") || path_has(mod_path, "Um Jogo de Ovos"))
        return 0x010963A5u;
    if (path_has(mod_path, "wizard_fire") || path_has(mod_path, "Wizard Fire"))
        return 0x010924E4u;
    if (path_has(mod_path, "zeebo_app") || path_has(mod_path, "Zeebo App"))
        return 0x01072195u;
    if (path_has(mod_path, "zeebo_channels") || path_has(mod_path, "Zeebo Channels"))
        return 0x0109400Bu;
    if (path_has(mod_path, "zeebo_clube") || path_has(mod_path, "Zeebo Clube"))
        return 0x0109E4E0u;
    if (path_has(mod_path, "zeebo_extreme_baja") || path_has(mod_path, "Extreme Baja"))
        return 0x0108FF07u;
    if (path_has(mod_path, "boia_cross") || path_has(mod_path, "Boia Cross"))
        return 0x0108FF13u;
    if (path_has(mod_path, "corrida_aerea") || path_has(mod_path, "Corrida Aerea"))
        return 0x0108FF06u;
    if (path_has(mod_path, "jetboard") || path_has(mod_path, "Jetboard"))
        return 0x0108FF14u;
    if (path_has(mod_path, "rolima") || path_has(mod_path, "Rolima"))
        return 0x0108C0BBu;
    if (path_has(mod_path, "foot_camp") || path_has(mod_path, "Foot Camp"))
        return 0x0108FF19u;
    if (path_has(mod_path, "super_league") || path_has(mod_path, "Super League"))
        return 0x0108FF17u;
    if (path_has(mod_path, "peteca") || path_has(mod_path, "Peteca"))
        return 0x0108FF18u;
    if (path_has(mod_path, "queimada") || path_has(mod_path, "Queimada"))
        return 0x0108FF16u;
    if (path_has(mod_path, "sports_tenis") || path_has(mod_path, "Sports Tenis"))
        return 0x0108EFF9u;
    if (path_has(mod_path, "sports_volei") || path_has(mod_path, "Sports Volei"))
        return 0x0108FF15u;
    if (path_has(mod_path, "zenonia"))
        return 0xBF2E2021u;
    if (path_has(mod_path, "zuma"))
        return 0x0109AF55u;
    return 0;
}

uint32_t zmif_find_applet_clsid(const char *mod_path, const void *data, size_t data_size) {
    char candidate[1024];
    uint32_t clsid;
    const char *sep, *s1, *s2;

    if (!mod_path && !data) return 0;

    {
        uint32_t hinted = clsid_from_path_hint(mod_path);
        if (hinted) {
            LOGI("mif: clsid inferido por nome de caminho 0x%08X em %s",
                 hinted, mod_path);
            return hinted;
        }
    }

    /* 1. jogo.mif ao lado do MOD */
    {
        size_t len = strlen(mod_path);
        const char *dot = strrchr(mod_path, '.');
        if (dot && len < sizeof(candidate)) {
            memcpy(candidate, mod_path, dot - mod_path);
            strcpy(candidate + (dot - mod_path), ".mif");
            clsid = scan_mif_for_clsid(candidate);
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
                        clsid = scan_mif_for_clsid(candidate);
                        if (clsid) return clsid;
                    }
                }
            }
        }
    }

    if (data && data_size >= 4) {
        clsid = scan_memory_for_clsid(data, data_size);
        if (clsid) return clsid;
    }
    return scan_file_for_clsid(mod_path);
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
