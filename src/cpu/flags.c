/* flags.c - Condicoes e flags do CPSR */
#include "cpu.h"

bool zcpu_cond_pass(uint32_t cond) {
    uint32_t c = g_cpu.cpsr;
    bool n = (c & CPSR_N) != 0;
    bool z = (c & CPSR_Z) != 0;
    bool cf = (c & CPSR_C) != 0;
    bool v = (c & CPSR_V) != 0;

    switch (cond & 0xF) {
        case 0x0: return z;              /* EQ */
        case 0x1: return !z;             /* NE */
        case 0x2: return cf;             /* CS/HS */
        case 0x3: return !cf;            /* CC/LO */
        case 0x4: return n;              /* MI */
        case 0x5: return !n;             /* PL */
        case 0x6: return v;              /* VS */
        case 0x7: return !v;             /* VC */
        case 0x8: return cf && !z;       /* HI */
        case 0x9: return !cf || z;       /* LS */
        case 0xA: return n == v;         /* GE */
        case 0xB: return n != v;         /* LT */
        case 0xC: return !z && (n == v); /* GT */
        case 0xD: return z || (n != v);  /* LE */
        case 0xE: return true;           /* AL */
        case 0xF: return true;           /* NV - tratado como especial fora */
    }
    return true;
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
