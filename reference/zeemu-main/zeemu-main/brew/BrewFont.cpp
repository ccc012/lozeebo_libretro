#include "brew/BrewFont.h"
#include "cpu/core/CPU.h"
#include <cstdio>
#include <cstring>

BrewFont::BrewFont(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {}

bool BrewFont::is_font_clsid(uint32_t clsId) {
    switch (clsId) {
        case 0x01012786: // AEECLSID_FONTSYSNORMAL
        case 0x01012787: // AEECLSID_FONTSYSLARGE
        case 0x01012788: // AEECLSID_FONTSYSBOLD
        case 0x0101402c: // AEECLSID_FONTSYSITALIC
        case 0x0101402d: // AEECLSID_FONTSYSBOLDITALIC
        case 0x0101402e: // AEECLSID_FONTSYSLARGEITALIC
        case 0x01001022: // AEECLSID_FONT
        case 0x0102f679: // AEECLSID_FONT_STANDARD11
        case 0x0102f67a: // AEECLSID_FONT_STANDARD11B
        case 0x01030852: // AEECLSID_FONT_STANDARD15
        case 0x01030853: // AEECLSID_FONT_STANDARD15B
        case 0x0102f67b: // AEECLSID_FONT_STANDARD18
        case 0x0102f67c: // AEECLSID_FONT_STANDARD18B
        case 0x0102f67d: // AEECLSID_FONT_STANDARD23
        case 0x0102f67e: // AEECLSID_FONT_STANDARD23B
        case 0x0102f67f: // AEECLSID_FONT_STANDARD26
        case 0x0102f680: // AEECLSID_FONT_STANDARD26B
        case 0x0102f681: // AEECLSID_FONT_STANDARD36
        case 0x0100a001: // AEECLSID_FONT_FIXED4X6
        case 0x0100a002: // AEECLSID_FONT_BASIC6
        case 0x0100a003: // AEECLSID_FONT_BASIC9
        case 0x0100a004: // AEECLSID_FONT_BASIC10
        case 0x0100a005: // AEECLSID_FONT_BASIC11
        case 0x0100a006: // AEECLSID_FONT_BASIC11B
        case 0x0100a007: // AEECLSID_FONT_BASIC14
        case 0x0100a008: // AEECLSID_FONT_BASIC12
        case 0x0100a009: // AEECLSID_FONT_BASIC12B
        case 0x0100a00a: // AEECLSID_FONT_BASIC15
        case 0x0100a100: // AEECLSID_BITFONTFOUNDRY
            return true;
        default:
            return false;
    }
}

BrewFont::Metrics BrewFont::metrics_for_cls(uint32_t clsId) {
    Metrics m{};
    switch (clsId) {
        case 0x0100a001:
            m.ascent = 4;
            m.descent = 2;
            m.leading = 0;
            m.max_char_width = 4;
            m.height = 6;
            break;
        case 0x0100a002:
            m.ascent = 6;
            m.descent = 2;
            m.leading = 1;
            m.max_char_width = 5;
            m.height = 9;
            break;
        case 0x0100a003:
            m.ascent = 9;
            m.descent = 3;
            m.leading = 1;
            m.max_char_width = 6;
            m.height = 13;
            break;
        case 0x0100a004:
            m.ascent = 10;
            m.descent = 3;
            m.leading = 1;
            m.max_char_width = 7;
            m.height = 14;
            break;
        case 0x0100a005:
        case 0x01012786:
        case 0x0101402c:
        case 0x0102f679:
        case 0x01001022:
            m.ascent = 13;
            m.descent = 3;
            m.leading = 2;
            m.max_char_width = 8;
            m.height = 18;
            break;
        case 0x0100a006:
        case 0x01012788:
        case 0x0101402d:
        case 0x0102f67a:
            m.ascent = 13;
            m.descent = 3;
            m.leading = 2;
            m.max_char_width = 8;
            m.height = 18;
            m.bold = true;
            break;
        case 0x0100a007:
            m.ascent = 14;
            m.descent = 4;
            m.leading = 2;
            m.max_char_width = 9;
            m.height = 20;
            break;
        case 0x0100a008:
            m.ascent = 12;
            m.descent = 4;
            m.leading = 2;
            m.max_char_width = 8;
            m.height = 18;
            break;
        case 0x0100a009:
            m.ascent = 12;
            m.descent = 4;
            m.leading = 2;
            m.max_char_width = 8;
            m.height = 18;
            m.bold = true;
            break;
        case 0x0100a00a:
            m.ascent = 15;
            m.descent = 4;
            m.leading = 2;
            m.max_char_width = 10;
            m.height = 21;
            break;
        case 0x01012787:
        case 0x0101402e:
            m.ascent = 21;
            m.descent = 5;
            m.leading = 2;
            m.max_char_width = 10;
            m.height = 28;
            break;
        case 0x0102f67b:
            m.ascent = 17;
            m.descent = 4;
            m.leading = 2;
            m.max_char_width = 9;
            m.height = 23;
            break;
        case 0x01030852:
            m.ascent = 15;
            m.descent = 3;
            m.leading = 2;
            m.max_char_width = 8;
            m.height = 20;
            break;
        case 0x0102f67c:
        case 0x01030853:
            m.ascent = 17;
            m.descent = 4;
            m.leading = 2;
            m.max_char_width = 9;
            m.height = 23;
            m.bold = true;
            break;
        case 0x0102f67d:
            m.ascent = 21;
            m.descent = 5;
            m.leading = 2;
            m.max_char_width = 10;
            m.height = 28;
            break;
        case 0x0102f67e:
            m.ascent = 21;
            m.descent = 5;
            m.leading = 2;
            m.max_char_width = 10;
            m.height = 28;
            m.bold = true;
            break;
        case 0x0102f67f:
            m.ascent = 23;
            m.descent = 6;
            m.leading = 2;
            m.max_char_width = 10;
            m.height = 31;
            break;
        case 0x0102f680:
            m.ascent = 23;
            m.descent = 6;
            m.leading = 2;
            m.max_char_width = 10;
            m.height = 31;
            m.bold = true;
            break;
        case 0x0102f681:
            m.ascent = 38;
            m.descent = 10;
            m.leading = 2;
            m.max_char_width = 12;
            m.height = 50;
            break;
        default:
            m.ascent = 12;
            m.descent = 4;
            m.leading = 2;
            m.max_char_width = 8;
            m.height = 16;
            break;
    }

    if (clsId == 0x0101402c || clsId == 0x0101402d || clsId == 0x0101402e) {
        m.italic = true;
    }

    return m;
}

std::string BrewFont::class_name_for_cls(uint32_t clsId) {
    switch (clsId) {
        case 0x01012786: return "AEECLSID_FONTSYSNORMAL";
        case 0x01012787: return "AEECLSID_FONTSYSLARGE";
        case 0x01012788: return "AEECLSID_FONTSYSBOLD";
        case 0x0101402c: return "AEECLSID_FONTSYSITALIC";
        case 0x0101402d: return "AEECLSID_FONTSYSBOLDITALIC";
        case 0x0101402e: return "AEECLSID_FONTSYSLARGEITALIC";
        case 0x01001022: return "AEECLSID_FONT";
        case 0x0102f679: return "AEECLSID_FONT_STANDARD11";
        case 0x0102f67a: return "AEECLSID_FONT_STANDARD11B";
        case 0x01030852: return "AEECLSID_FONT_STANDARD15";
        case 0x01030853: return "AEECLSID_FONT_STANDARD15B";
        case 0x0102f67b: return "AEECLSID_FONT_STANDARD18";
        case 0x0102f67c: return "AEECLSID_FONT_STANDARD18B";
        case 0x0102f67d: return "AEECLSID_FONT_STANDARD23";
        case 0x0102f67e: return "AEECLSID_FONT_STANDARD23B";
        case 0x0102f67f: return "AEECLSID_FONT_STANDARD26";
        case 0x0102f680: return "AEECLSID_FONT_STANDARD26B";
        case 0x0102f681: return "AEECLSID_FONT_STANDARD36";
        case 0x0100a001: return "AEECLSID_FONT_FIXED4X6";
        case 0x0100a002: return "AEECLSID_FONT_BASIC6";
        case 0x0100a003: return "AEECLSID_FONT_BASIC9";
        case 0x0100a004: return "AEECLSID_FONT_BASIC10";
        case 0x0100a005: return "AEECLSID_FONT_BASIC11";
        case 0x0100a006: return "AEECLSID_FONT_BASIC11B";
        case 0x0100a007: return "AEECLSID_FONT_BASIC14";
        case 0x0100a008: return "AEECLSID_FONT_BASIC12";
        case 0x0100a009: return "AEECLSID_FONT_BASIC12B";
        case 0x0100a00a: return "AEECLSID_FONT_BASIC15";
        case 0x0100a100: return "AEECLSID_BITFONTFOUNDRY";
        default: return "AEECLSID_FONT_UNKNOWN";
    }
}

void BrewFont::setup_font(uint32_t clsId, const Metrics& metrics) {
    addr_t vtable = shell_.malloc(6 * 4);
    addr_t object = shell_.malloc(4);
    memory_.write_value(object, vtable);

    const char* names[] = { "AddRef", "Release", "QueryInterface", "DrawText", "GetInfo", "MeasureText" };
    for (int i = 0; i < 6; ++i) {
        memory_.write_value(vtable + (uint32_t)(i * 4), shell_.add_hook(std::string("IFont_") + names[i], this));
    }

    states_[object] = FontState{clsId, metrics, 1};
}

addr_t BrewFont::create_instance(uint32_t clsId) {
    for (const auto& it : states_) {
        if (it.second.cls_id == clsId) {
            return it.first;
        }
    }
    setup_font(clsId, metrics_for_cls(clsId));
    for (const auto& it : states_) {
        if (it.second.cls_id == clsId) {
            return it.first;
        }
    }
    return 0;
}

const BrewFont::FontState* BrewFont::find_state(addr_t font_obj) const {
    auto it = states_.find(font_obj);
    return it == states_.end() ? nullptr : &it->second;
}

BrewFont::FontState* BrewFont::find_state(addr_t font_obj) {
    auto it = states_.find(font_obj);
    return it == states_.end() ? nullptr : &it->second;
}

bool BrewFont::get_metrics(addr_t font_obj, Metrics& out) const {
    const FontState* state = find_state(font_obj);
    if (!state) {
        return false;
    }
    out = state->metrics;
    return true;
}

uint32_t BrewFont::get_class_id(addr_t font_obj) const {
    const FontState* state = find_state(font_obj);
    return state ? state->cls_id : 0;
}

void BrewFont::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    uint32_t r3 = cpu.get_reg(REG_R3);
    uint32_t sp = cpu.get_reg(REG_SP);

    FontState* state = find_state(r0);
    if (!state) {
        printf("  [%s] not implemented yet invalid font=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), r0, r1, r2, r3);
        cpu.set_reg(REG_R0, 0);
        return;
    }

    if (name == "IFont_AddRef") {
        cpu.set_reg(REG_R0, ++state->refs);
    } else if (name == "IFont_Release") {
        if (state->refs > 0) {
            --state->refs;
        }
        cpu.set_reg(REG_R0, state->refs);
    } else if (name == "IFont_QueryInterface") {
        uint32_t pp = r2;
        if (pp && pp < 0xFF000000) {
            memory_.write_value(pp, r0);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IFont_DrawText") {
        uint32_t pch = r1;
        int nChars = (int)r2;
        uint32_t x = r3;
        uint32_t y = memory_.read_value(sp);
        std::wstring ws;
        if (pch && pch < 0xFF000000) {
            for (int i = 0; i < nChars || (nChars == -1); ++i) {
                uint16_t c = (uint16_t)memory_.read_value(pch + (uint32_t)i * 2u, EndianMemory::Halfword);
                if (c == 0) {
                    break;
                }
                ws.push_back((wchar_t)c);
            }
        }
        std::string text(ws.begin(), ws.end());
        printf("  IFont_DrawText[%s]: '%s' at (%u, %u)\n", class_name_for_cls(state->cls_id).c_str(), text.c_str(), x, y);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IFont_GetInfo") {
        uint32_t pInfo = r1;
        if (pInfo && pInfo < 0xFF000000) {
            memory_.write_value(pInfo + 0, state->metrics.ascent, EndianMemory::Halfword);
            memory_.write_value(pInfo + 2, state->metrics.descent, EndianMemory::Halfword);
            memory_.write_value(pInfo + 4, state->metrics.leading, EndianMemory::Halfword);
            memory_.write_value(pInfo + 6, state->metrics.max_char_width, EndianMemory::Halfword);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IFont_MeasureText") {
        uint32_t pch = r1;
        int nChars = (int)r2;
        uint32_t pnWidth = r3;
        uint32_t pnHeight = memory_.read_value(sp);
        uint32_t width = 0;
        uint32_t height = state->metrics.height;

        if (pch && pch < 0xFF000000) {
            int len = 0;
            for (int i = 0; i < nChars || (nChars == -1); ++i) {
                uint16_t c = (uint16_t)memory_.read_value(pch + (uint32_t)i * 2u, EndianMemory::Halfword);
                if (c == 0) {
                    break;
                }
                ++len;
            }
            width = (uint32_t)len * state->metrics.max_char_width;
        }

        if (pnWidth && pnWidth < 0xFF000000) {
            memory_.write_value(pnWidth, width);
        }
        if (pnHeight && pnHeight < 0xFF000000) {
            memory_.write_value(pnHeight, height);
        }
        cpu.set_reg(REG_R0, 0);
    } else {
        printf("  [%s] not implemented yet font=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), r0, r1, r2, r3);
        cpu.set_reg(REG_R0, 0);
    }
}
