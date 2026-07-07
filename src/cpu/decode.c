/* decode.c - Barrel shifter do ARM (compartilhado entre ARM e Thumb) */
#include "decode.h"

uint32_t zshift(uint32_t value, int type, uint32_t amount, bool *carry,
                bool reg_shift) {
    switch (type) {
    case ZSHIFT_LSL:
        if (amount == 0) return value;              /* carry inalterado */
        if (amount < 32) {
            *carry = (value >> (32 - amount)) & 1;
            return value << amount;
        }
        if (amount == 32) { *carry = value & 1; return 0; }
        *carry = false; return 0;

    case ZSHIFT_LSR:
        if (amount == 0) {
            if (reg_shift) return value;            /* reg: sem efeito */
            amount = 32;                            /* imm: LSR #32 */
        }
        if (amount < 32) {
            *carry = (value >> (amount - 1)) & 1;
            return value >> amount;
        }
        if (amount == 32) { *carry = (value >> 31) & 1; return 0; }
        *carry = false; return 0;

    case ZSHIFT_ASR:
        if (amount == 0) {
            if (reg_shift) return value;
            amount = 32;                            /* imm: ASR #32 */
        }
        if (amount < 32) {
            *carry = (value >> (amount - 1)) & 1;
            return (uint32_t)((int32_t)value >> amount);
        }
        *carry = (value >> 31) & 1;
        return (value & 0x80000000u) ? 0xFFFFFFFFu : 0;

    case ZSHIFT_ROR:
        if (amount == 0) {
            if (reg_shift) return value;
            /* imm ROR #0 = RRX: rotaciona 1 bit atraves do carry */
            {
                bool old_c = *carry;
                *carry = value & 1;
                return (value >> 1) | (old_c ? 0x80000000u : 0);
            }
        }
        amount &= 31;
        if (amount == 0) { *carry = (value >> 31) & 1; return value; }
        *carry = (value >> (amount - 1)) & 1;
        return (value >> amount) | (value << (32 - amount));
    }
    return value;
}
