#ifndef ZEEMU_BREW_MODULE_H_
#define ZEEMU_BREW_MODULE_H_

#include <string>
#include <vector>
#include "cpu/cpu.h"

struct BrewModule {
    addr_t base_address;
    addr_t entry_point;
    addr_t mod_info_point;
    uint32_t code_size;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t header_size;
    uint32_t image_size;
    bool is_raw;
    std::string name;
};

#endif
