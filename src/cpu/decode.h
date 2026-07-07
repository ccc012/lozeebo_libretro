/* decode.h - Helpers compartilhados de decodificacao (barrel shifter) */
#ifndef ZEEBO_DECODE_H
#define ZEEBO_DECODE_H

#include <stdint.h>
#include <stdbool.h>

/* Tipos de shift do ARM */
#define ZSHIFT_LSL 0
#define ZSHIFT_LSR 1
#define ZSHIFT_ASR 2
#define ZSHIFT_ROR 3

/* Barrel shifter.
 * value  : valor a deslocar
 * type   : ZSHIFT_*
 * amount : quantidade (0-255)
 * carry  : entra com o C atual, sai com o carry-out do shifter
 * reg_shift: true se a quantidade veio de registrador (muda semantica
 *            dos casos amount==0 e amount>=32, conforme o manual ARM)
 */
uint32_t zshift(uint32_t value, int type, uint32_t amount, bool *carry,
                bool reg_shift);

#endif /* ZEEBO_DECODE_H */
