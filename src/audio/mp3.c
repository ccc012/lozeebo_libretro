/* mp3.c — Decoder MP3 via dr_mp3 (compatível com Infuse) */
#include <string.h>
#include <stdlib.h>
#include "audio.h"
#include "../memory/memory.h"
#include "../debug/log.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_SIMD
#include "extern/drmp3/dr_mp3.h"

#define ZMP3_MAX_STREAMS 4

typedef struct {
    drmp3 dr;
    bool initialized;
    uint8_t *data;
    uint32_t bytes;
    int voice;
    bool loop;
    int16_t pcm_buf[4096 * 2];
    uint32_t pcm_pos;
    uint32_t pcm_count;
} zmp3_stream_t;

static zmp3_stream_t g_mp3[ZMP3_MAX_STREAMS];

void zmp3_init(void) {
    memset(g_mp3, 0, sizeof(g_mp3));
}

void zmp3_shutdown(void) {
    for (int i = 0; i < ZMP3_MAX_STREAMS; i++) {
        if (g_mp3[i].initialized) {
            drmp3_uninit(&g_mp3[i].dr);
            free(g_mp3[i].data);
            g_mp3[i].initialized = false;
        }
    }
}

int zmp3_play(uint32_t addr, uint32_t bytes, bool loop) {
    for (int i = 0; i < ZMP3_MAX_STREAMS; i++) {
        if (g_mp3[i].initialized) continue;
        memset(&g_mp3[i], 0, sizeof(zmp3_stream_t));

        /* Copia dados da memoria emulada */
        g_mp3[i].data = (uint8_t *)malloc(bytes);
        if (!g_mp3[i].data) return -1;
        const uint8_t *src = (const uint8_t *)zmem_host_ptr(addr, bytes);
        if (!src) { free(g_mp3[i].data); return -1; }
        memcpy(g_mp3[i].data, src, bytes);
        g_mp3[i].bytes = bytes;

        if (!drmp3_init_memory(&g_mp3[i].dr, g_mp3[i].data, (size_t)bytes, NULL)) {
            LOGE("zmp3_play: drmp3_init_memory falhou");
            free(g_mp3[i].data);
            return -1;
        }
        g_mp3[i].initialized = true;
        g_mp3[i].loop = loop;
        g_mp3[i].pcm_pos = 0;
        g_mp3[i].pcm_count = 0;
        LOGI("zmp3_play: MP3 %uHz %uch bytes=%u", g_mp3[i].dr.sampleRate, g_mp3[i].dr.channels, bytes);
        return i;
    }
    LOGW("zmp3_play: sem slot livre");
    return -1;
}

void zmp3_stop(int stream) {
    if (stream < 0 || stream >= ZMP3_MAX_STREAMS || !g_mp3[stream].initialized) return;
    drmp3_uninit(&g_mp3[stream].dr);
    free(g_mp3[stream].data);
    g_mp3[stream].initialized = false;
}

void zmp3_stop_all(void) {
    for (int i = 0; i < ZMP3_MAX_STREAMS; i++)
        zmp3_stop(i);
}

/* Soma sample com clamp no destino stereo L/R. */
static void zmp3_mix_s16(int16_t *dst, int16_t sample) {
    int32_t acc = (int32_t)*dst + (int32_t)sample;
    if (acc > 32767) acc = 32767;
    if (acc < -32768) acc = -32768;
    *dst = (int16_t)acc;
}

/* Preenche o buffer stereo intercalado (L,R) com frames MP3.
 * Mono: duplica L=R. Stereo: copia intercalado. */
void zmp3_fill_voice(int stream, int16_t *out, uint32_t frames) {
    if (stream < 0 || stream >= ZMP3_MAX_STREAMS || !g_mp3[stream].initialized) return;
    zmp3_stream_t *s = &g_mp3[stream];
    uint32_t ch = s->dr.channels ? s->dr.channels : 1;
    uint32_t written = 0;
    while (written < frames) {
        if (s->pcm_pos >= s->pcm_count) {
            drmp3_uint64 got = drmp3_read_pcm_frames_s16(&s->dr, 2048, s->pcm_buf);
            if (got == 0) {
                if (s->loop) {
                    drmp3_seek_to_pcm_frame(&s->dr, 0);
                    got = drmp3_read_pcm_frames_s16(&s->dr, 2048, s->pcm_buf);
                }
                if (got == 0) break;
            }
            s->pcm_count = (uint32_t)got * ch;
            s->pcm_pos = 0;
        }
        uint32_t avail_frames = (s->pcm_count - s->pcm_pos) / ch;
        uint32_t n = frames - written;
        if (n > avail_frames) n = avail_frames;
        for (uint32_t i = 0; i < n; i++) {
            int16_t *dst = &out[(written + i) * 2];
            if (ch >= 2) {
                zmp3_mix_s16(&dst[0], s->pcm_buf[s->pcm_pos + i * 2]);
                zmp3_mix_s16(&dst[1], s->pcm_buf[s->pcm_pos + i * 2 + 1]);
            } else {
                int16_t m = s->pcm_buf[s->pcm_pos + i];
                zmp3_mix_s16(&dst[0], m);
                zmp3_mix_s16(&dst[1], m);
            }
        }
        s->pcm_pos += n * ch;
        written += n;
    }
}

/* Re-Play: volta ao inicio sem reabrir o decoder (evita PCM sujo). */
void zmp3_restart(int stream) {
    if (stream < 0 || stream >= ZMP3_MAX_STREAMS || !g_mp3[stream].initialized) return;
    drmp3_seek_to_pcm_frame(&g_mp3[stream].dr, 0);
    g_mp3[stream].pcm_pos = 0;
    g_mp3[stream].pcm_count = 0;
}

void zmp3_render_all(int16_t *out, uint32_t frames) {
    for (int i = 0; i < ZMP3_MAX_STREAMS; i++) {
        if (g_mp3[i].initialized)
            zmp3_fill_voice(i, out, frames);
    }
}

bool zmp3_is_active(int stream) {
    if (stream < 0 || stream >= ZMP3_MAX_STREAMS) return false;
    return g_mp3[stream].initialized;
}
