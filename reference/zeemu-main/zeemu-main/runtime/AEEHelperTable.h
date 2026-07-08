#ifndef ZEEMU_RUNTIME_AEE_HELPER_TABLE_H_
#define ZEEMU_RUNTIME_AEE_HELPER_TABLE_H_

#include <cstdint>

class BrewShell;
class EndianMemory;

uint32_t build_aee_helper_table(BrewShell& shell, EndianMemory& memory);

#endif