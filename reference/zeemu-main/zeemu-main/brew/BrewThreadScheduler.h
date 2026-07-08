#ifndef ZEEMU_BREW_THREAD_SCHEDULER_H_
#define ZEEMU_BREW_THREAD_SCHEDULER_H_

#include "cpu/cpu.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class BrewShell;
class CPU;

class BrewThreadScheduler {
public:
    BrewThreadScheduler(CPU& cpu,
                        BrewShell& shell,
                        uint32_t magic_ret,
                        uint32_t& hle_return_override,
                        const char*& active_guest_call,
                        uint32_t& active_guest_hle_count);

    void collect_pending();
    bool run_slices(uint32_t max_steps_per_thread);

private:
    struct Runtime {
        std::array<uint32_t, 17> regs{};
        uint32_t object = 0;
        std::string label;
        bool alive = true;
        bool ready = true;
        uint64_t steps = 0;
    };

    std::array<uint32_t, 17> save_cpu_context() const;
    void restore_cpu_context(const std::array<uint32_t, 17>& regs);
    void collect_resumes();

    CPU& cpu_;
    BrewShell& shell_;
    uint32_t magic_ret_;
    uint32_t& hle_return_override_;
    const char*& active_guest_call_;
    uint32_t& active_guest_hle_count_;
    std::vector<Runtime> active_threads_;
};

#endif
