/* audio_internal.h - Estado interno das vozes (audio.c + mixer.c) */
#ifndef ZEEBO_AUDIO_INTERNAL_H
#define ZEEBO_AUDIO_INTERNAL_H

#include "audio.h"

typedef struct {
    bool     active;
    uint32_t addr;        /* endereco emulado dos samples */
    uint32_t samples;     /* total de samples */
    uint32_t rate;        /* sample rate original */
    enum zpcm_format fmt;
    bool     loop;
    uint8_t  volume;      /* 0-255 */
    uint64_t pos_fp;      /* posicao em ponto fixo 32.32 */
    uint64_t step_fp;     /* incremento por frame de saida */
} zvoice_t;

zvoice_t *zaudio_voice_internal(int i);

#endif /* ZEEBO_AUDIO_INTERNAL_H */
