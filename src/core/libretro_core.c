/* libretro_core.c
 * Núcleo Zeebo - Skeleton inicial
 * Todas as funções existem, mas ainda não fazem nada.
 * Objetivo: compilar e o RetroArch reconhecer o núcleo.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"

/* =====================================================
 * CALLBACKS (RetroArch vai nos dar essas funções)
 * ===================================================== */
static retro_video_refresh_t video_cb = NULL;
static retro_audio_sample_t audio_cb = NULL;
static retro_audio_sample_batch_t audio_batch_cb = NULL;
static retro_environment_t environ_cb = NULL;
static retro_input_poll_t input_poll_cb = NULL;
static retro_input_state_t input_state_cb = NULL;

/* =====================================================
 * VERSÃO DA API
 * ===================================================== */
unsigned retro_api_version(void) {
    return RETRO_API_VERSION;
}

/* =====================================================
 * INIT / DEINIT
 * ===================================================== */
void retro_init(void) {
    printf("[Zeebo] retro_init() chamado\n");
}

void retro_deinit(void) {
    printf("[Zeebo] retro_deinit() chamado\n");
}

/* =====================================================
 * INFO DO NÚCLEO
 * ===================================================== */
void retro_get_system_info(struct retro_system_info *info) {
    memset(info, 0, sizeof(*info));
    info->library_name     = "Zeebo";
    info->library_version  = "0.1-skeleton";
    info->valid_extensions = "mod|mif";
    info->need_fullpath    = false;
    info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
    memset(info, 0, sizeof(*info));
    info->geometry.base_width   = 640;
    info->geometry.base_height  = 480;
    info->geometry.max_width    = 640;
    info->geometry.max_height   = 480;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps            = 60.0;
    info->timing.sample_rate    = 44100.0;
}

/* =====================================================
 * CALLBACKS - RetroArch registra essas funções
 * ===================================================== */
void retro_set_video_refresh(retro_video_refresh_t cb) {
    video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb) {
    audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
    audio_batch_cb = cb;
}

void retro_set_environment(retro_environment_t cb) {
    environ_cb = cb;

    bool no_content = false;
    environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);
}

void retro_set_input_poll(retro_input_poll_t cb) {
    input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb) {
    input_state_cb = cb;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
    (void)port;
    (void)device;
}

/* =====================================================
 * LOAD / UNLOAD GAME
 * ===================================================== */
bool retro_load_game(const struct retro_game_info *info) {
    if (!info) {
        printf("[Zeebo] retro_load_game: sem info\n");
        return false;
    }
    printf("[Zeebo] retro_load_game: %s\n", info->path ? info->path : "(sem path)");

    /* TODO (Fase 2): carregar ROM MOD de verdade aqui */
    return true;
}

void retro_unload_game(void) {
    printf("[Zeebo] retro_unload_game()\n");
}

unsigned retro_get_region(void) {
    return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) {
    (void)type;
    (void)info;
    (void)num;
    return false;
}

/* =====================================================
 * RESET
 * ===================================================== */
void retro_reset(void) {
    printf("[Zeebo] retro_reset()\n");
}

/* =====================================================
 * RUN - FUNÇÃO PRINCIPAL (ainda vazia)
 * ===================================================== */
void retro_run(void) {
    /* TODO (Fase 1+): rodar CPU, gráficos, áudio de verdade */
    if (input_poll_cb) {
        input_poll_cb();
    }

    /* Por enquanto, só envia uma tela preta (para não travar o RetroArch) */
    static uint32_t framebuffer[640 * 480];
    if (video_cb) {
        video_cb(framebuffer, 640, 480, 640 * sizeof(uint32_t));
    }
}

/* =====================================================
 * SAVE STATES (stub por enquanto)
 * ===================================================== */
size_t retro_serialize_size(void) {
    return 0;
}

bool retro_serialize(void *data, size_t size) {
    (void)data;
    (void)size;
    return false;
}

bool retro_unserialize(const void *data, size_t size) {
    (void)data;
    (void)size;
    return false;
}

/* =====================================================
 * CHEATS (não usado, mas precisa existir)
 * ===================================================== */
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {
    (void)index;
    (void)enabled;
    (void)code;
}

/* =====================================================
 * MEMORY (stub por enquanto)
 * ===================================================== */
void *retro_get_memory_data(unsigned id) {
    (void)id;
    return NULL;
}

size_t retro_get_memory_size(unsigned id) {
    (void)id;
    return 0;
}
