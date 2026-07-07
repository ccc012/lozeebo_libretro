/* cpu.h - CPU ARM emulada (ARM11 / ARMv6, modo User apenas)
 *
 * Decisoes de escopo (docs 02/04):
 *  - Apenas modo User, um banco unico de 16 registradores
 *  - Sem IRQ/FIQ reais (HLE substitui o SO)
 *  - Trap de APIs BREW: PC >= 0xF0000000 chama o handler registrado
 */
#ifndef ZEEBO_CPU_H
#define ZEEBO_CPU_H

#include <stdint.h>
#include <stdbool.h>

/* Flags do CPSR */
#define CPSR_N (1u << 31)
#define CPSR_Z (1u << 30)
#define CPSR_C (1u << 29)
#define CPSR_V (1u << 28)
#define CPSR_T (1u << 5)   /* Thumb */

#define REG_SP 13
#define REG_LR 14
#define REG_PC 15

typedef struct {
    uint32_t r[16];       /* R0-R15 (R15 = PC) */
    uint32_t cpsr;
    uint64_t executed;    /* instrucoes executadas (estatistica) */
    bool     halted;      /* CPU parada (applet terminou / erro fatal) */
} zcpu_t;

extern zcpu_t g_cpu;

/* Handler de trap HLE: chamado quando PC entra na regiao 0xF0000000+.
 * O handler emula a API e deve ajustar o PC (normalmente PC = LR). */
typedef void (*zcpu_trap_fn)(uint32_t addr);
void zcpu_set_trap_handler(zcpu_trap_fn fn);

void zcpu_reset(uint32_t entry, uint32_t sp, uint32_t lr);
void zcpu_step(void);
/* Executa ate max instrucoes (retorna quantas rodou; para se halted) */
uint32_t zcpu_run(uint32_t max);

/* Escrita de PC com deteccao de branch (uso interno dos executores) */
void zcpu_write_pc(uint32_t value);
/* Escrita de PC com interworking ARM<->Thumb (BX/BLX/LDM PC/POP PC) */
void zcpu_bx(uint32_t value);
/* True se a instrucao corrente fez branch (uso interno do step) */
extern bool zcpu_branched;

/* ---- flags.c ---- */
bool zcpu_cond_pass(uint32_t cond);
void zflags_nz(uint32_t result);
/* Soma com flags: result = a + b + carry_in; se set_flags, atualiza NZCV */
uint32_t zflags_add(uint32_t a, uint32_t b, uint32_t carry_in, bool set_flags);
/* Subtracao: result = a - b - !carry_in (carry_in=1 -> sem borrow) */
uint32_t zflags_sub(uint32_t a, uint32_t b, uint32_t carry_in, bool set_flags);

/* ---- executores ---- */
void zarm_execute(uint32_t instr);
void zthumb_execute(uint16_t instr);

#endif /* ZEEBO_CPU_H */
