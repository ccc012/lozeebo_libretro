#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Texture;

struct AppEntry {
    std::string mif_path;
    std::string mod_path;
    std::string package_root;
    std::string mod_name;
    std::string name;
    std::string mif_name;
    uint32_t app_id = 0;
    uint32_t module_id = 0;
    uint32_t clsid = 0;
    uint32_t mif_version = 0;
    uint16_t priv_level = 0;
    uint16_t class_count = 0;
    uint16_t applet_count = 0;
    uint16_t ext_class_count = 0;
    bool official_name = false;

    // All readable text strings recovered from the MIF (name, version, vendor,
    // copyright, etc.) and the detected version-like string, for richer display.
    std::vector<std::string> text_strings;
    std::string version_text;

    // All images embedded in the MIF (a MIF can carry more than one: icon,
    // banner, splash, etc.). image_data keeps the primary (first) image for
    // backward compatibility.
    std::vector<uint8_t> image_data;
    std::vector<std::vector<uint8_t>> images;

    // GPU textures uploaded by the GUI. icon/icon_w/icon_h is the primary
    // (icons[0]); icons holds every uploaded image for the detail strip.
    struct IconTexture {
        SDL_Texture* tex = nullptr;
        float w = 0;
        float h = 0;
    };
    std::vector<IconTexture> icons;
    SDL_Texture* icon = nullptr;
    float icon_w = 0;
    float icon_h = 0;

    // Library category for a quick visual badge: Homebrew (under roms/hb),
    // Commercial (resolved from the official Zeebo catalog), or plain Brew.
    enum class Category { Brew, Commercial, Homebrew };
    Category category = Category::Brew;

    // Optional full box art + description loaded from the Z-Wheel catalog SQLite
    // (built by tools/misc/zwheel_catalog.py), matched by CLSID. Only commercial
    // titles present in that DB get these; everything else shows metadata only.
    SDL_Texture* boxart = nullptr;
    float boxart_w = 0;
    float boxart_h = 0;
    std::string description_pt;
    std::string description_en;
};

std::vector<AppEntry> scan_apps(const std::string& base);

// Resolve an applet CLSID from the MIF accompanying a .mod file by walking up to
// the package root. Returns 0 if no MIF/CLSID can be found.
uint32_t resolve_clsid_for_mod(const std::string& mod_path);

int run_gui();