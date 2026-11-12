/* pakz.h - leitor do container "PACK" (.pakz) do Zeebo.
 *
 * Formato (little-endian):
 *   +0x00  char     magic[4] = "PACK"
 *   +0x04  uint32   toc_off   (offset da tabela de conteudo)
 *   +0x08  uint32   toc_size  (tamanho da tabela, multiplo de 64)
 * A tabela fica em toc_off, com toc_size/64 entradas de 64 bytes:
 *   +0x00  char     name[56]  (relativo, NUL-padded)
 *   +0x38  uint32   offset    (relativo ao inicio do arquivo)
 *   +0x3C  uint32   size
 *
 * Ex.: Zeebo Extreme Baja/Corrida Aerea/Jetboard/Boia usam
 * mod/<id>/pak0.pakz com audio/music/*.mp3, audio/xui/*.wav, texture/*.qxt.
 */
#ifndef ZEEBO_PAKZ_H
#define ZEEBO_PAKZ_H

#include <stdint.h>
#include <stdio.h>

typedef struct pakz pakz_t;

/* Abre e valida o arquivo. Retorna NULL se nao for um PACK valido. */
pakz_t *pakz_open(const char *path);
void pakz_close(pakz_t *p);

int pakz_count(const pakz_t *p);
FILE *pakz_host(const pakz_t *p);

/* Busca por nome relativo (aceita "./", "\\", "fs:/" e ignora caixa).
 * Retorna indice >= 0 ou -1. */
int pakz_find(const pakz_t *p, const char *name);

/* Metadados do membro idx. Retorna 1 em sucesso, 0 se invalido. */
int pakz_member(const pakz_t *p, int idx, uint32_t *off, uint32_t *size);

/* Le len bytes do membro a partir de member_off. Retorna os bytes lidos. */
uint32_t pakz_read(const pakz_t *p, int idx, uint32_t member_off,
                   void *dst, uint32_t len);

#endif /* ZEEBO_PAKZ_H */
