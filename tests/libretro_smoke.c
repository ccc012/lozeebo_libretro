#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define MAX_INPUT_EVENTS 64
struct input_event { unsigned frame, hold, id; };
static struct input_event input_events[MAX_INPUT_EVENTS];
static unsigned input_event_count;

static unsigned button_id(const char *name)
{
    if (!strcmp(name, "START")) return RETRO_DEVICE_ID_JOYPAD_START;
    if (!strcmp(name, "B")) return RETRO_DEVICE_ID_JOYPAD_B;
    if (!strcmp(name, "UP")) return RETRO_DEVICE_ID_JOYPAD_UP;
    if (!strcmp(name, "DOWN")) return RETRO_DEVICE_ID_JOYPAD_DOWN;
    if (!strcmp(name, "LEFT")) return RETRO_DEVICE_ID_JOYPAD_LEFT;
    if (!strcmp(name, "RIGHT")) return RETRO_DEVICE_ID_JOYPAD_RIGHT;
    return RETRO_DEVICE_ID_JOYPAD_A;
}

/* ZEEMU_SMOKE_INPUT_SCRIPT: frame:button:hold[,frame:button:hold...]. */
static bool parse_input_script(const char *script)
{
    char *copy, *part;
    if (!script || !*script) return false;
    copy = (char *)malloc(strlen(script) + 1);
    if (!copy) return false;
    strcpy(copy, script);
    for (part = strtok(copy, ","); part && input_event_count < MAX_INPUT_EVENTS;
         part = strtok(NULL, ",")) {
        char name[16];
        struct input_event *event = &input_events[input_event_count];
        if (sscanf(part, "%u:%15[^:]:%u", &event->frame, name, &event->hold) != 3 || !event->hold) {
            fprintf(stderr, "Invalid ZEEMU_SMOKE_INPUT_SCRIPT event: %s\n", part);
            free(copy);
            input_event_count = 0;
            return false;
        }
        event->id = button_id(name);
        input_event_count++;
    }
    free(copy);
    return input_event_count != 0;
}

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

static int16_t input_state(unsigned port, unsigned device,
                           unsigned index, unsigned id)
{
    unsigned press, hold, i;
    bool active = false;
    static unsigned last_logged = 0xFFFFFFFFu;
    (void)index;
    if (port != 0 || device != RETRO_DEVICE_JOYPAD)
        return 0;
    if (input_event_count) {
        for (i = 0; i < input_event_count; i++)
            if (id == input_events[i].id && input_frame >= input_events[i].frame &&
                input_frame < input_events[i].frame + input_events[i].hold)
                active = true;
    } else {
        press = smoke_press_frame();
        hold = smoke_hold_frames();
        active = id == smoke_button_id() && input_frame >= press && input_frame < press + hold;
    }
    if (active && input_frame != last_logged) {
        fprintf(stderr, "[SMOKE-INPUT] frame=%u segurando botao id=%u\n", input_frame, id);
        last_logged = input_frame;
    }
    return active ? 1 : 0;
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
    if (getenv("ZEEMU_SMOKE_INPUT_SCRIPT") &&
        !parse_input_script(getenv("ZEEMU_SMOKE_INPUT_SCRIPT")))
        return 2;

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
