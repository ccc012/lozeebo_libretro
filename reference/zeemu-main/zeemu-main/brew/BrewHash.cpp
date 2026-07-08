#include "brew/BrewHash.h"
#include "cpu/core/CPU.h"
#include <algorithm>
#include <array>
#include <cstring>

static uint32_t rotl32(uint32_t v, uint32_t n) {
    return (v << n) | (v >> (32u - n));
}

static uint32_t load_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
        | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16)
        | (static_cast<uint32_t>(p[3]) << 24);
}

static uint32_t load_u32_be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24)
        | (static_cast<uint32_t>(p[1]) << 16)
        | (static_cast<uint32_t>(p[2]) << 8)
        | static_cast<uint32_t>(p[3]);
}

static void store_u32_le(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xffu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xffu);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xffu);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xffu);
}

static void store_u32_be(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xffu);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xffu);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xffu);
    p[3] = static_cast<uint8_t>(v & 0xffu);
}

static std::array<uint8_t, 16> md5_digest(const std::vector<uint8_t>& input) {
    std::vector<uint8_t> msg = input;
    const uint64_t bit_len = static_cast<uint64_t>(msg.size()) * 8u;
    msg.push_back(0x80u);
    while ((msg.size() % 64u) != 56u) {
        msg.push_back(0u);
    }
    for (int i = 0; i < 8; ++i) {
        msg.push_back(static_cast<uint8_t>((bit_len >> (8u * i)) & 0xffu));
    }

    uint32_t a0 = 0x67452301u;
    uint32_t b0 = 0xefcdab89u;
    uint32_t c0 = 0x98badcfeu;
    uint32_t d0 = 0x10325476u;

    static const uint32_t s[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
    };
    static const uint32_t k[64] = {
        0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
        0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
        0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
        0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
        0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
        0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
        0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
        0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
        0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
        0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
        0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
        0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
        0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
        0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
        0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
        0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u
    };

    for (size_t off = 0; off < msg.size(); off += 64u) {
        uint32_t a = a0;
        uint32_t b = b0;
        uint32_t c = c0;
        uint32_t d = d0;
        uint32_t m[16];
        for (int i = 0; i < 16; ++i) {
            m[i] = load_u32_le(&msg[off + static_cast<size_t>(i) * 4u]);
        }

        for (int i = 0; i < 64; ++i) {
            uint32_t f = 0;
            uint32_t g = 0;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = static_cast<uint32_t>(i);
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5u * static_cast<uint32_t>(i) + 1u) & 15u;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3u * static_cast<uint32_t>(i) + 5u) & 15u;
            } else {
                f = c ^ (b | ~d);
                g = (7u * static_cast<uint32_t>(i)) & 15u;
            }
            uint32_t temp = d;
            d = c;
            c = b;
            b = b + rotl32(a + f + k[i] + m[g], s[i]);
            a = temp;
        }

        a0 += a;
        b0 += b;
        c0 += c;
        d0 += d;
    }

    std::array<uint8_t, 16> out{};
    store_u32_le(&out[0], a0);
    store_u32_le(&out[4], b0);
    store_u32_le(&out[8], c0);
    store_u32_le(&out[12], d0);
    return out;
}

static std::array<uint8_t, 20> sha1_digest(const std::vector<uint8_t>& input) {
    std::vector<uint8_t> msg = input;
    const uint64_t bit_len = static_cast<uint64_t>(msg.size()) * 8u;
    msg.push_back(0x80u);
    while ((msg.size() % 64u) != 56u) {
        msg.push_back(0u);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<uint8_t>((bit_len >> (8u * static_cast<uint64_t>(i))) & 0xffu));
    }

    uint32_t h0 = 0x67452301u;
    uint32_t h1 = 0xefcdab89u;
    uint32_t h2 = 0x98badcfeu;
    uint32_t h3 = 0x10325476u;
    uint32_t h4 = 0xc3d2e1f0u;

    for (size_t off = 0; off < msg.size(); off += 64u) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = load_u32_be(&msg[off + static_cast<size_t>(i) * 4u]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0;
            uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5a827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdcu;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6u;
            }
            uint32_t temp = rotl32(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl32(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::array<uint8_t, 20> out{};
    store_u32_be(&out[0], h0);
    store_u32_be(&out[4], h1);
    store_u32_be(&out[8], h2);
    store_u32_be(&out[12], h3);
    store_u32_be(&out[16], h4);
    return out;
}

static std::array<uint8_t, 16> md2_digest(const std::vector<uint8_t>& input) {
    static constexpr uint8_t s[256] = {
        41,46,67,201,162,216,124,1,61,54,84,161,236,240,6,19,
        98,167,5,243,192,199,115,140,152,147,43,217,188,76,130,202,
        30,155,87,60,253,212,224,22,103,66,111,24,138,23,229,18,
        190,78,196,214,218,158,222,73,160,251,245,142,187,47,238,122,
        169,104,121,145,21,178,7,63,148,194,16,137,11,34,95,33,
        128,127,93,154,90,144,50,39,53,62,204,231,191,247,151,3,
        255,25,48,179,72,165,181,209,215,94,146,42,172,86,170,198,
        79,184,56,210,150,164,125,182,118,252,107,226,156,116,4,241,
        69,157,112,89,100,113,135,32,134,91,207,101,230,45,168,2,
        27,96,37,173,174,176,185,246,28,70,97,105,52,64,126,15,
        85,71,163,35,221,81,175,58,195,92,249,206,186,197,234,38,
        44,83,13,110,133,40,132,9,211,223,205,244,65,129,77,82,
        106,220,55,200,108,193,171,250,36,225,123,8,12,189,177,74,
        120,136,149,139,227,99,232,109,233,203,213,254,59,0,29,57,
        242,239,183,14,102,88,208,228,166,119,114,248,235,117,75,10,
        49,68,80,180,143,237,31,26,219,153,141,51,159,17,131,20
    };

    std::vector<uint8_t> msg = input;
    const uint8_t pad_len = static_cast<uint8_t>(16u - (msg.size() % 16u));
    msg.insert(msg.end(), pad_len, pad_len);

    uint8_t checksum[16] = {};
    uint8_t l = 0;
    for (size_t off = 0; off < msg.size(); off += 16u) {
        for (int i = 0; i < 16; ++i) {
            uint8_t c = msg[off + static_cast<size_t>(i)];
            checksum[i] ^= s[c ^ l];
            l = checksum[i];
        }
    }
    msg.insert(msg.end(), checksum, checksum + 16);

    uint8_t x[48] = {};
    for (size_t off = 0; off < msg.size(); off += 16u) {
        for (int i = 0; i < 16; ++i) {
            x[16 + i] = msg[off + static_cast<size_t>(i)];
            x[32 + i] = static_cast<uint8_t>(x[16 + i] ^ x[i]);
        }

        uint8_t t = 0;
        for (int round = 0; round < 18; ++round) {
            for (uint8_t& v : x) {
                v ^= s[t];
                t = v;
            }
            t = static_cast<uint8_t>(t + round);
        }
    }

    std::array<uint8_t, 16> out{};
    std::copy(x, x + 16, out.begin());
    return out;
}

BrewHash::BrewHash(BrewShell& shell, EndianMemory& memory) : shell_(shell), memory_(memory) {}

addr_t BrewHash::create_instance(uint32_t clsId) {
    const bool is_ctx = is_ctx_class(clsId);
    const auto [algo, hmac] = decode_class(clsId);
    const char* const* names = is_ctx ? ctx_names_ : hash_names_;
    const int slot_count = is_ctx ? 7 : 6;

    addr_t vtable = shell_.malloc(static_cast<uint32_t>(slot_count * 4));
    addr_t object = shell_.malloc(4);
    memory_.write_value(object, vtable);
    for (int i = 0; i < slot_count; ++i) {
        memory_.write_value(vtable + static_cast<uint32_t>(i * 4), shell_.add_hook(names[i], this));
    }

    states_[object] = State{algo, hmac, is_ctx, object};
    return object;
}

void BrewHash::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t obj = cpu.get_reg(REG_R0);
    const uint32_t r1 = cpu.get_reg(REG_R1);
    const uint32_t r2 = cpu.get_reg(REG_R2);
    const uint32_t r3 = cpu.get_reg(REG_R3);
    const bool is_thunk = r1 >= 0xFF000000u;
    const uint32_t arg1 = is_thunk ? cpu.get_reg(REG_R5) : r1;
    const uint32_t arg2 = is_thunk ? cpu.get_reg(REG_R6) : r2;
    auto it = states_.find(obj);
    if (it == states_.end()) {
        printf("  [%s] not implemented yet invalid hash=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), obj, r1, r2, r3);
        cpu.set_reg(REG_R0, 0);
        return;
    }

    State& state = it->second;

    if (name == "Hash_AddRef") {
        cpu.set_reg(REG_R0, 1);
    } else if (name == "Hash_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "Hash_QueryInterface") {
        const uint32_t iid = arg1;
        const uint32_t ppObj = arg2;
        constexpr uint32_t kAEEIID_IQI = 0x01000001u;
        constexpr uint32_t kAEEIID_IHashCtx = 0x0102cb09u;
        const bool supported = state.is_ctx && (iid == kAEEIID_IQI || iid == kAEEIID_IHashCtx);
        if (ppObj != 0 && ppObj < 0xFF000000u) {
            memory_.write_value(ppObj, supported ? state.object_ptr : 0u);
        }
        cpu.set_reg(REG_R0, supported ? 0u : 2u);
    } else if (name == "Hash_Init") {
        state.data.clear();
        state.key.clear();
        cpu.set_reg(REG_R0, 0);
    } else if (name == "Hash_Update") {
        append_bytes(state, arg1, arg2);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "Hash_GetResult" || name == "Hash_Final") {
        write_result(state, cpu, name == "Hash_Final", arg1, arg2);
    } else if (name == "Hash_Restart") {
        state.data.clear();
        cpu.set_reg(REG_R0, 0);
    } else if (name == "Hash_SetKey") {
        state.key.clear();
        append_key(state, arg1, arg2);
        cpu.set_reg(REG_R0, 0);
    } else {
        printf("  [%s] not implemented yet hash=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), obj, r1, r2, r3);
        cpu.set_reg(REG_R0, 0);
    }
}

bool BrewHash::is_ctx_class(uint32_t clsId) {
    switch (clsId) {
        case 0x01001039:
        case 0x0100103a:
        case 0x0100103b:
        case 0x0100104c:
        case 0x0100104d:
        case 0x0100104e:
            return true;
        default:
            return false;
    }
}

std::pair<BrewHash::Algo, bool> BrewHash::decode_class(uint32_t clsId) {
    switch (clsId) {
        case 0x01001015: return {Algo::MD5, false};
        case 0x01001027: return {Algo::MD2, false};
        case 0x01001028: return {Algo::SHA1, false};
        case 0x01001039: return {Algo::MD5, false};
        case 0x0100103a: return {Algo::MD2, false};
        case 0x0100103b: return {Algo::SHA1, false};
        case 0x01001049: return {Algo::MD5, true};
        case 0x0100104a: return {Algo::MD2, true};
        case 0x0100104b: return {Algo::SHA1, true};
        case 0x0100104c: return {Algo::MD5, true};
        case 0x0100104d: return {Algo::MD2, true};
        case 0x0100104e: return {Algo::SHA1, true};
        default: return {Algo::MD5, false};
    }
}

std::vector<uint8_t> BrewHash::digest_for_algo(Algo algo, const std::vector<uint8_t>& data) {
    if (algo == Algo::SHA1) {
        auto d = sha1_digest(data);
        return std::vector<uint8_t>(d.begin(), d.end());
    }
    if (algo == Algo::MD2) {
        auto d = md2_digest(data);
        return std::vector<uint8_t>(d.begin(), d.end());
    }
    auto d = md5_digest(data);
    return std::vector<uint8_t>(d.begin(), d.end());
}

std::vector<uint8_t> BrewHash::hmac_for_state(const State& state) {
    constexpr size_t block_size = 64;
    std::vector<uint8_t> key = state.key;
    std::vector<uint8_t> inner_input;
    std::vector<uint8_t> outer_input;

    if (key.size() > block_size) {
        key = digest_for_algo(state.algo, key);
    }
    key.resize(block_size, 0);

    std::vector<uint8_t> ipad(block_size, 0x36);
    std::vector<uint8_t> opad(block_size, 0x5c);
    for (size_t i = 0; i < block_size; ++i) {
        ipad[i] ^= key[i];
        opad[i] ^= key[i];
    }

    inner_input.reserve(block_size + state.data.size());
    inner_input.insert(inner_input.end(), ipad.begin(), ipad.end());
    inner_input.insert(inner_input.end(), state.data.begin(), state.data.end());
    auto inner = digest_for_algo(state.algo, inner_input);

    outer_input.reserve(block_size + inner.size());
    outer_input.insert(outer_input.end(), opad.begin(), opad.end());
    outer_input.insert(outer_input.end(), inner.begin(), inner.end());
    return digest_for_algo(state.algo, outer_input);
}

std::vector<uint8_t> BrewHash::compute_digest(const State& state) {
    if (state.hmac) {
        return hmac_for_state(state);
    }
    return digest_for_algo(state.algo, state.data);
}

void BrewHash::append_bytes(State& state, uint32_t ptr, uint32_t count) {
    if (!ptr || ptr >= 0xFF000000 || count == 0) {
        return;
    }
    size_t start = state.data.size();
    state.data.resize(start + count);
    for (uint32_t i = 0; i < count; ++i) {
        state.data[start + i] = memory_.read_value(ptr + i, EndianMemory::Byte);
    }
}

void BrewHash::append_key(State& state, uint32_t ptr, uint32_t count) {
    if (!ptr || ptr >= 0xFF000000 || count == 0) {
        return;
    }
    size_t start = state.key.size();
    state.key.resize(start + count);
    for (uint32_t i = 0; i < count; ++i) {
        state.key[start + i] = memory_.read_value(ptr + i, EndianMemory::Byte);
    }
}

void BrewHash::write_result(State& state, CPU& cpu, bool final_call, uint32_t out_ptr, uint32_t len_ptr) {
    std::vector<uint8_t> digest = compute_digest(state);
    uint32_t requested = (len_ptr && len_ptr < 0xFF000000) ? memory_.read_value(len_ptr) : static_cast<uint32_t>(digest.size());
    uint32_t copy_len = std::min<uint32_t>(requested, static_cast<uint32_t>(digest.size()));

    if (out_ptr && out_ptr < 0xFF000000) {
        for (uint32_t i = 0; i < copy_len; ++i) {
            memory_.write_value(out_ptr + i, digest[i], EndianMemory::Byte);
        }
    }
    if (len_ptr && len_ptr < 0xFF000000) {
        memory_.write_value(len_ptr, static_cast<uint32_t>(digest.size()));
    }
    if (final_call) {
        state.data.clear();
    }
    cpu.set_reg(REG_R0, 0);
}
