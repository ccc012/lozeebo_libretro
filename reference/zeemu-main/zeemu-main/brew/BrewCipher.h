#ifndef ZEEMU_BREW_CIPHER_H_
#define ZEEMU_BREW_CIPHER_H_

#include "brew/BrewService.h"
#include "cpu/cpu.h"
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

class BrewShell;
class EndianMemory;
class CPU;

class BrewCipher : public BrewService {
public:
    BrewCipher(BrewShell& shell, EndianMemory& memory);

    [[nodiscard]] addr_t get_factory_object_ptr() const { return factory_object_ptr_; }
    void handle_hook(const std::string& name, CPU& cpu) override;

private:
    struct State {
        addr_t object_ptr = 0;
        uint32_t cipher_id = 0;
        uint32_t mode_id = 0;
        uint32_t direction = 0;
        uint32_t padding = 0;
        bool has_key = false;
        std::array<uint8_t, 16> key{};
        std::array<uint8_t, 16> iv{};
        std::vector<uint8_t> buffered;
    };

    addr_t create_cipher(uint32_t cipher_id, uint32_t direction, uint32_t mode_id, uint32_t padding);
    int query_cipher(uint32_t cipher_id, uint32_t mode_id, uint32_t padding, uint32_t key_size) const;
    int set_key(State& state, addr_t ptr, uint32_t size);
    int set_iv(State& state, addr_t ptr, uint32_t size);
    int process(State& state, addr_t in_ptr, uint32_t in_size, addr_t out_ptr, addr_t out_size_ptr);
    int process_last(State& state, addr_t out_ptr, addr_t out_size_ptr);
    int get_param(State& state, uint32_t id, addr_t param_ptr, addr_t param_len_ptr);
    int set_param(State& state, uint32_t id, addr_t param_ptr, uint32_t param_len);

    std::vector<uint8_t> read_bytes(addr_t ptr, uint32_t size) const;
    void write_bytes(addr_t ptr, const std::vector<uint8_t>& bytes) const;
    bool write_param_u32(addr_t param_ptr, addr_t param_len_ptr, uint32_t value) const;

    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t factory_object_ptr_ = 0;
    addr_t factory_vtable_ptr_ = 0;
    addr_t cipher_vtable_ptr_ = 0;
    std::unordered_map<addr_t, State> states_;
};

#endif
