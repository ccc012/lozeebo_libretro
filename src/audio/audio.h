/* audio.h - Mixer de audio 44.1kHz estereo 16-bit */
#ifndef ZEEBO_AUDIO_H
#define ZEEBO_AUDIO_H

#include <stdint.h>
#include <stdbool.h>

#define ZAUDIO_SAMPLE_RATE 44100
#define ZAUDIO_MAX_VOICES  16

/* Formatos de sample suportados pelo mixer */
enum zpcm_format {
    ZPCM_U8   = 0,   /* 8-bit unsigned mono  */
    ZPCM_S16  = 1    /* 16-bit signed mono (little-endian) */
};

void zaudio_init(void);
void zaudio_reset(void);

/* Toca PCM que esta na memoria emulada.
 * addr: endereco emulado dos samples
 * bytes: tamanho em bytes
 * rate: sample rate original (ex: 8000)
 * Retorna id da voz (>=0) ou -1 se sem voz livre. */
int  zaudio_play_pcm(uint32_t addr, uint32_t bytes, uint32_t rate,
                     enum zpcm_format fmt, bool loop, uint8_t volume);
void zaudio_stop(int voice);
void zaudio_stop_all(void);
bool zaudio_voice_active(int voice);
void zaudio_set_volume(int voice, uint8_t volume);

/* Renderiza 'frames' amostras estereo intercaladas (L,R) no buffer. */
void zaudio_render(int16_t *out, uint32_t frames);

/* Decodificacao de um sample individual (usado pelo mixer) */
int16_t zpcm_decode_sample(uint32_t addr, uint32_t index, enum zpcm_format fmt);
uint32_t zpcm_sample_count(uint32_t bytes, enum zpcm_format fmt);

#endif /* ZEEBO_AUDIO_H */
