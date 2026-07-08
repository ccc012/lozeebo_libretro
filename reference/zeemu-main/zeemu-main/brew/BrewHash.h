#ifndef ZEEMU_BREW_HASH_H_
#define ZEEMU_BREW_HASH_H_

#include "brew/BrewShell.h"
#include <unordered_map>
#include <vector>

class BrewHash : public BrewService {
public:
    BrewHash(BrewShell& shell, EndianMemory& memory);

    addr_t create_instance(uint32_t clsId);
    void handle_hook(const std::string& name, CPU& cpu) override;

private:
    enum class Algo {
        MD5,
        MD2,
        SHA1,
    };

    struct State {
        Algo algo;
        bool hmac{};
        bool is_ctx{};
        addr_t object_ptr{};
        std::vector<uint8_t> data;
        std::vector<uint8_t> key;
        State() = default;
        State(Algo algo, bool hmac, bool is_ctx, addr_t object_ptr)
            : algo(algo), hmac(hmac), is_ctx(is_ctx), object_ptr(object_ptr) {}
    };

    static bool is_ctx_class(uint32_t clsId);
    static std::pair<Algo, bool> decode_class(uint32_t clsId);
    static std::vector<uint8_t> digest_for_algo(Algo algo, const std::vector<uint8_t>& data);
    static std::vector<uint8_t> hmac_for_state(const State& state);

    std::vector<uint8_t> compute_digest(const State& state);
    void append_bytes(State& state, uint32_t ptr, uint32_t count);
    void append_key(State& state, uint32_t ptr, uint32_t count);
    void write_result(State& state, CPU& cpu, bool final_call, uint32_t out_ptr, uint32_t len_ptr);

    BrewShell& shell_;
    EndianMemory& memory_;
    std::unordered_map<addr_t, State> states_;
    static constexpr const char* hash_names_[6] = {
        "Hash_AddRef",
        "Hash_Release",
        "Hash_Update",
        "Hash_GetResult",
        "Hash_Restart",
        "Hash_SetKey",
    };
    static constexpr const char* ctx_names_[7] = {
        "Hash_AddRef",
        "Hash_Release",
        "Hash_QueryInterface",
        "Hash_Init",
        "Hash_Update",
        "Hash_Final",
        "Hash_SetKey",
    };
};

#endif
