#ifndef ZEEMU_BREW_FONT_H_
#define ZEEMU_BREW_FONT_H_

#include "brew/BrewShell.h"
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"
#include <cstdint>
#include <string>
#include <unordered_map>

class BrewFont : public BrewService {
public:
    struct Metrics {
        uint16_t ascent = 12;
        uint16_t descent = 4;
        uint16_t leading = 2;
        uint16_t max_char_width = 8;
        uint16_t height = 16;
        bool bold = false;
        bool italic = false;
    };

    BrewFont(BrewShell& shell, EndianMemory& memory);

    addr_t create_instance(uint32_t clsId);
    bool get_metrics(addr_t font_obj, Metrics& out) const;
    uint32_t get_class_id(addr_t font_obj) const;
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    struct FontState {
        uint32_t cls_id = 0;
        Metrics metrics;
        uint32_t refs = 1;
    };

    void setup_font(uint32_t clsId, const Metrics& metrics);
    const FontState* find_state(addr_t font_obj) const;
    FontState* find_state(addr_t font_obj);
    static Metrics metrics_for_cls(uint32_t clsId);
    static bool is_font_clsid(uint32_t clsId);
    static std::string class_name_for_cls(uint32_t clsId);

    BrewShell& shell_;
    EndianMemory& memory_;
    std::unordered_map<addr_t, FontState> states_;
};

#endif
