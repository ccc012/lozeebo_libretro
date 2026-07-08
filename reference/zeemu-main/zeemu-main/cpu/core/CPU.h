#ifndef ZEEMU_CPU_CORE_H
#define ZEEMU_CPU_CORE_H

#include <functional>
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"
#include "cpu/arm7tdmi/arm7tdmi.hpp"

class CPU : public higan::ARM7TDMI {
public:
    explicit CPU(EndianMemory& mem);

    void reset(uint32_t entry_point);
    void run(uint32_t entry_point);
    void step_loop();   // run from current PC/regs until stopped_=true (no reset)
    void stop();
    void step_once();

    [[nodiscard]] uint32_t get_reg(CPUReg reg) const;
    void     set_reg(CPUReg reg, uint32_t value);

    void set_hle_handler(std::function<uint32_t(uint32_t, uint32_t, uint32_t, uint32_t)> handler) {
        hook_handler_ = std::move(handler);
    }

    [[nodiscard]] bool         is_stopped() const { return stopped_; }
    [[nodiscard]] EndianMemory& mem() const { return mem_; }

    // ROPI mirror translation: link-time addresses [0, mirror_size) → [base, base+mirror_size)
    void set_ropi_params(uint32_t base, uint32_t mirror_size) {
        ropi_base_ = base;
        ropi_mirror_size_ = mirror_size;
    }
    [[nodiscard]] uint32_t translate_ropi(uint32_t addr) const {
        if (ropi_mirror_size_ > 0 && addr < ropi_mirror_size_ && addr != ropi_base_) {
            return ropi_base_ + addr;
        }
        return addr;
    }

    // Disassemble instruction at addr (thumb flag)
    std::string disassemble(uint32_t addr, bool thumb);

    // higan::ARM7TDMI pure virtuals
    auto step(higan::uint) -> void override {}
    auto sleep()                  -> void override {}
    auto get(higan::uint mode, higan::uint32 address) -> higan::uint32 override;
    auto set(higan::uint mode, higan::uint32 address, higan::uint32 word) -> void override;
    auto softwareInterrupt(uint32_t imm, uint32_t ret_addr) -> void override;
    higan::uint32 translate_address(higan::uint32 addr) override { return translate_ropi(addr); }

private:
    EndianMemory&                 mem_;
    std::function<uint32_t(uint32_t, uint32_t, uint32_t, uint32_t)> hook_handler_;
    bool                          stopped_ = false;
    uint32_t                      ropi_base_ = 0;
    uint32_t                      ropi_mirror_size_ = 0;
};

#endif
