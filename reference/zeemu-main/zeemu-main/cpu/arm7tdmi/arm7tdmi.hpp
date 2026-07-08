// higan-derived ARMv4T core extended with ARMv5E/ARMv6, VFP, and Zeebo/BREW
// compatibility pieces needed by ARM1136J-S title bringup.

#pragma once

#include "emulator/emulator.hpp"

namespace higan {

struct ARM7TDMI {
  enum : uint {
    Nonsequential = 1 << 0,  //N cycle
    Sequential    = 1 << 1,  //S cycle
    Prefetch      = 1 << 2,  //instruction fetch
    Byte          = 1 << 3,  // 8-bit access
    Half          = 1 << 4,  //16-bit access
    Word          = 1 << 5,  //32-bit access
    Load          = 1 << 6,  //load operation
    Store         = 1 << 7,  //store operation
    Signed        = 1 << 8,  //sign-extend
  };

  virtual auto step(uint clocks) -> void = 0;
  virtual auto sleep() -> void = 0;
  virtual auto get(uint mode, uint32 address) -> uint32 = 0;
  virtual auto set(uint mode, uint32 address, uint32 word) -> void = 0;
  // Called instead of jumping to SVC vector — HLE hook entry point
  virtual auto softwareInterrupt(uint32_t imm, uint32_t ret_addr) -> void = 0;

  //arm7tdmi.cpp
  ARM7TDMI();
  auto power() -> void;

  //registers.cpp
  struct GPR;
  struct PSR;
  inline auto r(uint4) -> GPR&;
  inline auto cpsr() -> PSR&;
  inline auto spsr() -> PSR&;
  inline auto privileged() const -> bool;
  inline auto exception() const -> bool;

  //memory.cpp
  auto idle() -> void;
  auto read(uint mode, uint32 address) -> uint32;
  auto load(uint mode, uint32 address) -> uint32;
  auto write(uint mode, uint32 address, uint32 word) -> void;
  auto store(uint mode, uint32 address, uint32 word) -> void;

  //algorithms.cpp
  auto ADD(uint32, uint32, bool) -> uint32;
  auto ASR(uint32, uint8) -> uint32;
  auto BIT(uint32) -> uint32;
  auto LSL(uint32, uint8) -> uint32;
  auto LSR(uint32, uint8) -> uint32;
  auto MUL(uint32, uint32, uint32) -> uint32;
  auto ROR(uint32, uint8) -> uint32;
  auto RRX(uint32) -> uint32;
  auto SUB(uint32, uint32, bool) -> uint32;
  auto TST(uint4) -> bool;

  // Address translation hook (override in CPU for ROPI support)
  virtual uint32 translate_address(uint32 addr) { return addr; }

  //instruction.cpp
  auto fetch() -> void;
  auto instruction() -> void;
  auto trace_nfs_boot(uint32 pc) -> void;
  auto trace_nfs_apt(uint32 pc) -> void;
  auto exception(uint mode, uint32 address) -> void;
  auto armInitialize() -> void;
  auto thumbInitialize() -> void;

  //instructions-arm.cpp
  auto armALU(uint4 mode, uint4 target, uint4 source, uint32 data) -> void;
  auto armMoveToStatus(uint4 field, uint1 source, uint32 data) -> void;

  auto armInstructionBranch(uint32) -> void;
  auto armInstructionBranchLinkExchangeRegister(uint32) -> void;
  auto armInstructionBranchExchangeRegister(uint32) -> void;
  auto armInstructionBranchExchangeJazelle(uint32) -> void;
  auto armInstructionBreakpoint(uint32) -> void;
  auto armInstructionChangeProcessorState(uint32) -> void;
  auto armInstructionCountLeadingZeros(uint32) -> void;
  auto armInstructionPreload(uint32) -> void;
  auto armInstructionSetEnd(uint32) -> void;
  auto armInstructionSignedExtendByte(uint32) -> void;
  auto armInstructionUnsignedExtendByte(uint32) -> void;
  auto armInstructionSignedExtendHalfword(uint32) -> void;
  auto armInstructionUnsignedExtendHalfword(uint32) -> void;
  auto armInstructionByteReverseWord(uint32) -> void;
  auto armInstructionByteReversePackedHalfword(uint32) -> void;
  auto armInstructionByteReverseSignedHalfword(uint32) -> void;
  auto armInstructionCoprocessorDataOperation(uint32) -> void;
  auto armInstructionCoprocessorRegisterTransferLong(uint32) -> void;
  auto armInstructionCoprocessorRegisterTransfer(uint32) -> void;
  auto armInstructionDataImmediate(uint32) -> void;
  auto armInstructionDataImmediateShift(uint32) -> void;
  auto armInstructionDataRegisterShift(uint32) -> void;
  auto armInstructionLoadImmediate(uint32) -> void;
  auto armInstructionLoadRegister(uint32) -> void;
  auto armInstructionMemorySwap(uint32) -> void;
  auto armInstructionMoveDoubleImmediate(uint32) -> void;
  auto armInstructionMoveDoubleRegister(uint32) -> void;
  auto armInstructionMoveHalfImmediate(uint32) -> void;
  auto armInstructionMoveHalfRegister(uint32) -> void;
  auto armInstructionMoveImmediateOffset(uint32) -> void;
  auto armInstructionMoveMultiple(uint32) -> void;
  auto armInstructionMoveRegisterOffset(uint32) -> void;
  auto armInstructionMoveToRegisterFromStatus(uint32) -> void;
  auto armInstructionMoveToStatusFromImmediate(uint32) -> void;
  auto armInstructionMoveToStatusFromRegister(uint32) -> void;
  auto armInstructionMultiply(uint32) -> void;
  auto armInstructionMultiplyLong(uint32) -> void;
  auto armInstructionMultiply16(uint32) -> void;
  auto armInstructionMultiply16Long(uint32) -> void;
  auto armInstructionMultiply16Wide(uint32) -> void;
  auto armInstructionSaturatingAdd(uint32) -> void;
  auto armInstructionPackHalfword(uint32) -> void;
  auto armInstructionSaturate(uint32) -> void;
  auto armInstructionSelect(uint32) -> void;
  auto armInstructionSIMD(uint32) -> void;
  auto armInstructionSoftwareInterrupt(uint32) -> void;
  auto armInstructionUndefined(uint32) -> void;

  //instructions-thumb.cpp
  auto thumbInstructionALU(uint16) -> void;
  auto thumbInstructionALUExtended(uint16) -> void;
  auto thumbInstructionAddRegister(uint16) -> void;
  auto thumbInstructionAdjustImmediate(uint16) -> void;
  auto thumbInstructionAdjustRegister(uint16) -> void;
  auto thumbInstructionAdjustStack(uint16) -> void;
  auto thumbInstructionBranchExchange(uint16) -> void;
  auto thumbInstructionBranchFarPrefix(uint16) -> void;
  auto thumbInstructionBranchFarSuffix(uint16) -> void;
  auto thumbInstructionBranchFarSuffixExchange(uint16) -> void;
  auto thumbInstructionBranchNear(uint16) -> void;
  auto thumbInstructionBranchTest(uint16) -> void;
  auto thumbInstructionImmediate(uint16) -> void;
  auto thumbInstructionLoadLiteral(uint16) -> void;
  auto thumbInstructionMoveByteImmediate(uint16) -> void;
  auto thumbInstructionMoveHalfImmediate(uint16) -> void;
  auto thumbInstructionMoveMultiple(uint16) -> void;
  auto thumbInstructionMoveRegisterOffset(uint16) -> void;
  auto thumbInstructionMoveStack(uint16) -> void;
  auto thumbInstructionMoveWordImmediate(uint16) -> void;
  auto thumbInstructionShiftImmediate(uint16) -> void;
  auto thumbInstructionSoftwareInterrupt(uint16) -> void;
  auto thumbInstructionStackMultiple(uint16) -> void;
  auto thumbInstructionUndefined(uint16) -> void;

  //serialization.cpp
  auto serialize(serializer&) -> void;

  //disassembler.cpp
  auto disassembleInstruction(maybe<uint32> pc = {}, maybe<boolean> thumb = {}) -> string;
  auto disassembleContext() -> string;

  struct GPR {
    GPR() = default;
    GPR(const GPR& value) : data(value.data), modify(value.modify) {}

    inline operator uint32_t() const { return data; }
    inline auto operator=(const GPR& value) -> GPR& { return operator=(value.data); }

    inline auto operator=(uint32 value) -> GPR& {
      data = value;
      if(modify) modify();
      return *this;
    }

    uint32 data;
    function<auto () -> void> modify;
  };

  struct PSR {
    enum : uint {
      USR = 0x10,  //user
      FIQ = 0x11,  //fast interrupt
      IRQ = 0x12,  //interrupt
      SVC = 0x13,  //service
      ABT = 0x17,  //abort
      UND = 0x1b,  //undefined
      SYS = 0x1f,  //system
    };

    inline operator uint32_t() const {
      return uint32_t(m) << 0 | uint32_t(t) << 5 | uint32_t(f) << 6 | uint32_t(i) << 7 |
             uint32_t(a) << 8 | uint32_t(e) << 9 | uint32_t(ge) << 16 |
             uint32_t(v) << 28 | uint32_t(c) << 29 | uint32_t(z) << 30 | uint32_t(n) << 31;
    }

    inline auto operator=(uint32 data) -> PSR& {
      m = data.bit(0,4);
      t = data.bit(5);
      f = data.bit(6);
      i = data.bit(7);
      a = data.bit(8);
      e = data.bit(9);
      ge = data.bit(16,19);
      v = data.bit(28);
      c = data.bit(29);
      z = data.bit(30);
      n = data.bit(31);
      return *this;
    }

    //serialization.cpp
    auto serialize(serializer&) -> void;

    uint5 m;    //mode
    boolean t;  //thumb
    boolean f;  //fiq
    boolean i;  //irq
    boolean a;  //abort disable
    boolean e;  //endianness
    uint4 ge;    //ARMv6 greater-than-or-equal flags
    boolean v;  //overflow
    boolean c;  //carry
    boolean z;  //zero
    boolean n;  //negative
  };

  struct Processor {
    //serialization.cpp
    auto serialize(serializer&) -> void;

    GPR r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15;
    PSR cpsr;

    struct FIQ {
      GPR r8, r9, r10, r11, r12, r13, r14;
      PSR spsr;
    } fiq;

    struct IRQ {
      GPR r13, r14;
      PSR spsr;
    } irq;

    struct SVC {
      GPR r13, r14;
      PSR spsr;
    } svc;

    struct ABT {
      GPR r13, r14;
      PSR spsr;
    } abt;

    struct UND {
      GPR r13, r14;
      PSR spsr;
    } und;

    // VFP coprocessor 10/11 registers (ARM1136J-S has VFPv2)
    // Stored as raw IEEE 754 bit patterns; use memcpy to read/write float values.
    uint32_t s[32] = {};    // s0-s31 single-precision bit patterns
    uint32 fpscr = 0;       // Floating-Point Status and Control Register
  } processor;

  struct Pipeline {
    //serialization.cpp
    auto serialize(serializer&) -> void;

    struct Instruction {
      uint32 address;
      uint32 instruction;
      boolean thumb;  //not used by fetch stage
    };

    uint1 reload = 1;
    uint1 nonsequential = 1;
    Instruction fetch;
    Instruction decode;
    Instruction execute;
  } pipeline;

  uint32 opcode;
  boolean carry;
  boolean irq;

  using ArmHandler = void (ARM7TDMI::*)(uint32);
  using ThumbHandler = void (ARM7TDMI::*)(uint16);
  ArmHandler armInstruction[4096] = {};
  ThumbHandler thumbInstruction[65536] = {};

  //disassembler.cpp
  auto armDisassembleBranch(int24, uint1) -> string;
  auto armDisassembleBranchLinkExchangeRegister(uint4) -> string;
  auto armDisassembleBranchExchangeRegister(uint4) -> string;
  auto armDisassembleBranchExchangeJazelle(uint4) -> string;
  auto armDisassembleBreakpoint(uint16) -> string;
  auto armDisassembleChangeProcessorState(uint2 imod, uint1 M, uint1 A, uint1 I, uint1 F, uint5 mode) -> string;
  auto armDisassembleCountLeadingZeros(uint4, uint4) -> string;
  auto armDisassemblePreload() -> string;
  auto armDisassembleSetEnd(uint1 endian) -> string;
  auto armDisassembleSignedExtendByte(uint4, uint2, uint4, uint4) -> string;
  auto armDisassembleUnsignedExtendByte(uint4, uint2, uint4, uint4) -> string;
  auto armDisassembleSignedExtendHalfword(uint4, uint2, uint4, uint4) -> string;
  auto armDisassembleUnsignedExtendHalfword(uint4, uint2, uint4, uint4) -> string;
  auto armDisassembleByteReverseWord(uint4, uint4) -> string;
  auto armDisassembleByteReversePackedHalfword(uint4, uint4) -> string;
  auto armDisassembleByteReverseSignedHalfword(uint4, uint4) -> string;
  auto armDisassembleCoprocessorDataOperation(uint4, uint4, uint4, uint4, uint4, uint3) -> string;
  auto armDisassembleCoprocessorRegisterTransferLong(uint4, uint4, uint4, uint4, uint4, uint1) -> string;
  auto armDisassembleCoprocessorRegisterTransfer(uint4, uint4, uint4, uint4, uint4, uint3, uint1) -> string;
  auto armDisassembleDataImmediate(uint8, uint4, uint4, uint4, uint1, uint4) -> string;
  auto armDisassembleDataImmediateShift(uint4, uint2, uint5, uint4, uint4, uint1, uint4) -> string;
  auto armDisassembleDataRegisterShift(uint4, uint2, uint4, uint4, uint4, uint1, uint4) -> string;
  auto armDisassembleLoadImmediate(uint8, uint1, uint4, uint4, uint1, uint1, uint1) -> string;
  auto armDisassembleLoadRegister(uint4, uint1, uint4, uint4, uint1, uint1, uint1) -> string;
  auto armDisassembleMemorySwap(uint4, uint4, uint4, uint1) -> string;
  auto armDisassembleMoveDoubleImmediate(uint8, uint4, uint4, uint1, uint1, uint1, uint1) -> string;
  auto armDisassembleMoveDoubleRegister(uint4, uint4, uint4, uint1, uint1, uint1, uint1) -> string;
  auto armDisassembleMoveHalfImmediate(uint8, uint4, uint4, uint1, uint1, uint1, uint1) -> string;
  auto armDisassembleMoveHalfRegister(uint4, uint4, uint4, uint1, uint1, uint1, uint1) -> string;
  auto armDisassembleMoveImmediateOffset(uint12, uint4, uint4, uint1, uint1, uint1, uint1, uint1) -> string;
  auto armDisassembleMoveMultiple(uint16, uint4, uint1, uint1, uint1, uint1, uint1) -> string;
  auto armDisassembleMoveRegisterOffset(uint4, uint2, uint5, uint4, uint4, uint1, uint1, uint1, uint1, uint1) -> string;
  auto armDisassembleMoveToRegisterFromStatus(uint4, uint1) -> string;
  auto armDisassembleMoveToStatusFromImmediate(uint8, uint4, uint4, uint1) -> string;
  auto armDisassembleMoveToStatusFromRegister(uint4, uint4, uint1) -> string;
  auto armDisassembleMultiply(uint4, uint4, uint4, uint4, uint1, uint1) -> string;
  auto armDisassembleMultiplyLong(uint4, uint4, uint4, uint4, uint1, uint1, uint1) -> string;
  auto armDisassembleMultiply16(uint32) -> string;
  auto armDisassembleMultiply16Long(uint32) -> string;
  auto armDisassembleMultiply16Wide(uint32) -> string;
  auto armDisassembleSaturatingAdd(uint32) -> string;
  auto armDisassemblePackHalfword(uint32) -> string;
  auto armDisassembleSaturate(uint32) -> string;
  auto armDisassembleSelect(uint32) -> string;
  auto armDisassembleSIMD(uint32) -> string;
  auto armDisassembleSoftwareInterrupt(uint24) -> string;
  auto armDisassembleUndefined() -> string;

  auto thumbDisassembleALU(uint3, uint3, uint4) -> string;
  auto thumbDisassembleALUExtended(uint4, uint4, uint2) -> string;
  auto thumbDisassembleAddRegister(uint8, uint3, uint1) -> string;
  auto thumbDisassembleAdjustImmediate(uint3, uint3, uint3, uint1) -> string;
  auto thumbDisassembleAdjustRegister(uint3, uint3, uint3, uint1) -> string;
  auto thumbDisassembleAdjustStack(uint7, uint1) -> string;
  auto thumbDisassembleBranchExchange(uint4, uint1) -> string;
  auto thumbDisassembleBranchFarPrefix(int11) -> string;
  auto thumbDisassembleBranchFarSuffix(uint11) -> string;
  auto thumbDisassembleBranchFarSuffixExchange(uint11) -> string;
  auto thumbDisassembleBranchNear(int11) -> string;
  auto thumbDisassembleBranchTest(int8, uint4) -> string;
  auto thumbDisassembleImmediate(uint8, uint3, uint2) -> string;
  auto thumbDisassembleLoadLiteral(uint8, uint3) -> string;
  auto thumbDisassembleMoveByteImmediate(uint3, uint3, uint5, uint1) -> string;
  auto thumbDisassembleMoveHalfImmediate(uint3, uint3, uint5, uint1) -> string;
  auto thumbDisassembleMoveMultiple(uint8, uint3, uint1) -> string;
  auto thumbDisassembleMoveRegisterOffset(uint3, uint3, uint3, uint3) -> string;
  auto thumbDisassembleMoveStack(uint8, uint3, uint1) -> string;
  auto thumbDisassembleMoveWordImmediate(uint3, uint3, uint5, uint1) -> string;
  auto thumbDisassembleShiftImmediate(uint3, uint3, uint5, uint2) -> string;
  auto thumbDisassembleSoftwareInterrupt(uint8) -> string;
  auto thumbDisassembleStackMultiple(uint8, uint1, uint1) -> string;
  auto thumbDisassembleUndefined() -> string;

  function<string (uint32 opcode)> armDisassemble[4096];
  function<string ()> thumbDisassemble[65536];

  uint32 _pc;
  string _c;
};

}
