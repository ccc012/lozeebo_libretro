/* execute_arm.c - Executor de instrucoes ARM 32-bit
 *
 * Cobertura (docs 10_INSTRUCOES_ARM.md):
 *  - Data processing (16 opcodes, imediato / shift imm / shift reg)
 *  - MUL, MLA, UMULL, UMLAL, SMULL, SMLAL
 *  - LDR/STR/LDRB/STRB (pre/post-index, offset imm/reg escalado)
 *  - LDRH/STRH/LDRSB/LDRSH, LDRD/STRD
 *  - LDM/STM (todos os modos de enderecamento, write-back)
 *  - B, BL, BX, BLX (reg e imediato), CLZ
 *  - MRS/MSR (flags apenas), SWI (logado, tratado como nop)
 *
 * Convencao do loop (cpu.c): durante a execucao R15 = pc + 8.
 */
#include "cpu.h"
#include "decode.h"
#include "../memory/memory.h"
#include "../debug/log.h"

#define BIT(x, n) (((x) >> (n)) & 1u)

/* Le registrador (R15 ja esta em pc+8 durante a execucao) */
static uint32_t rget(int r) { return g_cpu.r[r & 15]; }

/* Escreve registrador; escrita em R15 e branch */
static void rset(int r, uint32_t v) {
    r &= 15;
    if (r == REG_PC) zcpu_write_pc(v);
    else g_cpu.r[r] = v;
}

/* Operando 2 do data processing. Atualiza *carry com o carry-out. */
static uint32_t operand2(uint32_t instr, bool *carry) {
    if (BIT(instr, 25)) {
        /* imediato: imm8 rotacionado por rot*2 */
        uint32_t imm = instr & 0xFF;
        uint32_t rot = ((instr >> 8) & 0xF) * 2;
        if (rot == 0) return imm;
        uint32_t val = (imm >> rot) | (imm << (32 - rot));
        *carry = (val >> 31) & 1;
        return val;
    }
    /* registrador com shift */
    uint32_t rm = rget(instr & 0xF);
    int type = (instr >> 5) & 3;
    if (BIT(instr, 4)) {
        /* shift por registrador: Rm lido como pc+12 se for PC (raro; ignora) */
        uint32_t amount = rget((instr >> 8) & 0xF) & 0xFF;
        return zshift(rm, type, amount, carry, true);
    }
    uint32_t amount = (instr >> 7) & 31;
    return zshift(rm, type, amount, carry, false);
}

/* ---- Data processing ---- */
static void dp_execute(uint32_t instr) {
    uint32_t opcode = (instr >> 21) & 0xF;
    bool s = BIT(instr, 20);
    int rn = (instr >> 16) & 0xF;
    int rd = (instr >> 12) & 0xF;

    bool shifter_carry = (g_cpu.cpsr & CPSR_C) != 0;
    uint32_t op2 = operand2(instr, &shifter_carry);
    uint32_t op1 = rget(rn);
    uint32_t carry_in = (g_cpu.cpsr & CPSR_C) ? 1 : 0;
    uint32_t result = 0;
    bool write_rd = true;
    bool logical = false;

    switch (opcode) {
        case 0x0: result = op1 & op2;  logical = true; break;             /* AND */
        case 0x1: result = op1 ^ op2;  logical = true; break;             /* EOR */
        case 0x2: result = zflags_sub(op1, op2, 1, s); break;             /* SUB */
        case 0x3: result = zflags_sub(op2, op1, 1, s); break;             /* RSB */
        case 0x4: result = zflags_add(op1, op2, 0, s); break;             /* ADD */
        case 0x5: result = zflags_add(op1, op2, carry_in, s); break;      /* ADC */
        case 0x6: result = zflags_sub(op1, op2, carry_in, s); break;      /* SBC */
        case 0x7: result = zflags_sub(op2, op1, carry_in, s); break;      /* RSC */
        case 0x8: result = op1 & op2;  logical = true; write_rd = false; break; /* TST */
        case 0x9: result = op1 ^ op2;  logical = true; write_rd = false; break; /* TEQ */
        case 0xA: result = zflags_sub(op1, op2, 1, true); write_rd = false; break; /* CMP */
        case 0xB: result = zflags_add(op1, op2, 0, true); write_rd = false; break; /* CMN */
        case 0xC: result = op1 | op2;  logical = true; break;             /* ORR */
        case 0xD: result = op2;        logical = true; break;             /* MOV */
        case 0xE: result = op1 & ~op2; logical = true; break;             /* BIC */
        case 0xF: result = ~op2;       logical = true; break;             /* MVN */
    }

    if (logical && (s || !write_rd)) {
        zflags_nz(result);
        if (shifter_carry) g_cpu.cpsr |= CPSR_C;
        else               g_cpu.cpsr &= ~CPSR_C;
        /* V inalterado em operacoes logicas */
    }

    if (write_rd) {
        if (rd == REG_PC && s) {
            /* MOVS PC etc: retorno de excecao - nao suportado em User */
            LOGW("dp: escrita em PC com S=1 ignorando restauracao de modo");
        }
        rset(rd, result);
    }
}

/* ---- Multiplicacao ---- */
static void mul_execute(uint32_t instr) {
    bool s = BIT(instr, 20);
    bool acc = BIT(instr, 21);
    bool lng = BIT(instr, 23);

    if (!lng) {
        int rd = (instr >> 16) & 0xF;
        int rn = (instr >> 12) & 0xF;
        int rs = (instr >> 8) & 0xF;
        int rm = instr & 0xF;
        uint32_t result = rget(rm) * rget(rs);
        if (acc) result += rget(rn);           /* MLA */
        rset(rd, result);
        if (s) zflags_nz(result);
        return;
    }
    /* UMULL/UMLAL/SMULL/SMLAL */
    bool sign = BIT(instr, 22);
    int rdhi = (instr >> 16) & 0xF;
    int rdlo = (instr >> 12) & 0xF;
    int rs = (instr >> 8) & 0xF;
    int rm = instr & 0xF;
    uint64_t result;
    if (sign)
        result = (uint64_t)((int64_t)(int32_t)rget(rm) * (int32_t)rget(rs));
    else
        result = (uint64_t)rget(rm) * (uint64_t)rget(rs);
    if (acc)
        result += ((uint64_t)rget(rdhi) << 32) | rget(rdlo);
    rset(rdlo, (uint32_t)result);
    rset(rdhi, (uint32_t)(result >> 32));
    if (s) {
        g_cpu.cpsr &= ~(CPSR_N | CPSR_Z);
        if (result >> 63)  g_cpu.cpsr |= CPSR_N;
        if (result == 0)   g_cpu.cpsr |= CPSR_Z;
    }
}

/* ---- Load/Store palavra e byte ---- */
static void ldst_execute(uint32_t instr) {
    bool imm_off = !BIT(instr, 25);
    bool pre     = BIT(instr, 24);
    bool up      = BIT(instr, 23);
    bool byte    = BIT(instr, 22);
    bool wb      = BIT(instr, 21);
    bool load    = BIT(instr, 20);
    int rn = (instr >> 16) & 0xF;
    int rd = (instr >> 12) & 0xF;

    uint32_t offset;
    if (imm_off) {
        offset = instr & 0xFFF;
    } else {
        bool c = (g_cpu.cpsr & CPSR_C) != 0;
        offset = zshift(rget(instr & 0xF), (instr >> 5) & 3,
                        (instr >> 7) & 31, &c, false);
    }

    uint32_t base = rget(rn);
    uint32_t addr = pre ? (up ? base + offset : base - offset) : base;

    if (load) {
        uint32_t val = byte ? zmem_read8(addr) : zmem_read32(addr & ~3u);
        /* write-back antes de escrever Rd (LDR Rd,[Rn]! com Rd==Rn: Rd vence) */
        if (!pre)      rset(rn, up ? base + offset : base - offset);
        else if (wb)   rset(rn, addr);
        if (rd == REG_PC) zcpu_bx(val | ((val & 1) ? 1 : 0)); /* interworking */
        else rset(rd, val);
    } else {
        uint32_t val = rget(rd);
        if (rd == REG_PC) val += 4; /* STR PC guarda pc+12 */
        if (byte) zmem_write8(addr, (uint8_t)val);
        else      zmem_write32(addr & ~3u, val);
        if (!pre)    rset(rn, up ? base + offset : base - offset);
        else if (wb) rset(rn, addr);
    }
}

/* ---- Load/Store halfword, signed byte/halfword, doubleword ---- */
static void ldst_ext_execute(uint32_t instr) {
    bool pre  = BIT(instr, 24);
    bool up   = BIT(instr, 23);
    bool immf = BIT(instr, 22);
    bool wb   = BIT(instr, 21);
    bool load = BIT(instr, 20);
    int rn = (instr >> 16) & 0xF;
    int rd = (instr >> 12) & 0xF;
    int sh = (instr >> 5) & 3;   /* 1=H, 2=SB(D), 3=SH(D) */

    uint32_t offset = immf ? (((instr >> 4) & 0xF0) | (instr & 0xF))
                           : rget(instr & 0xF);
    uint32_t base = rget(rn);
    uint32_t addr = pre ? (up ? base + offset : base - offset) : base;
    uint32_t final_base = pre ? addr : (up ? base + offset : base - offset);

    if (load) {
        uint32_t val = 0;
        switch (sh) {
            case 1: val = zmem_read16(addr & ~1u); break;              /* LDRH */
            case 2: val = (uint32_t)(int32_t)(int8_t)zmem_read8(addr); break; /* LDRSB */
            case 3: val = (uint32_t)(int32_t)(int16_t)zmem_read16(addr & ~1u); break; /* LDRSH */
        }
        if (!pre || wb) rset(rn, final_base);
        rset(rd, val);
    } else {
        switch (sh) {
            case 1: /* STRH */
                zmem_write16(addr & ~1u, (uint16_t)rget(rd));
                if (!pre || wb) rset(rn, final_base);
                break;
            case 2: /* LDRD (ARMv5TE: L=0, SH=10) */
                {
                    uint32_t lo = zmem_read32(addr & ~3u);
                    uint32_t hi = zmem_read32((addr + 4) & ~3u);
                    if (!pre || wb) rset(rn, final_base);
                    rset(rd, lo);
                    rset(rd + 1, hi);
                }
                break;
            case 3: /* STRD */
                zmem_write32(addr & ~3u, rget(rd));
                zmem_write32((addr + 4) & ~3u, rget(rd + 1));
                if (!pre || wb) rset(rn, final_base);
                break;
        }
    }
}

/* ---- Load/Store multiplos (LDM/STM) ---- */
static void ldm_stm_execute(uint32_t instr) {
    bool pre  = BIT(instr, 24);
    bool up   = BIT(instr, 23);
    bool sbit = BIT(instr, 22);
    bool wb   = BIT(instr, 21);
    bool load = BIT(instr, 20);
    int rn = (instr >> 16) & 0xF;
    uint32_t list = instr & 0xFFFF;

    if (sbit) LOGW("LDM/STM com bit S (banco de user) - ignorado");

    int count = 0;
    int i;
    for (i = 0; i < 16; i++)
        if (list & (1u << i)) count++;
    if (count == 0) { LOGW("LDM/STM com lista vazia"); return; }

    uint32_t base = rget(rn);
    uint32_t lowest = up ? base : base - count * 4;
    uint32_t addr = lowest + (pre == up ? 4 : 0);
    uint32_t new_base = up ? base + count * 4 : base - count * 4;

    if (load) {
        bool pc_loaded = false;
        uint32_t pc_val = 0;
        if (wb) rset(rn, new_base);
        for (i = 0; i < 16; i++) {
            if (!(list & (1u << i))) continue;
            uint32_t val = zmem_read32(addr & ~3u);
            addr += 4;
            if (i == REG_PC) { pc_loaded = true; pc_val = val; }
            else g_cpu.r[i] = val;
        }
        if (pc_loaded) zcpu_bx(pc_val); /* POP PC com interworking */
    } else {
        for (i = 0; i < 16; i++) {
            if (!(list & (1u << i))) continue;
            uint32_t val = g_cpu.r[i];
            if (i == REG_PC) val += 4; /* STM PC guarda pc+12 */
            zmem_write32(addr & ~3u, val);
            addr += 4;
        }
        if (wb) rset(rn, new_base);
    }
}

/* ---- Branch ---- */
static void branch_execute(uint32_t instr) {
    bool link = BIT(instr, 24);
    int32_t offset = (int32_t)(instr << 8) >> 6; /* imm24 com sinal, <<2 */
    uint32_t pc = g_cpu.r[REG_PC];               /* = instrucao + 8 */
    if (link) g_cpu.r[REG_LR] = pc - 4;
    zcpu_write_pc(pc + offset);
}

void zarm_execute(uint32_t instr) {
    uint32_t cond = instr >> 28;

    if (cond == 0xF) {
        /* Instrucoes incondicionais (BLX imm etc) */
        if ((instr & 0x0E000000u) == 0x0A000000u) {
            /* BLX imediato: muda para Thumb */
            int32_t offset = (int32_t)(instr << 8) >> 6;
            offset |= (int32_t)(BIT(instr, 24) << 1);
            uint32_t pc = g_cpu.r[REG_PC];
            g_cpu.r[REG_LR] = pc - 4;
            g_cpu.cpsr |= CPSR_T;
            zcpu_write_pc(pc + offset);
            return;
        }
        static uint32_t warn_count = 0;
        if (warn_count < 16) {
            LOGW("instrucao incondicional 0x%08X nao implementada (PC=0x%08X)",
                 instr, g_cpu.r[REG_PC] - 8);
            warn_count++;
        }
        return;
    }

    if (!zcpu_cond_pass(cond)) return;

    uint32_t op = (instr >> 25) & 7;

    switch (op) {
    case 0: /* data processing / mul / halfword / BX / misc */
        if ((instr & 0x0FF000F0u) == 0x01200070u) {          /* BKPT */
            LOGW("BKPT ARM 0x%04X em PC=0x%08X", 
                 (unsigned)(((instr >> 4) & 0xFFF0u) | (instr & 0xFu)),
                 g_cpu.r[REG_PC] - 8);
            return;
        }
        if ((instr & 0x0FFFFFF0u) == 0x012FFF10u) {          /* BX  */
            zcpu_bx(rget(instr & 0xF));
            return;
        }
        if ((instr & 0x0FFFFFF0u) == 0x012FFF30u) {          /* BLX reg */
            uint32_t target = rget(instr & 0xF);
            g_cpu.r[REG_LR] = g_cpu.r[REG_PC] - 4;
            zcpu_bx(target);
            return;
        }
        if ((instr & 0x0FFF0FF0u) == 0x016F0F10u) {          /* CLZ */
            uint32_t rm = rget(instr & 0xF);
            uint32_t n = 0;
            if (rm == 0) n = 32;
            else while (!(rm & 0x80000000u)) { rm <<= 1; n++; }
            rset((instr >> 12) & 0xF, n);
            return;
        }
        if ((instr & 0x0FC000F0u) == 0x00000090u) {          /* MUL/MLA */
            mul_execute(instr);
            return;
        }
        if ((instr & 0x0F8000F0u) == 0x00800090u) {          /* xMULL/xMLAL */
            mul_execute(instr);
            return;
        }
        if ((instr & 0x0FB00FF0u) == 0x01000090u) {          /* SWP/SWPB */
            int rn = (instr >> 16) & 0xF;
            int rd = (instr >> 12) & 0xF;
            int rm = instr & 0xF;
            uint32_t addr = rget(rn);
            if (BIT(instr, 22)) {
                uint8_t old = zmem_read8(addr);
                zmem_write8(addr, (uint8_t)rget(rm));
                rset(rd, old);
            } else {
                uint32_t old = zmem_read32(addr & ~3u);
                zmem_write32(addr & ~3u, rget(rm));
                rset(rd, old);
            }
            return;
        }
        if ((instr & 0x0E000090u) == 0x00000090u &&
            ((instr >> 5) & 3) != 0) {                        /* LDRH/STRH/... */
            ldst_ext_execute(instr);
            return;
        }
        if ((instr & 0x0FBF0FFFu) == 0x010F0000u) {          /* MRS Rd, CPSR */
            rset((instr >> 12) & 0xF, g_cpu.cpsr);
            return;
        }
        if ((instr & 0x0FB0F000u) == 0x0120F000u) {          /* MSR CPSR */
            bool imm = BIT(instr, 25);
            bool c_ok = (g_cpu.cpsr & CPSR_C) != 0;
            uint32_t val = imm ? operand2(instr, &c_ok) : rget(instr & 0xF);
            uint32_t mask = 0;
            if (BIT(instr, 19)) mask |= 0xFF000000u; /* flags */
            if (BIT(instr, 16)) {
                LOGW("MSR tentando mudar modo (0x%02X) - ignorado", val & 0x1F);
            }
            g_cpu.cpsr = (g_cpu.cpsr & ~mask) | (val & mask);
            return;
        }
        dp_execute(instr);
        return;

    case 1: /* data processing imediato */
        /* MSR imediato ja coberto acima? Nao - checa aqui tambem */
        if ((instr & 0x0FB0F000u) == 0x0320F000u) {          /* MSR imm */
            bool c_ok = (g_cpu.cpsr & CPSR_C) != 0;
            uint32_t val = operand2(instr, &c_ok);
            uint32_t mask = 0;
            if (BIT(instr, 19)) mask |= 0xFF000000u;
            g_cpu.cpsr = (g_cpu.cpsr & ~mask) | (val & mask);
            return;
        }
        dp_execute(instr);
        return;

    case 2: /* LDR/STR offset imediato */
    case 3: /* LDR/STR offset registrador */
        if (op == 3 && BIT(instr, 4)) {
            /* ARMv6: SXTB/SXTH/UXTB/UXTH (sem acumulacao, Rn=PC).
             * Alguns modulos ARMCC usam estas instrucoes ja no AEEMod_Load. */
            if ((instr & 0x0FAF03F0u) == 0x06AF0070u) {
                uint32_t value = rget(instr & 0xFu);
                uint32_t rotate = ((instr >> 10) & 3u) * 8u;
                int rd = (instr >> 12) & 0xFu;
                bool is_unsigned = BIT(instr, 22) != 0;
                bool is_halfword = BIT(instr, 20) != 0;
                if (rotate)
                    value = (value >> rotate) | (value << (32u - rotate));
                if (is_halfword) {
                    value &= 0xFFFFu;
                    if (!is_unsigned && (value & 0x8000u))
                        value |= 0xFFFF0000u;
                } else {
                    value &= 0xFFu;
                    if (!is_unsigned && (value & 0x80u))
                        value |= 0xFFFFFF00u;
                }
                rset(rd, value);
                return;
            }
            static uint32_t warn_count = 0;
            if (warn_count < 16) {
                LOGW("instrucao de midia/indefinida 0x%08X (PC=0x%08X)",
                     instr, g_cpu.r[REG_PC] - 8);
                warn_count++;
            }
            return;
        }
        ldst_execute(instr);
        return;

    case 4: /* LDM/STM */
        ldm_stm_execute(instr);
        return;

    case 5: /* B/BL */
        branch_execute(instr);
        return;

    case 6: /* coprocessador LDC/STC */
        {
            static uint32_t warn_count = 0;
            if (warn_count < 8) {
                LOGW("coprocessador LDC/STC 0x%08X ignorado", instr);
                warn_count++;
            }
        }
        return;

    case 7:
        if (BIT(instr, 24)) {
            /* SWI - por decisao de escopo, logado e tratado como nop */
            static uint32_t warn_count = 0;
            if (warn_count < 8) {
                LOGW("SWI 0x%06X em PC=0x%08X (nao usado no trap HLE)",
                     instr & 0xFFFFFF, g_cpu.r[REG_PC] - 8);
                warn_count++;
            }
            return;
        }
        {
            static uint32_t warn_count = 0;
            if (warn_count < 8) {
                LOGW("coprocessador CDP/MCR/MRC 0x%08X ignorado", instr);
                warn_count++;
            }
        }
        return;
    }
}
