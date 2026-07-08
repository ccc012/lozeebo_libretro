// decode_arm.c — Decode ARM instruction encodings for debugging
// Usage: g++ decode_arm.c -o decode_arm.exe && ./decode_arm.exe <hex_opcode>
//
// Breaks down an ARM instruction into its component fields:
// condition, format group, coprocessor, registers, etc.

#include <cstdio>
#include <cstdlib>

int main(int argc, char* argv[]) {
    unsigned op = 0xE6E53013; // default
    if (argc > 1) op = strtoul(argv[1], nullptr, 16);

    printf("Opcode: 0x%08X\n", op);
    printf("Condition: %d (AL=%d)\n", (op>>28)&0xF, ((op>>28)&0xF)==14);
    printf("Bits 27-20: 0x%02X\n", (op>>20)&0xFF);
    printf("Bits 7-4: 0x%X\n", (op>>4)&0xF);
    printf("Bits 27-25: %d\n", (op>>25)&7);
    printf("Bit 4: %d\n", (op>>4)&1);

    // Coprocessor instructions
    if (((op>>24)&0xF) == 0xC && ((op>>4)&1) == 1) {
        printf("-> MRRC/MCRR (coprocessor register transfer)\n");
        printf("   Coprocessor: %d\n", (op>>8)&0xF);
        printf("   opc: %d\n", (op>>21)&7);
        printf("   Rd: %d\n", (op>>12)&0xF);
        printf("   Rn: %d\n", (op>>16)&0xF);
        printf("   CRm: %d\n", op&0xF);
    }

    // LDC/STC (coprocessor load/store)
    if (((op>>24)&0xF) == 0xC && ((op>>4)&1) == 0) {
        printf("-> LDC/STC (coprocessor memory transfer)\n");
        printf("   Coprocessor: %d\n", (op>>8)&0xF);
        printf("   D=%d (0=STC, 1=LDC)\n", (op>>22)&1);
        printf("   Rn: %d\n", (op>>16)&0xF);
        printf("   CRd: %d\n", (op>>12)&0xF);
    }

    // CDP (coprocessor data processing)
    if (((op>>24)&0xF) == 0xE && ((op>>4)&1) == 0) {
        printf("-> CDP (coprocessor data processing)\n");
        printf("   Coprocessor: %d\n", (op>>8)&0xF);
    }

    // MRC/MCR (coprocessor register transfer to/from ARM)
    if (((op>>24)&0xF) == 0xE && ((op>>4)&1) == 1) {
        printf("-> MRC/MCR (coprocessor register transfer)\n");
        printf("   Coprocessor: %d\n", (op>>8)&0xF);
        printf("   D=%d (0=MCR, 1=MRC)\n", (op>>20)&1);
    }

    return 0;
}
