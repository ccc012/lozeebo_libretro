/* audio.c - Estado das vozes do mixer */
#include <string.h>
#include "audio_internal.h"
#include "../debug/log.h"

static zvoice_t g_voices[ZAUDIO_MAX_VOICES];

void zaudio_init(void) {
    zaudio_reset();
}

void zaudio_reset(void) {
    memset(g_voices, 0, sizeof(g_voices));
}

int zaudio_play_pcm(uint32_t addr, uint32_t bytes, uint32_t rate,
                    enum zpcm_format fmt, bool loop, uint8_t volume) {
    int i;
    if (rate == 0) rate = ZAUDIO_SAMPLE_RATE;
    if (fmt != ZPCM_U8 && fmt != ZPCM_S16) fmt = ZPCM_S16;
    for (i = 0; i < ZAUDIO_MAX_VOICES; i++) {
        if (g_voices[i].active) continue;
        zvoice_t *v = &g_voices[i];
        v->active  = true;
        v->addr    = addr;
        v->samples = zpcm_sample_count(bytes, fmt);
        v->rate    = rate;
        v->fmt     = fmt;
        v->loop    = loop;
        v->volume  = volume;
        v->pos_fp  = 0;
        v->step_fp = rate ? (((uint64_t)rate << 32) / ZAUDIO_SAMPLE_RATE) : 0;
        if (v->samples == 0) v->active = false;
        return i;
    }
    LOGW("zaudio_play_pcm: sem voz livre");
    return -1;
}

void zaudio_stop(int voice) {
    if (voice >= 0 && voice < ZAUDIO_MAX_VOICES)
        g_voices[voice].active = false;
}

void zaudio_stop_all(void) {
    int i;
    for (i = 0; i < ZAUDIO_MAX_VOICES; i++)
        g_voices[i].active = false;
}

bool zaudio_voice_active(int voice) {
    if (voice < 0 || voice >= ZAUDIO_MAX_VOICES) return false;
    return g_voices[voice].active;
}

void zaudio_set_volume(int voice, uint8_t volume) {
    if (voice >= 0 && voice < ZAUDIO_MAX_VOICES)
        g_voices[voice].volume = volume;
}

zvoice_t *zaudio_voice_internal(int i) {
    return &g_voices[i];
}
