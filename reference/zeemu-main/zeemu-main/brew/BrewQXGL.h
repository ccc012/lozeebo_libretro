#ifndef ZEEMU_BREW_QXGL_H_
#define ZEEMU_BREW_QXGL_H_

#include "cpu/memory/EndianMemory.h"
#include <string>

class BrewShell;
class CPU;

bool handle_qx_gl_call(const std::string& name, BrewShell& shell, EndianMemory& memory, CPU& cpu, const char* label);
void brew_qxgl_register_tga_payload_hint(addr_t pixels,
                                         uint32_t width,
                                         uint32_t height,
                                         uint32_t bits_per_pixel,
                                         bool origin_top);

#endif
