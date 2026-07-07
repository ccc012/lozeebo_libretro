/* pcm.c - Decodificacao de samples PCM da memoria emulada */
#include "audio.h"
#include "../memory/memory.h"

uint32_t zpcm_sample_count(uint32_t bytes, enum zpcm_format fmt) {
    switch (fmt) {
        case ZPCM_U8:  return bytes;
        case ZPCM_S16: return bytes / 2;
    }
    return 0;
}

int16_t zpcm_decode_sample(uint32_t addr, uint32_t index, enum zpcm_format fmt) {
    switch (fmt) {
        case ZPCM_U8: {
            /* 8-bit unsigned -> 16-bit signed */
            uint8_t s = zmem_read8(addr + index);
            return (int16_t)(((int32_t)s - 128) << 8);
        }
        case ZPCM_S16:
            return (int16_t)zmem_read16(addr + index * 2);
    }
    return 0;
}
