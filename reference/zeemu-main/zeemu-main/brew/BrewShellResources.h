#ifndef ZEEMU_BREW_SHELL_RESOURCES_H_
#define ZEEMU_BREW_SHELL_RESOURCES_H_

#include "brew/BrewShell.h"
#include <string>
#include <vector>

struct BarResource {
    uint16_t base_id = 0;
    uint16_t type = 0;
    struct Entry {
        uint16_t id = 0;
        std::string mime;
        std::vector<uint8_t> raw;
        std::vector<uint8_t> data;
    };
    std::vector<Entry> entries;
};

constexpr uint32_t RESTYPE_IMAGE = 6;

bool parse_bar_resource_file(const std::string& data, BarResource& out);
const BarResource::Entry* find_bar_entry(const BarResource& bar, uint16_t res_id);
const BarResource::Entry* find_bar_image_entry(const BarResource& bar, uint16_t res_id);
std::string mime_from_path(const std::string& path);
bool load_resource_payload(
    BrewShell& shell,
    const std::string& res_file,
    uint16_t res_id,
    std::vector<uint8_t>& out_data,
    std::string& out_mime,
    bool* out_from_bar = nullptr,
    std::vector<uint8_t>* out_raw = nullptr);
std::vector<uint8_t> make_res_blob(const std::vector<uint8_t>& payload, const std::string& mime);
bool load_string_payload(
    BrewShell& shell,
    const std::string& res_file,
    uint16_t res_id,
    std::u16string& out_text);

#endif
