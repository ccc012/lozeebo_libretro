/* cpu.c - Loop fetch-decode-execute e controle da CPU */
#include <string.h>
#include "cpu.h"
#include "../memory/memory.h"
#include "../debug/log.h"
#include "../brew/brew.h"
#include "../gpu/egl_gl.h"
#include "../audio/miniz_tinfl.h"

zcpu_t g_cpu;
bool zcpu_branched = false;

static zcpu_trap_fn g_trap = NULL;
static zcpu_trap_fn g_custom_traps[256] = {0};

void zcpu_set_trap_handler(zcpu_trap_fn fn) {
    g_trap = fn;
}

void zcpu_set_custom_trap(uint32_t id, zcpu_trap_fn fn) {
    if (id < 256) g_custom_traps[id] = fn;
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
static int try_hle_empty_count(uint32_t instr);
static int try_hle_word_fill(uint32_t instr);

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

    /* Stack overflow protection: stack base 0x2FC00000, size 4MB */
    if ((g_cpu.r[REG_SP] < ZMEM_STACK_BASE + 0x1000 ||
         g_cpu.r[REG_SP] >= ZMEM_STACK_BASE + ZMEM_STACK_SIZE) &&
        (g_cpu.r[REG_SP] < ZMEM_HEAP_BASE ||
         g_cpu.r[REG_SP] >= ZMEM_HEAP_BASE + ZMEM_HEAP_SIZE)) {
        LOGE("STACK OVERFLOW: SP=0x%08X (base=0x%08X, limit=0x%08X) PC=0x%08X LR=0x%08X",
             g_cpu.r[REG_SP], ZMEM_STACK_BASE, ZMEM_STACK_BASE + 0x1000,
             g_cpu.r[REG_PC], g_cpu.r[REG_LR]);
        g_cpu.halted = true;
        return;
    }

    uint32_t pc = g_cpu.r[REG_PC];
    /* Bad Dudes hang: bit-pack init at 0x186F80 / 0x187194. Dump width (r2)
     * + stack args; count every entry (caller loops layers). */
    if (pc == 0x00186F80u || pc == 0x00187194u) {
        static uint32_t hits = 0;
        hits++;
        if (hits <= 32 || (hits & 0xFFu) == 0) {
            uint32_t sp = g_cpu.r[REG_SP];
            LOGI("BITPACK #%u PC=0x%08X LR=0x%08X r2(width)=0x%08X r3=0x%08X "
                 "r1=0x%08X [sp]=%08X [sp+4]=%08X [sp+8]=%08X [sp+0x10]=%08X",
                 hits, pc, g_cpu.r[REG_LR], g_cpu.r[2], g_cpu.r[3],
                 g_cpu.r[1], zmem_read32(sp), zmem_read32(sp + 4),
                 zmem_read32(sp + 8), zmem_read32(sp + 16));
        }
    }
    if (pc == 0x00000898u || pc == 0x0000098Cu || pc == 0x00000A88u ||
        pc == 0x00003408u || pc == 0x0000544Cu) {
        static uint32_t dtor_hits = 0;
        if (dtor_hits < 40) {
            LOGI("DTOR 0x%05X: r0=0x%08X LR=0x%08X SP=0x%08X [r0+0x48]=0x%08X "
                 "[r0+0x34]=0x%08X [r0+0x5C]=0x%08X",
                 pc, g_cpu.r[0], g_cpu.r[REG_LR], g_cpu.r[REG_SP],
                 zmem_read32(g_cpu.r[0] + 0x48), zmem_read32(g_cpu.r[0] + 0x34),
                 zmem_read32(g_cpu.r[0] + 0x5C));
            dtor_hits++;
        }
    }
    if (pc == 0x000948B8u) {
        static uint32_t fh = 0;
        if (fh < 30) {
            LOGI("VFSREAD wrapper entry: r0=0x%08X LR=0x%08X SP=0x%08X "
                 "r1=0x%08X",
                 g_cpu.r[0], g_cpu.r[REG_LR], g_cpu.r[REG_SP], g_cpu.r[1]);
            fh++;
        }
    }
    if (pc == 0x000100A0u || pc == 0x000100A8u || pc == 0x000100C0u) {
        static uint32_t hits = 0;
        if (hits < 600) {
            LOGI("watch factory 0x%05X: r0=0x%08X r1=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X",
                 pc, g_cpu.r[0], g_cpu.r[1], g_cpu.r[4], g_cpu.r[5],
                 g_cpu.r[6]);
            hits++;
        }
    }
    if (pc == 0x0001C168u) {
        static uint32_t hits = 0;
        if (hits < 60) {
            uint32_t st = g_cpu.r[4];
            LOGI("LDSCRIPT idx=%d status=%d r1=0x%08X LR=0x%08X",
                 (int)zmem_read16(st + 2), (int)zmem_read32(st + 0x14),
                 g_cpu.r[1], g_cpu.r[REG_LR]);
            hits++;
        }
    }
    if (pc == 0x0001C784u || pc == 0x0001C964u || pc == 0x0001C3ECu ||
        pc == 0x0000AE50u) {
        static uint32_t hits = 0;
        if (hits < 60) {
            LOGI("LDSUB 0x%08X: r0=0x%08X r1=0x%08X r2=0x%08X LR=0x%08X",
                 pc, g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[REG_LR]);
            hits++;
        }
    }
    if (pc == 0x0001C068u) {
        static uint32_t hits = 0;
        if (hits < 600) {
            LOGI("READTGT 0x1C068: r0=0x%08X (obj) [obj+4]=0x%08X(slot) r3=0x%08X "
                 "ifile-real[+4]=0x%08X",
                 g_cpu.r[0], zmem_read32(g_cpu.r[0] + 4), g_cpu.r[3],
                 zmem_read32(g_cpu.r[4] + 8) ? zmem_read32(zmem_read32(g_cpu.r[4] + 8) + 4) : 0);
            hits++;
        }
    }
    if (pc == 0x00019180u || pc == 0x00019200u || pc == 0x00019238u) {
        static uint32_t fits = 0;
        if (fits < 40) {
            LOGI("TIPREG 0x%05X: this=0x%08X [80]=0x%08X r1=0x%08X r2=0x%08X "
                 "r3=0x%08X r4=0x%08X LR=0x%08X",
                 pc, g_cpu.r[0], zmem_read32(g_cpu.r[0] + 0x80),
                 g_cpu.r[1], g_cpu.r[2], g_cpu.r[3], g_cpu.r[4], g_cpu.r[REG_LR]);
            fits++;
        }
    }
    if (pc == 0x00017BF4u) {
        static uint32_t cb = 0;
        if (cb < 20) {
            LOGI("TIPREAD 0x17BF4: r0=0x%08X (ret call) r4=0x%08X [r4+28]=0x%08X "
                 "[r4+30]=0x%08X LR=0x%08X", g_cpu.r[0], g_cpu.r[4],
                 zmem_read32(g_cpu.r[4] + 0x28), zmem_read32(g_cpu.r[4] + 0x30),
                 g_cpu.r[REG_LR]);
            cb++;
        }
    }
    if (pc == 0x00017C38u || pc == 0x00017BBEu) {
        static uint32_t msg = 0;
        if (msg < 20) {
            LOGI("TIPINIT 0x%08X: r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X LR=0x%08X",
                 pc, g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[3], g_cpu.r[REG_LR]);
            msg++;
        }
    }
    if (pc == 0x0001C04Cu || pc == 0x0001C060u) {
        static uint32_t hits = 0;
        static int unblocked = 0;
        static uint32_t nblock = 0;
        if (hits < 24) {
            uint32_t o = g_cpu.r[4];
            uint32_t mgr = zmem_read32(o + 4);
            uint32_t lr = g_cpu.r[REG_LR];
            uint32_t sp = g_cpu.r[REG_SP];
            uint32_t obj = g_cpu.r[2];
            uint32_t vt = obj ? zmem_read32(obj) : 0;
            uint32_t tgt = vt ? zmem_read32(vt + 0x13C) : 0;
            uint32_t g = zmem_read32(0x1C0B8);
            uint32_t sq = zmem_read32(sp);
            uint32_t sl = zmem_read32(sp + 4);
            uint32_t q8 = g ? zmem_read32(g + 8) : 0;
            uint32_t qC = g ? zmem_read32(g + 0xC) : 0;
            uint32_t obj2 = qC ? qC : q8;
            uint32_t vt2 = obj2 ? zmem_read32(obj2) : 0;
            uint32_t tgt2 = vt2 ? zmem_read32(vt2 + 0x190) : 0;
            LOGI("READLOOP D: obj=%08X vt=0x%08X call=vt[0x190]=0x%08X", obj2, vt2, tgt2);
            LOGI("READLOOP Q: G=0x%08X [G+8]=0x%08X [G+C]=0x%08X node[+8]=0x%08X [node+0x20]=0x%08X",
                 g, q8, qC, obj2 ? zmem_read32(obj2 + 8) : 0,
                 obj2 ? zmem_read32(obj2 + 0x20) : 0);
            static int wdump = 0;
            if (!wdump) {
                wdump = 1;
                int wj;
                for (wj = 0; wj < 7; wj++) {
                    uint32_t w = 0x2017C + 0x20 * wj;
                    LOGI("WLK %d @%08X: %08X %08X %08X %08X | %08X %08X %08X %08X",
                         wj, w,
                         zmem_read32(w + 0), zmem_read32(w + 4),
                         zmem_read32(w + 8), zmem_read32(w + 0xC),
                         zmem_read32(w + 0x10), zmem_read32(w + 0x14),
                         zmem_read32(w + 0x18), zmem_read32(w + 0x1C));
                }
            }
            static int vtdump = 0;
            if (!vtdump) {
                vtdump = 1;
                uint32_t v = 0x100051A0;
                int vj;
                for (vj = 0; vj < 8; vj++) {
                    LOGI("VT %08X: %08X %08X %08X %08X | %08X %08X %08X %08X",
                         v + 0x20 * vj,
                         zmem_read32(v + 0x20 * vj + 0),
                         zmem_read32(v + 0x20 * vj + 4),
                         zmem_read32(v + 0x20 * vj + 8),
                         zmem_read32(v + 0x20 * vj + 0xC),
                         zmem_read32(v + 0x20 * vj + 0x10),
                         zmem_read32(v + 0x20 * vj + 0x14),
                         zmem_read32(v + 0x20 * vj + 0x18),
                         zmem_read32(v + 0x20 * vj + 0x1C));
                }
            }
            LOGI("READLOOP R: r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X r5=0x%08X r6=0x%08X r8=0x%08X r12=0x%08X LR=0x%08X [SP]=0x%08X [SP+4]=0x%08X SP=0x%08X",
                g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[3],
                g_cpu.r[5], g_cpu.r[6], g_cpu.r[8], g_cpu.r[12], lr, sq, sl, sp);
            static int jbdump = 0;
            if (!jbdump) {
                jbdump = 1;
                uint32_t jobs[3] = { 0x10005D60, 0x10005DA8, 0x10005DF0 };
                int jb;
                for (jb = 0; jb < 3; jb++) {
                    uint32_t j = jobs[jb];
                    LOGI("TIPJOB %d @0x%08X: %08X %08X %08X %08X | %08X %08X %08X %08X",
                         jb, j,
                         zmem_read32(j + 0), zmem_read32(j + 4),
                         zmem_read32(j + 8), zmem_read32(j + 0xC),
                         zmem_read32(j + 0x10), zmem_read32(j + 0x14),
                         zmem_read32(j + 0x18), zmem_read32(j + 0x1C));
                }
            }
        }
        if (!unblocked && 0 /* desligado: sintese corrompia o estado do jogo */) {
            unblocked = 1;
            zmem_write32(0x10005A00, 0);
            zmem_write32(0x10005A04, 1);
            zmem_write32(0x10005A08, 2);
            LOGI("READLOOP UNBLOCK: slots sintetizados 0,1,2");
        }
        nblock++;
        if (nblock == 400000u) {
            uint32_t o = g_cpu.r[4];
            uint32_t mgr = zmem_read32(o + 4);
            LOGI("READLOOP SPIN-4K: o=0x%08X [o+8]=0x%08X mgr=0x%08X mgr[+80]=0x%08X "
                 "r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X",
                 o, zmem_read32(o + 8), mgr, zmem_read32(mgr + 0x80),
                 g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[3]);
        }
        if (nblock == 800u) {
            uint32_t o = g_cpu.r[4];
            uint32_t mgr = zmem_read32(o + 4);
            LOGI("READLOOP FORCE-ADVANCE: DESLIGADO (era mgr[+80]=0x%08X -> 3, o+8..=0x10005D60)",
                 zmem_read32(mgr + 0x80));
        }
        if (nblock == 1200u) {
            uint32_t o = g_cpu.r[4];
            uint32_t mgr = zmem_read32(o + 4);
            LOGI("READLOOP FORCE-ADVANCE: DESLIGADO (era mgr[+80]=0x%08X -> 4)", zmem_read32(mgr + 0x80));
        }
        if (nblock == 1600u) {
            uint32_t o = g_cpu.r[4];
            uint32_t mgr = zmem_read32(o + 4);
            LOGI("READLOOP FORCE-ADVANCE: DESLIGADO (era mgr[+80]=0x%08X -> 6)", zmem_read32(mgr + 0x80));
        }
        if (nblock == 2000u) {
            uint32_t o = g_cpu.r[4];
            uint32_t mgr = zmem_read32(o + 4);
            LOGI("READLOOP FORCE-ADVANCE: DESLIGADO (era mgr[+80]=0x%08X -> 0x1D(29))", zmem_read32(mgr + 0x80));
        }
    }
    if (pc == 0x0001C02Cu || pc == 0x0001C038u) {
        static uint32_t hits = 0;
        if (hits < 600) {
            LOGI("MALLOCPG 0x%05X: r8=0x%08X [r8-4]=0x%08X "
                 "[[r8-4]+0x68]=0x%08X r0=0x%08X(size) r1=0x%08X(fn) r6=0x%08X",
                 pc, g_cpu.r[8], zmem_read32(g_cpu.r[8] - 4),
                 zmem_read32(zmem_read32(g_cpu.r[8] - 4) + 0x68),
                 g_cpu.r[0], g_cpu.r[1], g_cpu.r[6]);
            hits++;
        }
    }
    if (pc == 0x0000739Cu || pc == 0x000074BCu || pc == 0x000074F0u) {
        static uint32_t hits = 0;
        if (hits < 600) {
            LOGI("GGZFILE 0x%08X: r0=0x%08X r1=0x%08X r2=0x%08X "
                 "r4=0x%08X [r4+8]=0x%08X [r4+0x10]=0x%08X [r4+0x14]=0x%08X",
                 pc, g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[4],
                 zmem_read32(g_cpu.r[4] + 8), zmem_read32(g_cpu.r[4] + 0x10),
                 zmem_read32(g_cpu.r[4] + 0x14));
            hits++;
        }
    }
    if (pc == 0x00020938u) {
        static uint32_t hits = 0;
        if (hits < 400) {
            LOGI("STSET 0x20938: r0=0x%08X LR=0x%08X [r0+0x50]=0x%08X [r0+0x54]=0x%08X",
                 g_cpu.r[0], g_cpu.r[REG_LR],
                 zmem_read32(g_cpu.r[0] + 0x50), zmem_read32(g_cpu.r[0] + 0x54));
            hits++;
        }
    }
    if (pc == 0x00020A70u) {
        static uint32_t hits = 0;
        if (hits < 100) {
            LOGI("LDSTAT 0x20A70: r4=0x%08X r6=0x%08X val@[r6+0x6C4]=0x%08X "
                 "r0=0x%08X [app+0x50]=0x%08X",
                 g_cpu.r[4], g_cpu.r[6], zmem_read32(g_cpu.r[6] + 0x6C4),
                 g_cpu.r[0], zmem_read32(g_cpu.r[4] + 0x50));
            hits++;
        }
    }
    if (pc == 0x0001C2C0u) {
        static uint32_t hits = 0;
        if (hits < 100) {
            LOGI("BLDISP 0x1C2C0: r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X "
                 "[r2+0x44](idx)=0x%08X [r2+0x48](flags)=0x%08X",
                 g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[3],
                 zmem_read32(g_cpu.r[2] + 0x44), zmem_read32(g_cpu.r[2] + 0x48));
            hits++;
        }
    }
    if (pc == 0x0001C2D0u) {
        static uint32_t hits = 0;
        if (hits < 100) {
            LOGI("BLDISP 0x1C2D0: ret0x9cbc=0x%08X r6=0x%08X r7=0x%08X",
                 g_cpu.r[0], g_cpu.r[6], g_cpu.r[7]);
            hits++;
        }
    }
    if (pc == 0x0001BFD0u && g_cpu.r[REG_LR] == 0x9D40u) {
        static uint32_t hits = 0;
        if (hits < 30) {
            LOGI("LDFILE ctx: r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X "
                 "[r0+0xC]=0x%08X [r0+0x10]=0x%08X [r0+0x14]=0x%08X "
                 "[r0+8]=0x%08X",
                 g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[3],
                 zmem_read32(g_cpu.r[0] + 0xC), zmem_read32(g_cpu.r[0] + 0x10),
                 zmem_read32(g_cpu.r[0] + 0x14), zmem_read32(g_cpu.r[0] + 8));
            hits++;
        }
    }
    if (pc == 0x0001E17Cu || pc == 0x0001E1A8u) {
        static uint32_t hits = 0;
        if (hits < 40) {
            uint32_t s = g_cpu.r[1];
            uint32_t sl = g_cpu.r[0];
            LOGI("INFLT 0x%05X: LR=0x%08X dest/slot=0x%08X src=0x%08X "
                 "slot[0]=0x%08X slot[4]=0x%08X slot[8]=0x%08X slot[12]=0x%08X "
                 "src[0..7]=%02X %02X %02X %02X %02X %02X %02X %02X",
                 pc, g_cpu.r[REG_LR], sl, s,
                 zmem_read32(sl + 0), zmem_read32(sl + 4), zmem_read32(sl + 8), zmem_read32(sl + 12),
                 zmem_read8(s), zmem_read8(s + 1), zmem_read8(s + 2), zmem_read8(s + 3),
                 zmem_read8(s + 4), zmem_read8(s + 5), zmem_read8(s + 6), zmem_read8(s + 7));
            hits++;
        }
    }
    if (pc == 0x00009D18u) {
        static uint32_t hits = 0;
        if (hits < 60) {
            LOGI("SLOT 0x9D18: [r4+0x530](cache)=0x%08X r5(dest)=0x%08X "
                 "r4=0x%08X r0(app)=0x%08X ip(flags)=0x%08X",
                 zmem_read32(g_cpu.r[4] + 0x530), g_cpu.r[5],
                 g_cpu.r[4], g_cpu.r[0], g_cpu.r[12]);
            hits++;
        }
    }
    if (pc == 0x00009D44u) {
        static uint32_t hits = 0;
        if (hits < 60) {
            LOGI("SLOT 0x9D44 (pos 1BFD0): r0=0x%08X", g_cpu.r[0]);
            hits++;
        }
    }
    if (pc == 0x00009D68u) {
        static uint32_t hits = 0;
        if (hits < 60) {
            LOGI("SLOT 0x9D68 (pos 1E17C): r0=0x%08X [sp]=0x%08X",
                 g_cpu.r[0], zmem_read32(g_cpu.r[REG_SP]));
            hits++;
        }
    }
    if (pc == 0x0004B64u || pc == 0x0004B74u) {
        static uint32_t hits = 0;
        if (hits < 600) {
            LOGI("MAINCB 0x%05X: r1=0x%08X (func a chamar) r0=0x%08X",
                 pc, g_cpu.r[1], g_cpu.r[0]);
            hits++;
        }
    }
    if (pc == 0x0001F528u) {
        static uint32_t hits = 0;
        if (hits < 600) {
            LOGI("UPDATE 0x1F528: r0=0x%08X LR=0x%08X", g_cpu.r[0],
                 g_cpu.r[REG_LR]);
            hits++;
        }
    }
    if (pc == 0x00005F98u) {
        static uint32_t hits = 0;
        if (hits < 600) {
            LOGI("ERRFN 0x5F98: r0=0x%08X r4=0x%08X r5=0x%08X LR=0x%08X SP=0x%08X "
                 "err=0x%08X list=0x%08X",
                 g_cpu.r[0], g_cpu.r[4], g_cpu.r[5], g_cpu.r[REG_LR],
                 g_cpu.r[REG_SP], zmem_read32(g_cpu.r[4] + 0xD02C4),
                 zmem_read16(g_cpu.r[4] + 0xD80B2));
            hits++;
        }
    }
    if (pc == 0x0001F320u || pc == 0x0001F358u || pc == 0x0001F37Cu ||
        pc == 0x0001F3ACu || pc == 0x0001F3C8u || pc == 0x0001F3DCu ||
        pc == 0x0001C6B0u || pc == 0x00022384u) {
        static uint32_t hits = 0;
        if (hits < 600) {
            LOGI("LOADER 0x%05X: r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X "
                 "r4=0x%08X LR=0x%08X",
                 pc, g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[3],
                 g_cpu.r[4], g_cpu.r[REG_LR]);
            hits++;
        }
    }
    /* Data East: zlib uncompress — tinfl HLE.
     * HB/BD @ 0x2A174; Caveman @ 0x2BB64 (mesmo corpo, base PIC).
     * r1=src r2=srcLen r3=dst [sp]=destLen (r0 unused / out scratch). */
    if (pc == 0x0002A174u || pc == 0x0002BB64u) {
        uint32_t src = g_cpu.r[1];
        uint32_t src_len = g_cpu.r[2];
        uint32_t dest = g_cpu.r[3];
        uint32_t dest_len = zmem_read32(g_cpu.r[REG_SP]);
        void *pdst, *psrc;
        size_t got;
        int ok = 0;

        pdst = (dest && dest_len && dest_len < 0x1000000u)
                   ? zmem_host_ptr(dest, dest_len) : NULL;
        psrc = (src && src_len && src_len < 0x1000000u)
                   ? zmem_host_ptr(src, src_len) : NULL;
        if (pdst && psrc) {
            got = tinfl_decompress_mem_to_mem(
                pdst, dest_len, psrc, src_len,
                TINFL_FLAG_PARSE_ZLIB_HEADER |
                    TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
            if (got == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED)
                got = tinfl_decompress_mem_to_mem(
                    pdst, dest_len, psrc, src_len,
                    TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
            if (got != TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
                g_cpu.r[0] = 0;
                ok = 1;
                LOGI("HLE uncompress dst=0x%08X src=0x%08X slen=%u dlen=%u -> %u "
                     "b0=%02X%02X%02X%02X b14=%02X%02X b210=%02X%02X%02X%02X "
                     "b214=%02X%02X%02X%02X b216=%08X",
                     dest, src, src_len, dest_len, (unsigned)got,
                     zmem_read8(dest), zmem_read8(dest + 1),
                     zmem_read8(dest + 2), zmem_read8(dest + 3),
                     zmem_read8(dest + 0x14), zmem_read8(dest + 0x15),
                     zmem_read8(dest + 0x210), zmem_read8(dest + 0x211),
                     zmem_read8(dest + 0x212), zmem_read8(dest + 0x213),
                     zmem_read8(dest + 0x214), zmem_read8(dest + 0x215),
                     zmem_read8(dest + 0x216), zmem_read8(dest + 0x217),
                     zmem_read32(dest + 0x216));
            }
        }
        if (!ok) {
            LOGI("HLE uncompress FAIL r0=%08X r1=%08X r2=%08X r3=%08X sp0=%08X",
                 g_cpu.r[0], src, src_len, dest, dest_len);
            g_cpu.r[0] = (uint32_t)-3;
        }
        g_cpu.r[REG_PC] = g_cpu.r[REG_LR] & ~3u;
        zcpu_branched = true;
        g_cpu.executed++;
        return;
    }
    /* Data East FONT: loop endian-swap; hang se base/count ruins. */
    if (pc == 0x0001A2F4u || pc == 0x0001A320u || pc == 0x0001A374u ||
        pc == 0x0001A39Cu || pc == 0x0001A3A4u) {
        static uint32_t hits = 0;
        if (hits < 80) {
            uint32_t r7 = g_cpu.r[7];
            uint32_t base = zmem_read32(r7);
            LOGI("FONT 0x%05X r0=%08X r2=%08X r3=%08X r4=%08X r5=%08X "
                 "lr_reg=%08X base=%08X "
                 "b0=%08X b4=%08X b8=%08X b10=%04X b14=%04X "
                 "b210=%02X%02X%02X%02X b214=%02X%02X%02X%02X "
                 "b216=%08X slot18=%08X slot2c=%08X slot30=%08X",
                 pc, g_cpu.r[0], g_cpu.r[2], g_cpu.r[3], g_cpu.r[4],
                 g_cpu.r[5], g_cpu.r[14], base,
                 zmem_read32(base), zmem_read32(base + 4),
                 zmem_read32(base + 8), zmem_read16(base + 0x10),
                 zmem_read16(base + 0x14),
                 zmem_read8(base + 0x210), zmem_read8(base + 0x211),
                 zmem_read8(base + 0x212), zmem_read8(base + 0x213),
                 zmem_read8(base + 0x214), zmem_read8(base + 0x215),
                 zmem_read8(base + 0x216), zmem_read8(base + 0x217),
                 zmem_read32(base + 0x216),
                 zmem_read32(r7 + 0x18), zmem_read32(r7 + 0x2c),
                 zmem_read32(r7 + 0x30));
            hits++;
        }
    }
    if (pc == 0x000237C4u || pc == 0x000237FCu || pc == 0x00023820u ||
        pc == 0x0002385Cu || pc == 0x0000009F3Cu || pc == 0x000023838u) {
        static uint32_t hits = 0;
        if (hits < 50) {
            LOGI("SAVEC 0x%08X: r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X "
                 "r4=0x%08X r7=0x%08X LR=0x%08X",
                 pc, g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[3],
                 g_cpu.r[4], g_cpu.r[7], g_cpu.r[REG_LR]);
            hits++;
        }
    }
    /* Zuma file+0x200→VA0: soft 9F58→9D58; depth@A154→9F54:
     * ldrb d; movs r0,d,lsr #3. d<8: cmp#2→BL 957C (RGB565→RGBA+TexImage@970C),
     * cmp#4→BL 9754. d>>3==1 smash@A22C. TexImage also A1FC/A2A8/A388; QGL@93F74.
     * HLE em 0957C: converte RGB565 no host e pula o loop ARM lento. */
    if (pc == 0x000044B8u || pc == 0x00009D58u || pc == 0x00009B18u ||
        pc == 0x0007D2BCu || pc == 0x00038E34u || pc == 0x00009CACu ||
        pc == 0x0000A1FCu || pc == 0x0000A2A8u || pc == 0x0000A388u ||
        pc == 0x0000957Cu || pc == 0x0000970Cu || pc == 0x00009754u)
        (void)zgl_create_igles_interface();
    /* HLE: intercepta BL 957C. r0=&tex_obj, r1=bmp, r2=pot_w, r3=pot_h.
     * Preenche [tex+1C/20/24] como o helper; upload RGB565 no host; retorna. */
    if (pc == 0x0000957Cu && zboot_get_applet_clsid() == 0x0109AF55u) {
        uint32_t tobj = g_cpu.r[0] ? zmem_read32(g_cpu.r[0]) : 0;
        uint32_t bmp = g_cpu.r[1];
        uint32_t pw = g_cpu.r[2], ph = g_cpu.r[3];
        if (tobj && zgl_hle_zuma_soft_rgb565(bmp, pw, ph)) {
            zmem_write32(tobj + 0x1Cu, 4u);   /* depth RGBA */
            zmem_write32(tobj + 0x20u, pw);
            zmem_write32(tobj + 0x24u, ph);
            g_cpu.r[0] = 0;
            g_cpu.r[REG_PC] = g_cpu.r[REG_LR];
            zcpu_branched = true;
            return;
        }
    }
    if (pc == 0x00009D58u) {
        static uint32_t hits = 0;
        uint32_t bmp = g_cpu.r[1]; /* mov r4,r1 logo apos */
        if (hits < 24)
            LOGI("ZUMA soft-enter 9D58 LR=0x%08X r0=0x%08X bmp=0x%08X "
                 "d=%u sch=%u pBmp=0x%08X cx=%u cy=%u",
                 g_cpu.r[REG_LR], g_cpu.r[0], bmp,
                 bmp ? zmem_read8(bmp + 28u) : 0u,
                 bmp ? zmem_read8(bmp + 29u) : 0u,
                 bmp ? zmem_read32(bmp + 8u) : 0u,
                 bmp ? zmem_read16(bmp + 20u) : 0u,
                 bmp ? zmem_read16(bmp + 22u) : 0u);
        hits++;
    }
    if (pc == 0x00009F54u || pc == 0x00009F58u || pc == 0x00009F64u ||
        pc == 0x00009F78u || pc == 0x00009F80u || pc == 0x00009FC4u ||
        pc == 0x00009FCCu) {
        static uint32_t hits = 0;
        if (hits < 48)
            LOGI("ZUMA soft-fmt pc=0x%05X r0=0x%08X r4=0x%08X d=%u "
                 "cx=%u cy=%u LR=0x%08X",
                 pc, g_cpu.r[0], g_cpu.r[4],
                 g_cpu.r[4] ? zmem_read8(g_cpu.r[4] + 28u) : 0u,
                 g_cpu.r[4] ? zmem_read16(g_cpu.r[4] + 20u) : 0u,
                 g_cpu.r[4] ? zmem_read16(g_cpu.r[4] + 22u) : 0u,
                 g_cpu.r[REG_LR]);
        hits++;
    }
    if (pc == 0x0000A22Cu) {
        static uint32_t hits = 0;
        if (hits < 16)
            LOGI("ZUMA soft-smash A22C r0=0x%08X r4=0x%08X d=%u",
                 g_cpu.r[0], g_cpu.r[4],
                 g_cpu.r[4] ? zmem_read8(g_cpu.r[4] + 28u) : 0u);
        hits++;
    }
    if (pc == 0x00009E04u) {
        static uint32_t hits = 0;
        if (hits < 24)
            LOGI("ZUMA soft-genchk 9E04 r0=0x%08X (0=>upload) r9=0x%08X",
                 g_cpu.r[0], g_cpu.r[9]);
        hits++;
    }
    /* d=2 helper entry + its TexImage@970C; also legacy smash TexImage sites. */
    if (pc == 0x0000A1FCu || pc == 0x0000A2A8u || pc == 0x0000A388u ||
        pc == 0x00009FBCu || pc == 0x0000A39Cu || pc == 0x0000957Cu ||
        pc == 0x0000970Cu || pc == 0x00009754u) {
        static uint32_t hits = 0;
        if (hits < 40)
            LOGI("ZUMA soft-TexImage pc=0x%05X LR=0x%08X r0=0x%08X r1=0x%08X "
                 "r2=0x%08X r3=0x%08X r4=0x%08X sp0=0x%08X tblC=0x%08X",
                 pc, g_cpu.r[REG_LR], g_cpu.r[0], g_cpu.r[1], g_cpu.r[2],
                 g_cpu.r[3], g_cpu.r[4], zmem_read32(g_cpu.r[REG_SP]),
                 zmem_read32(0x00093F74u + 0xCu));
        hits++;
    }
    if (pc == 0x00038E34u) {
        static uint32_t hits = 0;
        if (hits < 24)
            LOGI("ZUMA hit 38E34 LR=0x%08X r0=0x%08X r1=0x%08X r2=0x%08X "
                 "r3=0x%08X tblC=0x%08X",
                 g_cpu.r[REG_LR], g_cpu.r[0], g_cpu.r[1], g_cpu.r[2],
                 g_cpu.r[3], zmem_read32(0x00093F74u + 0xCu));
        hits++;
    }
    /* RE4-only. MakeTexture@4F160 → Fill@68030 (fmt[+20]); Create@4F33C
     * → softGen@6EFB0. Antigo force-BL@4F558 era ctor errado (HEAPEXEC). */
    if (zboot_get_applet_clsid() == 0x0108AF6Cu) {
        if (pc == 0x00005BDBCu || pc == 0x00004F160u ||
            pc == 0x000068030u || pc == 0x00004F33Cu ||
            pc == 0x00006EFB0u || pc == 0x00005BF80u) {
            static uint32_t hits = 0;
            uint32_t obj = g_cpu.r[0];
            if (hits < 96)
                LOGI("RE4 texpath pc=0x%05X r0=0x%08X r1=0x%08X "
                     "r2=0x%08X r3=0x%08X fmt=%02X id34=0x%08X "
                     "sp0=0x%08X sp1=0x%08X LR=0x%08X",
                     pc, g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[3],
                     obj ? zmem_read8(obj + 0x20u) : 0,
                     obj ? zmem_read32(obj + 0x34u) : 0,
                     zmem_read32(g_cpu.r[REG_SP]),
                     zmem_read32(g_cpu.r[REG_SP] + 4u),
                     g_cpu.r[REG_LR]);
            hits++;
        }
        /* Soft wrappers QGL: GenTextures@6E054 Bind@6E158 TexImage@6EC9C. */
        if (pc == 0x00006E054u || pc == 0x00006E158u ||
            pc == 0x00006E238u || pc == 0x00006EC9Cu) {
            static uint32_t hits = 0;
            (void)zgl_create_igles_interface();
            if (hits < 64)
                LOGI("RE4 soft pc=0x%05X r0=0x%08X r1=0x%08X r2=0x%08X "
                     "tblC=0x%08X LR=0x%08X",
                     pc, g_cpu.r[0], g_cpu.r[1], g_cpu.r[2],
                     zmem_read32(0x00072F30u + 0xCu), g_cpu.r[REG_LR]);
            hits++;
        }
        /* MCT type-T + type13 blx callback (deve ir a 5BDBC). */
        if (pc == 0x00001AD6Cu || pc == 0x00005C134u ||
            pc == 0x000053894u || pc == 0x00005A3C0u) {
            static uint32_t hits = 0;
            if (hits < 48)
                LOGI("RE4 mct-T pc=0x%05X r0=0x%08X r1=0x%08X r2=0x%08X "
                     "r3=0x%08X LR=0x%08X",
                     pc, g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[3],
                     g_cpu.r[REG_LR]);
            hits++;
        }
        /* CreateTexture mid: softGen retornou — ver se id==-1. */
        if (pc == 0x00004F368u) {
            static uint32_t hits = 0;
            if (hits < 48)
                LOGI("RE4 softGen-ret r0=0x%08X obj=0x%08X fmt=%02X LR=0x%08X",
                     g_cpu.r[0], g_cpu.r[4],
                     g_cpu.r[4] ? zmem_read8(g_cpu.r[4] + 0x20u) : 0,
                     g_cpu.r[REG_LR]);
            hits++;
        }
    }
    /* bx r12 em 38E9C/38EE8/38F24: vtbl[0x19C]=TexImage trap. */
    if (pc == 0x00038E9Cu || pc == 0x00038EE8u || pc == 0x00038F24u) {
        static uint32_t hits_bx = 0;
        if (hits_bx < 24) {
            uint32_t ctx = g_cpu.r[0];
            uint32_t vtbl = ctx ? zmem_read32(ctx) : 0;
            LOGI("ZUMA QGL-bx pc=0x%05X r12=0x%08X ctx=0x%08X vtbl=0x%08X "
                 "[+19C]=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X "
                 "sp0..5=%08X %08X %08X %08X %08X %08X",
                 pc, g_cpu.r[12], ctx, vtbl,
                 vtbl ? zmem_read32(vtbl + 0x19Cu) : 0,
                 g_cpu.r[1], g_cpu.r[2], g_cpu.r[3],
                 zmem_read32(g_cpu.r[REG_SP]),
                 zmem_read32(g_cpu.r[REG_SP] + 4u),
                 zmem_read32(g_cpu.r[REG_SP] + 8u),
                 zmem_read32(g_cpu.r[REG_SP] + 12u),
                 zmem_read32(g_cpu.r[REG_SP] + 16u),
                 zmem_read32(g_cpu.r[REG_SP] + 20u));
            hits_bx++;
        }
    }
    if (pc == 0x00000000u) {
        static uint32_t hits = 0;
        if (hits < 600) {
            LOGI("PC=0 (LR=0x%08X) %s", g_cpu.r[REG_LR],
                  g_cpu.r[REG_LR] ? "chamado como funcao ROM?" : "");
            hits++;
        }
        if (g_cpu.r[REG_LR] < ZMEM_HLE_BASE || g_cpu.r[REG_LR] >= ZMEM_HLE_END) {
            g_cpu.r[0] = 0;
            g_cpu.r[REG_PC] = g_cpu.r[REG_LR];
            return;
        }
    }
    if (pc == 0x00005868u || pc == 0x000057C8u || pc == 0x000057A4u) {
        static uint32_t hits = 0;
        if (hits < 200) {
            uint32_t obj = g_cpu.r[4];
            if (pc == 0x000057A4u)
                obj = g_cpu.r[0];
            LOGI("SIGDISP 0x%05X: obj=0x%08X [obj]=0x%08X [obj+4]=0x%08X "
                 "r2=0x%08X r3=0x%08X LR=0x%08X SP=0x%08X",
                 pc, obj, zmem_read32(obj), zmem_read32(obj + 4),
                 g_cpu.r[2], g_cpu.r[3], g_cpu.r[REG_LR], g_cpu.r[REG_SP]);
            hits++;
        }
    }
    if (pc == 0x0000586Cu || pc == 0x00005870u || pc == 0x0000587Cu) {
        static uint32_t hits = 0;
        if (hits < 200) {
            uint32_t obj = g_cpu.r[0];
            uint32_t vt = zmem_read32(obj);
            LOGI("SIGVTBL 0x%05X: obj=0x%08X [obj]=0x%08X ",
                 pc, obj, vt);
            if (vt < 0x60000000)
                LOGI("SIGSLOT [vt+0x24]=0x%08X", zmem_read32(vt + 0x24));
            hits++;
        }
    }
    ztrace_pc(pc);

    /* Diagnostico: primeiro acesso a regiao de heap alto (0x11F00000+),
     * onde jogos do Zeebo nunca executam codigo util; o primeiro PC la
     * revela quem pulou para os dados (salto corrupto). */
    if (pc >= 0x11F00000u && pc < 0x12000000u) {
        static uint32_t hits = 0;
        if (hits < 4) {
            hits++;
            LOGI("HEAPEXEC: PC=0x%08X LR=0x%08X SP=0x%08X "
                 "r4=0x%08X r5=0x%08X r6=0x%08X [r4]=0x%08X "
                 "[r4+4]=0x%08X [r4+8]=0x%08X",
                 pc, g_cpu.r[REG_LR], g_cpu.r[REG_SP],
                 g_cpu.r[4], g_cpu.r[5], g_cpu.r[6],
                 zmem_read32(g_cpu.r[4]),
                 zmem_read32(g_cpu.r[4] + 4),
                 zmem_read32(g_cpu.r[4] + 8));
        }
    }

    /* Dispatch tipo "virtual": 0x1BFE8 -> objeto [r2], vtable [r2],
     * funcao = [vtable + 0x180]. Se a vtable do objeto apontar para o
     * heap, o bx salta para dados (crash do Pac-Mania). */
    if (pc == 0x0001BFE8u || pc == 0x0001BFECu) {
        static uint32_t hits = 0;
        if (hits < 6) {
            uint32_t r1 = g_cpu.r[1], obj = 0, vt = 0;
            if (r1 < 0x10000000u) {
                uint32_t c = zmem_read32(r1 + 12), c8 = zmem_read32(r1 + 8);
                obj = c ? c : c8;
                if (obj && obj < 0x20000000u) vt = zmem_read32(obj);
            }
            LOGI("DISPATCH 0x%05X r0=0x%08X r1=0x%08X r2=0x%08X "
                 "[r1+8]=0x%08X [r1+12]=0x%08X obj=0x%08X "
                 "vt=0x%08X [vt+0x180]=0x%08X",
                 pc, g_cpu.r[0], g_cpu.r[1], g_cpu.r[2],
                 r1 < 0x10000000u ? zmem_read32(r1 + 8) : 0,
                 r1 < 0x10000000u ? zmem_read32(r1 + 12) : 0,
                 obj, vt,
                 vt && vt < 0x20000000u ? zmem_read32(vt + 0x180) : 0);
            hits++;
        }
    }
    /* Loop principal do Pac (0xA2AC, chamado por SetTimer). Dump dos
     * campos do applet em r0 para entender o estado que bloqueia o Draw. */
    if (pc == 0x0000A2ACu) {
        static uint32_t hits = 0;
        if (hits < 3) {
            uint32_t a = g_cpu.r[0];
            LOGI("MAINLOOP app=0x%08X [+11C]=%u [+119]=%u [+70]=0x%08X "
                 "[+74]=0x%08X [+C0]=0x%08X [+C4]=0x%08X [+E8]=0x%08X "
                 "r2=0x%08X r3=0x%08X",
                 a, a ? zmem_read32(a + 0x11C) & 0xFF : 0,
                 a ? zmem_read32(a + 0x119) & 0xFF : 0,
                 a ? zmem_read32(a + 0x70) : 0, a ? zmem_read32(a + 0x74) : 0,
                 a ? zmem_read32(a + 0xC0) : 0, a ? zmem_read32(a + 0xC4) : 0,
                 a ? zmem_read32(a + 0xE8) : 0,
                 g_cpu.r[2], g_cpu.r[3]);
            hits++;
        }
    }
    /* No blx do loop principal: alvo = [[r6-4]+0xB0] (slot de vtable
     * global). Se o global for 0/lixo, descobrimos o que falta. */
    if (pc == 0x0000A2D4u) {
        static uint32_t hits = 0;
        if (hits < 6) {
            uint32_t r6 = g_cpu.r[6], obj = 0, vt = 0, fn = 0;
            if (r6 >= 4 && r6 < 0x20000000u) {
                obj = zmem_read32(r6 - 4);
                if (obj && obj < 0x20000000u) {
                    vt = zmem_read32(obj);
                    if (vt && vt < 0x20000000u &&
                        (vt >= 0x30000u || vt < 0x10000000u))
                        fn = zmem_read32(vt + 0xB0);
                }
            }
            LOGI("GMAIN r6=0x%08X obj=[r6-4]=0x%08X vt=[obj]=0x%08X "
                 "fn=[vt+0xB0]=0x%08X r0=0x%08X r1=0x%08X r2=0x%08X "
                 "r3=0x%08X",
                 r6, obj, vt, fn, g_cpu.r[0], g_cpu.r[1],
                 g_cpu.r[2], g_cpu.r[3]);
            hits++;
        }
    }

    /* Mais infrauro: o bx real acontece em 0x1C01C (via [r1,#8])
     * ou 0x1C034 (via [r1,#4]). Logar o alvo e os registradores. */
    if (pc == 0x0001BFE8u || pc == 0x0001BFECu) {
        static uint32_t hits = 0;
        if (hits < 6) {
            uint32_t r1 = g_cpu.r[1], obj = 0, vt = 0;
            if (r1 < 0x10000000u) {
                uint32_t c = zmem_read32(r1 + 12), c8 = zmem_read32(r1 + 8);
                obj = c ? c : c8;
                if (obj && obj < 0x20000000u) vt = zmem_read32(obj);
            }
            if (hits == 0) {
                LOGI("DVT0 r1=0x%08X obj=0x%08X vt=0x%08X [vt+0xD0]=0x%08X "
                     "[vt+0x180]=0x%08X", r1, obj, vt,
                     vt < 0x20000000u && vt ? zmem_read32(vt + 0xD0) : 0,
                     vt < 0x20000000u && vt ? zmem_read32(vt + 0x180) : 0);
            }
            if (hits == 1 && vt && vt < 0x20000000u) {
                LOGI("DVTBL %08X %08X %08X %08X %08X %08X %08X %08X",
                     zmem_read32(vt + 0x00), zmem_read32(vt + 0x04),
                     zmem_read32(vt + 0x08), zmem_read32(vt + 0x0C),
                     zmem_read32(vt + 0x10), zmem_read32(vt + 0x14),
                     zmem_read32(vt + 0x18), zmem_read32(vt + 0x1C));
                LOGI("DVTBL2 %08X %08X %08X %08X %08X %08X %08X %08X",
                     zmem_read32(vt + 0x20), zmem_read32(vt + 0x24),
                     zmem_read32(vt + 0x28), zmem_read32(vt + 0x2C),
                     zmem_read32(vt + 0x30), zmem_read32(vt + 0x34),
                     zmem_read32(vt + 0x38), zmem_read32(vt + 0x3C));
                LOGI("DVTBL3 %08X %08X %08X %08X %08X %08X %08X %08X",
                     zmem_read32(vt + 0x40), zmem_read32(vt + 0x44),
                     zmem_read32(vt + 0x48), zmem_read32(vt + 0x4C),
                     zmem_read32(vt + 0x50), zmem_read32(vt + 0x54),
                     zmem_read32(vt + 0x58), zmem_read32(vt + 0x5C));
                LOGI("DVTBL4 %08X %08X %08X %08X %08X %08X %08X %08X",
                     zmem_read32(vt + 0x60), zmem_read32(vt + 0x64),
                     zmem_read32(vt + 0x68), zmem_read32(vt + 0x6C),
                     zmem_read32(vt + 0x70), zmem_read32(vt + 0x74),
                     zmem_read32(vt + 0x78), zmem_read32(vt + 0x7C));
                LOGI("DVTBL5 %08X %08X %08X %08X %08X %08X %08X %08X",
                     zmem_read32(vt + 0x80), zmem_read32(vt + 0x84),
                     zmem_read32(vt + 0x88), zmem_read32(vt + 0x8C),
                     zmem_read32(vt + 0x90), zmem_read32(vt + 0x94),
                     zmem_read32(vt + 0x98), zmem_read32(vt + 0x9C));
                LOGI("DVTBL6 %08X %08X %08X %08X %08X %08X %08X %08X",
                     zmem_read32(vt + 0xA0), zmem_read32(vt + 0xA4),
                     zmem_read32(vt + 0xA8), zmem_read32(vt + 0xAC),
                     zmem_read32(vt + 0xB0), zmem_read32(vt + 0xB4),
                     zmem_read32(vt + 0xB8), zmem_read32(vt + 0xBC));
            }
            hits++;
        }
    }

    /* Trap HLE: chamada de API BREW via endereco magico. Apenas a janela
     * realmente usada por este emulador (ZMEM_HLE_BASE..ZMEM_HLE_END) e
     * valida; PC fora dela (ex: 0xFF000000) nunca veio de um vtable/stub
     * legitimo - e um branch corrompido e deve cair no cheque de
     * descarrilamento abaixo, em vez de ser absorvido silenciosamente
     * como "API nao implementada" (o que mascarava o crash como um loop
     * infinito de warnings). */
    if (pc >= ZMEM_HLE_BASE && pc < ZMEM_HLE_END) {
        if (pc >= ZT_STUB_BASE && pc < ZT_STUB_BASE + 256) {
            uint32_t id = (pc - ZT_STUB_BASE);
            if (id < 256 && g_custom_traps[id]) {
                g_custom_traps[id](pc);
                return;
            }
        }
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
        LOGI("  desc-regs R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X R4=0x%08X "
             "R5=0x%08X R12=0x%08X",
             g_cpu.r[0], g_cpu.r[1], g_cpu.r[2],
             g_cpu.r[3], g_cpu.r[4], g_cpu.r[5], g_cpu.r[12]);
        if (g_cpu.r[5] >= 0x10000000u && g_cpu.r[5] < 0x12000000u) {
            uint32_t r5 = g_cpu.r[5];
            uint32_t r5vt = zmem_read32(r5);
            LOGI("  desc-r5obj@0x%08X: refcnt=0x%08X vtbl=0x%08X vt[0]=0x%08X vt[4]=0x%08X",
                 r5, zmem_read32(r5 + 4), r5vt,
                 zmem_read32(r5vt), zmem_read32(r5vt + 4));
        }
        {
            uint32_t ob = g_cpu.r[2] ? g_cpu.r[2] : g_cpu.r[0];
            if (ob >= 0x10000000u && ob < 0x12000000u) {
                LOGI("  desc-obj@0x%08X: v0=0x%08X v1=0x%08X v2=0x%08X v3=0x%08X",
                     ob, zmem_read32(ob), zmem_read32(ob + 4),
                     zmem_read32(ob + 8), zmem_read32(ob + 12));
                uint32_t vt = zmem_read32(ob);
                if (vt >= 0x10000000u && vt < 0x12000000u)
                    LOGI("  desc-vtbl[0x28]=0x%08X [0x2C]=0x%08X [0x30]=0x%08X",
                         zmem_read32(vt + 0x28), zmem_read32(vt + 0x2C),
                         zmem_read32(vt + 0x30));
            } else {
                uint32_t lm = ob & 0xFFF0;
                LOGI("  desc-mem@0x%08X: %08X %08X %08X %08X | %08X %08X %08X %08X",
                     lm, zmem_read32(lm), zmem_read32(lm + 4),
                     zmem_read32(lm + 8), zmem_read32(lm + 12),
                     zmem_read32(lm + 16), zmem_read32(lm + 20),
                     zmem_read32(lm + 24), zmem_read32(lm + 28));
            }
        }
        {
            /* Diagnostico: conteudo do stack e vizinhanca do LR para
             * rastrear o salto (quem devolveu/carregou o PC ruim). */
            uint32_t sp = g_cpu.r[REG_SP];
            int i;
            for (i = -2; i < 10; i++) {
                uint32_t a = sp + i * 4;
                if (a < 0x10000000u || a >= 0x40000000u) continue;
                LOGI("  desc-stack[%+d]=0x%08X", i, zmem_read32(a));
            }
            {
                uint32_t lr = g_cpu.r[REG_LR];
                if (lr && lr < 0xFF000000u) {
                    uint32_t base = lr & ~3u;
                    uint32_t w[8];
                    for (i = 0; i < 8; i++) w[i] = zmem_read32(base - 8 + i * 4);
                    LOGI("  lr-8[%08X]: %08X %08X %08X %08X | %08X %08X %08X %08X",
                         base - 8, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
                }
            }
        }
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
        if (try_hle_empty_count(instr) || try_hle_word_fill(instr))
            return;
        g_cpu.r[REG_PC] = pc + 8;
        zarm_execute(instr);
        if (!zcpu_branched)
            g_cpu.r[REG_PC] = pc + 4;
    }
    g_cpu.executed++;
}

/* HLE de busy-wait vazio ARMCC (sem side-effect de memoria):
 *   CMP Ra,Rb; ADDCOND Rc,Rc,#1; BCOND -4
 * Zuma: 91B14 (cmp r3,r2 / addhi r2 / bhi) chamado de 1E8DC/0DBE8/0F328
 *       e 92C40 (cmp r0,r5 / addlt r0 / blt) apos memset via GOT.
 * Completar o contador — NAO zerar o limite (cap cega). */
#if defined(_MSC_VER)
#pragma optimize("", off)
#endif
static int try_hle_empty_count(uint32_t instr) {
    uint32_t pc, w0, w1, w2, a, b, n;
    pc = g_cpu.r[REG_PC] & ~3u;
    w0 = instr;
    w1 = zmem_read32(pc + 4u);
    w2 = zmem_read32(pc + 8u);
    /* Mid-loop: PC no ADD ou no B — recua ate o CMP. */
    if (w0 == 0x82822001u || w0 == 0xB2800001u) {
        uint32_t prev = zmem_read32(pc - 4u);
        if (prev == 0xE1530002u || prev == 0xE1500005u) {
            pc -= 4u;
            w2 = w1; w1 = w0; w0 = prev;
        }
    } else if (w0 == 0x8AFFFFFCu || w0 == 0xBAFFFFFCu) {
        uint32_t p1 = zmem_read32(pc - 4u);
        uint32_t p0 = zmem_read32(pc - 8u);
        if ((p0 == 0xE1530002u && p1 == 0x82822001u) ||
            (p0 == 0xE1500005u && p1 == 0xB2800001u)) {
            pc -= 8u;
            w0 = p0; w1 = p1; w2 = zmem_read32(pc + 8u);
        }
    }
    /* cmp r3,r2; addhi r2,r2,#1; bhi -4  →  r2 = r3 */
    if (w0 == 0xE1530002u && w1 == 0x82822001u && w2 == 0x8AFFFFFCu) {
        a = g_cpu.r[3];
        b = g_cpu.r[2];
        if (b >= a) return 0;
        n = a - b;
        if (n > 0x1000000u) n = 0x1000000u;
        g_cpu.r[2] = a;
        g_cpu.cpsr = (g_cpu.cpsr | CPSR_Z | CPSR_C) & ~(CPSR_N | CPSR_V);
        zcpu_write_pc(pc + 12u);
        g_cpu.executed += (uint64_t)n * 3u + 3u;
        {
            static uint32_t logs = 0;
            if (logs < 12)
                LOGI("HLE empty-count HI pc=0x%08X n=%u r3=%u LR=0x%08X",
                     pc, n, a, g_cpu.r[REG_LR]);
            logs++;
        }
        return 1;
    }
    /* cmp r0,r5; addlt r0,r0,#1; blt -4  →  r0 = r5 */
    if (w0 == 0xE1500005u && w1 == 0xB2800001u && w2 == 0xBAFFFFFCu) {
        a = g_cpu.r[0];
        b = g_cpu.r[5];
        if ((int32_t)a >= (int32_t)b) return 0;
        n = b - a;
        if (n > 0x1000000u) n = 0x1000000u;
        g_cpu.r[0] = b;
        g_cpu.cpsr = (g_cpu.cpsr | CPSR_Z | CPSR_C) & ~(CPSR_N | CPSR_V);
        zcpu_write_pc(pc + 12u);
        g_cpu.executed += (uint64_t)n * 3u + 3u;
        {
            static uint32_t logs = 0;
            if (logs < 12)
                LOGI("HLE empty-count LT pc=0x%08X n=%u r5=%u LR=0x%08X",
                     pc, n, b, g_cpu.r[REG_LR]);
            logs++;
        }
        return 1;
    }
    return 0;
}

/* HLE de fill guest: padrao ARMCC comum
 *   LDR r2,[r4,#4]; STR r1,[r2,r0,LSL#2]; ADD r0,#1; CMP r0,#imm; BLT
 * FIFA 09 (e similares) zeram milhares de words assim. Sem HLE o budget
 * de 1M instr/frame nao completa o clear em 45s do smoke.
 *
 * MSVC /O2 ja apagou o corpo desta funcao (dead-code falso); manter
 * optimize off + words[] spill explicito. */
static int try_hle_word_fill(uint32_t instr) {
    uint32_t pc, w0, w1, w2, w3, w4, base, idx, limit, dst, bytes, n, k, v;
    uint32_t *host;
    void *p;
    int32_t off;
    uint32_t imm8, rot;

    pc = g_cpu.r[REG_PC] & ~3u;
    w0 = instr;
    /* Mid-loop: PC no STR — recua 4. */
    if (w0 == 0xE7821100u) {
        uint32_t prev = zmem_read32(pc - 4u);
        if (prev == 0xE5942004u) {
            pc -= 4u;
            w0 = prev;
        }
    }
    w1 = zmem_read32(pc + 4u);
    w2 = zmem_read32(pc + 8u);
    w3 = zmem_read32(pc + 12u);
    w4 = zmem_read32(pc + 16u);
    /* Exact: ldr r2,[r4,#4]. Mask 0xFFFF0FFF zera Rd — nao comparar com
     * E5942004 (Rd=2 ainda setado); use igualdade plena. */
    if (w0 != 0xE5942004u) return 0;
    if (w1 != 0xE7821100u) return 0;
    if (w2 != 0xE2800001u) return 0;
    if ((w3 & 0xFFFFF000u) != 0xE3500000u) return 0;
    if ((w4 & 0xFF000000u) != 0xBA000000u) return 0;
    /* Sign-extend imm24 (bits[23:0]). */
    off = (int32_t)(((w4 & 0x00FFFFFFu) ^ 0x00800000u) - 0x00800000u);
    if (off != -6) return 0;
    imm8 = w3 & 0xFFu;
    rot = ((w3 >> 8) & 0xFu) * 2u;
    if (rot == 0u)
        limit = imm8;
    else
        limit = (imm8 >> rot) | (imm8 << (32u - rot));
    base = zmem_read32(g_cpu.r[4] + 4u);
    idx = g_cpu.r[0];
    if (!base || idx >= limit) return 0;
    if (limit - idx > 0x100000u) return 0;
    dst = base + idx * 4u;
    bytes = (limit - idx) * 4u;
    n = limit - idx;
    v = g_cpu.r[1];
    p = (!zbrew_ifile_block_write(dst, bytes)) ? zmem_host_ptr(dst, bytes)
                                               : NULL;
    if (p) {
        host = (uint32_t *)p;
        for (k = 0; k < n; k++) host[k] = v;
    } else {
        for (k = 0; k < n; k++) zmem_write32(dst + k * 4u, v);
    }
    g_cpu.r[0] = limit;
    g_cpu.r[2] = base;
    g_cpu.cpsr = (g_cpu.cpsr | CPSR_Z | CPSR_C) & ~(CPSR_N | CPSR_V);
    zcpu_write_pc(pc + 20u);
    g_cpu.executed += (uint64_t)n * 5u + 5u;
    {
        static uint32_t logs = 0;
        if (logs < 8) {
            LOGI("HLE word-fill: base=0x%08X idx=%u limit=%u val=0x%08X PC=0x%08X host=%d",
                 base, idx, limit, v, pc, p ? 1 : 0);
            logs++;
        }
    }
    return 1;
}
#if defined(_MSC_VER)
#pragma optimize("", on)
#endif

uint32_t zcpu_run(uint32_t max) {
    uint32_t i;
    for (i = 0; i < max; i++) {
        if (g_cpu.halted) break;
        zcpu_step();
    }
    return i;
}
