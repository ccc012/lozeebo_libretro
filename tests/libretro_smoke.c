#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
typedef HMODULE core_handle_t;
#else
#include <dlfcn.h>
typedef void *core_handle_t;
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

static void video(const void *data, unsigned width, unsigned height, size_t pitch)
{
    const uint32_t first_pixel = data ? *(const uint32_t *)data : 0;
    static unsigned frames;
    if ((frames++ % 60) == 0)
        fprintf(stderr, "[VIDEO] %ux%u pitch=%zu first=0x%08X\n",
                width, height, pitch, first_pixel);
    if (g_dump_path && data) {
        /* reescreve a cada frame; o arquivo final = ultimo frame */
        FILE *f = fopen(g_dump_path, "wb");
        if (f) {
            unsigned x, y;
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

static size_t audio_batch(const int16_t *data, size_t frames)
{
    (void)data;
    return frames;
}

static void input_poll(void) {}

static unsigned input_frame;

static int16_t input_state(unsigned port, unsigned device,
                           unsigned index, unsigned id)
{
    (void)port;
    (void)device;
    (void)index;
    if (port == 0 && device == RETRO_DEVICE_JOYPAD && index == 0 &&
        id == RETRO_DEVICE_ID_JOYPAD_A && input_frame == 30)
        return 1;
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

    for (frame = 0; frame < (argc == 5 ? atoi(argv[4]) : 180); ++frame) {
        input_frame = frame;
        retro_run();
    }

    retro_unload_game();
    retro_deinit();
    free(rom_data);
    core_close(core);
    return 0;
}
