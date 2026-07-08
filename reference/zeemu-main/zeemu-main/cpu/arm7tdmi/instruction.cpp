auto ARM7TDMI::fetch() -> void {
  pipeline.execute = pipeline.decode;
  pipeline.decode = pipeline.fetch;
  pipeline.decode.thumb = cpsr().t;

  uint sequential = Sequential;
  if(pipeline.nonsequential) {
    pipeline.nonsequential = false;
    sequential = Nonsequential;
  }

  uint mask = !cpsr().t ? 3 : 1;
  uint size = !cpsr().t ? Word : Half;

  r(15).data += size >> 3;
  pipeline.fetch.address = r(15) & ~mask;
  pipeline.fetch.instruction = read(Prefetch | size | sequential, pipeline.fetch.address);
}

static auto trace_cpu_pc_range(uint32 address) -> bool {
  static bool initialized = false;
  static bool enabled = false;
  static uint32 start = 0;
  static uint32 end = 0;

  if(!initialized) {
    initialized = true;
    if(const char* env = std::getenv("ZEEMU_TRACE_CPU_PC")) {
      char* parse_end = nullptr;
      start = static_cast<uint32>(std::strtoul(env, &parse_end, 0));
      uint32 bytes = 4;
      if(parse_end && *parse_end == '+') {
        bytes = static_cast<uint32>(std::strtoul(parse_end + 1, nullptr, 0));
      }
      end = start + bytes;
      enabled = bytes != 0 && end >= start;
    }
  }

  return enabled && address >= start && address < end;
}

static auto trace_nfs_apt_enabled() -> bool {
  static bool initialized = false;
  static bool enabled = false;
  if(!initialized) {
    initialized = true;
    enabled = std::getenv("ZEEMU_TRACE_NFS_APT") != nullptr;
  }
  return enabled;
}

static auto trace_nfs_boot_enabled() -> bool {
  static bool initialized = false;
  static bool enabled = false;
  if(!initialized) {
    initialized = true;
    enabled = std::getenv("ZEEMU_TRACE_NFS_BOOT") != nullptr;
  }
  return enabled;
}

static auto trace_nfs_boot_is_interesting_pc(uint32 pc) -> bool {
  switch(pc) {
  case 0x100489a4:
  case 0x10033684:
  case 0x10033690:
  case 0x10033694:
  case 0x10033698:
  case 0x1003369c:
  case 0x100336b0:
  case 0x100336b4:
  case 0x100336bc:
  case 0x10030ef0:
  case 0x10030f50:
  case 0x10030f5c:
  case 0x10030f68:
  case 0x10030f74:
  case 0x10030f80:
  case 0x10030f84:
  case 0x10030fb8:
  case 0x10030fbc:
  case 0x10030fc0:
  case 0x10030fc4:
  case 0x10030fc8:
  case 0x10030fd0:
  case 0x10030ffc:
  case 0x10031028:
  case 0x10031054:
  case 0x1003105c:
  case 0x100311e0:
  case 0x100311e4:
  case 0x10031c54:
  case 0x10031c60:
  case 0x10031c64:
  case 0x1009c60c:
  case 0x1009c610:
  case 0x1009c6f0:
  case 0x1009c6f4:
  case 0x100b0790:
  case 0x100b07ac:
  case 0x100b07b0:
  case 0x100b07b4:
  case 0x100b0958:
  case 0x100b0968:
  case 0x100b098c:
  case 0x100b09a4:
  case 0x100b09b8:
  case 0x100b09cc:
  case 0x10049130:
  case 0x10049148:
  case 0x10049184:
  case 0x10049188:
  case 0x1004918c:
  case 0x10049190:
  case 0x10049194:
  case 0x100491c0:
  case 0x10049244:
  case 0x10049248:
  case 0x1004924c:
  case 0x10049294:
  case 0x100492a4:
  case 0x100492b0:
  case 0x100492d4:
  case 0x10169b9c:
  case 0x10169bc8:
  case 0x10169bcc:
  case 0x10169bd0:
  case 0x10169bd4:
  case 0x10169de0:
  case 0x1016a0e4:
  case 0x1016a0e8:
  case 0x1016a0ec:
  case 0x1016a0f0:
    return true;
  default:
    return false;
  }
}

static auto trace_nfs_apt_is_interesting_pc(uint32 pc) -> bool {
  switch(pc) {
  case 0x100486a8:
  case 0x100486d0:
  case 0x100489a4:
  case 0x10049130:
  case 0x10049148:
  case 0x10049150:
  case 0x10049184:
  case 0x10049188:
  case 0x1004918c:
  case 0x10049190:
  case 0x10049194:
  case 0x100491c0:
  case 0x10049244:
  case 0x10049248:
  case 0x1004924c:
  case 0x10049294:
  case 0x100492a4:
  case 0x100492b0:
  case 0x100492d4:
  case 0x1009c578:
  case 0x1009c628:
  case 0x1009c5a4:
  case 0x1009c5e8:
  case 0x1009c604:
  case 0x1009c620:
  case 0x1009c63c:
  case 0x1009c670:
  case 0x1009c684:
  case 0x1009c698:
  case 0x1009c6ac:
  case 0x1009c6c0:
  case 0x1009c6d4:
  case 0x1009c6e4:
  case 0x1009cab4:
  case 0x1009cae0:
  case 0x1009cafc:
  case 0x1009cb18:
  case 0x1009cb34:
  case 0x1009cb50:
  case 0x1009cb6c:
  case 0x1009cb88:
  case 0x1009cc2c:
  case 0x1009cc48:
  case 0x1009ccbc:
  case 0x1009ccec:
  case 0x1009ce4c:
  case 0x1016c0a8:
  case 0x1016ce20:
  case 0x1016ce74:
  case 0x1016ceb4:
  case 0x1016cf00:
  case 0x1016cf28:
  case 0x1016cf58:
  case 0x1016cf90:
  case 0x1016cfa8:
  case 0x1016d01c:
  case 0x101aadec:
  case 0x101aaedc:
    return true;
  default:
    return false;
  }
}

auto trace_nfs_apt_read_word(ARM7TDMI& cpu, uint32 addr) -> uint32 {
  if(!addr || addr >= 0xff000000u || (addr & 3)) return 0;
  try {
    return cpu.read(ARM7TDMI::Word, addr);
  } catch(...) {
    return 0;
  }
}

auto trace_nfs_apt_read_byte(ARM7TDMI& cpu, uint32 addr) -> uint32 {
  if(!addr || addr >= 0xff000000u) return 0;
  try {
    return cpu.read(ARM7TDMI::Byte, addr) & 0xff;
  } catch(...) {
    return 0;
  }
}

auto trace_nfs_apt_read_string(ARM7TDMI& cpu, uint32 addr) -> std::string {
  std::string out;
  if(!addr || addr >= 0xff000000u) return out;
  for(uint i = 0; i < 96; ++i) {
    uint32 value = 0;
    try {
      value = cpu.read(ARM7TDMI::Byte, addr + i) & 0xff;
    } catch(...) {
      break;
    }
    if(value == 0) break;
    if(value < 0x20 || value >= 0x7f) {
      if(out.empty()) return {};
      out += '.';
      continue;
    }
    out += static_cast<char>(value);
  }
  return out;
}

auto ARM7TDMI::trace_nfs_boot(uint32 pc) -> void {
  if(!trace_nfs_boot_enabled() || !trace_nfs_boot_is_interesting_pc(pc)) return;

  const auto plausible_boot_object = [](uint32 ptr) -> bool {
    return ptr >= 0x50000000u && ptr < 0x60000000u;
  };
  const auto read_boot_object = [&]() -> uint32 {
    const uint32 r0v = r(0);
    if(plausible_boot_object(r0v)) return r0v;

    uint32 boot_obj = trace_nfs_apt_read_word(*this, 0x10216f5c);
    if(plausible_boot_object(boot_obj)) return boot_obj;

    boot_obj = trace_nfs_apt_read_word(*this, 0x00216f5c);
    if(plausible_boot_object(boot_obj)) return boot_obj;

    // Debug fallback observed for NFS's BootPSA object. Env-gated only.
    return 0x5036bc90;
  };

  const uint32 boot_obj = read_boot_object();
  const uint32 state = trace_nfs_apt_read_word(*this, boot_obj + 0x04);
  const uint32 flags8 = trace_nfs_apt_read_byte(*this, boot_obj + 0x08);
  const uint32 flags9 = trace_nfs_apt_read_byte(*this, boot_obj + 0x09);
  const uint32 stamp = trace_nfs_apt_read_word(*this, boot_obj + 0x0c);

  static uint32 last_state = 0xffffffffu;
  static uint32 last_flags8 = 0xffffffffu;
  static uint32 last_flags9 = 0xffffffffu;
  static uint32 last_stamp = 0xffffffffu;
  const bool changed = state != last_state || flags8 != last_flags8 || flags9 != last_flags9 || stamp != last_stamp;
  const bool boot_psa_handler = pc >= 0x10049294 && pc <= 0x100492d4;
  const bool frame_gate = state == 2 && pc >= 0x10033684 && pc <= 0x100336bc;
  const bool loading_callback = state == 2 && (
    (pc >= 0x10030ef0 && pc <= 0x100311e4) ||
    (pc >= 0x10031c54 && pc <= 0x10031c64) ||
    (pc >= 0x10169b9c && pc <= 0x1016a0f0));
  const bool flow_queue = state == 2 && (
    (pc >= 0x1009c60c && pc <= 0x1009c6f4) ||
    (pc >= 0x100b0790 && pc <= 0x100b09cc));
  const bool state2_branch = state == 2 && (
    pc == 0x10049184 || pc == 0x10049188 || pc == 0x1004918c || pc == 0x10049190 ||
    pc == 0x10049244 || pc == 0x10049248 || pc == 0x1004924c || pc == 0x100489a4);
  if(!changed && !boot_psa_handler && !state2_branch && !frame_gate && !flow_queue && !loading_callback) return;
  last_state = state;
  last_flags8 = flags8;
  last_flags9 = flags9;
  last_stamp = stamp;

  std::printf("[NFS_BOOT] pc=0x%08x obj=0x%08x state=%u flags8=0x%02x flags9=0x%02x stamp=0x%08x r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x",
    (unsigned)pc, (unsigned)boot_obj, (unsigned)state, (unsigned)flags8,
    (unsigned)flags9, (unsigned)stamp, (unsigned)(uint32)r(0),
    (unsigned)(uint32)r(1), (unsigned)(uint32)r(2), (unsigned)(uint32)r(3));
  if(frame_gate || flow_queue) {
    const uint32 gate_obj = trace_nfs_apt_read_word(*this, r(9));
    const uint32 gate_flags = trace_nfs_apt_read_byte(*this, gate_obj + 0x08);
    const uint32 flow_high0 = trace_nfs_apt_read_word(*this, 0x10217220);
    const uint32 flow_high1 = trace_nfs_apt_read_word(*this, 0x10217224);
    const uint32 flow_high8 = trace_nfs_apt_read_word(*this, 0x10217228);
    const uint32 flow_high1c = trace_nfs_apt_read_byte(*this, 0x1021723c);
    const uint32 flow_high20 = trace_nfs_apt_read_word(*this, 0x10217240);
    const uint32 flow_high24 = trace_nfs_apt_read_word(*this, 0x10217244);
    std::printf(" gate{r9=0x%08x obj=0x%08x flags=0x%02x} flow{cb=0x%08x arg=0x%08x name=0x%08x flag1c=0x%02x pending=0x%08x state=0x%08x}",
      (unsigned)(uint32)r(9), (unsigned)gate_obj, (unsigned)gate_flags,
      (unsigned)flow_high0, (unsigned)flow_high1, (unsigned)flow_high8,
      (unsigned)flow_high1c, (unsigned)flow_high20, (unsigned)flow_high24);
  }
  std::printf("\n");
}

auto ARM7TDMI::trace_nfs_apt(uint32 pc) -> void {
  if(!trace_nfs_apt_enabled() || !trace_nfs_apt_is_interesting_pc(pc)) return;

  const uint32 r0v = r(0);
  const uint32 r1v = r(1);
  const uint32 r2v = r(2);
  const uint32 r3v = r(3);
  std::printf("[NFS_APT] pc=0x%08x r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x",
    (unsigned)pc, (unsigned)r0v, (unsigned)r1v, (unsigned)r2v, (unsigned)r3v);

  if((pc >= 0x1009c500 && pc < 0x1009cf00) || (pc >= 0x1016c000 && pc < 0x1016d100) || pc == 0x101aadec || pc == 0x101aaedc) {
    const std::string s0 = trace_nfs_apt_read_string(*this, r0v);
    const std::string s1 = trace_nfs_apt_read_string(*this, r1v);
    const std::string s2 = trace_nfs_apt_read_string(*this, r2v);
    if(!s0.empty()) std::printf(" s0='%s'", s0.c_str());
    if(!s1.empty()) std::printf(" s1='%s'", s1.c_str());
    if(!s2.empty()) std::printf(" s2='%s'", s2.c_str());
  }

  if((pc >= 0x1016c000 && pc < 0x1016d100) || pc == 0x1009c578 || pc == 0x100486d0) {
    const uint32 player = r0v;
    const uint32 count = trace_nfs_apt_read_word(*this, player + 0x1430);
    const uint32 current = trace_nfs_apt_read_word(*this, player + 0x1434);
    const uint32 pending = trace_nfs_apt_read_word(*this, player + 0x145c);
    const uint32 gate = trace_nfs_apt_read_word(*this, player + 0x1458);
    std::printf(" movie{count=%u current=0x%08x gate=%u pending=%u}",
      (unsigned)count, (unsigned)current, (unsigned)gate, (unsigned)pending);
  }
  if((pc >= 0x10048900 && pc < 0x10049300) || pc == 0x1009c604) {
    const auto plausible_boot_object = [](uint32 ptr) -> bool {
      return ptr >= 0x50000000u && ptr < 0x60000000u;
    };
    uint32 boot_obj = 0;
    if(plausible_boot_object(r0v)) {
      boot_obj = r0v;
    } else {
      boot_obj = trace_nfs_apt_read_word(*this, 0x10216f5c);
      if(!plausible_boot_object(boot_obj)) {
        boot_obj = trace_nfs_apt_read_word(*this, 0x00216f5c);
      }
    }
    const uint32 state = trace_nfs_apt_read_word(*this, boot_obj + 0x04);
    const uint32 flags8 = trace_nfs_apt_read_byte(*this, boot_obj + 0x08);
    const uint32 flags9 = trace_nfs_apt_read_byte(*this, boot_obj + 0x09);
    const uint32 stamp = trace_nfs_apt_read_word(*this, boot_obj + 0x0c);
    std::printf(" boot{obj=0x%08x state=%u flags8=0x%02x flags9=0x%02x stamp=0x%08x}",
      (unsigned)boot_obj, (unsigned)state, (unsigned)flags8, (unsigned)flags9, (unsigned)stamp);
  }
  std::printf("\n");
}

auto ARM7TDMI::instruction() -> void {
  uint mask = !cpsr().t ? 3 : 1;
  uint size = !cpsr().t ? Word : Half;

  if(pipeline.reload) {
    pipeline.reload = false;
    r(15).data &= ~mask;
    pipeline.fetch.address = r(15) & ~mask;
    pipeline.fetch.instruction = read(Prefetch | size | Nonsequential, pipeline.fetch.address);
    fetch();
  }
  fetch();

  if(irq && !cpsr().i) {
    exception(PSR::IRQ, 0x18);
    if(pipeline.execute.thumb) r(14).data += 2;
    return;
  }

  opcode = pipeline.execute.instruction;
  trace_nfs_boot(pipeline.execute.address);
  trace_nfs_apt(pipeline.execute.address);
  if(trace_cpu_pc_range(pipeline.execute.address)) {
    std::printf("[CPU_PC] pc=0x%08x op=0x%08x cpsr=0x%08x r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x r4=0x%08x r5=0x%08x r6=0x%08x r7=0x%08x r8=0x%08x r9=0x%08x r10=0x%08x r11=0x%08x r12=0x%08x sp=0x%08x lr=0x%08x\n",
      (unsigned)pipeline.execute.address,
      (unsigned)opcode,
      (unsigned)(uint32)cpsr(),
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
      (unsigned)(uint32)r(12),
      (unsigned)(uint32)r(13),
      (unsigned)(uint32)r(14));
  }
  if(!pipeline.execute.thumb) {
    if(opcode.bit(28,31) == 15) {
      if(opcode.bit(25,27) == 5) {
        armInstructionBranch(opcode);
        return;
      }
      if(opcode.bit(20,27) == 0x10) {
        armInstructionChangeProcessorState(opcode);
        return;
      }
      if(opcode.bit(20,27) == 0x11 && opcode.bit(4,7) == 0) {
        armInstructionSetEnd(opcode);
        return;
      }
      if(opcode.bit(25,27) == 2 && opcode.bit(20,23) == 5 && opcode.bit(12,15) == 15) {
        armInstructionPreload(opcode);
        return;
      }
      if(opcode.bit(25,27) == 3 && opcode.bit(20,23) == 5 && opcode.bit(12,15) == 15) {
        armInstructionPreload(opcode);
        return;
      }
    }
    if(!TST(opcode.bit(28,31))) return;
    if(opcode.bit(20,27) == 0x12 && opcode.bit(4,7) == 2) {
      armInstructionBranchExchangeJazelle(opcode);
      return;
    }
    uint12 index = (opcode & 0x0ff00000) >> 16 | (opcode & 0x000000f0) >> 4;
    (this->*armInstruction[index])(opcode);
  } else {
    (this->*thumbInstruction[(uint16)opcode])((uint16)opcode);
  }
}

auto ARM7TDMI::exception(uint mode, uint32 address) -> void {
  auto psr = cpsr();
  cpsr().m = mode;
  spsr() = psr;
  cpsr().t = 0;
  if(cpsr().m == PSR::FIQ) cpsr().f = 1;
  cpsr().i = 1;
  r(14) = pipeline.decode.address;
  r(15) = address;
}

auto ARM7TDMI::armInitialize() -> void {
  #define bind(id, name, ...) { \
    uint index = (id & 0x0ff00000) >> 16 | (id & 0x000000f0) >> 4; \
    assert(!armInstruction[index]); \
    armInstruction[index] = &ARM7TDMI::armInstruction##name; \
    armDisassemble[index] = [&](uint32 opcode) { (void)opcode; return armDisassemble##name(arguments); }; \
  }

  #define pattern(s) \
    std::integral_constant<uint32_t, bit::test(s)>::value

  #define arguments \
    opcode.bit( 0,23),  /* displacement */ \
    opcode.bit(24)      /* link */
  for(uint4 displacementLo : range(16))
  for(uint4 displacementHi : range(16))
  for(uint1 link : range(2)) {
    auto opcode = pattern(".... 101? ???? ???? ???? ???? ???? ????")
                | displacementLo << 4 | displacementHi << 20 | link << 24;
    bind(opcode, Branch);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3)   /* m */
  {
    auto opcode = pattern(".... 0001 0010 ---- ---- ---- 0011 ????");
    bind(opcode, BranchLinkExchangeRegister);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3)   /* m */
  {
    auto opcode = pattern(".... 0001 0010 ---- ---- ---- 0001 ????");
    bind(opcode, BranchExchangeRegister);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3) << 0 | opcode.bit( 8,11) << 4 | opcode.bit(16,19) << 8 | opcode.bit( 4, 7) << 12  /* immediate */
  {
    auto opcode = pattern(".... 0001 0010 ---- ---- ---- 0111 ----");
    bind(opcode, Breakpoint);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit(12,15)   /* d */
  {
    auto opcode = pattern(".... 0001 0110 1111 ???? 1111 0001 ????");
    bind(opcode, CountLeadingZeros);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit(10,11),  /* rotate */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19)   /* n */
  {
    auto opcode = pattern(".... 0110 1111 ---- ---- ---- 0111 ----");
    bind(opcode, UnsignedExtendHalfword);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit(10,11),  /* rotate */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19)   /* n */
  {
    auto opcode = pattern(".... 0110 1011 ---- ---- ---- 0111 ----");
    bind(opcode, SignedExtendHalfword);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit(10,11),  /* rotate */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19)   /* n */
  {
    auto opcode = pattern(".... 0110 1110 ---- ---- ---- 0111 ----");
    bind(opcode, UnsignedExtendByte);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit(10,11),  /* rotate */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19)   /* n */
  {
    auto opcode = pattern(".... 0110 1010 ---- ---- ---- 0111 ----");
    bind(opcode, SignedExtendByte);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit(12,15)   /* d */
  {
    auto opcode = pattern(".... 0110 1011 1111 ---- ---- 0011 ----");
    bind(opcode, ByteReverseWord);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit(12,15)   /* d */
  {
    auto opcode = pattern(".... 0110 1011 1111 ---- ---- 1011 ----");
    bind(opcode, ByteReversePackedHalfword);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit(12,15)   /* d */
  {
    auto opcode = pattern(".... 0110 1111 1111 ---- ---- 1011 ----");
    bind(opcode, ByteReverseSignedHalfword);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 7),  /* immediate */ \
    opcode.bit( 8,11),  /* shift */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19),  /* n */ \
    opcode.bit(20),     /* save */ \
    opcode.bit(21,24)   /* mode */
  for(uint4 shiftHi : range(16))
  for(uint1 save : range(2))
  for(uint4 mode : range(16)) {
    if(mode >= 8 && mode <= 11 && !save) continue;  //TST, TEQ, CMP, CMN
    auto opcode = pattern(".... 001? ???? ???? ???? ???? ???? ????") | shiftHi << 4 | save << 20 | mode << 21;
    bind(opcode, DataImmediate);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit( 5, 6),  /* type */ \
    opcode.bit( 7,11),  /* shift */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19),  /* n */ \
    opcode.bit(20),     /* save */ \
    opcode.bit(21,24)   /* mode */
  for(uint2 type : range(4))
  for(uint1 shiftLo : range(2))
  for(uint1 save : range(2))
  for(uint4 mode : range(16)) {
    if(mode >= 8 && mode <= 11 && !save) continue;  //TST, TEQ, CMP, CMN
    auto opcode = pattern(".... 000? ???? ???? ???? ???? ???0 ????") | type << 5 | shiftLo << 7 | save << 20 | mode << 21;
    bind(opcode, DataImmediateShift);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit( 5, 6),  /* type */ \
    opcode.bit( 8,11),  /* s */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19),  /* n */ \
    opcode.bit(20),     /* save */ \
    opcode.bit(21,24)   /* mode */
  for(uint2 type : range(4))
  for(uint1 save : range(2))
  for(uint4 mode : range(16)) {
    if(mode >= 8 && mode <= 11 && !save) continue;  //TST, TEQ, CMP, CMN
    auto opcode = pattern(".... 000? ???? ???? ???? ???? 0??1 ????") | type << 5 | save << 20 | mode << 21;
    bind(opcode, DataRegisterShift);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3) << 0 | opcode.bit( 8,11) << 4,  /* immediate */ \
    opcode.bit( 5),     /* half */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19),  /* n */ \
    opcode.bit(21),     /* writeback */ \
    opcode.bit(23),     /* up */ \
    opcode.bit(24)      /* pre */
  for(uint1 half : range(2))
  for(uint1 writeback : range(2))
  for(uint1 up : range(2))
  for(uint1 pre : range(2)) {
    auto opcode = pattern(".... 000? ?1?1 ???? ???? ???? 11?1 ????") | half << 5 | writeback << 21 | up << 23 | pre << 24;
    bind(opcode, LoadImmediate);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit( 5),     /* half */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19),  /* n */ \
    opcode.bit(21),     /* writeback */ \
    opcode.bit(23),     /* up */ \
    opcode.bit(24)      /* pre */
  for(uint1 half : range(2))
  for(uint1 writeback : range(2))
  for(uint1 up : range(2))
  for(uint1 pre : range(2)) {
    auto opcode = pattern(".... 000? ?0?1 ???? ???? ---- 11?1 ????") | half << 5 | writeback << 21 | up << 23 | pre << 24;
    bind(opcode, LoadRegister);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19),  /* n */ \
    opcode.bit(22)      /* byte */
  for(uint1 byte : range(2)) {
    auto opcode = pattern(".... 0001 0?00 ???? ???? ---- 1001 ????") | byte << 22;
    bind(opcode, MemorySwap);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3) << 0 | opcode.bit( 8,11) << 4,  /* immediate */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19),  /* n */ \
    uint1(1),            /* mode: LDRD */ \
    opcode.bit(21),     /* writeback */ \
    opcode.bit(23),     /* up */ \
    opcode.bit(24)      /* pre */
  for(uint1 writeback : range(2))
  for(uint1 up : range(2))
  for(uint1 pre : range(2)) {
    auto opcode = pattern(".... 000? ?1?0 ???? ???? ???? 1101 ????") | writeback << 21 | up << 23 | pre << 24;
    bind(opcode, MoveDoubleImmediate);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3) << 0 | opcode.bit( 8,11) << 4,  /* immediate */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19),  /* n */ \
    uint1(0),            /* mode: STRD */ \
    opcode.bit(21),     /* writeback */ \
    opcode.bit(23),     /* up */ \
    opcode.bit(24)      /* pre */
  for(uint1 writeback : range(2))
  for(uint1 up : range(2))
  for(uint1 pre : range(2)) {
    auto opcode = pattern(".... 000? ?1?0 ???? ???? ???? 1111 ????") | writeback << 21 | up << 23 | pre << 24;
    bind(opcode, MoveDoubleImmediate);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19),  /* n */ \
    uint1(1),            /* mode: LDRD */ \
    opcode.bit(21),     /* writeback */ \
    opcode.bit(23),     /* up */ \
    opcode.bit(24)      /* pre */
  for(uint1 writeback : range(2))
  for(uint1 up : range(2))
  for(uint1 pre : range(2)) {
    auto opcode = pattern(".... 000? ?0?0 ???? ???? ---- 1101 ????") | writeback << 21 | up << 23 | pre << 24;
    bind(opcode, MoveDoubleRegister);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19),  /* n */ \
    uint1(0),            /* mode: STRD */ \
    opcode.bit(21),     /* writeback */ \
    opcode.bit(23),     /* up */ \
    opcode.bit(24)      /* pre */
  for(uint1 writeback : range(2))
  for(uint1 up : range(2))
  for(uint1 pre : range(2)) {
    auto opcode = pattern(".... 000? ?0?0 ???? ???? ---- 1111 ????") | writeback << 21 | up << 23 | pre << 24;
    bind(opcode, MoveDoubleRegister);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3) << 0 | opcode.bit( 8,11) << 4,  /* immediate */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19),  /* n */ \
    opcode.bit(20),     /* mode */ \
    opcode.bit(21),     /* writeback */ \
    opcode.bit(23),     /* up */ \
    opcode.bit(24)      /* pre */
  for(uint1 mode : range(2))
  for(uint1 writeback : range(2))
  for(uint1 up : range(2))
  for(uint1 pre : range(2)) {
    auto opcode = pattern(".... 000? ?1?? ???? ???? ???? 1011 ????") | mode << 20 | writeback << 21 | up << 23 | pre << 24;
    bind(opcode, MoveHalfImmediate);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19),  /* n */ \
    opcode.bit(20),     /* mode */ \
    opcode.bit(21),     /* writeback */ \
    opcode.bit(23),     /* up */ \
    opcode.bit(24)      /* pre */
  for(uint1 mode : range(2))
  for(uint1 writeback : range(2))
  for(uint1 up : range(2))
  for(uint1 pre : range(2)) {
    auto opcode = pattern(".... 000? ?0?? ???? ???? ---- 1011 ????") | mode << 20 | writeback << 21 | up << 23 | pre << 24;
    bind(opcode, MoveHalfRegister);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0,11),  /* immediate */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19),  /* n */ \
    opcode.bit(20),     /* mode */ \
    opcode.bit(21),     /* writeback */ \
    opcode.bit(22),     /* byte */ \
    opcode.bit(23),     /* up */ \
    opcode.bit(24)      /* pre */
  for(uint4 immediatePart : range(16))
  for(uint1 mode : range(2))
  for(uint1 writeback : range(2))
  for(uint1 byte : range(2))
  for(uint1 up : range(2))
  for(uint1 pre : range(2)) {
    auto opcode = pattern(".... 010? ???? ???? ???? ???? ???? ????")
                | immediatePart << 4 | mode << 20 | writeback << 21 | byte << 22 | up << 23 | pre << 24;
    bind(opcode, MoveImmediateOffset);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0,15),  /* list */ \
    opcode.bit(16,19),  /* n */ \
    opcode.bit(20),     /* mode */ \
    opcode.bit(21),     /* writeback */ \
    opcode.bit(22),     /* type */ \
    opcode.bit(23),     /* up */ \
    opcode.bit(24)      /* pre */
  for(uint4 listPart : range(16))
  for(uint1 mode : range(2))
  for(uint1 writeback : range(2))
  for(uint1 type : range(2))
  for(uint1 up : range(2))
  for(uint1 pre : range(2)) {
    auto opcode = pattern(".... 100? ???? ???? ???? ???? ???? ????")
                | listPart << 4 | mode << 20 | writeback << 21 | type << 22 | up << 23 | pre << 24;
    bind(opcode, MoveMultiple);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit( 5, 6),  /* type */ \
    opcode.bit( 7,11),  /* shift */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19),  /* n */ \
    opcode.bit(20),     /* mode */ \
    opcode.bit(21),     /* writeback */ \
    opcode.bit(22),     /* byte */ \
    opcode.bit(23),     /* up */ \
    opcode.bit(24)      /* pre */
  for(uint2 type : range(4))
  for(uint1 shiftLo : range(2))
  for(uint1 mode : range(2))
  for(uint1 writeback : range(2))
  for(uint1 byte : range(2))
  for(uint1 up : range(2))
  for(uint1 pre : range(2)) {
    auto opcode = pattern(".... 011? ???? ???? ???? ???? ???0 ????")
                | type << 5 | shiftLo << 7 | mode << 20 | writeback << 21 | byte << 22 | up << 23 | pre << 24;
    bind(opcode, MoveRegisterOffset);
  }
  #undef arguments

  #define arguments \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(22)      /* mode */
  for(uint1 mode : range(2)) {
    auto opcode = pattern(".... 0001 0?00 ---- ???? ---- 0000 ----") | mode << 22;
    bind(opcode, MoveToRegisterFromStatus);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 7),  /* immediate */ \
    opcode.bit( 8,11),  /* rotate */ \
    opcode.bit(16,19),  /* field */ \
    opcode.bit(22)      /* mode */
  for(uint4 immediateHi : range(16))
  for(uint1 mode : range(2)) {
    auto opcode = pattern(".... 0011 0?10 ???? ---- ???? ???? ????") | immediateHi << 4 | mode << 22;
    bind(opcode, MoveToStatusFromImmediate);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit(16,19),  /* field */ \
    opcode.bit(22)      /* mode */
  for(uint1 mode : range(2)) {
    auto opcode = pattern(".... 0001 0?10 ???? ---- ---- 0000 ????") | mode << 22;
    bind(opcode, MoveToStatusFromRegister);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit( 8,11),  /* s */ \
    opcode.bit(12,15),  /* n */ \
    opcode.bit(16,19),  /* d */ \
    opcode.bit(20),     /* save */ \
    opcode.bit(21)      /* accumulate */
  for(uint1 save : range(2))
  for(uint1 accumulate : range(2)) {
    auto opcode = pattern(".... 0000 00?? ???? ???? ???? 1001 ????") | save << 20 | accumulate << 21;
    bind(opcode, Multiply);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit( 8,11),  /* s */ \
    opcode.bit(12,15),  /* l */ \
    opcode.bit(16,19),  /* h */ \
    opcode.bit(20),     /* save */ \
    opcode.bit(21),     /* accumulate */ \
    opcode.bit(22)      /* sign */
  for(uint1 save : range(2))
  for(uint1 accumulate : range(2))
  for(uint1 sign : range(2)) {
    auto opcode = pattern(".... 0000 1??? ???? ???? ???? 1001 ????") | save << 20 | accumulate << 21 | sign << 22;
    bind(opcode, MultiplyLong);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit( 8,11),  /* coprocessor */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19),  /* n */ \
    opcode.bit(20,23),  /* opcode1 */ \
    opcode.bit( 5, 7)   /* opcode2 */
  for(uint4 opcode1 : range(16))
  for(uint3 opcode2 : range(8)) {
    auto opcode = pattern(".... 1110 ???? ???? ???? ???? ???0 ????") | opcode1 << 20 | opcode2 << 5;
    bind(opcode, CoprocessorDataOperation);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit( 8,11),  /* coprocessor */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19),  /* n */ \
    opcode.bit( 4, 7),  /* opcode */ \
    opcode.bit(20)      /* load */
  for(uint4 opcodePart : range(16))
  for(uint1 load : range(2)) {
    auto opcode = pattern(".... 1100 010? ???? ???? ???? ???? ????") | opcodePart << 4 | load << 20;
    bind(opcode, CoprocessorRegisterTransferLong);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0, 3),  /* m */ \
    opcode.bit( 8,11),  /* coprocessor */ \
    opcode.bit(12,15),  /* d */ \
    opcode.bit(16,19),  /* n */ \
    opcode.bit(21,23),  /* opcode1 */ \
    opcode.bit( 5, 7),  /* opcode2 */ \
    opcode.bit(20)      /* load */
  for(uint3 opcode1 : range(8))
  for(uint3 opcode2 : range(8))
  for(uint1 load : range(2)) {
    auto opcode = pattern(".... 1110 ???? ???? ???? ???? ???1 ????") | opcode1 << 21 | opcode2 << 5 | load << 20;
    bind(opcode, CoprocessorRegisterTransfer);
  }
  #undef arguments

  #define arguments \
    opcode.bit( 0,23)  /* immediate */
  for(uint4 immediateLo : range(16))
  for(uint4 immediateHi : range(16)) {
    auto opcode = pattern(".... 1111 ???? ???? ???? ???? ???? ????") | immediateLo << 4 | immediateHi << 20;
    bind(opcode, SoftwareInterrupt);
  }
  #undef arguments

  // DSP multiplies (ARMv5E) and ARMv6 — disassemble functions take raw opcode
  #define arguments opcode

  for(uint1 X : range(2))
  for(uint1 Y : range(2)) {
    auto opcode = pattern(".... 0001 0000 ???? ???? ???? 1??0 ????") | X << 5 | Y << 6;
    bind(opcode, Multiply16);  // SMLA<xy>
  }
  for(uint1 X : range(2))
  for(uint1 Y : range(2)) {
    auto opcode = pattern(".... 0001 0110 ???? ---- ???? 1??0 ????") | X << 5 | Y << 6;
    bind(opcode, Multiply16);  // SMUL<xy>
  }
  for(uint1 X : range(2))
  for(uint1 Y : range(2)) {
    auto opcode = pattern(".... 0001 0100 ???? ???? ???? 1??0 ????") | X << 5 | Y << 6;
    bind(opcode, Multiply16Long);  // SMLAL<xy>
  }
  for(uint1 Y : range(2)) {
    auto opcode = pattern(".... 0001 0010 ???? ???? ???? 1?00 ????") | Y << 6;
    bind(opcode, Multiply16Wide);  // SMLAW<y>
  }
  for(uint1 Y : range(2)) {
    auto opcode = pattern(".... 0001 0010 ???? ---- ???? 1?10 ????") | Y << 6;
    bind(opcode, Multiply16Wide);  // SMULW<y>
  }
  { auto opcode = pattern(".... 0001 0000 ???? ???? ---- 0101 ????"); bind(opcode, SaturatingAdd); } // QADD
  { auto opcode = pattern(".... 0001 0010 ???? ???? ---- 0101 ????"); bind(opcode, SaturatingAdd); } // QSUB
  { auto opcode = pattern(".... 0001 0100 ???? ???? ---- 0101 ????"); bind(opcode, SaturatingAdd); } // QDADD
  { auto opcode = pattern(".... 0001 0110 ???? ???? ---- 0101 ????"); bind(opcode, SaturatingAdd); } // QDSUB

  // ARMv6 PKHBT/PKHTB: bits[27:20]=0x68, bit5=0 (fixed), bit4=1 (fixed)
  // bit7=imm5[0], bit6=TB (0=PKHBT, 1=PKHTB)
  for(uint1 imm5_0 : range(2))
  for(uint1 TB : range(2)) {
    auto opcode = pattern(".... 0110 1000 ???? ???? ???? 0001 ????") | imm5_0 << 7 | TB << 6;
    bind(opcode, PackHalfword);
  }

  // SSAT: bits[27:20]=0x6A/0x6B (bit20=sat_imm[4]), bit5=0, bit4=1
  // USAT: bits[27:20]=0x6E/0x6F (bit22=1)
  // bit7=imm5[0], bit6=sh (shift type)
  for(uint1 sat4   : range(2))
  for(uint1 imm5_0 : range(2))
  for(uint1 sh     : range(2)) {
    { auto opcode = pattern(".... 0110 1010 ???? ???? ???? 0001 ????") | sat4 << 20 | imm5_0 << 7 | sh << 6; bind(opcode, Saturate); } // SSAT
    { auto opcode = pattern(".... 0110 1110 ???? ???? ???? 0001 ????") | sat4 << 20 | imm5_0 << 7 | sh << 6; bind(opcode, Saturate); } // USAT
  }

  { auto opcode = pattern(".... 0110 1000 ???? ???? ---- 1011 ????"); bind(opcode, Select); } // SEL

  // ARMv6 SIMD: bits[7:5]=op, bit4=1 (fixed), bit5 part of op
  // Signed: bits[27:20]=0x61; Unsigned: bits[27:20]=0x65
  for(uint3 op : range(8)) { auto opcode = pattern(".... 0110 0001 ???? ???? ---- 0001 ????") | op << 5; bind(opcode, SIMD); }
  for(uint3 op : range(8)) { auto opcode = pattern(".... 0110 0101 ???? ???? ---- 0001 ????") | op << 5; bind(opcode, SIMD); }

  #undef arguments

  #define arguments
  for(uint12 id : range(4096)) {
    if(armInstruction[id]) continue;
    auto opcode = pattern(".... ???? ???? ---- ---- ---- ???? ----") | id.bit(0,3) << 4 | id.bit(4,11) << 20;
    bind(opcode, Undefined);
  }
  #undef arguments

  #undef bind
  #undef pattern
}

auto ARM7TDMI::thumbInitialize() -> void {
  #define bind(id, name, ...) { \
    assert(!thumbInstruction[id]); \
    thumbInstruction[id] = &ARM7TDMI::thumbInstruction##name; \
    thumbDisassemble[id] = [=] { return thumbDisassemble##name(__VA_ARGS__); }; \
  }

  #define pattern(s) \
    std::integral_constant<uint16_t, bit::test(s)>::value

  for(uint3 d : range(8))
  for(uint3 m : range(8))
  for(uint4 mode : range(16)) {
    auto opcode = pattern("0100 00?? ???? ????") | d << 0 | m << 3 | mode << 6;
    bind(opcode, ALU, d, m, mode);
  }

  for(uint4 d : range(16))
  for(uint4 m : range(16))
  for(uint2 mode : range(4)) {
    if(mode == 3) continue;
    auto opcode = pattern("0100 01?? ???? ????") | d.bit(0,2) << 0 | m << 3 | d.bit(3) << 7 | mode << 8;
    bind(opcode, ALUExtended, d, m, mode);
  }

  for(uint8 immediate : range(256))
  for(uint3 d : range(8))
  for(uint1 mode : range(2)) {
    auto opcode = pattern("1010 ???? ???? ????") | immediate << 0 | d << 8 | mode << 11;
    bind(opcode, AddRegister, immediate, d, mode);
  }

  for(uint3 d : range(8))
  for(uint3 n : range(8))
  for(uint3 immediate : range(8))
  for(uint1 mode : range(2)) {
    auto opcode = pattern("0001 11?? ???? ????") | d << 0 | n << 3 | immediate << 6 | mode << 9;
    bind(opcode, AdjustImmediate, d, n, immediate, mode);
  }

  for(uint3 d : range(8))
  for(uint3 n : range(8))
  for(uint3 m : range(8))
  for(uint1 mode : range(2)) {
    auto opcode = pattern("0001 10?? ???? ????") | d << 0 | n << 3 | m << 6 | mode << 9;
    bind(opcode, AdjustRegister, d, n, m, mode);
  }

  for(uint7 immediate : range(128))
  for(uint1 mode : range(2)) {
    auto opcode = pattern("1011 0000 ???? ????") | immediate << 0 | mode << 7;
    bind(opcode, AdjustStack, immediate, mode);
  }

  for(uint1 link : range(2))
  for(uint3 _ : range(8))
  for(uint4 m : range(16)) {
    auto opcode = pattern("0100 0111 0??? ?---") | _ << 0 | m << 3 | link << 7;
    bind(opcode, BranchExchange, m, link);
  }

  for(uint11 displacement : range(2048)) {
    auto opcode = pattern("1111 0??? ???? ????") | displacement << 0;
    bind(opcode, BranchFarPrefix, displacement);
  }

  for(uint11 displacement : range(2048)) {
    auto opcode = pattern("1111 1??? ???? ????") | displacement << 0;
    bind(opcode, BranchFarSuffix, displacement);
  }

  for(uint11 displacement : range(2048)) {
    auto opcode = pattern("1110 1??? ???? ????") | displacement << 0;
    bind(opcode, BranchFarSuffixExchange, displacement);
  }

  for(uint11 displacement : range(2048)) {
    auto opcode = pattern("1110 0??? ???? ????") | displacement << 0;
    bind(opcode, BranchNear, displacement);
  }

  for(uint8 displacement : range(256))
  for(uint4 condition : range(16)) {
    if(condition == 15) continue;  //BNV
    auto opcode = pattern("1101 ???? ???? ????") | displacement << 0 | condition << 8;
    bind(opcode, BranchTest, displacement, condition);
  }

  for(uint8 immediate : range(256))
  for(uint3 d : range(8))
  for(uint2 mode : range(4)) {
    auto opcode = pattern("001? ???? ???? ????") | immediate << 0 | d << 8 | mode << 11;
    bind(opcode, Immediate, immediate, d, mode);
  }

  for(uint8 displacement : range(256))
  for(uint3 d : range(8)) {
    auto opcode = pattern("0100 1??? ???? ????") | displacement << 0 | d << 8;
    bind(opcode, LoadLiteral, displacement, d);
  }

  for(uint3 d : range(8))
  for(uint3 n : range(8))
  for(uint5 immediate : range(32))
  for(uint1 mode : range(2)) {
    auto opcode = pattern("0111 ???? ???? ????") | d << 0 | n << 3 | immediate << 6 | mode << 11;
    bind(opcode, MoveByteImmediate, d, n, immediate, mode);
  }

  for(uint3 d : range(8))
  for(uint3 n : range(8))
  for(uint5 immediate : range(32))
  for(uint1 mode : range(2)) {
    auto opcode = pattern("1000 ???? ???? ????") | d << 0 | n << 3 | immediate << 6 | mode << 11;
    bind(opcode, MoveHalfImmediate, d, n, immediate, mode);
  }

  for(uint8 list : range(256))
  for(uint3 n : range(8))
  for(uint1 mode : range(2)) {
    auto opcode = pattern("1100 ???? ???? ????") | list << 0 | n << 8 | mode << 11;
    bind(opcode, MoveMultiple, list, n, mode);
  }

  for(uint3 d : range(8))
  for(uint3 n : range(8))
  for(uint3 m : range(8))
  for(uint3 mode : range(8)) {
    auto opcode = pattern("0101 ???? ???? ????") | d << 0 | n << 3 | m << 6 | mode << 9;
    bind(opcode, MoveRegisterOffset, d, n, m, mode);
  }

  for(uint8 immediate : range(256))
  for(uint3 d : range(8))
  for(uint1 mode : range(2)) {
    auto opcode = pattern("1001 ???? ???? ????") | immediate << 0 | d << 8 | mode << 11;
    bind(opcode, MoveStack, immediate, d, mode);
  }

  for(uint3 d : range(8))
  for(uint3 n : range(8))
  for(uint5 offset : range(32))
  for(uint1 mode : range(2)) {
    auto opcode = pattern("0110 ???? ???? ????") | d << 0 | n << 3 | offset << 6 | mode << 11;
    bind(opcode, MoveWordImmediate, d, n, offset, mode);
  }

  for(uint3 d : range(8))
  for(uint3 m : range(8))
  for(uint5 immediate : range(32))
  for(uint2 mode : range(4)) {
    if(mode == 3) continue;
    auto opcode = pattern("000? ???? ???? ????") | d << 0 | m << 3 | immediate << 6 | mode << 11;
    bind(opcode, ShiftImmediate, d, m, immediate, mode);
  }

  for(uint8 immediate : range(256)) {
    auto opcode = pattern("1101 1111 ???? ????") | immediate << 0;
    bind(opcode, SoftwareInterrupt, immediate);
  }

  for(uint8 list : range(256))
  for(uint1 lrpc : range(2))
  for(uint1 mode : range(2)) {
    auto opcode = pattern("1011 ?10? ???? ????") | list << 0 | lrpc << 8 | mode << 11;
    bind(opcode, StackMultiple, list, lrpc, mode);
  }

  for(uint16 id : range(65536)) {
    if(thumbInstruction[id]) continue;
    auto opcode = pattern("???? ???? ???? ????") | id << 0;
    bind(opcode, Undefined);
  }

  #undef bind
  #undef pattern
}
