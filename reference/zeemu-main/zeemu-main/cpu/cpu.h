#ifndef ZEEMU_CPU_H
#define ZEEMU_CPU_H

#include <cstdint>

#if defined(__GNUC__) && __GNUC__ >= 4
#   define ZEEMU_EXPORT __attribute__ ((visibility("default")))
#elif defined(_MSC_VER)
#   if defined(Zeemu_EXPORTS)
#       define ZEEMU_EXPORT __declspec(dllexport)
#   else
#       define ZEEMU_EXPORT __declspec(dllimport)
#   endif
#else
#   define ZEEMU_EXPORT
#endif

typedef uint32_t ARM_Word;
typedef uint32_t addr_t;

enum Endianness {
    LittleEndian,
    BigEndian
};

#define runtime_platform_endianness LittleEndian

enum CPUReg {
    REG_R0, REG_R1, REG_R2, REG_R3, REG_R4, REG_R5, REG_R6, REG_R7,
    REG_R8, REG_R9, REG_R10, REG_R11, REG_R12, REG_SP, REG_LR, REG_PC,
    REG_CPSR
};

#endif
