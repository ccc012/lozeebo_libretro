#ifndef ZEEMU_VIRTUAL_FILE_SYSTEM_H_
#define ZEEMU_VIRTUAL_FILE_SYSTEM_H_

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

#include "cpu/cpu.h"

class ZEEMU_EXPORT VirtualFileSystem {
public:
    struct Mount {
        std::string virtual_path;
        std::filesystem::path host_path;
        std::string label;
    };

    struct ResolveResult {
        std::string input_path;
        std::string normalized_path;
        std::string matched_virtual_path;
        std::filesystem::path host_path;
        bool resolved = false;
        bool exists = false;
        bool is_directory = false;
        uint64_t size = 0;
    };

    void mount(std::string virtual_path, std::filesystem::path host_path, std::string label = {});
    void alias(std::string alias_path, std::string target_path, std::string label = {});
    void mount_roms(const std::filesystem::path& roms_root);
    void setup_app_mounts(uint32_t clsid, const std::filesystem::path& app_dir);

    ResolveResult resolve(const std::string& virtual_path, const std::string& current_directory = "fs:/") const;
    bool read_file(const std::string& virtual_path, std::string& data, const std::string& current_directory = "fs:/") const;
    bool write_file(const std::string& virtual_path, const std::string& data, const std::string& current_directory = "fs:/") const;
    const std::vector<Mount>& mounts() const;

    static std::string normalize(const std::string& path, const std::string& current_directory = "fs:/");
    static VirtualFileSystem create_default(const std::filesystem::path& project_root);

private:
    std::vector<Mount> mounts_;
};

class ZEEMU_EXPORT VirtualFileSystemInspector {
public:
    static bool print_default_map(const std::filesystem::path& project_root, std::ostream& out);
    static bool resolve_default(
        const std::filesystem::path& project_root,
        const std::string& virtual_path,
        const std::string& current_directory,
        std::ostream& out);
};

#endif
