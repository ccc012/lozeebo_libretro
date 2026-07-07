/* execute_thumb.c - Executor de instrucoes Thumb 16-bit
 *
 * Todos os 19 formatos classicos + extensoes ARMv6 comuns
 * (SXTB/SXTH/UXTB/UXTH/REV) que compiladores BREW emitem.
 *
 * Convencao do loop (cpu.c): durante a execucao R15 = pc + 4.
 */
#include "cpu.h"
#include "decode.h"
#include "../memory/memory.h"
#include "../debug/log.h"

#define BIT(x, n) (((x) >> (n)) & 1u)

static uint32_t rget(int r) { return g_cpu.r[r & 15]; }
static void rset(int r, uint32_t v) {
    r &= 15;
    if (r == REG_PC) zcpu_write_pc(v);
    else g_cpu.r[r] = v;
}

static void set_carry(bool c) {
    if (c) g_cpu.cpsr |= CPSR_C;
    else   g_cpu.cpsr &= ~CPSR_C;
}

void zthumb_execute(uint16_t instr) {
    /* Formato 19: BL/BLX (dois halfwords) */
    if ((instr & 0xF000) == 0xF000) {
        uint32_t h = (instr >> 11) & 3;
        if (h == 2) {
            /* prefixo: LR = PC + (offset com sinal << 12) */
            int32_t off = (int32_t)((uint32_t)(instr & 0x7FF) << 21) >> 9;
            g_cpu.r[REG_LR] = g_cpu.r[REG_PC] + off;
            return;
        }
        if (h == 3 || h == 1) {
            /* sufixo BL (h=3) ou BLX (h=1, volta pra ARM) */
            uint32_t target = g_cpu.r[REG_LR] + ((instr & 0x7FF) << 1);
            uint32_t ret = (g_cpu.r[REG_PC] - 2) | 1; /* proxima instr | Thumb */
            g_cpu.r[REG_LR] = ret;
            if (h == 1) {
                g_cpu.cpsr &= ~CPSR_T;
                zcpu_write_pc(target & ~3u);
            } else {
                zcpu_write_pc(target);
            }
            return;
        }
    }

    /* Formato 18: B incondicional */
    if ((instr & 0xF800) == 0xE000) {
        int32_t off = (int32_t)((uint32_t)(instr & 0x7FF) << 21) >> 20;
        zcpu_write_pc(g_cpu.r[REG_PC] + off);
        return;
    }

    /* Formato 17: SWI */
    if ((instr & 0xFF00) == 0xDF00) {
        LOGW("SWI Thumb 0x%02X em PC=0x%08X", instr & 0xFF,
             g_cpu.r[REG_PC] - 4);
        return;
    }

    /* Formato 16: branch condicional */
    if ((instr & 0xF000) == 0xD000) {
        uint32_t cond = (instr >> 8) & 0xF;
        if (zcpu_cond_pass(cond)) {
            int32_t off = (int32_t)(int8_t)(instr & 0xFF) << 1;
            zcpu_write_pc(g_cpu.r[REG_PC] + off);
        }
        return;
    }

    /* Formato 15: LDMIA/STMIA */
    if ((instr & 0xF000) == 0xC000) {
        bool load = BIT(instr, 11);
        int rb = (instr >> 8) & 7;
        uint32_t list = instr & 0xFF;
        uint32_t addr = rget(rb);
        int i, count = 0;
        for (i = 0; i < 8; i++) if (list & (1u << i)) count++;
        if (count == 0) { LOGW("LDM/STM Thumb lista vazia"); return; }
        bool rb_in_list = (list & (1u << rb)) != 0;
        for (i = 0; i < 8; i++) {
            if (!(list & (1u << i))) continue;
            if (load) g_cpu.r[i] = zmem_read32(addr & ~3u);
            else      zmem_write32(addr & ~3u, g_cpu.r[i]);
            addr += 4;
        }
        /* write-back exceto LDMIA com Rb na lista */
        if (!(load && rb_in_list)) g_cpu.r[rb] = addr;
        return;
    }

    /* Formato 14: PUSH/POP */
    if ((instr & 0xF600) == 0xB400) {
        bool load = BIT(instr, 11);     /* 1 = POP */
        bool r_bit = BIT(instr, 8);     /* PUSH: LR / POP: PC */
        uint32_t list = instr & 0xFF;
        int i;
        if (load) {
            uint32_t sp = rget(REG_SP);
            for (i = 0; i < 8; i++) {
                if (!(list & (1u << i))) continue;
                g_cpu.r[i] = zmem_read32(sp & ~3u);
                sp += 4;
            }
            if (r_bit) {
                uint32_t pc_val = zmem_read32(sp & ~3u);
                sp += 4;
                g_cpu.r[REG_SP] = sp;
                zcpu_bx(pc_val);        /* POP PC com interworking */
                return;
            }
            g_cpu.r[REG_SP] = sp;
        } else {
            int count = 0;
            for (i = 0; i < 8; i++) if (list & (1u << i)) count++;
            if (r_bit) count++;
            uint32_t sp = rget(REG_SP) - count * 4;
            g_cpu.r[REG_SP] = sp;
            for (i = 0; i < 8; i++) {
                if (!(list & (1u << i))) continue;
                zmem_write32(sp & ~3u, g_cpu.r[i]);
                sp += 4;
            }
            if (r_bit) zmem_write32(sp & ~3u, g_cpu.r[REG_LR]);
        }
        return;
    }

    /* Extensoes ARMv6 (formato 1011 0010): SXTH/SXTB/UXTH/UXTB */
    if ((instr & 0xFF00) == 0xB200) {
        int op = (instr >> 6) & 3;
        uint32_t rm = rget((instr >> 3) & 7);
        int rd = instr & 7;
        switch (op) {
            case 0: rset(rd, (uint32_t)(int32_t)(int16_t)rm); break; /* SXTH */
            case 1: rset(rd, (uint32_t)(int32_t)(int8_t)rm);  break; /* SXTB */
            case 2: rset(rd, rm & 0xFFFF); break;                    /* UXTH */
            case 3: rset(rd, rm & 0xFF);   break;                    /* UXTB */
        }
        return;
    }

    /* Extensoes ARMv6 (1011 1010): REV/REV16/REVSH */
    if ((instr & 0xFF00) == 0xBA00) {
        int op = (instr >> 6) & 3;
        uint32_t rm = rget((instr >> 3) & 7);
        int rd = instr & 7;
        switch (op) {
            case 0: /* REV */
                rset(rd, ((rm & 0xFF) << 24) | ((rm & 0xFF00) << 8) |
                         ((rm >> 8) & 0xFF00) | (rm >> 24));
                break;
            case 1: /* REV16 */
                rset(rd, ((rm & 0xFF) << 8) | ((rm >> 8) & 0xFF) |
                         ((rm & 0xFF0000) << 8) | ((rm >> 8) & 0xFF0000));
                break;
            case 3: /* REVSH */
                rset(rd, (uint32_t)(int32_t)(int16_t)(((rm & 0xFF) << 8) |
                                                      ((rm >> 8) & 0xFF)));
                break;
            default:
                LOGW("Thumb 0x%04X nao implementado", instr);
        }
        return;
    }

    /* Formato 13: ADD/SUB SP, #imm */
    if ((instr & 0xFF00) == 0xB000) {
        uint32_t off = (instr & 0x7F) << 2;
        if (BIT(instr, 7)) g_cpu.r[REG_SP] -= off;
        else               g_cpu.r[REG_SP] += off;
        return;
    }

    /* Formato 12: ADD Rd, PC/SP, #imm (load address) */
    if ((instr & 0xF000) == 0xA000) {
        int rd = (instr >> 8) & 7;
        uint32_t off = (instr & 0xFF) << 2;
        if (BIT(instr, 11)) rset(rd, rget(REG_SP) + off);
        else                rset(rd, (g_cpu.r[REG_PC] & ~3u) + off);
        return;
    }

    /* Formato 11: LDR/STR Rd, [SP, #imm] */
    if ((instr & 0xF000) == 0x9000) {
        int rd = (instr >> 8) & 7;
        uint32_t addr = rget(REG_SP) + ((instr & 0xFF) << 2);
        if (BIT(instr, 11)) rset(rd, zmem_read32(addr & ~3u));
        else                zmem_write32(addr & ~3u, rget(rd));
        return;
    }

    /* Formato 10: LDRH/STRH imediato */
    if ((instr & 0xF000) == 0x8000) {
        int rd = instr & 7;
        int rb = (instr >> 3) & 7;
        uint32_t addr = rget(rb) + (((instr >> 6) & 31) << 1);
        if (BIT(instr, 11)) rset(rd, zmem_read16(addr & ~1u));
        else                zmem_write16(addr & ~1u, (uint16_t)rget(rd));
        return;
    }

    /* Formato 9: LDR/STR/LDRB/STRB imediato */
    if ((instr & 0xE000) == 0x6000) {
        bool byte = BIT(instr, 12);
        bool load = BIT(instr, 11);
        int rd = instr & 7;
        int rb = (instr >> 3) & 7;
        uint32_t off5 = (instr >> 6) & 31;
        if (byte) {
            uint32_t addr = rget(rb) + off5;
            if (load) rset(rd, zmem_read8(addr));
            else      zmem_write8(addr, (uint8_t)rget(rd));
        } else {
            uint32_t addr = rget(rb) + (off5 << 2);
            if (load) rset(rd, zmem_read32(addr & ~3u));
            else      zmem_write32(addr & ~3u, rget(rd));
        }
        return;
    }

    /* Formatos 7/8: load/store com offset em registrador */
    if ((instr & 0xF000) == 0x5000) {
        int rd = instr & 7;
        uint32_t addr = rget((instr >> 3) & 7) + rget((instr >> 6) & 7);
        switch ((instr >> 9) & 7) {
            case 0: zmem_write32(addr & ~3u, rget(rd)); break;          /* STR   */
            case 1: zmem_write16(addr & ~1u, (uint16_t)rget(rd)); break;/* STRH  */
            case 2: zmem_write8(addr, (uint8_t)rget(rd)); break;        /* STRB  */
            case 3: rset(rd, (uint32_t)(int32_t)(int8_t)zmem_read8(addr)); break; /* LDRSB */
            case 4: rset(rd, zmem_read32(addr & ~3u)); break;           /* LDR   */
            case 5: rset(rd, zmem_read16(addr & ~1u)); break;           /* LDRH  */
            case 6: rset(rd, zmem_read8(addr)); break;                  /* LDRB  */
            case 7: rset(rd, (uint32_t)(int32_t)(int16_t)zmem_read16(addr & ~1u)); break; /* LDRSH */
        }
        return;
    }

    /* Formato 6: LDR Rd, [PC, #imm] (literal pool) */
    if ((instr & 0xF800) == 0x4800) {
        int rd = (instr >> 8) & 7;
        uint32_t addr = (g_cpu.r[REG_PC] & ~3u) + ((instr & 0xFF) << 2);
        rset(rd, zmem_read32(addr));
        return;
    }

    /* Formato 5: operacoes com registradores altos / BX */
    if ((instr & 0xFC00) == 0x4400) {
        int op = (instr >> 8) & 3;
        int rs = (instr >> 3) & 0xF;              /* H2:Rs */
        int rd = (instr & 7) | ((instr >> 4) & 8); /* H1:Rd */
        uint32_t vs = rget(rs);
        switch (op) {
            case 0: /* ADD (sem flags) */
                if (rd == REG_PC) zcpu_write_pc(g_cpu.r[REG_PC] + vs);
                else rset(rd, rget(rd) + vs);
                break;
            case 1: /* CMP */
                zflags_sub(rget(rd), vs, 1, true);
                break;
            case 2: /* MOV (sem flags) */
                rset(rd, vs);
                break;
            case 3: /* BX / BLX */
                if (BIT(instr, 7)) /* BLX */
                    g_cpu.r[REG_LR] = (g_cpu.r[REG_PC] - 2) | 1;
                zcpu_bx(vs);
                break;
        }
        return;
    }

    /* Formato 4: ALU */
    if ((instr & 0xFC00) == 0x4000) {
        int op = (instr >> 6) & 0xF;
        int rs = (instr >> 3) & 7;
        int rd = instr & 7;
        uint32_t a = rget(rd), b = rget(rs);
        bool c = (g_cpu.cpsr & CPSR_C) != 0;
        uint32_t r;
        switch (op) {
            case 0x0: r = a & b; zflags_nz(r); rset(rd, r); break;          /* AND */
            case 0x1: r = a ^ b; zflags_nz(r); rset(rd, r); break;          /* EOR */
            case 0x2: r = zshift(a, ZSHIFT_LSL, b & 0xFF, &c, true);        /* LSL */
                      zflags_nz(r); set_carry(c); rset(rd, r); break;
            case 0x3: r = zshift(a, ZSHIFT_LSR, b & 0xFF, &c, true);        /* LSR */
                      zflags_nz(r); set_carry(c); rset(rd, r); break;
            case 0x4: r = zshift(a, ZSHIFT_ASR, b & 0xFF, &c, true);        /* ASR */
                      zflags_nz(r); set_carry(c); rset(rd, r); break;
            case 0x5: r = zflags_add(a, b, c ? 1 : 0, true); rset(rd, r); break; /* ADC */
            case 0x6: r = zflags_sub(a, b, c ? 1 : 0, true); rset(rd, r); break; /* SBC */
            case 0x7: r = zshift(a, ZSHIFT_ROR, b & 0xFF, &c, true);        /* ROR */
                      zflags_nz(r); set_carry(c); rset(rd, r); break;
            case 0x8: r = a & b; zflags_nz(r); break;                       /* TST */
            case 0x9: r = zflags_sub(0, b, 1, true); rset(rd, r); break;    /* NEG */
            case 0xA: zflags_sub(a, b, 1, true); break;                     /* CMP */
            case 0xB: zflags_add(a, b, 0, true); break;                     /* CMN */
            case 0xC: r = a | b; zflags_nz(r); rset(rd, r); break;          /* ORR */
            case 0xD: r = a * b; zflags_nz(r); rset(rd, r); break;          /* MUL */
            case 0xE: r = a & ~b; zflags_nz(r); rset(rd, r); break;         /* BIC */
            case 0xF: r = ~b; zflags_nz(r); rset(rd, r); break;             /* MVN */
        }
        return;
    }

    /* Formato 3: MOV/CMP/ADD/SUB imediato de 8 bits */
    if ((instr & 0xE000) == 0x2000) {
        int op = (instr >> 11) & 3;
        int rd = (instr >> 8) & 7;
        uint32_t imm = instr & 0xFF;
        switch (op) {
            case 0: rset(rd, imm); zflags_nz(imm); break;                  /* MOV */
            case 1: zflags_sub(rget(rd), imm, 1, true); break;             /* CMP */
            case 2: rset(rd, zflags_add(rget(rd), imm, 0, true)); break;   /* ADD */
            case 3: rset(rd, zflags_sub(rget(rd), imm, 1, true)); break;   /* SUB */
        }
        return;
    }

    /* Formato 2: ADD/SUB registrador ou imediato de 3 bits */
    if ((instr & 0xF800) == 0x1800) {
        bool imm = BIT(instr, 10);
        bool sub = BIT(instr, 9);
        uint32_t b = imm ? ((instr >> 6) & 7) : rget((instr >> 6) & 7);
        uint32_t a = rget((instr >> 3) & 7);
        int rd = instr & 7;
        uint32_t r = sub ? zflags_sub(a, b, 1, true)
                         : zflags_add(a, b, 0, true);
        rset(rd, r);
        return;
    }

    /* Formato 1: shift por imediato (LSL/LSR/ASR) */
    if ((instr & 0xE000) == 0x0000) {
        int op = (instr >> 11) & 3;
        uint32_t amount = (instr >> 6) & 31;
        uint32_t val = rget((instr >> 3) & 7);
        int rd = instr & 7;
        bool c = (g_cpu.cpsr & CPSR_C) != 0;
        uint32_t r = zshift(val, op, amount, &c, false);
        zflags_nz(r);
        set_carry(c);
        rset(rd, r);
        return;
    }

    LOGW("instrucao Thumb 0x%04X nao implementada (PC=0x%08X)",
         instr, g_cpu.r[REG_PC] - 4);
}
