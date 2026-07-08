#ifndef ZEEMU_BREW_FILEMGR_H_
#define ZEEMU_BREW_FILEMGR_H_

#include "brew/BrewShell.h"
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"
#include "vfs/VirtualFileSystem.h"
#include <cstddef>
#include <string>
#include <vector>
#include <cstdio>
#include <filesystem>

const std::vector<uint8_t>* brew_take_low_pointer_shadow(addr_t ptr, uint32_t min_size);
const std::vector<uint8_t>* brew_take_low_pointer_shadow_with_prefix(addr_t ptr, const uint8_t* prefix, size_t prefix_size);
void brew_store_low_pointer_shadow(addr_t ptr, const uint8_t* data, size_t size);

class BrewFile : public BrewService {
public:
    BrewFile(BrewShell& shell, EndianMemory& memory, FILE* fp, const std::string& host_path);
    ~BrewFile();

    addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;
    bool read_remaining_from_current(std::vector<uint8_t>& out);
    void close_if_matches(const std::string& host_path) {
        if (fp_ && path_ == host_path) { fclose(fp_); fp_ = nullptr; }
    }

private:
    void setup_vtable();

    BrewShell& shell_;
    EndianMemory& memory_;
    FILE* fp_;
    std::string path_;
    addr_t object_ptr_{};
    addr_t vtable_ptr_{};
    bool pending_tga_payload_hint_ = false;
    uint32_t pending_tga_width_ = 0;
    uint32_t pending_tga_height_ = 0;
    uint32_t pending_tga_bpp_ = 0;
    bool pending_tga_origin_top_ = true;
};

class BrewFileMgr : public BrewService {
public:
    BrewFileMgr(BrewShell& shell, EndianMemory& memory, VirtualFileSystem& vfs);
    ~BrewFileMgr();

    addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;
    BrewFile* find_open_file(addr_t object_ptr) const;

private:
    void setup_vtable();
    std::string read_guest_string(addr_t addr) const;

    BrewShell& shell_;
    EndianMemory& memory_;
    VirtualFileSystem& vfs_;
    addr_t object_ptr_{};
    addr_t vtable_ptr_{};
    std::vector<BrewFile*> open_files_;
    int last_error_ = 0;

    std::vector<std::filesystem::path> enum_entries_;
    size_t enum_idx_ = 0;
    std::string enum_name_prefix_;
};

#endif
