/* mod_loader.c - Carrega a ROM na memoria emulada e prepara a CPU */
#include <string.h>
#include <stdio.h>
#include "mod_loader.h"
#include "../memory/memory.h"
#include "../cpu/cpu.h"
#include "../brew/brew.h"
#include "../debug/log.h"

bool zmod_load(const void *data, size_t size, const char *path,
               zmod_info_t *out) {
    if (!data || size == 0) {
        LOGE("zmod_load: sem dados");
        return false;
    }
    if (size > ZMEM_RAM_SIZE - ZMEM_CODE_ENTRY) {
        LOGE("zmod_load: ROM grande demais (%u bytes)", (unsigned)size);
        return false;
    }

    /* Copia o binario para 0x1000 */
    if (!zmem_write_block(ZMEM_CODE_ENTRY, data, (uint32_t)size)) {
        LOGE("zmod_load: falha ao copiar ROM para a memoria");
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->entry = ZMEM_CODE_ENTRY;
    out->code_base = ZMEM_CODE_ENTRY;
    out->code_size = (uint32_t)size;
    strncpy(out->name, "Zeebo Game", sizeof(out->name) - 1);

    /* Tenta ler o nome do .mif ao lado da ROM */
    if (path) {
        char name[64];
        if (zmif_parse_name(path, name, sizeof(name)))
            strncpy(out->name, name, sizeof(out->name) - 1);

        /* Diretorio da ROM vira a base do IFile */
        {
            char dir[512];
            const char *s1 = strrchr(path, '/');
            const char *s2 = strrchr(path, '\\');
            const char *sep = s1 > s2 ? s1 : s2;
            if (sep && (size_t)(sep - path) < sizeof(dir)) {
                memcpy(dir, path, sep - path);
                dir[sep - path] = '\0';
                zbrew_set_file_base(dir);
            }
        }
    }

    /* Prepara a CPU:
     *  PC = entry, SP = topo da stack
     *  LR = trap de retorno (se o entry retornar, o applet acabou)
     *  R0 = ponteiro do IShell (convencao AEEMod_Load(shell, ...)) */
    zcpu_reset(out->entry, ZMEM_STACK_TOP, ZTRAP_ADDR(ZT_RETURN_APPLET));
    g_cpu.r[0] = zbrew_shell_ptr();

    LOGI("zmod_load: '%s' %u bytes em 0x%08X, entry 0x%08X",
         out->name, out->code_size, out->code_base, out->entry);
    return true;
}
