#ifndef ZEEMU_BREW_APP_UI_H_
#define ZEEMU_BREW_APP_UI_H_

#include "brew/BrewService.h"
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class BrewShell;

class BrewAppUI : public BrewService {
public:
    BrewAppUI(BrewShell& shell, EndianMemory& memory);

    addr_t create_instance(uint32_t clsid);
    bool handles(uint32_t clsid) const;
    const char* class_name(uint32_t clsid) const;

    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    struct ObjectInfo {
        uint32_t clsid = 0;
        addr_t object = 0;
        addr_t vtable = 0;
        addr_t parent = 0;
        addr_t root_child = 0;
        addr_t image_object = 0;
        addr_t text_model = 0;
        addr_t value_model = 0;
        addr_t owner_widget = 0;
        addr_t text_buffer = 0;
        addr_t value_ptr = 0;
        int value_len = 0;
        int x = 0;
        int y = 0;
        int width = 120;
        int height = 24;
        int sel_start = 0;
        int sel_end = 0;
        bool visible = true;
        bool dirty = true;
        std::string text;
        std::vector<addr_t> children;
        std::unordered_map<uint32_t, uint32_t> props;
    };

    ObjectInfo& create_object(uint32_t clsid);
    ObjectInfo* find_object(addr_t object);
    void add_child(addr_t parent, addr_t child);
    addr_t get_model(ObjectInfo& widget, uint32_t iid);
    addr_t ensure_text_buffer(ObjectInfo& info);
    void copy_text_to_owner(ObjectInfo& model);
    void render_tree(addr_t object);
    void render_form(ObjectInfo& form);

    BrewShell& shell_;
    EndianMemory& memory_;
    std::unordered_map<addr_t, ObjectInfo> objects_;
};

#endif
