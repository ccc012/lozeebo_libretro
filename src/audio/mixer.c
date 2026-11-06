/* mixer.c - Mixagem das vozes para o buffer de saida do RetroArch */
#include <string.h>
#include "audio_internal.h"
#include "../audio/adpcm.h"
#include "../memory/memory.h"
#include "../debug/log.h"

void zaudio_render(int16_t *out, uint32_t frames) {
    uint32_t f;
    int v;

    memset(out, 0, frames * 2 * sizeof(int16_t));

    for (v = 0; v < ZAUDIO_MAX_VOICES; v++) {
        zvoice_t *voice = zaudio_voice_internal(v);
        if (!voice->active || voice->paused) continue;

        /* pan -128..+127 -> fatores L/R 0..255 */
        int pr = (voice->pan < 0) ? 128 : (128 - voice->pan);
        int pl = (voice->pan > 0) ? 128 : (128 + voice->pan);
        const int stereo = voice->channels >= 2;

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
            int32_t sl, sr;
            if (voice->fmt == ZPCM_ADPCM) {
                /* ADPCM: decode via state machine de blocos (portado do Zeemu) */
                struct zimad_state *st = voice->adpcm;
                int16_t s = 0;
                bool ok = false;
                if (!st) { voice->active = false; break; }
                if (st->eof && voice->loop) zimad_reset(st);
                if (zimad_next(st, &s)) ok = true;
                else if (voice->loop) { zimad_reset(st); if (zimad_next(st, &s)) ok = true; }
                if (!ok) { voice->active = false; break; }
                sl = sr = s;
            }
            else if (stereo) {
                sl = zpcm_decode_sample(voice->addr, idx * 2, voice->fmt);
                sr = zpcm_decode_sample(voice->addr, idx * 2 + 1, voice->fmt);
            } else {
                sl = sr = zpcm_decode_sample(voice->addr, idx, voice->fmt);
            }
            if (voice->muted) {
                sl = 0;
                sr = 0;
            } else {
                sl = (sl * (int32_t)voice->volume) / 255;
                sr = (sr * (int32_t)voice->volume) / 255;
            }

            /* pan + volume, acumula com clamp */
            int32_t l = (int32_t)out[f * 2]     + (sl * pl) / 128;
            int32_t r = (int32_t)out[f * 2 + 1] + (sr * pr) / 128;
            if (l > 32767) l = 32767;
            if (l < -32768) l = -32768;
            if (r > 32767) r = 32767;
            if (r < -32768) r = -32768;
            out[f * 2]     = (int16_t)l;
            out[f * 2 + 1] = (int16_t)r;
        }
    }
}
