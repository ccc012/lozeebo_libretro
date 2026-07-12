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
 * 2. Binario ARM "cru" (Double Dragon, Family Pack, test ROMs):
 *    entry no offset 0 (ou branch inicial), sem header.
 *
 * Em ambos: tabela AEEHelperFuncs gravada nos locais esperados pelo formato.
 * O bootstrap PIC ARMCC cru obtem a tabela em [-4] e a copia sozinho; nele,
 * [entry-4] faz parte do codigo e nao pode ser sobrescrito.
 */
#include <string.h>
#include <stdio.h>
#include <ctype.h>
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

static void zmod_set_asset_base_from_path(const char *path) {
    char full[512];
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
    uint32_t hinted_clsid = path ? zmif_find_applet_clsid(path) : 0;

    if (!data || size < 8) {
        LOGE("zmod_load: sem dados");
        return false;
    }
    if (size > ZMEM_RAM_SIZE / 2) {
        LOGE("zmod_load: ROM grande demais (%u bytes)", (unsigned)size);
        return false;
    }

    /* Rejeita containers que nao sao codigo ARM: sem isto, um .zip/.ggz
     * entregue cru pelo frontend cairia no ramo "mod cru" e seria
     * executado como codigo em VA 0. */
    if (bytes[0] == 'P' && bytes[1] == 'K' &&
        (bytes[2] == 0x03 || bytes[2] == 0x05 || bytes[2] == 0x07)) {
        LOGE("zmod_load: arquivo e um ZIP - extraia o .mod de dentro "
             "(o core nao descompacta; use o browse-archive do RetroArch)");
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
        if (!hinted_clsid && size >= 4) {
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
        if (raw_armcc_bootstrap)
            LOGI("mod cru: bootstrap PIC ARMCC detectado; preservando literal em +0x10");
    }

    /* Nome e diretorio de assets */
    if (path) {
        char name[64];
        if (zmif_parse_name(path, name, sizeof(name)))
            strncpy(out->name, name, sizeof(out->name) - 1);
        if (!out->name[0] || strcmp(out->name, "Zeebo Game") == 0) {
            uint32_t hinted = zmif_find_applet_clsid(path);
            if (hinted == 0x010903C6u) {
                strncpy(out->name, "Zeebo Family Pack", sizeof(out->name) - 1);
            }
        }
        zmod_set_asset_base_from_path(path);
        zmod_try_probe_packaged_assets(path);
    }

    /* CPU: SP no topo, LR provisorio (zboot_start define o resto) */
    zcpu_reset(out->entry, ZMEM_STACK_TOP, ZTRAP_ADDR(ZT_RETURN_APPLET));

    /* Tabela de helpers AEE. O bootstrap PIC cru le [-4]/[-8] e copia os
     * valores para +0x98/+0x94 antes de saltar ao entry real em +0x9C. */
    {
        uint32_t table = zbrew_build_helper_table();
        uint32_t slot;
        if (out->entry >= 8 && !raw_armcc_bootstrap) {
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
        uint32_t clsid = zmif_find_applet_clsid(path);
        if (!clsid)
            LOGW("zmod_load: CLSID do applet desconhecido - "
                 "CreateInstance recebera 0");
        zboot_start(out->entry, clsid);
    }

    LOGI("zmod_load: '%s' pronto (%u bytes, entry 0x%08X)",
         out->name, out->code_size, out->entry);
    return true;
}
