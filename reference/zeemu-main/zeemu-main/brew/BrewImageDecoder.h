#ifndef ZEEMU_BREW_IMAGE_DECODER_H_
#define ZEEMU_BREW_IMAGE_DECODER_H_

#include "brew/BrewShell.h"
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"

#include <memory>
#include <string>
#include <vector>

class BrewBitmap;

class BrewImageDecoder : public BrewService {
public:
    BrewImageDecoder(BrewShell& shell, EndianMemory& memory, std::string label);

    addr_t get_decoder_object_ptr() const { return decoder_object_ptr_; }
    addr_t get_forcefeed_object_ptr() const { return forcefeed_object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    void setup_vtables();
    bool decode_if_needed();
    void write_query_result(uint32_t iid, uint32_t pp, class CPU& cpu);

    BrewShell& shell_;
    EndianMemory& memory_;
    std::string label_;
    addr_t decoder_object_ptr_ = 0;
    addr_t decoder_vtable_ptr_ = 0;
    addr_t forcefeed_object_ptr_ = 0;
    addr_t forcefeed_vtable_ptr_ = 0;
    uint32_t refs_ = 1;
    bool dirty_ = false;
    std::vector<uint8_t> encoded_;
    std::unique_ptr<BrewBitmap> bitmap_;
};

#endif
