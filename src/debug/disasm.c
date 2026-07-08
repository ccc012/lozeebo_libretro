/* disasm.c - Classificacao rapida de instrucoes para logs de debug
 * (Disassembler completo e um TODO da fase de debugging.)
 */
#include <stdint.h>

const char *zdisasm_class_arm(uint32_t instr);

const char *zdisasm_class_arm(uint32_t instr) {
    uint32_t op = (instr >> 25) & 7;
    if ((instr >> 28) == 0xF) return "incondicional";
    switch (op) {
        case 0:
            if ((instr & 0x0FFFFFF0u) == 0x012FFF10u) return "BX";
            if ((instr & 0x0FC000F0u) == 0x00000090u) return "MUL";
            if ((instr & 0x0E000090u) == 0x00000090u) return "LDRH/STRH";
            return "data-proc";
        case 1: return "data-proc imm";
        case 2: case 3: return "LDR/STR";
        case 4: return "LDM/STM";
        case 5: return "B/BL";
        case 6: return "LDC/STC";
        case 7: return (instr & 0x01000000u) ? "SWI" : "coproc";
    }
    return "?";
}
