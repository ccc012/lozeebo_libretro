#ifndef BREW_IIMAGE_H
#define BREW_IIMAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* =========================================================================
 * IImage (BREW image decoder / renderer)
 * Portado da logica do zeemu BrewImage.cpp (GPLv3).
 * Nao copia C++ verbatim - reimplementa a semantica em C99.
 *
 * Vtable slots (11 metodos):
 *   0 AddRef, 1 Release, 2 Draw, 3 DrawFrame, 4 GetInfo,
 *   5 SetParm, 6 Start, 7 Stop, 8 SetStream, 9 HandleEvent, 10 Notify
 *
 * ROP valores:
 *   2 = Copy, 7 = Transparent (magenta 0xFF00FF), 9 = Blend (alpha)
 * ========================================================================= */

/* Params de IImage_SetParm */
#define IIMAGE_PARM_SIZE      0x00  /* cx, cy */
#define IIMAGE_PARM_OFFSET    0x01  /* ox, oy */
#define IIMAGE_PARM_CXFRAME   0x02  /* frame width */
#define IIMAGE_PARM_ROP       0x03  /* rop value */
#define IIMAGE_PARM_NFRAMES   0x04  /* frame count */
#define IIMAGE_PARM_OFFSCREEN 0x06  /* offscreen flag */
#define IIMAGE_PARM_SCALE     0x0C  /* scale cx, cy */

/* ROP */
#define IIMAGE_ROP_COPY      2u
#define IIMAGE_ROP_TRANSPARENT 7u   /* magenta 0xFF00FF + preto puro */
#define IIMAGE_ROP_BLEND     9u     /* alpha blend */

/* CLSIDs IImage (zeemu BrewShell) — NAO 0x0100100C (isso e MemAStream). */
#define AEECLSID_WINBMP     0x01004001u
#define AEECLSID_NATIVEBMP  0x01004002u
#define AEECLSID_CPNG       0x01004004u
#define AEECLSID_CPNGBREW   0x01030765u
#define AEECLSID_IMAGE      AEECLSID_CPNG

/* Cria uma interface IImage a partir de um buffer de dados codificados
 * (PNG/JPEG/BMP/GIF/TGA). Retorna ponteiro do objeto ou 0 em falha. */
uint32_t zimage_create_from_buffer(uint32_t clsid, const uint8_t *data,
                                   size_t size, const char *label);

/* Cria uma IImage carregando de um caminho de arquivo relativo ao .mod */
uint32_t zimage_create_from_file(uint32_t clsid, const char *path);

/* Factory chamada por real_create_instance para viewers IImage */
uint32_t zimage_create(uint32_t clsid);

/* IImageDecoder + IForceFeed (PNGDecoderBREW / JPEGDecoder) — zeemu BrewImageDecoder */
uint32_t zimage_decoder_create(uint32_t clsid);
void     zimage_decoder_handle(uint32_t id);
void     zimage_forcefeed_handle(uint32_t id);

/* IMemAStream (CLSID 0x0100100C) */
#define AEECLSID_MEMASTREAM 0x0100100Cu
uint32_t zmemastream_create(void);
void     zmemastream_handle(uint32_t id);
/* Le bytes restantes do stream a partir da posicao atual. Caller free(). */
uint8_t *zmemastream_read_remaining(uint32_t stream_obj, size_t *out_len);

void zimage_handle(uint32_t id);

#endif /* BREW_IIMAGE_H */
