/* imedia.h - IMedia internals header */
#ifndef ZEEBO_IMEDIA_H
#define ZEEBO_IMEDIA_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
struct zimad_state;

/* Estados de playback (AEEIMedia.h MM_STATE_*) */
#define ZMEDIA_STATE_IDLE       1
#define ZMEDIA_STATE_READY      2
#define ZMEDIA_STATE_PLAY       3
#define ZMEDIA_STATE_PLAY_PAUSE 5

/* Estado MIDI — renderer TinySoundFont (header-only, estado em zmidi.c) */
struct zmidi_state {
    void *tsf;              /* renderer MIDI ativo (NULL = sem soundfont) */
    uint8_t  volume;        /* volume geral MIDI 0-255 */
    uint8_t  chan_vol[16];  /* volume por canal 0-127 */
    uint8_t  chan_prog[16]; /* program/preset por canal */
    uint16_t chan_bend[16]; /* pitch bend por canal (0-16383, centro 8192) */
    bool     chan_mute[16]; /* mute por canal */
};

typedef struct {
    bool     active;
    uint32_t clsid;
    uint32_t obj;      /* endereco emulado do objeto IMedia */
    uint32_t addr;
    uint32_t bytes;
    uint32_t rate;
    uint32_t state;
    uint8_t  channels;   /* 1 = mono, 2 = estereo (intercalado) */
    uint8_t  fmt;        /* enum zpcm_format */
    int      voice;
    uint8_t  volume;
    bool     loop;
    bool     is_mp3;     /* payload com assinatura MP3 (ID3/MPEG sync) */
    uint32_t notify_cb;
    uint32_t notify_user;
    bool     notify_enabled;
    uint32_t notify_buf; /* AEEMediaCmdNotify no heap emulado */
    uint32_t time_ms;    /* duracao (MIDI/MP3) */

    struct zimad_state *adpcm_state;
    struct zmidi_state *midi_state;
    void *mp3_state;
    int      mp3_stream; /* id do stream dr_mp3 (-1 = nenhum) */
    uint32_t mp3_offs;   /* offset de leitura no stream */
    uint16_t block_align;     /* bytes por bloco (ADPCM, do WAV fmt) */
    uint16_t samples_per_block; /* samples por bloco (ADPCM) */
} zmedia_t;

zmedia_t *media_from_obj(uint32_t obj);

/* Factories chamadas pelo IMediaUtil / IShell_CreateInstance */
uint32_t zbrew_create_media_pcm(void);
uint32_t zbrew_create_media_midi(void);
uint32_t zbrew_create_media_mp3(void);
uint32_t zbrew_create_media_adpcm(void);
uint32_t zbrew_create_media_util(void);
uint32_t zbrew_create_source_util(void);
uint32_t zsource_create_from_file(const char *path);
uint32_t zsource_create_from_buffer(uint32_t addr, uint32_t len);

#endif /* ZEEBO_IMEDIA_H */