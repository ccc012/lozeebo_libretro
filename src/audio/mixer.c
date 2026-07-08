/* mixer.c - Mixagem das vozes para o buffer de saida do RetroArch */
#include <string.h>
#include "audio_internal.h"

void zaudio_render(int16_t *out, uint32_t frames) {
    uint32_t f;
    int v;

    memset(out, 0, frames * 2 * sizeof(int16_t));

    for (v = 0; v < ZAUDIO_MAX_VOICES; v++) {
        zvoice_t *voice = zaudio_voice_internal(v);
        if (!voice->active) continue;

        for (f = 0; f < frames; f++) {
            uint32_t idx = (uint32_t)(voice->pos_fp >> 32);
            if (idx >= voice->samples) {
                if (voice->loop && voice->samples > 0) {
                    voice->pos_fp = 0;
                    idx = 0;
                } else {
                    voice->active = false;
                    break;
                }
            }
            int32_t s = zpcm_decode_sample(voice->addr, idx, voice->fmt);
            s = (s * (int32_t)voice->volume) / 255;

            /* mono -> estereo, acumula com clamp */
            int32_t l = (int32_t)out[f * 2] + s;
            int32_t r = (int32_t)out[f * 2 + 1] + s;
            if (l > 32767) l = 32767;
            if (l < -32768) l = -32768;
            if (r > 32767) r = 32767;
            if (r < -32768) r = -32768;
            out[f * 2]     = (int16_t)l;
            out[f * 2 + 1] = (int16_t)r;

            voice->pos_fp += voice->step_fp;
        }
    }
}
