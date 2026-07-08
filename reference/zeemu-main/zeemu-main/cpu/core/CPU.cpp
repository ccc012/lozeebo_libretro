#include "cpu/core/CPU.h"
#include <cstdlib>
#include <cstdio>

CPU::CPU(EndianMemory& mem) : mem_(mem) {
    // higan::ARM7TDMI() already called armInitialize()/thumbInitialize()
}

void CPU::reset(uint32_t entry_point) {
    const bool thumb = (entry_point & 1u) != 0;
    const uint32_t pc = entry_point & ~1u;
    printf("[CPU] Resetting at entry point 0x%08X%s\n",
           (unsigned)pc,
           thumb ? " (Thumb)" : "");
    power();                          // higan reset: zero regs, SVC mode, reload pipeline
    processor.r13 = 0x1FF00000u;      // SP
    processor.cpsr = 0x00000010u | (thumb ? 0x20u : 0u); // USR mode, optional Thumb state
    processor.r15 = pc;               // triggers pipeline.reload = true via modify callback
    stopped_ = false;
}

void CPU::run(uint32_t entry_point) {
    reset(entry_point);
    while (!stopped_) {
        instruction();
    }
}

void CPU::step_loop() {
    stopped_ = false;
    uint64_t n = 0;
    while (!stopped_) {
        instruction();
        if ((++n & 0x1FFFFF) == 0) {
            fprintf(stderr, "[CPU] n=%llu PC=0x%08X LR=0x%08X SP=0x%08X\n",
                    (unsigned long long)n,
                    uint32_t(processor.r15.data),
                    uint32_t(processor.r14.data),
                    uint32_t(processor.r13.data));
            fflush(stderr);
        }
    }
}

void CPU::stop() {
    stopped_ = true;
}

void CPU::step_once() {
    if (!stopped_) instruction();
}

// ---------------------------------------------------------------------------
// Register access — direct processor member access (USR mode)
// ---------------------------------------------------------------------------
uint32_t CPU::get_reg(CPUReg reg) const {
    switch (reg) {
    case REG_R0:   return uint32_t(processor.r0.data);
    case REG_R1:   return uint32_t(processor.r1.data);
    case REG_R2:   return uint32_t(processor.r2.data);
    case REG_R3:   return uint32_t(processor.r3.data);
    case REG_R4:   return uint32_t(processor.r4.data);
    case REG_R5:   return uint32_t(processor.r5.data);
    case REG_R6:   return uint32_t(processor.r6.data);
    case REG_R7:   return uint32_t(processor.r7.data);
    case REG_R8:   return uint32_t(processor.r8.data);
    case REG_R9:   return uint32_t(processor.r9.data);
    case REG_R10:  return uint32_t(processor.r10.data);
    case REG_R11:  return uint32_t(processor.r11.data);
    case REG_R12:  return uint32_t(processor.r12.data);
    case REG_SP:   return uint32_t(processor.r13.data);
    case REG_LR:   return uint32_t(processor.r14.data);
    case REG_PC:   return uint32_t(processor.r15.data);
    case REG_CPSR: return uint32_t(processor.cpsr);
    default:       return 0;
    }
}

void CPU::set_reg(CPUReg reg, uint32_t value) {
    switch (reg) {
    case REG_R0:   processor.r0  = value; break;
    case REG_R1:   processor.r1  = value; break;
    case REG_R2:   processor.r2  = value; break;
    case REG_R3:   processor.r3  = value; break;
    case REG_R4:   processor.r4  = value; break;
    case REG_R5:   processor.r5  = value; break;
    case REG_R6:   processor.r6  = value; break;
    case REG_R7:   processor.r7  = value; break;
    case REG_R8:   processor.r8  = value; break;
    case REG_R9:   processor.r9  = value; break;
    case REG_R10:  processor.r10 = value; break;
    case REG_R11:  processor.r11 = value; break;
    case REG_R12:  processor.r12 = value; break;
    case REG_SP:   processor.r13 = value; break;
    case REG_LR:   processor.r14 = value; break;
    case REG_PC:   processor.r15 = value; break;  // triggers pipeline reload
    case REG_CPSR: processor.cpsr = value; break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Memory callbacks
// Higan's load() always calls get() and then masks/shifts the result for
// byte/halfword. We return naturally-sized values in the low bits.
// ---------------------------------------------------------------------------
auto CPU::get(higan::uint mode, higan::uint32 address) -> higan::uint32 {
    uint32_t addr = translate_ropi(uint32_t(address));
    ARM_Word value = 0;
    if (mode & Byte) {
        if (mem_.try_read_fast(addr, EndianMemory::Byte, value)) return value;
        try { return mem_.read_value(addr, EndianMemory::Byte); } catch (...) { return 0; }
    }
    if (mode & Half) {
        addr &= ~1u;
        if (mem_.try_read_fast(addr, EndianMemory::Halfword, value)) return value;
        try { return mem_.read_value(addr, EndianMemory::Halfword); } catch (...) { return 0; }
    }
    addr = (mode & Load) ? addr : (addr & ~3u);
    if (mem_.try_read_word_mapped_fast(addr, value)) return value;
    if (mem_.try_read_fast(addr, EndianMemory::Word, value)) return value;
    try { return mem_.read_value(addr); } catch (...) { return 0; }
}

auto CPU::set(higan::uint mode, higan::uint32 address, higan::uint32 word) -> void {
    uint32_t addr = translate_ropi(uint32_t(address));
    uint32_t val  = uint32_t(word);
    if (mode & Byte) {
        if (mem_.try_write_fast(addr, val, EndianMemory::Byte)) return;
        try { mem_.write_value(addr, val, EndianMemory::Byte); } catch (...) {}
        return;
    }
    if (mode & Half) {
        addr &= ~1u;
        if (mem_.try_write_fast(addr, val, EndianMemory::Halfword)) return;
        try { mem_.write_value(addr, val, EndianMemory::Halfword); } catch (...) {}
        return;
    }
    addr &= ~3u;
    static const bool trace_memwatch_enabled = std::getenv("ZEEMU_TRACE_MEMWATCH") != nullptr;
    if (!trace_memwatch_enabled && mem_.try_write_word_mapped_fast_untraced(addr, val)) return;
    if (mem_.try_write_fast(addr, val, EndianMemory::Word)) return;
    try { mem_.write_value(addr, val); } catch (...) {}
}

// ---------------------------------------------------------------------------
// SVC hook
// Called instead of jumping to the SVC vector (0x08).
// imm      = SVC immediate = hook ID
// ret_addr = pipeline.decode.address = instruction after the SVC
//
// After calling the handler we just set r15 to ret_addr. The modify callback
// on r15 sets pipeline.reload = true, so the next instruction() call reloads
// the pipeline from ret_addr and continues normally.
// stop() is NOT called — execution resumes after every SVC automatically.
// ---------------------------------------------------------------------------
auto CPU::softwareInterrupt(uint32_t imm, uint32_t ret_addr) -> void {
    const bool was_thumb = processor.cpsr.t;
    const uint32_t lr = uint32_t(processor.r14.data);
    const uint32_t svc_pc = ret_addr - (was_thumb ? 2u : 4u);
    const bool brew_hook_stub = svc_pc >= 0xFF000000u && svc_pc < 0xFF100000u;
    uint32_t next_pc = brew_hook_stub ? lr : (ret_addr | (was_thumb ? 1u : 0u));
    if (hook_handler_) {
        // BREW hook stubs are generated as SVC instructions at 0xffxxxxxx and
        // are reached through BLX/BL, so they must return through LR. Guest
        // code can also contain inline SVCs (for example old runtime
        // semihost-style output); those must resume at the instruction after
        // the SVC. Let the runtime distinguish the two cases without making
        // every SVC immediate a global hook id.
        const uint32_t inline_return = ret_addr | (was_thumb ? 1u : 0u);
        const uint32_t override_pc = hook_handler_(imm, inline_return, lr, svc_pc);
        if (override_pc != 0) {
            next_pc = override_pc;
        }
    }
    processor.cpsr.t = next_pc & 1u;
    processor.r15 = next_pc;
}

std::string CPU::disassemble(uint32_t addr, bool thumb) {
    return std::string(disassembleInstruction(
        higan::maybe<higan::uint32>{addr},
        higan::maybe<higan::boolean>{thumb}
    ));
}
