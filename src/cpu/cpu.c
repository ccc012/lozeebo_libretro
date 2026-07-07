/* cpu.c - Loop fetch-decode-execute e controle da CPU */
#include <string.h>
#include "cpu.h"
#include "../memory/memory.h"
#include "../debug/log.h"

zcpu_t g_cpu;
bool zcpu_branched = false;

static zcpu_trap_fn g_trap = NULL;

void zcpu_set_trap_handler(zcpu_trap_fn fn) {
    g_trap = fn;
}

void zcpu_reset(uint32_t entry, uint32_t sp, uint32_t lr) {
    memset(&g_cpu, 0, sizeof(g_cpu));
    g_cpu.r[REG_SP] = sp;
    g_cpu.r[REG_LR] = lr;
    g_cpu.r[REG_PC] = entry & ~1u;
    g_cpu.cpsr = 0x10; /* modo User */
    if (entry & 1) g_cpu.cpsr |= CPSR_T; /* entry Thumb (bit 0 setado) */
    g_cpu.halted = false;
    LOGI("cpu_reset: PC=0x%08X SP=0x%08X LR=0x%08X %s",
         g_cpu.r[REG_PC], sp, lr, (g_cpu.cpsr & CPSR_T) ? "Thumb" : "ARM");
}

void zcpu_write_pc(uint32_t value) {
    g_cpu.r[REG_PC] = value & ((g_cpu.cpsr & CPSR_T) ? ~1u : ~3u);
    zcpu_branched = true;
}

void zcpu_bx(uint32_t value) {
    if (value & 1) {
        g_cpu.cpsr |= CPSR_T;
        g_cpu.r[REG_PC] = value & ~1u;
    } else {
        g_cpu.cpsr &= ~CPSR_T;
        g_cpu.r[REG_PC] = value & ~3u;
    }
    zcpu_branched = true;
}

void zcpu_step(void) {
    if (g_cpu.halted) return;

    uint32_t pc = g_cpu.r[REG_PC];

    /* Trap HLE: chamada de API BREW via endereco magico */
    if (pc >= ZMEM_HLE_BASE) {
        if (g_trap) {
            g_trap(pc);
        } else {
            LOGE("trap HLE em 0x%08X sem handler - parando CPU", pc);
            g_cpu.halted = true;
        }
        return;
    }

    zcpu_branched = false;

    if (g_cpu.cpsr & CPSR_T) {
        /* Thumb: 16-bit. Durante execucao, R15 = pc + 4 */
        uint16_t instr = zmem_read16(pc & ~1u);
        g_cpu.r[REG_PC] = pc + 4;
        zthumb_execute(instr);
        if (!zcpu_branched)
            g_cpu.r[REG_PC] = pc + 2;
    } else {
        /* ARM: 32-bit. Durante execucao, R15 = pc + 8 */
        uint32_t instr = zmem_read32(pc & ~3u);
        g_cpu.r[REG_PC] = pc + 8;
        zarm_execute(instr);
        if (!zcpu_branched)
            g_cpu.r[REG_PC] = pc + 4;
    }
    g_cpu.executed++;
}

uint32_t zcpu_run(uint32_t max) {
    uint32_t i;
    for (i = 0; i < max; i++) {
        if (g_cpu.halted) break;
        zcpu_step();
    }
    return i;
}
