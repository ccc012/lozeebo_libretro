#include "brew/BrewCipher.h"
#include "brew/BrewShell.h"
#include "cpu/core/CPU.h"
#include "cpu/memory/EndianMemory.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace {

constexpr uint32_t kSuccess = 0;
constexpr uint32_t kEFailed = 1;
constexpr uint32_t kENoMemory = 2;
constexpr uint32_t kEClassNotSupport = 3;
constexpr uint32_t kEBadState = 13;
constexpr uint32_t kEBadParm = 14;
constexpr uint32_t kEBufferTooSmall = 38;
constexpr uint32_t kCryptInvalidKey = 0x603;
constexpr uint32_t kCryptInvalidPadType = 0x604;
constexpr uint32_t kCryptPadError = 0x605;
constexpr uint32_t kCryptInvalidIv = 0x608;
constexpr uint32_t kCryptInvalidSize = 0x609;

constexpr uint32_t kAeeIidICipher1 = 0x0102cce3;
constexpr uint32_t kAeeIidICipherFactory = 0x0104171d;

constexpr uint32_t kBlockAes128 = 0x0102ccd3;
constexpr uint32_t kBlockAes128Sw = 0x0102ccd4;
constexpr uint32_t kBlockAes128Hw = 0x0102ccd5;
constexpr uint32_t kModeEcb = 0x0102ccd7;
constexpr uint32_t kModeCbc = 0x0102ccd9;

constexpr uint32_t kCipherDirectionEncrypt = 0;
constexpr uint32_t kCipherDirectionDecrypt = 1;
constexpr uint32_t kCipherPaddingNone = 0;
constexpr uint32_t kCipherPaddingZero = 1;
constexpr uint32_t kCipherPaddingLength = 2;
constexpr uint32_t kCipherPaddingRandom = 3;
constexpr uint32_t kCipherPaddingRfc2630 = 4;
constexpr uint32_t kCipherPaddingSequential = 5;

constexpr uint32_t kParamDirection = 0;
constexpr uint32_t kParamKey = 1;
constexpr uint32_t kParamKeySize = 2;
constexpr uint32_t kParamIv = 3;
constexpr uint32_t kParamIvSize = 4;
constexpr uint32_t kParamPadding = 5;
constexpr uint32_t kParamBlockSize = 6;
constexpr uint32_t kParamMode = 8;
constexpr uint32_t kParamBuffered = 9;

constexpr uint8_t kSbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

constexpr uint8_t kInvSbox[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

constexpr uint8_t kRcon[11] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

bool plausible_ptr(uint32_t ptr) {
    return ptr != 0 && ptr < 0xFF000000u;
}

bool is_supported_aes128(uint32_t cls) {
    return cls == kBlockAes128 || cls == kBlockAes128Sw || cls == kBlockAes128Hw;
}

bool is_supported_mode(uint32_t mode) {
    return mode == 0 || mode == kModeEcb || mode == kModeCbc;
}

bool is_supported_padding(uint32_t padding) {
    return padding <= kCipherPaddingSequential;
}

uint8_t xtime(uint8_t x) {
    return static_cast<uint8_t>((x << 1u) ^ ((x & 0x80u) ? 0x1bu : 0u));
}

uint8_t mul(uint8_t x, uint8_t y) {
    uint8_t out = 0;
    while (y) {
        if (y & 1u) {
            out ^= x;
        }
        x = xtime(x);
        y >>= 1u;
    }
    return out;
}

void key_expansion(const std::array<uint8_t, 16>& key, std::array<uint8_t, 176>& round_key) {
    std::copy(key.begin(), key.end(), round_key.begin());
    uint8_t temp[4];
    int bytes = 16;
    int rcon_iter = 1;
    while (bytes < 176) {
        for (int i = 0; i < 4; ++i) {
            temp[i] = round_key[bytes - 4 + i];
        }
        if ((bytes % 16) == 0) {
            const uint8_t t = temp[0];
            temp[0] = static_cast<uint8_t>(kSbox[temp[1]] ^ kRcon[rcon_iter++]);
            temp[1] = kSbox[temp[2]];
            temp[2] = kSbox[temp[3]];
            temp[3] = kSbox[t];
        }
        for (int i = 0; i < 4; ++i) {
            round_key[bytes] = static_cast<uint8_t>(round_key[bytes - 16] ^ temp[i]);
            ++bytes;
        }
    }
}

void add_round_key(uint8_t state[16], const std::array<uint8_t, 176>& round_key, int round) {
    for (int i = 0; i < 16; ++i) {
        state[i] ^= round_key[static_cast<size_t>(round) * 16u + static_cast<size_t>(i)];
    }
}

void sub_bytes(uint8_t state[16]) {
    for (int i = 0; i < 16; ++i) {
        state[i] = kSbox[state[i]];
    }
}

void inv_sub_bytes(uint8_t state[16]) {
    for (int i = 0; i < 16; ++i) {
        state[i] = kInvSbox[state[i]];
    }
}

void shift_rows(uint8_t s[16]) {
    uint8_t t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[3]; s[3] = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = t;
}

void inv_shift_rows(uint8_t s[16]) {
    uint8_t t = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t;
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[3]; s[3] = s[7]; s[7] = s[11]; s[11] = s[15]; s[15] = t;
}

void mix_columns(uint8_t s[16]) {
    for (int c = 0; c < 4; ++c) {
        uint8_t* a = &s[c * 4];
        const uint8_t t = static_cast<uint8_t>(a[0] ^ a[1] ^ a[2] ^ a[3]);
        const uint8_t u = a[0];
        a[0] ^= t ^ xtime(static_cast<uint8_t>(a[0] ^ a[1]));
        a[1] ^= t ^ xtime(static_cast<uint8_t>(a[1] ^ a[2]));
        a[2] ^= t ^ xtime(static_cast<uint8_t>(a[2] ^ a[3]));
        a[3] ^= t ^ xtime(static_cast<uint8_t>(a[3] ^ u));
    }
}

void inv_mix_columns(uint8_t s[16]) {
    for (int c = 0; c < 4; ++c) {
        uint8_t* a = &s[c * 4];
        const uint8_t a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
        a[0] = static_cast<uint8_t>(mul(a0, 14) ^ mul(a1, 11) ^ mul(a2, 13) ^ mul(a3, 9));
        a[1] = static_cast<uint8_t>(mul(a0, 9) ^ mul(a1, 14) ^ mul(a2, 11) ^ mul(a3, 13));
        a[2] = static_cast<uint8_t>(mul(a0, 13) ^ mul(a1, 9) ^ mul(a2, 14) ^ mul(a3, 11));
        a[3] = static_cast<uint8_t>(mul(a0, 11) ^ mul(a1, 13) ^ mul(a2, 9) ^ mul(a3, 14));
    }
}

std::array<uint8_t, 16> aes_encrypt_block(const std::array<uint8_t, 16>& key, const uint8_t* input) {
    std::array<uint8_t, 176> round_key{};
    key_expansion(key, round_key);
    uint8_t state[16];
    std::memcpy(state, input, 16);
    add_round_key(state, round_key, 0);
    for (int round = 1; round < 10; ++round) {
        sub_bytes(state);
        shift_rows(state);
        mix_columns(state);
        add_round_key(state, round_key, round);
    }
    sub_bytes(state);
    shift_rows(state);
    add_round_key(state, round_key, 10);
    std::array<uint8_t, 16> out{};
    std::memcpy(out.data(), state, 16);
    return out;
}

std::array<uint8_t, 16> aes_decrypt_block(const std::array<uint8_t, 16>& key, const uint8_t* input) {
    std::array<uint8_t, 176> round_key{};
    key_expansion(key, round_key);
    uint8_t state[16];
    std::memcpy(state, input, 16);
    add_round_key(state, round_key, 10);
    for (int round = 9; round > 0; --round) {
        inv_shift_rows(state);
        inv_sub_bytes(state);
        add_round_key(state, round_key, round);
        inv_mix_columns(state);
    }
    inv_shift_rows(state);
    inv_sub_bytes(state);
    add_round_key(state, round_key, 0);
    std::array<uint8_t, 16> out{};
    std::memcpy(out.data(), state, 16);
    return out;
}

void cbc_encrypt_blocks(const std::array<uint8_t, 16>& key,
                        std::array<uint8_t, 16>& iv,
                        const uint8_t* input,
                        size_t bytes,
                        std::vector<uint8_t>& out) {
    for (size_t off = 0; off < bytes; off += 16u) {
        uint8_t block[16];
        for (int i = 0; i < 16; ++i) {
            block[i] = static_cast<uint8_t>(input[off + static_cast<size_t>(i)] ^ iv[static_cast<size_t>(i)]);
        }
        auto enc = aes_encrypt_block(key, block);
        out.insert(out.end(), enc.begin(), enc.end());
        iv = enc;
    }
}

void ecb_encrypt_blocks(const std::array<uint8_t, 16>& key,
                        const uint8_t* input,
                        size_t bytes,
                        std::vector<uint8_t>& out) {
    for (size_t off = 0; off < bytes; off += 16u) {
        auto enc = aes_encrypt_block(key, input + off);
        out.insert(out.end(), enc.begin(), enc.end());
    }
}

void cbc_decrypt_blocks(const std::array<uint8_t, 16>& key,
                        std::array<uint8_t, 16>& iv,
                        const uint8_t* input,
                        size_t bytes,
                        std::vector<uint8_t>& out) {
    for (size_t off = 0; off < bytes; off += 16u) {
        auto dec = aes_decrypt_block(key, input + off);
        for (int i = 0; i < 16; ++i) {
            dec[static_cast<size_t>(i)] ^= iv[static_cast<size_t>(i)];
        }
        out.insert(out.end(), dec.begin(), dec.end());
        std::copy(input + off, input + off + 16, iv.begin());
    }
}

void ecb_decrypt_blocks(const std::array<uint8_t, 16>& key,
                        const uint8_t* input,
                        size_t bytes,
                        std::vector<uint8_t>& out) {
    for (size_t off = 0; off < bytes; off += 16u) {
        auto dec = aes_decrypt_block(key, input + off);
        out.insert(out.end(), dec.begin(), dec.end());
    }
}

} // namespace

BrewCipher::BrewCipher(BrewShell& shell, EndianMemory& memory) : shell_(shell), memory_(memory) {
    factory_vtable_ptr_ = shell_.malloc(6 * 4);
    factory_object_ptr_ = shell_.malloc(4);
    memory_.write_value(factory_object_ptr_, factory_vtable_ptr_);
    const char* factory_names[] = {
        "AddRef", "Release", "QueryInterface", "CreateCipher", "CreateCipher2", "QueryCipher"
    };
    for (int i = 0; i < 6; ++i) {
        memory_.write_value(factory_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("ICipherFactory_") + factory_names[i], this));
    }

    cipher_vtable_ptr_ = shell_.malloc(8 * 4);
    const char* cipher_names[] = {
        "AddRef", "Release", "QueryInterface", "GetParam", "SetParam", "Process", "ProcessLast", "ProcessBlocks"
    };
    for (int i = 0; i < 8; ++i) {
        memory_.write_value(cipher_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("ICipher1_") + cipher_names[i], this));
    }
}

std::vector<uint8_t> BrewCipher::read_bytes(addr_t ptr, uint32_t size) const {
    std::vector<uint8_t> out;
    if (!plausible_ptr(ptr) || size == 0) {
        return out;
    }
    out.resize(size);
    for (uint32_t i = 0; i < size; ++i) {
        out[i] = static_cast<uint8_t>(memory_.read_value(ptr + i, EndianMemory::Byte));
    }
    return out;
}

void BrewCipher::write_bytes(addr_t ptr, const std::vector<uint8_t>& bytes) const {
    if (!plausible_ptr(ptr)) {
        return;
    }
    for (size_t i = 0; i < bytes.size(); ++i) {
        memory_.write_value(ptr + static_cast<uint32_t>(i), bytes[i], EndianMemory::Byte);
    }
}

bool BrewCipher::write_param_u32(addr_t param_ptr, addr_t param_len_ptr, uint32_t value) const {
    if (!plausible_ptr(param_len_ptr)) {
        return false;
    }
    const uint32_t capacity = memory_.read_value(param_len_ptr);
    memory_.write_value(param_len_ptr, 4u);
    if (param_ptr == 0) {
        return true;
    }
    if (!plausible_ptr(param_ptr) || capacity < 4) {
        return false;
    }
    memory_.write_value(param_ptr, value);
    return true;
}

int BrewCipher::query_cipher(uint32_t cipher_id, uint32_t mode_id, uint32_t padding, uint32_t key_size) const {
    if (!is_supported_aes128(cipher_id) || !is_supported_mode(mode_id) || !is_supported_padding(padding)) {
        return kEClassNotSupport;
    }
    if (key_size != 0 && key_size != 16) {
        return kEClassNotSupport;
    }
    return kSuccess;
}

addr_t BrewCipher::create_cipher(uint32_t cipher_id, uint32_t direction, uint32_t mode_id, uint32_t padding) {
    if (direction != kCipherDirectionEncrypt && direction != kCipherDirectionDecrypt) {
        return 0;
    }
    if (query_cipher(cipher_id, mode_id, padding, 0) != kSuccess) {
        return 0;
    }
    addr_t object = shell_.malloc(4);
    if (!object) {
        return 0;
    }
    memory_.write_value(object, cipher_vtable_ptr_);
    State state;
    state.object_ptr = object;
    state.cipher_id = cipher_id;
    state.mode_id = (mode_id == 0) ? kModeEcb : mode_id;
    state.direction = direction;
    state.padding = padding;
    states_[object] = state;
    return object;
}

int BrewCipher::set_key(State& state, addr_t ptr, uint32_t size) {
    if (!plausible_ptr(ptr) || size != 16) {
        return kCryptInvalidKey;
    }
    const auto key = read_bytes(ptr, size);
    if (key.size() != 16) {
        return kCryptInvalidKey;
    }
    std::copy(key.begin(), key.end(), state.key.begin());
    state.has_key = true;
    state.buffered.clear();
    return kSuccess;
}

int BrewCipher::set_iv(State& state, addr_t ptr, uint32_t size) {
    if (!plausible_ptr(ptr) || size != 16) {
        return kCryptInvalidIv;
    }
    const auto iv = read_bytes(ptr, size);
    if (iv.size() != 16) {
        return kCryptInvalidIv;
    }
    std::copy(iv.begin(), iv.end(), state.iv.begin());
    state.buffered.clear();
    return kSuccess;
}

int BrewCipher::get_param(State& state, uint32_t id, addr_t param_ptr, addr_t param_len_ptr) {
    switch (id) {
    case kParamDirection:
        return write_param_u32(param_ptr, param_len_ptr, state.direction) ? kSuccess : kEBadParm;
    case kParamKeySize:
        return write_param_u32(param_ptr, param_len_ptr, 16) ? kSuccess : kEBadParm;
    case kParamIvSize:
        return write_param_u32(param_ptr, param_len_ptr, state.mode_id == kModeCbc ? 16 : 0) ? kSuccess : kEBadParm;
    case kParamPadding:
        return write_param_u32(param_ptr, param_len_ptr, state.padding) ? kSuccess : kEBadParm;
    case kParamBlockSize:
        return write_param_u32(param_ptr, param_len_ptr, 16) ? kSuccess : kEBadParm;
    case kParamMode:
        return write_param_u32(param_ptr, param_len_ptr, state.mode_id) ? kSuccess : kEBadParm;
    case kParamBuffered:
        return write_param_u32(param_ptr, param_len_ptr, static_cast<uint32_t>(state.buffered.size())) ?
            (state.buffered.empty() ? kEFailed : kSuccess) : kEBadParm;
    case kParamIv:
        if (!plausible_ptr(param_len_ptr)) {
            return kEBadParm;
        }
        {
            const uint32_t capacity = memory_.read_value(param_len_ptr);
            memory_.write_value(param_len_ptr, 16u);
            if (param_ptr == 0) {
                return kSuccess;
            }
            if (!plausible_ptr(param_ptr) || capacity < 16) {
                return kEBufferTooSmall;
            }
            std::vector<uint8_t> iv(state.iv.begin(), state.iv.end());
            write_bytes(param_ptr, iv);
            return kSuccess;
        }
    default:
        return kEBadParm;
    }
}

int BrewCipher::set_param(State& state, uint32_t id, addr_t param_ptr, uint32_t param_len) {
    switch (id) {
    case kParamDirection:
        if (!plausible_ptr(param_ptr) || param_len < 4) {
            return kEBadParm;
        }
        {
            const uint32_t direction = memory_.read_value(param_ptr);
            if (direction != kCipherDirectionEncrypt && direction != kCipherDirectionDecrypt) {
                return kEBadParm;
            }
            state.direction = direction;
            state.buffered.clear();
            return kSuccess;
        }
    case kParamKey:
        return set_key(state, param_ptr, param_len);
    case kParamIv:
        return set_iv(state, param_ptr, param_len);
    case kParamPadding:
        if (!plausible_ptr(param_ptr) || param_len < 4) {
            return kEBadParm;
        }
        {
            const uint32_t padding = memory_.read_value(param_ptr);
            if (!is_supported_padding(padding)) {
                return kCryptInvalidPadType;
            }
            state.padding = padding;
            state.buffered.clear();
            return kSuccess;
        }
    case kParamMode:
        if (!plausible_ptr(param_ptr) || param_len < 4) {
            return kEBadParm;
        }
        {
            const uint32_t mode = memory_.read_value(param_ptr);
            if (!is_supported_mode(mode)) {
                return kEClassNotSupport;
            }
            state.mode_id = (mode == 0) ? kModeEcb : mode;
            state.buffered.clear();
            return kSuccess;
        }
    default:
        return kEBadParm;
    }
}

int BrewCipher::process(State& state, addr_t in_ptr, uint32_t in_size, addr_t out_ptr, addr_t out_size_ptr) {
    if (!state.has_key) {
        return kEBadState;
    }
    if ((!plausible_ptr(in_ptr) && in_size != 0) || !plausible_ptr(out_ptr) || !plausible_ptr(out_size_ptr)) {
        return kEBadParm;
    }

    const uint32_t capacity = memory_.read_value(out_size_ptr);
    auto input = read_bytes(in_ptr, in_size);
    if (input.size() != in_size) {
        return kEBadParm;
    }
    state.buffered.insert(state.buffered.end(), input.begin(), input.end());

    size_t process_bytes = (state.buffered.size() / 16u) * 16u;
    if (state.direction == kCipherDirectionDecrypt &&
        (state.padding == kCipherPaddingLength ||
         state.padding == kCipherPaddingRandom ||
         state.padding == kCipherPaddingRfc2630 ||
         state.padding == kCipherPaddingSequential) &&
        process_bytes >= 16u) {
        process_bytes -= 16u;
    }
    if (process_bytes == 0) {
        memory_.write_value(out_size_ptr, 0u);
        return kSuccess;
    }
    if (capacity < process_bytes) {
        return kEBufferTooSmall;
    }

    std::vector<uint8_t> out;
    if (state.direction == kCipherDirectionEncrypt) {
        if (state.mode_id == kModeCbc) {
            cbc_encrypt_blocks(state.key, state.iv, state.buffered.data(), process_bytes, out);
        } else {
            ecb_encrypt_blocks(state.key, state.buffered.data(), process_bytes, out);
        }
    } else {
        if (state.mode_id == kModeCbc) {
            cbc_decrypt_blocks(state.key, state.iv, state.buffered.data(), process_bytes, out);
        } else {
            ecb_decrypt_blocks(state.key, state.buffered.data(), process_bytes, out);
        }
    }
    write_bytes(out_ptr, out);
    memory_.write_value(out_size_ptr, static_cast<uint32_t>(out.size()));
    state.buffered.erase(state.buffered.begin(), state.buffered.begin() + static_cast<std::ptrdiff_t>(process_bytes));
    return kSuccess;
}

int BrewCipher::process_last(State& state, addr_t out_ptr, addr_t out_size_ptr) {
    if (!state.has_key) {
        return kEBadState;
    }
    if (!plausible_ptr(out_ptr) || !plausible_ptr(out_size_ptr)) {
        return kEBadParm;
    }
    std::vector<uint8_t> block = state.buffered;
    state.buffered.clear();

    if (state.direction == kCipherDirectionEncrypt) {
        if (state.padding == kCipherPaddingNone && (block.size() % 16u) != 0) {
            return kCryptInvalidSize;
        }
        const size_t rem = block.size() % 16u;
        if (state.padding == kCipherPaddingZero && rem != 0) {
            block.resize(block.size() + (16u - rem), 0);
        } else if (state.padding == kCipherPaddingLength ||
                   state.padding == kCipherPaddingRandom ||
                   state.padding == kCipherPaddingRfc2630 ||
                   state.padding == kCipherPaddingSequential) {
            const uint8_t pad = static_cast<uint8_t>(16u - rem);
            if (state.padding == kCipherPaddingRfc2630) {
                block.insert(block.end(), pad, pad);
            } else if (state.padding == kCipherPaddingSequential) {
                for (uint8_t i = 1; i < pad; ++i) {
                    block.push_back(i);
                }
                block.push_back(pad);
            } else {
                block.insert(block.end(), pad - 1u, 0);
                block.push_back(pad);
            }
        }
    } else if ((block.size() % 16u) != 0) {
        return kCryptInvalidSize;
    }

    const uint32_t capacity = memory_.read_value(out_size_ptr);
    std::vector<uint8_t> out;
    if (!block.empty()) {
        if (state.direction == kCipherDirectionEncrypt) {
            if (state.mode_id == kModeCbc) {
                cbc_encrypt_blocks(state.key, state.iv, block.data(), block.size(), out);
            } else {
                ecb_encrypt_blocks(state.key, block.data(), block.size(), out);
            }
        } else {
            if (state.mode_id == kModeCbc) {
                cbc_decrypt_blocks(state.key, state.iv, block.data(), block.size(), out);
            } else {
                ecb_decrypt_blocks(state.key, block.data(), block.size(), out);
            }
            if (state.padding == kCipherPaddingLength ||
                state.padding == kCipherPaddingRandom ||
                state.padding == kCipherPaddingRfc2630 ||
                state.padding == kCipherPaddingSequential) {
                if (out.empty() || out.back() == 0 || out.back() > 16 || out.back() > out.size()) {
                    return kCryptPadError;
                }
                const uint8_t pad = out.back();
                if (state.padding == kCipherPaddingRfc2630) {
                    for (size_t i = out.size() - pad; i < out.size(); ++i) {
                        if (out[i] != pad) {
                            return kCryptPadError;
                        }
                    }
                }
                out.resize(out.size() - pad);
            }
        }
    }
    if (capacity < out.size()) {
        return kEBufferTooSmall;
    }
    write_bytes(out_ptr, out);
    memory_.write_value(out_size_ptr, static_cast<uint32_t>(out.size()));
    return kSuccess;
}

void BrewCipher::handle_hook(const std::string& name, CPU& cpu) {
    const uint32_t r0 = cpu.get_reg(REG_R0);
    const uint32_t r1 = cpu.get_reg(REG_R1);
    const uint32_t r2 = cpu.get_reg(REG_R2);
    const uint32_t r3 = cpu.get_reg(REG_R3);
    const uint32_t r5 = cpu.get_reg(REG_R5);
    const uint32_t r6 = cpu.get_reg(REG_R6);
    const uint32_t r7 = cpu.get_reg(REG_R7);
    const uint32_t sp = cpu.get_reg(REG_SP);
    const bool is_thunk = (r1 >= 0xFF000000u && r0 < 512);
    auto stack_arg = [&](uint32_t index) -> uint32_t {
        if (!plausible_ptr(sp)) {
            return 0;
        }
        return memory_.read_value(sp + index * 4);
    };
    auto write_out = [&](uint32_t pp, uint32_t value) {
        if (plausible_ptr(pp)) {
            memory_.write_value(pp, value);
        }
    };

    if (name == "ICipherFactory_AddRef" || name == "ICipher1_AddRef") {
        cpu.set_reg(REG_R0, 1);
        return;
    }
    if (name == "ICipherFactory_Release" || name == "ICipher1_Release") {
        cpu.set_reg(REG_R0, 0);
        return;
    }
    if (name == "ICipherFactory_QueryInterface") {
        const uint32_t cls = is_thunk ? r5 : r1;
        const uint32_t pp = is_thunk ? r6 : r2;
        if (cls == 0 || cls == kAeeIidICipherFactory) {
            write_out(pp, factory_object_ptr_);
            cpu.set_reg(REG_R0, kSuccess);
        } else {
            write_out(pp, 0);
            cpu.set_reg(REG_R0, kEClassNotSupport);
        }
        return;
    }
    if (name == "ICipherFactory_CreateCipher") {
        const uint32_t cipher = is_thunk ? r5 : r1;
        const uint32_t direction = is_thunk ? r6 : r2;
        const uint32_t mode = is_thunk ? r7 : r3;
        const uint32_t padding = stack_arg(0);
        const uint32_t pp_cipher = stack_arg(1);
        const addr_t object = create_cipher(cipher, direction, mode, padding);
        write_out(pp_cipher, object);
        printf("  [ICipherFactory_CreateCipher] cipher=0x%08x direction=%u mode=0x%08x padding=%u pp=0x%08x -> %s\n",
               cipher, direction, mode, padding, pp_cipher, object ? "SUCCESS" : "ECLASSNOTSUPPORT");
        cpu.set_reg(REG_R0, object ? kSuccess : kEClassNotSupport);
        return;
    }
    if (name == "ICipherFactory_CreateCipher2") {
        const uint32_t p_info = is_thunk ? r5 : r1;
        const uint32_t info_size = is_thunk ? r6 : r2;
        const uint32_t pp_cipher = is_thunk ? r7 : r3;
        if (!plausible_ptr(p_info) || info_size < 32) {
            write_out(pp_cipher, 0);
            cpu.set_reg(REG_R0, kEBadParm);
            return;
        }
        const uint32_t cipher = memory_.read_value(p_info + 0);
        const uint32_t mode = memory_.read_value(p_info + 4);
        const uint32_t padding = memory_.read_value(p_info + 8);
        const uint32_t direction = memory_.read_value(p_info + 12);
        const uint32_t p_key = memory_.read_value(p_info + 16);
        const uint32_t c_key = memory_.read_value(p_info + 20);
        const uint32_t p_iv = memory_.read_value(p_info + 24);
        const uint32_t c_iv = memory_.read_value(p_info + 28);
        const addr_t object = create_cipher(cipher, direction, mode, padding);
        write_out(pp_cipher, object);
        if (!object) {
            cpu.set_reg(REG_R0, kEClassNotSupport);
            return;
        }
        State& state = states_[object];
        int result = kSuccess;
        if (p_key && c_key) {
            result = set_key(state, p_key, c_key);
        }
        if (result == kSuccess && p_iv && c_iv) {
            result = set_iv(state, p_iv, c_iv);
        }
        if (result != kSuccess) {
            write_out(pp_cipher, 0);
        }
        printf("  [ICipherFactory_CreateCipher2] cipher=0x%08x direction=%u mode=0x%08x padding=%u key=%u iv=%u -> %d\n",
               cipher, direction, mode, padding, c_key, c_iv, result);
        cpu.set_reg(REG_R0, result);
        return;
    }
    if (name == "ICipherFactory_QueryCipher") {
        const uint32_t cipher = is_thunk ? r5 : r1;
        const uint32_t mode = is_thunk ? r6 : r2;
        const uint32_t padding = is_thunk ? r7 : r3;
        const uint32_t key_size = stack_arg(0);
        const int result = query_cipher(cipher, mode, padding, key_size);
        printf("  [ICipherFactory_QueryCipher] cipher=0x%08x mode=0x%08x padding=%u keysize=%u -> %d\n",
               cipher, mode, padding, key_size, result);
        cpu.set_reg(REG_R0, result);
        return;
    }

    auto it = states_.find(is_thunk ? r5 : r0);
    if (it == states_.end()) {
        cpu.set_reg(REG_R0, kEBadParm);
        return;
    }
    State& state = it->second;
    if (name == "ICipher1_QueryInterface") {
        const uint32_t cls = is_thunk ? r6 : r1;
        const uint32_t pp = is_thunk ? r7 : r2;
        if (cls == 0 || cls == kAeeIidICipher1) {
            write_out(pp, state.object_ptr);
            cpu.set_reg(REG_R0, kSuccess);
        } else {
            write_out(pp, 0);
            cpu.set_reg(REG_R0, kEClassNotSupport);
        }
    } else if (name == "ICipher1_GetParam") {
        const uint32_t id = is_thunk ? r6 : r1;
        const uint32_t param = is_thunk ? r7 : r2;
        const uint32_t param_len = is_thunk ? stack_arg(0) : r3;
        cpu.set_reg(REG_R0, get_param(state, id, param, param_len));
    } else if (name == "ICipher1_SetParam") {
        const uint32_t id = is_thunk ? r6 : r1;
        const uint32_t param = is_thunk ? r7 : r2;
        const uint32_t param_len = is_thunk ? stack_arg(0) : r3;
        cpu.set_reg(REG_R0, set_param(state, id, param, param_len));
    } else if (name == "ICipher1_Process") {
        const uint32_t in_ptr = is_thunk ? r6 : r1;
        const uint32_t in_size = is_thunk ? r7 : r2;
        const uint32_t out_ptr = is_thunk ? stack_arg(0) : r3;
        const uint32_t out_size_ptr = is_thunk ? stack_arg(1) : stack_arg(0);
        cpu.set_reg(REG_R0, process(state, in_ptr, in_size, out_ptr, out_size_ptr));
    } else if (name == "ICipher1_ProcessLast") {
        const uint32_t out_ptr = is_thunk ? r6 : r1;
        const uint32_t out_size_ptr = is_thunk ? r7 : r2;
        cpu.set_reg(REG_R0, process_last(state, out_ptr, out_size_ptr));
    } else if (name == "ICipher1_ProcessBlocks") {
        const uint32_t in_ptr = is_thunk ? r6 : r1;
        const uint32_t in_size = is_thunk ? r7 : r2;
        const uint32_t out_ptr = is_thunk ? stack_arg(0) : r3;
        if (!state.has_key || (in_size % 16u) != 0 || !plausible_ptr(in_ptr) || !plausible_ptr(out_ptr)) {
            cpu.set_reg(REG_R0, state.has_key ? kEBadParm : kEBadState);
            return;
        }
        std::vector<uint8_t> input = read_bytes(in_ptr, in_size);
        std::vector<uint8_t> out;
        if (state.direction == kCipherDirectionEncrypt) {
            ecb_encrypt_blocks(state.key, input.data(), input.size(), out);
        } else {
            ecb_decrypt_blocks(state.key, input.data(), input.size(), out);
        }
        write_bytes(out_ptr, out);
        cpu.set_reg(REG_R0, kSuccess);
    } else {
        cpu.set_reg(REG_R0, kEBadParm);
    }
}
