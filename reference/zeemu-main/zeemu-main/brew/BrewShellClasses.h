#ifndef ZEEMU_BREW_SHELL_CLASSES_H_
#define ZEEMU_BREW_SHELL_CLASSES_H_

#include <cstdint>

bool is_generic_core_stub_clsid(uint32_t clsId);
bool is_hash_clsid(uint32_t clsId);
bool is_known_applet_clsid(uint32_t clsId);
bool is_font_clsid(uint32_t clsId);

#endif
