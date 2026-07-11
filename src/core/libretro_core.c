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
#include "../audio/audio_internal.h"
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
static struct retro_memory_map g_memory_map;
static struct retro_memory_descriptor g_memory_desc[4];
static bool g_memory_map_set = false;
static bool g_crop_overscan = false;
static int g_language = RETRO_LANGUAGE_ENGLISH;
static const char *g_username = "Player";
static bool g_disk_ejected = false;
static unsigned g_disk_index = 0;
static bool g_disk_has_initial = false;
static char g_disk_label[64];
static char g_cheat_code[32][32];
static uint32_t g_cheat_addr[32];
static uint8_t g_cheat_val[32];
static bool g_cheat_enabled[32];
static unsigned g_cheat_count = 0;
static bool g_game_from_archive = false;
static char g_archive_path[512];
static char g_archive_file[256];

static struct retro_core_option_v2_category g_option_categories[] = {
    { "video", "Video", "Video and framebuffer presentation options." },
    { "system", "System", "System integration and compatibility options." },
    { NULL, NULL, NULL }
};

static struct retro_core_option_v2_definition g_option_defs[] = {
    {
        "zeebo_crop_overscan",
        "Crop Overscan",
        "Crop Overscan",
        "Hide the outer border of the framebuffer when enabled.",
        "Hide the outer border of the framebuffer when enabled.",
        "video",
        {
            { "disabled", "Disabled" },
            { "enabled",  "Enabled" },
            { NULL, NULL }
        },
        "disabled"
    },
    {
        "zeebo_username",
        "Username",
        "Username",
        "Name reported to games that query the frontend username.",
        "Name reported to games that query the frontend username.",
        "system",
        {
            { "Player", "Player" },
            { "Lucas",  "Lucas" },
            { NULL, NULL }
        },
        "Player"
    },
    {
        NULL, NULL, NULL, NULL, NULL, NULL,
        { { NULL, NULL } }, NULL
    }
};

static struct retro_core_options_v2 g_core_options = {
    g_option_categories,
    g_option_defs
};

static struct retro_variable g_legacy_variables[] = {
    { "zeebo_crop_overscan", "Crop Overscan; disabled|enabled" },
    { "zeebo_username", "Username; Player|Lucas" },
    { NULL, NULL }
};

/* Estado do jogo carregado */
static bool g_game_loaded = false;
static void *g_game_data = NULL;      /* copia para retro_reset */
static size_t g_game_size = 0;
static char g_game_path[512];
static zmod_info_t g_mod_info;

/* Buffer de audio do frame */
static int16_t g_audio_buf[ZEEBO_AUDIO_FRAMES * 2];

typedef struct {
    uint32_t magic;
    uint32_t version;
    zcpu_t cpu;
    uint32_t ram_size;
    uint32_t heap_size;
    uint32_t stack_size;
    uint32_t vram_size;
    uint32_t highpage_size;
    uint32_t audio_count[ZAUDIO_MAX_VOICES];
} zsave_header_t;

typedef struct {
    uint32_t active;
    uint32_t addr;
    uint32_t samples;
    uint32_t rate;
    uint32_t fmt;
    uint32_t loop;
    uint32_t volume;
    uint64_t pos_fp;
    uint64_t step_fp;
} zsave_voice_t;

static void sync_runtime_options(void);
static void register_memory_maps(void);
static void register_core_options(void);
static void register_disk_control(void);
static bool disk_set_eject_state(bool ejected);
static bool disk_get_eject_state(void);
static unsigned disk_get_image_index(void);
static bool disk_set_image_index(unsigned index);
static unsigned disk_get_num_images(void);
static bool disk_replace_image_index(unsigned index, const struct retro_game_info *info);
static bool disk_add_image_index(void);
static bool disk_set_initial_image(unsigned index, const char *path);
static bool disk_get_image_path(unsigned index, char *s, size_t len);
static bool disk_get_image_label(unsigned index, char *s, size_t len);
static bool parse_cheat_code(const char *code, uint32_t *addr, uint8_t *val);
static void apply_cheats(void);

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
    info->valid_extensions = "mod|mif|bar|ggz|zip";
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
    unsigned version = 0;
    environ_cb = cb;
    environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

    if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_iface))
        log_iface_ok = true;
    if (environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && version >= 2)
        register_core_options();
    else
        environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, g_legacy_variables);
    register_disk_control();
    register_memory_maps();
    if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &g_language) && g_language < 0)
        g_language = RETRO_LANGUAGE_ENGLISH;
    sync_runtime_options();
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
    (void)port;
    (void)device;
}

/* =====================================================
 * LOAD / UNLOAD GAME
 * ===================================================== */
static bool load_game_from_path(const char *path, void **out_data, size_t *out_size) {
    FILE *f;
    long sz;
    void *buf;

    if (!path || !path[0] || !out_data || !out_size)
        return false;

    f = fopen(path, "rb");
    if (!f) {
        LOGE("retro_load_game: falha ao abrir '%s'", path);
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    sz = ftell(f);
    if (sz <= 0) {
        fclose(f);
        return false;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    buf = malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return false;
    }

    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return false;
    }

    fclose(f);
    *out_data = buf;
    *out_size = (size_t)sz;
    return true;
}

static void make_asset_path_from_content(const struct retro_game_info *info,
                                         char *out, size_t out_len) {
    if (!out || out_len == 0) return;
    out[0] = '\0';

    if (g_game_from_archive && g_archive_path[0] && g_archive_file[0]) {
        const char *sep1 = strrchr(g_archive_path, '/');
        const char *sep2 = strrchr(g_archive_path, '\\');
        const char *sep = sep1 > sep2 ? sep1 : sep2;
        size_t dir_len = sep ? (size_t)(sep - g_archive_path) : 0;
        if (dir_len + 1 + strlen(g_archive_file) + 1 < out_len) {
            memcpy(out, g_archive_path, dir_len);
            out[dir_len] = '\0';
            if (dir_len > 0)
                snprintf(out + dir_len, out_len - dir_len, "%c%s",
                         sep && sep[0] == '/' ? '/' : '\\', g_archive_file);
            else
                snprintf(out, out_len, "%s", g_archive_file);
            return;
        }
    }

    if (info->path && info->path[0]) {
        snprintf(out, out_len, "%s", info->path);
    }
}

bool retro_load_game(const struct retro_game_info *info) {
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    void *loaded_data = NULL;
    size_t loaded_size = 0;
    const struct retro_game_info_ext *ext_info = NULL;
    char asset_path[512];

    if (!info) {
        LOGE("retro_load_game: sem info de conteudo");
        return false;
    }
    if ((!info->data || !info->size) && !(info->path && info->path[0])) {
        LOGE("retro_load_game: sem dados e sem caminho de conteudo");
        return false;
    }
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
        LOGE("retro_load_game: frontend nao suporta XRGB8888");
        return false;
    }
    g_game_from_archive = false;
    g_archive_path[0] = '\0';
    g_archive_file[0] = '\0';
    if (environ_cb(RETRO_ENVIRONMENT_GET_GAME_INFO_EXT, &ext_info) && ext_info &&
        ext_info->file_in_archive && ext_info->archive_path && ext_info->archive_file) {
        g_game_from_archive = true;
        snprintf(g_archive_path, sizeof(g_archive_path), "%s", ext_info->archive_path);
        snprintf(g_archive_file, sizeof(g_archive_file), "%s", ext_info->archive_file);
    }

    if (info->data && info->size) {
        loaded_data = malloc(info->size);
        if (!loaded_data) return false;
        memcpy(loaded_data, info->data, info->size);
        loaded_size = info->size;
    } else {
        if (!load_game_from_path(info->path, &loaded_data, &loaded_size)) {
            LOGE("retro_load_game: nao foi possivel carregar '%s'", info->path ? info->path : "(null)");
            return false;
        }
    }

    /* guarda copia para retro_reset */
    free(g_game_data);
    g_game_data = loaded_data;
    g_game_size = loaded_size;
    g_game_path[0] = '\0';
    make_asset_path_from_content(info, g_game_path, sizeof(g_game_path));
    LOGI("retro_load_game: caminho='%s' tamanho=%zu",
         g_game_path[0] ? g_game_path : "(memoria)", g_game_size);
    LOGI("retro_load_game: username='%s' overscan=%s", g_username, g_crop_overscan ? "enabled" : "disabled");

    zmem_reset();
    zfb_init();
    zaudio_reset();
    zbrew_reset();
    zbrew_init();

    make_asset_path_from_content(info, asset_path, sizeof(asset_path));
    if (!zmod_load(g_game_data, g_game_size,
                   asset_path[0] ? asset_path : NULL, &g_mod_info)) {
        LOGE("retro_load_game: falha no loader");
        free(g_game_data);
        g_game_data = NULL;
        g_game_size = 0;
        return false;
    }

    g_game_loaded = true;
    g_disk_ejected = false;
    g_disk_index = 0;
    LOGI("retro_load_game: '%s' pronto para executar", g_mod_info.name);
    return true;
}

void retro_unload_game(void) {
    LOGI("retro_unload_game");
    g_game_loaded = false;
    g_game_from_archive = false;
    g_archive_path[0] = '\0';
    g_archive_file[0] = '\0';
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
    sync_runtime_options();
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
    sync_runtime_options();

    if (g_game_loaded) {
        zbrew_tick_ms(16);
        zboot_tick(16);
        zboot_process_timers();
        if (!g_cpu.halted)
            zcpu_run(ZEEBO_INSTR_PER_FRAME);
        apply_cheats();
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
        if (g_crop_overscan)
            video_cb(fb + (8 * ZFB_WIDTH), ZFB_WIDTH, ZFB_HEIGHT - 16, ZFB_WIDTH * sizeof(uint32_t));
        else
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
    return sizeof(zsave_header_t)
         + ZMEM_RAM_SIZE
         + ZMEM_HEAP_SIZE
         + ZMEM_STACK_SIZE
         + ZMEM_VRAM_SIZE
         + 256u
         + sizeof(zsave_voice_t) * ZAUDIO_MAX_VOICES
         + 256u
         + 2048u
         + 1024u;
}

bool retro_serialize(void *data, size_t size) {
    uint8_t *p = (uint8_t *)data;
    zsave_header_t hdr;
    size_t need = retro_serialize_size();
    size_t i;
    if (!data || size < need)
        return false;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = 0x5A535441u; /* 'ZSTA' */
    hdr.version = 1;
    hdr.cpu = g_cpu;
    hdr.ram_size = ZMEM_RAM_SIZE;
    hdr.heap_size = ZMEM_HEAP_SIZE;
    hdr.stack_size = ZMEM_STACK_SIZE;
    hdr.vram_size = ZMEM_VRAM_SIZE;
    hdr.highpage_size = 256u;
    memcpy(p, &hdr, sizeof(hdr));
    p += sizeof(hdr);
    memcpy(p, zmem_ram_ptr_const(), ZMEM_RAM_SIZE); p += ZMEM_RAM_SIZE;
    memcpy(p, zmem_heap_ptr_const(), ZMEM_HEAP_SIZE); p += ZMEM_HEAP_SIZE;
    memcpy(p, zmem_stack_ptr_const(), ZMEM_STACK_SIZE); p += ZMEM_STACK_SIZE;
    memcpy(p, zmem_vram_ptr_const(), ZMEM_VRAM_SIZE); p += ZMEM_VRAM_SIZE;
    memcpy(p, zmem_highpage_ptr_const(), 256u); p += 256u;
    for (i = 0; i < ZAUDIO_MAX_VOICES; i++) {
        zvoice_t *v = zaudio_voice_internal((int)i);
        zsave_voice_t sv;
        memset(&sv, 0, sizeof(sv));
        sv.active = v->active ? 1u : 0u;
        sv.addr = v->addr;
        sv.samples = v->samples;
        sv.rate = v->rate;
        sv.fmt = v->fmt;
        sv.loop = v->loop ? 1u : 0u;
        sv.volume = v->volume;
        sv.pos_fp = v->pos_fp;
        sv.step_fp = v->step_fp;
        memcpy(p, &sv, sizeof(sv));
        p += sizeof(sv);
    }
    {
        size_t boot_need = zboot_serialize(p, size - (size_t)(p - (uint8_t *)data));
        if (boot_need == 0)
            return false;
        p += boot_need;
    }
    return (size_t)(p - (uint8_t *)data) <= size;
}

bool retro_unserialize(const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    zsave_header_t hdr;
    size_t i;
    if (!data || size < sizeof(hdr))
        return false;
    memcpy(&hdr, p, sizeof(hdr));
    if (hdr.magic != 0x5A535441u || hdr.version != 1)
        return false;
    p += sizeof(hdr);
    memcpy(zmem_ram_ptr(), p, ZMEM_RAM_SIZE); p += ZMEM_RAM_SIZE;
    memcpy(zmem_heap_ptr(), p, ZMEM_HEAP_SIZE); p += ZMEM_HEAP_SIZE;
    memcpy(zmem_stack_ptr(), p, ZMEM_STACK_SIZE); p += ZMEM_STACK_SIZE;
    memcpy(zmem_vram_ptr(), p, ZMEM_VRAM_SIZE); p += ZMEM_VRAM_SIZE;
    memcpy(zmem_highpage_ptr(), p, 256u); p += 256u;
    g_cpu = hdr.cpu;
    for (i = 0; i < ZAUDIO_MAX_VOICES; i++) {
        zsave_voice_t sv;
        zvoice_t *v = zaudio_voice_internal((int)i);
        memcpy(&sv, p, sizeof(sv));
        p += sizeof(sv);
        memset(v, 0, sizeof(*v));
        v->active = sv.active ? true : false;
        v->addr = sv.addr;
        v->samples = sv.samples;
        v->rate = sv.rate;
        v->fmt = (enum zpcm_format)sv.fmt;
        v->loop = sv.loop ? true : false;
        v->volume = sv.volume;
        v->pos_fp = sv.pos_fp;
        v->step_fp = sv.step_fp;
    }
    if (!zboot_unserialize(p, size - (size_t)(p - (const uint8_t *)data)))
        return false;
    return true;
}

/* =====================================================
 * CHEATS (nao usado)
 * ===================================================== */
void retro_cheat_reset(void) {
    memset(g_cheat_code, 0, sizeof(g_cheat_code));
    memset(g_cheat_addr, 0, sizeof(g_cheat_addr));
    memset(g_cheat_val, 0, sizeof(g_cheat_val));
    memset(g_cheat_enabled, 0, sizeof(g_cheat_enabled));
    g_cheat_count = 0;
}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {
    uint32_t addr = 0;
    uint8_t val = 0;
    if (index >= 32 || !code)
        return;
    snprintf(g_cheat_code[index], sizeof(g_cheat_code[index]), "%s", code);
    if (parse_cheat_code(code, &addr, &val)) {
        g_cheat_addr[index] = addr;
        g_cheat_val[index] = val;
        g_cheat_enabled[index] = enabled;
        if (index >= g_cheat_count)
            g_cheat_count = index + 1;
    }
}

/* =====================================================
 * MEMORY (para o frontend inspecionar; fase 7)
 * ===================================================== */
void *retro_get_memory_data(unsigned id) {
    switch (id) {
    case RETRO_MEMORY_SYSTEM_RAM: return zmem_ram_ptr();
    case RETRO_MEMORY_VIDEO_RAM: return zmem_vram_ptr();
    default: return NULL;
    }
}

static void sync_runtime_options(void) {
    struct retro_variable var = {0};
    if (!environ_cb)
        return;
    var.key = "zeebo_crop_overscan";
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        g_crop_overscan = (strcmp(var.value, "enabled") == 0);
    var.key = "zeebo_username";
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        g_username = var.value;
}

static void register_memory_maps(void) {
    if (!environ_cb || g_memory_map_set)
        return;
    memset(g_memory_desc, 0, sizeof(g_memory_desc));

    g_memory_desc[0].flags = RETRO_MEMDESC_SYSTEM_RAM;
    g_memory_desc[0].ptr = zmem_ram_ptr();
    g_memory_desc[0].start = ZMEM_RAM_BASE;
    g_memory_desc[0].len = ZMEM_RAM_SIZE;
    g_memory_desc[0].addrspace = "RAM";

    g_memory_desc[1].flags = RETRO_MEMDESC_SYSTEM_RAM;
    g_memory_desc[1].ptr = zmem_heap_ptr();
    g_memory_desc[1].start = ZMEM_HEAP_BASE;
    g_memory_desc[1].len = ZMEM_HEAP_SIZE;
    g_memory_desc[1].addrspace = "HEAP";

    g_memory_desc[2].flags = RETRO_MEMDESC_SYSTEM_RAM;
    g_memory_desc[2].ptr = zmem_stack_ptr();
    g_memory_desc[2].start = ZMEM_STACK_BASE;
    g_memory_desc[2].len = ZMEM_STACK_SIZE;
    g_memory_desc[2].addrspace = "STACK";

    g_memory_desc[3].flags = RETRO_MEMDESC_VIDEO_RAM;
    g_memory_desc[3].ptr = zmem_vram_ptr();
    g_memory_desc[3].start = ZMEM_VRAM_BASE;
    g_memory_desc[3].len = ZMEM_VRAM_SIZE;
    g_memory_desc[3].addrspace = "VRAM";

    g_memory_map.descriptors = g_memory_desc;
    g_memory_map.num_descriptors = (unsigned)(sizeof(g_memory_desc) / sizeof(g_memory_desc[0]));
    if (environ_cb(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, &g_memory_map))
        g_memory_map_set = true;
}

static void register_core_options(void) {
    if (!environ_cb)
        return;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &g_core_options))
        environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, g_legacy_variables);
}

static void register_disk_control(void) {
    struct retro_disk_control_ext_callback cb;
    memset(&cb, 0, sizeof(cb));
    cb.set_eject_state = disk_set_eject_state;
    cb.get_eject_state = disk_get_eject_state;
    cb.get_image_index = disk_get_image_index;
    cb.set_image_index = disk_set_image_index;
    cb.get_num_images = disk_get_num_images;
    cb.replace_image_index = disk_replace_image_index;
    cb.add_image_index = disk_add_image_index;
    cb.set_initial_image = disk_set_initial_image;
    cb.get_image_path = disk_get_image_path;
    cb.get_image_label = disk_get_image_label;
    environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, &cb);
}

static void apply_cheats(void) {
    unsigned i;
    for (i = 0; i < g_cheat_count; i++) {
        if (g_cheat_enabled[i]) {
            zmem_write8(g_cheat_addr[i], g_cheat_val[i]);
        }
    }
}

static bool parse_cheat_code(const char *code, uint32_t *addr, uint8_t *val) {
    unsigned a = 0, v = 0;
    if (!code) return false;
    if (sscanf(code, "%x=%x", &a, &v) == 2) {
        *addr = (uint32_t)a;
        *val = (uint8_t)(v & 0xFFu);
        return true;
    }
    return false;
}

static bool disk_set_eject_state(bool ejected) { g_disk_ejected = ejected; return true; }
static bool disk_get_eject_state(void) { return g_disk_ejected; }
static unsigned disk_get_image_index(void) { return g_disk_ejected ? g_disk_index + 1u : g_disk_index; }
static bool disk_set_image_index(unsigned index) { if (!g_disk_ejected && index != g_disk_index) return false; g_disk_index = index; return true; }
static unsigned disk_get_num_images(void) { return 1; }
static bool disk_replace_image_index(unsigned index, const struct retro_game_info *info) { (void)index; (void)info; return false; }
static bool disk_add_image_index(void) { return false; }
static bool disk_set_initial_image(unsigned index, const char *path) { (void)path; g_disk_index = index; g_disk_has_initial = true; return true; }
static bool disk_get_image_path(unsigned index, char *s, size_t len) {
    if (index != 0 || !s || len == 0 || !g_game_path[0]) return false;
    snprintf(s, len, "%s", g_game_path);
    return true;
}
static bool disk_get_image_label(unsigned index, char *s, size_t len) {
    if (index != 0 || !s || len == 0) return false;
    snprintf(s, len, "%s", g_game_path[0] ? g_game_path : "Zeebo");
    return true;
}

size_t retro_get_memory_size(unsigned id) {
    switch (id) {
    case RETRO_MEMORY_SYSTEM_RAM: return ZMEM_RAM_SIZE;
    case RETRO_MEMORY_VIDEO_RAM: return ZMEM_VRAM_SIZE;
    default: return 0;
    }
}
