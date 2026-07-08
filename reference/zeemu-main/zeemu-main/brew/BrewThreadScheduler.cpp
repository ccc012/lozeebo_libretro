#include "brew/BrewThreadScheduler.h"

#include "brew/BrewShell.h"
#include "cpu/core/CPU.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>

namespace {

bool arm_condition_passed(uint32_t cpsr, uint32_t cond) {
    const bool n = (cpsr & 0x80000000u) != 0;
    const bool z = (cpsr & 0x40000000u) != 0;
    const bool c = (cpsr & 0x20000000u) != 0;
    const bool v = (cpsr & 0x10000000u) != 0;
    switch (cond) {
    case 0x0: return z;
    case 0x1: return !z;
    case 0x2: return c;
    case 0x3: return !c;
    case 0x4: return n;
    case 0x5: return !n;
    case 0x6: return v;
    case 0x7: return !v;
    case 0x8: return c && !z;
    case 0x9: return !c || z;
    case 0xa: return n == v;
    case 0xb: return n != v;
    case 0xc: return !z && n == v;
    case 0xd: return z || n != v;
    case 0xe: return true;
    default: return false;
    }
}

} // namespace

BrewThreadScheduler::BrewThreadScheduler(CPU& cpu,
                                         BrewShell& shell,
                                         uint32_t magic_ret,
                                         uint32_t& hle_return_override,
                                         const char*& active_guest_call,
                                         uint32_t& active_guest_hle_count)
    : cpu_(cpu),
      shell_(shell),
      magic_ret_(magic_ret),
      hle_return_override_(hle_return_override),
      active_guest_call_(active_guest_call),
      active_guest_hle_count_(active_guest_hle_count) {}

std::array<uint32_t, 17> BrewThreadScheduler::save_cpu_context() const {
    std::array<uint32_t, 17> regs{};
    for (int i = 0; i <= 16; ++i) {
        regs[static_cast<size_t>(i)] = cpu_.get_reg(static_cast<CPUReg>(i));
    }
    if (!cpu_.pipeline.reload && cpu_.pipeline.decode.address != 0) {
        regs[static_cast<size_t>(REG_PC)] = cpu_.pipeline.decode.address;
    }
    return regs;
}

void BrewThreadScheduler::restore_cpu_context(const std::array<uint32_t, 17>& regs) {
    for (int i = 0; i <= 16; ++i) {
        cpu_.set_reg(static_cast<CPUReg>(i), regs[static_cast<size_t>(i)]);
    }
}

void BrewThreadScheduler::collect_pending() {
    auto threads = shell_.pop_pending_threads();
    for (const auto& t : threads) {
        Runtime rt;
        rt.object = t.object;
        cpu_.reset(t.entry);
        cpu_.set_reg(REG_R0, t.object);
        cpu_.set_reg(REG_R1, t.pUser);
        cpu_.set_reg(REG_R2, 0);
        cpu_.set_reg(REG_R3, 0);
        if (t.stackSize != 0) {
            addr_t stack = shell_.malloc(t.stackSize + 16);
            cpu_.set_reg(REG_SP, (stack + t.stackSize) & ~0x7u);
        }
        cpu_.set_reg(REG_LR, magic_ret_);
        rt.regs = save_cpu_context();
        rt.label = t.label;
        std::cout << "Queued " << rt.label
                  << " object=0x" << std::hex << t.object
                  << " entry=0x" << t.entry
                  << " pUser=0x" << t.pUser
                  << " stack=0x" << t.stackSize << std::dec << std::endl;
        active_threads_.push_back(std::move(rt));
    }
}

void BrewThreadScheduler::collect_resumes() {
    auto resumes = shell_.pop_pending_thread_resumes();
    for (uint32_t object : resumes) {
        for (auto& t : active_threads_) {
            if (t.object == object) {
                t.ready = true;
            }
        }
    }
}

bool BrewThreadScheduler::run_slices(uint32_t max_steps_per_thread) {
    // Thread_Suspend and AEEHelper_sleep use shell-level yield flags because
    // they are raised from generic HLE hooks. If a guest calls either helper
    // while the primordial app/event callback is running, there is no active
    // Runtime here to suspend or slice-yield. Do not let that stale request
    // leak into the next cooperative thread and suspend the wrong context.
    (void)shell_.consume_thread_yield_request();
    (void)shell_.consume_thread_slice_yield_request();

    collect_pending();
    collect_resumes();
    const bool trace_threads = std::getenv("ZEEMU_TRACE_THREADS") != nullptr;
    const bool trace_branch_high = std::getenv("ZEEMU_TRACE_BRANCH_HIGH") != nullptr;
    bool did_work = false;
    for (auto& t : active_threads_) {
        if (!t.alive || !t.ready) {
            continue;
        }
        did_work = true;
        restore_cpu_context(t.regs);
        active_guest_call_ = t.label.c_str();
        active_guest_hle_count_ = 0;
        uint32_t steps = 0;
        bool suspended = false;
        bool yielded_slice = false;
        bool hit_zero_instruction = false;
        std::array<uint32_t, 16> recent_pc{};
        std::array<uint32_t, 16> recent_op{};
        uint32_t recent_pos = 0;
        for (; steps < max_steps_per_thread && !cpu_.is_stopped() && cpu_.get_reg(REG_PC) != magic_ret_; ++steps) {
            uint32_t pc = cpu_.pipeline.execute.address;
            uint32_t op = cpu_.pipeline.execute.instruction;
            recent_pc[recent_pos & 15u] = pc;
            recent_op[recent_pos & 15u] = op;
            ++recent_pos;
            if (op == 0 && pc >= 0x80000000u && pc < 0xFF000000u) {
                hit_zero_instruction = true;
                break;
            }
            if ((op & 0x0FFFFFF0u) == 0x012FFF30u) {
                uint32_t rm = op & 0x0Fu;
                uint32_t target = cpu_.get_reg(static_cast<CPUReg>(rm));
                const bool link = (op & 0x20u) != 0;
                if (link && target >= 0xFF000000u &&
                    arm_condition_passed(cpu_.get_reg(REG_CPSR), op >> 28)) {
                    hle_return_override_ = pc + 4;
                }
            }
            cpu_.step_once();
            if (shell_.consume_thread_yield_request()) {
                suspended = true;
                break;
            }
            if (shell_.consume_thread_slice_yield_request()) {
                yielded_slice = true;
                break;
            }
        }
        t.steps += steps;
        if (suspended) {
            t.regs = save_cpu_context();
            t.ready = false;
        } else if (yielded_slice) {
            t.regs = save_cpu_context();
        } else if (hit_zero_instruction) {
            t.alive = false;
            std::cout << t.label << " hit zero instruction at high PC=0x" << std::hex << cpu_.get_reg(REG_PC)
                      << " after " << std::dec << t.steps
                      << " steps LR=0x" << std::hex << cpu_.get_reg(REG_LR)
                      << " R0=0x" << cpu_.get_reg(REG_R0)
                      << " R1=0x" << cpu_.get_reg(REG_R1)
                      << " R2=0x" << cpu_.get_reg(REG_R2)
                      << " R3=0x" << cpu_.get_reg(REG_R3)
                      << std::dec << std::endl;
            if (trace_branch_high) {
                const uint32_t r0 = cpu_.get_reg(REG_R0);
                const uint32_t r2 = cpu_.get_reg(REG_R2);
                const uint32_t v0 = r0 < 0xFF000000u ? cpu_.mem().read_value(r0) : 0;
                const uint32_t v2 = r2 < 0xFF000000u ? cpu_.mem().read_value(r2) : 0;
                std::cout << "  high-pc object dump:"
                          << " [r0]=0x" << std::hex << v0
                          << " [r0+8]=0x" << (r0 < 0xFF000000u ? cpu_.mem().read_value(r0 + 8u) : 0)
                          << " [r2]=0x" << v2
                          << " [r2+8]=0x" << (r2 < 0xFF000000u ? cpu_.mem().read_value(r2 + 8u) : 0)
                          << " [v2+8]=0x" << (v2 < 0xFF000000u ? cpu_.mem().read_value(v2 + 8u) : 0)
                          << std::dec << std::endl;
            }
            const uint32_t recent_count = std::min<uint32_t>(recent_pos, 16u);
            for (uint32_t i = recent_count; i > 0; --i) {
                const uint32_t idx = (recent_pos - i) & 15u;
                std::cout << "  recent PC=0x" << std::hex << recent_pc[idx]
                          << " op=0x" << recent_op[idx]
                          << std::dec << std::endl;
            }
        } else if (cpu_.get_reg(REG_PC) == magic_ret_ || cpu_.is_stopped()) {
            t.alive = false;
            std::cout << t.label << " exited after " << std::dec << t.steps
                      << " steps R0=0x" << std::hex << cpu_.get_reg(REG_R0) << std::dec << std::endl;
        } else {
            t.regs = save_cpu_context();
        }
        if (trace_threads) {
            const uint32_t pc = cpu_.get_reg(REG_PC);
            uint32_t op = 0;
            if (pc < 0xFF000000u) {
                op = cpu_.mem().read_value(pc);
            }
            std::cout << "thread slice label=" << t.label
                      << " object=0x" << std::hex << t.object
                      << " steps=0x" << steps
                      << " total=0x" << t.steps
                      << " pc=0x" << pc
                      << " op=0x" << op
                      << " lr=0x" << cpu_.get_reg(REG_LR)
                      << " r0=0x" << cpu_.get_reg(REG_R0)
                      << " r1=0x" << cpu_.get_reg(REG_R1)
                      << " r2=0x" << cpu_.get_reg(REG_R2)
                      << " r3=0x" << cpu_.get_reg(REG_R3)
                      << " r12=0x" << cpu_.get_reg(REG_R12)
                      << " sp=0x" << cpu_.get_reg(REG_SP)
                      << " ready=" << std::dec << (t.ready ? 1 : 0)
                      << " alive=" << (t.alive ? 1 : 0)
                      << std::endl;
        }
        active_guest_call_ = nullptr;
        collect_pending();
    }
    active_threads_.erase(
        std::remove_if(active_threads_.begin(), active_threads_.end(),
                       [](const Runtime& t) { return !t.alive; }),
        active_threads_.end());
    return did_work;
}
