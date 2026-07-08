#include "vfs/VirtualFileSystem.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

std::string slashes(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool starts_with_ci(const std::string& text, const std::string& prefix)
{
    if (text.size() < prefix.size()) {
        return false;
    }
    return lower(text.substr(0, prefix.size())) == lower(prefix);
}

bool is_absolute_brew_path(const std::string& path)
{
    return starts_with_ci(path, "fs:/");
}

std::string trim_trailing_slash(std::string path)
{
    while (path.size() > 4 && path.back() == '/') {
        path.pop_back();
    }
    return path;
}

std::vector<std::string> split(const std::string& text, char separator)
{
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : text) {
        if (ch == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    parts.push_back(current);
    return parts;
}

std::string join_normalized_components(const std::vector<std::string>& components)
{
    std::string result = "fs:/";
    for (std::size_t i = 0; i < components.size(); ++i) {
        if (i != 0) {
            result += '/';
        }
        result += components[i];
    }
    return trim_trailing_slash(result);
}

std::string make_brew_absolute(const std::string& path, const std::string& current_directory)
{
    std::string fixed = slashes(path);
    if (is_absolute_brew_path(fixed)) {
        return fixed;
    }

    std::string current = VirtualFileSystem::normalize(current_directory);
    if (!is_absolute_brew_path(current)) {
        current = "fs:/";
    }

    if (fixed.rfind("./", 0) == 0) {
        fixed.erase(0, 2);
    }

    if (!current.empty() && current.back() != '/') {
        current.push_back('/');
    }
    return current + fixed;
}

bool path_matches_mount(const std::string& path, const std::string& mount)
{
    if (lower(path) == lower(mount)) {
        return true;
    }
    return starts_with_ci(path, mount + "/");
}

std::string relative_to_mount(const std::string& path, const std::string& mount)
{
    if (path.size() == mount.size()) {
        return {};
    }
    return path.substr(mount.size() + 1);
}

fs::path append_relative(fs::path root, const std::string& relative)
{
    for (const std::string& component : split(relative, '/')) {
        if (!component.empty()) {
            root /= component;
        }
    }
    return root;
}

uint64_t file_size_or_zero(const fs::path& path)
{
    std::error_code error;
    const uint64_t size = fs::file_size(path, error);
    return error ? 0 : size;
}

void print_resolve_result(const VirtualFileSystem::ResolveResult& result, std::ostream& out)
{
    out << "input: " << result.input_path << '\n';
    out << "normalized: " << result.normalized_path << '\n';
    if (!result.resolved) {
        out << "resolved: no\n";
        return;
    }

    out << "resolved: yes\n";
    out << "mount: " << result.matched_virtual_path << '\n';
    out << "host: " << result.host_path.string() << '\n';
    out << "exists: " << (result.exists ? "yes" : "no") << '\n';
    if (result.exists) {
        out << "type: " << (result.is_directory ? "directory" : "file") << '\n';
        if (!result.is_directory) {
            out << "size: " << result.size << " bytes\n";
        }
    }
}
}

void VirtualFileSystem::mount(std::string virtual_path, fs::path host_path, std::string label)
{
    Mount entry;
    entry.virtual_path = normalize(virtual_path);
    entry.host_path = std::move(host_path);
    entry.label = std::move(label);
    mounts_.push_back(std::move(entry));

    std::sort(mounts_.begin(), mounts_.end(), [](const Mount& left, const Mount& right) {
        return left.virtual_path.size() > right.virtual_path.size();
    });
}

void VirtualFileSystem::alias(std::string alias_path, std::string target_path, std::string label)
{
    const ResolveResult target = resolve(target_path);
    mount(std::move(alias_path), target.resolved ? target.host_path : fs::path(), std::move(label));
}

VirtualFileSystem::ResolveResult VirtualFileSystem::resolve(
    const std::string& virtual_path,
    const std::string& current_directory) const
{
    ResolveResult result;
    result.input_path = virtual_path;
    result.normalized_path = normalize(virtual_path, current_directory);

    for (const Mount& mount_entry : mounts_) {
        if (!path_matches_mount(result.normalized_path, mount_entry.virtual_path)) {
            continue;
        }

        result.resolved = true;
        result.matched_virtual_path = mount_entry.virtual_path;
        result.host_path = append_relative(
            mount_entry.host_path,
            relative_to_mount(result.normalized_path, mount_entry.virtual_path));

        std::error_code error;
        result.exists = fs::exists(result.host_path, error);
        result.is_directory = result.exists && fs::is_directory(result.host_path, error);
        result.size = (result.exists && !result.is_directory) ? file_size_or_zero(result.host_path) : 0;
        return result;
    }

    return result;
}

const std::vector<VirtualFileSystem::Mount>& VirtualFileSystem::mounts() const
{
    return mounts_;
}

bool VirtualFileSystem::read_file(
    const std::string& virtual_path,
    std::string& data,
    const std::string& current_directory) const
{
    const ResolveResult result = resolve(virtual_path, current_directory);
    if (!result.resolved || !result.exists || result.is_directory) {
        return false;
    }

    std::ifstream file(result.host_path, std::ios::binary);
    if (!file) {
        return false;
    }

    data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool VirtualFileSystem::write_file(
    const std::string& virtual_path,
    const std::string& data,
    const std::string& current_directory) const
{
    const ResolveResult result = resolve(virtual_path, current_directory);
    if (!result.resolved) {
        return false;
    }

    std::error_code error;
    fs::create_directories(result.host_path.parent_path(), error);
    if (error) {
        return false;
    }

    std::ofstream file(result.host_path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }

    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    return !!file;
}

std::string VirtualFileSystem::normalize(const std::string& path, const std::string& current_directory)
{
    std::string absolute = make_brew_absolute(path, current_directory);
    absolute = slashes(absolute);

    if (!is_absolute_brew_path(absolute)) {
        absolute = "fs:/" + absolute;
    }

    std::vector<std::string> normalized;
    std::string rest = absolute.substr(4);
    while (!rest.empty() && rest.front() == '/') {
        rest.erase(rest.begin());
    }

    for (const std::string& component : split(rest, '/')) {
        if (component.empty() || component == ".") {
            continue;
        }
        if (component == "..") {
            if (normalized.size() == 1 && !normalized[0].empty() && normalized[0][0] == '~') {
                normalized.push_back(component);
            } else if (!normalized.empty()) {
                normalized.pop_back();
            }
            continue;
        }
        normalized.push_back(component);
    }

    return join_normalized_components(normalized);
}

void VirtualFileSystem::mount_roms(const fs::path& roms_root)
{
    if (!fs::exists(roms_root) || !fs::is_directory(roms_root)) {
        return;
    }

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(roms_root, ec)) {
        if (ec) break;
        const auto& path = entry.path();
        
        if (entry.is_directory()) {
            std::string name = path.filename().string();
            std::string parent_name = path.parent_path().filename().string();

            // Pattern: .../mod/<id>
            if (parent_name == "mod") {
                mount("fs:/mmc4/mod/" + name, path, "Auto-mount MOD " + name);
                if (name == "274755") {
                    mount("fs:/mod/274755", path, "Z-Wheel NAND mod");
                }
            } else if (name == "doom") {
                // Special case for PrBoom data folder
                mount("fs:/mmc4/doom", path, "Auto-mount PrBoom doom data");
            }
        } else if (entry.is_regular_file()) {
            std::string ext = lower(path.extension().string());
            if (ext == ".mif") {
                std::string id = path.stem().string();
                mount("fs:/mmc4/mif/" + id + ".mif", path, "Auto-mount MIF " + id);
                if (id == "274755") {
                    mount("fs:/mif/274755.mif", path, "Z-Wheel NAND MIF");
                } else if (id == "274802") {
                    mount("fs:/mif/274802.mif", path, "Quake NAND MIF");
                }
            }
        }
    }
}

void VirtualFileSystem::setup_app_mounts(uint32_t clsid, const fs::path& app_dir)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%08X", clsid);
    std::string clsid_str(buf);

    mount("fs:/~" + clsid_str, app_dir, "App CLSID root alias");
    mount("fs:/~", app_dir, "App root alias");
}

VirtualFileSystem VirtualFileSystem::create_default(const fs::path& project_root)
{
    VirtualFileSystem vfs;
    const fs::path root = project_root.empty() ? fs::current_path() : project_root;

    // Writable areas
    vfs.mount("fs:/shared", root / "rootfs" / "shared", "writable shared area");
    vfs.mount("fs:/sys", root / "rootfs" / "sys", "writable system area");
    vfs.mount("fs:/lctsys", root / "rootfs" / "lctsys", "writable lctsys area");

    // Scan and mount ROMs
    vfs.mount_roms(root / "roms");

    // Z-Wheel's MCP/catalog helper enumerates installed package roots through
    // fs:/mcp/mod. In the extracted local layout, the launcher package keeps
    // those roots under roms/274755/mod and roms/274755/mif.
    vfs.mount("fs:/mcp/mod", root / "roms" / "274755" / "mod", "Z-Wheel MCP mod root");
    vfs.mount("fs:/mcp/mif", root / "roms" / "274755" / "mif", "Z-Wheel MCP MIF root");

    return vfs;
}

bool VirtualFileSystemInspector::print_default_map(const fs::path& project_root, std::ostream& out)
{
    const VirtualFileSystem vfs = VirtualFileSystem::create_default(project_root);
    out << "Default Zeebo VFS map:\n";
    for (const VirtualFileSystem::Mount& mount : vfs.mounts()) {
        out << "  " << std::left << std::setw(26) << mount.virtual_path
            << " -> " << mount.host_path.string();
        if (!mount.label.empty()) {
            out << "  [" << mount.label << "]";
        }
        out << '\n';
    }
    return true;
}

bool VirtualFileSystemInspector::resolve_default(
    const fs::path& project_root,
    const std::string& virtual_path,
    const std::string& current_directory,
    std::ostream& out)
{
    const VirtualFileSystem vfs = VirtualFileSystem::create_default(project_root);
    print_resolve_result(vfs.resolve(virtual_path, current_directory), out);
    return true;
}
