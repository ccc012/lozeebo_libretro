/* mod_loader.c - Carregador de modulos BREW (MOD)
 *
 * Formatos suportados (confirmados nas ROMs reais do Zeebo):
 *
 * 1. "BREW mod1" (magic "BREW" em +0x08 ou +0x48):
 *      +0x00 branch ARM -> entry point
 *      +0x04 branch ARM -> mod info
 *      +0x08 "BREW"  +0x0C versao  +0x10 tam. header (0x40)
 *      +0x1C code_size  +0x20 data_size  +0x24 bss_size
 *    Codigo ROPI, base de link 0. Nos mod1 com bootstrap PIC, +0x18 informa
 *    o deslocamento do codigo real (normalmente 0x200): copiamos code+data
 *    desse ponto para VA 0, zeramos o BSS e pulamos o scatter-loader ARMCC.
 *
 * 2. Binario ARM "cru" (Double Dragon, Family Pack, ovos, test ROMs):
 *    entry no offset 0 (ou branch inicial), sem header. Bootstrap PIC ARMCC
 *    (assinatura em +0x00/+0x14/+0x84..) roda nativo no guest (entry=0x14);
 *    NAO aplicar reloc HLE estatica — corrompe opcodes (ovos SOLID_FAIL).
 *
 * Em ambos: tabela AEEHelperFuncs gravada nos locais esperados pelo formato.
 * O bootstrap PIC ARMCC cru obtem a tabela em [-4] e a copia sozinho; nele,
 * [entry-4] faz parte do codigo e nao pode ser sobrescrito.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#endif
#include "mod_loader.h"
#include "../memory/memory.h"
#include "../cpu/cpu.h"
#include "../brew/brew.h"
#include "../debug/log.h"



static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Alvo de um branch ARM (B imm24) com PC dado */
static uint32_t branch_target(uint32_t insn, uint32_t pc) {
    int32_t imm = (int32_t)(insn << 8) >> 6; /* sign-extend imm24, <<2 */
    return pc + 8 + (uint32_t)imm;
}

static bool is_arm_branch(uint32_t insn) {
    return (insn & 0x0F000000u) == 0x0A000000u;
}

static int ci_is_mod_segment(const char *p) {
    return p[0] && p[1] && p[2] && p[3] &&
           tolower((unsigned char)p[0]) == 'm' &&
           tolower((unsigned char)p[1]) == 'o' &&
           tolower((unsigned char)p[2]) == 'd' &&
           p[3] == '/';
}

/* Tamanho do flat rom (para casar .mod empacotado com nome diferente). */
static long zmod_file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    long n;
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    n = ftell(f);
    fclose(f);
    return n;
}

/* Score de um id empacotado: sidecars/res > .mod do mesmo tamanho > leaf > qualquer .mod.
 * Action Hero: 12875/imicro3d.mod vs 274259/a3d.mod+a3dres.res — precisa varrer todos. */
static int zmod_score_asset_id(const char *assets_root, const char *id,
                               const char *leaf, long flat_sz, char *out_mod,
                               size_t out_mod_sz) {
    char probe[600];
    int score = 0;
    static const char *const sidecars[] = {
        "resources.dat", "resources.bar", "resources.pakz", "resources.pak",
        "a3dres.res", "string.txt",
        NULL
    };
    int si;
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char search[600];
#else
    DIR *d;
    struct dirent *de;
#endif

    if (out_mod && out_mod_sz) out_mod[0] = '\0';

    for (si = 0; sidecars[si]; si++) {
        snprintf(probe, sizeof(probe), "%s/%s/%s", assets_root, id, sidecars[si]);
        {
            FILE *f = fopen(probe, "rb");
            if (f) {
                fclose(f);
                score += (si < 4) ? 100 : 80;
            }
        }
    }

    snprintf(probe, sizeof(probe), "%s/%s/%s", assets_root, id, leaf);
    {
        FILE *f = fopen(probe, "rb");
        if (f) {
            fclose(f);
            score += 40;
            if (out_mod && out_mod_sz) {
                strncpy(out_mod, leaf, out_mod_sz - 1);
                out_mod[out_mod_sz - 1] = '\0';
            }
        }
    }

#if defined(_WIN32)
    snprintf(search, sizeof(search), "%s/%s/*.mod", assets_root, id);
    h = FindFirstFileA(search, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            long sz;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            score += 10;
            snprintf(probe, sizeof(probe), "%s/%s/%s",
                     assets_root, id, fd.cFileName);
            sz = zmod_file_size(probe);
            if (flat_sz > 0 && sz == flat_sz) {
                score += 200;
                if (out_mod && out_mod_sz) {
                    strncpy(out_mod, fd.cFileName, out_mod_sz - 1);
                    out_mod[out_mod_sz - 1] = '\0';
                }
            } else if (out_mod && out_mod_sz && !out_mod[0]) {
                strncpy(out_mod, fd.cFileName, out_mod_sz - 1);
                out_mod[out_mod_sz - 1] = '\0';
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    {
        char iddir[600];
        snprintf(iddir, sizeof(iddir), "%s/%s", assets_root, id);
        d = opendir(iddir);
        if (d) {
            while ((de = readdir(d)) != NULL) {
                size_t n;
                long sz;
                if (de->d_name[0] == '.') continue;
                n = strlen(de->d_name);
                if (n < 5 || strcmp(de->d_name + n - 4, ".mod") != 0) continue;
                score += 10;
                snprintf(probe, sizeof(probe), "%s/%s/%s",
                         assets_root, id, de->d_name);
                sz = zmod_file_size(probe);
                if (flat_sz > 0 && sz == flat_sz) {
                    score += 200;
                    if (out_mod && out_mod_sz) {
                        strncpy(out_mod, de->d_name, out_mod_sz - 1);
                        out_mod[out_mod_sz - 1] = '\0';
                    }
                } else if (out_mod && out_mod_sz && !out_mod[0]) {
                    strncpy(out_mod, de->d_name, out_mod_sz - 1);
                    out_mod[out_mod_sz - 1] = '\0';
                }
            }
            closedir(d);
        }
    }
#endif
    return score;
}

/* Flat tests/roms/<game>.mod -> tests/assets/<game>/mod/<id>/<file>.mod
 * quando o dump empacotado existe (resources.dat etc. ao lado do .mod). */
static int zmod_remap_flat_rom_to_assets(char *full, size_t fullsz) {
    char *roms, *slash, *dot, *leaf;
    char game[256], assets_root[512], search[600];
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    HANDLE h;
#else
    DIR *d;
    struct dirent *de;
#endif
    char found_id[64] = {0};
    char found_mod[256] = {0};
    char remapped[600];
    long flat_sz;
    int best_score = 0;

    for (roms = full; *roms; ++roms) {
        if ((roms == full || roms[-1] == '/') &&
            tolower((unsigned char)roms[0]) == 'r' &&
            tolower((unsigned char)roms[1]) == 'o' &&
            tolower((unsigned char)roms[2]) == 'm' &&
            tolower((unsigned char)roms[3]) == 's' &&
            roms[4] == '/')
            break;
    }
    if (!*roms) return 0;
    leaf = roms + 5;
    if (!*leaf || strchr(leaf, '/')) return 0;
    dot = strrchr(leaf, '.');
    if (!dot || dot == leaf) return 0;
    if ((size_t)(dot - leaf) >= sizeof(game)) return 0;
    memcpy(game, leaf, (size_t)(dot - leaf));
    game[dot - leaf] = '\0';

    slash = roms;
    if ((size_t)(slash - full) + 8 + strlen(game) + 5 >= sizeof(assets_root))
        return 0;
    snprintf(assets_root, sizeof(assets_root), "%.*sassets/%s/mod",
             (int)(slash - full), full, game);

    flat_sz = zmod_file_size(full);

#if defined(_WIN32)
    snprintf(search, sizeof(search), "%s/*", assets_root);
    h = FindFirstFileA(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        char cand_mod[256];
        int sc;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == '.') continue;
        if (fd.cFileName[0] < '0' || fd.cFileName[0] > '9') continue;
        sc = zmod_score_asset_id(assets_root, fd.cFileName, leaf, flat_sz,
                                 cand_mod, sizeof(cand_mod));
        if (sc > best_score) {
            best_score = sc;
            strncpy(found_id, fd.cFileName, sizeof(found_id) - 1);
            found_id[sizeof(found_id) - 1] = '\0';
            strncpy(found_mod, cand_mod, sizeof(found_mod) - 1);
            found_mod[sizeof(found_mod) - 1] = '\0';
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    d = opendir(assets_root);
    if (!d) return 0;
    while ((de = readdir(d)) != NULL) {
        char cand_mod[256];
        int sc;
        if (de->d_name[0] == '.') continue;
        if (de->d_name[0] < '0' || de->d_name[0] > '9') continue;
        sc = zmod_score_asset_id(assets_root, de->d_name, leaf, flat_sz,
                                 cand_mod, sizeof(cand_mod));
        if (sc > best_score) {
            best_score = sc;
            strncpy(found_id, de->d_name, sizeof(found_id) - 1);
            found_id[sizeof(found_id) - 1] = '\0';
            strncpy(found_mod, cand_mod, sizeof(found_mod) - 1);
            found_mod[sizeof(found_mod) - 1] = '\0';
        }
    }
    closedir(d);
#endif
    if (!found_id[0] || best_score <= 0) return 0;
    if (!found_mod[0]) {
        strncpy(found_mod, leaf, sizeof(found_mod) - 1);
        found_mod[sizeof(found_mod) - 1] = '\0';
    }
    snprintf(remapped, sizeof(remapped), "%s/%s/%s",
             assets_root, found_id, found_mod);
    if (strlen(remapped) >= fullsz) return 0;
    strcpy(full, remapped);
    LOGI("mod: remap flat rom -> app root '%s/%s' (via %s score=%d)",
         assets_root, found_id, found_mod, best_score);
    return 1;
}

static void zmod_set_asset_base_from_path(const char *path) {
    char full[600];
    char *last_sep;
    char *p;
    char *mod_seg = NULL;

    if (!path || !path[0])
        return;

    strncpy(full, path, sizeof(full) - 1);
    full[sizeof(full) - 1] = '\0';

    for (p = full; *p; ++p) {
        if (*p == '\\')
            *p = '/';
    }

    /* Remove nome do arquivo */
    last_sep = strrchr(full, '/');
    if (!last_sep)
        return;
    *last_sep = '\0';

    /* Detecta .../mod/<id>/arquivo.mod e usa raiz antes de /mod/ */
    for (p = full; *p; ++p) {
        if ((p == full || p[-1] == '/') && ci_is_mod_segment(p))
            mod_seg = p;
    }

    if (mod_seg) {
        char *id_start = mod_seg + 4;
        char *id_end = strchr(id_start, '/');
        if (id_end && id_end[1] != '\0') {
            if (mod_seg == full) {
                zbrew_set_file_base(".");
                return;
            }
            *mod_seg = '\0';
            zbrew_set_file_base(full);
            return;
        }
    }

    zbrew_set_file_base(full);
}

static void zmod_try_probe_packaged_assets(const char *path) {
    char candidate[1024];
    char *dot;
    size_t len;

    if (!path || !path[0]) return;
    len = strlen(path);
    if (len >= sizeof(candidate)) return;
    strcpy(candidate, path);
    dot = strrchr(candidate, '.');
    if (!dot) return;

    if ((tolower((unsigned char)dot[1]) == 'm' && tolower((unsigned char)dot[2]) == 'o' &&
         tolower((unsigned char)dot[3]) == 'd' && dot[4] == '\0') ||
        (tolower((unsigned char)dot[1]) == 'b' && tolower((unsigned char)dot[2]) == 'a' &&
         tolower((unsigned char)dot[3]) == 'r' && dot[4] == '\0')) {
        strcpy((char *)dot, ".bar");
        zbar_probe(candidate);
        strcpy((char *)dot, ".sig");
        {
            FILE *sig = fopen(candidate, "rb");
            if (sig) {
                LOGI("mod: assinatura companheira presente '%s'", candidate);
                fclose(sig);
            }
        }
    }
}

bool zmod_load(const void *data, size_t size, const char *path,
               zmod_info_t *out) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t hdr_off = 0;
    bool has_brew_hdr = false;
    bool raw_armcc_bootstrap = false;
    uint32_t hinted_clsid = zmif_find_applet_clsid(path, data, size);
    char *extracted_path = NULL;

    if (!data || size < 8) {
        LOGE("zmod_load: sem dados");
        return false;
    }
    if (size > ZMEM_RAM_SIZE / 2) {
        LOGE("zmod_load: ROM grande demais (%u bytes)", (unsigned)size);
        return false;
    }

    /* ZIP: extrai o primeiro .mod/.bar/.mif valido de dentro do arquivo. */
    if (bytes[0] == 'P' && bytes[1] == 'K' &&
        (bytes[2] == 0x03 || bytes[2] == 0x05 || bytes[2] == 0x07)) {
        void *extracted = NULL;
        size_t extracted_size = 0;
        if (zmod_extract_from_zip(data, size, &extracted, &extracted_size, &extracted_path)) {
            /* Preserva o caminho externo (ZIP) para inferencia de CLSID por
             * nome; o nome interno do .mod raramente contem o titulo. */
            bool ok = zmod_load(extracted, extracted_size,
                                 path ? path : extracted_path, out);
            free(extracted);
            free(extracted_path);
            return ok;
        }
        LOGE("zmod_load: falha ao extrair .mod do ZIP");
        return false;
    }
    if (bytes[0] == 0x1F && bytes[1] == 0x8B) {
        LOGE("zmod_load: arquivo e um GZIP/GGZ - extraia o conteudo antes");
        return false;
    }

    /* Detecta header BREW em 0x0 ou 0x40 */
    if (size >= 0x10 && memcmp(bytes + 8, "BREW", 4) == 0) {
        hdr_off = 0;
        has_brew_hdr = true;
    } else if (size >= 0x50 && memcmp(bytes + 0x48, "BREW", 4) == 0) {
        hdr_off = 0x40;
        has_brew_hdr = true;
    }

    /* Bootstrap PIC ARMCC observado nos modulos crus do conjunto Zeebo.
     * A palavra em +0x10 e o deslocamento do entry real relativo a +0x9C.
     * A assinatura usa varias instrucoes distantes para nao confundir um
     * binario cru comum que apenas comece com um branch para +0x14. */
    if (!has_brew_hdr && size >= 0x94 &&
        rd32(bytes + 0x00) == 0xEA000003u &&
        rd32(bytes + 0x14) == 0xE92D00F0u &&
        rd32(bytes + 0x18) == 0xE24F4020u &&
        rd32(bytes + 0x1C) == 0xE284509Cu &&
        rd32(bytes + 0x84) == 0xE51F307Cu &&
        rd32(bytes + 0x88) == 0xE0833005u &&
        rd32(bytes + 0x90) == 0xE12FFF13u) {
        raw_armcc_bootstrap = true;
    }

    memset(out, 0, sizeof(*out));
    strncpy(out->name, "Zeebo Game", sizeof(out->name) - 1);

    /* Imagem inteira em VA 0 (base de link dos modulos BREW) */
    if (!zmem_write_block(0, data, (uint32_t)size)) {
        LOGE("zmod_load: falha ao copiar ROM");
        return false;
    }
    out->code_base = 0;
    out->code_size = (uint32_t)size;

    if (has_brew_hdr) {
        uint32_t hdr_size  = rd32(bytes + hdr_off + 0x10);
        uint32_t code_off  = rd32(bytes + hdr_off + 0x18);
        uint32_t code_size = rd32(bytes + hdr_off + 0x1C);
        uint32_t data_size = rd32(bytes + hdr_off + 0x20);
        uint32_t bss_size  = rd32(bytes + hdr_off + 0x24);
        size_t source_off = (size_t)hdr_off + code_off;
        size_t image_size = (size_t)code_size + data_size;
        bool hle_scatter = code_off >= hdr_size && code_off <= 0x10000u &&
                           source_off <= size && image_size > 0 &&
                           image_size <= size - source_off &&
                           image_size <= ZMEM_RAM_SIZE &&
                           bss_size <= ZMEM_RAM_SIZE - image_size;

        if (hle_scatter) {
            uint32_t bss_start = (uint32_t)image_size;
            uint32_t i;
            if (!zmem_write_block(0, bytes + source_off, (uint32_t)image_size)) {
                LOGE("mod1: falha ao aplicar scatter-load HLE");
                return false;
            }
            for (i = 0; i < bss_size; i++)
                zmem_write8(bss_start + i, 0);
            out->entry = 0;
            out->code_size = (uint32_t)image_size;
            LOGI("mod1: bootstrap PIC pulado via HLE "
                 "(arquivo+0x%X -> VA 0, imagem=%u)",
                 (unsigned)source_off, (unsigned)image_size);
            LOGI("mod1: BSS zerado em 0x%08X (%u bytes)",
                 bss_start, bss_size);
        } else {
            out->entry = hdr_off + branch_target(rd32(bytes + hdr_off), 0);
            /* Formato antigo: BSS depois de cabecalho, codigo e dados. */
            if (bss_size > 0 && bss_size < ZMEM_RAM_SIZE / 2) {
                uint32_t bss_start = hdr_off + hdr_size + code_size + data_size;
                uint32_t i;
                for (i = 0; i < bss_size; i++)
                    zmem_write8(bss_start + i, 0);
                LOGI("mod1: BSS zerado em 0x%08X (%u bytes)",
                     bss_start, bss_size);
            }
        }
        LOGI("mod1: hdr=0x%X code=%u data=%u bss=%u entry=0x%08X",
             hdr_off, code_size, data_size, bss_size, out->entry);
    } else {
        /* binario cru: entry no offset 0 */
        out->entry = 0;
        if (raw_armcc_bootstrap) {
            /* Módulos crus com bootstrap ARMCC executam a inicialização nativa no
             * guest (entry 0x14 via branch inicial). A realocação HLE estática
             * corrompia opcodes de instrução em jogos como um_jogo_de_ovos e FP. */
            out->entry = 0x14u;
            LOGI("mod cru: bootstrap PIC ARMCC detectado -> entry=0x%08X (bootstrap nativo)", out->entry);
        } else if (!hinted_clsid && size >= 4) {
            uint32_t first = rd32(bytes);
            if (is_arm_branch(first)) {
                uint32_t target = branch_target(first, 0);
                if (target < (uint32_t)size) {
                    LOGI("mod cru: branch inicial detectado -> entry=0x%08X", target);
                    out->entry = target;
                }
            }
        }
        LOGI("mod cru: %u bytes, entry=0x%08X", out->code_size, out->entry);
    }

    /* Nome e diretorio de assets */
    if (path) {
        char name[64];
        if (zmif_parse_name(path, name, sizeof(name)))
            strncpy(out->name, name, sizeof(out->name) - 1);
        if (!out->name[0] || strcmp(out->name, "Zeebo Game") == 0) {
            uint32_t hinted = zmif_find_applet_clsid(path, data, size);
            if (hinted == 0x010903C6u) {
                strncpy(out->name, "Zeebo Family Pack", sizeof(out->name) - 1);
            }
        }
        {
            char resolved[600];
            int remapped;
            strncpy(resolved, path, sizeof(resolved) - 1);
            resolved[sizeof(resolved) - 1] = '\0';
            {
                char *p;
                for (p = resolved; *p; ++p)
                    if (*p == '\\') *p = '/';
            }
            remapped = zmod_remap_flat_rom_to_assets(resolved, sizeof(resolved));
            /* Flat tests/roms/*.mod: base fica em roms/ (data.vfs); app root
             * (resources.dat) aponta para tests/assets/<game>/mod/<id>/. */
            if (remapped)
                zmod_set_asset_base_from_path(path);
            else
                zmod_set_asset_base_from_path(resolved);
            zbrew_set_mod_dir(resolved);
            zmod_try_probe_packaged_assets(resolved);
        }
    }

    /* CPU: SP no topo, LR provisorio (zboot_start define o resto) */
    zcpu_reset(out->entry, ZMEM_STACK_TOP, ZTRAP_ADDR(ZT_RETURN_APPLET));

    /* Tabela de helpers AEE. O bootstrap PIC cru le [-4]/[-8] e copia os
     * valores para +0x98/+0x94 antes de saltar ao entry real em +0x9C. */
    {
        uint32_t table = zbrew_build_helper_table();
        uint32_t slot;
        /* Bootstrap ARMCC nativo le [-4]/[-8] e copia para +0x98/+0x94.
         * Nao pre-escrever 0x98/0x94 nem entry-4 (sobrescreveria o bootstrap). */
        if (!raw_armcc_bootstrap && out->entry >= 8) {
            zmem_write32(out->entry - 4, table);
            zmem_write32(out->entry - 8, 0);
        }
        for (slot = 0x1F0; slot <= 0x1FC; slot += 4) {
            if (zmem_read32(slot) == 0)
                zmem_write32(slot, table);
        }
        /* bootstrap do mirror em base 0 le helper/versao de [-4]/[-8] */
        zmem_write32(0xFFFFFFFCu, table);
        zmem_write32(0xFFFFFFF8u, 0);
    }

    /* CLSID do applet (MIF ao lado da ROM, ou tabela de conhecidos) */
    {
        uint32_t clsid = zmif_find_applet_clsid(path, data, size);
        if (!clsid)
            LOGW("zmod_load: CLSID do applet desconhecido - "
                 "CreateInstance recebera 0");
        zboot_start(out->entry, clsid);
    }

    LOGI("zmod_load: '%s' pronto (%u bytes, entry 0x%08X)",
         out->name, out->code_size, out->entry);
    return true;
}
