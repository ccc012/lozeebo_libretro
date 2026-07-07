/* libretro_core.c
 * Nucleo Zeebo - integracao de todos os subsistemas
 *
 * Pipeline por frame (retro_run):
 *   input -> CPU (N instrucoes) -> video (framebuffer) -> audio (mixer)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "libretro.h"

#include "../debug/log.h"
#include "../memory/memory.h"
#include "../cpu/cpu.h"
#include "../gpu/framebuffer.h"
#include "../audio/audio.h"
#include "../input/input.h"
#include "../brew/brew.h"
#include "../loader/mod_loader.h"

/* Instrucoes emuladas por frame (~60 MIPS; ajustar com profiling) */
#define ZEEBO_INSTR_PER_FRAME 1000000u
#define ZEEBO_AUDIO_FRAMES (ZAUDIO_SAMPLE_RATE / 60)   /* 735 */

/* =====================================================
 * CALLBACKS DO FRONTEND
 * ===================================================== */
static retro_video_refresh_t video_cb = NULL;
static retro_audio_sample_t audio_cb = NULL;
static retro_audio_sample_batch_t audio_batch_cb = NULL;
static retro_environment_t environ_cb = NULL;
static retro_input_poll_t input_poll_cb = NULL;
static retro_input_state_t input_state_cb = NULL;
static struct retro_log_callback log_iface;
static bool log_iface_ok = false;

/* Estado do jogo carregado */
static bool g_game_loaded = false;
static void *g_game_data = NULL;      /* copia para retro_reset */
static size_t g_game_size = 0;
static char g_game_path[512];
static zmod_info_t g_mod_info;

/* Buffer de audio do frame */
static int16_t g_audio_buf[ZEEBO_AUDIO_FRAMES * 2];

/* =====================================================
 * LOG: adapta zlog para o retro_log do frontend
 * ===================================================== */
static void log_backend(enum zlog_level level, const char *fmt, va_list ap) {
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    if (log_iface_ok) {
        log_iface.log((enum retro_log_level)level, "[Zeebo] %s\n", buf);
    } else {
        fprintf(stderr, "[Zeebo] %s\n", buf);
    }
}

/* =====================================================
 * VERSAO DA API
 * ===================================================== */
unsigned retro_api_version(void) {
    return RETRO_API_VERSION;
}

/* =====================================================
 * INIT / DEINIT
 * ===================================================== */
void retro_init(void) {
    zlog_set_backend(log_backend);
    LOGI("retro_init: nucleo Zeebo 0.2 (CPU ARM + BREW HLE)");
    zmem_init();
    zfb_init();
    zaudio_init();
    zbrew_init();
}

void retro_deinit(void) {
    LOGI("retro_deinit");
    zbrew_shutdown();
    zmem_shutdown();
    free(g_game_data);
    g_game_data = NULL;
    g_game_loaded = false;
}

/* =====================================================
 * INFO DO NUCLEO
 * ===================================================== */
void retro_get_system_info(struct retro_system_info *info) {
    memset(info, 0, sizeof(*info));
    info->library_name     = "Zeebo";
    info->library_version  = "0.2-cpu";
    info->valid_extensions = "mod|mif";
    info->need_fullpath    = false;
    info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
    memset(info, 0, sizeof(*info));
    info->geometry.base_width   = ZFB_WIDTH;
    info->geometry.base_height  = ZFB_HEIGHT;
    info->geometry.max_width    = ZFB_WIDTH;
    info->geometry.max_height   = ZFB_HEIGHT;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps            = 60.0;
    info->timing.sample_rate    = (double)ZAUDIO_SAMPLE_RATE;
}

/* =====================================================
 * CALLBACKS
 * ===================================================== */
void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { audio_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void retro_set_environment(retro_environment_t cb) {
    bool no_content = false;
    environ_cb = cb;
    environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

    if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_iface))
        log_iface_ok = true;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
    (void)port;
    (void)device;
}

/* =====================================================
 * LOAD / UNLOAD GAME
 * ===================================================== */
bool retro_load_game(const struct retro_game_info *info) {
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;

    if (!info || !info->data || !info->size) {
        LOGE("retro_load_game: sem dados de conteudo");
        return false;
    }
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
        LOGE("retro_load_game: frontend nao suporta XRGB8888");
        return false;
    }

    /* guarda copia para retro_reset */
    free(g_game_data);
    g_game_data = malloc(info->size);
    if (!g_game_data) return false;
    memcpy(g_game_data, info->data, info->size);
    g_game_size = info->size;
    g_game_path[0] = '\0';
    if (info->path)
        snprintf(g_game_path, sizeof(g_game_path), "%s", info->path);

    zmem_reset();
    zfb_init();
    zaudio_reset();
    zbrew_reset();
    zbrew_init();

    if (!zmod_load(g_game_data, g_game_size,
                   g_game_path[0] ? g_game_path : NULL, &g_mod_info)) {
        LOGE("retro_load_game: falha no loader");
        return false;
    }

    g_game_loaded = true;
    LOGI("retro_load_game: '%s' pronto para executar", g_mod_info.name);
    return true;
}

void retro_unload_game(void) {
    LOGI("retro_unload_game");
    g_game_loaded = false;
    zaudio_stop_all();
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
    LOGI("retro_reset");
    if (!g_game_loaded || !g_game_data) return;
    zmem_reset();
    zfb_init();
    zaudio_reset();
    zbrew_reset();
    zbrew_init();
    zmod_load(g_game_data, g_game_size,
              g_game_path[0] ? g_game_path : NULL, &g_mod_info);
}

/* =====================================================
 * RUN - executado a cada frame (60 Hz)
 * ===================================================== */
static void poll_input(void) {
    if (!input_poll_cb || !input_state_cb) return;
    input_poll_cb();

#define PAD(btn) (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, \
                                 RETRO_DEVICE_ID_JOYPAD_##btn) != 0)
    zinput_update(PAD(UP), PAD(DOWN), PAD(LEFT), PAD(RIGHT),
                  PAD(A), PAD(B), PAD(Y), PAD(START), PAD(SELECT));
#undef PAD
}

void retro_run(void) {
    static unsigned frame_count = 0;

    poll_input();

    if (g_game_loaded) {
        /* timers do IShell podem "acordar" a CPU com um guest call */
        zboot_tick(16);
        if (!g_cpu.halted)
            zcpu_run(ZEEBO_INSTR_PER_FRAME);
        zbrew_tick_ms(16);
    }

    /* Diagnostico: estado da CPU a cada 60 frames (1s) */
    if ((frame_count++ % 60) == 0) {
        LOGI("frame %u: boot=%s instrucoes=%llu PC=0x%08X halted=%d fb[0]=0x%08X",
             frame_count, zboot_state_name(),
             (unsigned long long)g_cpu.executed,
             g_cpu.r[REG_PC], g_cpu.halted ? 1 : 0,
             zfb_pixels() ? zfb_pixels()[0] : 0);
    }

    /* Video: framebuffer da VRAM emulada (sempre valido apos zfb_init) */
    if (video_cb) {
        uint32_t *fb = zfb_pixels();
        video_cb(fb, ZFB_WIDTH, ZFB_HEIGHT, ZFB_WIDTH * sizeof(uint32_t));
    }
    zbrew_clear_frame_flag();

    /* Audio: 735 frames estereo por tick de 60 Hz */
    zaudio_render(g_audio_buf, ZEEBO_AUDIO_FRAMES);
    if (audio_batch_cb)
        audio_batch_cb(g_audio_buf, ZEEBO_AUDIO_FRAMES);
}

/* =====================================================
 * SAVE STATES (fase 7)
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
 * CHEATS (nao usado)
 * ===================================================== */
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {
    (void)index;
    (void)enabled;
    (void)code;
}

/* =====================================================
 * MEMORY (para o frontend inspecionar; fase 7)
 * ===================================================== */
void *retro_get_memory_data(unsigned id) {
    (void)id;
    return NULL;
}

size_t retro_get_memory_size(unsigned id) {
    (void)id;
    return 0;
}
