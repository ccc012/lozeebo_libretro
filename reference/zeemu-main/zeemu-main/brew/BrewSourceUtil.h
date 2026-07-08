#ifndef ZEEMU_BREW_SOURCE_UTIL_H_
#define ZEEMU_BREW_SOURCE_UTIL_H_

#include "brew/BrewService.h"
#include "cpu/memory/EndianMemory.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class BrewFileMgr;
class BrewShell;

class BrewSourceUtil : public BrewService {
public:
    BrewSourceUtil(BrewShell& shell, EndianMemory& memory, BrewFileMgr& file_mgr);

    addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    struct SourceState {
        addr_t object = 0;
        addr_t vtable = 0;
        std::vector<uint8_t> data;
        size_t pos = 0;
    };

    struct GetLineState {
        addr_t object = 0;
        addr_t vtable = 0;
        SourceState* source = nullptr;
        size_t pos = 0;
        addr_t last_line = 0;
    };

    void setup_vtable();
    SourceState* create_source(std::vector<uint8_t> data);
    GetLineState* create_getline(SourceState* source);
    SourceState* find_source(addr_t object);
    GetLineState* find_getline(addr_t object);
    addr_t write_line_buffer(const std::string& line, uint32_t prefix_size);

    BrewShell& shell_;
    EndianMemory& memory_;
    BrewFileMgr& file_mgr_;
    addr_t object_ptr_ = 0;
    addr_t vtable_ptr_ = 0;
    addr_t source_vtable_ptr_ = 0;
    addr_t getline_vtable_ptr_ = 0;
    std::vector<SourceState> sources_;
    std::vector<GetLineState> getlines_;
};

#endif
