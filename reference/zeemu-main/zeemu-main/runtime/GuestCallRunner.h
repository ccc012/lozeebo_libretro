#ifndef ZEEMU_RUNTIME_GUEST_CALL_RUNNER_H_
#define ZEEMU_RUNTIME_GUEST_CALL_RUNNER_H_

#include <cstdint>

class CPU;
class EndianMemory;

class GuestCallRunner {
public:
    GuestCallRunner(CPU& cpu,
                    EndianMemory& memory,
                    uint32_t magic_ret,
                    uint32_t& hle_return_override,
                    const char*& active_guest_call,
                    uint32_t& active_guest_hle_count);

    uint32_t call(const char* label, uint32_t fn, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3);

private:
    CPU& cpu_;
    EndianMemory& memory_;
    uint32_t magic_ret_;
    uint32_t& hle_return_override_;
    const char*& active_guest_call_;
    uint32_t& active_guest_hle_count_;
};

#endif
