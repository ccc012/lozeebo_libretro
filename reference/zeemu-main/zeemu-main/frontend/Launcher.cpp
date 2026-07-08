#include "frontend/Launcher.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <tuple>
#include <unordered_map>

#if __has_include("third_party/stb_image.h")
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_NO_FAILURE_STRINGS
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "third_party/stb_image.h"
#pragma GCC diagnostic pop
#define ZEEMU_HAS_STB_IMAGE 1
#else
#define ZEEMU_HAS_STB_IMAGE 0
#endif

#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#if __has_include("third_party/sqlite/sqlite3.h")
#include "third_party/sqlite/sqlite3.h"
#define ZEEMU_HAS_SQLITE 1
#else
#define ZEEMU_HAS_SQLITE 0
#endif

int run_emulator(const std::string& mod_path, uint32_t clsid);

struct DebugTraceOption {
    const char* name;
    const char* category;
    const char* description;
    const char* value_hint;
};

struct KeyboardProfileOption {
    const char* label;
    const char* env_value;
    const char* description;
};

struct GamepadProfileOption {
    const char* label;
    const char* env_value;
    const char* description;
};

static constexpr KeyboardProfileOption kKeyboardProfileOptions[] = {
    {"Standard Keys", "standard", "Keyboard arrows/buttons send BREW AVK events and Zeebo HID state."},
    {"BREW Keys Only", "brew-menu", "Arrows, Enter, Space, Escape, and Backspace send only BREW AVK events."},
};

static constexpr size_t kKeyboardProfileOptionCount = std::size(kKeyboardProfileOptions);

static constexpr GamepadProfileOption kGamepadProfileOptions[] = {
    {"Standard Controller", "standard", "D-pad and buttons send BREW AVK events and Zeebo HID state."},
    {"Off", "off", "Ignore SDL gamepad input for this run."},
};

static constexpr size_t kGamepadProfileOptionCount = std::size(kGamepadProfileOptions);

static void apply_sdl_gamepad_env_hints() {
    if (const char* mapping = std::getenv("ZEEMU_SDL_GAMEPAD_MAPPING"); mapping && *mapping) {
        SDL_SetHint(SDL_HINT_GAMECONTROLLERCONFIG, mapping);
    }
    if (const char* mapping_file = std::getenv("ZEEMU_SDL_GAMEPAD_MAPPING_FILE"); mapping_file && *mapping_file) {
        SDL_SetHint(SDL_HINT_GAMECONTROLLERCONFIG_FILE, mapping_file);
    }
    if (std::getenv("ZEEMU_INPUT_BACKGROUND")) {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    }
}

static constexpr DebugTraceOption kDebugTraceOptions[] = {
    {"ZEEMU_TRACE_APPINSTANCE", "BREW/HLE", "Logs app instance helper details while applets are created.", nullptr},
    {"ZEEMU_TRACE_BITMAP", "BREW/HLE", "Logs bitmap creation and bitmap image loading paths.", nullptr},
    {"ZEEMU_TRACE_FILE_IO", "BREW/HLE", "Logs FileMgr and IFile open, seek, read, write, info, remove, and enum calls.", nullptr},
    {"ZEEMU_TRACE_FILE_READS", "BREW/HLE", "Logs large IFile_Read transfers and low-address read shadow handling.", nullptr},
    {"ZEEMU_TRACE_FREE_BYTES", "BREW/HLE", "Dumps bytes from pointers passed to FREE-style helper calls.", nullptr},
    {"ZEEMU_TRACE_HLE", "BREW/HLE", "Enables verbose HLE hook dispatch logging.", nullptr},
    {"ZEEMU_TRACE_INPUT", "BREW/HLE", "Logs SDL key mapping, BREW key dispatch/fallback, HID button queues, input signals, and HID drains.", nullptr},
    {"ZEEMU_TRACE_INLINE_SVC", "BREW/HLE", "Logs inline SVC hook dispatch while creating app/module instances.", nullptr},
    {"ZEEMU_TRACE_MEMOPS", "BREW/HLE", "Logs helper memory operation buffers such as MEMCPY/MEMSET-style calls.", nullptr},
    {"ZEEMU_TRACE_RESOURCES", "BREW/HLE", "Logs resource lookups, BAR/cache reads, cache hits, and resource fallback decisions.", nullptr},
    {"ZEEMU_TRACE_STRINGS", "BREW/HLE", "Logs string helper inputs and outputs.", nullptr},
    {"ZEEMU_TRACE_TEXT_BYTES", "BREW/HLE", "Logs text rendering byte buffers and decoded text paths.", nullptr},
    {"ZEEMU_TRACE_TIME", "BREW/HLE", "Logs time/date shell calls.", nullptr},
    {"ZEEMU_TRACE_TIMERS", "BREW/HLE", "Logs timer registration, cancellation, and timer queue decisions.", nullptr},
    {"ZEEMU_TRACE_UNZIP", "BREW/HLE", "Logs IUnzipAStream chunk reads in addition to stream setup and short-read markers.", nullptr},

    {"ZEEMU_TRACE_MEDIA", "Media", "Logs IMedia/MediaPCM calls, playback setup, and audio resource load/copy timings.", nullptr},

    {"ZEEMU_TRACE_GLES_TEXTURES", "Graphics", "Logs GLES texture state, texture upload hints, and texture binding decisions.", nullptr},
    {"ZEEMU_TRACE_GLES_VERTS", "Graphics", "Logs GLES vertex/draw state, transformed coordinates, UVs, and draw inputs.", nullptr},
    {"ZEEMU_TRACE_GLES_TEXT", "Graphics", "Lifts the per-draw GLES trace cap so later text/font glyph draws (tex=2) also log uv-summary/draw-state and per-tri UVs.", nullptr},
    {"ZEEMU_TRACE_QXGL", "Graphics", "Logs QXGL fixed-function call state such as matrix, viewport, VBO, clear, and skipped draw details.", nullptr},
    {"ZEEMU_TRACE_RENDER", "Graphics", "Logs SDL renderer presentation operations.", nullptr},
    {"ZEEMU_TRACE_RENDER_PROFILE", "Graphics", "Logs per-swap guest GL renderer timing and triangle/pixel counts.", nullptr},
    {"ZEEMU_TRACE_TEXTURE_UPLOADS", "Graphics", "Logs renderer texture upload operations.", nullptr},
    {"ZEEMU_DUMP_GUEST_GL_FRAME", "Graphics", "Writes the first swapped guest GL framebuffer to logs/guest_gl_frame.ppm.", nullptr},
    {"ZEEMU_DUMP_GUEST_TEX", "Graphics", "Dumps every decoded guest texture to logs/guesttex_<id>_<w>x<h>.ppm (RGB) plus a _a.pgm alpha map, for atlas/glyph inspection.", nullptr},

    {"ZEEMU_TRACE_BRANCHES", "CPU/Guest", "Logs branch activity in app/module bootstrap execution.", nullptr},
    {"ZEEMU_TRACE_BRANCH_HIGH", "CPU/Guest", "Logs high-address ARM branch targets from the CPU backend.", nullptr},
    {"ZEEMU_TRACE_CPU_PC", "CPU/Guest", "Logs PC/op/CPSR/registers when execution is inside the given address range.", "addr+size"},
    {"ZEEMU_TRACE_FASTPATHS", "CPU/Guest", "Logs guest fast paths and opcode-block accelerators when they fire.", nullptr},
    {"ZEEMU_TRACE_GUEST_PROGRESS", "CPU/Guest", "Logs guest callback progress counters and recent PC history.", nullptr},
    {"ZEEMU_TRACE_GUEST_PROGRESS_STEP", "CPU/Guest", "Sets the guest progress logging interval in executed instructions.", "steps"},
    {"ZEEMU_TRACE_MEMWATCH", "CPU/Guest", "Logs guest memory accesses that overlap the given address range.", "addr+size"},
    {"ZEEMU_TRACE_THREADS", "CPU/Guest", "Logs cooperative guest thread scheduling and thread slice execution.", nullptr},

    {"ZEEMU_TRACE_LOOP", "Launcher/App Loop", "Logs the host app loop, event polling, timer firing, and frame presentation stages.", nullptr},
    {"ZEEMU_TRACE_PETECA", "Launcher/App Loop", "Enables Peteca-specific state dumps in the launcher bringup path.", nullptr},
};

static constexpr size_t kDebugTraceOptionCount = sizeof(kDebugTraceOptions) / sizeof(kDebugTraceOptions[0]);

static void set_process_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value && value[0] != '\0') {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
#endif
}

static bool trace_option_needs_value(const DebugTraceOption& option) {
    return option.value_hint != nullptr;
}

static const char* trace_option_value(const char* name,
                                      const char* cpu_pc,
                                      const char* guest_progress_step,
                                      const char* memwatch) {
    const std::string key(name);
    if (key == "ZEEMU_TRACE_CPU_PC") return cpu_pc;
    if (key == "ZEEMU_TRACE_GUEST_PROGRESS_STEP") return guest_progress_step;
    if (key == "ZEEMU_TRACE_MEMWATCH") return memwatch;
    return "1";
}

static void apply_debug_trace_env(const std::array<bool, kDebugTraceOptionCount>& enabled,
                                  const char* cpu_pc,
                                  const char* guest_progress_step,
                                  const char* memwatch) {
    for (size_t i = 0; i < kDebugTraceOptionCount; ++i) {
        const DebugTraceOption& option = kDebugTraceOptions[i];
        const char* value = enabled[i] ? trace_option_value(option.name, cpu_pc, guest_progress_step, memwatch) : "";
        if (trace_option_needs_value(option) && (!value || value[0] == '\0')) {
            value = "";
        }
        set_process_env(option.name, value);
    }
}

static void write_launch_log_header(const std::string& mod_path,
                                    uint32_t clsid,
                                    bool fast,
                                    const char* keyboard_profile,
                                    const char* gamepad_profile,
                                    const std::array<bool, kDebugTraceOptionCount>& enabled,
                                    const char* cpu_pc,
                                    const char* guest_progress_step,
                                    const char* memwatch) {
    std::cout << "=== Zeemu Launcher Run ===" << std::endl;
    std::cout << "command: " << (fast ? "run-app-fast" : "run-app")
              << " " << mod_path
              << " 0x" << std::hex << std::setw(8) << std::setfill('0') << clsid
              << std::dec << std::setfill(' ') << std::endl;
    std::cout << "fast: " << (fast ? "1" : "0") << std::endl;
    std::cout << "keyboard_profile: " << (keyboard_profile ? keyboard_profile : "hid") << std::endl;
    std::cout << "gamepad_profile: " << (gamepad_profile ? gamepad_profile : "off") << std::endl;
    std::cout << "trace flags:" << std::endl;

    bool any_trace = false;
    for (size_t i = 0; i < kDebugTraceOptionCount; ++i) {
        const DebugTraceOption& option = kDebugTraceOptions[i];
        if (!enabled[i]) {
            continue;
        }
        const char* value = trace_option_value(option.name, cpu_pc, guest_progress_step, memwatch);
        if (trace_option_needs_value(option) && (!value || value[0] == '\0')) {
            continue;
        }
        any_trace = true;
        std::cout << "  " << option.name << "=" << (value ? value : "1") << std::endl;
    }

    if (!any_trace) {
        std::cout << "  none" << std::endl;
    }
    std::cout << "==========================" << std::endl;
    std::cout.flush();
}

static void draw_debug_trace_menu(std::array<bool, kDebugTraceOptionCount>& enabled,
                                  char* cpu_pc,
                                  size_t cpu_pc_size,
                                  char* guest_progress_step,
                                  size_t guest_progress_step_size,
                                  char* memwatch,
                                  size_t memwatch_size) {
    if (!ImGui::BeginMenu("Debug")) {
        return;
    }
    if (ImGui::MenuItem("Clear Trace Toggles")) {
        enabled.fill(false);
    }
    ImGui::SeparatorText("Trace Environment");

    const char* current_category = "";
    for (size_t i = 0; i < kDebugTraceOptionCount; ++i) {
        const DebugTraceOption& option = kDebugTraceOptions[i];
        if (std::string(current_category) != option.category) {
            current_category = option.category;
            ImGui::SeparatorText(current_category);
        }

        ImGui::PushID(static_cast<int>(i));
        ImGui::Checkbox(option.name, &enabled[i]);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", option.description);
        }
        if (trace_option_needs_value(option)) {
            ImGui::SameLine();
            const std::string key(option.name);
            if (key == "ZEEMU_TRACE_CPU_PC") {
                ImGui::SetNextItemWidth(150.0f);
                ImGui::InputTextWithHint("##value", option.value_hint, cpu_pc, cpu_pc_size);
            } else if (key == "ZEEMU_TRACE_GUEST_PROGRESS_STEP") {
                ImGui::SetNextItemWidth(110.0f);
                ImGui::InputTextWithHint("##value", option.value_hint, guest_progress_step, guest_progress_step_size);
            } else if (key == "ZEEMU_TRACE_MEMWATCH") {
                ImGui::SetNextItemWidth(150.0f);
                ImGui::InputTextWithHint("##value", option.value_hint, memwatch, memwatch_size);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Value format: %s", option.value_hint);
            }
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Descriptions are documented in status/debug-traces.md");
    ImGui::EndMenu();
}

// ---------------------------------------------------------------------------
// MIF discovery
// ---------------------------------------------------------------------------

static uint16_t read_le16(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
}

static uint32_t read_le32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return (uint32_t)data[offset] |
           ((uint32_t)data[offset + 1] << 8) |
           ((uint32_t)data[offset + 2] << 16) |
           ((uint32_t)data[offset + 3] << 24);
}

static bool is_decimal_string(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
}

static std::string lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return s;
}

static std::string normalize_path_string(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

static std::string short_path(const std::string& path) {
    std::string s = normalize_path_string(path);
    const std::string marker = "roms/";
    size_t pos = lower_ascii(s).find(marker);
    if (pos != std::string::npos) {
        return s.substr(pos);
    }
    return s;
}

static uint32_t find_numeric_path_component(const std::filesystem::path& path) {
    for (const auto & it : path) {
        const std::string part = it.string();
        if (is_decimal_string(part)) {
            try {
                return static_cast<uint32_t>(std::stoul(part));
            } catch (...) {
                return 0;
            }
        }
    }
    return 0;
}

static bool is_mif_extension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".mif";
}

static bool is_mod_extension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".mod";
}

static std::string utf8_from_codepoint(uint32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

static bool looks_like_version_string(const std::string& s) {
    if (s.empty()) return false;
    bool saw_digit = false;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            saw_digit = true;
        } else if (c != '.') {
            return false;
        }
    }
    return saw_digit;
}

static bool looks_like_app_name(const std::string& s) {
    if (s.size() <= 2 || s.find('=') != std::string::npos || looks_like_version_string(s))
        return false;
    const std::string lower = lower_ascii(s);
    if (lower.find(".mif") != std::string::npos || lower.find(".mod") != std::string::npos ||
        lower.find("qualcomm") != std::string::npos || lower.find("brew") != std::string::npos ||
        lower.find("sigtool") != std::string::npos || lower.find("root ca") != std::string::npos ||
        lower.find("(c)") != std::string::npos || lower.find("copyright") != std::string::npos ||
        lower.find("author") != std::string::npos || lower.find("corporation") != std::string::npos ||
        lower.find(" inc") != std::string::npos) {
        return false;
    }
    for (char c : s) {
        if (static_cast<unsigned char>(c) >= 0x80 || std::isalnum(static_cast<unsigned char>(c)))
            return true;
    }
    return false;
}

struct MifTextEntry {
    size_t offset = 0;
    std::string value;
};

static bool is_clean_mif_text(const std::string& s, bool ascii_prefixed) {
    if (s.size() < 2) return false;
    for (unsigned char c : s) {
        if (c < 0x20 && c != '\t') return false;
        if (ascii_prefixed && c >= 0x80) return false;
    }
    return true;
}

static std::vector<MifTextEntry> collect_mif_text_entries(const std::vector<uint8_t>& data) {
    std::vector<MifTextEntry> strings;
    strings.reserve(64);

    for (size_t i = 0; i + 2 < data.size();) {
        if (data[i] == 0xFF && data[i + 1] == 0xFE) {
            std::string s;
            size_t j = i + 2;
            for (; j + 1 < data.size(); j += 2) {
                uint16_t ch = read_le16(data, j);
                if (ch == 0 || ch == 0xFEFF || ch < 0x20 || ch >= 0x1000) break;
                if (ch >= 0x20) s += utf8_from_codepoint(ch);
            }
            if (is_clean_mif_text(s, false)) {
                strings.push_back({i, std::move(s)});
                i = std::max(i + 1, j);
                continue;
            }
        } else if (data[i] == 0x03) {
            std::string s;
            size_t j = i + 1;
            for (; j < data.size() && j < i + 96; ++j) {
                uint8_t ch = data[j];
                if (ch < 0x20 || ch == 0xFF) break;
                s.push_back(static_cast<char>(ch));
            }
            if (is_clean_mif_text(s, true)) {
                strings.push_back({i, std::move(s)});
                i = std::max(i + 1, j);
                continue;
            }
        }
        ++i;
    }
    return strings;
}

static bool is_plausible_clsid(uint32_t clsid) {
    if (clsid == 0 || clsid == 0xFFFFFFFF) return false;
    if (clsid == 0xBF2E2021) return true; // Zenonia uses a Korean publisher range.
    // Zeebo app CLSIDs include both normal BREW/QVERSION IDs and a few low legacy IDs.
    return clsid < 0x02000000;
}

static std::pair<size_t, uint32_t> find_mif_app_clsid_by_record_scan(const std::vector<uint8_t>& data, size_t min_offset) {
    for (size_t i = min_offset; i + 20 <= data.size(); ++i) {
        if (read_le32(data, i + 4) != 1) continue;
        if (read_le32(data, i + 8) != 0 || read_le32(data, i + 12) != 0) continue;
        uint32_t clsid = read_le32(data, i + 16);
        if (is_plausible_clsid(clsid)) {
            return {i, clsid};
        }
    }
    return {0, 0};
}

static std::pair<size_t, uint32_t> find_mif_app_clsid_by_offset_table(const std::vector<uint8_t>& data) {
    if (data.size() < 0x18) return {0, 0};

    uint32_t table_offset = read_le32(data, 0x10);
    uint32_t table_count = read_le32(data, 0x14);
    if (table_offset >= data.size() || table_count > 256 ||
        table_offset + table_count * 4ull > data.size()) {
        return {0, 0};
    }

    std::vector<size_t> offsets;
    offsets.reserve(table_count);
    for (uint32_t i = 0; i < table_count; ++i) {
        uint32_t offset = read_le32(data, table_offset + i * 4);
        if (offset < data.size()) offsets.push_back(offset);
    }

    size_t marker_index = offsets.size();
    for (size_t i = 0; i < offsets.size(); ++i) {
        uint32_t marker = read_le32(data, offsets[i]);
        if (marker == 0x00001000 || marker == 0x00001002) {
            marker_index = i;
        }
    }
    if (marker_index == offsets.size()) return {0, 0};

    for (size_t i = marker_index + 1; i < offsets.size(); ++i) {
        size_t offset = offsets[i];
        uint32_t clsid = read_le32(data, offset);
        if (!is_plausible_clsid(clsid)) continue;

        // Some launcher MIFs expose the app CLSID as a second table entry
        // inside the same short class record.
        if (i + 1 < offsets.size() && offsets[i + 1] == offset + 8) {
            uint32_t nested_clsid = read_le32(data, offsets[i + 1]);
            if (is_plausible_clsid(nested_clsid)) {
                return {offsets[i + 1], nested_clsid};
            }
        }
        return {offset, clsid};
    }

    return {0, 0};
}

// Extract every embedded image (JPEG/PNG/BMP) from a MIF. A MIF can carry
// several images (icon, banner, splash); each found blob advances the scan
// cursor past its end so the next one is found too.
static std::vector<std::vector<uint8_t>> extract_images_from_mif(const std::vector<uint8_t>& data) {
    std::vector<std::vector<uint8_t>> images;
    constexpr size_t kMaxImages = 16;
    for (size_t i = 0; i + 3 < data.size() && images.size() < kMaxImages; ) {
        size_t consumed = 0;
        // JPEG: FF D8 FF ... FF D9
        if (data[i] == 0xFF && data[i+1] == 0xD8 && data[i+2] == 0xFF) {
            for (size_t j = i + 2; j + 1 < data.size(); j++) {
                if (data[j] == 0xFF && data[j+1] == 0xD9) {
                    images.emplace_back(data.begin() + i, data.begin() + j + 2);
                    consumed = (j + 2) - i;
                    break;
                }
            }
        }
        // PNG: 89 50 4E 47 0D 0A 1A 0A ... IEND (49 45 4E 44 AE 42 60 82)
        if (!consumed && i + 8 <= data.size() &&
            data[i]==0x89 && data[i+1]==0x50 && data[i+2]==0x4E && data[i+3]==0x47 &&
            data[i+4]==0x0D && data[i+5]==0x0A && data[i+6]==0x1A && data[i+7]==0x0A) {
            for (size_t j = i + 8; j + 7 < data.size(); j++) {
                if (data[j]==0x49 && data[j+1]==0x45 && data[j+2]==0x4E && data[j+3]==0x44 &&
                    data[j+4]==0xAE && data[j+5]==0x42 && data[j+6]==0x60 && data[j+7]==0x82) {
                    images.emplace_back(data.begin() + i, data.begin() + j + 8);
                    consumed = (j + 8) - i;
                    break;
                }
            }
        }
        // BMP: "BM" + uint32 file size
        if (!consumed && i + 6 <= data.size() && data[i] == 0x42 && data[i+1] == 0x4D) {
            const uint32_t sz = static_cast<uint32_t>(data[i + 2]) | (static_cast<uint32_t>(data[i + 3])<<8) |
                          (static_cast<uint32_t>(data[i + 4])<<16) | (static_cast<uint32_t>(data[i + 5])<<24);
            if (sz > 14 && sz < 4*1024*1024 && i + sz <= data.size()) {
                images.emplace_back(data.begin() + i, data.begin() + i + sz);
                consumed = sz;
            }
        }
        i += consumed ? consumed : 1;
    }
    return images;
}

static bool may_contain_package_images(const std::filesystem::path& path) {
    std::string ext = lower_ascii(path.extension().string());
    return ext == ".aez" || ext == ".bar" || ext == ".dat" || ext == ".bin" ||
           ext == ".pak" || ext == ".pack" || ext == ".res";
}

static std::vector<std::vector<uint8_t>> extract_images_from_package_files(const std::string& package_root) {
    namespace fs = std::filesystem;
    std::vector<std::vector<uint8_t>> images;
    constexpr size_t kMaxFallbackImages = 16;
    std::error_code ec;
    if (package_root.empty() || !fs::is_directory(package_root, ec)) {
        return images;
    }

    std::vector<fs::path> candidates;
    for (const auto& entry : fs::recursive_directory_iterator(package_root, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || !may_contain_package_images(entry.path())) {
            continue;
        }
        candidates.push_back(entry.path());
    }
    std::sort(candidates.begin(), candidates.end(), [](const fs::path& a, const fs::path& b) {
        const std::string an = lower_ascii(a.filename().string());
        const std::string bn = lower_ascii(b.filename().string());
        const bool a_data = an.find("data") != std::string::npos;
        const bool b_data = bn.find("data") != std::string::npos;
        if (a_data != b_data) return a_data > b_data;
        return a.string() < b.string();
    });

    for (const auto& path : candidates) {
        std::ifstream f(path, std::ios::binary);
        if (!f) continue;
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
        std::vector<std::vector<uint8_t>> found = extract_images_from_mif(data);
        for (auto& image : found) {
            images.push_back(std::move(image));
            if (images.size() >= kMaxFallbackImages) {
                return images;
            }
        }
    }
    return images;
}

// Collect readable text strings for the info panel: app name, version, vendor,
// copyright, MIME, etc.
static std::vector<std::string> collect_mif_text(const std::vector<MifTextEntry>& entries) {
    std::vector<std::string> out;
    constexpr size_t kMaxStrings = 48;
    out.reserve(std::min(kMaxStrings, entries.size()));
    for (const auto& entry : entries) {
        if (out.size() >= kMaxStrings) {
            break;
        }
        if (std::find(out.begin(), out.end(), entry.value) == out.end()) {
            out.push_back(entry.value);
        }
    }
    return out;
}

static const std::unordered_map<uint32_t, const char*> kOfficialGameNames = {
    {12875,  "IMICRO3D"},
    {274259, "Action Hero 3D: Wild Dog"},
    {280386, "Alice no País das Maravilhas"},
    {279369, "Alien Breaker Deluxe"},
    {276151, "Alpine Racer"},
    {280214, "Armageddon Squadron"},
    {279888, "Bad Dudes vs. DragonNinja"},
    {277083, "Bejeweled Twist"},
    {278986, "Caveman Ninja"},
    {274214, "Crash Bandicoot Nitro Kart 3D"},
    {279233, "Dark Seal"},
    {280173, "Disney All Star Cards"},
    {274754, "Double Dragon"},
    {274803, "FIFA 09"},
    {277380, "Galaxy on Fire"},
    {279889, "Heavy Barrel"},
    {278200, "Heavy Weapon"},
    {280221, "Iron Sight"},
    {279126, "Karnov's Revenge"},
    {279200, "Magical Drop III"},
    {276121, "Need For Speed Carbon: Domine a Cidade"},
    {276212, "Pac-Mania"},
    {278962, "Peggle"},
    {280238, "Powerboat Challenge"},
    {276154, "Prey Evil"},
    {274802, "Quake"},
    {276153, "Quake II"},
    {280602, "Raging Thunder 2"},
    {278282, "Rally Master Pro"},
    {280394, "Reckless Racing"},
    {276675, "Resident Evil 4 Zeebo Edition"},
    {276152, "Ridge Racer"},
    {278987, "Spin Master"},
    {278988, "Street Hoop"},
    {279125, "Super BurgerTime"},
    {276731, "Tekken 2"},
    {280463, "Tork and Kral: A Prehistorik Adventure"},
    {278965, "Toy Raid"},
    {274804, "Treino Cerebral"},
    {280634, "Turma da Mónica em: Vamos Brincar Vol. 1"},
    {263019, "Ultimate Chess 3D"},
    {279036, "Um Jogo de Ovos"},
    {279173, "Wizard Fire"},
    {274755, "Z-Wheel"},
    {274791, "Zeebo App"},
    {277495, "Zeebo Channels (Opera Mini)"},
    {279394, "Zeebo Clube"},
    {277727, "Zeebo Extreme Baja"},
    {278285, "Zeebo Extreme B\xC3\xB3ia Cross"},
    {277285, "Zeebo Extreme Corrida A\xC3\xA9rea"},
    {278283, "Zeebo Extreme Jetboard"},
    {276809, "Zeebo Extreme Rolim\xC3\xA3"},
    {279380, "Zeebo F.C. Foot Camp"},
    {280647, "Zeebo F.C. Super League"},
    {277229, "Zeebo Family Pack"},
    {279159, "Zeebo Sports Peteca"},
    {278738, "Zeebo Sports Queimada"},
    {277534, "Zeebo Sports Ténis"},
    {278212, "Zeebo Sports Vólei"},
    {279382, "Zeeboids"},
    {277455, "Zenonia"},
    {279712, "Zuma's Revenge"},
    {271041, "Devil May Cry: Dante X Vergil (BREW)"},
};

static AppEntry parse_mif(const std::string& mif_path) {
    AppEntry e;
    e.mif_path = mif_path;
    std::filesystem::path path(mif_path);
    e.mif_name = path.filename().string();
    e.name = path.stem().string();
    e.app_id = find_numeric_path_component(path);
    if (e.app_id == 0 && is_decimal_string(e.name)) {
        e.app_id = static_cast<uint32_t>(std::stoul(e.name));
    }

    std::ifstream f(mif_path, std::ios::binary);
    if (!f) return e;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});

    // MIF magic: 11 00 01 00
    if (data.size() < 4 || data[0] != 0x11 || data[1] != 0x00 ||
        data[2] != 0x01 || data[3] != 0x00)
        return e;

    e.mif_version = read_le32(data, 0x00);
    e.module_id = read_le32(data, 0x04);
    e.priv_level = read_le16(data, 0x08);
    e.class_count = read_le16(data, 0x0a);
    e.applet_count = read_le16(data, 0x0c);
    e.ext_class_count = read_le16(data, 0x0e);

    auto [app_record_offset, clsid] = find_mif_app_clsid_by_offset_table(data);
    std::vector<MifTextEntry> strings = collect_mif_text_entries(data);
    size_t last_string_offset = 0;
    for (const auto& entry : strings) {
        if (!looks_like_app_name(entry.value)) continue;
        if (app_record_offset != 0 && entry.offset > app_record_offset) continue;
        if (entry.value.size() >= e.name.size() || is_decimal_string(e.name)) {
            e.name = entry.value;
        }
        last_string_offset = entry.offset;
    }

    if (clsid == 0) {
        auto [fst, snd] = find_mif_app_clsid_by_record_scan(data, last_string_offset);
        app_record_offset = fst;
        clsid = snd;
    }
    e.clsid = clsid;

    // Override with official name if game ID folder is known
    {
        namespace fs = std::filesystem;
        try {
            uint32_t game_id = e.app_id;
            if (game_id == 0 && is_decimal_string(path.parent_path().filename().string())) {
                game_id = static_cast<uint32_t>(std::stoul(path.parent_path().filename().string()));
            }
            if (game_id == 0 && path.parent_path().filename() == "mif" &&
                is_decimal_string(path.parent_path().parent_path().filename().string())) {
                game_id = static_cast<uint32_t>(std::stoul(path.parent_path().parent_path().filename().string()));
            }
            auto it = kOfficialGameNames.find(game_id);
            if (it != kOfficialGameNames.end()) {
                e.app_id = game_id;
                e.name = it->second;
                e.official_name = true;
            }
        } catch (...) {}
    }

    e.images = extract_images_from_mif(data);
    if (!e.images.empty()) {
        e.image_data = e.images.front(); // primary image (backward compatible)
    }

    e.text_strings = collect_mif_text(strings);
    for (const auto& s : e.text_strings) {
        // Require a dotted form (e.g. 1.0, 0.85, 1.0.696) so bare numbers like
        // build counters are not mistaken for a version.
        if (looks_like_version_string(s) && s.find('.') != std::string::npos) {
            e.version_text = s;
            break;
        }
    }

    return e;
}

static std::string find_mod_for_mif(const std::string& mif_path) {
    namespace fs = std::filesystem;
    fs::path mif = fs::path(mif_path);
    fs::path stem = mif.stem();
    fs::path dir = mif.parent_path();
    fs::path root = (dir.filename() == "mif") ? dir.parent_path() : dir;

    auto add_file = [](std::vector<fs::path>& candidates, const fs::path& path) {
        if (fs::is_regular_file(path) && is_mod_extension(path)) {
            candidates.push_back(path);
        }
    };
    auto add_mods_in_dir = [](std::vector<fs::path>& candidates, const fs::path& path) {
        if (!fs::is_directory(path)) return;
        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.is_regular_file() && is_mod_extension(entry.path())) {
                candidates.push_back(entry.path());
            }
        }
    };
    auto add_mods_recursive = [](std::vector<fs::path>& candidates, const fs::path& path) {
        if (!fs::is_directory(path)) return;
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (entry.is_regular_file() && is_mod_extension(entry.path())) {
                candidates.push_back(entry.path());
            }
        }
    };
    auto select_unique = [](std::vector<fs::path>& candidates) -> std::string {
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
        return candidates.size() == 1 ? candidates.front().string() : "";
    };

    std::vector<fs::path> candidates;
    add_file(candidates, root / (stem.string() + ".mod"));
    if (std::string selected = select_unique(candidates); !selected.empty()) return selected;

    candidates.clear();
    add_mods_in_dir(candidates, root / "mod" / stem);
    if (std::string selected = select_unique(candidates); !selected.empty()) return selected;

    candidates.clear();
    add_mods_in_dir(candidates, root / stem);
    if (std::string selected = select_unique(candidates); !selected.empty()) return selected;

    candidates.clear();
    add_mods_in_dir(candidates, root / "mod");
    if (std::string selected = select_unique(candidates); !selected.empty()) return selected;

    candidates.clear();
    add_mods_recursive(candidates, root / "mod");
    if (std::string selected = select_unique(candidates); !selected.empty()) return selected;

    candidates.clear();
    add_mods_recursive(candidates, root / stem);
    if (std::string selected = select_unique(candidates); !selected.empty()) return selected;

    return "";
}

static std::string package_root_for_mif(const std::string& mif_path) {
    namespace fs = std::filesystem;
    fs::path mif = fs::path(mif_path);
    fs::path dir = mif.parent_path();
    fs::path root = (dir.filename() == "mif") ? dir.parent_path() : dir;
    return root.string();
}

uint32_t resolve_clsid_for_mod(const std::string& mod_path) {
    namespace fs = std::filesystem;
    fs::path mod(mod_path);
    if (mod.empty()) return 0;

    auto try_mif = [](const fs::path& mif) -> uint32_t {
        std::error_code ec;
        if (!fs::is_regular_file(mif, ec) || !is_mif_extension(mif)) {
            return 0;
        }
        AppEntry app = parse_mif(mif.string());
        return app.clsid;
    };

    const fs::path mod_dir = mod.parent_path();
    const fs::path package_id = mod_dir.filename();
    const fs::path package_parent = mod_dir.parent_path();
    const fs::path mod_stem = mod.stem();
    if (uint32_t clsid = try_mif(package_parent / (package_id.string() + ".mif"))) {
        return clsid;
    }
    if (uint32_t clsid = try_mif(package_parent / "mif" / (package_id.string() + ".mif"))) {
        return clsid;
    }
    if (package_parent.filename() == "mod") {
        const fs::path package_root = package_parent.parent_path();
        if (uint32_t clsid = try_mif(package_root / "mif" / (package_id.string() + ".mif"))) {
            return clsid;
        }
        if (uint32_t clsid = try_mif(package_root / "mif" / (mod_stem.string() + ".mif"))) {
            return clsid;
        }
    }
    if (uint32_t clsid = try_mif(mod_dir / (mod_stem.string() + ".mif"))) {
        return clsid;
    }

    // Walk up from the mod's directory toward the package root, looking for the
    // accompanying MIF (a sibling `mif/` folder or a `.mif` in the directory).
    // The package layout is `<root>/mif/<id>.mif` next to `<root>/mod/<id>/*.mod`.
    for (fs::path dir = mod.parent_path(); ; dir = dir.parent_path()) {
        std::vector<fs::path> mif_candidates;
        fs::path mif_dir = dir / "mif";
        std::error_code ec;
        if (fs::is_directory(mif_dir, ec)) {
            for (const auto& e : fs::directory_iterator(mif_dir, ec)) {
                if (e.is_regular_file() && is_mif_extension(e.path()))
                    mif_candidates.push_back(e.path());
            }
        }
        if (fs::is_directory(dir, ec)) {
            for (const auto& e : fs::directory_iterator(dir, ec)) {
                if (e.is_regular_file() && is_mif_extension(e.path()))
                    mif_candidates.push_back(e.path());
            }
        }
        const std::string expected_package_mif = lower_ascii(package_id.string() + ".mif");
        const std::string expected_mod_mif = lower_ascii(mod_stem.string() + ".mif");
        std::sort(mif_candidates.begin(), mif_candidates.end(),
                  [&](const fs::path& a, const fs::path& b) {
                      const std::string an = lower_ascii(a.filename().string());
                      const std::string bn = lower_ascii(b.filename().string());
                      const int as = an == expected_package_mif ? 0 : (an == expected_mod_mif ? 1 : 2);
                      const int bs = bn == expected_package_mif ? 0 : (bn == expected_mod_mif ? 1 : 2);
                      return as != bs ? as < bs : an < bn;
                  });
        for (const auto& mif : mif_candidates) {
            AppEntry app = parse_mif(mif.string());
            if (app.clsid != 0) return app.clsid;
        }
        if (dir.parent_path() == dir || dir.empty()) break; // reached filesystem root
    }
    return 0;
}

std::vector<AppEntry> scan_apps(const std::string& base) {
    namespace fs = std::filesystem;
    std::vector<AppEntry> apps;
    if (!fs::exists(base)) return apps;
    for (const auto& entry : fs::recursive_directory_iterator(base)) {
        if (entry.is_regular_file() && is_mif_extension(entry.path())) {
            AppEntry app = parse_mif(entry.path().string());
            if (app.clsid != 0) {
                app.package_root = package_root_for_mif(app.mif_path);
                app.mod_path = find_mod_for_mif(app.mif_path);
                app.mod_name = app.mod_path.empty() ? "" : fs::path(app.mod_path).filename().string();
                if (!app.mod_path.empty()) {
                    if (app.images.empty()) {
                        app.images = extract_images_from_package_files(app.package_root);
                        if (!app.images.empty()) {
                            app.image_data = app.images.front();
                        }
                    }
                    apps.push_back(app);
                }
            }
        }
    }
    std::sort(apps.begin(), apps.end(), [](const AppEntry& a, const AppEntry& b) {
        return std::tie(a.mod_path, a.clsid, a.app_id) < std::tie(b.mod_path, b.clsid, b.app_id);
    });
    apps.erase(std::unique(apps.begin(), apps.end(), [](const AppEntry& a, const AppEntry& b) {
        return a.mod_path == b.mod_path && a.clsid == b.clsid && a.app_id == b.app_id;
    }), apps.end());
    std::sort(apps.begin(), apps.end(), [](const AppEntry& a, const AppEntry& b) {
        if (a.name != b.name) return a.name < b.name;
        return a.mif_path < b.mif_path;
    });
    return apps;
}

// ---------------------------------------------------------------------------

// Classify a library entry for the list badge. Homebrew lives under roms/hb;
// titles resolved from the official Zeebo catalog are Commercial; the rest are
// plain Brew apps (system apps, samples, unknown packages).
static AppEntry::Category classify_app(const AppEntry& app) {
    std::string p = lower_ascii(app.mif_path + " " + app.mod_path);
    if (p.find("/hb/") != std::string::npos || p.find("\\hb\\") != std::string::npos) {
        return AppEntry::Category::Homebrew;
    }
    if (app.official_name) {
        return AppEntry::Category::Commercial;
    }
    return AppEntry::Category::Brew;
}

#if ZEEMU_HAS_SQLITE && ZEEMU_HAS_STB_IMAGE
// Load full box art + description for each app from the Z-Wheel catalog SQLite
// (tools/misc/zwheel_catalog.py output), matched by CLSID. Blobs are zlib-
// compressed JPEG; decode through stb and upload as an SDL texture. Missing DB
// or missing rows are fine - those apps simply keep metadata-only details.
static void load_catalog_boxart(std::vector<AppEntry>& apps, SDL_Renderer* renderer,
                                const std::string& db_path) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT boxart_jpg, desc_pt, desc_en FROM games WHERE clsid = ?1",
            -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return;
    }
    for (auto& app : apps) {
        char clsid_hex[16];
        std::snprintf(clsid_hex, sizeof(clsid_hex), "%08X", app.clsid);
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, clsid_hex, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_ROW) continue;

        const void* zblob = sqlite3_column_blob(stmt, 0);
        const int zlen = sqlite3_column_bytes(stmt, 0);
        const unsigned char* pt = sqlite3_column_text(stmt, 1);
        const unsigned char* en = sqlite3_column_text(stmt, 2);
        if (pt && *pt) app.description_pt = reinterpret_cast<const char*>(pt);
        if (en && *en) app.description_en = reinterpret_cast<const char*>(en);

        if (zblob && zlen > 0) {
            int jpg_len = 0;
            char* jpg = stbi_zlib_decode_malloc(static_cast<const char*>(zblob), zlen, &jpg_len);
            if (jpg && jpg_len > 0) {
                int w, h, ch;
                uint8_t* px = stbi_load_from_memory(reinterpret_cast<uint8_t*>(jpg), jpg_len, &w, &h, &ch, 4);
                if (px) {
                    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                                          SDL_TEXTUREACCESS_STATIC, w, h);
                    if (tex) {
                        SDL_UpdateTexture(tex, nullptr, px, w * 4);
                        app.boxart = tex;
                        app.boxart_w = (float)w;
                        app.boxart_h = (float)h;
                    }
                    stbi_image_free(px);
                }
            }
            if (jpg) free(jpg);
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}
#endif

int run_gui() {
    apply_sdl_gamepad_env_hints();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::cerr << "Failed to init SDL: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Zeemu Launcher", 1024, 720, 0);
    if (!window) return 1;

    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    const char* ui_font_path = "font/IBMPlexSansJP-Regular.ttf";
    if (std::filesystem::exists(ui_font_path)) {
        ImFontConfig font_config;
        font_config.OversampleH = 2;
        font_config.OversampleV = 2;
        ImFont* font = io.Fonts->AddFontFromFileTTF(ui_font_path, 20.0f, &font_config, io.Fonts->GetGlyphRangesJapanese());
        if (font) {
            io.FontDefault = font;
        }
    }

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    bool done = false;
    std::string selected_mif;
    std::string selected_mod_path;
    int selected_index = -1;
    uint32_t selected_clsid = 0;
    bool opt_fast = false;
    bool opt_log  = false;
    int opt_keyboard_profile = 0;
    int opt_gamepad_profile = 0;
    std::array<bool, kDebugTraceOptionCount> debug_trace_enabled{};
    char trace_cpu_pc[64] = {};
    char trace_guest_progress_step[32] = "20000000";
    char trace_memwatch[64] = {};
    char filter_buf[128] = {};

    // Scan once at startup
    std::vector<AppEntry> apps = scan_apps("roms");
    if (!apps.empty()) {
        selected_index = 0;
        selected_mif = apps[0].mif_path;
        selected_mod_path = apps[0].mod_path;
        selected_clsid = apps[0].clsid;
    }

    // Upload every MIF image to the GPU. icons[] holds all of them; icon/icon_w/
    // icon_h mirror the primary (first) image for the list view and previews.
#if ZEEMU_HAS_STB_IMAGE
    for (auto& app : apps) {
        for (const auto& blob : app.images) {
            if (blob.empty()) continue;
            int w, h, ch;
            uint8_t* px = stbi_load_from_memory(blob.data(), (int)blob.size(), &w, &h, &ch, 4);
            if (!px) continue;
            SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                                  SDL_TEXTUREACCESS_STATIC, w, h);
            if (tex) {
                SDL_UpdateTexture(tex, nullptr, px, w * 4);
                app.icons.push_back({tex, (float)w, (float)h});
            }
            stbi_image_free(px);
        }
        if (!app.icons.empty()) {
            // Prefer the second image as the primary (it's usually the title/
            // banner art); fall back to the first when there is no second.
            const auto& primary = app.icons.size() >= 2 ? app.icons[1] : app.icons[0];
            app.icon   = primary.tex;
            app.icon_w = primary.w;
            app.icon_h = primary.h;
        }
        app.image_data.clear();
        app.images.clear();
    }
#endif

    // Tag each entry's category for the list badge.
    for (auto& app : apps) {
        app.category = classify_app(app);
    }

    // Enrich commercial titles with full box art + description from the catalog
    // SQLite if it is present next to the executable / repo root. Optional.
#if ZEEMU_HAS_SQLITE && ZEEMU_HAS_STB_IMAGE
    for (const char* db_path : {"catalog.sqlite", "logs/catalog.sqlite"}) {
        if (std::filesystem::exists(db_path)) {
            load_catalog_boxart(apps, renderer, db_path);
            break;
        }
    }
#endif

    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) done = true;
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        float main_menu_h = 0.0f;
        if (ImGui::BeginMainMenuBar()) {
            main_menu_h = ImGui::GetFrameHeight();
            draw_debug_trace_menu(debug_trace_enabled,
                                  trace_cpu_pc,
                                  sizeof(trace_cpu_pc),
                                  trace_guest_progress_step,
                                  sizeof(trace_guest_progress_step),
                                  trace_memwatch,
                                  sizeof(trace_memwatch));
            ImGui::EndMainMenuBar();
        }

        ImGui::SetNextWindowPos(ImVec2(0, main_menu_h));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, std::max(0.0f, io.DisplaySize.y - main_menu_h)));
        ImGui::Begin("Launcher", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.060f, 0.070f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.080f, 0.095f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.23f, 0.27f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.16f, 0.32f, 0.36f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.40f, 0.42f, 1.0f));

        ImGui::TextUnformatted("Zeemu");
        ImGui::SameLine();
        ImGui::TextDisabled("Zeebo Emulator Library");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 150.0f);
        ImGui::TextDisabled("%zu launchable apps", apps.size());
        ImGui::Separator();

        ImGui::InputTextWithHint("##filter", "Filter by name, id, clsid, or path", filter_buf, sizeof(filter_buf));
        std::string filter = lower_ascii(filter_buf);

        const float detail_w = std::max(330.0f, ImGui::GetContentRegionAvail().x * 0.38f);
        const float list_w = ImGui::GetContentRegionAvail().x - detail_w - ImGui::GetStyle().ItemSpacing.x;
        const float content_h = ImGui::GetContentRegionAvail().y;
        const float ICON_SIZE = 72.0f;
        int visible_apps = 0;

        ImGui::BeginChild("AppList", ImVec2(list_w, content_h), true);
        for (int idx = 0; idx < (int)apps.size(); idx++) {
            const auto& app = apps[idx];
            std::ostringstream haystack;
            haystack << app.name << ' ' << app.app_id << " 0x" << std::hex << app.clsid << ' '
                     << short_path(app.mif_path) << ' ' << short_path(app.mod_path);
            if (!filter.empty() && lower_ascii(haystack.str()).find(filter) == std::string::npos) {
                continue;
            }
            ++visible_apps;

            ImGui::PushID(idx);
            bool sel = (selected_index == idx);

            // Prefer the catalog box art as the list thumbnail; fall back to the
            // (uglier) MIF icon when a title has no box art.
            SDL_Texture* thumb = app.boxart ? app.boxart : app.icon;
            float thumb_w = app.boxart ? app.boxart_w : app.icon_w;
            float thumb_h = app.boxart ? app.boxart_h : app.icon_h;
            if (thumb) {
                float scale = ICON_SIZE / std::max(thumb_w, thumb_h);
                float dw = thumb_w * scale, dh = thumb_h * scale;
                ImVec2 cursor = ImGui::GetCursorPos();
                ImGui::SetCursorPos(ImVec2(cursor.x + (ICON_SIZE - dw) * 0.5f, cursor.y + (ICON_SIZE - dh) * 0.5f));
                ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(thumb)), ImVec2(dw, dh));
                ImGui::SetCursorPos(ImVec2(cursor.x + ICON_SIZE + 10, cursor.y));
                ImGui::SameLine(0, 0);
            }

            const char* cat_tag = "BREW";
            switch (app.category) {
                case AppEntry::Category::Commercial: cat_tag = "COMMERCIAL"; break;
                case AppEntry::Category::Homebrew:   cat_tag = "HOMEBREW";   break;
                case AppEntry::Category::Brew:       cat_tag = "BREW";       break;
            }
            char label[320];
            snprintf(label, sizeof(label), "%s\n%06u  0x%08X  [%s]%s",
                     app.name.c_str(), app.app_id, app.clsid, cat_tag,
                     app.boxart ? "  *" : "");
            if (ImGui::Selectable(label, sel, 0, ImVec2(0, ICON_SIZE))) {
                selected_index = idx;
                selected_mif      = app.mif_path;
                selected_mod_path = app.mod_path;
                selected_clsid    = app.clsid;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", app.mif_path.c_str());
            ImGui::PopID();
        }
        if (visible_apps == 0) {
            ImGui::TextDisabled("No apps match the current filter.");
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("Details", ImVec2(0, content_h), true);

        if (selected_index >= 0 && selected_index < (int)apps.size()) {
            const AppEntry& selected = apps[selected_index];
            ImGui::TextWrapped("%s", selected.name.c_str());
            // Coloured category badge.
            {
                ImVec4 col; const char* tag;
                switch (selected.category) {
                    case AppEntry::Category::Commercial: col = ImVec4(0.30f,0.70f,0.40f,1); tag = "COMMERCIAL"; break;
                    case AppEntry::Category::Homebrew:   col = ImVec4(0.85f,0.55f,0.20f,1); tag = "HOMEBREW";   break;
                    default:                             col = ImVec4(0.45f,0.55f,0.75f,1); tag = "BREW";        break;
                }
                ImGui::TextColored(col, "[%s]", tag);
            }
            ImGui::Spacing();
            // Prefer full box art (commercial titles from the catalog); fall back
            // to the MIF icon. Box art is shown large on the left of the metadata.
            if (selected.boxart) {
                float scale = 200.0f / std::max(selected.boxart_w, selected.boxart_h);
                ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(selected.boxart)),
                             ImVec2(selected.boxart_w * scale, selected.boxart_h * scale));
                ImGui::SameLine();
            } else if (selected.icon) {
                float scale = 96.0f / std::max(selected.icon_w, selected.icon_h);
                ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(selected.icon)), ImVec2(selected.icon_w * scale, selected.icon_h * scale));
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            ImGui::Text("Game ID: %06u", selected.app_id);
            ImGui::Text("CLSID:   0x%08X", selected_clsid);
            ImGui::Text("MIF ID:  0x%08X", selected.module_id);
            ImGui::Text("MIF:     v0x%08X", selected.mif_version);
            if (!selected.version_text.empty())
                ImGui::Text("Version: %s", selected.version_text.c_str());
            ImGui::Text("Priv:    0x%04X", selected.priv_level);
            ImGui::Text("Classes: %u applets, %u classes, %u ext",
                        selected.applet_count, selected.class_count, selected.ext_class_count);
            ImGui::Text("Images:  %zu", selected.icons.size());
            ImGui::EndGroup();

            // Catalog description (commercial titles).
            if (!selected.description_en.empty()) {
                ImGui::Spacing();
                ImGui::SeparatorText("Description");
                ImGui::TextWrapped("%s", selected.description_en.c_str());
            }

            // Thumbnail strip of every image embedded in the MIF.
            // if (selected.icons.size() > 1) {
            //     ImGui::Spacing();
            //     ImGui::TextDisabled("Images");
            //     float avail = ImGui::GetContentRegionAvail().x;
            //     float x = 0.0f;
            //     const float kThumb = 72.0f;
            //     for (size_t k = 0; k < selected.icons.size(); ++k) {
            //         const auto& ic = selected.icons[k];
            //         if (!ic.tex) continue;
            //         float scale = kThumb / std::max(ic.w, ic.h);
            //         float dw = ic.w * scale, dh = ic.h * scale;
            //         if (x > 0.0f && x + dw > avail) { x = 0.0f; } else if (x > 0.0f) { ImGui::SameLine(); }
            //         ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(ic.tex)), ImVec2(dw, dh));
            //         if (ImGui::IsItemHovered())
            //             ImGui::SetTooltip("%.0f x %.0f", ic.w, ic.h);
            //         x += dw + ImGui::GetStyle().ItemSpacing.x;
            //     }
            // }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Package");
            ImGui::TextDisabled("root: %s", short_path(selected.package_root).c_str());
            ImGui::TextDisabled(".mif: %s", short_path(selected_mif).c_str());
            if (!selected_mod_path.empty())
                ImGui::TextDisabled(".mod: %s", short_path(selected_mod_path).c_str());
            else
                ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "No .mod found for this app.");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Launch");
            // CLSID is resolved from the MIF (shown read-only above); no manual
            // override needed.

            ImGui::TextDisabled("Controller");
            const int profile_idx = std::clamp(opt_keyboard_profile, 0, static_cast<int>(kKeyboardProfileOptionCount) - 1);
            const KeyboardProfileOption& profile = kKeyboardProfileOptions[profile_idx];
            ImGui::SetNextItemWidth(std::min(260.0f, ImGui::GetContentRegionAvail().x));
            if (ImGui::BeginCombo("Keyboard Profile", profile.label)) {
                for (int i = 0; i < static_cast<int>(kKeyboardProfileOptionCount); ++i) {
                    const bool selected = opt_keyboard_profile == i;
                    if (ImGui::Selectable(kKeyboardProfileOptions[i].label, selected)) {
                        opt_keyboard_profile = i;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::TextWrapped("%s", profile.description);

            const int gamepad_profile_idx = std::clamp(opt_gamepad_profile, 0, static_cast<int>(kGamepadProfileOptionCount) - 1);
            const GamepadProfileOption& gamepad_profile_option = kGamepadProfileOptions[gamepad_profile_idx];
            ImGui::SetNextItemWidth(std::min(260.0f, ImGui::GetContentRegionAvail().x));
            if (ImGui::BeginCombo("Gamepad Mapping", gamepad_profile_option.label)) {
                for (int i = 0; i < static_cast<int>(kGamepadProfileOptionCount); ++i) {
                    const bool selected = opt_gamepad_profile == i;
                    if (ImGui::Selectable(kGamepadProfileOptions[i].label, selected)) {
                        opt_gamepad_profile = i;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::TextWrapped("%s", gamepad_profile_option.description);
            ImGui::Spacing();

            ImGui::Checkbox("Fast (suppress dbgprintf)", &opt_fast);
            ImGui::SameLine();
            ImGui::Checkbox("Log to file", &opt_log);
            ImGui::TextDisabled("%s %s 0x%08X",
                                opt_fast ? "run-app-fast" : "run-app",
                                short_path(selected_mod_path).c_str(),
                                selected_clsid);

            bool can_run = !selected_mod_path.empty() && selected_clsid != 0;
            if (!can_run) ImGui::BeginDisabled();
            float btn_w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            auto do_launch = [&](bool fast) {
                if (fast) _putenv_s("ZEEMU_QUIET", "1");
                else      _putenv_s("ZEEMU_QUIET", "");
                apply_debug_trace_env(debug_trace_enabled,
                                      trace_cpu_pc,
                                      trace_guest_progress_step,
                                      trace_memwatch);
                const int active_profile_idx = std::clamp(opt_keyboard_profile, 0, static_cast<int>(kKeyboardProfileOptionCount) - 1);
                const char* keyboard_profile = kKeyboardProfileOptions[active_profile_idx].env_value;
                set_process_env("ZEEMU_KEYBOARD_PROFILE", keyboard_profile);
                const int active_gamepad_profile_idx = std::clamp(opt_gamepad_profile, 0, static_cast<int>(kGamepadProfileOptionCount) - 1);
                const char* gamepad_profile_env = kGamepadProfileOptions[active_gamepad_profile_idx].env_value;
                set_process_env("ZEEMU_GAMEPAD_PROFILE", gamepad_profile_env);
                if (opt_log) {
                    namespace fs = std::filesystem;
                    std::string stem = fs::path(selected_mod_path).stem().string();
                    std::string logpath = stem + "_run.log";
                    freopen(logpath.c_str(), "w", stdout);
                    freopen(logpath.c_str(), "a", stderr);
                    write_launch_log_header(selected_mod_path,
                                            selected_clsid,
                                            fast,
                                            keyboard_profile,
                                            gamepad_profile_env,
                                            debug_trace_enabled,
                                            trace_cpu_pc,
                                            trace_guest_progress_step,
                                            trace_memwatch);
                }
                ImGui_ImplSDLRenderer3_Shutdown();
                ImGui_ImplSDL3_Shutdown();
                ImGui::DestroyContext();
                SDL_DestroyRenderer(renderer);
                SDL_DestroyWindow(window);
                SDL_Quit();
            };
            if (ImGui::Button("Run", ImVec2(btn_w, 0))) {
                do_launch(opt_fast);
                return run_emulator(selected_mod_path, selected_clsid);
            }
            ImGui::SameLine();
            if (ImGui::Button("Run Fast", ImVec2(btn_w, 0))) {
                do_launch(true);
                return run_emulator(selected_mod_path, selected_clsid);
            }
            if (!can_run) ImGui::EndDisabled();
        } else {
            ImGui::TextDisabled("Select an app above.");
        }
        ImGui::EndChild();

        ImGui::PopStyleColor(5);
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    for (auto& app : apps) {
        for (auto& ic : app.icons) {
            if (ic.tex) SDL_DestroyTexture(ic.tex);
        }
        if (app.boxart) SDL_DestroyTexture(app.boxart);
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
