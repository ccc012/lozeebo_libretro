#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
typedef HMODULE core_handle_t;
static uint64_t monotonic_ms(void) { return GetTickCount64(); }
#else
#include <dlfcn.h>
#include <time.h>
typedef void *core_handle_t;
static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
#endif

#include "../src/core/libretro.h"

static void frontend_log(enum retro_log_level level, const char *fmt, ...)
{
    va_list args;
    static const char *names[] = { "DEBUG", "INFO", "WARN", "ERROR" };
    fprintf(stderr, "[%s] ", names[level <= RETRO_LOG_ERROR ? level : RETRO_LOG_ERROR]);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static bool environment(unsigned command, void *data)
{
    if (command == RETRO_ENVIRONMENT_GET_LOG_INTERFACE) {
        ((struct retro_log_callback *)data)->log = frontend_log;
        return true;
    }
    if (command == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT)
        return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
    if (command == RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME)
        return true;
    return false;
}

static const char *g_dump_path; /* argv[3]: despeja o ultimo frame (PPM) */
static unsigned g_target_frames;
static unsigned g_video_frames;
static size_t g_audio_frames;
static unsigned g_audio_peak;
static size_t g_audio_nonzero;
static uint32_t g_video_first;
static uint32_t g_video_xor;
static bool g_video_varied;
static bool g_video_ever_varied;
static FILE *g_audio_dump;

static void video(const void *data, unsigned width, unsigned height, size_t pitch)
{
    const uint32_t first_pixel = data ? *(const uint32_t *)data : 0;
    unsigned x, y;
    g_video_first = first_pixel;
    g_video_xor = 0;
    g_video_varied = false;
    for (y = 0; data && y < height; ++y) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)data + y * pitch);
        for (x = 0; x < width; ++x) {
            g_video_xor ^= row[x] + x + y * width;
            if (row[x] != first_pixel) g_video_varied = true;
        }
    }
    if (g_video_varied) g_video_ever_varied = true;
    if ((g_video_frames++ % 60) == 0)
        fprintf(stderr, "[VIDEO] %ux%u pitch=%zu first=0x%08X\n",
                width, height, pitch, first_pixel);
    /* DUMP_DIR: stride (default 10). Sem DUMP_DIR: so frame-alvo. */
    {
        const char *dump_dir = getenv("ZEEMU_SMOKE_DUMP_DIR");
        int dump_stride = 10;
        const char *ds = getenv("ZEEMU_SMOKE_DUMP_STRIDE");
        if (ds && ds[0]) dump_stride = atoi(ds);
        if (dump_stride < 1) dump_stride = 1;
        int dump_dir_on = dump_dir && dump_dir[0];
        unsigned fi = g_video_frames ? g_video_frames - 1 : 0;
        int dump_now = g_dump_path && data &&
            (dump_dir_on ? (fi % (unsigned)dump_stride == 0) :
             (!g_target_frames || g_video_frames == g_target_frames));
        /* Sempre gravar PPM final no ultimo frame, mesmo com DUMP_DIR */
        int is_final = g_dump_path && data && g_target_frames &&
                       g_video_frames == g_target_frames;
    if (dump_now || is_final) {
        char p_file[512];
        const char *p_target = g_dump_path;
        if (dump_dir_on && !is_final) {
            _snprintf(p_file, sizeof(p_file), "%s\\frame_%04u.ppm",
                      dump_dir, fi);
            p_target = p_file;
        }
        /* reescreve a cada frame; o arquivo final = ultimo frame */
        FILE *f = fopen(p_target, "wb");
        if (f) {
            fprintf(f, "P6\n%u %u\n255\n", width, height);
            for (y = 0; y < height; y++) {
                const uint32_t *row = (const uint32_t *)
                    ((const uint8_t *)data + y * pitch);
                for (x = 0; x < width; x++) {
                    uint8_t rgb[3];
                    rgb[0] = (uint8_t)(row[x] >> 16);
                    rgb[1] = (uint8_t)(row[x] >> 8);
                    rgb[2] = (uint8_t)(row[x]);
                    fwrite(rgb, 1, 3, f);
                }
            }
            fclose(f);
        }
    }
    }
}

static size_t audio_batch(const int16_t *data, size_t frames)
{
    size_t i;
    g_audio_frames += frames;
    for (i = 0; data && i < frames * 2; ++i) {
        unsigned sample = data[i] < 0 ? (unsigned)-(int)data[i] : (unsigned)data[i];
        if (sample) g_audio_nonzero++;
        if (sample > g_audio_peak) g_audio_peak = sample;
    }
    if (g_audio_dump && data && frames > 0) {
        fwrite(data, sizeof(int16_t), frames * 2, g_audio_dump);
    }
    return frames;
}

static void input_poll(void) {}

static unsigned input_frame;

/* Simulacao de input pra teste ponta-a-ponta (Tarefa E, Rodada 5):
 * segura o botao configurado via ZEEMU_SMOKE_BUTTON (padrao "A") por
 * ZEEMU_SMOKE_HOLD_FRAMES (padrao 10) a partir de ZEEMU_SMOKE_PRESS_FRAME
 * (padrao 120). Isso vira infraestrutura permanente pra testar o
 * caminho RetroPad -> HID -> signal/EVT_KEY sem precisar do RetroArch. */
static unsigned smoke_press_frame(void) {
    const char *v = getenv("ZEEMU_SMOKE_PRESS_FRAME");
    return v ? (unsigned)atoi(v) : 120u;
}
static unsigned smoke_hold_frames(void) {
    const char *v = getenv("ZEEMU_SMOKE_HOLD_FRAMES");
    return v ? (unsigned)atoi(v) : 10u;
}
static unsigned smoke_button_id(void) {
    const char *v = getenv("ZEEMU_SMOKE_BUTTON");
    if (!v) return RETRO_DEVICE_ID_JOYPAD_A;
    if (!strcmp(v, "START")) return RETRO_DEVICE_ID_JOYPAD_START;
    if (!strcmp(v, "B")) return RETRO_DEVICE_ID_JOYPAD_B;
    if (!strcmp(v, "UP")) return RETRO_DEVICE_ID_JOYPAD_UP;
    if (!strcmp(v, "DOWN")) return RETRO_DEVICE_ID_JOYPAD_DOWN;
    return RETRO_DEVICE_ID_JOYPAD_A;
}

/* Sequencia de input via ZEEMU_SMOKE_SEQ: "UP:1100,A:1130,DOWN:1200:30"
 * Cada item = BOTAO:FRAME[:HOLD]. Botao segura por HOLD frames (padrao
 * ZEEMU_SMOKE_HOLD_FRAMES). Permite testar navegacao+selecao num run so. */
typedef struct {
    unsigned id;
    unsigned frame;
    unsigned hold;
} smoke_seq_item_t;

static const smoke_seq_item_t *smoke_seq_items(int *count)
{
    static smoke_seq_item_t items[128];
    static int parsed = 0;
    static int parsed_n = 0;
    const char *v = getenv("ZEEMU_SMOKE_SEQ");
    char buf[2048];
    char *tok, *save;
    int n = 0;
    if (parsed) {
        *count = parsed_n;
        return items;
    }
    if (!v) {
        *count = 0;
        parsed = 1;
        parsed_n = 0;
        return items;
    }
    strncpy(buf, v, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    tok = strtok_s(buf, ",", &save);
    while (tok && n < 128) {
        char btn[32];
        unsigned frame = 0, hold = 0;
        int field = sscanf(tok, "%31[^:]%*[:]%u%*[:]%u", btn, &frame, &hold);
        if (field >= 2) {
            unsigned id = RETRO_DEVICE_ID_JOYPAD_A;
            if (!strcmp(btn, "START")) id = RETRO_DEVICE_ID_JOYPAD_START;
            else if (!strcmp(btn, "A")) id = RETRO_DEVICE_ID_JOYPAD_A;
            else if (!strcmp(btn, "B")) id = RETRO_DEVICE_ID_JOYPAD_B;
            else if (!strcmp(btn, "UP")) id = RETRO_DEVICE_ID_JOYPAD_UP;
            else if (!strcmp(btn, "DOWN")) id = RETRO_DEVICE_ID_JOYPAD_DOWN;
            else if (!strcmp(btn, "LEFT")) id = RETRO_DEVICE_ID_JOYPAD_LEFT;
            else if (!strcmp(btn, "RIGHT")) id = RETRO_DEVICE_ID_JOYPAD_RIGHT;
            items[n].id = id;
            items[n].frame = frame;
            items[n].hold = (field >= 3 && hold) ? hold : smoke_hold_frames();
            n++;
        }
        tok = strtok_s(NULL, ",", &save);
    }
    parsed = 1;
    parsed_n = n;
    *count = n;
    return items;
}

static int16_t input_state(unsigned port, unsigned device,
                           unsigned index, unsigned id)
{
    unsigned press, hold;
    int seq_n;
    const smoke_seq_item_t *seq;
    (void)index;
    if (port != 0 || device != RETRO_DEVICE_JOYPAD)
        return 0;
    press = smoke_press_frame();
    hold = smoke_hold_frames();
    if (id == smoke_button_id() &&
        input_frame >= press && input_frame < press + hold) {
        static unsigned last_logged = 0xFFFFFFFFu;
        if (input_frame != last_logged) {
            fprintf(stderr, "[SMOKE-INPUT] frame=%u segurando botao id=%u\n",
                    input_frame, id);
            last_logged = input_frame;
        }
        return 1;
    }
    seq = smoke_seq_items(&seq_n);
    for (int i = 0; i < seq_n; i++) {
        if (id == seq[i].id &&
            input_frame >= seq[i].frame &&
            input_frame < seq[i].frame + seq[i].hold) {
            static unsigned last_seq_logged = 0xFFFFFFFFu;
            if (input_frame != last_seq_logged) {
                fprintf(stderr, "[SMOKE-SEQ] frame=%u segurando id=%u\n",
                        input_frame, id);
                last_seq_logged = input_frame;
            }
            return 1;
        }
    }
    return 0;
}

static void *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    void *data;
    long length;
    if (!file)
        return NULL;
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);
    data = malloc((size_t)length);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        data = NULL;
    }
    fclose(file);
    *size = data ? (size_t)length : 0;
    return data;
}

static core_handle_t core_open(const char *path)
{
#if defined(_WIN32)
    return LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void *core_symbol(core_handle_t core, const char *name)
{
#if defined(_WIN32)
    return (void *)GetProcAddress(core, name);
#else
    return dlsym(core, name);
#endif
}

static void core_close(core_handle_t core)
{
#if defined(_WIN32)
    FreeLibrary(core);
#else
    dlclose(core);
#endif
}

static void print_load_error(const char *prefix)
{
#if defined(_WIN32)
    fprintf(stderr, "%s: %lu\n", prefix, GetLastError());
#else
    const char *err = dlerror();
    fprintf(stderr, "%s: %s\n", prefix, err ? err : "desconhecido");
#endif
}

#define LOAD_API(name) do { \
    name = (name##_t)core_symbol(core, #name); \
    if (!name) { fprintf(stderr, "Missing export: %s\n", #name); return 3; } \
} while (0)

typedef void (*retro_set_environment_t)(retro_environment_t);
typedef void (*retro_set_video_refresh_t)(retro_video_refresh_t);
typedef void (*retro_set_audio_sample_batch_t)(retro_audio_sample_batch_t);
typedef void (*retro_set_input_poll_t)(retro_input_poll_t);
typedef void (*retro_set_input_state_t)(retro_input_state_t);
typedef void (*retro_init_t)(void);
typedef void (*retro_deinit_t)(void);
typedef bool (*retro_load_game_t)(const struct retro_game_info *);
typedef void (*retro_unload_game_t)(void);
typedef void (*retro_run_t)(void);

int main(int argc, char **argv)
{
    core_handle_t core;
    void *rom_data;
    size_t rom_size;
    struct retro_game_info game = {0};
    unsigned frame;
    retro_set_environment_t retro_set_environment;
    retro_set_video_refresh_t retro_set_video_refresh;
    retro_set_audio_sample_batch_t retro_set_audio_sample_batch;
    retro_set_input_poll_t retro_set_input_poll;
    retro_set_input_state_t retro_set_input_state;
    retro_init_t retro_init;
    retro_deinit_t retro_deinit;
    retro_load_game_t retro_load_game;
    retro_unload_game_t retro_unload_game;
    retro_run_t retro_run;
    uint64_t started_ms;
    unsigned max_seconds = 0;

    if (argc < 3 || argc > 5) {
        fprintf(stderr,
                "Usage: %s core.{dll,so,dylib} game.mod [frame_dump.ppm]\n",
                argv[0]);
        return 2;
    }
    if (argc >= 4)
        g_dump_path = argv[3];

    core = core_open(argv[1]);
    if (!core) {
        print_load_error("Could not load core");
        return 3;
    }

    LOAD_API(retro_set_environment);
    LOAD_API(retro_set_video_refresh);
    LOAD_API(retro_set_audio_sample_batch);
    LOAD_API(retro_set_input_poll);
    LOAD_API(retro_set_input_state);
    LOAD_API(retro_init);
    LOAD_API(retro_deinit);
    LOAD_API(retro_load_game);
    LOAD_API(retro_unload_game);
    LOAD_API(retro_run);

    rom_data = read_file(argv[2], &rom_size);
    if (!rom_data) {
        fprintf(stderr, "Could not read ROM: %s\n", argv[2]);
        core_close(core);
        return 4;
    }

    game.path = argv[2];
    game.data = rom_data;
    game.size = rom_size;

    {
        const char *audio_dump_path = getenv("ZEEMU_SMOKE_AUDIO_DUMP");
        if (audio_dump_path && audio_dump_path[0]) {
            g_audio_dump = fopen(audio_dump_path, "wb");
            if (g_audio_dump) {
                /* WAV header placeholder: 44100Hz stereo s16le */
                uint8_t wav_hdr[44] = {
                    'R','I','F','F', 0,0,0,0, 'W','A','V','E',
                    'f','m','t',' ', 16,0,0,0, 1,0, 2,0,
                    0x44,0xAC,0,0, 0x10,0xB1,2,0, 4,0, 16,0,
                    'd','a','t','a', 0,0,0,0
                };
                fwrite(wav_hdr, 1, 44, g_audio_dump);
            }
        }
    }
    retro_set_environment(environment);
    retro_set_video_refresh(video);
    retro_set_audio_sample_batch(audio_batch);
    retro_set_input_poll(input_poll);
    retro_set_input_state(input_state);
    retro_init();

    if (!retro_load_game(&game)) {
        fprintf(stderr, "retro_load_game failed\n");
        retro_deinit();
        free(rom_data);
        core_close(core);
        return 5;
    }

    {
        const char *limit = getenv("ZEEMU_SMOKE_MAX_SECONDS");
        if (limit) max_seconds = (unsigned)atoi(limit);
    }
    g_target_frames = argc == 5 ? (unsigned)atoi(argv[4]) : 180u;
    started_ms = monotonic_ms();
    for (frame = 0; frame < g_target_frames; ++frame) {
        input_frame = frame;
        retro_run();
        if (max_seconds && monotonic_ms() - started_ms > (uint64_t)max_seconds * 1000u) {
            fprintf(stderr, "[SMOKE-TIMEOUT] frame=%u elapsed_ms=%llu limit_s=%u\n",
                    frame + 1, (unsigned long long)(monotonic_ms() - started_ms), max_seconds);
            retro_unload_game();
            retro_deinit();
            free(rom_data);
            core_close(core);
            return 6;
        }
    }

    fprintf(stderr, "[SMOKE-TIMING] frames=%u elapsed_ms=%llu\n", frame,
            (unsigned long long)(monotonic_ms() - started_ms));
    fprintf(stderr,
            "[SMOKE-SUMMARY] video_frames=%u video_first=%08X video_xor=%08X video_varied=%u video_ever_varied=%u audio_frames=%zu audio_nonzero=%zu audio_peak=%u\n",
            g_video_frames, g_video_first, g_video_xor, g_video_varied ? 1u : 0u,
            g_video_ever_varied ? 1u : 0u,
            g_audio_frames, g_audio_nonzero, g_audio_peak);
    fflush(stderr);
    {
        FILE *af = fopen("smoke_audio_summary.txt", "w");
        if (af) {
            fprintf(af,
                    "video_frames=%u audio_frames=%zu audio_nonzero=%zu audio_peak=%u\n",
                    g_video_frames, g_audio_frames, g_audio_nonzero, g_audio_peak);
            fclose(af);
        }
    }
    if (g_audio_dump) {
        uint32_t data_size = (uint32_t)(g_audio_frames * 4);
        uint32_t file_size = data_size + 36;
        fseek(g_audio_dump, 4, SEEK_SET);
        fwrite(&file_size, 4, 1, g_audio_dump);
        fseek(g_audio_dump, 40, SEEK_SET);
        fwrite(&data_size, 4, 1, g_audio_dump);
        fclose(g_audio_dump);
        g_audio_dump = NULL;
    }
    retro_unload_game();
    retro_deinit();
    free(rom_data);
    core_close(core);
    return 0;
}
