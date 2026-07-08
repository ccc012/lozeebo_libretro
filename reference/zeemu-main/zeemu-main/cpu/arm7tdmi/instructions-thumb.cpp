namespace {

auto trace_thumb_suspicious_branch_enabled() -> bool {
  static const bool enabled = std::getenv("ZEEMU_TRACE_BRANCH_HIGH") != nullptr;
  return enabled;
}

auto is_thumb_suspicious_branch_target(uint32 address) -> bool {
  if(address == 0xdeadbee0u || address == 0xdeadbeefu) return false;
  return (address >= 0x50000000u && address < 0x54000000u) ||
         (address >= 0x1f000000u && address < 0x20000000u) ||
         (address >= 0x80000000u && address < 0xff000000u);
}

auto describe_thumb_exec_region(uint32 address) -> const char* {
  if(address >= 0xff000000u) return "hle";
  if(address >= 0x50000000u && address < 0x54000000u) return "guest-heap";
  if(address >= 0x1f000000u && address < 0x20000000u) return "guest-stack";
  if(address >= 0x10000000u && address < 0x11000000u) return "module";
  if(address < 0x01000000u) return "low-module/mirror";
  return "unknown";
}

}

auto ARM7TDMI::thumbInstructionALU
(uint16 opcode) -> void {
  uint3 d = opcode.bit(0,2);
  uint3 m = opcode.bit(3,5);
  uint4 mode = opcode.bit(6,9);
  switch(mode) {
  case  0: r(d) = BIT(r(d) & r(m)); break;  //AND
  case  1: r(d) = BIT(r(d) ^ r(m)); break;  //EOR
  case  2: r(d) = BIT(LSL(r(d), r(m))); break;  //LSL
  case  3: r(d) = BIT(LSR(r(d), r(m))); break;  //LSR
  case  4: r(d) = BIT(ASR(r(d), r(m))); break;  //ASR
  case  5: r(d) = ADD(r(d), r(m), cpsr().c); break;  //ADC
  case  6: r(d) = SUB(r(d), r(m), cpsr().c); break;  //SBC
  case  7: r(d) = BIT(ROR(r(d), r(m))); break;  //ROR
  case  8:        BIT(r(d) & r(m)); break;  //TST
  case  9: r(d) = SUB(0, r(m), 1); break;  //NEG
  case 10:        SUB(r(d), r(m), 1); break;  //CMP
  case 11:        ADD(r(d), r(m), 0); break;  //CMN
  case 12: r(d) = BIT(r(d) | r(m)); break;  //ORR
  case 13: r(d) = MUL(0, r(m), r(d)); break;  //MUL
  case 14: r(d) = BIT(r(d) & ~r(m)); break;  //BIC
  case 15: r(d) = BIT(~r(m)); break;  //MVN
  }
}

auto ARM7TDMI::thumbInstructionALUExtended
(uint16 opcode) -> void {
  uint4 d = opcode.bit(0,2) | opcode.bit(7) << 3;
  uint4 m = opcode.bit(3,6);
  uint2 mode = opcode.bit(8,9);
  switch(mode) {
  case 0: r(d) = r(d) + r(m); break;  //ADD
  case 1: SUB(r(d), r(m), 1); break;  //SUBS
  case 2: r(d) = r(m); break;  //MOV
  }
}

auto ARM7TDMI::thumbInstructionAddRegister
(uint16 opcode) -> void {
  uint8 immediate = opcode.bit(0,7);
  uint3 d = opcode.bit(8,10);
  uint1 mode = opcode.bit(11);
  switch(mode) {
  case 0: r(d) = (r(15) & ~3) + immediate * 4; break;  //ADD pc
  case 1: r(d) = r(13) + immediate * 4; break;  //ADD sp
  }
}

auto ARM7TDMI::thumbInstructionAdjustImmediate
(uint16 opcode) -> void {
  uint3 d = opcode.bit(0,2);
  uint3 n = opcode.bit(3,5);
  uint3 immediate = opcode.bit(6,8);
  uint1 mode = opcode.bit(11);
  switch(mode) {
  case 0: r(d) = ADD(r(n), immediate, 0); break;  //ADD
  case 1: r(d) = SUB(r(n), immediate, 1); break;  //SUB
  }
}

auto ARM7TDMI::thumbInstructionAdjustRegister
(uint16 opcode) -> void {
  uint3 d = opcode.bit(0,2);
  uint3 n = opcode.bit(3,5);
  uint3 m = opcode.bit(6,8);
  uint1 mode = opcode.bit(10);
  switch(mode) {
  case 0: r(d) = ADD(r(n), r(m), 0); break;  //ADD
  case 1: r(d) = SUB(r(n), r(m), 1); break;  //SUB
  }
}

auto ARM7TDMI::thumbInstructionAdjustStack
(uint16 opcode) -> void {
  uint7 immediate = opcode.bit(0,6);
  uint1 mode = opcode.bit(7);
  switch(mode) {
  case 0: r(13) = r(13) + immediate * 4; break;  //ADD
  case 1: r(13) = r(13) - immediate * 4; break;  //SUB
  }
}

auto ARM7TDMI::thumbInstructionBranchExchange
(uint16 opcode) -> void {
  uint1 link = opcode.bit(7);
  uint4 m = opcode.bit(3,6);
  uint32 address = translate_address(r(m));
  if(trace_thumb_suspicious_branch_enabled() && is_thumb_suspicious_branch_target(address)) {
    printf("[CPU_BRANCH_SUSPECT] kind=%s pc=0x%08X opcode=0x%04X rm=r%u target=0x%08X region=%s raw=0x%08X lr=0x%08X r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X r12=0x%08X sp=0x%08X\n",
      link ? "THUMB_BLX_REG" : "THUMB_BX",
      (unsigned)pipeline.execute.address, (unsigned)opcode, (unsigned)m, (unsigned)address,
      describe_thumb_exec_region(address), (unsigned)r(m),
      (unsigned)r(14), (unsigned)r(0), (unsigned)r(1), (unsigned)r(2), (unsigned)r(3), (unsigned)r(12), (unsigned)r(13));
  }
  if(link) {
    r(14) = pipeline.decode.address | 1;
  }
  cpsr().t = address.bit(0);
  r(15) = address;
}

auto ARM7TDMI::thumbInstructionBranchFarPrefix
(uint16 opcode) -> void {
  int11 displacement = opcode.bit(0,10);
  r(14) = r(15) + (displacement * 2 << 11);
}

auto ARM7TDMI::thumbInstructionBranchFarSuffix
(uint16 opcode) -> void {
  uint11 displacement = opcode.bit(0,10);
  r(15) = r(14) + (displacement * 2);
  r(14) = pipeline.decode.address | 1;
}

auto ARM7TDMI::thumbInstructionBranchFarSuffixExchange
(uint16 opcode) -> void {
  uint11 displacement = opcode.bit(0,10);
  uint32 address = r(14) + displacement * 2;
  if(trace_thumb_suspicious_branch_enabled() && is_thumb_suspicious_branch_target(address)) {
    printf("[CPU_BRANCH_SUSPECT] kind=THUMB_BLX pc=0x%08X opcode=0x%04X target=0x%08X region=%s lr=0x%08X r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X r12=0x%08X sp=0x%08X\n",
      (unsigned)pipeline.execute.address, (unsigned)opcode, (unsigned)address,
      describe_thumb_exec_region(address), (unsigned)r(14),
      (unsigned)r(0), (unsigned)r(1), (unsigned)r(2), (unsigned)r(3), (unsigned)r(12), (unsigned)r(13));
  }
  r(14) = pipeline.decode.address | 1;
  cpsr().t = 0;
  r(15) = address & ~3u;
}

auto ARM7TDMI::thumbInstructionBranchNear
(uint16 opcode) -> void {
  int11 displacement = opcode.bit(0,10);
  r(15) = r(15) + displacement * 2;
}

auto ARM7TDMI::thumbInstructionBranchTest
(uint16 opcode) -> void {
  int8 displacement = opcode.bit(0,7);
  uint4 condition = opcode.bit(8,11);
  if(!TST(condition)) return;
  r(15) = r(15) + displacement * 2;
}

auto ARM7TDMI::thumbInstructionImmediate
(uint16 opcode) -> void {
  uint8 immediate = opcode.bit(0,7);
  uint3 d = opcode.bit(8,10);
  uint2 mode = opcode.bit(11,12);
  switch(mode) {
  case 0: r(d) = BIT(immediate); break;  //MOV
  case 1:        SUB(r(d), immediate, 1); break;  //CMP
  case 2: r(d) = ADD(r(d), immediate, 0); break;  //ADD
  case 3: r(d) = SUB(r(d), immediate, 1); break;  //SUB
  }
}

auto ARM7TDMI::thumbInstructionLoadLiteral
(uint16 opcode) -> void {
  uint8 displacement = opcode.bit(0,7);
  uint3 d = opcode.bit(8,10);
  uint32 address = (r(15) & ~3) + (displacement << 2);
  r(d) = load(Word | Nonsequential, address);
}

auto ARM7TDMI::thumbInstructionMoveByteImmediate
(uint16 opcode) -> void {
  uint3 d = opcode.bit(0,2);
  uint3 n = opcode.bit(3,5);
  uint5 offset = opcode.bit(6,10);
  uint1 mode = opcode.bit(11);
  switch(mode) {
  case 0: store(Byte | Nonsequential, r(n) + offset, r(d)); break;  //STRB
  case 1: r(d) = load(Byte | Nonsequential, r(n) + offset); break;  //LDRB
  }
}

auto ARM7TDMI::thumbInstructionMoveHalfImmediate
(uint16 opcode) -> void {
  uint3 d = opcode.bit(0,2);
  uint3 n = opcode.bit(3,5);
  uint5 offset = opcode.bit(6,10);
  uint1 mode = opcode.bit(11);
  switch(mode) {
  case 0: store(Half | Nonsequential, r(n) + offset * 2, r(d)); break;  //STRH
  case 1: r(d) = load(Half | Nonsequential, r(n) + offset * 2); break;  //LDRH
  }
}

auto ARM7TDMI::thumbInstructionMoveMultiple
(uint16 opcode) -> void {
  uint8 list = opcode.bit(0,7);
  uint3 n = opcode.bit(8,10);
  uint1 mode = opcode.bit(11);
  uint32 rn = r(n);

  for(uint m : range(8)) {
    if(!list.bit(m)) continue;
    switch(mode) {
    case 0: write(Word | Nonsequential, rn, r(m)); break;  //STMIA
    case 1: r(m) = read(Word | Nonsequential, rn); break;  //LDMIA
    }
    rn += 4;
  }

  if(mode == 0 || !list.bit(n)) r(n) = rn;
  if(mode == 1) idle();
}

auto ARM7TDMI::thumbInstructionMoveRegisterOffset
(uint16 opcode) -> void {
  uint3 d = opcode.bit(0,2);
  uint3 n = opcode.bit(3,5);
  uint3 m = opcode.bit(6,8);
  uint3 mode = opcode.bit(9,11);
  switch(mode) {
  case 0: store(Word | Nonsequential, r(n) + r(m), r(d)); break;  //STR
  case 1: store(Half | Nonsequential, r(n) + r(m), r(d)); break;  //STRH
  case 2: store(Byte | Nonsequential, r(n) + r(m), r(d)); break;  //STRB
  case 3: r(d) = load(Byte | Nonsequential | Signed, r(n) + r(m)); break;  //LDSB
  case 4: r(d) = load(Word | Nonsequential, r(n) + r(m)); break;  //LDR
  case 5: r(d) = load(Half | Nonsequential, r(n) + r(m)); break;  //LDRH
  case 6: r(d) = load(Byte | Nonsequential, r(n) + r(m)); break;  //LDRB
  case 7: r(d) = load(Half | Nonsequential | Signed, r(n) + r(m)); break;  //LDSH
  }
}

auto ARM7TDMI::thumbInstructionMoveStack
(uint16 opcode) -> void {
  uint8 immediate = opcode.bit(0,7);
  uint3 d = opcode.bit(8,10);
  uint1 mode = opcode.bit(11);
  switch(mode) {
  case 0: store(Word | Nonsequential, r(13) + immediate * 4, r(d)); break;  //STR
  case 1: r(d) = load(Word | Nonsequential, r(13) + immediate * 4); break;  //LDR
  }
}

auto ARM7TDMI::thumbInstructionMoveWordImmediate
(uint16 opcode) -> void {
  uint3 d = opcode.bit(0,2);
  uint3 n = opcode.bit(3,5);
  uint5 offset = opcode.bit(6,10);
  uint1 mode = opcode.bit(11);
  switch(mode) {
  case 0: store(Word | Nonsequential, r(n) + offset * 4, r(d)); break;  //STR
  case 1: r(d) = load(Word | Nonsequential, r(n) + offset * 4); break;  //LDR
  }
}

auto ARM7TDMI::thumbInstructionShiftImmediate
(uint16 opcode) -> void {
  uint3 d = opcode.bit(0,2);
  uint3 m = opcode.bit(3,5);
  uint5 immediate = opcode.bit(6,10);
  uint2 mode = opcode.bit(11,12);
  switch(mode) {
  case 0: r(d) = BIT(LSL(r(m), immediate)); break;  //LSL
  case 1: r(d) = BIT(LSR(r(m), immediate ? (uint)immediate : 32)); break;  //LSR
  case 2: r(d) = BIT(ASR(r(m), immediate ? (uint)immediate : 32)); break;  //ASR
  }
}

auto ARM7TDMI::thumbInstructionSoftwareInterrupt
(uint16 opcode) -> void {
  uint8 immediate = opcode.bit(0,7);
  softwareInterrupt(uint32_t(immediate), uint32_t(pipeline.decode.address));
}

auto ARM7TDMI::thumbInstructionStackMultiple
(uint16 opcode) -> void {
  uint8 list = opcode.bit(0,7);
  uint1 lrpc = opcode.bit(8);
  uint1 mode = opcode.bit(11);
  uint32 sp;
  switch(mode) {
  case 0: sp = r(13) - (bit::count(list) + lrpc) * 4; break;  //PUSH
  case 1: sp = r(13);  //POP
  }

  uint sequential = Nonsequential;
  for(uint m : range(8)) {
    if(!list.bit(m)) continue;
    switch(mode) {
    case 0: write(Word | sequential, sp, r(m)); break;  //PUSH
    case 1: r(m) = read(Word | sequential, sp); break;  //POP
    }
    sp += 4;
    sequential = Sequential;
  }

  if(lrpc) {
    switch(mode) {
    case 0: write(Word | sequential, sp, r(14)); break;  //PUSH
    case 1: {  //POP
      uint32 address = read(Word | sequential, sp);
      cpsr().t = address.bit(0);
      r(15) = address;
      break;
    }
    }
    sp += 4;
  }

  if(mode == 1) {
    idle();
    r(13) = r(13) + (bit::count(list) + lrpc) * 4;  //POP
  } else {
    pipeline.nonsequential = true;
    r(13) = r(13) - (bit::count(list) + lrpc) * 4;  //PUSH
  }
}

auto ARM7TDMI::thumbInstructionUndefined
(uint16 opcode) -> void {
  auto region = [](uint32 address) -> const char* {
    if(address >= 0xff000000u) return "hle";
    if(address >= 0x50000000u && address < 0x54000000u) return "guest-heap";
    if(address >= 0x1f000000u && address < 0x20000000u) return "guest-stack";
    if(address >= 0x10000000u && address < 0x11000000u) return "module";
    if(address < 0x01000000u) return "low-module/mirror";
    return "unknown";
  };
  printf("[CPU] Undefined Thumb Instruction 0x%04X at PC=0x%08X region=%s CPSR=0x%08X LR=0x%08X SP=0x%08X\n",
    (unsigned)opcode,
    (unsigned)pipeline.execute.address,
    region(pipeline.execute.address),
    (unsigned)(uint32)cpsr(),
    (unsigned)(uint32)r(14),
    (unsigned)(uint32)r(13));
  printf("[CPU] Undefined Thumb regs: R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X R4=0x%08X R5=0x%08X R6=0x%08X R7=0x%08X R8=0x%08X R9=0x%08X R10=0x%08X R11=0x%08X R12=0x%08X\n",
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
  exception(PSR::UND, 0x04);
}
