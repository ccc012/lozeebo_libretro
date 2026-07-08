namespace {

auto trace_suspicious_branch_enabled() -> bool {
  static const bool enabled = std::getenv("ZEEMU_TRACE_BRANCH_HIGH") != nullptr;
  return enabled;
}

auto is_suspicious_branch_target(uint32 address) -> bool {
  if(address == 0xdeadbee0u || address == 0xdeadbeefu) return false;
  return (address >= 0x50000000u && address < 0x54000000u) ||
         (address >= 0x1f000000u && address < 0x20000000u) ||
         (address >= 0x80000000u && address < 0xff000000u);
}

auto describe_exec_region(uint32 address) -> const char* {
  if(address >= 0xff000000u) return "hle";
  if(address >= 0x50000000u && address < 0x54000000u) return "guest-heap";
  if(address >= 0x1f000000u && address < 0x20000000u) return "guest-stack";
  if(address >= 0x10000000u && address < 0x11000000u) return "module";
  if(address < 0x01000000u) return "low-module/mirror";
  return "unknown";
}

}

auto ARM7TDMI::armALU(uint4 mode, uint4 d, uint4 n, uint32 rm) -> void {
  uint32 rn = r(n);

  switch(mode) {
  case  0: r(d) = BIT(rn & rm); break;  //AND
  case  1: r(d) = BIT(rn ^ rm); break;  //EOR
  case  2: r(d) = SUB(rn, rm, 1); break;  //SUB
  case  3: r(d) = SUB(rm, rn, 1); break;  //RSB
  case  4: r(d) = ADD(rn, rm, 0); break;  //ADD
  case  5: r(d) = ADD(rn, rm, cpsr().c); break;  //ADC
  case  6: r(d) = SUB(rn, rm, cpsr().c); break;  //SBC
  case  7: r(d) = SUB(rm, rn, cpsr().c); break;  //RSC
  case  8:        BIT(rn & rm); break;  //TST
  case  9:        BIT(rn ^ rm); break;  //TEQ
  case 10:        SUB(rn, rm, 1); break;  //CMP
  case 11:        ADD(rn, rm, 0); break;  //CMN
  case 12: r(d) = BIT(rn | rm); break;  //ORR
  case 13: r(d) = BIT(rm); break;  //MOV
  case 14: r(d) = BIT(rn & ~rm); break;  //BIC
  case 15: r(d) = BIT(~rm); break;  //MVN
  }

  if(exception() && d == 15 && opcode.bit(20)) {
    cpsr() = spsr();
  }
}

auto ARM7TDMI::armMoveToStatus(uint4 field, uint1 mode, uint32 data) -> void {
  if(mode && (cpsr().m == PSR::USR || cpsr().m == PSR::SYS)) return;
  PSR& psr = mode ? spsr() : cpsr();

  if(field.bit(0)) {
    if(mode || privileged()) {
      psr.m = data.bit(0,4);
      psr.t = data.bit(5);
      psr.f = data.bit(6);
      psr.i = data.bit(7);
      if(!mode && psr.t) r(15).data += 2;
    }
  }

  if(field.bit(3)) {
    psr.v = data.bit(28);
    psr.c = data.bit(29);
    psr.z = data.bit(30);
    psr.n = data.bit(31);
  }
}

//

auto ARM7TDMI::armInstructionBranch
(uint32 opcode) -> void {
  int24 displacement = opcode.bit(0,23);
  uint1 link = opcode.bit(24);
  if(opcode.bit(28,31) == 15) {
    r(14) = r(15) - 4;
    cpsr().t = 1;
    r(15) = r(15) + displacement * 4 + link * 2;
    return;
  }
  if(link) r(14) = r(15) - 4;
  r(15) = r(15) + displacement * 4;
}

auto ARM7TDMI::armInstructionBranchLinkExchangeRegister
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint32 address = translate_address(r(m));
  if(trace_suspicious_branch_enabled() && is_suspicious_branch_target(address)) {
    printf("[CPU_BRANCH_SUSPECT] kind=BLX pc=0x%08X opcode=0x%08X rm=r%u target=0x%08X region=%s raw=0x%08X lr=0x%08X r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X r12=0x%08X sp=0x%08X\n",
      (unsigned)pipeline.execute.address, (unsigned)opcode, (unsigned)m, (unsigned)address,
      describe_exec_region(address), (unsigned)r(m),
      (unsigned)r(14), (unsigned)r(0), (unsigned)r(1), (unsigned)r(2), (unsigned)r(3), (unsigned)r(12), (unsigned)r(13));
  }
  r(14) = pipeline.execute.address + 4;
  cpsr().t = address.bit(0);
  r(15) = address;
}

auto ARM7TDMI::armInstructionBranchExchangeRegister
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint32 address = translate_address(r(m));
  if(trace_suspicious_branch_enabled() && is_suspicious_branch_target(address)) {
    printf("[CPU_BRANCH_SUSPECT] kind=BX pc=0x%08X opcode=0x%08X rm=r%u target=0x%08X region=%s raw=0x%08X lr=0x%08X r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X r12=0x%08X sp=0x%08X\n",
      (unsigned)pipeline.execute.address, (unsigned)opcode, (unsigned)m, (unsigned)address,
      describe_exec_region(address), (unsigned)r(m),
      (unsigned)r(14), (unsigned)r(0), (unsigned)r(1), (unsigned)r(2), (unsigned)r(3), (unsigned)r(12), (unsigned)r(13));
  }
  cpsr().t = address.bit(0);
  r(15) = address;
}

auto ARM7TDMI::armInstructionBranchExchangeJazelle
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint32 address = translate_address(r(m));
  if(trace_suspicious_branch_enabled() && is_suspicious_branch_target(address)) {
    printf("[CPU_BRANCH_SUSPECT] kind=BXJ pc=0x%08X opcode=0x%08X rm=r%u target=0x%08X region=%s raw=0x%08X lr=0x%08X r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X r12=0x%08X sp=0x%08X\n",
      (unsigned)pipeline.execute.address, (unsigned)opcode, (unsigned)m, (unsigned)address,
      describe_exec_region(address), (unsigned)r(m),
      (unsigned)r(14), (unsigned)r(0), (unsigned)r(1), (unsigned)r(2), (unsigned)r(3), (unsigned)r(12), (unsigned)r(13));
  }
  cpsr().t = address.bit(0);
  r(15) = address;
}

auto ARM7TDMI::armInstructionBreakpoint
(uint32 opcode) -> void {
  uint16 immediate = opcode.bit(0,3) << 0 | opcode.bit(8,11) << 4 | opcode.bit(16,19) << 8 | opcode.bit(4,7) << 12;
  printf("[CPU] Breakpoint #0x%04X at PC=0x%08X\n", (unsigned)immediate, (unsigned)pipeline.execute.address);
  exception(PSR::UND, 0x04);
}

auto ARM7TDMI::armInstructionChangeProcessorState(uint32 opcode) -> void {
  uint2 imod = opcode.bit(18,19);
  uint1 M = opcode.bit(17);
  uint1 A = opcode.bit(8);
  uint1 I = opcode.bit(7);
  uint1 F = opcode.bit(6);
  uint5 mode = opcode.bit(0,4);
  if(imod == 2) { // IE
    if(A) cpsr().a = 0;
    if(I) cpsr().i = 0;
    if(F) cpsr().f = 0;
  }
  if(imod == 3) { // ID
    if(A) cpsr().a = 1;
    if(I) cpsr().i = 1;
    if(F) cpsr().f = 1;
  }
  if(M) {
    cpsr().m = mode;
  }
}

auto ARM7TDMI::armInstructionCountLeadingZeros
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint4 d = opcode.bit(12,15);
  uint32 value = r(m);
  uint32 count = 0;
  while(count < 32 && !value.bit(31 - count)) count = count + 1;
  r(d) = count;
}

auto ARM7TDMI::armInstructionPreload(uint32 opcode) -> void {
  (void)opcode;
  // NOP for now
}

auto ARM7TDMI::armInstructionSetEnd(uint32 opcode) -> void {
  uint1 endian = opcode.bit(9);
  cpsr().e = endian;
}

auto ARM7TDMI::armInstructionUnsignedExtendByte
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint2 rotate = opcode.bit(10,11);
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  uint32 value = r(m);
  uint32 shift = rotate * 8;
  if(shift) value = value >> shift | value << (32 - shift);
  uint32 res = value & 0xff;
  if(n != 15) res += r(n);
  r(d) = res;
}

auto ARM7TDMI::armInstructionSignedExtendByte
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint2 rotate = opcode.bit(10,11);
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  uint32 value = r(m);
  uint32 shift = rotate * 8;
  if(shift) value = value >> shift | value << (32 - shift);
  uint32 res = (int32)(int8_t)(value & 0xff);
  if(n != 15) res += r(n);
  r(d) = res;
}

auto ARM7TDMI::armInstructionUnsignedExtendHalfword
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint2 rotate = opcode.bit(10,11);
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  uint32 value = r(m);
  uint32 shift = rotate * 8;
  if(shift) value = value >> shift | value << (32 - shift);
  uint32 res = value & 0xffff;
  if(n != 15) res += r(n);
  r(d) = res;
}

auto ARM7TDMI::armInstructionSignedExtendHalfword
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint2 rotate = opcode.bit(10,11);
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  uint32 value = r(m);
  uint32 shift = rotate * 8;
  if(shift) value = value >> shift | value << (32 - shift);
  uint32 res = (int32)(int16_t)(value & 0xffff);
  if(n != 15) res += r(n);
  r(d) = res;
}

auto ARM7TDMI::armInstructionByteReverseWord(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint4 d = opcode.bit(12,15);
  uint32 value = r(m);
  r(d) = (value >> 24) | ((value >> 8) & 0x0000FF00) | ((value << 8) & 0x00FF0000) | (value << 24);
}

auto ARM7TDMI::armInstructionByteReversePackedHalfword(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint4 d = opcode.bit(12,15);
  uint32 value = r(m);
  r(d) = ((value >> 8) & 0x00FF00FF) | ((value << 8) & 0xFF00FF00);
}

auto ARM7TDMI::armInstructionByteReverseSignedHalfword(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint4 d = opcode.bit(12,15);
  uint32 value = r(m);
  uint32 reversed = ((value >> 8) & 0x000000FF) | ((value << 8) & 0x0000FF00);
  r(d) = (int32)(int16_t)reversed;
}

auto ARM7TDMI::armInstructionCoprocessorDataOperation
(uint32 opcode) -> void {
  uint4 CRm        = opcode.bit( 0, 3);
  uint4 coprocessor= opcode.bit( 8,11);
  uint4 CRd        = opcode.bit(12,15);
  uint4 CRn        = opcode.bit(16,19);
  bool  M          = opcode.bit(5);   // Fm register LSB / unary op variant
  bool  N          = opcode.bit(7);   // Fn register LSB / binary op variant
  bool  D          = opcode.bit(22);  // Fd register LSB (part of opc1)

  if(coprocessor != 10 && coprocessor != 11) return;

  // 5-bit VFP single-precision register numbers: {CRx, bit} → s0..s31
  // CP10 → s0..s15 base, CP11 → s16..s31 base
  unsigned base = (coprocessor == 10) ? 0u : 16u;
  unsigned sD = base + ((unsigned)CRd << 1 | (unsigned)D);
  unsigned sN = base + ((unsigned)CRn << 1 | (unsigned)N);
  unsigned sM = base + ((unsigned)CRm << 1 | (unsigned)M);
  if(sD >= 32u || sN >= 32u || sM >= 32u) return;

  // processor.s holds raw IEEE 754 bit patterns as uint32_t
  auto vget = [&](unsigned r) -> float {
    float v; memcpy(&v, &processor.s[r], 4); return v;
  };
  auto vset = [&](unsigned r, float v) {
    memcpy(&processor.s[r], &v, 4);
  };

  auto fpscr_cmp = [&](float l, float r) {
    uint32_t lu, ru; memcpy(&lu, &l, 4); memcpy(&ru, &r, 4);
    auto is_nan = [](uint32_t b){ return (b & 0x7f800000u) == 0x7f800000u && (b & 0x7fffffu); };
    processor.fpscr &= 0x0fffffffu;
    if(is_nan(lu) || is_nan(ru)) { processor.fpscr |= (1u<<29)|(1u<<28); return; } // Unordered: C+V
    if(l == r) { processor.fpscr |= (1u<<30)|(1u<<29); return; } // EQ: Z+C
    if(l >  r) { processor.fpscr |= (1u<<29); return; }          // GT: C
    processor.fpscr |= (1u<<31);                                  // LT: N
  };

  // Strip D (bit22) from opc1 to get the true 3-bit operation code.
  // D simultaneously selects the destination register LSB and is part of opc1[2].
  // true_op encodes: {bit23, bit21, bit20}
  unsigned true_op = ((unsigned)opcode.bit(23) << 2)
                   | ((unsigned)opcode.bit(21) << 1)
                   |  (unsigned)opcode.bit(20);

  if(true_op != 7u) {
    // Binary operations. N simultaneously selects Fn register LSB and op variant.
    float fn = vget(sN), fm = vget(sM), fd = vget(sD);
    switch(true_op) {
    case 0: vset(sD, N ? -(fd + fn*fm) : (fd + fn*fm)); break; // VMLA / VNMLA
    case 1: vset(sD, N ? (fn*fm - fd)  : (fd - fn*fm)); break; // VMLS / VNMLS
    case 2: vset(sD, N ? -(fn*fm)      :  fn*fm);       break; // VMUL / VNMUL
    case 3: vset(sD, N ? (fn - fm)     : (fn + fm));    break; // VADD / VSUB
    case 4: vset(sD, fn / fm);                          break; // VDIV
    default: break;
    }
  } else {
    // Unary operations (true_op==7, opc1 upper bits = 101x).
    // CRn field selects the specific operation; M selects variant and sM register LSB.
    switch((unsigned)CRn) {
    case 0x0: // VMOV (M=0) / VABS (M=1)
      if(!M) processor.s[sD] = processor.s[sM];
      else   processor.s[sD] = processor.s[sM] & 0x7fffffffu;
      break;
    case 0x1: // VNEG (M=0) / VSQRT (M=1)
      if(!M) { processor.s[sD] = processor.s[sM] ^ 0x80000000u; }
      else {
        float fm = vget(sM);
        if(fm < 0.0f) processor.s[sD] = 0x7fc00000u; // NaN
        else          vset(sD, sqrtf(fm));
      }
      break;
    case 0x4: // VCMP / VCMPE — compare Fd with Fm
      fpscr_cmp(vget(sD), vget(sM));
      break;
    case 0x5: // VCMPZ / VCMPZE — compare Fd with zero
      fpscr_cmp(vget(sD), 0.0f);
      break;
    case 0x8: { // VCVT.F32.U32 (M=0) / VCVT.F32.S32 (M=1) — integer → float
      uint32_t raw = processor.s[sM];
      vset(sD, M ? (float)(int32_t)raw : (float)(uint32_t)raw);
      break;
    }
    case 0xC: { // VCVT.U32.F32 — float → unsigned int (round toward zero)
      float f = vget(sM);
      uint32_t r = (f >= 0.0f) ? (uint32_t)f : 0u;
      processor.s[sD] = r;
      break;
    }
    case 0xD: { // VCVT.S32.F32 — float → signed int (round toward zero)
      int32_t r = (int32_t)vget(sM);
      memcpy(&processor.s[sD], &r, 4);
      break;
    }
    default: break;
    }
  }
}

auto ARM7TDMI::armInstructionCoprocessorRegisterTransferLong
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint4 coprocessor = opcode.bit(8,11);
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  uint4 opc = opcode.bit(21,23);
  uint1 load = opcode.bit(20);
  printf("[CPU] Coprocessor %u Long Transfer: %s p%u,#%u,r%u,r%u,c%u at PC=0x%08X\n",
         (unsigned)coprocessor, load ? "mrrc" : "mcrr", (unsigned)coprocessor, (unsigned)opc, (unsigned)d, (unsigned)n, (unsigned)m, (unsigned)pipeline.execute.address);

  if (coprocessor != 15) {
    return;
  }

  if (!load) {
    return;
  }

  uint64 value = 0;
  switch (m) {
  case 0:
    value = 0x4107B36000000000ull;
    break;
  case 1:
    value = 0x1D15215200000000ull;
    break;
  default:
    value = 0;
    break;
  }

  r(d) = uint32(value & 0xFFFFFFFFu);
  if (n != d) {
    r(n) = uint32(value >> 32);
  }
}

auto ARM7TDMI::armInstructionCoprocessorRegisterTransfer
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint4 coprocessor = opcode.bit(8,11);
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  uint4 opcode1 = opcode.bit(21,23);
  uint3 opcode2 = opcode.bit(5,7);
  uint1 load = opcode.bit(20);
  if(coprocessor != 15) {
    // VFP coprocessors 10/11: handle VMOV (register transfer)
    if(coprocessor == 10 || coprocessor == 11) {
      if(opcode1 == 0 && opcode2 == 0 && n == 1 && m == 0) {
        if(load) r(d) = processor.fpscr;
        else     processor.fpscr = r(d);
        return;
      }

      // Sn = {CRn, N} where N = bit7 is the register LSB
      bool   Nbit   = opcode.bit(7);
      unsigned base = (coprocessor == 10) ? 0u : 16u;
      unsigned sReg = base + ((unsigned)n << 1 | (unsigned)Nbit);
      if(sReg >= 32u) { if(load) r(d) = 0; return; }
      if(load) r(d) = processor.s[sReg];
      else     processor.s[sReg] = (uint32_t)r(d);
      return;
    }
    if(load) r(d) = 0;
    return;
  }

  if(!load) {
    if(n == 1 && m == 0 && opcode1 == 0) {
        if(opcode2 == 0) {
            // Control Register
            printf("[CPU] CP15: MCR p15,0,r%u,c1,c0,0 (Control = 0x%08X)\n", (unsigned)d, (unsigned)r(d));
        } else if(opcode2 == 2) {
            // CPACR
            printf("[CPU] CP15: MCR p15,0,r%u,c1,c0,2 (CPACR = 0x%08X)\n", (unsigned)d, (unsigned)r(d));
        }
    }
    return;
  }

  uint32 value = 0;
  if(n == 0 && m == 0 && opcode1 == 0) {
    switch(uint32(opcode2)) {
    case 0: value = 0x4107B360u; break;  // Main ID: ARM1136 r3p0 class.
    case 1: value = 0x1D152152u; break;  // Cache type: plausible ARM1136-style split cache layout.
    case 2: value = 0x0000000Fu; break;  // TCM status: no TCM regions exposed.
    default: value = 0; break;
    }
  } else if(n == 1 && m == 0 && opcode1 == 0) {
    if(opcode2 == 0) {
        // Control Register
        // Bit 0: MMU, Bit 2: Cache, Bit 12: I-cache
        value = 0x00050078; 
    } else if(opcode2 == 2) {
        // CPACR (Coprocessor Access Control Register)
        // Bit 20-23: Access to p10/p11 (VFP)
        value = 0x00F00000;
    }
  }
  r(d) = value;
}

auto ARM7TDMI::armInstructionDataImmediate
(uint32 opcode) -> void {
  uint8 immediate = opcode.bit(0,7);
  uint4 shift = opcode.bit(8,11);
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  uint1 save = opcode.bit(20);
  uint4 mode = opcode.bit(21,24);
  (void)save;
  uint32 data = immediate;
  carry = cpsr().c;
  if(shift) data = ROR(data, shift << 1);
  armALU(mode, d, n, data);
}

auto ARM7TDMI::armInstructionDataImmediateShift
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint2 type = opcode.bit(5,6);
  uint5 shift = opcode.bit(7,11);
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  uint1 save = opcode.bit(20);
  uint4 mode = opcode.bit(21,24);
  uint32 rm = r(m);
  (void)save;
  carry = cpsr().c;

  switch(type) {
  case 0: rm = LSL(rm, shift); break;
  case 1: rm = LSR(rm, shift ? (uint)shift : 32); break;
  case 2: rm = ASR(rm, shift ? (uint)shift : 32); break;
  case 3: rm = shift ? ROR(rm, shift) : RRX(rm); break;
  }

  armALU(mode, d, n, rm);
}

auto ARM7TDMI::armInstructionDataRegisterShift
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint2 type = opcode.bit(5,6);
  uint4 s = opcode.bit(8,11);
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  uint1 save = opcode.bit(20);
  uint4 mode = opcode.bit(21,24);
  uint8 rs = r(s) + (s == 15 ? 4 : 0);
  uint32 rm = r(m) + (m == 15 ? 4 : 0);
  (void)save;
  carry = cpsr().c;

  switch(type) {
  case 0: rm = LSL(rm, rs < 33 ? rs : (uint8)33); break;
  case 1: rm = LSR(rm, rs < 33 ? rs : (uint8)33); break;
  case 2: rm = ASR(rm, rs < 32 ? rs : (uint8)32); break;
  case 3: if(rs) rm = ROR(rm, rs & 31 ? uint(rs & 31) : 32); break;
  }

  armALU(mode, d, n, rm);
}

auto ARM7TDMI::armInstructionLoadImmediate
(uint32 opcode) -> void {
  uint8 immediate = opcode.bit(0,3) << 0 | opcode.bit(8,11) << 4;
  uint1 half = opcode.bit(5);
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  uint1 writeback = opcode.bit(21);
  uint1 up = opcode.bit(23);
  uint1 pre = opcode.bit(24);
  uint32 rn = r(n);
  uint32 rd = r(d);

  if(pre == 1) rn = up ? rn + immediate : rn - immediate;
  rd = load((half ? Half : Byte) | Nonsequential | Signed, rn);
  if(pre == 0) rn = up ? rn + immediate : rn - immediate;

  if(pre == 0 || writeback) r(n) = rn;
  r(d) = rd;
}

auto ARM7TDMI::armInstructionLoadRegister
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint1 half = opcode.bit(5);
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  uint1 writeback = opcode.bit(21);
  uint1 up = opcode.bit(23);
  uint1 pre = opcode.bit(24);
  uint32 rn = r(n);
  uint32 rm = r(m);
  uint32 rd = r(d);

  if(pre == 1) rn = up ? rn + rm : rn - rm;
  rd = load((half ? Half : Byte) | Nonsequential | Signed, rn);
  if(pre == 0) rn = up ? rn + rm : rn - rm;

  if(pre == 0 || writeback) r(n) = rn;
  r(d) = rd;
}

auto ARM7TDMI::armInstructionMemorySwap
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  uint1 byte = opcode.bit(22);
  uint32 word = load((byte ? Byte : Word) | Nonsequential, r(n));
  store((byte ? Byte : Word) | Nonsequential, r(n), r(m));
  r(d) = word;
}

auto ARM7TDMI::armInstructionMoveDoubleImmediate
(uint32 opcode) -> void {
  uint8 immediate = opcode.bit(0,3) << 0 | opcode.bit(8,11) << 4;
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  // ARM doubleword transfers encode LDRD/STRD in bits[7:4]:
  // ...1101 is LDRD and ...1111 is STRD. Bit 20 belongs to the halfword
  // transfer family and using it here turns valid LDRD opcodes into STRD.
  uint1 mode = opcode.bit(5) == 0;
  uint1 writeback = opcode.bit(21);
  uint1 up = opcode.bit(23);
  uint1 pre = opcode.bit(24);
  uint32 rn = r(n);
  if(pre == 1) rn = up ? rn + immediate : rn - immediate;

  if(mode == 1) {
    r(d) = load(Word | Nonsequential, rn);
    r(d + 1) = load(Word | Sequential, rn + 4);
    idle();
  } else {
    store(Word | Nonsequential, rn, r(d));
    store(Word | Sequential, rn + 4, r(d + 1));
  }

  if(pre == 0) rn = up ? rn + immediate : rn - immediate;
  if(pre == 0 || writeback) r(n) = rn;
}

auto ARM7TDMI::armInstructionMoveDoubleRegister
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  // ARM doubleword transfers encode LDRD/STRD in bits[7:4]:
  // ...1101 is LDRD and ...1111 is STRD. Bit 20 belongs to the halfword
  // transfer family and using it here turns valid LDRD opcodes into STRD.
  uint1 mode = opcode.bit(5) == 0;
  uint1 writeback = opcode.bit(21);
  uint1 up = opcode.bit(23);
  uint1 pre = opcode.bit(24);
  uint32 rn = r(n);
  uint32 rm = r(m);
  if(pre == 1) rn = up ? rn + rm : rn - rm;

  if(mode == 1) {
    r(d) = load(Word | Nonsequential, rn);
    r(d + 1) = load(Word | Sequential, rn + 4);
    idle();
  } else {
    store(Word | Nonsequential, rn, r(d));
    store(Word | Sequential, rn + 4, r(d + 1));
  }

  if(pre == 0) rn = up ? rn + rm : rn - rm;
  if(pre == 0 || writeback) r(n) = rn;
}

auto ARM7TDMI::armInstructionMoveHalfImmediate
(uint32 opcode) -> void {
  uint8 immediate = opcode.bit(0,3) << 0 | opcode.bit(8,11) << 4;
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  uint1 mode = opcode.bit(20);
  uint1 writeback = opcode.bit(21);
  uint1 up = opcode.bit(23);
  uint1 pre = opcode.bit(24);
  uint32 rn = r(n);
  uint32 rd = r(d);

  if(pre == 1) rn = up ? rn + immediate : rn - immediate;
  if(mode == 1) rd = load(Half | Nonsequential, rn);
  if(mode == 0) store(Half | Nonsequential, rn, rd);
  if(pre == 0) rn = up ? rn + immediate : rn - immediate;

  if(pre == 0 || writeback) r(n) = rn;
  if(mode == 1) r(d) = rd;
}

auto ARM7TDMI::armInstructionMoveHalfRegister
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  uint1 mode = opcode.bit(20);
  uint1 writeback = opcode.bit(21);
  uint1 up = opcode.bit(23);
  uint1 pre = opcode.bit(24);
  uint32 rn = r(n);
  uint32 rm = r(m);
  uint32 rd = r(d);

  if(pre == 1) rn = up ? rn + rm : rn - rm;
  if(mode == 1) rd = load(Half | Nonsequential, rn);
  if(mode == 0) store(Half | Nonsequential, rn, rd);
  if(pre == 0) rn = up ? rn + rm : rn - rm;

  if(pre == 0 || writeback) r(n) = rn;
  if(mode == 1) r(d) = rd;
}

auto ARM7TDMI::armInstructionMoveImmediateOffset
(uint32 opcode) -> void {
  uint12 immediate = opcode.bit(0,11);
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  uint1 mode = opcode.bit(20);
  uint1 writeback = opcode.bit(21);
  uint1 byte = opcode.bit(22);
  uint1 up = opcode.bit(23);
  uint1 pre = opcode.bit(24);
  uint32 rn = r(n);
  uint32 rd = r(d);

  if(pre == 1) rn = up ? rn + immediate : rn - immediate;
  if(mode == 1) rd = load((byte ? Byte : Word) | Nonsequential, rn);
  if(mode == 0) store((byte ? Byte : Word) | Nonsequential, rn, rd);
  if(pre == 0) rn = up ? rn + immediate : rn - immediate;

  if(pre == 0 || writeback) r(n) = rn;
  if(mode == 1) r(d) = rd;
}

auto ARM7TDMI::armInstructionMoveMultiple
(uint32 opcode) -> void {
  uint16 list = opcode.bit(0,15);
  uint4 n = opcode.bit(16,19);
  uint1 mode = opcode.bit(20);
  uint1 writeback = opcode.bit(21);
  uint1 type = opcode.bit(22);
  uint1 up = opcode.bit(23);
  uint1 pre = opcode.bit(24);
  uint32 rn = r(n);
  if(pre == 0 && up == 1) rn = rn + 0;  //IA
  if(pre == 1 && up == 1) rn = rn + 4;  //IB
  if(pre == 1 && up == 0) rn = rn - bit::count(list) * 4 + 0;  //DB
  if(pre == 0 && up == 0) rn = rn - bit::count(list) * 4 + 4;  //DA

  if(writeback && mode == 1) {
    if(up == 1) r(n) = r(n) + bit::count(list) * 4;  //IA,IB
    if(up == 0) r(n) = r(n) - bit::count(list) * 4;  //DA,DB
  }

  auto cpsrMode = cpsr().m;
  bool usr = false;
  if(type && mode == 1 && !list.bit(15)) usr = true;
  if(type && mode == 0) usr = true;
  if(usr) cpsr().m = PSR::USR;

  uint sequential = Nonsequential;
  for(uint m : range(16)) {
    if(!list.bit(m)) continue;
    if(mode == 1) r(m) = read(Word | sequential, rn);
    if(mode == 0) write(Word | sequential, rn, r(m));
    rn += 4;
    sequential = Sequential;
  }

  if(usr) cpsr().m = cpsrMode;

  if(mode) {
    idle();
    if(type && list.bit(15) && cpsr().m != PSR::USR && cpsr().m != PSR::SYS) {
      cpsr() = spsr();
    }
  } else {
    pipeline.nonsequential = true;
  }

  if(writeback && mode == 0) {
    if(up == 1) r(n) = r(n) + bit::count(list) * 4;  //IA,IB
    if(up == 0) r(n) = r(n) - bit::count(list) * 4;  //DA,DB
  }
}

auto ARM7TDMI::armInstructionMoveRegisterOffset
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint2 type = opcode.bit(5,6);
  uint5 shift = opcode.bit(7,11);
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  uint1 mode = opcode.bit(20);
  uint1 writeback = opcode.bit(21);
  uint1 byte = opcode.bit(22);
  uint1 up = opcode.bit(23);
  uint1 pre = opcode.bit(24);
  if(n == 15 && mode == 1 && writeback == 1 && byte == 1 && up == 1 && pre == 0 && type == 3 && (shift & 7) == 0) {
    return armInstructionUnsignedExtendHalfword(opcode);
  }

  uint32 rm = r(m);
  uint32 rd = r(d);
  uint32 rn = r(n);
  carry = cpsr().c;

  switch(type) {
  case 0: rm = LSL(rm, shift); break;
  case 1: rm = LSR(rm, shift ? (uint)shift : 32); break;
  case 2: rm = ASR(rm, shift ? (uint)shift : 32); break;
  case 3: rm = shift ? ROR(rm, shift) : RRX(rm); break;
  }

  if(pre == 1) rn = up ? rn + rm : rn - rm;
  if(mode == 1) rd = load((byte ? Byte : Word) | Nonsequential, rn);
  if(mode == 0) store((byte ? Byte : Word) | Nonsequential, rn, rd);
  if(pre == 0) rn = up ? rn + rm : rn - rm;

  if(pre == 0 || writeback) r(n) = rn;
  if(mode == 1) r(d) = rd;
}

auto ARM7TDMI::armInstructionMoveToRegisterFromStatus
(uint32 opcode) -> void {
  uint4 d = opcode.bit(12,15);
  uint1 mode = opcode.bit(22);
  if(mode && (cpsr().m == PSR::USR || cpsr().m == PSR::SYS)) return;
  r(d) = mode ? spsr() : cpsr();
}

auto ARM7TDMI::armInstructionMoveToStatusFromImmediate
(uint32 opcode) -> void {
  uint8 immediate = opcode.bit(0,7);
  uint4 rotate = opcode.bit(8,11);
  uint4 field = opcode.bit(16,19);
  uint1 mode = opcode.bit(22);
  uint32 data = immediate;
  if(rotate) data = ROR(data, rotate << 1);
  armMoveToStatus(field, mode, data);
}

auto ARM7TDMI::armInstructionMoveToStatusFromRegister
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint4 field = opcode.bit(16,19);
  uint1 mode = opcode.bit(22);
  armMoveToStatus(field, mode, r(m));
}

auto ARM7TDMI::armInstructionMultiply
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint4 s = opcode.bit(8,11);
  uint4 n = opcode.bit(12,15);
  uint4 d = opcode.bit(16,19);
  uint1 save = opcode.bit(20);
  uint1 accumulate = opcode.bit(21);
  (void)save;
  if(accumulate) idle();
  r(d) = MUL(accumulate ? r(n) : 0, r(m), r(s));
}

auto ARM7TDMI::armInstructionMultiplyLong
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint4 s = opcode.bit(8,11);
  uint4 l = opcode.bit(12,15);
  uint4 h = opcode.bit(16,19);
  uint1 save = opcode.bit(20);
  uint1 accumulate = opcode.bit(21);
  uint1 sign = opcode.bit(22);
  const uint32_t rm = r(m);
  const uint32_t rs = r(s);

  idle();
  idle();
  if(accumulate) idle();

  if(sign) {
    // Signed multiply: check each byte of Rs for non-zero timing
    if((rs >>  8) & 0xFF) idle();
    if((rs >> 16) & 0xFF) idle();
    if((rs >> 24) & 0xFF) idle();
  } else {
    // Unsigned multiply: check each byte of Rs for non-zero timing
    if((rs >>  8) & 0xFF) idle();
    if((rs >> 16) & 0xFF) idle();
    if((rs >> 24) & 0xFF) idle();
  }

  uint64_t rd = sign
      ? static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(rm)) *
                              static_cast<int64_t>(static_cast<int32_t>(rs)))
      : static_cast<uint64_t>(rm) * static_cast<uint64_t>(rs);
  if(accumulate) {
    rd += (static_cast<uint64_t>(static_cast<uint32_t>(r(h))) << 32) |
          static_cast<uint32_t>(r(l));
  }

  r(h) = rd >> 32;
  r(l) = rd >>  0;

  if(save) {
    cpsr().z = rd == 0;
    cpsr().n = (rd >> 63) & 1u;
  }
}

// ---------------------------------------------------------------------------
// DSP multiplies (ARMv5E)
// ---------------------------------------------------------------------------

// SMLA<xy> / SMUL<xy> — 16×16 multiply (accumulate)
// x selects top/bottom half of Rm, y selects top/bottom half of Rs
auto ARM7TDMI::armInstructionMultiply16
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  bool  X = opcode.bit(5);  // 0=bottom, 1=top halfword of Rm
  bool  Y = opcode.bit(6);  // 0=bottom, 1=top halfword of Rs
  uint4 s = opcode.bit(8,11);
  uint4 n = opcode.bit(12,15);
  uint4 d = opcode.bit(16,19);
  bool  accumulate = !opcode.bit(22); // SMLA (bit22=0) vs SMUL (bit22=1)

  int16_t rm16 = X ? (int16_t)(r(m) >> 16) : (int16_t)(uint16_t)r(m);
  int16_t rs16 = Y ? (int16_t)(r(s) >> 16) : (int16_t)(uint16_t)r(s);
  int32_t product = (int32_t)rm16 * (int32_t)rs16;

  if(accumulate) {
    int64_t result = (int64_t)(int32_t)r(n) + product;
    r(d) = (uint32_t)result;
    // Q flag: set on overflow
    if(result > INT32_MAX || result < INT32_MIN) processor.cpsr.v = 1;
  } else {
    r(d) = (uint32_t)product;
  }
}

// SMLAL<xy> — 16×16 multiply accumulate long (64-bit)
auto ARM7TDMI::armInstructionMultiply16Long
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  bool  X = opcode.bit(5);
  bool  Y = opcode.bit(6);
  uint4 s = opcode.bit(8,11);
  uint4 l = opcode.bit(12,15);  // RdLo
  uint4 h = opcode.bit(16,19);  // RdHi

  int16_t rm16 = X ? (int16_t)(r(m) >> 16) : (int16_t)(uint16_t)r(m);
  int16_t rs16 = Y ? (int16_t)(r(s) >> 16) : (int16_t)(uint16_t)r(s);
  int64_t product = (int64_t)rm16 * (int64_t)rs16;
  int64_t acc     = ((int64_t)(uint64_t)r(h) << 32) | (uint64_t)r(l);
  int64_t result  = acc + product;
  r(h) = (uint32_t)((uint64_t)result >> 32);
  r(l) = (uint32_t)result;
}

// SMLAW<y> / SMULW<y> — 32×16 multiply (wide), result is top 32 bits
auto ARM7TDMI::armInstructionMultiply16Wide
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  bool  Y = opcode.bit(6);
  uint4 s = opcode.bit(8,11);
  uint4 n = opcode.bit(12,15);
  uint4 d = opcode.bit(16,19);
  bool  accumulate = !opcode.bit(5); // SMLAW (bit5=0) vs SMULW (bit5=1)

  int32_t rm32 = (int32_t)r(m);
  int16_t rs16 = Y ? (int16_t)(r(s) >> 16) : (int16_t)(uint16_t)r(s);
  int64_t product = ((int64_t)rm32 * rs16) >> 16;

  if(accumulate) {
    int64_t result = (int64_t)(int32_t)r(n) + product;
    r(d) = (uint32_t)result;
    if(result > INT32_MAX || result < INT32_MIN) processor.cpsr.v = 1;
  } else {
    r(d) = (uint32_t)product;
  }
}

// QADD / QSUB / QDADD / QDSUB — saturating add/sub
auto ARM7TDMI::armInstructionSaturatingAdd
(uint32 opcode) -> void {
  uint4 m  = opcode.bit(0,3);
  uint4 d  = opcode.bit(12,15);
  uint4 n  = opcode.bit(16,19);
  uint2 op = opcode.bit(21,22); // 0=QADD 1=QSUB 2=QDADD 3=QDSUB

  auto sat32 = [&](int64_t v) -> int32_t {
    if(v > INT32_MAX) { processor.cpsr.v = 1; return INT32_MAX; }
    if(v < INT32_MIN) { processor.cpsr.v = 1; return INT32_MIN; }
    return (int32_t)v;
  };

  int32_t rm = (int32_t)r(m);
  int32_t rn = (int32_t)r(n);
  int32_t doubled_rn = sat32((int64_t)rn * 2);

  switch((unsigned)op) {
  case 0: r(d) = (uint32_t)sat32((int64_t)rm + rn);         break; // QADD
  case 1: r(d) = (uint32_t)sat32((int64_t)rm - rn);         break; // QSUB
  case 2: r(d) = (uint32_t)sat32((int64_t)rm + doubled_rn); break; // QDADD
  case 3: r(d) = (uint32_t)sat32((int64_t)rm - doubled_rn); break; // QDSUB
  }
}

// ---------------------------------------------------------------------------
// ARMv6 — pack, saturate, select, SIMD
// ---------------------------------------------------------------------------

// PKHBT / PKHTB — pack halfword
auto ARM7TDMI::armInstructionPackHalfword
(uint32 opcode) -> void {
  uint4 m     = opcode.bit(0,3);
  bool  TB    = opcode.bit(6);  // 0=PKHBT, 1=PKHTB
  uint5 shift = opcode.bit(7,11);
  uint4 d     = opcode.bit(12,15);
  uint4 n     = opcode.bit(16,19);
  uint32_t rm = r(m), rn = r(n);

  if(!TB) { // PKHBT: bottom half from Rn, top half from Rm LSL shift
    uint32_t shifted = shift ? (rm << shift) : rm;
    r(d) = (shifted & 0xffff0000u) | (rn & 0x0000ffffu);
  } else {  // PKHTB: top half from Rn, bottom half from Rm ASR shift
    uint32_t shifted = shift ? ((int32_t)rm >> shift) : (int32_t)rm;
    r(d) = (rn & 0xffff0000u) | (shifted & 0x0000ffffu);
  }
}

// SSAT / USAT — signed/unsigned saturate
auto ARM7TDMI::armInstructionSaturate
(uint32 opcode) -> void {
  uint4 m       = opcode.bit(0,3);
  bool  sh      = opcode.bit(6);    // shift type: 0=LSL, 1=ASR
  uint5 shift   = opcode.bit(7,11);
  uint4 d       = opcode.bit(12,15);
  uint5 sat_imm = opcode.bit(16,20);
  bool  unsign  = opcode.bit(22);   // 0=SSAT, 1=USAT

  int32_t operand = (int32_t)r(m);
  if(shift) operand = sh ? (operand >> shift) : (operand << shift);

  if(!unsign) { // SSAT — saturate to signed N-bit range
    int32_t lo = -(1 << sat_imm), hi = (1 << sat_imm) - 1;
    if(operand > hi)      { r(d) = (uint32_t)hi; processor.cpsr.v = 1; }
    else if(operand < lo) { r(d) = (uint32_t)lo; processor.cpsr.v = 1; }
    else                  { r(d) = (uint32_t)operand; }
  } else { // USAT — saturate to unsigned N-bit range
    uint32_t hi = (1u << sat_imm) - 1u;
    if(operand < 0)                  { r(d) = 0;  processor.cpsr.v = 1; }
    else if((uint32_t)operand > hi)  { r(d) = hi; processor.cpsr.v = 1; }
    else                             { r(d) = (uint32_t)operand; }
  }
}

// SEL — select bytes using GE flags (CPSR bits 19:16)
auto ARM7TDMI::armInstructionSelect
(uint32 opcode) -> void {
  uint4 m = opcode.bit(0,3);
  uint4 d = opcode.bit(12,15);
  uint4 n = opcode.bit(16,19);
  uint32_t rm = r(m), rn = r(n);
  uint32_t result = 0;
  for(int i = 0; i < 4; i++) {
    bool sel = processor.cpsr.ge.bit(i);
    uint32_t byte = sel ? ((rn >> (i*8)) & 0xffu) : ((rm >> (i*8)) & 0xffu);
    result |= byte << (i*8);
  }
  r(d) = result;
}

// SIMD 8/16-bit add/sub — SADD8, SSUB8, UADD8, USUB8, SADD16, SSUB16, UADD16, USUB16
auto ARM7TDMI::armInstructionSIMD
(uint32 opcode) -> void {
  uint4 m    = opcode.bit(0,3);
  uint3 op   = opcode.bit(5,7);   // operation selector within family
  uint4 d    = opcode.bit(12,15);
  uint4 n    = opcode.bit(16,19);
  uint4 pre  = opcode.bit(20,23); // 0x61=S*, 0x65=U* (signed vs unsigned)
  bool unsign = (pre == 0x5);     // 0x61 prefix → signed, 0x65 → unsigned

  uint32_t rm = r(m), rn = r(n);
  uint32_t result = 0;
  uint4 ge = 0;

  if(op == 0x1 || op == 0x5) {
    // SADD16 (op=1) / SSUB16 (op=7) / UADD16 / USUB16 — 16-bit lanes
    bool sub = (op == 0x5);
    for(int i = 0; i < 2; i++) {
      uint32_t au = (rn >> (i*16)) & 0xffffu;
      uint32_t bu = (rm >> (i*16)) & 0xffffu;
      int32_t as = (int16_t)au;
      int32_t bs = (int16_t)bu;
      int32_t rs = sub ? as - bs : as + bs;
      uint32_t ru = sub ? au - bu : au + bu;
      result |= (uint32_t)(uint16_t)(unsign ? ru : rs) << (i*16);

      bool lane_ge = unsign ? (sub ? au >= bu : ru >= 0x10000u)
                            : (rs >= 0);
      if(lane_ge) ge |= (uint4)(3 << (i * 2));
    }
  } else if(op == 0x2 || op == 0x6) {
    // SASX (op=2) / SSAX (op=6) / UASX / USAX:
    // exchange Rm halfwords, then mix one add and one subtract.
    const uint32_t rn_lo = rn & 0xffffu;
    const uint32_t rn_hi = (rn >> 16) & 0xffffu;
    const uint32_t rm_lo = rm & 0xffffu;
    const uint32_t rm_hi = (rm >> 16) & 0xffffu;
    const int32_t rn_lo_s = (int16_t)rn_lo;
    const int32_t rn_hi_s = (int16_t)rn_hi;
    const int32_t rm_lo_s = (int16_t)rm_lo;
    const int32_t rm_hi_s = (int16_t)rm_hi;

    int32_t low_s, high_s;
    uint32_t low_u, high_u;
    bool low_add, high_add;
    if(op == 0x2) { // ASX: low=sub, high=add
      low_s = rn_lo_s - rm_hi_s;
      high_s = rn_hi_s + rm_lo_s;
      low_u = rn_lo - rm_hi;
      high_u = rn_hi + rm_lo;
      low_add = false;
      high_add = true;
    } else { // SAX: low=add, high=sub
      low_s = rn_lo_s + rm_hi_s;
      high_s = rn_hi_s - rm_lo_s;
      low_u = rn_lo + rm_hi;
      high_u = rn_hi - rm_lo;
      low_add = true;
      high_add = false;
    }

    result = (uint32_t)(uint16_t)(unsign ? low_u : low_s)
           | ((uint32_t)(uint16_t)(unsign ? high_u : high_s) << 16);

    bool low_ge = unsign ? (low_add ? low_u >= 0x10000u : rn_lo >= rm_hi)
                         : (low_s >= 0);
    bool high_ge = unsign ? (high_add ? high_u >= 0x10000u : rn_hi >= rm_lo)
                          : (high_s >= 0);
    if(low_ge) ge |= 0x3;
    if(high_ge) ge |= 0xc;
  } else if(op == 0x3 || op == 0x7) {
    // SADD8 (op=3) / SSUB8 (op=7) / UADD8 / USUB8
    bool sub = (op == 0x7);
    for(int i = 0; i < 4; i++) {
      uint32_t au = (rn >> (i*8)) & 0xffu;
      uint32_t bu = (rm >> (i*8)) & 0xffu;
      int32_t as = (int8_t)au;
      int32_t bs = (int8_t)bu;
      int32_t rs = sub ? as - bs : as + bs;
      uint32_t ru = sub ? au - bu : au + bu;
      result |= (uint32_t)(uint8_t)(unsign ? ru : rs) << (i*8);

      bool lane_ge = unsign ? (sub ? au >= bu : ru >= 0x100u)
                            : (rs >= 0);
      if(lane_ge) ge |= (uint4)(1 << i);
    }
  } else {
    armInstructionUndefined(opcode);
    return;
  }
  processor.cpsr.ge = ge;
  r(d) = result;
}

auto ARM7TDMI::armInstructionSoftwareInterrupt
(uint32 opcode) -> void {
  uint24 immediate = opcode.bit(0,23);
  if (uint32_t(immediate) >= 0x100) {
     // Log HLE calls that are not in the low range (usually hooks are 0x00..0xFF)
  }
  softwareInterrupt(uint32_t(immediate), uint32_t(pipeline.decode.address));
}

auto ARM7TDMI::armInstructionUndefined
(uint32 opcode) -> void {
  // Coprocessor load/store (STC/LDC) — bits 27:25 = 110
  if(opcode.bit(25, 27) == 6) {
      uint32 coprocessor = opcode.bit(8, 11);
      uint32 d = opcode.bit(12, 15);
      uint32 n = opcode.bit(16, 19);
      uint32 raw_imm8 = opcode.bit(0, 7);
      uint32 offset = raw_imm8 << 2;
      bool pre = opcode.bit(24);
      bool up = opcode.bit(23);
      bool writeback = opcode.bit(21);
      bool is_load = opcode.bit(20);

      uint32 address = r(n);
      if(pre) {
        address = up ? address + offset : address - offset;
      }

      // VFP coprocessors 10 and 11
      if(coprocessor == 10 || coprocessor == 11) {
        // writeback=1 → FLDMS/FSTMS (multiple regs, imm8=count)
        // writeback=0 → FLDS/FSTS (single reg, imm8=address offset already applied)
        unsigned count = writeback ? (unsigned)raw_imm8 : 1u;
        unsigned vfp_base = (coprocessor == 10) ? 0u : 16u;
        unsigned vfp_reg = vfp_base + (unsigned)d;
        unsigned sequential = Nonsequential;
        for(unsigned i = 0; i < count && (vfp_reg + i) < 32u; i++) {
          uint32 addr = address + i * 4;
          if(is_load) {
            uint32_t bits = (uint32_t)read(Word | sequential, addr);
            memcpy(&processor.s[vfp_reg + i], &bits, 4);
          } else {
            uint32_t bits;
            memcpy(&bits, &processor.s[vfp_reg + i], 4);
            write(Word | sequential, addr, (uint32_t)bits);
          }
          sequential = Sequential;
        }
      } else {
        // Generic coprocessor: use ARM GPR as before
        if(is_load) {
          uint32 value = read(Word | Nonsequential, address);
          r(d) = value;
        } else {
          write(Word | Nonsequential, address, r(d));
        }
      }

    if(!pre) {
      address = up ? address + offset : address - offset;
    }
    if(writeback || !pre) {
      r(n) = address;
    }
    return;
  }

  uint4 mode = opcode.bit(21, 24);
  if(opcode.bit(27, 26) == 0 && mode >= 8 && mode <= 11 && !opcode.bit(20)) {
    if(opcode.bit(25)) {
      armInstructionDataImmediateShift(opcode);
    } else {
      armInstructionDataRegisterShift(opcode);
    }
    return;
  }

  printf("[CPU] Undefined ARM Instruction 0x%08X at PC=0x%08X region=%s CPSR=0x%08X LR=0x%08X SP=0x%08X\n",
    (unsigned)opcode,
    (unsigned)pipeline.execute.address,
    describe_exec_region(pipeline.execute.address),
    (unsigned)(uint32)cpsr(),
    (unsigned)(uint32)r(14),
    (unsigned)(uint32)r(13));
  printf("[CPU] Undefined ARM regs: R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X R4=0x%08X R5=0x%08X R6=0x%08X R7=0x%08X R8=0x%08X R9=0x%08X R10=0x%08X R11=0x%08X R12=0x%08X\n",
    (unsigned)(uint32)r(0),
    (unsigned)(uint32)r(1),
    (unsigned)(uint32)r(2),
    (unsigned)(uint32)r(3),
    (unsigned)(uint32)r(4),
    (unsigned)(uint32)r(5),
    (unsigned)(uint32)r(6),
    (unsigned)(uint32)r(7),
    (unsigned)(uint32)r(8),
    (unsigned)(uint32)r(9),
    (unsigned)(uint32)r(10),
    (unsigned)(uint32)r(11),
    (unsigned)(uint32)r(12));
  auto should_dump_guest_words = [](uint32 address) -> bool {
    return (address >= 0x50000000u && address < 0x54000000u) ||
           (address >= 0x1f000000u && address < 0x20000000u);
  };
  auto dump_guest_words = [&](const char* label, uint32 center) {
    if(!should_dump_guest_words(center)) return;
    const uint32 base = center - 16u;
    printf("[CPU] Undefined ARM %s memory:", label);
    unsigned sequential = Nonsequential;
    for(unsigned i = 0; i < 9; i++) {
      const uint32 address = base + i * 4u;
      const uint32 value = read(Word | sequential, address);
      printf(" [0x%08X]=0x%08X", (unsigned)address, (unsigned)value);
      sequential = Sequential;
    }
    printf("\n");
  };
  dump_guest_words("PC", pipeline.execute.address);
  dump_guest_words("LR", r(14));
  exception(PSR::UND, 0x04);
}
