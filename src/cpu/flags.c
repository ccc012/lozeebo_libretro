/* flags.c - Condicoes e flags do CPSR */
#include "cpu.h"

bool zcpu_cond_pass(uint32_t cond) {
    /* NZCV empacotado em nibble: bit3=N bit2=Z bit1=C bit0=V.
     * Lookup 16x16: linha=cond, coluna=flags. AL/NV sempre 1. */
    static const uint16_t k_cond[16] = {
        /* EQ */ 0xF0F0u, /* Z */
        /* NE */ 0x0F0Fu, /* !Z */
        /* CS */ 0xCCCCu, /* C */
        /* CC */ 0x3333u, /* !C */
        /* MI */ 0xFF00u, /* N */
        /* PL */ 0x00FFu, /* !N */
        /* VS */ 0xAAAAu, /* V */
        /* VC */ 0x5555u, /* !V */
        /* HI */ 0x0C0Cu, /* C && !Z */
        /* LS */ 0xF3F3u, /* !C || Z */
        /* GE */ 0xAA55u, /* N == V */
        /* LT */ 0x55AAu, /* N != V */
        /* GT */ 0x0A05u, /* !Z && N==V */
        /* LE */ 0xF5FAu, /* Z || N!=V */
        /* AL */ 0xFFFFu,
        /* NV */ 0xFFFFu
    };
    uint32_t flags = (g_cpu.cpsr >> 28) & 0xFu;
    return (k_cond[cond & 0xFu] >> flags) & 1u;
}

void zflags_nz(uint32_t result) {
    g_cpu.cpsr &= ~(CPSR_N | CPSR_Z);
    if (result & 0x80000000u) g_cpu.cpsr |= CPSR_N;
    if (result == 0)          g_cpu.cpsr |= CPSR_Z;
}

uint32_t zflags_add(uint32_t a, uint32_t b, uint32_t carry_in, bool set_flags) {
    uint64_t wide = (uint64_t)a + (uint64_t)b + (uint64_t)carry_in;
    uint32_t result = (uint32_t)wide;
    if (set_flags) {
        g_cpu.cpsr &= ~(CPSR_N | CPSR_Z | CPSR_C | CPSR_V);
        if (result & 0x80000000u) g_cpu.cpsr |= CPSR_N;
        if (result == 0)          g_cpu.cpsr |= CPSR_Z;
        if (wide >> 32)           g_cpu.cpsr |= CPSR_C;
        /* overflow: operandos com mesmo sinal, resultado com sinal diferente */
        if (~(a ^ b) & (a ^ result) & 0x80000000u) g_cpu.cpsr |= CPSR_V;
    }
    return result;
}

uint32_t zflags_sub(uint32_t a, uint32_t b, uint32_t carry_in, bool set_flags) {
    /* a - b - (1 - carry_in)  ==  a + ~b + carry_in */
    uint64_t wide = (uint64_t)a + (uint64_t)(~b) + (uint64_t)carry_in;
    uint32_t result = (uint32_t)wide;
    if (set_flags) {
        g_cpu.cpsr &= ~(CPSR_N | CPSR_Z | CPSR_C | CPSR_V);
        if (result & 0x80000000u) g_cpu.cpsr |= CPSR_N;
        if (result == 0)          g_cpu.cpsr |= CPSR_Z;
        if (wide >> 32)           g_cpu.cpsr |= CPSR_C; /* C=1: sem borrow */
        if ((a ^ b) & (a ^ result) & 0x80000000u) g_cpu.cpsr |= CPSR_V;
    }
    return result;
}
