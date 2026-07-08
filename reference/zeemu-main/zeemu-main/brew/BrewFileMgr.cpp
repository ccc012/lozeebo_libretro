#include "brew/BrewFileMgr.h"
#include "brew/GgzArchive.h"
#include "brew/BrewQXGL.h"
#include "cpu/core/CPU.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <unordered_map>

namespace {

std::unordered_map<addr_t, std::deque<std::vector<uint8_t>>> g_low_pointer_read_shadow;
std::unordered_map<addr_t, size_t> g_low_pointer_read_shadow_index;

std::string filename_only(std::string path) {
    const auto slash = path.find_last_of("/\\");
    if (slash != std::string::npos) {
        path = path.substr(slash + 1);
    }
    for (auto& c : path) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return path;
}

bool is_tga_path(const std::string& path) {
    const std::string name = filename_only(path);
    return name.size() >= 4 && name.substr(name.size() - 4) == ".tga";
}

uint16_t read_le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

bool is_builtin_fontsize_map(const std::string& path) {
    return filename_only(path) == "fontsize.map";
}

bool has_extension_ci(const std::string& path, const char* extension) {
    const std::string name = filename_only(path);
    const std::string ext(extension);
    return name.size() >= ext.size() && name.substr(name.size() - ext.size()) == ext;
}

std::string lower_ascii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool wildcard_match_ci(const std::string& pattern_raw, const std::string& text_raw) {
    const std::string pattern = lower_ascii(pattern_raw);
    const std::string text = lower_ascii(text_raw);
    size_t p = 0;
    size_t t = 0;
    size_t star = std::string::npos;
    size_t star_text = 0;

    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            star_text = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++star_text;
        } else {
            return false;
        }
    }

    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

bool has_wildcard(const std::string& path) {
    return path.find_first_of("*?") != std::string::npos;
}

std::string slashes_for_filemgr(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

std::string replace_extension(std::string path, const char* extension) {
    const auto slash = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return path + extension;
    }
    path.replace(dot, std::string::npos, extension);
    return path;
}

bool trace_file_io() {
    static const bool enabled =
        std::getenv("ZEEMU_TRACE_FILE_IO") != nullptr ||
        std::getenv("ZEEMU_TRACE_FILE_READS") != nullptr;
    return enabled;
}

FILE* open_builtin_fontsize_map() {
    // Z-Wheel probes fontsize.map before building its AppUI splash path. The
    // retail package currently does not carry the file in the observed mod
    // folder, so provide a tiny read-only default map as a virtual BREW file
    // instead of modifying ROM contents on disk.
    static constexpr const char* kContent =
        "small=12\n"
        "medium=16\n"
        "large=20\n"
        "default=16\n";
    FILE* fp = tmpfile();
    if (!fp) {
        return nullptr;
    }
    fputs(kContent, fp);
    rewind(fp);
    return fp;
}

[[maybe_unused]] bool is_builtin_preloaded_cfg(const std::string& path) {
    return filename_only(path) == "preloaded.cfg";
}

[[maybe_unused]] FILE* open_builtin_preloaded_cfg() {
    // The Z-Wheel launcher opens preloaded.cfg (read-only) for its downloaded
    // game/channel cache before building the stage. That cache is normally
    // generated/provisioned from the Zeebo network at install time and is not in
    // the retail package; the package only ships an EMPTY preload.cfg, which
    // shows that an empty cache is the valid first-boot state ("nothing
    // preloaded"). Provide an empty virtual file so the open succeeds and the
    // launcher proceeds past the splash to build its stage from the fixed forms
    // (Shop/Game/Setup/Zeebo/Terms) instead of stalling on the missing cache.
    // This is the no-content case, not a synthesized cache format.
    return tmpfile();
}

} // namespace

const std::vector<uint8_t>* brew_take_low_pointer_shadow(addr_t ptr, uint32_t min_size) {
    auto it = g_low_pointer_read_shadow.find(ptr);
    if (it == g_low_pointer_read_shadow.end()) {
        return nullptr;
    }
    size_t& index = g_low_pointer_read_shadow_index[ptr];
    while (index < it->second.size() && it->second[index].size() < min_size) {
        ++index;
    }
    if (index >= it->second.size()) {
        return nullptr;
    }
    return &it->second[index++];
}

const std::vector<uint8_t>* brew_take_low_pointer_shadow_with_prefix(addr_t ptr, const uint8_t* prefix, size_t prefix_size) {
    auto it = g_low_pointer_read_shadow.find(ptr);
    if (it == g_low_pointer_read_shadow.end() || prefix == nullptr || prefix_size == 0) {
        return nullptr;
    }
    size_t& index = g_low_pointer_read_shadow_index[ptr];
    while (index < it->second.size()) {
        const auto& candidate = it->second[index];
        if (candidate.size() >= prefix_size && std::equal(prefix, prefix + prefix_size, candidate.begin())) {
            return &it->second[index++];
        }
        ++index;
    }
    return nullptr;
}

void brew_store_low_pointer_shadow(addr_t ptr, const uint8_t* data, size_t size) {
    if (ptr == 0 || ptr >= 0x10000 || data == nullptr || size == 0) {
        return;
    }
    g_low_pointer_read_shadow[ptr].emplace_back(data, data + size);
}

// AEEFileInfo offsets (ARM, packed with natural alignment):
//   char   attrib;          +0  (1 byte + 3 pad)
//   uint32 dwCreationDate;  +4
//   uint32 dwSize;          +8
//   char   szName[64];      +12
static constexpr uint32_t FILEINFO_ATTRIB = 0;
static constexpr uint32_t FILEINFO_DATE   = 4;
static constexpr uint32_t FILEINFO_SIZE   = 8;
static constexpr uint32_t FILEINFO_NAME   = 12;
static constexpr uint32_t FILEINFO_TOTAL  = 76;

// AEEOpenFileMode flags
static constexpr uint32_t OFM_READ      = 0x0001;
static constexpr uint32_t OFM_READWRITE = 0x0002;
static constexpr uint32_t OFM_CREATE    = 0x0004;
static constexpr uint32_t OFM_APPEND    = 0x0008;
static constexpr uint32_t OFM_NO_BUFFER = 0x8000;

// AEEError.h file errors.
static constexpr uint32_t EFILEEXISTS   = 0x100;
static constexpr uint32_t EFILENOEXISTS = 0x101;

// AEEFileSeekType. SDK AEEFile.h orders these as START, END, CURRENT.
static constexpr uint32_t SEEK_START   = 0;
static constexpr uint32_t SEEK_END_T   = 1;
static constexpr uint32_t SEEK_CUR_T   = 2;

// ---- BrewFileMgr ----

BrewFileMgr::BrewFileMgr(BrewShell& shell, EndianMemory& memory, VirtualFileSystem& vfs)
    : shell_(shell), memory_(memory), vfs_(vfs)
{
    setup_vtable();
}

BrewFileMgr::~BrewFileMgr() {
    for (auto* f : open_files_) delete f;
}

BrewFile* BrewFileMgr::find_open_file(addr_t object_ptr) const {
    for (auto* file : open_files_) {
        if (file && file->get_object_ptr() == object_ptr) {
            return file;
        }
    }
    return nullptr;
}

std::string BrewFileMgr::read_guest_string(addr_t addr) const {
    return shell_.read_guest_text(addr, 512);
}

void BrewFileMgr::setup_vtable() {
    // INHERIT_IFileMgr uses INHERIT_IBase (AddRef[0], Release[1], no QI):
    // [2] OpenFile  [3] GetInfo  [4] Remove  [5] MkDir  [6] RmDir
    // [7] Test  [8] GetFreeSpace  [9] GetLastError  [10] EnumInit  [11] EnumNext
    // [12] Rename  [13] EnumNextEx  [14] SetDescription  [15] GetInfoEx
    // [16] Use  [17] GetFileUseInfo  [18] ResolvePath  [19] CheckPathAccess  [20] GetFreeSpaceEx
    vtable_ptr_ = shell_.malloc(24 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);

    auto add = [&](int idx, const std::string& n) {
        addr_t h = shell_.add_hook("IFileMgr_" + n, this);
        memory_.write_value(vtable_ptr_ + static_cast<uint32_t>(idx * 4), h);
    };

    add(0,  "AddRef");
    add(1,  "Release");
    add(2,  "OpenFile");
    add(3,  "GetInfo");
    add(4,  "Remove");
    add(5,  "MkDir");
    add(6,  "RmDir");
    add(7,  "Test");
    add(8,  "GetFreeSpace");
    add(9,  "GetLastError");
    add(10, "EnumInit");
    add(11, "EnumNext");
    add(12, "Rename");
    add(13, "EnumNextEx");
    add(14, "SetDescription");
    add(15, "GetInfoEx");
    add(16, "Use");
    add(17, "GetFileUseInfo");
    add(18, "ResolvePath");
    add(19, "CheckPathAccess");
    add(20, "GetFreeSpaceEx");
    for (int idx = 21; idx < 24; ++idx) {
        add(idx, "Fn" + std::to_string(idx));
    }
}

void BrewFileMgr::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r5 = cpu.get_reg(REG_R5);
    uint32_t r6 = cpu.get_reg(REG_R6);
    uint32_t r7 = cpu.get_reg(REG_R7);

    bool is_thunk = (r1 >= 0xFF000000);
    addr_t arg1 = is_thunk ? r5 : r1;
    uint32_t arg2 = is_thunk ? r6 : cpu.get_reg(REG_R2);

    if (name == "IFileMgr_AddRef") {
        if (trace_file_io()) {
            printf("IFileMgr_AddRef\n");
        }
        cpu.set_reg(REG_R0, 1); return;
    }
    if (name == "IFileMgr_Release") {
        if (trace_file_io()) {
            printf("IFileMgr_Release\n");
        }
        cpu.set_reg(REG_R0, 0); return;
    }

    if (name == "IFileMgr_GetLastError") {
        if (trace_file_io()) {
            printf("  IFileMgr_GetLastError\n");
        }
        cpu.set_reg(REG_R0, static_cast<uint32_t>(last_error_));
        return;
    }

    if (name == "IFileMgr_GetFreeSpace") {
        if (arg1 && arg1 < 0xFF000000)
            memory_.write_value(arg1, (uint32_t)(64u * 1024u * 1024u));
        cpu.set_reg(REG_R0, 64u * 1024u * 1024u);
        return;
    }
    if (name == "IFileMgr_GetFreeSpaceEx") {
        addr_t pTotal = is_thunk ? r6 : cpu.get_reg(REG_R2);
        addr_t pFree = is_thunk ? r7 : cpu.get_reg(REG_R3);
        if (pTotal && pTotal < 0xFF000000) memory_.write_value(pTotal, 64u * 1024u * 1024u);
        if (pFree && pFree < 0xFF000000) memory_.write_value(pFree, 64u * 1024u * 1024u);
        cpu.set_reg(REG_R0, 0);
        return;
    }

    if (name == "IFileMgr_OpenFile") {
        std::string path = read_guest_string(arg1);
        uint32_t mode = arg2;
        uint32_t file_mode = mode & ~OFM_NO_BUFFER;
        if (trace_file_io()) {
            printf("IFileMgr_OpenFile: '%s' mode=0x%x\n", path.c_str(), mode);
        }

        // Resolve path using app's current directory
        VirtualFileSystem::ResolveResult result = vfs_.resolve(path, shell_.get_current_directory());
        std::string host;
        if (result.resolved) {
            host = result.host_path.string();
        } else {
            // Fallback to absolute if resolve failed
            host = path;
        }

        const bool wants_create = (file_mode & OFM_CREATE) != 0;
        const bool wants_write = wants_create || (file_mode & OFM_APPEND) || (file_mode & OFM_READWRITE);
        if (wants_create && result.resolved && result.exists) {
            if (trace_file_io()) {
                printf("  CREATE target exists: %s\n", host.c_str());
            }
            cpu.set_reg(REG_R0, 0);
            last_error_ = EFILEEXISTS;
            return;
        }

        const char* fmode = "rb";
        if (wants_create) fmode = "w+b";
        else if (file_mode & OFM_APPEND) fmode = "a+b";
        else if (file_mode & OFM_READWRITE) fmode = "r+b";

        if (result.resolved && wants_write) {
            std::error_code ec;
            std::filesystem::create_directories(result.host_path.parent_path(), ec);
            if (ec && trace_file_io()) {
                printf("  Create parent failed: %s\n", ec.message().c_str());
            }
        }

        FILE* fp = fopen(host.c_str(), fmode);
        if (!fp && (file_mode & OFM_READ)) {
            // retry read-only with resolved host
            if (result.resolved) fp = fopen(result.host_path.string().c_str(), "rb");
        }

        if (!fp && (file_mode & OFM_READ) && has_extension_ci(path, ".pkg")) {
            const std::string zip_path = replace_extension(path, ".zip");
            auto zip = vfs_.resolve(zip_path, shell_.get_current_directory());
            if (zip.resolved && zip.exists) {
                fp = fopen(zip.host_path.string().c_str(), "rb");
                if (fp) {
                    host = zip.host_path.string();
                    if (trace_file_io()) {
                        printf("  Fallback .pkg -> .zip: %s\n", host.c_str());
                    }
                }
            }
        }

        // Fallback: try common subdirectories and case-insensitive variants
        if (!fp && (file_mode & OFM_READ)) {
            std::string filename = path;
            auto slash = filename.find_last_of("/\\");
            if (slash != std::string::npos) filename = filename.substr(slash + 1);
            if (!filename.empty()) {
                std::string lower_fn = filename;
                for (auto& c : lower_fn) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                std::string upper_fn = filename;
                for (auto& c : upper_fn) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                const char* subdirs[] = {"doom/", "data/", ""};
                for (const char* prefix : subdirs) {
                    if (fp) break;
                    // Try original case
                    std::string try_path = std::string(prefix) + filename;
                    auto fallback = vfs_.resolve(try_path, shell_.get_current_directory());
                    if (fallback.resolved && fallback.exists) {
                        fp = fopen(fallback.host_path.string().c_str(), "rb");
                        if (fp) { host = fallback.host_path.string(); break; }
                    }
                    // Try lowercase
                    if (lower_fn != filename) {
                        try_path = std::string(prefix) + lower_fn;
                        fallback = vfs_.resolve(try_path, shell_.get_current_directory());
                        if (fallback.resolved && fallback.exists) {
                            fp = fopen(fallback.host_path.string().c_str(), "rb");
                            if (fp) { host = fallback.host_path.string(); break; }
                        }
                    }
                    // Try uppercase
                    if (upper_fn != filename) {
                        try_path = std::string(prefix) + upper_fn;
                        fallback = vfs_.resolve(try_path, shell_.get_current_directory());
                        if (fallback.resolved && fallback.exists) {
                            fp = fopen(fallback.host_path.string().c_str(), "rb");
                            if (fp) { host = fallback.host_path.string(); break; }
                        }
                    }
                }
                if (fp && trace_file_io()) printf("  Fallback: %s\n", host.c_str());
            }
        }

        if (fp) {
            if (trace_file_io()) {
                printf("  Opened: %s\n", host.c_str());
            }
            const std::filesystem::path host_path(host);
            if (host_path.extension() == ".ggz") {
                GgzArchive::extract_to_cache(host_path);
            }
        } else if ((file_mode & OFM_READ) && is_builtin_fontsize_map(path)) {
            fp = open_builtin_fontsize_map();
            if (fp) {
                host = "builtin:/fontsize.map";
                if (trace_file_io()) {
                    printf("  Opened builtin: %s\n", host.c_str());
                }
            }
        }
        // NOTE: an empty virtual preloaded.cfg was tried here and correctly lets
        // the launcher pass the splash gate, but it then exposes an infinite loop
        // in the trace-shaped ZWheelConfig iterator (0x01028e35 Fn6/Fn9). Leaving
        // preloaded.cfg failing keeps the working splash; re-enable the virtual
        // file (is_builtin_preloaded_cfg / open_builtin_preloaded_cfg below) once
        // the iterator's loop-exit contract is reverse-engineered. See
        // status/targets-launcher-services.md.

        if (fp) {
            auto* f = new BrewFile(shell_, memory_, fp, host);
            open_files_.push_back(f);
            cpu.set_reg(REG_R0, f->get_object_ptr());
            last_error_ = 0;
        } else {
            if (trace_file_io()) {
                printf("  FAILED: %s\n", host.c_str());
            }
            cpu.set_reg(REG_R0, 0);
            last_error_ = EFILENOEXISTS;
        }
        return;
    }

    if (name == "IFileMgr_Test") {
        std::string path = read_guest_string(arg1);
        auto r = vfs_.resolve(path, shell_.get_current_directory());
        bool ok = r.resolved && r.exists;
        if (!ok && is_builtin_fontsize_map(path)) {
            ok = true;
        }
        if (!ok) {
            // Fallback: try doom/ subdirectory and case-insensitive variants
            std::string filename = path;
            auto slash = filename.find_last_of("/\\");
            if (slash != std::string::npos) filename = filename.substr(slash + 1);
            if (!filename.empty()) {
                // Try lowercase in doom/ subdirectory
                std::string lower_fn = filename;
                for (auto& c : lower_fn) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                const char* subdirs[] = {"doom/", ""};
                for (const char* prefix : subdirs) {
                    if (ok) break;
                    std::string try_path = std::string(prefix) + filename;
                    auto fb = vfs_.resolve(try_path, shell_.get_current_directory());
                    ok = fb.resolved && fb.exists;
                    if (!ok && lower_fn != filename) {
                        try_path = std::string(prefix) + lower_fn;
                        fb = vfs_.resolve(try_path, shell_.get_current_directory());
                        ok = fb.resolved && fb.exists;
                    }
                }
                // Try uppercase variant in doom/ subdirectory
                if (!ok) {
                    std::string upper_fn = filename;
                    for (auto& c : upper_fn) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    if (upper_fn != filename) {
                        auto fb = vfs_.resolve("doom/" + upper_fn, shell_.get_current_directory());
                        ok = fb.resolved && fb.exists;
                    }
                }
            }
        }
        if (trace_file_io()) {
            printf("IFileMgr_Test: '%s' -> %s\n", path.c_str(), ok ? "EXISTS" : "MISS");
        }
        cpu.set_reg(REG_R0, ok ? 0u : static_cast<uint32_t>(-1));
        return;
    }

    if (name == "IFileMgr_GetInfo") {
        std::string path = read_guest_string(arg1);
        addr_t pInfo = arg2;
        auto r = vfs_.resolve(path, shell_.get_current_directory());
        if (trace_file_io()) {
            printf("IFileMgr_GetInfo: '%s' pInfo=0x%x exists=%d\n", path.c_str(), pInfo, (int)r.exists);
        }
        if (r.resolved && r.exists && pInfo && pInfo < 0xFF000000) {
            memory_.write_value(pInfo + FILEINFO_ATTRIB, (uint8_t)0, EndianMemory::Byte);
            memory_.write_value(pInfo + FILEINFO_DATE, (uint32_t)0);
            memory_.write_value(pInfo + FILEINFO_SIZE, (uint32_t)r.size);
            std::string fname = r.host_path.filename().string();
            size_t len = std::min(fname.size(), (size_t)63);
            for (size_t i = 0; i < len; ++i)
                memory_.write_value(pInfo + FILEINFO_NAME + (uint32_t)i, (uint8_t)fname[i], EndianMemory::Byte);
            memory_.write_value(pInfo + FILEINFO_NAME + (uint32_t)len, (uint8_t)0, EndianMemory::Byte);
            cpu.set_reg(REG_R0, 0);
        } else {
            cpu.set_reg(REG_R0, (uint32_t)-1);
        }
        return;
    }
    if (name == "IFileMgr_GetInfoEx") {
        std::string path = read_guest_string(arg1);
        addr_t pEx = arg2;
        auto r = vfs_.resolve(path, shell_.get_current_directory());
        if (trace_file_io()) {
            printf("IFileMgr_GetInfoEx: '%s' pInfo=0x%x exists=%d\n", path.c_str(), pEx, (int)r.exists);
        }
        if (r.resolved && r.exists && pEx && pEx < 0xFF000000) {
            memory_.write_value(pEx + 4, (uint8_t)(std::filesystem::is_directory(r.host_path) ? 0x10 : 0), EndianMemory::Byte);
            memory_.write_value(pEx + 8, (uint32_t)0);
            memory_.write_value(pEx + 12, (uint32_t)r.size);
            uint32_t pszFile = memory_.read_value(pEx + 16);
            int32_t nMaxFile = static_cast<int32_t>(memory_.read_value(pEx + 20));
            if (pszFile && pszFile < 0xFF000000 && nMaxFile > 0) {
                std::string fname = r.host_path.filename().string();
                size_t len = std::min(fname.size(), static_cast<size_t>(nMaxFile - 1));
                for (size_t i = 0; i < len; ++i)
                    memory_.write_value(pszFile + static_cast<uint32_t>(i), (uint8_t)fname[i], EndianMemory::Byte);
                memory_.write_value(pszFile + static_cast<uint32_t>(len), 0, EndianMemory::Byte);
            }
            cpu.set_reg(REG_R0, 0);
        } else {
            cpu.set_reg(REG_R0, (uint32_t)-1);
        }
        return;
    }

    if (name == "IFileMgr_MkDir") {
        std::string path = read_guest_string(arg1);
        auto r = vfs_.resolve(path, shell_.get_current_directory());
        if (r.resolved) std::filesystem::create_directories(r.host_path);
        cpu.set_reg(REG_R0, 0);
        return;
    }
    if (name == "IFileMgr_RmDir") {
        std::string path = read_guest_string(arg1);
        auto r = vfs_.resolve(path, shell_.get_current_directory());
        std::error_code ec;
        bool ok = r.resolved && std::filesystem::remove(r.host_path, ec);
        cpu.set_reg(REG_R0, (ok && !ec) ? 0u : static_cast<uint32_t>(-1));
        return;
    }

    if (name == "IFileMgr_Remove") {
        std::string path = read_guest_string(arg1);
        auto r = vfs_.resolve(path, shell_.get_current_directory());
        if (trace_file_io()) {
            printf("IFileMgr_Remove: '%s' resolved=%d exists=%d\n", path.c_str(), static_cast<int>(r.resolved), (int)r.exists);
        }
        if (r.resolved && r.exists) {
            // Force-close any open handles to this file
            for (auto* f : open_files_) {
                f->close_if_matches(r.host_path.string());
            }
            try {
                std::error_code ec;
                std::filesystem::remove(r.host_path, ec);
                if (ec) {
                    if (trace_file_io()) {
                        printf("  Remove error: %s\n", ec.message().c_str());
                    }
                    cpu.set_reg(REG_R0, (uint32_t)-1);
                } else {
                    if (trace_file_io()) {
                        printf("  Removed OK\n");
                    }
                    cpu.set_reg(REG_R0, 0);
                }
            } catch (const std::exception& e) {
                if (trace_file_io()) {
                    printf("  Remove exception: %s\n", e.what());
                }
                cpu.set_reg(REG_R0, (uint32_t)-1);
            }
        }
        else cpu.set_reg(REG_R0, (uint32_t)-1);
        return;
    }

    if (name == "IFileMgr_EnumInit") {
        std::string path = read_guest_string(arg1);
        const bool dirs_only = arg2 != 0;
        const bool wildcard = has_wildcard(path);
        std::string enum_dir = path;
        std::string pattern;
        enum_name_prefix_.clear();
        if (wildcard) {
            const std::string fixed = slashes_for_filemgr(path);
            const size_t slash = fixed.find_last_of('/');
            if (slash == std::string::npos) {
                enum_dir.clear();
                pattern = fixed;
            } else {
                enum_dir = fixed.substr(0, slash);
                pattern = fixed.substr(slash + 1);
                const size_t raw_slash = path.find_last_of("\\/");
                if (raw_slash != std::string::npos) {
                    enum_name_prefix_ = path.substr(0, raw_slash + 1);
                }
            }
        }
        auto r = vfs_.resolve(enum_dir, shell_.get_current_directory());
        
        enum_entries_.clear();
        enum_idx_ = 0;
        
        if (r.resolved && r.exists && std::filesystem::is_directory(r.host_path)) {
            for (const auto& entry : std::filesystem::directory_iterator(r.host_path)) {
                const bool is_dir = entry.is_directory();
                if (dirs_only != is_dir) {
                    continue;
                }
                if (wildcard && !wildcard_match_ci(pattern, entry.path().filename().string())) {
                    continue;
                }
                enum_entries_.push_back(entry.path());
            }
            std::sort(enum_entries_.begin(), enum_entries_.end());
            if (trace_file_io()) {
                printf("IFileMgr_EnumInit: '%s' dir='%s' pattern='%s' dirs=%d -> %zu entries\n",
                       path.c_str(), enum_dir.c_str(), pattern.c_str(), dirs_only ? 1 : 0,
                       enum_entries_.size());
            }
            cpu.set_reg(REG_R0, 0); // SUCCESS
        } else {
            if (trace_file_io()) {
                printf("IFileMgr_EnumInit: '%s' dir='%s' FAILED\n", path.c_str(), enum_dir.c_str());
            }
            cpu.set_reg(REG_R0, (uint32_t)-1);
        }
        return;
    }

    if (name == "IFileMgr_EnumNext") {
        if (trace_file_io()) {
            printf("IFileMgr_EnumNext: idx=%zu\n", enum_idx_);
        }
        addr_t pInfo = arg1;
        if (pInfo && pInfo < 0xFF000000 && enum_idx_ < enum_entries_.size()) {
            const auto& path = enum_entries_[enum_idx_++];
            
            uint8_t attrib = 0;
            uint32_t size = 0;
            if (std::filesystem::is_directory(path)) attrib = 0x10; // AEE_ATTRIB_DIRECTORY
            else size = (uint32_t)std::filesystem::file_size(path);

            memory_.write_value(pInfo + FILEINFO_ATTRIB, attrib, EndianMemory::Byte);
            memory_.write_value(pInfo + FILEINFO_DATE, (uint32_t)0);
            memory_.write_value(pInfo + FILEINFO_SIZE, size);
            
            std::string fname = enum_name_prefix_ + path.filename().string();
            size_t len = std::min(fname.size(), (size_t)63);
            for (size_t i = 0; i < len; ++i)
                memory_.write_value(pInfo + FILEINFO_NAME + (uint32_t)i, (uint8_t)fname[i], EndianMemory::Byte);
            memory_.write_value(pInfo + FILEINFO_NAME + (uint32_t)len, (uint8_t)0, EndianMemory::Byte);
            
            cpu.set_reg(REG_R0, 1); // TRUE
        } else {
            cpu.set_reg(REG_R0, 0); // FALSE
        }
        return;
    }
    if (name == "IFileMgr_Rename") {
        std::string src = read_guest_string(arg1);
        addr_t dst_ptr = is_thunk ? r6 : cpu.get_reg(REG_R2);
        std::string dst = read_guest_string(dst_ptr);
        auto rs = vfs_.resolve(src, shell_.get_current_directory());
        auto rd = vfs_.resolve(dst, shell_.get_current_directory());
        std::error_code ec;
        if (rs.resolved && rd.resolved) {
            std::filesystem::rename(rs.host_path, rd.host_path, ec);
        }
        cpu.set_reg(REG_R0, (!ec && rs.resolved && rd.resolved) ? 0u : static_cast<uint32_t>(-1));
        return;
    }
    if (name == "IFileMgr_ResolvePath") {
        std::string in = read_guest_string(arg1);
        addr_t out = is_thunk ? r6 : cpu.get_reg(REG_R2);
        addr_t pnOutLen = is_thunk ? r7 : cpu.get_reg(REG_R3);
        auto r = vfs_.resolve(in, shell_.get_current_directory());
        std::string resolved = r.resolved ? r.host_path.string() : in;
        uint32_t need = static_cast<uint32_t>(resolved.size() + 1);
        uint32_t cap = (pnOutLen && pnOutLen < 0xFF000000) ? memory_.read_value(pnOutLen) : 0;
        if (pnOutLen && pnOutLen < 0xFF000000) memory_.write_value(pnOutLen, need);
        if (out && out < 0xFF000000 && cap >= need) {
            for (uint32_t i = 0; i < need; ++i)
                memory_.write_value(out + i, (uint8_t)resolved[i], EndianMemory::Byte);
            cpu.set_reg(REG_R0, 0);
        } else {
            cpu.set_reg(REG_R0, (out == 0) ? 0u : static_cast<uint32_t>(-1));
        }
        return;
    }
    if (name == "IFileMgr_CheckPathAccess") {
        addr_t pActual = is_thunk ? r7 : cpu.get_reg(REG_R3);
        uint32_t desired = is_thunk ? r6 : cpu.get_reg(REG_R2);
        if (pActual && pActual < 0xFF000000) memory_.write_value(pActual, desired);
        cpu.set_reg(REG_R0, 0);
        return;
    }
    if (name == "IFileMgr_Use" || name == "IFileMgr_SetDescription" || name == "IFileMgr_GetFileUseInfo") {
        cpu.set_reg(REG_R0, 0);
        return;
    }

    printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x R5=0x%08x R6=0x%08x R7=0x%08x\n",
           name.c_str(), cpu.get_reg(REG_R0), r1, cpu.get_reg(REG_R2),
           cpu.get_reg(REG_R3), r5, r6, r7);
    cpu.set_reg(REG_R0, 0);
}

// ---- BrewFile ----

BrewFile::BrewFile(BrewShell& shell, EndianMemory& memory, FILE* fp, const std::string& host_path)
    : shell_(shell), memory_(memory), fp_(fp), path_(host_path)
{
    setup_vtable();
}

BrewFile::~BrewFile() {
    if (fp_) { fclose(fp_); fp_ = nullptr; }
}

bool BrewFile::read_remaining_from_current(std::vector<uint8_t>& out) {
    out.clear();
    if (!fp_) {
        return false;
    }
    const long cur = ftell(fp_);
    if (cur < 0 || fseek(fp_, 0, SEEK_END) != 0) {
        return false;
    }
    const long end = ftell(fp_);
    if (end < cur || fseek(fp_, cur, SEEK_SET) != 0) {
        return false;
    }
    out.resize(static_cast<size_t>(end - cur));
    if (out.empty()) {
        return true;
    }
    const size_t n = fread(out.data(), 1, out.size(), fp_);
    return n == out.size();
}

void BrewFile::setup_vtable() {
    // INHERIT_IFile via INHERIT_IAStream:
    // [0] AddRef  [1] Release  [2] Readable  [3] Read  [4] Cancel
    // [5] Write  [6] GetInfo  [7] Seek  [8] Truncate  [9] GetInfoEx  [10] SetCacheSize  [11] Map
    vtable_ptr_ = shell_.malloc(12 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);

    auto add = [&](int idx, const std::string& n) {
        addr_t h = shell_.add_hook("IFile_" + n, this);
        memory_.write_value(vtable_ptr_ + static_cast<uint32_t>(idx * 4), h);
    };

    add(0,  "AddRef");
    add(1,  "Release");
    add(2,  "Readable");
    add(3,  "Read");
    add(4,  "Cancel");
    add(5,  "Write");
    add(6,  "GetInfo");
    add(7,  "Seek");
    add(8,  "Truncate");
    add(9,  "GetInfoEx");
    add(10, "SetCacheSize");
    add(11, "Map");
}

void BrewFile::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r5 = cpu.get_reg(REG_R5);
    uint32_t r6 = cpu.get_reg(REG_R6);

    bool is_thunk = (r1 >= 0xFF000000);
    addr_t arg1 = is_thunk ? r5 : r1;
    uint32_t arg2 = is_thunk ? r6 : cpu.get_reg(REG_R2);

    if (name == "IFile_AddRef") {
        if (trace_file_io()) {
            printf("IFile_AddRef: '%s'\n", path_.c_str());
        }
        cpu.set_reg(REG_R0, 1); return;
    }
    if (name == "IFile_Release") {
        if (trace_file_io()) {
            printf("IFile_Release: Closing file '%s'\n", path_.c_str());
        }
        if (fp_) { fclose(fp_); fp_ = nullptr; }
        cpu.set_reg(REG_R0, 0);
        return;
    }
    if (name == "IFile_Readable") {
        if (trace_file_io()) {
            printf("IFile_Readable: '%s' -> %d\n", path_.c_str(), fp_ ? 1 : 0);
        }
        cpu.set_reg(REG_R0, 0); return;
    }
    if (name == "IFile_Cancel")   { cpu.set_reg(REG_R0, 0); return; }
    if (name == "IFile_Truncate") {
        if (!fp_) { cpu.set_reg(REG_R0, static_cast<uint32_t>(-1)); return; }
        fflush(fp_);
        std::error_code ec;
        std::filesystem::resize_file(path_, arg1, ec);
        cpu.set_reg(REG_R0, ec ? static_cast<uint32_t>(-1) : 0u);
        return;
    }
    if (name == "IFile_SetCacheSize") { cpu.set_reg(REG_R0, 0); return; }
    if (name == "IFile_Map") {
        if (!fp_) { cpu.set_reg(REG_R0, 0); return; }
        addr_t requested = arg1;
        uint32_t size = arg2;
        uint32_t offset = is_thunk ? memory_.read_value(cpu.get_reg(REG_SP) + 4) : memory_.read_value(cpu.get_reg(REG_SP) + 4);
        long cur = ftell(fp_);
        fseek(fp_, 0, SEEK_END);
        long file_size = ftell(fp_);
        if (offset > static_cast<uint32_t>(std::max<long>(file_size, 0))) {
            fseek(fp_, cur, SEEK_SET);
            cpu.set_reg(REG_R0, 0);
            return;
        }
        uint32_t available = static_cast<uint32_t>(file_size - static_cast<long>(offset));
        uint32_t to_map = size ? std::min(size, available) : available;
        addr_t dst = (requested && requested < 0xFF000000) ? requested : shell_.malloc(to_map ? to_map : 1);
        std::vector<uint8_t> buf(to_map);
        fseek(fp_, static_cast<long>(offset), SEEK_SET);
        size_t n = fread(buf.data(), 1, to_map, fp_);
        fseek(fp_, cur, SEEK_SET);
        for (size_t i = 0; i < n; ++i)
            memory_.write_value(dst + static_cast<uint32_t>(i), buf[i], EndianMemory::Byte);
        if (trace_file_io()) {
            printf("IFile_Map: '%s' size=%u offset=%u -> 0x%x (%zu bytes)\n", path_.c_str(), size, offset, dst, n);
        }
        cpu.set_reg(REG_R0, dst);
        return;
    }

    if (name == "IFile_Read") {
        addr_t pDest = arg1;
        uint32_t cnt = arg2;
        if (!fp_ || cnt == 0 || pDest == 0 || pDest >= 0xFF000000) {
            cpu.set_reg(REG_R0, 0); return;
        }
        const bool trace_file_reads = std::getenv("ZEEMU_TRACE_FILE_READS") != nullptr;
        if (trace_file_reads && cnt >= 65536) {
            printf("IFile_Read: large begin path='%s' dest=0x%08x cnt=%u\n", path_.c_str(), pDest, cnt);
            fflush(stdout);
        }
        std::vector<uint8_t> buf(cnt);
        auto n = static_cast<uint32_t>(fread(buf.data(), 1, cnt, fp_));
        if (trace_file_reads && cnt >= 65536) {
            printf("IFile_Read: large fread done read=%u\n", n);
            fflush(stdout);
        }
        if (is_tga_path(path_) && n >= 18 && buf[1] == 0 && (buf[2] == 2 || buf[2] == 3)) {
            const uint32_t width = read_le16(&buf[12]);
            const uint32_t height = read_le16(&buf[14]);
            const uint32_t bpp = buf[16];
            if (width != 0 && height != 0 && (bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32)) {
                pending_tga_payload_hint_ = true;
                pending_tga_width_ = width;
                pending_tga_height_ = height;
                pending_tga_bpp_ = bpp;
                pending_tga_origin_top_ = (buf[17] & 0x20) != 0;
            }
        } else if (pending_tga_payload_hint_ && pDest >= 0x10000 && pDest < 0xFF000000 && n != 0) {
            brew_qxgl_register_tga_payload_hint(pDest,
                                                pending_tga_width_,
                                                pending_tga_height_,
                                                pending_tga_bpp_,
                                                pending_tga_origin_top_);
            pending_tga_payload_hint_ = false;
        }
        if (pDest < 0x10000 && n != 0) {
            brew_store_low_pointer_shadow(pDest, buf.data(), n);
            if (trace_file_reads) {
                printf("IFile_Read: low dest shadow ptr=0x%x bytes=%u\n", pDest, n);
            }
        } else {
            memory_.write(pDest, std::string(reinterpret_cast<const char*>(buf.data()), n));
        }
        if (trace_file_reads && cnt >= 65536) {
            printf("IFile_Read: large guest write done dest=0x%08x read=%u\n", pDest, n);
            fflush(stdout);
        }
        if (trace_file_io()) {
            printf("IFile_Read: cnt=%u -> read=%u\n", cnt, n);
            fflush(stdout);
        }
        cpu.set_reg(REG_R0, n);
        return;
    }

    if (name == "IFile_Write") {
        addr_t pSrc = arg1;
        uint32_t cnt = arg2;
        if (!fp_ || cnt == 0 || pSrc == 0) { cpu.set_reg(REG_R0, 0); return; }
        std::vector<uint8_t> buf(cnt);
        for (uint32_t i = 0; i < cnt; ++i)
            buf[i] = static_cast<uint8_t>(memory_.read_value(pSrc + i, EndianMemory::Byte));
        auto n = static_cast<uint32_t>(fwrite(buf.data(), 1, cnt, fp_));
        fflush(fp_);
        if (trace_file_io()) {
            printf("IFile_Write: cnt=%u -> wrote=%u\n", cnt, n);
        }
        cpu.set_reg(REG_R0, n);
        return;
    }

    if (name == "IFile_Seek") {
        if (trace_file_io()) {
            printf("IFile_Seek: '%s' seek=%u offset=%d\n", path_.c_str(), arg1, static_cast<int32_t>(arg2));
        }
        uint32_t seek_type = arg1;
        auto offset = static_cast<int32_t>(arg2);
        int whence = SEEK_SET;
        if (seek_type == SEEK_END_T) whence = SEEK_END;
        else if (seek_type == SEEK_CUR_T) whence = SEEK_CUR;
        if (!fp_) { cpu.set_reg(REG_R0, 1); return; }
        if (seek_type == SEEK_CUR_T && offset == 0) {
            cpu.set_reg(REG_R0, static_cast<uint32_t>(ftell(fp_)));
        } else {
            cpu.set_reg(REG_R0, fseek(fp_, offset, whence) == 0 ? 0u : 1u);
        }
        return;
    }

    if (name == "IFile_GetInfo") {
        if (trace_file_io()) {
            printf("IFile_GetInfo: '%s'\n", path_.c_str());
        }
        addr_t pInfo = arg1;
        if (!fp_ || pInfo == 0 || pInfo >= 0xFF000000) { cpu.set_reg(REG_R0, static_cast<uint32_t>(-1)); return; }
        long cur = ftell(fp_);
        fseek(fp_, 0, SEEK_END);
        long sz = ftell(fp_);
        fseek(fp_, cur, SEEK_SET);
        memory_.write_value(pInfo + FILEINFO_ATTRIB, (uint8_t)0, EndianMemory::Byte);
        memory_.write_value(pInfo + FILEINFO_DATE, (uint32_t)0);
        memory_.write_value(pInfo + FILEINFO_SIZE, static_cast<uint32_t>(sz));
        std::string fname = std::filesystem::path(path_).filename().string();
        size_t len = std::min(fname.size(), static_cast<size_t>(63));
        for (size_t i = 0; i < len; ++i)
            memory_.write_value(pInfo + FILEINFO_NAME + static_cast<uint32_t>(i), (uint8_t)fname[i], EndianMemory::Byte);
        memory_.write_value(pInfo + FILEINFO_NAME + static_cast<uint32_t>(len), 0, EndianMemory::Byte);
        cpu.set_reg(REG_R0, 0);
        return;
    }

    // AEEFileInfoEx layout (differs from AEEFileInfo):
    //   +0  nStructSize    (int32)
    //   +4  attrib         (char, +3 pad)
    //   +8  dwCreationDate (uint32)
    //   +12 dwSize         (uint32)
    //   +16 pszFile        (char* — guest pointer to filename buffer)
    //   +20 nMaxFile       (int32)
    //   +24 pszDescription (AECHAR*)
    //   +28 nDescriptionSize (int32)
    //   +32 pClasses       (AEECLSID*)
    //   +36 nClassesSize   (int32)
    if (name == "IFile_GetInfoEx") {
        if (trace_file_io()) {
            printf("IFile_GetInfoEx: '%s'\n", path_.c_str());
        }
        addr_t pEx = arg1;
        if (!fp_ || pEx == 0 || pEx >= 0xFF000000) { cpu.set_reg(REG_R0, static_cast<uint32_t>(-1)); return; }
        long cur = ftell(fp_);
        fseek(fp_, 0, SEEK_END);
        long sz = ftell(fp_);
        fseek(fp_, cur, SEEK_SET);
        // attrib at +4, date at +8, size at +12
        memory_.write_value(pEx + 4, (uint8_t)0, EndianMemory::Byte);
        memory_.write_value(pEx + 8, (uint32_t)0);
        memory_.write_value(pEx + 12, static_cast<uint32_t>(sz));
        // write filename to the guest buffer at pszFile (+16), limited by nMaxFile (+20)
        uint32_t pszFile = static_cast<uint32_t>(memory_.read_value(pEx + 16));
        int32_t nMaxFile = static_cast<int32_t>(memory_.read_value(pEx + 20));
        if (pszFile && pszFile < 0xFF000000 && nMaxFile > 0) {
            std::string fname = std::filesystem::path(path_).filename().string();
            size_t len = std::min(fname.size(), static_cast<size_t>(nMaxFile - 1));
            for (size_t i = 0; i < len; ++i)
                memory_.write_value(pszFile + static_cast<uint32_t>(i), (uint8_t)fname[i], EndianMemory::Byte);
            memory_.write_value(pszFile + static_cast<uint32_t>(len), 0, EndianMemory::Byte);
        }
        cpu.set_reg(REG_R0, 0);
        return;
    }

    printf("  [%s] not implemented yet file='%s' R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x R5=0x%08x R6=0x%08x\n",
           name.c_str(), path_.c_str(), cpu.get_reg(REG_R0), r1,
           cpu.get_reg(REG_R2), cpu.get_reg(REG_R3), r5, r6);
    cpu.set_reg(REG_R0, 0);
}
