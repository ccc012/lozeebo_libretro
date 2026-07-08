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

void ztrace_pc(uint32_t pc);
void ztrace_dump(void);

/* PC valido para fetch: RAM, heap ou stack (BREW pode materializar
 * thunks no heap/stack). Fora disso a CPU descarrilou. */
static bool pc_fetchable(uint32_t pc) {
    if (pc < ZMEM_RAM_SIZE) return true;
    if (pc >= ZMEM_HEAP_BASE && pc < ZMEM_HEAP_BASE + ZMEM_HEAP_SIZE)
        return true;
    if (pc >= ZMEM_STACK_BASE && pc < ZMEM_STACK_BASE + ZMEM_STACK_SIZE)
        return true;
    return false;
}

void zcpu_step(void) {
    if (g_cpu.halted) return;

    uint32_t pc = g_cpu.r[REG_PC];
    if (pc == 0x0007AA9Cu) {
        static uint32_t hits = 0;
        if (hits < 8) {
            uint32_t r8 = g_cpu.r[8];
            LOGI("watch loop-div 0x7AA9C: r8=0x%08X [r8+0x54]=0x%08X "
                 "[r8+0x78]=0x%08X [r8+0x7C]=0x%08X r4=0x%08X LR=0x%08X",
                 r8, zmem_read32(r8 + 0x54u), zmem_read32(r8 + 0x78u),
                 zmem_read32(r8 + 0x7Cu), g_cpu.r[4], g_cpu.r[REG_LR]);
            hits++;
        }
    }
    if (pc == 0x0007AA40u) {
        static uint32_t hits2 = 0;
        if (hits2 < 8) {
            LOGI("watch entrada 0x7AA40: r0=0x%08X r1=0x%08X r2=0x%08X "
                 "r3=0x%08X LR=0x%08X SP=0x%08X",
                 g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[3],
                 g_cpu.r[REG_LR], g_cpu.r[REG_SP]);
            hits2++;
        }
    }
    ztrace_pc(pc);

    /* Trap HLE: chamada de API BREW via endereco magico. Apenas a janela
     * realmente usada por este emulador (ZMEM_HLE_BASE..ZMEM_HLE_END) e
     * valida; PC fora dela (ex: 0xFF000000) nunca veio de um vtable/stub
     * legitimo - e um branch corrompido e deve cair no cheque de
     * descarrilamento abaixo, em vez de ser absorvido silenciosamente
     * como "API nao implementada" (o que mascarava o crash como um loop
     * infinito de warnings). */
    if (pc >= ZMEM_HLE_BASE && pc < ZMEM_HLE_END) {
        if (g_trap) {
            g_trap(pc);
        } else {
            LOGE("trap HLE em 0x%08X sem handler - parando CPU", pc);
            g_cpu.halted = true;
        }
        return;
    }

    /* Descarrilou: fetch fora de qualquer regiao executavel */
    if (!pc_fetchable(pc)) {
        LOGE("CPU descarrilou: fetch em 0x%08X (LR=0x%08X SP=0x%08X)",
             pc, g_cpu.r[REG_LR], g_cpu.r[REG_SP]);
        ztrace_dump();
        g_cpu.halted = true;
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
