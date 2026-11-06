/* adpcm.c - IMA ADPCM decoder implementation */
#include <string.h>
#include "audio_internal.h"
#include "adpcm.h"
#include "../memory/memory.h"

/* Tabela de passos IMA ADPCM */
static const int16_t ima_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

/* Tabela de ajuste do step index */
static const int8_t ima_index_table[8] = {
    -1, -1, -1, -1, 2, 4, 6, 8
};

/* Decodifica um nibble IMA ADPCM */
static inline int16_t ima_decode_nibble(struct zimad_state *state, uint8_t nibble) {
    int16_t step = ima_step_table[state->step_index];
    int32_t diff = step >> 3;
    
    if (nibble & 4) diff += step;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 1) diff += step >> 2;
    
    if (nibble & 8) state->predictor -= diff;
    else state->predictor += diff;
    
    /* Clamp predictor */
    if (state->predictor > 32767) state->predictor = 32767;
    if (state->predictor < -32768) state->predictor = -32768;
    
    /* Atualiza step index */
    state->step_index += ima_index_table[nibble & 7];
    if (state->step_index < 0) state->step_index = 0;
    if (state->step_index > 88) state->step_index = 88;
    
    return state->predictor;
}

/* Carrega e decodifica o proximo bloco ADPCM */
static bool zimad_load_block(struct zimad_state *state) {
    uint32_t bytes_remaining = state->bytes_total - state->block_pos;
    if (bytes_remaining < 4) {
        state->eof = true;
        return false;
    }

    uint32_t block_size = state->block_align;
    if (!block_size) {
        /* Sem block_align informado: deriva do samples_per_block (padrao 1024) */
        uint32_t spb = state->samples_per_block ? state->samples_per_block : 1024;
        block_size = 4 + ((spb + 1) / 2); /* header(4) + ceil((spb-1)/2) bytes de nibbles */
    }
    if (bytes_remaining < block_size) block_size = bytes_remaining;

    if (block_size < 4) {
        state->eof = true;
        return false;
    }

    uint32_t block_addr = state->addr + state->block_pos;
    int predictor = (int)(int16_t)zmem_read16(block_addr);
    int step_index = (int)(int8_t)zmem_read8(block_addr + 2);
    if (step_index < 0) step_index = 0;
    if (step_index > 88) step_index = 88;

    uint32_t block_end = state->addr + state->block_pos + block_size;
    uint32_t data_end = state->addr + state->bytes_total;
    uint32_t eff_end = (block_end < data_end) ? block_end : data_end;

    state->predictor = (int16_t)predictor;
    state->step_index = (int8_t)step_index;
    state->block_pos += block_size;
    state->block_samples_count = 0;
    state->block_samples_pos = 0;

    /* Primeiro sample do bloco e o predictor; nibbles a partir de block_addr+4 */
    uint32_t samples_in_block = 1;
    state->block_samples[0] = state->predictor;
    state->block_samples_count = 1;

    for (uint32_t off = block_addr + 4; off < eff_end; ++off) {
        uint8_t packed = zmem_read8(off);
        int16_t s = ima_decode_nibble(state, packed & 0x0F);
        state->block_samples[state->block_samples_count++] = s;
        samples_in_block++;
        if (state->samples_per_block && samples_in_block >= (uint32_t)state->samples_per_block) break;
        s = ima_decode_nibble(state, (packed >> 4) & 0x0F);
        state->block_samples[state->block_samples_count++] = s;
        samples_in_block++;
        if (state->samples_per_block && samples_in_block >= (uint32_t)state->samples_per_block) break;
    }

    return true;
}

int zimad_next(struct zimad_state *state, int16_t *out) {
    if (!state || !out || state->eof) return 0;
    if (state->block_samples_pos >= state->block_samples_count) {
        if (!zimad_load_block(state)) return 0;
    }
    *out = state->block_samples[state->block_samples_pos++];
    state->total_samples_decoded++;
    return 1;
}

void zimad_set_stream(struct zimad_state *state, uint32_t addr, uint32_t bytes,
                     uint16_t block_align, uint16_t samples_per_block) {
    if (!state) return;
    state->addr = addr;
    state->bytes_total = bytes;
    state->block_align = block_align;
    state->samples_per_block = samples_per_block;
    zimad_reset(state);
}

int zimad_render(struct zvoice_t *voice, int16_t *out, uint32_t frames) {
    struct zimad_state *state = voice->adpcm;
    if (!state || state->eof) return 0;
    
    uint32_t rendered = 0;
    int32_t volume = voice->volume;
    int8_t pan = voice->pan;
    
    for (uint32_t f = 0; f < frames; f++) {
        /* Se buffer do bloco acabou, carrega proximo */
        if (state->block_samples_pos >= state->block_samples_count) {
            if (!zimad_load_block(state)) {
                /* EOF */
                return rendered;
            }
        }
        
        int32_t s = state->block_samples[state->block_samples_pos++];
        state->total_samples_decoded++;
        
        /* Aplica volume */
        s = (s * volume) / 255;
        
        /* Aplica pan */
        int32_t l = s, r = s;
        if (pan < 0) {
            r = (r * (128 + pan)) / 128;
        } else if (pan > 0) {
            l = (l * (128 - pan)) / 128;
        }
        
        /* Mix estereo com clamp */
        int32_t ol = (int32_t)out[f * 2] + l;
        int32_t or = (int32_t)out[f * 2 + 1] + r;
        if (ol > 32767) ol = 32767;
        if (ol < -32768) ol = -32768;
        if (or > 32767) or = 32767;
        if (or < -32768) or = -32768;
        out[f * 2]     = (int16_t)ol;
        out[f * 2 + 1] = (int16_t)or;
        
        rendered++;
        
        /* Resample se necessario */
        if (voice->step_fp != (1ULL << 32)) {
            /* Para ADPCM, o step_fp ja contem a razao rate/44100 */
            /* O decoder roda no sample rate original, entao precisamos pular samples */
            uint64_t extra_steps = voice->step_fp - (1ULL << 32);
            if (extra_steps > 0) {
                /* Pula samples proporcionalmente */
                uint32_t skip = (uint32_t)((extra_steps * state->block_samples_pos) >> 32);
                if (skip > 0 && state->block_samples_pos + skip < state->block_samples_count) {
                    state->block_samples_pos += skip;
                    state->total_samples_decoded += skip;
                }
            }
        }
    }
    
    return rendered;
}

void zimad_reset(struct zimad_state *state) {
    if (!state) return;
    state->block_pos = 0;
    state->total_samples_decoded = 0;
    state->eof = false;
    state->block_samples_pos = 0;
    state->block_samples_count = 0;
    state->predictor = 0;
    state->step_index = 0;
}