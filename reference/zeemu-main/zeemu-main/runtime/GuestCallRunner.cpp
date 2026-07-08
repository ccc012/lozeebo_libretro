#include "runtime/GuestCallRunner.h"

#include "cpu/core/CPU.h"
#include "cpu/memory/EndianMemory.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <vector>

GuestCallRunner::GuestCallRunner(CPU& cpu,
                                 EndianMemory& memory,
                                 uint32_t magic_ret,
                                 uint32_t& hle_return_override,
                                 const char*& active_guest_call,
                                 uint32_t& active_guest_hle_count)
    : cpu_(cpu),
      memory_(memory),
      magic_ret_(magic_ret),
      hle_return_override_(hle_return_override),
      active_guest_call_(active_guest_call),
      active_guest_hle_count_(active_guest_hle_count) {}

namespace {

double regs_to_double(uint32_t lo, uint32_t hi) {
    const uint64_t bits = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void double_to_regs(CPU& cpu, double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(value));
    cpu.set_reg(REG_R0, static_cast<uint32_t>(bits & 0xffffffffu));
    cpu.set_reg(REG_R1, static_cast<uint32_t>(bits >> 32));
}

float reg_to_float(uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void float_to_reg(CPU& cpu, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(value));
    cpu.set_reg(REG_R0, bits);
}

void continue_at_arm_pc(CPU& cpu, EndianMemory& memory, uint32_t pc) {
    cpu.processor.r15.data = pc;
    cpu.pipeline.reload = 0;
    cpu.pipeline.nonsequential = 1;
    cpu.pipeline.execute = {};
    cpu.pipeline.decode = {};
    cpu.pipeline.fetch = {pc, memory.read_value(pc), false};
}

bool arm_bl_target(uint32_t pc, uint32_t op, uint32_t& target) {
    if ((op & 0xff000000u) != 0xeb000000u) {
        return false;
    }
    uint32_t imm = op & 0x00ffffffu;
    if (imm & 0x00800000u) {
        imm |= 0xff000000u;
    }
    target = pc + 8u + (imm << 2);
    return target < 0xff000000u;
}

bool maybe_fast_path_softfloat_call(CPU& cpu, EndianMemory& memory, uint32_t pc, uint32_t op) {
    uint32_t target = 0;
    if (!arm_bl_target(pc, op, target)) {
        return false;
    }

    enum class Op {
        None,
        DAdd,
        DSub,
        DRSub,
        DMul,
        DDiv,
        FAdd,
        FSub,
        FMul,
        FDiv,
        FEq,
        FLt,
        FLe,
        FGt,
        FGe,
        DEq,
        DLt,
        DLe,
        DGt,
        DGe,
        DUnord,
        I2F,
        UI2F,
        D2I,
        D2UI,
        D2F,
        DPow,
        FSin,
        FCos,
        FTan,
        SDiv64,
    } kind = Op::None;

    const uint32_t t0 = memory.read_value(target);
    const uint32_t t1 = memory.read_value(target + 4u);
    const uint32_t t2 = memory.read_value(target + 8u);
    const uint32_t t3 = memory.read_value(target + 12u);
    const uint32_t t4 = memory.read_value(target + 16u);
    const uint32_t t5 = memory.read_value(target + 20u);
    const uint32_t t6 = memory.read_value(target + 24u);
    const uint32_t t7 = memory.read_value(target + 28u);
    const uint32_t t8 = memory.read_value(target + 32u);
    const uint32_t t9 = memory.read_value(target + 36u);

    if (t0 == 0xe92d4030u && t1 == 0xe1a04081u && t2 == 0xe1a05083u) {
        kind = Op::DAdd;
    } else if (t0 == 0xe2233102u && t1 == 0xe92d4030u && t2 == 0xe1a04081u) {
        kind = Op::DSub;
    } else if (t0 == 0xe2211102u && t1 == 0xea000000u && t2 == 0xe92d4030u) {
        kind = Op::DRSub;
    } else if (t0 == 0xe92d4070u && t1 == 0xe3a0c0ffu && t2 == 0xe38ccc07u && t8 == 0xe0844005u) {
        kind = Op::DMul;
    } else if (t0 == 0xe92d4070u && t1 == 0xe3a0c0ffu && t2 == 0xe38ccc07u && t8 == 0xe0444005u) {
        kind = Op::DDiv;
    } else if (t0 == 0xe1b02080u && t1 == 0x11b03081u && t2 == 0x11320003u) {
        kind = Op::FAdd;
    } else if (t0 == 0xe2211102u && t1 == 0xe1b02080u && t2 == 0x11b03081u) {
        kind = Op::FSub;
    } else if (t0 == 0xe3a0c0ffu && t1 == 0xe01c2ba0u && t2 == 0x101c3ba1u && t6 == 0xe0822003u) {
        kind = Op::FMul;
    } else if (t0 == 0xe3a0c0ffu && t1 == 0xe01c2ba0u && t2 == 0x101c3ba1u && t6 == 0xe0422003u) {
        kind = Op::FDiv;
    } else if (t0 == 0xe52de008u && t1 == 0xebfffff7u && t2 == 0x03a00001u) {
        kind = Op::FEq;
    } else if (t0 == 0xe52de008u && t1 == 0xebfffff1u && t2 == 0x33a00001u) {
        kind = Op::FLt;
    } else if (t0 == 0xe52de008u && t1 == 0xebffffebu && t2 == 0x93a00001u) {
        kind = Op::FLe;
    } else if (t0 == 0xe52de008u && t1 == 0xebffffd8u && t2 == 0x33a00001u) {
        kind = Op::FGt;
    } else if (t0 == 0xe52de008u && t1 == 0xebffffdeu && t2 == 0x93a00001u) {
        kind = Op::FGe;
    } else if (t0 == 0xe1a0c081u && t1 == 0xe1f0caccu && t2 == 0x1a000001u) {
        kind = Op::DUnord;
    } else if (t0 == 0xe2103102u && t1 == 0x42600000u && t2 == 0xe1b0c000u) {
        kind = Op::I2F;
    } else if (t0 == 0xe3a03000u && t1 == 0xea000001u && t2 == 0xe2103102u) {
        kind = Op::UI2F;
    } else if (t0 == 0xe1a02081u && t1 == 0xe2922602u && t2 == 0x2a00000cu) {
        kind = Op::D2I;
    } else if (t0 == 0xe1b02081u && t1 == 0x2a00000au && t2 == 0xe2922602u) {
        kind = Op::D2UI;
    } else if (t0 == 0xe1a02081u && t1 == 0xe2523207u && t2 == 0x2253c602u) {
        kind = Op::D2F;
    } else if (t0 == 0xe92d47f0u && t1 == 0xe1a04002u && t2 == 0xe1a05003u && t3 == 0xe1a08000u) {
        kind = Op::DPow;
    } else if (target == 0x10068680u ||
               (t0 == 0xe52de004u && t1 == 0xe59f20b0u && t2 == 0xe3c03102u && t3 == 0xe1530002u)) {
        kind = Op::FSin;
    } else if (target == 0x10068744u ||
               (t0 == 0xe52de004u && t1 == 0xe59f20b8u && t2 == 0xe3c03102u && t3 == 0xe1530002u)) {
        kind = Op::FCos;
    } else if (target == 0x10068810u ||
               (t0 == 0xe52de004u && t1 == 0xe59f2074u && t2 == 0xe3c03102u && t3 == 0xe1530002u)) {
        kind = Op::FTan;
    } else if (t0 == 0xe92d4010u && t1 == 0xe1b040c1u && t2 == 0xe02440a3u &&
               t3 == 0x5a000001u && t4 == 0xe2700000u && t5 == 0xe2e11000u &&
               t6 == 0xe1130003u && t7 == 0x5a000001u && t8 == 0xe2722000u &&
               t9 == 0xe2e33000u) {
        kind = Op::SDiv64;
    }

    if (kind == Op::None) {
        return false;
    }

    switch (kind) {
        case Op::DAdd:
            double_to_regs(cpu, regs_to_double(cpu.get_reg(REG_R0), cpu.get_reg(REG_R1)) +
                                regs_to_double(cpu.get_reg(REG_R2), cpu.get_reg(REG_R3)));
            break;
        case Op::DSub:
            double_to_regs(cpu, regs_to_double(cpu.get_reg(REG_R0), cpu.get_reg(REG_R1)) -
                                regs_to_double(cpu.get_reg(REG_R2), cpu.get_reg(REG_R3)));
            break;
        case Op::DRSub:
            double_to_regs(cpu, regs_to_double(cpu.get_reg(REG_R2), cpu.get_reg(REG_R3)) -
                                regs_to_double(cpu.get_reg(REG_R0), cpu.get_reg(REG_R1)));
            break;
        case Op::DMul:
            double_to_regs(cpu, regs_to_double(cpu.get_reg(REG_R0), cpu.get_reg(REG_R1)) *
                                regs_to_double(cpu.get_reg(REG_R2), cpu.get_reg(REG_R3)));
            break;
        case Op::DDiv:
            double_to_regs(cpu, regs_to_double(cpu.get_reg(REG_R0), cpu.get_reg(REG_R1)) /
                                regs_to_double(cpu.get_reg(REG_R2), cpu.get_reg(REG_R3)));
            break;
        case Op::FAdd:
            float_to_reg(cpu, reg_to_float(cpu.get_reg(REG_R0)) + reg_to_float(cpu.get_reg(REG_R1)));
            break;
        case Op::FSub:
            float_to_reg(cpu, reg_to_float(cpu.get_reg(REG_R0)) - reg_to_float(cpu.get_reg(REG_R1)));
            break;
        case Op::FMul:
            float_to_reg(cpu, reg_to_float(cpu.get_reg(REG_R0)) * reg_to_float(cpu.get_reg(REG_R1)));
            break;
        case Op::FDiv:
            float_to_reg(cpu, reg_to_float(cpu.get_reg(REG_R0)) / reg_to_float(cpu.get_reg(REG_R1)));
            break;
        case Op::FEq: {
            const float a = reg_to_float(cpu.get_reg(REG_R0));
            const float b = reg_to_float(cpu.get_reg(REG_R1));
            cpu.set_reg(REG_R0, (!std::isnan(a) && !std::isnan(b) && a == b) ? 1u : 0u);
            break;
        }
        case Op::FLt: {
            const float a = reg_to_float(cpu.get_reg(REG_R0));
            const float b = reg_to_float(cpu.get_reg(REG_R1));
            cpu.set_reg(REG_R0, (!std::isnan(a) && !std::isnan(b) && a < b) ? 1u : 0u);
            break;
        }
        case Op::FLe: {
            const float a = reg_to_float(cpu.get_reg(REG_R0));
            const float b = reg_to_float(cpu.get_reg(REG_R1));
            cpu.set_reg(REG_R0, (!std::isnan(a) && !std::isnan(b) && a <= b) ? 1u : 0u);
            break;
        }
        case Op::FGt: {
            const float a = reg_to_float(cpu.get_reg(REG_R0));
            const float b = reg_to_float(cpu.get_reg(REG_R1));
            cpu.set_reg(REG_R0, (!std::isnan(a) && !std::isnan(b) && a > b) ? 1u : 0u);
            break;
        }
        case Op::FGe: {
            const float a = reg_to_float(cpu.get_reg(REG_R0));
            const float b = reg_to_float(cpu.get_reg(REG_R1));
            cpu.set_reg(REG_R0, (!std::isnan(a) && !std::isnan(b) && a >= b) ? 1u : 0u);
            break;
        }
        case Op::DEq: {
            const double a = regs_to_double(cpu.get_reg(REG_R0), cpu.get_reg(REG_R1));
            const double b = regs_to_double(cpu.get_reg(REG_R2), cpu.get_reg(REG_R3));
            cpu.set_reg(REG_R0, (!std::isnan(a) && !std::isnan(b) && a == b) ? 1u : 0u);
            break;
        }
        case Op::DLt: {
            const double a = regs_to_double(cpu.get_reg(REG_R0), cpu.get_reg(REG_R1));
            const double b = regs_to_double(cpu.get_reg(REG_R2), cpu.get_reg(REG_R3));
            cpu.set_reg(REG_R0, (!std::isnan(a) && !std::isnan(b) && a < b) ? 1u : 0u);
            break;
        }
        case Op::DLe: {
            const double a = regs_to_double(cpu.get_reg(REG_R0), cpu.get_reg(REG_R1));
            const double b = regs_to_double(cpu.get_reg(REG_R2), cpu.get_reg(REG_R3));
            cpu.set_reg(REG_R0, (!std::isnan(a) && !std::isnan(b) && a <= b) ? 1u : 0u);
            break;
        }
        case Op::DGt: {
            const double a = regs_to_double(cpu.get_reg(REG_R0), cpu.get_reg(REG_R1));
            const double b = regs_to_double(cpu.get_reg(REG_R2), cpu.get_reg(REG_R3));
            cpu.set_reg(REG_R0, (!std::isnan(a) && !std::isnan(b) && a > b) ? 1u : 0u);
            break;
        }
        case Op::DGe: {
            const double a = regs_to_double(cpu.get_reg(REG_R0), cpu.get_reg(REG_R1));
            const double b = regs_to_double(cpu.get_reg(REG_R2), cpu.get_reg(REG_R3));
            cpu.set_reg(REG_R0, (!std::isnan(a) && !std::isnan(b) && a >= b) ? 1u : 0u);
            break;
        }
        case Op::DUnord: {
            const double a = regs_to_double(cpu.get_reg(REG_R0), cpu.get_reg(REG_R1));
            const double b = regs_to_double(cpu.get_reg(REG_R2), cpu.get_reg(REG_R3));
            cpu.set_reg(REG_R0, (std::isnan(a) || std::isnan(b)) ? 1u : 0u);
            break;
        }
        case Op::I2F:
            float_to_reg(cpu, static_cast<float>(static_cast<int32_t>(cpu.get_reg(REG_R0))));
            break;
        case Op::UI2F:
            float_to_reg(cpu, static_cast<float>(cpu.get_reg(REG_R0)));
            break;
        case Op::D2I:
            cpu.set_reg(REG_R0, static_cast<uint32_t>(static_cast<int32_t>(regs_to_double(cpu.get_reg(REG_R0), cpu.get_reg(REG_R1)))));
            break;
        case Op::D2UI:
            cpu.set_reg(REG_R0, static_cast<uint32_t>(regs_to_double(cpu.get_reg(REG_R0), cpu.get_reg(REG_R1))));
            break;
        case Op::D2F:
            float_to_reg(cpu, static_cast<float>(regs_to_double(cpu.get_reg(REG_R0), cpu.get_reg(REG_R1))));
            break;
        case Op::DPow:
            double_to_regs(cpu, std::pow(regs_to_double(cpu.get_reg(REG_R0), cpu.get_reg(REG_R1)),
                                         regs_to_double(cpu.get_reg(REG_R2), cpu.get_reg(REG_R3))));
            break;
        case Op::FSin:
            float_to_reg(cpu, std::sin(reg_to_float(cpu.get_reg(REG_R0))));
            break;
        case Op::FCos:
            float_to_reg(cpu, std::cos(reg_to_float(cpu.get_reg(REG_R0))));
            break;
        case Op::FTan:
            float_to_reg(cpu, std::tan(reg_to_float(cpu.get_reg(REG_R0))));
            break;
        case Op::SDiv64: {
            const uint64_t dividend_bits =
                static_cast<uint64_t>(cpu.get_reg(REG_R0)) |
                (static_cast<uint64_t>(cpu.get_reg(REG_R1)) << 32);
            const uint64_t divisor_bits =
                static_cast<uint64_t>(cpu.get_reg(REG_R2)) |
                (static_cast<uint64_t>(cpu.get_reg(REG_R3)) << 32);
            const int64_t dividend = static_cast<int64_t>(dividend_bits);
            const int64_t divisor = static_cast<int64_t>(divisor_bits);
            if (divisor == 0) {
                return false;
            }
            int64_t quotient = 0;
            if (dividend == std::numeric_limits<int64_t>::min() && divisor == -1) {
                quotient = std::numeric_limits<int64_t>::min();
            } else {
                quotient = dividend / divisor;
            }
            const uint64_t quotient_bits = static_cast<uint64_t>(quotient);
            cpu.set_reg(REG_R0, static_cast<uint32_t>(quotient_bits & 0xffffffffu));
            cpu.set_reg(REG_R1, static_cast<uint32_t>(quotient_bits >> 32));
            break;
        }
        case Op::None:
            return false;
    }

    const uint32_t ret = pc + 4u;
    cpu.set_reg(REG_LR, ret);
    continue_at_arm_pc(cpu, memory, ret);
    if (std::getenv("ZEEMU_TRACE_FASTPATHS")) {
        std::cout << "[fastpath] softfloat target=0x" << std::hex << target
                  << " ret=0x" << ret << std::endl;
    }
    return true;
}

bool maybe_fast_path_arm_palette_distance_loop(CPU& cpu, EndianMemory& memory, uint32_t pc) {
    const uint32_t signature[] = {
        0xe5323004u, // ldrb r3,[r2,#-4]!
        0xe53e1004u, // ldrb r1,[lr,#-4]!
        0xe592c400u, // ldr r12,[r2,#0x400]
        0xe5920800u, // ldr r0,[r2,#0x800]
        0xe0030893u, // mul r3,r3,r8
        0xe00c079cu, // mul r12,r12,r7
        0xe0000690u, // mul r0,r0,r6
        0xe0413003u, // sub r3,r1,r3
        0xe043300cu, // sub r3,r3,r12
        0xe0433000u, // sub r3,r3,r0
        0xe1530005u, // cmp r3,r5
        0xb5c94000u, // strblt r4,[r9]
        0xb1a05003u, // movlt r5,r3
        0xe15a000eu, // cmp r10,lr
        0xe2444001u, // sub r4,r4,#1
        0x1affffefu, // bne pc-0x44
    };
    for (uint32_t i = 0; i < sizeof(signature) / sizeof(signature[0]); ++i) {
        if (memory.read_value(pc + i * 4u) != signature[i]) {
            return false;
        }
    }

    uint32_t r2 = cpu.get_reg(REG_R2);
    uint32_t lr = cpu.get_reg(REG_LR);
    uint32_t r4 = cpu.get_reg(REG_R4);
    uint32_t r5 = cpu.get_reg(REG_R5);
    const uint32_t r6 = cpu.get_reg(REG_R6);
    const uint32_t r7 = cpu.get_reg(REG_R7);
    const uint32_t r8 = cpu.get_reg(REG_R8);
    const uint32_t r9 = cpu.get_reg(REG_R9);
    const uint32_t r10 = cpu.get_reg(REG_R10);
    if (r2 >= 0xff000000u || lr >= 0xff000000u || r9 >= 0xff000000u) {
        return false;
    }

    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r3 = cpu.get_reg(REG_R3);
    uint32_t r12 = cpu.get_reg(REG_R12);
    uint32_t iterations = 0;
    constexpr uint32_t kMaxIterations = 4096;
    for (; iterations < kMaxIterations; ++iterations) {
        r2 -= 4;
        lr -= 4;
        if (r2 >= 0xff000000u || lr >= 0xff000000u ||
            r2 + 0x800u >= 0xff000000u || r9 >= 0xff000000u) {
            return false;
        }
        r3 = memory.read_value(r2, EndianMemory::Byte);
        r1 = memory.read_value(lr, EndianMemory::Byte);
        r12 = memory.read_value(r2 + 0x400u);
        r0 = memory.read_value(r2 + 0x800u);
        r3 = r3 * r8;
        r12 = r12 * r7;
        r0 = r0 * r6;
        r3 = r1 - r3;
        r3 = r3 - r12;
        r3 = r3 - r0;
        if (static_cast<int32_t>(r3) < static_cast<int32_t>(r5)) {
            memory.write_value(r9, r4, EndianMemory::Byte);
            r5 = r3;
        }
        const bool done = (r10 == lr);
        r4 -= 1;
        if (done) {
            break;
        }
    }
    if (iterations == kMaxIterations) {
        return false;
    }

    cpu.set_reg(REG_R0, r0);
    cpu.set_reg(REG_R1, r1);
    cpu.set_reg(REG_R2, r2);
    cpu.set_reg(REG_R3, r3);
    cpu.set_reg(REG_R4, r4);
    cpu.set_reg(REG_R5, r5);
    cpu.set_reg(REG_R12, r12);
    cpu.set_reg(REG_LR, lr);
    // The original block exits with flags from CMP r10,lr where Z=1, C=1, N=V=0.
    cpu.set_reg(REG_CPSR, (cpu.get_reg(REG_CPSR) & 0x0fffffffu) | (1u << 30) | 0x20000000u);
    cpu.set_reg(REG_PC, pc + static_cast<uint32_t>(sizeof(signature)));
    if (std::getenv("ZEEMU_TRACE_FASTPATHS")) {
        std::cout << "[fastpath] ARM palette distance loop iterations=" << std::dec << (iterations + 1)
                  << " pc=0x" << std::hex << pc << std::endl;
    }
    return true;
}

bool maybe_fast_path_arm_udiv_nibble_loop(CPU& cpu, EndianMemory& memory, uint32_t pc) {
    const uint32_t signature[] = {
        0xe1530001u, // cmp r3,r1
        0x20433001u, // subcs r3,r3,r1
        0x2180000cu, // orrcs r0,r0,r12
        0xe15300a1u, // cmp r3,r1,lsr #1
        0x204330a1u, // subcs r3,r3,r1,lsr #1
        0x218000acu, // orrcs r0,r0,r12,lsr #1
        0xe1530121u, // cmp r3,r1,lsr #2
        0x20433121u, // subcs r3,r3,r1,lsr #2
        0x2180012cu, // orrcs r0,r0,r12,lsr #2
        0xe15301a1u, // cmp r3,r1,lsr #3
        0x204331a1u, // subcs r3,r3,r1,lsr #3
        0x218001acu, // orrcs r0,r0,r12,lsr #3
        0xe1b03203u, // movs r3,r3,lsl #4
        0x11b0c22cu, // movnes r12,r12,lsr #4
        0x1afffff0u, // bne pc-0x40
    };
    for (uint32_t i = 0; i < sizeof(signature) / sizeof(signature[0]); ++i) {
        if (memory.read_value(pc + i * 4u) != signature[i]) {
            return false;
        }
    }

    uint32_t r0 = cpu.get_reg(REG_R0);
    const uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r3 = cpu.get_reg(REG_R3);
    uint32_t r12 = cpu.get_reg(REG_R12);
    uint32_t iterations = 0;
    for (; iterations < 16; ++iterations) {
        if (r3 >= r1) {
            r3 -= r1;
            r0 |= r12;
        }
        const uint32_t r1_1 = r1 >> 1;
        if (r3 >= r1_1) {
            r3 -= r1_1;
            r0 |= r12 >> 1;
        }
        const uint32_t r1_2 = r1 >> 2;
        if (r3 >= r1_2) {
            r3 -= r1_2;
            r0 |= r12 >> 2;
        }
        const uint32_t r1_3 = r1 >> 3;
        if (r3 >= r1_3) {
            r3 -= r1_3;
            r0 |= r12 >> 3;
        }
        r3 <<= 4;
        if (r3 == 0) {
            break;
        }
        r12 >>= 4;
    }
    if (iterations == 16) {
        return false;
    }

    cpu.set_reg(REG_R0, r0);
    cpu.set_reg(REG_R3, r3);
    cpu.set_reg(REG_R12, r12);
    // Next instruction is CMP r2,#0xfd, so flags from the final MOVS are dead.
    cpu.set_reg(REG_PC, pc + static_cast<uint32_t>(sizeof(signature)));
    if (std::getenv("ZEEMU_TRACE_FASTPATHS")) {
        std::cout << "[fastpath] ARM udiv nibble loop iterations=" << std::dec << (iterations + 1)
                  << " pc=0x" << std::hex << pc << std::endl;
    }
    return true;
}

} // namespace

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

uint32_t GuestCallRunner::call(const char* label, uint32_t fn, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    cpu_.reset(fn);
    cpu_.set_reg(REG_R0, r0);
    cpu_.set_reg(REG_R1, r1);
    cpu_.set_reg(REG_R2, r2);
    cpu_.set_reg(REG_R3, r3);
    cpu_.set_reg(REG_LR, magic_ret_);
    active_guest_call_ = label;
    active_guest_hle_count_ = 0;

    constexpr int kRecentGuestCount = 64;
    uint32_t recent_guest_pc[kRecentGuestCount] = {};
    uint32_t recent_guest_op[kRecentGuestCount] = {};
    int recent_guest_pos = 0;
    bool hit_zero_instruction = false;
    bool hit_guest_heap_instruction = false;
    const bool trace_guest_progress = std::getenv("ZEEMU_TRACE_GUEST_PROGRESS") != nullptr;
    const bool trace_guest_profile = std::getenv("ZEEMU_TRACE_GUEST_PROFILE") != nullptr;
    const bool trace_branch_high = std::getenv("ZEEMU_TRACE_BRANCH_HIGH") != nullptr;
    std::unordered_map<uint32_t, uint64_t> guest_pc_counts;
    if (trace_guest_profile) {
        guest_pc_counts.reserve(4096);
    }
    uint64_t trace_guest_progress_step = 5000000ull;
    uint64_t trace_guest_profile_min_steps = 100000ull;
    if (const char* value = std::getenv("ZEEMU_TRACE_GUEST_PROGRESS_STEP")) {
        char* end = nullptr;
        const unsigned long long parsed = std::strtoull(value, &end, 0);
        if (end != value && parsed > 0) {
            trace_guest_progress_step = parsed;
        }
    }
    if (const char* value = std::getenv("ZEEMU_TRACE_GUEST_PROFILE_MIN_STEPS")) {
        char* end = nullptr;
        const unsigned long long parsed = std::strtoull(value, &end, 0);
        if (end != value) {
            trace_guest_profile_min_steps = parsed;
        }
    }
    uint64_t guest_steps = 0;
    auto print_hot_pcs = [&]() {
        if (!trace_guest_profile || guest_steps < trace_guest_profile_min_steps || guest_pc_counts.empty()) {
            return;
        }
        std::vector<std::pair<uint32_t, uint64_t>> hot_pcs(guest_pc_counts.begin(), guest_pc_counts.end());
        std::sort(hot_pcs.begin(), hot_pcs.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) {
                return a.second > b.second;
            }
            return a.first < b.first;
        });
        const size_t count = std::min<size_t>(hot_pcs.size(), 24);
        std::cout << label << " hot PCs:" << std::endl;
        for (size_t i = 0; i < count; ++i) {
            const auto& [pc, hits] = hot_pcs[i];
            const uint32_t op = memory_.read_value(pc);
            const double pct = guest_steps == 0 ? 0.0 : (100.0 * static_cast<double>(hits) / static_cast<double>(guest_steps));
            std::cout << "  PC=0x" << std::hex << pc
                      << " op=0x" << op
                      << std::dec << " hits=" << hits
                      << " pct=" << pct << std::endl;
        }
    };

    for (int guard = 0; guard < 200000; ++guard) {
        for (int step = 0; step < 20000 && !cpu_.is_stopped() && cpu_.get_reg(REG_PC) != magic_ret_; ++step) {
            uint32_t pc = cpu_.pipeline.execute.address;
            uint32_t op = cpu_.pipeline.execute.instruction;
            recent_guest_pc[recent_guest_pos & (kRecentGuestCount - 1)] = pc;
            recent_guest_op[recent_guest_pos & (kRecentGuestCount - 1)] = op;
            ++recent_guest_pos;
            if (trace_guest_profile && (pc != 0 || op != 0)) {
                ++guest_pc_counts[pc];
            }

            if (op == 0 && pc >= 0x80000000u && pc < 0xFF000000u) {
                hit_zero_instruction = true;
                break;
            }
            if (trace_branch_high && pc >= 0x50000000u && pc < 0x54000000u) {
                hit_guest_heap_instruction = true;
                std::cout << "[CPU_BRANCH_SUSPECT] " << label
                          << " reached guest-heap PC before step"
                          << " PC=0x" << std::hex << pc
                          << " op=0x" << op
                          << " LR=0x" << cpu_.get_reg(REG_LR)
                          << " R0=0x" << cpu_.get_reg(REG_R0)
                          << " R1=0x" << cpu_.get_reg(REG_R1)
                          << " R2=0x" << cpu_.get_reg(REG_R2)
                          << " R3=0x" << cpu_.get_reg(REG_R3)
                          << " R4=0x" << cpu_.get_reg(REG_R4)
                          << " R12=0x" << cpu_.get_reg(REG_R12)
                          << " SP=0x" << cpu_.get_reg(REG_SP) << std::endl;
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
            if (maybe_fast_path_softfloat_call(cpu_, memory_, pc, op)) {
                ++guest_steps;
                continue;
            }
            if (maybe_fast_path_arm_palette_distance_loop(cpu_, memory_, pc)) {
                ++guest_steps;
                continue;
            }
            if (maybe_fast_path_arm_udiv_nibble_loop(cpu_, memory_, pc)) {
                ++guest_steps;
                continue;
            }
            cpu_.step_once();
            ++guest_steps;

            if (trace_guest_progress && (guest_steps % trace_guest_progress_step) == 0) {
                std::cout << label << " progress steps=" << std::dec << guest_steps
                          << " HLE=" << active_guest_hle_count_
                          << " PC=0x" << std::hex << cpu_.get_reg(REG_PC)
                          << " LR=0x" << cpu_.get_reg(REG_LR)
                          << " R0=0x" << cpu_.get_reg(REG_R0)
                          << " R1=0x" << cpu_.get_reg(REG_R1)
                          << " R2=0x" << cpu_.get_reg(REG_R2)
                          << " R3=0x" << cpu_.get_reg(REG_R3)
                          << " R4=0x" << cpu_.get_reg(REG_R4)
                          << " R12=0x" << cpu_.get_reg(REG_R12) << std::endl;
                std::cout << label << " recent PCs:" << std::endl;
                int count = recent_guest_pos < kRecentGuestCount ? recent_guest_pos : kRecentGuestCount;
                for (int i = count; i > 0; --i) {
                    int idx = (recent_guest_pos - i) & (kRecentGuestCount - 1);
                    std::cout << "  PC=0x" << std::hex << recent_guest_pc[idx]
                              << " op=0x" << recent_guest_op[idx] << std::endl;
                }
                print_hot_pcs();
            }
        }
        if (hit_zero_instruction || hit_guest_heap_instruction) {
            break;
        }
        if (cpu_.get_reg(REG_PC) == magic_ret_ || cpu_.is_stopped()) {
            break;
        }
    }

    bool returned = cpu_.get_reg(REG_PC) == magic_ret_ || cpu_.is_stopped();
    std::cout << label << (returned ? " returned" : " timed out")
              << " after " << std::dec << active_guest_hle_count_ << " HLE calls"
              << " PC=0x" << std::hex << cpu_.get_reg(REG_PC)
              << " LR=0x" << cpu_.get_reg(REG_LR)
              << " R0=0x" << cpu_.get_reg(REG_R0)
              << " R1=0x" << cpu_.get_reg(REG_R1)
              << " R2=0x" << cpu_.get_reg(REG_R2)
              << " R3=0x" << cpu_.get_reg(REG_R3)
              << " R4=0x" << cpu_.get_reg(REG_R4)
              << " R12=0x" << cpu_.get_reg(REG_R12) << std::endl;

    if (hit_zero_instruction) {
        std::cout << label << " stopped at zero instruction before stepping it" << std::endl;
    }
    if (hit_guest_heap_instruction) {
        std::cout << label << " stopped at guest-heap instruction before stepping it" << std::endl;
    }
    if (!returned) {
        std::cout << label << " recent PCs:" << std::endl;
        int count = recent_guest_pos < kRecentGuestCount ? recent_guest_pos : kRecentGuestCount;
        for (int i = count; i > 0; --i) {
            int idx = (recent_guest_pos - i) & (kRecentGuestCount - 1);
            std::cout << "  PC=0x" << std::hex << recent_guest_pc[idx]
                      << " op=0x" << recent_guest_op[idx] << std::endl;
        }
    }
    print_hot_pcs();

    active_guest_call_ = nullptr;
    return cpu_.get_reg(REG_R0);
}
