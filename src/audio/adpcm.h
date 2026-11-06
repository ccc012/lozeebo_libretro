/* adpcm.h - IMA ADPCM decoder */
#ifndef ZEEBO_ADPCM_H
#define ZEEBO_ADPCM_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declaration - zvoice_t is defined in audio_internal.h */
struct zvoice_t;

struct zimad_state {
    uint32_t addr;            /* endereco dos dados ADPCM */
    uint32_t bytes_total;     /* tamanho total */
    uint32_t block_align;     /* bytes por bloco (ex: 256) */
    uint32_t sample_rate;     /* sample rate original */
    
    /* Estado do decoder */
    int16_t predictor;        /* predictor atual */
    int8_t step_index;        /* indice da tabela de passos */
    uint32_t block_pos;       /* posicao dentro do bloco atual */
    uint32_t total_samples_decoded; /* samples decodificados total */
    bool     eof;             /* fim do stream */

    uint32_t samples_per_block; /* samples por bloco (0 = padrao 1024) */
    
    /* Buffer de saida do bloco atual */
    int16_t block_samples[1024]; /* max samples per block (block_align * 2) */
    uint32_t block_samples_count;
    uint32_t block_samples_pos;
};

int zimad_render(struct zvoice_t *voice, int16_t *out, uint32_t frames);
int zimad_next(struct zimad_state *state, int16_t *out);
void zimad_set_stream(struct zimad_state *state, uint32_t addr, uint32_t bytes,
                     uint16_t block_align, uint16_t samples_per_block);
void zimad_reset(struct zimad_state *state);

#endif /* ZEEBO_ADPCM_H */