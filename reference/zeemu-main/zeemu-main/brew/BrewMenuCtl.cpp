#include "brew/BrewMenuCtl.h"
#include "brew/BrewDisplay.h"
#include "brew/BrewShell.h"
#include "brew/BrewShellResources.h"
#include "graphics/RenderBackend.h"
#include "cpu/core/CPU.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

// Per-call info logging gate; full dispatch logging already comes from the
// BrewShell hook tracer under the same env var.
static bool menuctl_trace_enabled() {
    return std::getenv("ZEEMU_TRACE_HLE") != nullptr ||
           std::getenv("ZEEMU_TRACE_MENUCTL") != nullptr;
}

BrewMenuCtl::BrewMenuCtl(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {
    setup_vtable();
    object_ptr_ = create_instance();
}

void BrewMenuCtl::setup_vtable() {
    vtable_ptr_ = shell_.malloc(48 * 4);

    static const char* kNames[] = {
        "AddRef", "Release",
        "HandleEvent", "Redraw", "SetActive", "IsActive", "SetRect", "GetRect",
        "SetProperties", "GetProperties", "Reset",
        "SetTitle", "AddItem", "AddItemEx", "GetItemData", "DeleteItem", "DeleteAll",
        "SetSel", "GetSel", "EnableCommand", "SetItemText", "SetItemTime", "GetItemTime",
        "SetStyle", "SetColors", "MoveItem", "GetItemCount", "GetItemID", "GetItem",
        "SetItem", "Sort", "SetSelEx", "EnumSelInit", "EnumNextSel", "GetStyle",
        "GetColors", "GetItemRect", "SetOwnerDrawCB"
    };
    for (int i = 0; i < 48; ++i) {
        std::string name = i < static_cast<int>(sizeof(kNames) / sizeof(kNames[0]))
            ? std::string("IMenuCtl_") + kNames[i]
            : "IMenuCtl_Fn" + std::to_string(i);
        memory_.write_value(vtable_ptr_ + static_cast<uint32_t>(i * 4), shell_.add_hook(name, this));
    }
}

addr_t BrewMenuCtl::create_instance(uint32_t clsid) {
    addr_t object = shell_.malloc(4);
    memory_.write_value(object, vtable_ptr_);
    auto [it, inserted] = states_.try_emplace(object);
    it->second.kind = kind_from_clsid(clsid);
    return object;
}

BrewMenuCtl::State& BrewMenuCtl::state_for(addr_t object_ptr) {
    if (object_ptr == 0 || object_ptr >= 0xFF000000) {
        object_ptr = object_ptr_;
    }
    return states_[object_ptr];
}

BrewMenuCtl::ControlKind BrewMenuCtl::kind_from_clsid(uint32_t clsid) {
    switch (clsid) {
    case 0x01003101: return ControlKind::SoftKey;  // AEECLSID_SOFTKEYCTL
    case 0x01003102: return ControlKind::List;     // AEECLSID_LISTCTL
    case 0x01003103: return ControlKind::IconView; // AEECLSID_ICONVIEWCTL
    case 0x01003100: // AEECLSID_MENUCTL
    default: return ControlKind::Menu;
    }
}

std::string BrewMenuCtl::read_aechar(addr_t ptr, size_t max_chars) const {
    std::string out;
    if (!ptr || ptr >= 0xFF000000) {
        return out;
    }
    for (size_t i = 0; i < max_chars; ++i) {
        uint16_t c = static_cast<uint16_t>(memory_.read_value(ptr + static_cast<addr_t>(i * 2), EndianMemory::Halfword));
        if (c == 0) {
            break;
        }
        if (i == 0 && (c == 0xfeff || c == 0xfffe)) {
            continue;
        }
        out.push_back((c >= 0x20 && c < 0x7f) ? static_cast<char>(c) : '?');
    }
    return out;
}

std::string BrewMenuCtl::text_from_add_item(addr_t item_ptr) const {
    if (!item_ptr || item_ptr >= 0xFF000000) {
        return {};
    }
    addr_t p_text = memory_.read_value(item_ptr + 0);
    if (std::string text = read_aechar(p_text); !text.empty()) {
        return text;
    }
    addr_t psz_text = memory_.read_value(item_ptr + 12);
    uint16_t w_text = static_cast<uint16_t>(memory_.read_value(item_ptr + 16, EndianMemory::Halfword));
    if (psz_text && psz_text < 0xFF000000 && w_text != 0) {
        char path[256] = {};
        shell_.read_string(psz_text, path, sizeof(path));
        if (menuctl_trace_enabled()) {
            printf("  IMenuCtl_AddItemEx resource text path='%s' id=0x%x\n", path, w_text);
        }
        std::u16string text16;
        if (load_string_payload(shell_, path, w_text, text16)) {
            std::string text;
            text.reserve(text16.size());
            for (char16_t ch : text16) {
                text.push_back((ch >= 0x20 && ch < 0x7f) ? static_cast<char>(ch) : '?');
            }
            return text;
        }
    }
    return {};
}

BrewMenuCtl::ItemStyle BrewMenuCtl::read_item_style(addr_t style_ptr) const {
    ItemStyle style;
    if (!style_ptr || style_ptr >= 0xFF000000) {
        return style;
    }
    // BREW ARM AEEItemStyle layout (AEEShell.h): int8 ft @ +0,
    // uint16 xOffset @ +2, uint16 yOffset @ +4, int8 roImage @ +6.
    style.frame_type = static_cast<uint8_t>(memory_.read_value(style_ptr + 0, EndianMemory::Byte));
    style.x_offset = static_cast<uint16_t>(memory_.read_value(style_ptr + 2, EndianMemory::Halfword));
    style.y_offset = static_cast<uint16_t>(memory_.read_value(style_ptr + 4, EndianMemory::Halfword));
    style.ro_image = static_cast<uint8_t>(memory_.read_value(style_ptr + 6, EndianMemory::Byte));
    return style;
}

void BrewMenuCtl::write_item_style(addr_t style_ptr, const ItemStyle& style) {
    if (!style_ptr || style_ptr >= 0xFF000000) {
        return;
    }
    memory_.write_value(style_ptr + 0, style.frame_type, EndianMemory::Byte);
    memory_.write_value(style_ptr + 1, 0, EndianMemory::Byte);
    memory_.write_value(style_ptr + 2, style.x_offset, EndianMemory::Halfword);
    memory_.write_value(style_ptr + 4, style.y_offset, EndianMemory::Halfword);
    memory_.write_value(style_ptr + 6, style.ro_image, EndianMemory::Byte);
    memory_.write_value(style_ptr + 7, 0, EndianMemory::Byte);
}

void BrewMenuCtl::default_rect(const State& state, int& x, int& y, int& dx, int& dy) const {
    if (state.has_rect) {
        x = state.rect_x;
        y = state.rect_y;
        dx = state.rect_dx;
        dy = state.rect_dy;
        return;
    }
    const int display_w = static_cast<int>(shell_.get_display_width());
    const int display_h = static_cast<int>(shell_.get_display_height());
    if (state.kind == ControlKind::SoftKey) {
        x = 0;
        y = std::max(0, display_h - 20);
        dx = display_w;
        dy = 20;
        return;
    }
    x = 16;
    y = 32;
    dx = std::max(0, display_w - 32);
    dy = std::max(0, display_h - y);
}

void BrewMenuCtl::redraw(State& state) {
    BrewDisplay* display = shell_.get_display();
    auto* presenter = shell_.get_presenter();
    if (!presenter && !display) {
        return;
    }
    // AEEMenuColors mask bits (BREW 4.0.2 SP19 AEEMenu.h:52-61).
    constexpr uint16_t kMcBack = 0x0001;
    constexpr uint16_t kMcText = 0x0002;
    constexpr uint16_t kMcSelBack = 0x0004;
    constexpr uint16_t kMcSelText = 0x0008;
    constexpr uint16_t kMcFrame = 0x0010;
    constexpr uint16_t kMcTitleText = 0x0200;
    // Default BREW menu palette: black text on light gray, gray selection.
    // MAKE_RGB = r<<8|g<<16|b<<24.
    constexpr uint32_t kDefaultText = 0x00000000;
    constexpr uint32_t kDefaultBack = 0xF0F0F000;
    constexpr uint32_t kDefaultSelText = 0xFFFFFF00;
    constexpr uint32_t kDefaultSelBack = 0x60606000;
    constexpr uint32_t kDefaultFrame = 0x80808000;

    int origin_x = 0;
    int origin_y = 0;
    int width = 0;
    int height = 0;
    default_rect(state, origin_x, origin_y, width, height);
    const int line_height = state.kind == ControlKind::SoftKey ? std::max(16, height) : 16;
    const uint32_t back = (state.mask & kMcBack) ? state.color_back : kDefaultBack;
    const uint32_t text_color = (state.mask & kMcText) ? state.color_text : kDefaultText;
    const uint32_t sel_back = (state.mask & kMcSelBack) ? state.color_sel_back : kDefaultSelBack;
    const uint32_t sel_text = (state.mask & kMcSelText) ? state.color_sel_text : kDefaultSelText;
    const uint32_t frame_color = (state.mask & kMcFrame) ? state.color_frame : kDefaultFrame;
    const uint32_t title_text = (state.mask & kMcTitleText) ? state.color_title_text : text_color;

    int y = origin_y;
    if (display) {
        display->fill_rect_device(origin_x, origin_y, width, height, back);
    }
    if (state.kind == ControlKind::SoftKey) {
        const int count = std::max(1, static_cast<int>(state.items.size()));
        const int cell_w = std::max(1, width / count);
        for (size_t i = 0; i < state.items.size(); ++i) {
            const auto& item = state.items[i];
            const bool selected = item.id == state.selected_id;
            const ItemStyle& style = selected ? state.selected_style : state.normal_style;
            const int item_x = origin_x + static_cast<int>(i) * cell_w;
            const int item_w = (i + 1 == state.items.size()) ? (origin_x + width - item_x) : cell_w;
            const std::string& text = item.text.empty() ? ("item " + std::to_string(item.id)) : item.text;
            if (display) {
                if (selected) {
                    display->fill_rect_device(item_x, origin_y, item_w, line_height, sel_back);
                }
                if (style.frame_type == 5) { // AEE_FT_BOX
                    display->fill_rect_device(item_x, origin_y, item_w, 1, frame_color);
                    display->fill_rect_device(item_x, origin_y + line_height - 1, item_w, 1, frame_color);
                    display->fill_rect_device(item_x, origin_y, 1, line_height, frame_color);
                    display->fill_rect_device(item_x + item_w - 1, origin_y, 1, line_height, frame_color);
                }
                display->draw_text_to_device_bitmap_rgb(
                    item_x + 8 + static_cast<int>(style.x_offset),
                    origin_y + 4 + static_cast<int>(style.y_offset),
                    text,
                    selected ? sel_text : text_color);
            }
            if (presenter) {
                std::string line = (selected ? "> " : "  ") + text;
                presenter->draw_debug_text(static_cast<float>(item_x + 8), static_cast<float>(origin_y + 4), line.c_str());
            }
        }
        return;
    }
    if (!state.title.empty()) {
        if (display) {
            display->draw_text_to_device_bitmap_rgb(origin_x + 8, y + 4, state.title, title_text);
        }
        if (presenter) {
            presenter->draw_debug_text(static_cast<float>(origin_x + 8), static_cast<float>(y + 4), state.title.c_str());
        }
        y += line_height + 2;
    }
    for (const auto& item : state.items) {
        const bool selected = item.id == state.selected_id;
        const ItemStyle& style = selected ? state.selected_style : state.normal_style;
        const std::string& text = item.text.empty() ? ("item " + std::to_string(item.id)) : item.text;
        const int text_x = origin_x + 8 + static_cast<int>(style.x_offset);
        const int text_y = y + 4 + static_cast<int>(style.y_offset);
        if (display) {
            if (selected) {
                display->fill_rect_device(origin_x, y, width, line_height, sel_back);
            }
            if (style.frame_type == 5) { // AEE_FT_BOX
                display->fill_rect_device(origin_x, y, width, 1, frame_color);
                display->fill_rect_device(origin_x, y + line_height - 1, width, 1, frame_color);
                display->fill_rect_device(origin_x, y, 1, line_height, frame_color);
                display->fill_rect_device(origin_x + width - 1, y, 1, line_height, frame_color);
            }
            display->draw_text_to_device_bitmap_rgb(text_x, text_y, text, selected ? sel_text : text_color);
        }
        if (presenter) {
            std::string line = (selected ? "> " : "  ") + text;
            presenter->draw_debug_text(static_cast<float>(text_x), static_cast<float>(text_y), line.c_str());
        }
        y += line_height;
    }
}

void BrewMenuCtl::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    uint32_t r3 = cpu.get_reg(REG_R3);
    State& state = state_for(r0);

    if (name == "IMenuCtl_AddRef") {
        cpu.set_reg(REG_R0, r0);
    } else if (name == "IMenuCtl_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMenuCtl_HandleEvent") {
        uint32_t evt = r1;
        uint32_t wp = r2;
        if (evt == 0x100 && !state.items.empty()) {
            if (state.selected_id == 0) {
                state.selected_id = state.items.front().id;
            }
            if (wp == 0xE031 || wp == 0xE032 || wp == 0xE033 || wp == 0xE034) {
                auto it = std::find_if(state.items.begin(), state.items.end(), [&](const Item& item) {
                    return item.id == state.selected_id;
                });
                size_t idx = it != state.items.end() ? static_cast<size_t>(std::distance(state.items.begin(), it)) : 0u;
                if (wp == 0xE031 || wp == 0xE033) {
                    idx = idx == 0 ? state.items.size() - 1u : idx - 1u;
                } else {
                    idx = (idx + 1u) % state.items.size();
                }
                state.selected_id = state.items[idx].id;
            } else if (wp == 0xE035 && state.command_enabled && state.selected_id != 0) {
                uint32_t selected_data = 0;
                auto it = std::find_if(state.items.begin(), state.items.end(), [&](const Item& item) { return item.id == state.selected_id; });
                if (it != state.items.end()) {
                    selected_data = it->data;
                }
                shell_.queue_app_event(0x200, state.selected_id, selected_data, "EVT_COMMAND");
            }
        }
        if (menuctl_trace_enabled()) {
            printf("  IMenuCtl_HandleEvent obj=0x%08x evt=0x%x wp=0x%x sel=%u\n", r0, evt, wp, state.selected_id);
        }
        redraw(state);
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IMenuCtl_Redraw") {
        redraw(state);
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IMenuCtl_SetActive") {
        state.active = r1 != 0;
        if (state.active) {
            redraw(state);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMenuCtl_IsActive") {
        cpu.set_reg(REG_R0, state.active ? 1u : 0u);
    } else if (name == "IMenuCtl_SetRect") {
        // IMENUCTL_SetRect(p, AEERect*): four int16 fields x,y,dx,dy.
        if (r1 && r1 < 0xFF000000) {
            state.rect_x = static_cast<int16_t>(memory_.read_value(r1 + 0, EndianMemory::Halfword));
            state.rect_y = static_cast<int16_t>(memory_.read_value(r1 + 2, EndianMemory::Halfword));
            state.rect_dx = static_cast<int16_t>(memory_.read_value(r1 + 4, EndianMemory::Halfword));
            state.rect_dy = static_cast<int16_t>(memory_.read_value(r1 + 6, EndianMemory::Halfword));
            state.has_rect = true;
            if (menuctl_trace_enabled()) {
                printf("  IMenuCtl_SetRect obj=0x%08x rect=(%d,%d,%d,%d)\n",
                       r0, state.rect_x, state.rect_y, state.rect_dx, state.rect_dy);
            }
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMenuCtl_GetRect") {
        if (r1 && r1 < 0xFF000000) {
            int x = 0;
            int y = 0;
            int dx = 0;
            int dy = 0;
            default_rect(state, x, y, dx, dy);
            memory_.write_value(r1 + 0, static_cast<uint16_t>(x), EndianMemory::Halfword);
            memory_.write_value(r1 + 2, static_cast<uint16_t>(y), EndianMemory::Halfword);
            memory_.write_value(r1 + 4, static_cast<uint16_t>(dx), EndianMemory::Halfword);
            memory_.write_value(r1 + 6, static_cast<uint16_t>(dy), EndianMemory::Halfword);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMenuCtl_SetProperties") {
        state.properties = r1;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMenuCtl_GetProperties") {
        cpu.set_reg(REG_R0, state.properties);
    } else if (name == "IMenuCtl_Reset" || name == "IMenuCtl_DeleteAll") {
        state.items.clear();
        state.selected_id = 0;
        cpu.set_reg(REG_R0, name == "IMenuCtl_DeleteAll" ? 1u : 0u);
    } else if (name == "IMenuCtl_SetTitle") {
        state.title = read_aechar(r3);
        if (state.title.empty() && r1 && r1 < 0xFF000000 && r2 != 0) {
            char path[256] = {};
            shell_.read_string(r1, path, sizeof(path));
            std::u16string text16;
            if (load_string_payload(shell_, path, static_cast<uint16_t>(r2), text16)) {
                state.title.reserve(text16.size());
                for (char16_t ch : text16) {
                    state.title.push_back((ch >= 0x20 && ch < 0x7f) ? static_cast<char>(ch) : '?');
                }
            }
        }
        if (menuctl_trace_enabled()) {
            printf("  IMenuCtl_SetTitle obj=0x%08x '%s'\n", r0, state.title.c_str());
        }
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IMenuCtl_AddItem") {
        auto item_id = static_cast<uint16_t>(r3);
        addr_t p_text = memory_.read_value(cpu.get_reg(REG_SP));
        uint32_t data = memory_.read_value(cpu.get_reg(REG_SP) + 4);
        Item item{item_id, data, read_aechar(p_text)};
        if (state.selected_id == 0) {
            state.selected_id = item.id;
        }
        auto existing = std::find_if(state.items.begin(), state.items.end(),
                                     [&](const Item& candidate) { return candidate.id == item.id; });
        if (existing != state.items.end()) {
            *existing = item;
        } else {
            state.items.push_back(item);
        }
        if (menuctl_trace_enabled()) {
            printf("  IMenuCtl_AddItem obj=0x%08x id=%u text='%s' data=0x%x\n", r0, item.id, item.text.c_str(), item.data);
        }
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IMenuCtl_AddItemEx") {
        addr_t item_ptr = r1;
        if (std::getenv("ZEEMU_TRACE_MENUCTL") != nullptr && item_ptr && item_ptr < 0xFF000000) {
            printf("  IMenuCtl_AddItemEx raw obj=0x%08x item=0x%08x "
                   "pText=0x%08x pImage=0x%08x pszResImage=0x%08x pszResText=0x%08x "
                   "wText=0x%04x wFont=0x%04x wImage=0x%04x wItemID=0x%04x dwData=0x%08x "
                   "R5=0x%08x R6=0x%08x\n",
                   r0,
                   item_ptr,
                   memory_.read_value(item_ptr + 0),
                   memory_.read_value(item_ptr + 4),
                   memory_.read_value(item_ptr + 8),
                   memory_.read_value(item_ptr + 12),
                   static_cast<unsigned>(memory_.read_value(item_ptr + 16, EndianMemory::Halfword)),
                   static_cast<unsigned>(memory_.read_value(item_ptr + 18, EndianMemory::Halfword)),
                   static_cast<unsigned>(memory_.read_value(item_ptr + 20, EndianMemory::Halfword)),
                   static_cast<unsigned>(memory_.read_value(item_ptr + 22, EndianMemory::Halfword)),
                   memory_.read_value(item_ptr + 24),
                   cpu.get_reg(REG_R5),
                   cpu.get_reg(REG_R6));
        }
        Item item;
        item.id = static_cast<uint16_t>(memory_.read_value(item_ptr + 22, EndianMemory::Halfword));
        item.data = memory_.read_value(item_ptr + 24);
        item.text = text_from_add_item(item_ptr);
        if (state.selected_id == 0) {
            state.selected_id = item.id;
        }
        auto existing = std::find_if(state.items.begin(), state.items.end(),
                                     [&](const Item& candidate) { return candidate.id == item.id; });
        if (existing != state.items.end()) {
            *existing = item;
        } else {
            state.items.push_back(item);
        }
        if (menuctl_trace_enabled()) {
            printf("  IMenuCtl_AddItemEx obj=0x%08x id=%u text='%s' data=0x%x\n", r0, item.id, item.text.c_str(), item.data);
        }
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IMenuCtl_GetItemData") {
        uint16_t item_id = static_cast<uint16_t>(r1);
        uint32_t out = r2;
        auto it = std::find_if(state.items.begin(), state.items.end(), [item_id](const Item& item) { return item.id == item_id; });
        if (it != state.items.end() && out && out < 0xFF000000) {
            memory_.write_value(out, it->data);
        }
        cpu.set_reg(REG_R0, it != state.items.end() ? 1u : 0u);
    } else if (name == "IMenuCtl_SetSel" || name == "IMenuCtl_SetSelEx") {
        state.selected_id = static_cast<uint16_t>(r1);
        redraw(state);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMenuCtl_GetSel" || name == "IMenuCtl_EnumNextSel") {
        cpu.set_reg(REG_R0, state.selected_id);
    } else if (name == "IMenuCtl_GetItemCount") {
        cpu.set_reg(REG_R0, static_cast<uint32_t>(state.items.size()));
    } else if (name == "IMenuCtl_GetItemID") {
        int idx = static_cast<int>(r1);
        cpu.set_reg(REG_R0, idx >= 0 && idx < static_cast<int>(state.items.size()) ? state.items[idx].id : 0u);
    } else if (name == "IMenuCtl_EnableCommand") {
        state.command_enabled = r1 != 0;
        if (menuctl_trace_enabled()) {
            printf("  IMenuCtl_EnableCommand obj=0x%08x %u\n", r0, state.command_enabled ? 1u : 0u);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMenuCtl_EnumSelInit") {
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IMenuCtl_GetItemRect") {
        if (r2 && r2 < 0xFF000000) {
            int idx = static_cast<int>(r1);
            int x = 0;
            int y = 0;
            int dx = 0;
            int dy = 0;
            default_rect(state, x, y, dx, dy);
            if (state.kind == ControlKind::SoftKey) {
                const int count = std::max(1, static_cast<int>(state.items.size()));
                const int safe_idx = std::max(0, std::min(idx, count - 1));
                const int cell_w = std::max(1, dx / count);
                const int item_x = x + safe_idx * cell_w;
                const int item_w = safe_idx + 1 == count ? (x + dx - item_x) : cell_w;
                memory_.write_value(r2 + 0, static_cast<uint16_t>(item_x), EndianMemory::Halfword);
                memory_.write_value(r2 + 2, static_cast<uint16_t>(y), EndianMemory::Halfword);
                memory_.write_value(r2 + 4, static_cast<uint16_t>(item_w), EndianMemory::Halfword);
                memory_.write_value(r2 + 6, static_cast<uint16_t>(dy), EndianMemory::Halfword);
            } else {
                memory_.write_value(r2 + 0, static_cast<uint16_t>(x + 8), EndianMemory::Halfword);
                memory_.write_value(r2 + 2, static_cast<uint16_t>(y + 26 + std::max(0, idx) * 16), EndianMemory::Halfword);
                memory_.write_value(r2 + 4, static_cast<uint16_t>(std::max(0, dx - 16)), EndianMemory::Halfword);
                memory_.write_value(r2 + 6, static_cast<uint16_t>(16), EndianMemory::Halfword);
            }
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMenuCtl_SetStyle") {
        // void IMENUCTL_SetStyle(IMenuCtl*, AEEItemStyle* normal, AEEItemStyle* selected).
        // AEEItemStyle is a compact BREW ARM struct, not the host C++ enum layout.
        if (r1 && r1 < 0xFF000000) {
            state.normal_style = read_item_style(r1);
        }
        if (r2 && r2 < 0xFF000000) {
            state.selected_style = read_item_style(r2);
        }
        if (menuctl_trace_enabled()) {
            printf("  IMenuCtl_SetStyle obj=0x%08x normal(ft=%u,x=%u,y=%u,rop=%u) sel(ft=%u,x=%u,y=%u,rop=%u)\n",
                   r0,
                   state.normal_style.frame_type,
                   state.normal_style.x_offset,
                   state.normal_style.y_offset,
                   state.normal_style.ro_image,
                   state.selected_style.frame_type,
                   state.selected_style.x_offset,
                   state.selected_style.y_offset,
                   state.selected_style.ro_image);
        }
        if (state.active) {
            redraw(state);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMenuCtl_SetColors") {
        // void IMENUCTL_SetColors(IMenuCtl*, AEEMenuColors* pc) -- AEEMenu.h:176.
        // AEEMenuColors: uint16 wMask followed by nine RGBVAL (uint32) fields.
        // Natural ARM alignment pads the uint16, so cBack starts at +4 and the
        // colors step by 4 (AEEMenu.h:63-75). NULL pc resets to system colors.
        if (r1 && r1 < 0xFF000000) {
            state.mask = static_cast<uint16_t>(memory_.read_value(r1, EndianMemory::Halfword));
            state.color_back = memory_.read_value(r1 + 4);
            state.color_text = memory_.read_value(r1 + 8);
            state.color_sel_back = memory_.read_value(r1 + 12);
            state.color_sel_text = memory_.read_value(r1 + 16);
            state.color_frame = memory_.read_value(r1 + 20);
            state.color_scrollbar = memory_.read_value(r1 + 24);
            state.color_scrollbar_fill = memory_.read_value(r1 + 28);
            state.color_title = memory_.read_value(r1 + 32);
            state.color_title_text = memory_.read_value(r1 + 36);
        } else {
            state.mask = 0;
        }
        if (menuctl_trace_enabled()) {
            printf("  IMenuCtl_SetColors obj=0x%08x mask=0x%04x selBack=0x%08x selText=0x%08x\n",
                   r0, state.mask, state.color_sel_back, state.color_sel_text);
        }
        if (state.active) {
            redraw(state);
        }
        cpu.set_reg(REG_R0, 0); // void in the SDK vtable; R0 is scratch
    } else if (name == "IMenuCtl_GetColors") {
        // void IMENUCTL_GetColors(IMenuCtl*, AEEMenuColors* pc) -- AEEMenu.h:194.
        if (r1 && r1 < 0xFF000000) {
            memory_.write_value(r1 + 0, state.mask, EndianMemory::Halfword);
            memory_.write_value(r1 + 4, state.color_back);
            memory_.write_value(r1 + 8, state.color_text);
            memory_.write_value(r1 + 12, state.color_sel_back);
            memory_.write_value(r1 + 16, state.color_sel_text);
            memory_.write_value(r1 + 20, state.color_frame);
            memory_.write_value(r1 + 24, state.color_scrollbar);
            memory_.write_value(r1 + 28, state.color_scrollbar_fill);
            memory_.write_value(r1 + 32, state.color_title);
            memory_.write_value(r1 + 36, state.color_title_text);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMenuCtl_GetStyle") {
        // void IMENUCTL_GetStyle(IMenuCtl*, AEEItemStyle* normal, AEEItemStyle* selected).
        write_item_style(r1, state.normal_style);
        write_item_style(r2, state.selected_style);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMenuCtl_DeleteItem") {
        // boolean IMENUCTL_DeleteItem(IMenuCtl*, uint16 wID).
        auto item_id = static_cast<uint16_t>(r1);
        auto it = std::find_if(state.items.begin(), state.items.end(),
                               [item_id](const Item& item) { return item.id == item_id; });
        if (it != state.items.end()) {
            state.items.erase(it);
            if (state.selected_id == item_id) {
                state.selected_id = state.items.empty() ? 0 : state.items.front().id;
            }
            cpu.set_reg(REG_R0, 1);
        } else {
            cpu.set_reg(REG_R0, 0);
        }
    } else {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), r0, r1, r2, r3);
        cpu.set_reg(REG_R0, 0);
    }
}
