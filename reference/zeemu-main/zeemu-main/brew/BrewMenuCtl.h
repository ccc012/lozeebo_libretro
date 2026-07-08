#ifndef ZEEMU_BREW_MENU_CTL_H_
#define ZEEMU_BREW_MENU_CTL_H_

#include "brew/BrewService.h"
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"
#include <map>
#include <string>
#include <vector>

class BrewShell;

class BrewMenuCtl : public BrewService {
public:
    BrewMenuCtl(BrewShell& shell, EndianMemory& memory);

    addr_t get_object_ptr() const { return object_ptr_; }
    addr_t create_instance(uint32_t clsid = 0x01003100);
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    struct Item {
        uint16_t id = 0;
        uint32_t data = 0;
        std::string text;
    };
    struct ItemStyle {
        uint8_t frame_type = 0;
        uint16_t x_offset = 0;
        uint16_t y_offset = 0;
        uint8_t ro_image = 7; // AEE_RO_TRANSPARENT
    };
    enum class ControlKind {
        Menu,
        SoftKey,
        List,
        IconView,
    };
    struct State {
        ControlKind kind = ControlKind::Menu;
        bool active = false;
        bool command_enabled = true;
        uint32_t properties = 0;
        uint16_t selected_id = 0;
        std::string title;
        std::vector<Item> items;

        // Control rect from IMENUCTL_SetRect (AEERect x,y,dx,dy).
        int16_t rect_x = 0;
        int16_t rect_y = 0;
        int16_t rect_dx = 0;
        int16_t rect_dy = 0;
        bool has_rect = false;

        // AEEMenuColors overrides (RGBVAL format: R<<8 | G<<16 | B<<24).
        // wMask selects which entries apply (MC_* bits, AEEMenu.h).
        uint16_t mask = 0;
        uint32_t color_back = 0;     // Unselected background
        uint32_t color_text = 0;     // Unselected text
        uint32_t color_sel_back = 0; // Selected background
        uint32_t color_sel_text = 0; // Selected text
        uint32_t color_frame = 0;    // Frame color
        uint32_t color_scrollbar = 0;
        uint32_t color_scrollbar_fill = 0;
        uint32_t color_title = 0;
        uint32_t color_title_text = 0;

        // BREW AEEItemStyle values from IMENUCTL_SetStyle.
        ItemStyle normal_style;
        ItemStyle selected_style;
    };

    void setup_vtable();
    State& state_for(addr_t object_ptr);
    static ControlKind kind_from_clsid(uint32_t clsid);
    std::string read_aechar(addr_t ptr, size_t max_chars = 128) const;
    std::string text_from_add_item(addr_t item_ptr) const;
    ItemStyle read_item_style(addr_t style_ptr) const;
    void write_item_style(addr_t style_ptr, const ItemStyle& style);
    void default_rect(const State& state, int& x, int& y, int& dx, int& dy) const;
    void redraw(State& state);

    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_ = 0;
    addr_t vtable_ptr_ = 0;
    std::map<addr_t, State> states_;
};

#endif
