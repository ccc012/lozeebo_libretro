/* pcm.c - Decodificacao de samples PCM/ADPCM da memoria emulada */
#include "audio.h"
#include "../memory/memory.h"

/* ---- IMA-ADPCM step + index tables ---- */
static const int g_ima_step[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,
    88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,
    544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,
    2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,
    10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767
};
static const int g_ima_index[8] = { -1,-1,-1,-1, 2, 4, 6, 8 };

uint32_t zpcm_sample_count(uint32_t bytes, enum zpcm_format fmt) {
    switch (fmt) {
        case ZPCM_U8:    return bytes;
        case ZPCM_S16:   return bytes / 2;
        case ZPCM_ADPCM: return bytes < 4 ? 0 : ((bytes - 4) * 2 + 1);
    }
    return 0;
}

int16_t zpcm_decode_sample(uint32_t addr, uint32_t index, enum zpcm_format fmt) {
    switch (fmt) {
        case ZPCM_U8: {
            uint8_t s = zmem_read8(addr + index);
            return (int16_t)(((int32_t)s - 128) << 8);
        }
        case ZPCM_S16:
            return (int16_t)zmem_read16(addr + index * 2);
        case ZPCM_ADPCM: {
            if (index == 0) return (int16_t)zmem_read16(addr);
            int pred = (int)(int16_t)zmem_read16(addr);
            int idx_table = (int)(uint8_t)zmem_read16(addr + 2);
            if (idx_table > 88) idx_table = 88;
            uint32_t data_off = addr + 4 + ((index - 1) / 2);
            uint8_t byte = zmem_read8(data_off);
            int nibble = ((index - 1) & 1) ? (byte >> 4) : (byte & 0x0F);
            int step = g_ima_step[idx_table];
            int diff = step >> 3;
            if (nibble & 4) diff += step;
            if (nibble & 2) diff += step >> 1;
            if (nibble & 1) diff += step >> 2;
            if (nibble & 8) pred -= diff; else pred += diff;
            if (pred > 32767) pred = 32767;
            if (pred < -32768) pred = -32768;
            idx_table += g_ima_index[nibble & 7];
            if (idx_table < 0) idx_table = 0;
            if (idx_table > 88) idx_table = 88;
            return (int16_t)pred;
        }
    }
    return 0;
}
