/* audio_internal.h - Estado interno das vozes (audio.c + mixer.c) */
#ifndef ZEEBO_AUDIO_INTERNAL_H
#define ZEEBO_AUDIO_INTERNAL_H

#include "audio.h"

typedef struct zvoice_t {
    bool     active;
    uint32_t addr;        /* endereco emulado dos samples */
    uint32_t samples;     /* total de samples */
    uint32_t rate;        /* sample rate original */
    uint8_t  channels;    /* 1=mono 2=estereo intercalado */
    enum zpcm_format fmt;
    bool     loop;
    uint8_t  volume;      /* 0-255 */
    uint64_t pos_fp;      /* posicao em ponto fixo 32.32 */
    uint64_t step_fp;     /* incremento por frame de saida */
    bool     paused;      /* pausado (IAudio control) */
    bool     muted;       /* mute explícito */
    void    *adpcm;       /* decoder ADPCM (zimad_state) quando fmt=ZPCM_ADPCM */
    int16_t  adpcm_sample; /* sample retido entre frames do resampler */
    int8_t   pan;         /* -128..+127, 0 = centro */
} zvoice_t;

zvoice_t *zaudio_voice_internal(int i);

#endif /* ZEEBO_AUDIO_INTERNAL_H */
