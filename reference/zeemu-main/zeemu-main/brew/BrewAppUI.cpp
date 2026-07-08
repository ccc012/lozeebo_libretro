#include "brew/BrewAppUI.h"
#include "brew/BrewDisplay.h"
#include "brew/BrewShell.h"
#include "cpu/core/CPU.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr uint32_t AEECLSID_CBTFETypeface = 0x01035156;
constexpr uint32_t AEECLSID_CForm = 0x01028E47;
constexpr uint32_t AEECLSID_CDisplayCanvas = 0x010551C8;
constexpr uint32_t AEECLSID_CRootContainer = 0x01055241;
constexpr uint32_t AEECLSID_CStaticWidget = 0x010551E0;
constexpr uint32_t AEECLSID_CViewportWidget = 0x010551EB;
constexpr uint32_t AEECLSID_CXYContainer = 0x010551ED;
constexpr uint32_t AEECLSID_IMAGEWIDGET_1 = 0x01028E19;
constexpr uint32_t AEECLSID_STATICWIDGET_1 = 0x01028E2A;
constexpr uint32_t AEECLSID_VALUEMODEL_1 = 0x01028E3C;
constexpr uint32_t AEECLSID_XYCONTAINER_1 = 0x01028E3F;
constexpr uint32_t AEECLSID_ZWHEEL_APPUI_01028E50 = 0x01028E50;
constexpr uint32_t AEECLSID_FLASHPLAYER_UI_COMPONENT = 0x010748D3;
constexpr uint32_t AEEIID_IModel = 0x0101593A;
constexpr uint32_t AEEIID_IValueModel = 0x0101593B;
constexpr uint32_t AEEIID_ITextModel = 0x01015946;
constexpr uint32_t AEEIID_IQI = 0x01000001;
constexpr uint32_t AEEIID_ICanvas = 0x0101E3F2;
constexpr uint32_t AEEIID_IDisplayCanvas = 0x0101E443;
constexpr uint32_t AEEIID_IRootContainer = 0x0102EC16;
constexpr uint32_t AEEIID_IContainer = 0x01015932;
constexpr uint32_t AEEIID_IXYContainer = 0x01015954;
constexpr uint32_t AEEIID_IHandler = 0x01015956;
constexpr uint32_t AEEIID_IWidget = 0x01015952;

bool parse_hook_name(const std::string& name, uint32_t& clsid, int& slot) {
    static const std::string prefix = "AppUI_";
    if (name.rfind(prefix, 0) != 0) {
        return false;
    }
    const size_t sep = name.rfind('_');
    if (sep == std::string::npos || sep <= prefix.size()) {
        return false;
    }
    clsid = static_cast<uint32_t>(std::strtoul(name.c_str() + prefix.size(), nullptr, 16));
    slot = std::atoi(name.c_str() + sep + 1);
    return true;
}

bool valid_guest_ptr(uint32_t ptr) {
    return ptr != 0 && ptr < 0xFF000000u;
}

std::string read_aechar(EndianMemory& memory, uint32_t ptr, int chars) {
    std::string text;
    if (!valid_guest_ptr(ptr)) {
        return text;
    }
    int limit = chars;
    if (limit < 0 || limit > 1024) {
        limit = 1024;
    }
    for (int i = 0; i < limit; ++i) {
        uint16_t ch = static_cast<uint16_t>(memory.read_value(ptr + static_cast<uint32_t>(i) * 2u, EndianMemory::Halfword));
        if (ch == 0) {
            break;
        }
        if (ch == '\n' || ch == '\r' || ch == '\t' || (ch >= 0x20 && ch < 0x7f)) {
            text.push_back(static_cast<char>(ch));
        } else {
            text.push_back('?');
        }
    }
    return text;
}

void write_aechar(EndianMemory& memory, uint32_t ptr, const std::string& text, int max_chars) {
    if (!valid_guest_ptr(ptr) || max_chars <= 0) {
        return;
    }
    const int count = std::min<int>(static_cast<int>(text.size()), max_chars - 1);
    for (int i = 0; i < count; ++i) {
        memory.write_value(ptr + static_cast<uint32_t>(i) * 2u, static_cast<uint16_t>(text[static_cast<size_t>(i)]), EndianMemory::Halfword);
    }
    memory.write_value(ptr + static_cast<uint32_t>(count) * 2u, 0, EndianMemory::Halfword);
}

bool is_container_clsid(uint32_t clsid) {
    return clsid == AEECLSID_XYCONTAINER_1 || clsid == AEECLSID_CXYContainer || clsid == AEECLSID_CRootContainer;
}

bool is_model_clsid(uint32_t clsid) {
    return clsid == AEEIID_IModel || clsid == AEEIID_ITextModel || clsid == AEEIID_IValueModel ||
           clsid == AEECLSID_VALUEMODEL_1;
}

}

BrewAppUI::BrewAppUI(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {
}

bool BrewAppUI::handles(uint32_t clsid) const {
    switch (clsid) {
        case AEECLSID_CBTFETypeface:
        case AEECLSID_CForm:
        case AEECLSID_CDisplayCanvas:
        case AEECLSID_CRootContainer:
        case AEECLSID_CStaticWidget:
        case AEECLSID_CViewportWidget:
        case AEECLSID_CXYContainer:
        case AEECLSID_IMAGEWIDGET_1:
        case AEECLSID_STATICWIDGET_1:
        case AEECLSID_VALUEMODEL_1:
        case AEECLSID_XYCONTAINER_1:
        case AEECLSID_ZWHEEL_APPUI_01028E50:
        case AEECLSID_FLASHPLAYER_UI_COMPONENT:
            return true;
        default:
            return false;
    }
}

const char* BrewAppUI::class_name(uint32_t clsid) const {
    switch (clsid) {
        case AEECLSID_CBTFETypeface: return "AEECLSID_CBTFETypeface";
        case AEECLSID_CForm: return "AEECLSID_CForm";
        case AEECLSID_CDisplayCanvas: return "AEECLSID_CDisplayCanvas";
        case AEECLSID_CRootContainer: return "AEECLSID_CRootContainer";
        case AEECLSID_CStaticWidget: return "AEECLSID_CStaticWidget";
        case AEECLSID_CViewportWidget: return "AEECLSID_CViewportWidget";
        case AEECLSID_CXYContainer: return "AEECLSID_CXYContainer";
        case AEECLSID_IMAGEWIDGET_1: return "AEECLSID_IMAGEWIDGET_1";
        case AEECLSID_STATICWIDGET_1: return "AEECLSID_STATICWIDGET_1";
        case AEECLSID_VALUEMODEL_1: return "AEECLSID_VALUEMODEL_1";
        case AEECLSID_XYCONTAINER_1: return "AEECLSID_XYCONTAINER_1";
        case AEECLSID_ZWHEEL_APPUI_01028E50: return "AEECLSID_ZWHEEL_APPUI_01028E50";
        case AEECLSID_FLASHPLAYER_UI_COMPONENT: return "AEECLSID_FlashPlayer_UI_Component_010748D3";
        case AEEIID_IModel: return "AEEIID_IModel";
        case AEEIID_ITextModel: return "AEEIID_ITextModel";
        case AEEIID_IValueModel: return "AEEIID_IValueModel";
        default: return "AEECLSID_AppUI_Unknown";
    }
}

BrewAppUI::ObjectInfo& BrewAppUI::create_object(uint32_t clsid) {
    ObjectInfo info;
    info.clsid = clsid;
    info.object = shell_.malloc(0x80);
    info.vtable = shell_.malloc(128 * 4);
    memory_.write_value(info.object, info.vtable);
    memory_.write_value(info.object + 4, clsid);
    for (int i = 0; i < 128; ++i) {
        char hook_name[64] = {};
        std::snprintf(hook_name, sizeof(hook_name), "AppUI_%08x_%d", clsid, i);
        memory_.write_value(info.vtable + static_cast<uint32_t>(i * 4), shell_.add_hook(hook_name, this));
    }

    auto inserted = objects_.emplace(info.object, std::move(info));
    return inserted.first->second;
}

addr_t BrewAppUI::create_instance(uint32_t clsid) {
    return create_object(clsid).object;
}

BrewAppUI::ObjectInfo* BrewAppUI::find_object(addr_t object) {
    auto it = objects_.find(object);
    return it == objects_.end() ? nullptr : &it->second;
}

void BrewAppUI::add_child(addr_t parent, addr_t child) {
    ObjectInfo* p = find_object(parent);
    ObjectInfo* c = find_object(child);
    if (!p || !c) {
        return;
    }
    for (addr_t existing : p->children) {
        if (existing == child) {
            return;
        }
    }
    p->children.push_back(child);
    c->parent = parent;
    p->dirty = true;
}

addr_t BrewAppUI::get_model(ObjectInfo& widget, uint32_t iid) {
    if (iid == AEEIID_ITextModel) {
        if (!widget.text_model) {
            ObjectInfo& model = create_object(AEEIID_ITextModel);
            model.owner_widget = widget.object;
            model.text = widget.text;
            widget.text_model = model.object;
        }
        return widget.text_model;
    }
    if (iid == AEEIID_IValueModel) {
        if (!widget.value_model) {
            ObjectInfo& model = create_object(AEEIID_IValueModel);
            model.owner_widget = widget.object;
            model.text = widget.text;
            widget.value_model = model.object;
        }
        return widget.value_model;
    }
    return 0;
}

addr_t BrewAppUI::ensure_text_buffer(ObjectInfo& info) {
    if (!info.text_buffer) {
        info.text_buffer = shell_.malloc(512);
    }
    write_aechar(memory_, info.text_buffer, info.text, 256);
    return info.text_buffer;
}

void BrewAppUI::copy_text_to_owner(ObjectInfo& model) {
    ObjectInfo* owner = find_object(model.owner_widget);
    if (!owner) {
        return;
    }
    owner->text = model.text;
    owner->dirty = true;
}

void BrewAppUI::render_tree(addr_t object) {
    ObjectInfo* info = find_object(object);
    if (!info || !info->visible) {
        return;
    }

    if (info->clsid == AEECLSID_CStaticWidget) {
        std::string text = info->text.empty() ? "Flash Player" : info->text;
        int width = std::max(info->width, 8);
        int height = std::max(info->height, 10);
        shell_.get_display()->fill_rect_device(info->x, info->y, width, height, 0x00000000);
        shell_.get_display()->draw_text_to_device_bitmap_rgb(info->x + 2, info->y + 2, text, 0x00ffffff);
        printf("  [BrewAppUI] render CStaticWidget '%s' at %d,%d %dx%d\n",
               text.c_str(), info->x, info->y, width, height);
    }

    if (info->clsid == AEECLSID_IMAGEWIDGET_1 && info->image_object) {
        if (shell_.draw_image_object(info->image_object, 0, 0)) {
            printf("  [BrewAppUI] render IMAGEWIDGET_1 image=0x%08x\n", info->image_object);
        }
    }

    for (addr_t child : info->children) {
        render_tree(child);
    }
    info->dirty = false;
}

void BrewAppUI::render_form(ObjectInfo& form) {
    if (form.root_child) {
        render_tree(form.root_child);
    }
    for (addr_t child : form.children) {
        render_tree(child);
    }
    form.dirty = false;
}

void BrewAppUI::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t clsid = 0;
    int slot = -1;
    if (!parse_hook_name(name, clsid, slot)) {
        printf("  [%s] not implemented yet: invalid AppUI hook name R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), cpu.get_reg(REG_R0), cpu.get_reg(REG_R1), cpu.get_reg(REG_R2), cpu.get_reg(REG_R3));
        cpu.set_reg(REG_R0, 0);
        return;
    }

    const uint32_t r0 = cpu.get_reg(REG_R0);
    const uint32_t r1 = cpu.get_reg(REG_R1);
    const uint32_t r2 = cpu.get_reg(REG_R2);
    const uint32_t r3 = cpu.get_reg(REG_R3);
    const uint32_t r6 = cpu.get_reg(REG_R6);
    const bool is_thunk = r1 >= 0xFF000000;
    const uint32_t arg1 = is_thunk ? cpu.get_reg(REG_R5) : r1;
    const uint32_t arg2 = is_thunk ? cpu.get_reg(REG_R6) : r2;
    const uint32_t arg3 = is_thunk ? cpu.get_reg(REG_R7) : r3;
    ObjectInfo* self = find_object(r0);

    printf("  [BrewAppUI] %s slot=%d R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
           class_name(clsid), slot, r0, r1, r2, r3);

    if (slot == 0 || slot == 1) {
        cpu.set_reg(REG_R0, 1);
        return;
    }

    if (slot == 2) {
        bool supported = false;
        switch (arg1) {
            case AEEIID_IQI:
                supported = true;
                break;
            case AEEIID_IModel:
                supported = is_model_clsid(clsid);
                break;
            case AEEIID_ITextModel:
                supported = clsid == AEEIID_ITextModel;
                break;
            case AEEIID_IValueModel:
                supported = clsid == AEEIID_IValueModel || clsid == AEECLSID_VALUEMODEL_1;
                break;
            case AEEIID_IHandler:
            case AEEIID_IWidget:
                supported = !is_model_clsid(clsid);
                break;
            case AEEIID_ICanvas:
            case AEEIID_IDisplayCanvas:
                supported = clsid == AEECLSID_CDisplayCanvas;
                break;
            case AEEIID_IContainer:
            case AEEIID_IXYContainer:
                supported = clsid == AEECLSID_CRootContainer || clsid == AEECLSID_CXYContainer;
                break;
            case AEEIID_IRootContainer:
                supported = clsid == AEECLSID_CRootContainer;
                break;
            default:
                supported = arg1 == clsid;
                break;
        }
        if (arg2 && arg2 < 0xFF000000) {
            memory_.write_value(arg2, supported ? r0 : 0u);
        }
        cpu.set_reg(REG_R0, supported ? 0u : 1u);
        return;
    }

    if (is_model_clsid(clsid)) {
        if (slot == 3) {
            if (self) {
                self->props[slot] = arg1;
            }
            cpu.set_reg(REG_R0, 0);
            return;
        }
        if (slot == 4) {
            cpu.set_reg(REG_R0, 0);
            return;
        }
        if (clsid == AEEIID_ITextModel) {
            if (slot == 5) {
                if (self) {
                    self->text = read_aechar(memory_, arg1, static_cast<int>(arg2));
                    self->sel_start = static_cast<int>(self->text.size());
                    self->sel_end = self->sel_start;
                    copy_text_to_owner(*self);
                    printf("  [BrewAppUI] ITextModel_ReplaceSel '%s'\n", self->text.c_str());
                }
                cpu.set_reg(REG_R0, 0);
                return;
            }
            if (slot == 6) {
                if (self) {
                    write_aechar(memory_, arg1, self->text, static_cast<int>(arg2));
                }
                cpu.set_reg(REG_R0, 0);
                return;
            }
            if (slot == 7) {
                if (self && valid_guest_ptr(arg1)) {
                    memory_.write_value(arg1 + 0, ensure_text_buffer(*self));
                    memory_.write_value(arg1 + 4, static_cast<uint32_t>(self->text.size()));
                    memory_.write_value(arg1 + 8, static_cast<uint32_t>(self->sel_start));
                    memory_.write_value(arg1 + 12, static_cast<uint32_t>(self->sel_end));
                }
                cpu.set_reg(REG_R0, 0);
                return;
            }
            if (slot == 8) {
                if (self) {
                    self->sel_start = static_cast<int>(arg1);
                    self->sel_end = static_cast<int>(arg2);
                }
                cpu.set_reg(REG_R0, 0);
                return;
            }
        }
        if (clsid == AEEIID_IValueModel || clsid == AEECLSID_VALUEMODEL_1) {
            if (slot == 5) {
                if (self) {
                    self->value_ptr = arg1;
                    self->value_len = static_cast<int>(arg2);
                    if (valid_guest_ptr(arg1) && arg2 != 0 && arg2 < 1024) {
                        self->text = read_aechar(memory_, arg1, static_cast<int>(arg2));
                        copy_text_to_owner(*self);
                    }
                }
                cpu.set_reg(REG_R0, 0);
                return;
            }
            if (slot == 6) {
                if (self) {
                    if (valid_guest_ptr(arg1)) {
                        memory_.write_value(arg1, static_cast<uint32_t>(self->value_len));
                    }
                    cpu.set_reg(REG_R0, self->text.empty() ? self->value_ptr : ensure_text_buffer(*self));
                } else {
                    cpu.set_reg(REG_R0, 0);
                }
                return;
            }
            if (slot == 7 || slot == 8) {
                cpu.set_reg(REG_R0, 0);
                return;
            }
        }
    }

    if (is_container_clsid(clsid)) {
        if (slot == 3) {
            if (self) {
                self->dirty = true;
                render_tree(r0);
            }
            cpu.set_reg(REG_R0, 0);
            return;
        }
        if (slot == 4) {
            if (valid_guest_ptr(arg2)) {
                memory_.write_value(arg2, r0);
            }
            if (valid_guest_ptr(arg3)) {
                memory_.write_value(arg3 + 0, 0);
                memory_.write_value(arg3 + 4, 0);
                memory_.write_value(arg3 + 8, 0);
                memory_.write_value(arg3 + 12, 0);
            }
            if (self) {
                render_tree(r0);
            }
            cpu.set_reg(REG_R0, 0);
            return;
        }
        if (slot == 5) {
            add_child(r0, arg1);
            if (valid_guest_ptr(arg3)) {
                ObjectInfo* child = find_object(arg1);
                if (child) {
                    child->x = static_cast<int>(memory_.read_value(arg3 + 0));
                    child->y = static_cast<int>(memory_.read_value(arg3 + 4));
                    child->visible = memory_.read_value(arg3 + 8) != 0;
                }
            }
            printf("  [BrewAppUI] %s insert child 0x%08x\n", class_name(clsid), arg1);
            render_tree(r0);
            cpu.set_reg(REG_R0, 0);
            return;
        }
        if (slot == 6) {
            ObjectInfo* child = find_object(arg1);
            if (self && child && child->parent == r0) {
                self->children.erase(std::remove(self->children.begin(), self->children.end(), arg1), self->children.end());
                child->parent = 0;
            }
            cpu.set_reg(REG_R0, 0);
            return;
        }
        if (slot == 7) {
            addr_t result = 0;
            if (self && !self->children.empty()) {
                if (arg1 == 0) {
                    result = arg2 ? self->children.front() : self->children.back();
                } else {
                    auto it = std::find(self->children.begin(), self->children.end(), arg1);
                    if (it != self->children.end()) {
                        if (arg2 && ++it != self->children.end()) {
                            result = *it;
                        } else if (!arg2 && it != self->children.begin()) {
                            result = *(--it);
                        } else if (arg3) {
                            result = arg2 ? self->children.front() : self->children.back();
                        }
                    }
                }
            }
            cpu.set_reg(REG_R0, result);
            return;
        }
        if (slot == 8) {
            add_child(r0, arg1);
            ObjectInfo* child = find_object(arg1);
            if (child && valid_guest_ptr(arg3)) {
                child->x = static_cast<int>(memory_.read_value(arg3 + 0));
                child->y = static_cast<int>(memory_.read_value(arg3 + 4));
                child->visible = memory_.read_value(arg3 + 8) != 0;
            }
            printf("  [BrewAppUI] %s setpos child 0x%08x\n", class_name(clsid), arg1);
            render_tree(r0);
            cpu.set_reg(REG_R0, 0);
            return;
        }
        if (slot == 9) {
            ObjectInfo* child = find_object(arg1);
            if (child && valid_guest_ptr(arg2)) {
                memory_.write_value(arg2 + 0, static_cast<uint32_t>(child->x));
                memory_.write_value(arg2 + 4, static_cast<uint32_t>(child->y));
                memory_.write_value(arg2 + 8, child->visible ? 1u : 0u);
            }
            cpu.set_reg(REG_R0, 0);
            return;
        }
    }

    if (clsid == AEECLSID_CRootContainer && slot == 10) {
        if (self) {
            self->props[slot] = arg1;
            self->dirty = true;
        }
        cpu.set_reg(REG_R0, 0);
        return;
    }

    if (clsid == AEECLSID_CDisplayCanvas) {
        if (slot == 3) {
            if (arg1 && arg1 < 0xFF000000) {
                memory_.write_value(arg1, shell_.get_display()->get_device_bitmap()->get_object_ptr());
            }
            cpu.set_reg(REG_R0, 0);
            return;
        }
        if (slot == 5) {
            if (arg1 && arg1 < 0xFF000000) {
                memory_.write_value(arg1 + 0, 0);
                memory_.write_value(arg1 + 4, 0);
                memory_.write_value(arg1 + 8, 640);
                memory_.write_value(arg1 + 12, 480);
            }
            cpu.set_reg(REG_R0, 0);
            return;
        }
        if (slot == 7) {
            if (arg1 && arg1 < 0xFF000000) {
                memory_.write_value(arg1, shell_.get_display()->get_object_ptr());
            }
            cpu.set_reg(REG_R0, 0);
            return;
        }
        if (slot == 8) {
            if (self) {
                self->props[slot] = arg1;
            }
            cpu.set_reg(REG_R0, 0);
            return;
        }
    }

    if (!is_model_clsid(clsid)) {
        if (slot == 4) {
            if (self) {
                self->props[slot] = arg1;
                if (clsid == AEECLSID_CForm) {
                    render_form(*self);
                }
            }
            cpu.set_reg(REG_R0, 0);
            return;
        }
        if (slot == 5 || slot == 6) {
            if (self && valid_guest_ptr(arg1)) {
                const int width = std::max(self->width, static_cast<int>(self->text.size()) * 6 + 6);
                const int height = std::max(self->height, 12);
                memory_.write_value(arg1 + 0, static_cast<uint32_t>(width));
                memory_.write_value(arg1 + 4, static_cast<uint32_t>(height));
            }
            cpu.set_reg(REG_R0, 0);
            return;
        }
        if (slot == 7) {
            if (self && valid_guest_ptr(arg1)) {
                self->width = static_cast<int>(memory_.read_value(arg1 + 0));
                self->height = static_cast<int>(memory_.read_value(arg1 + 4));
                self->dirty = true;
            }
            cpu.set_reg(REG_R0, 0);
            return;
        }
        if (slot == 8) {
            if (valid_guest_ptr(arg1)) {
                memory_.write_value(arg1, self ? self->parent : 0u);
            }
            cpu.set_reg(REG_R0, 0);
            return;
        }
        if (slot == 9) {
            if (self) {
                self->parent = arg1;
            }
            cpu.set_reg(REG_R0, 0);
            return;
        }
        if (slot == 10) {
            if (self) {
                const int old_x = self->x;
                const int old_y = self->y;
                self->x += static_cast<int>(arg2);
                self->y += static_cast<int>(arg3);
                render_tree(r0);
                self->x = old_x;
                self->y = old_y;
            }
            cpu.set_reg(REG_R0, 0);
            return;
        }
        if (slot == 11) {
            cpu.set_reg(REG_R0, 0);
            return;
        }
        if (slot == 12) {
            addr_t model = self ? get_model(*self, arg1) : 0;
            if (valid_guest_ptr(arg2)) {
                memory_.write_value(arg2, model);
            }
            cpu.set_reg(REG_R0, model ? 0u : 1u);
            return;
        }
        if (slot == 13) {
            if (self) {
                self->props[slot] = arg1;
            }
            cpu.set_reg(REG_R0, 0);
            return;
        }
    }

    if (clsid == AEECLSID_CForm && slot == 3 && arg1 == 0x801 && arg2 == 0x5000) {
        if (self) {
            self->root_child = arg3;
            self->dirty = true;
        }
        printf("  [BrewAppUI] CForm root child=0x%08x\n", arg3);
        cpu.set_reg(REG_R0, 1);
        return;
    }

    if (clsid == AEECLSID_CForm && slot == 4) {
        if (self) {
            render_form(*self);
        }
        cpu.set_reg(REG_R0, 1);
        return;
    }

    if (clsid == AEECLSID_IMAGEWIDGET_1 && slot == 3 && arg1 == 0x800 && arg2 == 0x414) {
        // Z-Wheel's splash path receives the decoded IImage through the guest
        // callback-preserved R6 while configuring the image widget.
        if (self && r6 && r6 < 0xFF000000) {
            self->image_object = r6;
            self->dirty = true;
            printf("  [BrewAppUI] IMAGEWIDGET_1 image object=0x%08x\n", r6);
            render_tree(r0);
        }
        cpu.set_reg(REG_R0, 1);
        return;
    }

    if (slot == 3 && self) {
        if (arg1 == 0x801) {
            self->props[arg2] = arg3;
            self->dirty = true;
        } else if (arg1 == 0x800 && arg3 && arg3 < 0xFF000000) {
            auto it = self->props.find(arg2);
            memory_.write_value(arg3, it == self->props.end() ? 0u : it->second);
        }
        cpu.set_reg(REG_R0, 1);
        return;
    }

    if ((arg1 & 0x800u) != 0) {
        printf("  [BrewAppUI_%08x_slot%d] not implemented yet generic op=0x%08x arg2=0x%08x arg3=0x%08x\n",
               clsid, slot, arg1, arg2, arg3);
        cpu.set_reg(REG_R0, 1);
        return;
    }

    printf("  [BrewAppUI_%08x_slot%d] not implemented yet arg1=0x%08x arg2=0x%08x arg3=0x%08x\n",
           clsid, slot, arg1, arg2, arg3);
    cpu.set_reg(REG_R0, 0);
}
