/* memory.h - Mapa de memoria HLE do Zeebo
 *
 * Mapa (ver docs 09_MAPEAMENTO_MEMORIAS.md):
 *   0x00000000 - 0x00000FFF : Vetores/reservado
 *   0x00001000 - ...        : Codigo do jogo (RAM principal)
 *   0x01000000 - ...        : Dados do jogo (mesma RAM principal)
 *   0x10000000 - 0x11FFFFFF : Heap (MALLOC/FREE)
 *   0x2FC00000 - 0x2FFFFFFF : Stack (cresce para baixo, SP inicia no topo)
 *   0x30000000 - 0x303FFFFF : VRAM/Framebuffer
 *   0xF0000000 - 0xFFFFFFFF : Regiao HLE (trap de APIs, sem memoria real)
 */
#ifndef ZEEBO_MEMORY_H
#define ZEEBO_MEMORY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Limites das regioes */
#define ZMEM_RAM_BASE      0x00000000u
#define ZMEM_RAM_SIZE      (64u * 1024u * 1024u)   /* 64MB: codigo + dados */
#define ZMEM_CODE_ENTRY    0x00001000u             /* entry point tipico  */

#define ZMEM_HEAP_BASE     0x10000000u
#define ZMEM_HEAP_SIZE     (32u * 1024u * 1024u)   /* 32MB de heap */

/* Janela extra apos o heap (0x12000000..0x21FFFFFF, 256MB).
 * Alguns jogos (ex: Alice/TTDMM) varrem linearmente a partir de
 * 0x12000000 (heap_base + heap_size). Com janela pequena (32MB) o mesmo
 * acesso invalido apenas desloca para o fim da janela (ex: 0x14000000);
 * com 256MB o scan atravessa ate 0x22000000. Esta janela e memoria real
 * (zerada) SEM ser usada pelo alocador: nao desloca nenhum endereco do
 * heap (heap continua 0x10000000..0x11FFFFFF) e nao muda
 * GetRAMFree/first-fit dos outros jogos. 256MB e sob demanda (calloc).
 * NOTA: reads/writes em buracos do mapa (0x04000000..0x0FFFFFFF e alem
 * da guard ate a stack) sao silenciosos — o scan de Alice/TTDMM le e
 * escreve zeros atraves desses buracos byte a byte; logar geraria spam
 * infinito e travava o smoke. */
#define ZMEM_GUARD_BASE    0x12000000u
#define ZMEM_GUARD_SIZE    (256u * 1024u * 1024u)

#define ZMEM_STACK_BASE    0x2FC00000u
#define ZMEM_STACK_SIZE    (4u * 1024u * 1024u)    /* 4MB de stack */
#define ZMEM_STACK_TOP     0x2FFFFFF0u             /* SP inicial */

#define ZMEM_VRAM_BASE     0x30000000u
#define ZMEM_VRAM_SIZE     (4u * 1024u * 1024u)    /* Family Pack usa o segundo banco em 0x30200000 */

#define ZMEM_HLE_BASE      0xF0000000u             /* trap de APIs BREW */
/* Fim real da janela de traps: nenhum vtable/stub deste emulador jamais
 * escreve um endereco de trap alem de ZTRAP_ADDR(ZT_MAX) (brew.h), ou
 * seja 0xF0000000+0x900*4 = 0xF0002400. Mantido aqui (em vez de incluir
 * brew.h) para nao criar dependencia circular; se ZT_MAX mudar, atualize
 * este valor tambem. PCs >= ZMEM_HLE_BASE mas fora desta janela nunca
 * podem ser um alvo de branch legitimo - sao PC corrompido e devem cair
 * no cheque de "CPU descarrilou", nao ser mascarados como chamada de API
 * nao implementada (ver zcpu_step em cpu.c). */
#define ZMEM_HLE_END       0xF0002400u

bool  zmem_init(void);
void  zmem_shutdown(void);
void  zmem_reset(void);

/* Acesso (little-endian). Nunca trava: fora dos limites -> log + 0. */
uint8_t  zmem_read8(uint32_t addr);
uint16_t zmem_read16(uint32_t addr);
uint32_t zmem_read32(uint32_t addr);
void     zmem_write8(uint32_t addr, uint8_t val);
void     zmem_write16(uint32_t addr, uint16_t val);
void     zmem_write32(uint32_t addr, uint32_t val);

/* Acesso em bloco (para loader, MEMCPY HLE, strings de argumentos) */
bool zmem_read_block(uint32_t addr, void *dst, uint32_t len);
bool zmem_write_block(uint32_t addr, const void *src, uint32_t len);

/* Le string C terminada em zero da memoria emulada (max maxlen-1 chars) */
void zmem_read_cstr(uint32_t addr, char *dst, uint32_t maxlen);

/* Ponteiro direto para uma regiao (NULL se invalido). Uso interno
 * do framebuffer/loader para evitar copia pixel a pixel. */
void *zmem_host_ptr(uint32_t addr, uint32_t len);
uint8_t *zmem_ram_ptr(void);
uint8_t *zmem_heap_ptr(void);
uint8_t *zmem_stack_ptr(void);
uint8_t *zmem_vram_ptr(void);
const uint8_t *zmem_ram_ptr_const(void);
const uint8_t *zmem_heap_ptr_const(void);
const uint8_t *zmem_stack_ptr_const(void);
const uint8_t *zmem_vram_ptr_const(void);
const uint8_t *zmem_highpage_ptr_const(void);
uint8_t *zmem_highpage_ptr(void);

uint8_t *zmem_guard_ptr(void);
const uint8_t *zmem_guard_ptr_const(void);
/* ---- Heap (alocador de blocos, usado pelo MALLOC/FREE do BREW) ----
 * size_and_flags: bit 31 = ZERO (MALLOC_ZERO / realloc zero-extend);
 * bits 30..0 = tamanho. Nunca passar size bruto >= 0x80000000 ao first-fit. */
void     zheap_reset(void);
uint32_t zheap_alloc(uint32_t size_and_flags);  /* retorna endereco emulado ou 0 */
void     zheap_free(uint32_t addr);
uint32_t zheap_realloc(uint32_t addr, uint32_t size_and_flags);
uint32_t zheap_block_size(uint32_t addr);       /* tamanho utilizavel do bloco */
uint32_t zheap_used(void);                      /* bytes em uso (estatistica) */

#endif /* ZEEBO_MEMORY_H */
