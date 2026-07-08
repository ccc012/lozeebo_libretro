#include "runtime/CliCommands.h"

#include "cpu/core/CPU.h"
#include "cpu/disassembler/Disassembler.h"
#include "cpu/memory/EndianMemory.h"
#include "cpu/memory/VirtualMemory.h"
#include "frontend/Launcher.h"
#include "runtime/AppRunner.h"
#include "tools/app_inspector/AppPackageInspector.h"
#include "tools/firmware_inspector/FirmwareInspector.h"
#include "tools/firmware_inspector/SplitFirmwareInspector.h"
#include "vfs/VirtualFileSystem.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

void print_usage(std::ostream& out, const char* argv0) {
    out << "Usage: " << argv0 << " <command> [args]" << std::endl;
    out << "Commands:" << std::endl;
    out << "  help                  Show this help" << std::endl;
    out << "  vfs-map <root_path>    Show VFS mapping" << std::endl;
    out << "  vfs-resolve <host_path> <guest_path>  Resolve a path" << std::endl;
    out << "  inspect-pkg <pkg_path> Inspect a Zeebo app package" << std::endl;
    out << "  inspect-fw <fw_path>   Inspect a firmware dump" << std::endl;
    out << "  inspect-split-fw <dir> Inspect split firmware partitions" << std::endl;
    out << "  scan-apps [root]       List launcher MIF/.mod matches" << std::endl;
    out << "  run-app <mod_path> [clsid]   Run a BREW module (.mod)" << std::endl;
    out << "  run-app-fast <mod_path> [clsid]  Run with dbgprintf suppressed (faster)" << std::endl;
    out << "  smoke <target> [seconds] [fast|normal] [frame|frame-png|frame-last|frame-last-png] [profile] [cpu] [hle] [media] [clean] [input=<script>]" << std::endl;
    out << "  disasm <file> [base] [count] [file-offset]  Disassemble ARM code" << std::endl;
    out << "  disasm-thumb <file> [base] [count] [file-offset]  Disassemble Thumb code" << std::endl;
    out << "  disasm-mod <target|mod> [start-va] [count] [thumb]  Disassemble a BREW module by VA" << std::endl;
    out << "  cpu-fixtures [filter] Run CPU interworking micro-fixtures" << std::endl;
    out << "  inspect-frame <ppm>    Inspect a captured guest GL PPM frame" << std::endl;
    out << "  crop-frame <ppm> <x> <y> <w> <h> [scale] [out.png]  Crop+upscale a PPM frame to PNG" << std::endl;
}

bool parse_u32(const char* s, uint32_t& val) {
    try {
        std::size_t pos = 0;
        val = std::stoul(s, &pos, 0);
        if (s[pos] != '\0') {
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_size(const char* s, std::size_t& val) {
    try {
        std::size_t pos = 0;
        val = std::stoull(s, &pos, 0);
        if (s[pos] != '\0') {
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_seconds_arg(const char* s, double& val) {
    try {
        std::size_t pos = 0;
        val = std::stod(s, &pos);
        if (s[pos] != '\0' || !std::isfinite(val) || val < 0.0) {
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

int cli_vfs_map(int argc, char* argv[]) {
    std::string root = (argc >= 3) ? argv[2] : ".";
    VirtualFileSystemInspector::print_default_map(root, std::cout);
    return 0;
}

int cli_vfs_resolve(int argc, char* argv[]) {
    if (argc < 4) return 1;
    VirtualFileSystemInspector::resolve_default(".", argv[2], argv[3], std::cout);
    return 0;
}

int cli_inspect_pkg(int argc, char* argv[]) {
    if (argc < 3) return 1;
    AppPackageInspector::inspect(argv[2], std::cout);
    return 0;
}

int cli_inspect_fw(int argc, char* argv[]) {
    if (argc < 3) return 1;
    FirmwareInspector::inspect(argv[2], std::cout);
    return 0;
}

int cli_inspect_split_fw(int argc, char* argv[]) {
    if (argc < 3) return 1;
    SplitFirmwareInspector::inspect(argv[2], std::cout);
    return 0;
}

int cli_scan_apps(int argc, char* argv[]) {
    std::string root = (argc >= 3) ? argv[2] : "roms";
    std::vector<AppEntry> apps = scan_apps(root);
    std::cout << "Apps found: " << apps.size() << "\n";
    for (const AppEntry& app : apps) {
        std::cout << std::left << std::setw(42) << app.name
                  << " id=" << std::right << std::setw(6) << std::setfill('0') << app.app_id
                  << std::setfill(' ') << " clsid=0x"
                  << std::hex << std::setw(8) << std::setfill('0') << app.clsid
                  << " mif_id=0x" << std::setw(8) << app.module_id
                  << " mif_ver=0x" << std::setw(8) << app.mif_version
                  << std::dec << std::setfill(' ')
                  << " applets=" << app.applet_count
                  << " classes=" << app.class_count
                  << " images=" << app.images.size()
                  << " ver=" << (app.version_text.empty() ? "-" : app.version_text)
                  << " mif=" << app.mif_path
                  << " mod=" << app.mod_path << "\n";
    }
    return 0;
}

int cli_run_app(int argc, char* argv[], bool fast) {
    if (argc < 3) return 1;

    uint32_t target_clsid = 0;
    if (argc >= 4) {
        parse_u32(argv[3], target_clsid);
    } else {
        // No CLSID given: resolve it from the MIF accompanying the .mod.
        target_clsid = resolve_clsid_for_mod(argv[2]);
        if (target_clsid != 0) {
            std::cout << "Resolved CLSID from MIF: 0x" << std::hex << std::setw(8)
                      << std::setfill('0') << target_clsid << std::dec
                      << std::setfill(' ') << std::endl;
        }
    }

    if (target_clsid == 0) {
        std::cerr << "Could not resolve applet CLSID for '" << argv[2]
                  << "'. Pass it explicitly: run-app <mod> <clsid>" << std::endl;
        return 2;
    }

    if (fast) {
        _putenv_s("ZEEMU_QUIET", "1");
    }
    return run_emulator(argv[2], target_clsid);
}

int cli_disasm(int argc, char* argv[], bool thumb) {
    if (argc < 3) {
        print_usage(std::cerr, argv[0]);
        return 2;
    }

    uint32_t base_address = 0;
    std::size_t instruction_count = 100;
    std::size_t file_offset = 0;
    if (argc >= 4 && !parse_u32(argv[3], base_address)) {
        std::cerr << "Invalid base address: " << argv[3] << std::endl;
        return 2;
    }
    if (argc >= 5 && !parse_size(argv[4], instruction_count)) {
        std::cerr << "Invalid instruction count: " << argv[4] << std::endl;
        return 2;
    }
    if (argc >= 6 && !parse_size(argv[5], file_offset)) {
        std::cerr << "Invalid file offset: " << argv[5] << std::endl;
        return 2;
    }

    bool ok = thumb
        ? Disassembler::disassemble_raw_thumb_le(argv[2], std::cout, base_address, instruction_count, file_offset)
        : Disassembler::disassemble_raw_arm_le(argv[2], std::cout, base_address, instruction_count, file_offset);
    return ok ? 0 : 1;
}

namespace {

constexpr uint32_t kFixtureBase = 0x00100000u;
constexpr uint32_t kFixtureSize = 0x00020000u;
constexpr uint32_t kCpsrThumb = 0x20u;

struct CpuExpect {
    uint32_t pc = 0;
    uint32_t lr = 0;
    bool thumb = false;
    bool check_lr = true;
};

struct CpuFixture {
    const char* name;
    std::function<bool(std::string&)> run;
};

void write_arm(EndianMemory& mem, uint32_t addr, uint32_t opcode) {
    mem.write_value(addr, opcode, EndianMemory::Word);
}

void write_thumb(EndianMemory& mem, uint32_t addr, uint16_t opcode) {
    mem.write_value(addr, opcode, EndianMemory::Halfword);
}

std::string hex32(uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

bool expect_value(const char* field, uint32_t actual, uint32_t expected, std::string& error) {
    if (actual == expected) {
        return true;
    }
    error = std::string(field) + " expected " + hex32(expected) + " got " + hex32(actual);
    return false;
}

bool expect_thumb(CPU& cpu, bool expected, std::string& error) {
    const bool actual = (cpu.get_reg(REG_CPSR) & kCpsrThumb) != 0;
    if (actual == expected) {
        return true;
    }
    error = std::string("CPSR.T expected ") + (expected ? "1" : "0") +
            " got " + (actual ? "1" : "0");
    return false;
}

bool run_cpu_case(const std::function<void(EndianMemory&, CPU&)>& setup,
                  uint32_t entry,
                  int steps,
                  const CpuExpect& expect,
                  std::string& error) {
    VirtualMemory vmem;
    EndianMemory mem(&vmem, LittleEndian);
    vmem.alloc_protect(kFixtureBase, kFixtureSize, Memory::Read | Memory::Write | Memory::Execute);

    auto cpu = std::make_unique<CPU>(mem);
    cpu->reset(entry);
    setup(mem, *cpu);
    for (int i = 0; i < steps && !cpu->is_stopped(); ++i) {
        cpu->step_once();
    }

    if (!expect_value("PC", cpu->get_reg(REG_PC), expect.pc, error)) {
        return false;
    }
    if (expect.check_lr && !expect_value("LR", cpu->get_reg(REG_LR), expect.lr, error)) {
        return false;
    }
    return expect_thumb(*cpu, expect.thumb, error);
}

std::vector<CpuFixture> make_cpu_fixtures() {
    return {
        {
            "arm_bx_to_thumb_preserves_lr",
            [](std::string& error) {
                const uint32_t pc = kFixtureBase + 0x0000u;
                const uint32_t target = kFixtureBase + 0x2001u;
                const uint32_t lr = 0xaabbccddu;
                return run_cpu_case([&](EndianMemory& mem, CPU& cpu) {
                    write_arm(mem, pc, 0xe12fff10u); // BX r0
                    cpu.set_reg(REG_R0, target);
                    cpu.set_reg(REG_LR, lr);
                }, pc, 1, {target, lr, true}, error);
            }
        },
        {
            "arm_blx_reg_to_arm_sets_lr",
            [](std::string& error) {
                const uint32_t pc = kFixtureBase + 0x1000u;
                const uint32_t target = kFixtureBase + 0x3000u;
                return run_cpu_case([&](EndianMemory& mem, CPU& cpu) {
                    write_arm(mem, pc, 0xe12fff30u); // BLX r0
                    cpu.set_reg(REG_R0, target);
                }, pc, 1, {target, pc + 4u, false}, error);
            }
        },
        {
            "arm_blx_reg_to_thumb_sets_lr",
            [](std::string& error) {
                const uint32_t pc = kFixtureBase + 0x2000u;
                const uint32_t target = kFixtureBase + 0x4001u;
                return run_cpu_case([&](EndianMemory& mem, CPU& cpu) {
                    write_arm(mem, pc, 0xe12fff30u); // BLX r0
                    cpu.set_reg(REG_R0, target);
                }, pc, 1, {target, pc + 4u, true}, error);
            }
        },
        {
            "arm_blx_immediate_to_thumb_sets_lr",
            [](std::string& error) {
                const uint32_t pc = kFixtureBase + 0x2800u;
                const uint32_t target = kFixtureBase + 0x3800u;
                return run_cpu_case([&](EndianMemory& mem, CPU&) {
                    // ARMv5T BLX immediate. cond=0xf, H=0, imm24=0x3fe:
                    // target = pc+8 + sign_extend(imm24 << 2) = pc+0x1000.
                    write_arm(mem, pc, 0xfa0003feu);
                }, pc, 1, {target, pc + 4u, true}, error);
            }
        },
        {
            "thumb_bx_to_arm_preserves_lr",
            [](std::string& error) {
                const uint32_t pc = kFixtureBase + 0x3000u;
                const uint32_t target = kFixtureBase + 0x5000u;
                const uint32_t lr = 0x11223345u;
                return run_cpu_case([&](EndianMemory& mem, CPU& cpu) {
                    write_thumb(mem, pc, 0x4700u); // BX r0
                    cpu.set_reg(REG_R0, target);
                    cpu.set_reg(REG_LR, lr);
                }, pc | 1u, 1, {target, lr, false}, error);
            }
        },
        {
            "thumb_blx_reg_to_arm_sets_thumb_lr",
            [](std::string& error) {
                const uint32_t pc = kFixtureBase + 0x4000u;
                const uint32_t target = kFixtureBase + 0x6000u;
                return run_cpu_case([&](EndianMemory& mem, CPU& cpu) {
                    write_thumb(mem, pc, 0x4780u); // BLX r0
                    cpu.set_reg(REG_R0, target);
                }, pc | 1u, 1, {target, pc + 3u, false}, error);
            }
        },
        {
            "thumb_blx_immediate_to_arm_sets_thumb_lr",
            [](std::string& error) {
                const uint32_t pc = kFixtureBase + 0x5000u;
                const uint32_t target = kFixtureBase + 0x6000u;
                return run_cpu_case([&](EndianMemory& mem, CPU&) {
                    write_thumb(mem, pc + 0u, 0xf000u); // BLX prefix, high displacement 0
                    write_thumb(mem, pc + 2u, 0xeffeu); // BLX suffix/exchange to pc+0x1000
                }, pc | 1u, 2, {target, pc + 5u, false}, error);
            }
        },
        {
            "thumb_pop_pc_to_arm_switches_state",
            [](std::string& error) {
                const uint32_t pc = kFixtureBase + 0x5800u;
                const uint32_t sp = kFixtureBase + 0x1e000u;
                const uint32_t target = kFixtureBase + 0x6800u;
                return run_cpu_case([&](EndianMemory& mem, CPU& cpu) {
                    write_thumb(mem, pc, 0xbd00u); // POP {pc}
                    mem.write_value(sp, target, EndianMemory::Word);
                    cpu.set_reg(REG_SP, sp);
                }, pc | 1u, 1, {target, 0, false, false}, error);
            }
        },
        {
            "thumb_bx_pc_switches_to_arm_veneer",
            [](std::string& error) {
                const uint32_t pc = kFixtureBase + 0x7000u;
                return run_cpu_case([&](EndianMemory& mem, CPU&) {
                    write_thumb(mem, pc + 0u, 0x4778u); // BX pc
                    write_thumb(mem, pc + 2u, 0x46c0u); // NOP/alignment halfword
                }, pc | 1u, 1, {pc + 4u, 0, false, false}, error);
            }
        },
        {
            "sourcery_interwork_call_via_r0_thumb_target",
            [](std::string& error) {
                const uint32_t pc = kFixtureBase + 0x8000u;
                const uint32_t target = kFixtureBase + 0x9001u;
                const uint32_t lr = 0xcafebabeu;
                return run_cpu_case([&](EndianMemory& mem, CPU& cpu) {
                    // Pattern observed in Sourcery libgcc _interwork_call_via_r0:
                    // Thumb BX PC, ARM TST/conditional LR fixup, then BX R0.
                    write_thumb(mem, pc + 0u, 0x4778u);      // BX pc
                    write_thumb(mem, pc + 2u, 0x46c0u);      // NOP/alignment halfword
                    write_arm(mem, pc + 4u, 0xe3100001u);    // TST r0, #1
                    write_arm(mem, pc + 8u, 0x052de008u);    // STREQ lr, [sp, #-8]!
                    write_arm(mem, pc + 12u, 0x024fe02cu);   // SUBEQ lr, pc, #44
                    write_arm(mem, pc + 16u, 0xe12fff10u);   // BX r0
                    cpu.set_reg(REG_R0, target);
                    cpu.set_reg(REG_LR, lr);
                    cpu.set_reg(REG_SP, kFixtureBase + 0x1f000u);
                }, pc | 1u, 5, {target, lr, true}, error);
            }
        }
    };
}

struct SmokeTarget {
    const char* name;
    const char* mod_path;
    const char* clsid;
};

const SmokeTarget kSmokeTargets[] = {
    {"quake", R"(.\roms\274802\mod\274802\quake.mod)", "0x01087A3C"},
    {"quake2", R"(.\roms\276153\mod\276153\quake2brew.mod)", "0x01087C1C"},
    {"double-dragon", R"(.\roms\274754\mod\274754\ddragonz.mod)", "0x0102F789"},
    {"ddz", R"(.\roms\274754\mod\274754\ddragonz.mod)", "0x0102F789"},
    {"fifa", R"(.\roms\274803\mod\274803\fifa09.mod)", "0x01087AE8"},
    {"fifa09", R"(.\roms\274803\mod\274803\fifa09.mod)", "0x01087AE8"},
    {"fifa-09", R"(.\roms\274803\mod\274803\fifa09.mod)", "0x01087AE8"},
    {"prboom", R"(.\roms\hb\prboom\mod\prboom\prboom.mod)", "0x0103D666"},
    {"openlara", R"(.\roms\hb\OpenLara\mod\OpenLara\OpenLara.mod)", "0x0103D8EC"},
    {"bjt", R"(.\roms\277083\mod\277083\bjt.mod)", "0x0108C1E1"},
    {"bejeweled-twist", R"(.\roms\277083\mod\277083\bjt.mod)", "0x0108C1E1"},
    {"alice", R"(.\roms\280386\mod\280386\alice.mod)", "0x010A2337"},
    {"action-hero-3d", R"(.\roms\274259\mod\274259\a3d.mod)", "0x01081970"},
    {"wild-dog", R"(.\roms\274259\mod\274259\a3d.mod)", "0x01081970"},
    {"a3d", R"(.\roms\274259\mod\274259\a3d.mod)", "0x01081970"},
    {"duke3d", R"(.\roms\hb\DukeNukem3D_114\duke3d\duke3d.mod)", "0x01034B16"},
    {"duke-nukem-3d", R"(.\roms\hb\DukeNukem3D_114\duke3d\duke3d.mod)", "0x01034B16"},
    {"allstarcards", R"(.\roms\280173\mod\280173\allstarcards.mod)", "0x010940DA"},
    {"all-star-cards", R"(.\roms\280173\mod\280173\allstarcards.mod)", "0x010940DA"},
    {"disney-all-star-cards", R"(.\roms\280173\mod\280173\allstarcards.mod)", "0x010940DA"},
    {"ironsight", R"(.\roms\280221\mod\280221\ironsight.mod)", "0x0109DA8F"},
    {"iron-sight", R"(.\roms\280221\mod\280221\ironsight.mod)", "0x0109DA8F"},
    {"armageddon-squadron", R"(.\roms\280214\mod\280214\asq.mod)", "0x0109DA8E"},
    {"armageddon", R"(.\roms\280214\mod\280214\asq.mod)", "0x0109DA8E"},
    {"asq", R"(.\roms\280214\mod\280214\asq.mod)", "0x0109DA8E"},
    {"zeebo-extreme-baja", R"(.\roms\277727\mod\277727\Bajaz.mod)", "0x0108FF07"},
    {"baja", R"(.\roms\277727\mod\277727\Bajaz.mod)", "0x0108FF07"},
    {"bajaz", R"(.\roms\277727\mod\277727\Bajaz.mod)", "0x0108FF07"},
    {"heavy-barrel", R"(.\roms\279889\mod\279889\hbarrel.mod)", "0x0109EC1C"},
    {"hbarrel", R"(.\roms\279889\mod\279889\hbarrel.mod)", "0x0109EC1C"},
    {"alien-breaker-deluxe", R"(.\roms\279369\mod\279369\abd.mod)", "0x0108E356"},
    {"alien-breaker", R"(.\roms\279369\mod\279369\abd.mod)", "0x0108E356"},
    {"abd", R"(.\roms\279369\mod\279369\abd.mod)", "0x0108E356"},
    {"crash-bandicoot", R"(.\roms\274214\mod\274214\cnk2.mod)", "0x01081984"},
    {"crash", R"(.\roms\274214\mod\274214\cnk2.mod)", "0x01081984"},
    {"cnk2", R"(.\roms\274214\mod\274214\cnk2.mod)", "0x01081984"},
    {"devil-may-cry", R"(.\roms\brew\271041\dmc_mas.mod)", "0x01070E36"},
    {"dmc", R"(.\roms\brew\271041\dmc_mas.mod)", "0x01070E36"},
    {"dmc-dante-x-vergil", R"(.\roms\brew\271041\dmc_mas.mod)", "0x01070E36"},
    {"resident-evil-4", R"(.\roms\276675\mod\276675\bio4_brew.mod)", "0x0108AF6C"},
    {"re4", R"(.\roms\276675\mod\276675\bio4_brew.mod)", "0x0108AF6C"},
    {"bio4", R"(.\roms\276675\mod\276675\bio4_brew.mod)", "0x0108AF6C"},
    {"peggle", R"(.\roms\278962\mod\278962\peggle.mod)", "0x01099CD6"},
    {"treino-cerebral", R"(.\roms\274804\mod\274804\brainchallenge.mod)", "0x01087A49"},
    {"brainchallenge", R"(.\roms\274804\mod\274804\brainchallenge.mod)", "0x01087A49"},
    {"brain-challenge", R"(.\roms\274804\mod\274804\brainchallenge.mod)", "0x01087A49"},
    {"tekken2", R"(.\roms\276731\mod\276731\tekken2.mod)", "0x0108D1B7"},
    {"tekken-2", R"(.\roms\276731\mod\276731\tekken2.mod)", "0x0108D1B7"},
    {"ovos", R"(.\roms\279036\mod\279036\game.mod)", "0x010963A5"},
    {"um-jogo-de-ovos", R"(.\roms\279036\mod\279036\game.mod)", "0x010963A5"},
    {"peteca", R"(.\roms\279159\mod\279159\zeebopeteca.mod)", "0x0108FF18"},
    {"nfs", R"(.\roms\276121\mod\276121\nfs.mod)", "0x0108C0BC"},
    {"need-for-speed", R"(.\roms\276121\mod\276121\nfs.mod)", "0x0108C0BC"},
    {"nfs-carbon", R"(.\roms\276121\mod\276121\nfs.mod)", "0x0108C0BC"},
    {"pacmania", R"(.\roms\276212\mod\276212\pacmania.mod)", "0x01087B72"},
    {"tutori3d", R"(.\roms\hb\TutorI3d\tutori3d\tutori3d.mod)", "0x010132E0"},
    {"tutor-i3d", R"(.\roms\hb\TutorI3d\tutori3d\tutori3d.mod)", "0x010132E0"},
    {"flashplayer", R"(.\roms\brew\c_ui_flashplayer\c_ui_flashplayer.mod)", "0x010ABA57"},
    {"c-ui-flashplayer", R"(.\roms\brew\c_ui_flashplayer\c_ui_flashplayer.mod)", "0x010ABA57"},
    {"c_ui_flashplayer", R"(.\roms\brew\c_ui_flashplayer\c_ui_flashplayer.mod)", "0x010ABA57"},
    {"conftest", R"(.\roms\hb\conftest\conftest\conftest.mod)", "0x01A2345F"},
    {"conf-test", R"(.\roms\hb\conftest\conftest\conftest.mod)", "0x01A2345F"},
    {"qxmeshbuddy", R"(.\roms\hb\qxmeshbuddy\mod\qxmeshbuddy.mod)", "0x0102C35B"},
    {"qx-mesh-buddy", R"(.\roms\hb\qxmeshbuddy\mod\qxmeshbuddy.mod)", "0x0102C35B"},
    {"ogles-demo-01", R"(.\roms\ogles_demo_01\ogles_demo_01\ogles_demo_01.mod)", "0x0101D5E6"},
    {"ogles-demo-02", R"(.\roms\ogles_demo_02\ogles_demo_02\ogles_demo_02.mod)", "0x0101E37B"},
    {"ogles-demo-03", R"(.\roms\ogles_demo_03\ogles_demo_03\ogles_demo_03.mod)", "0x0101E43A"},
    {"zeeboids", R"(.\roms\279382\mod\279382\zeeboids.mod)", "0x0108FF1A"},
    {"zeeboids-zeebo", R"(.\roms\279382\mod\279382\zeeboids.mod)", "0x0108FF1A"},
    {"zenonia", R"(.\roms\277455\mod\277455\zenonia.mod)", "0xBF2E2021"},
    {"zenonia-zeebo", R"(.\roms\277455\mod\277455\zenonia.mod)", "0xBF2E2021"},
    {"z-wheel", R"(.\roms\274755\mod\274755\tectoy.mod)", "0x01070798"},
    {"zwheel", R"(.\roms\274755\mod\274755\tectoy.mod)", "0x01070798"}
};

const SmokeTarget* find_smoke_target(const std::string& name) {
    for (const auto& target : kSmokeTargets) {
        if (name == target.name) {
            return &target;
        }
    }
    return nullptr;
}

void print_smoke_targets(std::ostream& out) {
    out << "Known smoke targets:" << std::endl;
    for (const auto&[name, mod_path, clsid] : kSmokeTargets) {
        out << "  " << name << " -> " << mod_path << " " << clsid << std::endl;
    }
}

int cli_disasm_mod_impl(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(std::cerr, argv[0]);
        return 2;
    }

    std::string mod_path = argv[2];
    if (const SmokeTarget* target = find_smoke_target(mod_path)) {
        mod_path = target->mod_path;
    }
    if (!std::filesystem::exists(mod_path)) {
        std::cerr << "Module not found: " << mod_path << std::endl;
        return 2;
    }

    uint32_t start_address = 0;
    std::size_t instruction_count = 100;
    if (argc >= 4 && !parse_u32(argv[3], start_address)) {
        std::cerr << "Invalid start VA: " << argv[3] << std::endl;
        return 2;
    }
    if (argc >= 5 && !parse_size(argv[4], instruction_count)) {
        std::cerr << "Invalid instruction count: " << argv[4] << std::endl;
        return 2;
    }
    const bool thumb = argc >= 6 && std::string(argv[5]) == "thumb";

    return Disassembler::disassemble_brew_module_le(mod_path, std::cout, start_address, instruction_count, thumb) ? 0 : 1;
}

#ifdef _WIN32
std::wstring widen(const std::string& s) {
    if (s.empty()) {
        return std::wstring();
    }
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (needed <= 0) {
        return std::wstring(s.begin(), s.end());
    }
    std::wstring out(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), needed);
    return out;
}

std::wstring quote_arg(const std::wstring& arg) {
    std::wstring out = L"\"";
    for (wchar_t ch : arg) {
        if (ch == L'"') {
            out += L"\\\"";
        } else {
            out += ch;
        }
    }
    out += L"\"";
    return out;
}
#endif

class ScopedEnvVar {
public:
    ScopedEnvVar(const char* name, const std::string& value)
        : name_(name) {
#ifdef _WIN32
        DWORD needed = GetEnvironmentVariableA(name_.c_str(), nullptr, 0);
        if (needed > 0) {
            old_value_.resize(needed);
            DWORD copied = GetEnvironmentVariableA(name_.c_str(), old_value_.data(), needed);
            if (copied > 0 && copied < needed) {
                old_value_.resize(copied);
                had_old_value_ = true;
            } else {
                old_value_.clear();
            }
        }
        SetEnvironmentVariableA(name_.c_str(), value.c_str());
#else
        if (const char* old = std::getenv(name_.c_str())) {
            old_value_ = old;
            had_old_value_ = true;
        }
        setenv(name_.c_str(), value.c_str(), 1);
#endif
    }

    ~ScopedEnvVar() {
#ifdef _WIN32
        SetEnvironmentVariableA(name_.c_str(), had_old_value_ ? old_value_.c_str() : nullptr);
#else
        if (had_old_value_) {
            setenv(name_.c_str(), old_value_.c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
#endif
    }

    ScopedEnvVar(const ScopedEnvVar&) = delete;
    ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;

private:
    std::string name_;
    std::string old_value_;
    bool had_old_value_ = false;
};

struct FrameData {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;
};

bool read_ppm_token(std::istream& in, std::string& token) {
    token.clear();
    char ch = 0;
    while (in.get(ch)) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            continue;
        }
        if (ch == '#') {
            std::string ignored;
            std::getline(in, ignored);
            continue;
        }
        token.push_back(ch);
        break;
    }
    while (in.get(ch)) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            break;
        }
        token.push_back(ch);
    }
    return !token.empty();
}

std::optional<FrameData> load_ppm_frame(const std::filesystem::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "could not open frame";
        return std::nullopt;
    }
    std::string token;
    if (!read_ppm_token(in, token) || token != "P6") {
        error = "unsupported frame format; expected binary PPM P6";
        return std::nullopt;
    }
    if (!read_ppm_token(in, token)) {
        error = "missing width";
        return std::nullopt;
    }
    int width = 0;
    int height = 0;
    int max_value = 0;
    try {
        width = std::stoi(token);
    } catch (...) {
        error = "invalid width";
        return std::nullopt;
    }
    if (!read_ppm_token(in, token)) {
        error = "missing height";
        return std::nullopt;
    }
    try {
        height = std::stoi(token);
    } catch (...) {
        error = "invalid height";
        return std::nullopt;
    }
    if (!read_ppm_token(in, token)) {
        error = "missing max value";
        return std::nullopt;
    }
    try {
        max_value = std::stoi(token);
    } catch (...) {
        error = "invalid max value";
        return std::nullopt;
    }
    if (width <= 0 || height <= 0 || max_value != 255) {
        error = "invalid PPM dimensions or max value";
        return std::nullopt;
    }
    FrameData frame;
    frame.width = width;
    frame.height = height;
    frame.rgb.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 3u);
    in.read(reinterpret_cast<char*>(frame.rgb.data()), static_cast<std::streamsize>(frame.rgb.size()));
    if (in.gcount() != static_cast<std::streamsize>(frame.rgb.size())) {
        error = "truncated pixel data";
        return std::nullopt;
    }
    return frame;
}

uint32_t crc32_bytes(const uint8_t* data, size_t size) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xffffffffu;
}

uint32_t adler32_bytes(const std::vector<uint8_t>& data) {
    uint32_t a = 1;
    uint32_t b = 0;
    for (uint8_t value : data) {
        a = (a + value) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16u) | a;
}

void append_be32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
    out.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
    out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
    out.push_back(static_cast<uint8_t>(value & 0xffu));
}

void write_png_chunk(std::ofstream& out, const char type[4], const std::vector<uint8_t>& data) {
    std::vector<uint8_t> length_bytes;
    append_be32(length_bytes, static_cast<uint32_t>(data.size()));
    out.write(reinterpret_cast<const char*>(length_bytes.data()), static_cast<std::streamsize>(length_bytes.size()));
    out.write(type, 4);
    if (!data.empty()) {
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    std::vector<uint8_t> crc_input;
    crc_input.insert(crc_input.end(), type, type + 4);
    crc_input.insert(crc_input.end(), data.begin(), data.end());
    std::vector<uint8_t> crc_bytes;
    append_be32(crc_bytes, crc32_bytes(crc_input.data(), crc_input.size()));
    out.write(reinterpret_cast<const char*>(crc_bytes.data()), static_cast<std::streamsize>(crc_bytes.size()));
}

bool write_png_rgb(const std::filesystem::path& path, const FrameData& frame, std::string& error) {
    std::vector<uint8_t> scanlines;
    const size_t row_bytes = static_cast<size_t>(frame.width) * 3u;
    scanlines.reserve((row_bytes + 1u) * static_cast<size_t>(frame.height));
    for (int y = 0; y < frame.height; ++y) {
        scanlines.push_back(0); // PNG filter type 0.
        const size_t row = static_cast<size_t>(y) * row_bytes;
        scanlines.insert(scanlines.end(), frame.rgb.begin() + static_cast<std::ptrdiff_t>(row),
                         frame.rgb.begin() + static_cast<std::ptrdiff_t>(row + row_bytes));
    }

    std::vector<uint8_t> zlib;
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    size_t offset = 0;
    while (offset < scanlines.size()) {
        const size_t chunk = std::min<size_t>(65535u, scanlines.size() - offset);
        const bool final_block = (offset + chunk) == scanlines.size();
        zlib.push_back(final_block ? 0x01 : 0x00);
        zlib.push_back(static_cast<uint8_t>(chunk & 0xffu));
        zlib.push_back(static_cast<uint8_t>((chunk >> 8u) & 0xffu));
        const uint16_t nlen = static_cast<uint16_t>(~static_cast<uint16_t>(chunk));
        zlib.push_back(static_cast<uint8_t>(nlen & 0xffu));
        zlib.push_back(static_cast<uint8_t>((nlen >> 8u) & 0xffu));
        zlib.insert(zlib.end(), scanlines.begin() + static_cast<std::ptrdiff_t>(offset),
                    scanlines.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
        offset += chunk;
    }
    append_be32(zlib, adler32_bytes(scanlines));

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "could not create PNG";
        return false;
    }
    const std::array<uint8_t, 8> signature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    out.write(reinterpret_cast<const char*>(signature.data()), static_cast<std::streamsize>(signature.size()));
    std::vector<uint8_t> ihdr;
    append_be32(ihdr, static_cast<uint32_t>(frame.width));
    append_be32(ihdr, static_cast<uint32_t>(frame.height));
    ihdr.push_back(8); // bit depth
    ihdr.push_back(2); // truecolor RGB
    ihdr.push_back(0); // compression
    ihdr.push_back(0); // filter
    ihdr.push_back(0); // interlace
    write_png_chunk(out, "IHDR", ihdr);
    write_png_chunk(out, "IDAT", zlib);
    write_png_chunk(out, "IEND", {});
    if (!out) {
        error = "failed while writing PNG";
        return false;
    }
    return true;
}

void remove_smoke_outputs_for_target(const std::string& target_name) {
    const std::filesystem::path logs_dir("logs");
    if (!std::filesystem::exists(logs_dir)) {
        return;
    }
    const std::string prefix = "smoke_" + target_name + "_";
    for (const auto& entry : std::filesystem::directory_iterator(logs_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0) {
            std::filesystem::remove(entry.path());
        }
    }
}

} // namespace

int cli_disasm_mod(int argc, char* argv[]) {
    return cli_disasm_mod_impl(argc, argv);
}

int cli_cpu_fixtures(int argc, char* argv[]) {
    const std::string filter = argc >= 3 ? argv[2] : "";
    const std::vector<CpuFixture> fixtures = make_cpu_fixtures();

    int ran = 0;
    int failed = 0;
    for (const CpuFixture& fixture : fixtures) {
        if (!filter.empty() && std::string(fixture.name).find(filter) == std::string::npos) {
            continue;
        }
        ++ran;
        std::string error;
        const bool ok = fixture.run(error);
        std::cout << (ok ? "PASS " : "FAIL ") << fixture.name;
        if (!ok) {
            ++failed;
            std::cout << " - " << error;
        }
        std::cout << std::endl;
    }

    if (ran == 0) {
        std::cerr << "No CPU fixture matched filter '" << filter << "'" << std::endl;
        return 2;
    }

    std::cout << "CPU fixtures: " << (ran - failed) << "/" << ran << " passed" << std::endl;
    return failed == 0 ? 0 : 1;
}

int cli_inspect_frame(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: inspect-frame <ppm>" << std::endl;
        return 2;
    }
    const std::filesystem::path frame_path(argv[2]);
    std::string error;
    std::optional<FrameData> frame = load_ppm_frame(frame_path, error);
    if (!frame) {
        std::cerr << "Could not inspect frame '" << frame_path.string() << "': " << error << std::endl;
        return 1;
    }

    uint64_t sum_r = 0;
    uint64_t sum_g = 0;
    uint64_t sum_b = 0;
    size_t non_black = 0;
    for (size_t i = 0; i + 2 < frame->rgb.size(); i += 3) {
        const uint8_t r = frame->rgb[i + 0];
        const uint8_t g = frame->rgb[i + 1];
        const uint8_t b = frame->rgb[i + 2];
        sum_r += r;
        sum_g += g;
        sum_b += b;
        if (r != 0 || g != 0 || b != 0) {
            ++non_black;
        }
    }
    const size_t pixels = static_cast<size_t>(frame->width) * static_cast<size_t>(frame->height);
    std::cout << "Frame: " << frame_path.string() << std::endl;
    std::cout << "  format: P6 RGB" << std::endl;
    std::cout << "  size:   " << frame->width << "x" << frame->height << std::endl;
    if (std::filesystem::exists(frame_path)) {
        std::cout << "  bytes:  " << std::filesystem::file_size(frame_path) << std::endl;
    }
    std::cout << "  avg:    rgb("
              << (pixels ? sum_r / pixels : 0) << ","
              << (pixels ? sum_g / pixels : 0) << ","
              << (pixels ? sum_b / pixels : 0) << ")" << std::endl;
    std::cout << "  nonblack_pixels: " << non_black << "/" << pixels << std::endl;
    return 0;
}

int cli_crop_frame(int argc, char* argv[]) {
    // Crop a rectangle of a captured PPM frame and write a nearest-upscaled PNG.
    // Replaces the ad hoc external Python/PIL crop+zoom used during graphics
    // bringup so frame inspection stays inside Zeemu (see tooling policy).
    if (argc < 7) {
        std::cerr << "Usage: crop-frame <ppm> <x> <y> <w> <h> [scale] [out.png]" << std::endl;
        return 2;
    }
    const std::filesystem::path frame_path(argv[2]);
    std::string error;
    std::optional<FrameData> frame = load_ppm_frame(frame_path, error);
    if (!frame) {
        std::cerr << "Could not load frame '" << frame_path.string() << "': " << error << std::endl;
        return 1;
    }

    const long x = std::strtol(argv[3], nullptr, 0);
    const long y = std::strtol(argv[4], nullptr, 0);
    const long w = std::strtol(argv[5], nullptr, 0);
    const long h = std::strtol(argv[6], nullptr, 0);
    long scale = (argc >= 8) ? std::strtol(argv[7], nullptr, 0) : 3;
    if (w <= 0 || h <= 0) {
        std::cerr << "Crop width/height must be positive" << std::endl;
        return 2;
    }
    if (scale < 1) scale = 1;
    if (scale > 16) scale = 16;

    // Clamp the requested rectangle to the source image so out-of-range
    // arguments produce a valid (smaller) crop instead of failing.
    const long fw = frame->width;
    const long fh = frame->height;
    const long x0 = std::clamp<long>(x, 0, fw);
    const long y0 = std::clamp<long>(y, 0, fh);
    const long x1 = std::clamp<long>(x + w, 0, fw);
    const long y1 = std::clamp<long>(y + h, 0, fh);
    const long cw = std::max<long>(0, x1 - x0);
    const long ch = std::max<long>(0, y1 - y0);
    if (cw == 0 || ch == 0) {
        std::cerr << "Crop rectangle is empty after clamping to " << fw << "x" << fh << std::endl;
        return 1;
    }

    FrameData out{};
    out.width = static_cast<int>(cw * scale);
    out.height = static_cast<int>(ch * scale);
    out.rgb.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height) * 3u);
    for (long dy = 0; dy < ch * scale; ++dy) {
        const long sy = y0 + dy / scale;
        for (long dx = 0; dx < cw * scale; ++dx) {
            const long sx = x0 + dx / scale;
            const size_t si = (static_cast<size_t>(sy) * fw + static_cast<size_t>(sx)) * 3u;
            const size_t di = (static_cast<size_t>(dy) * out.width + static_cast<size_t>(dx)) * 3u;
            out.rgb[di + 0] = frame->rgb[si + 0];
            out.rgb[di + 1] = frame->rgb[si + 1];
            out.rgb[di + 2] = frame->rgb[si + 2];
        }
    }

    std::filesystem::path out_path;
    if (argc >= 9) {
        out_path = argv[8];
    } else {
        out_path = frame_path;
        out_path.replace_extension();
        out_path += "_crop.png";
    }
    if (!write_png_rgb(out_path, out, error)) {
        std::cerr << "Could not write crop PNG '" << out_path.string() << "': " << error << std::endl;
        return 1;
    }
    std::cout << "Crop: " << frame_path.string() << " [" << x0 << "," << y0 << " "
              << cw << "x" << ch << "] x" << scale << " -> " << out_path.string()
              << " (" << out.width << "x" << out.height << ")" << std::endl;
    return 0;
}

int cli_smoke(int argc, char* argv[]) {
    if (argc < 3) {
        print_smoke_targets(std::cerr);
        return 2;
    }

    const SmokeTarget* target = find_smoke_target(argv[2]);
    if (!target) {
        std::cerr << "Unknown smoke target: " << argv[2] << std::endl;
        print_smoke_targets(std::cerr);
        return 2;
    }

    double seconds = 8.0;
    bool fast = true;
    bool dump_frame = false;
    bool dump_last_frame = false;
    bool write_png_frame = false;
    bool trace_profile = false;
    bool trace_cpu = false;
    bool trace_hle = false;
    bool trace_media = false;
    bool clean_logs = false;
    std::string input_script;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "fast") {
            fast = true;
            continue;
        }
        if (arg == "normal") {
            fast = false;
            continue;
        }
        if (arg == "frame" || arg == "dump-frame" || arg == "--frame" || arg == "--dump-frame") {
            dump_frame = true;
            continue;
        }
        if (arg == "frame-png" || arg == "png" || arg == "--frame-png") {
            dump_frame = true;
            write_png_frame = true;
            continue;
        }
        if (arg == "frame-last" || arg == "last-frame" || arg == "--frame-last" || arg == "--last-frame") {
            dump_frame = true;
            dump_last_frame = true;
            continue;
        }
        if (arg == "frame-last-png" || arg == "last-frame-png" || arg == "--frame-last-png" || arg == "--last-frame-png") {
            dump_frame = true;
            dump_last_frame = true;
            write_png_frame = true;
            continue;
        }
        if (arg == "profile" || arg == "--profile") {
            trace_profile = true;
            continue;
        }
        if (arg == "cpu" || arg == "cpu-profile" || arg == "guest-profile" ||
            arg == "--cpu" || arg == "--cpu-profile" || arg == "--guest-profile") {
            trace_cpu = true;
            continue;
        }
        if (arg == "hle" || arg == "--hle") {
            trace_hle = true;
            continue;
        }
        if (arg == "media" || arg == "--media") {
            trace_media = true;
            continue;
        }
        if (arg == "clean" || arg == "--clean") {
            clean_logs = true;
            continue;
        }
        if (arg.rfind("input=", 0) == 0 || arg.rfind("--input=", 0) == 0) {
            input_script = arg.substr(arg[0] == '-' ? 8 : 6);
            continue;
        }
        double parsed = 0.0;
        if (parse_seconds_arg(argv[i], parsed)) {
            seconds = parsed;
            continue;
        }
        std::cerr << "Invalid smoke argument: " << arg << std::endl;
        std::cerr << "Usage: smoke <target> [seconds] [fast|normal] [frame|frame-png|frame-last|frame-last-png] [profile] [cpu] [hle] [media] [clean] [input=<script>]" << std::endl;
        return 2;
    }
    if (seconds == 0.0) {
        seconds = 1.0;
    }

    // Resolve the CLSID from the target's MIF; fall back to the table value only
    // if no MIF/CLSID can be found (keeps MIF authoritative without a per-game
    // hardcoded CLSID requirement).
    char clsid_str[16];
    uint32_t resolved_clsid = resolve_clsid_for_mod(target->mod_path);
    if (resolved_clsid != 0) {
        std::snprintf(clsid_str, sizeof(clsid_str), "0x%08X", resolved_clsid);
        std::cout << "Smoke " << target->name << " resolved CLSID from MIF: "
                  << clsid_str << std::endl;
    } else {
        std::snprintf(clsid_str, sizeof(clsid_str), "%s", target->clsid);
        std::cout << "Smoke " << target->name
                  << " using fallback CLSID (no MIF found): " << clsid_str << std::endl;
    }

    std::filesystem::create_directories("logs");
    if (clean_logs) {
        remove_smoke_outputs_for_target(target->name);
    }
    const std::string mode = fast ? "fast" : "normal";
    const std::string run_command = fast ? "run-app-fast" : "run-app";
    const std::string log_prefix = std::string("smoke_") + target->name + "_" + mode;
    std::filesystem::path stdout_path = std::filesystem::path("logs") / (log_prefix + "_stdout.txt");
    std::filesystem::path stderr_path = std::filesystem::path("logs") / (log_prefix + "_stderr.txt");
    std::filesystem::path frame_path = std::filesystem::path("logs") / (log_prefix + "_frame.ppm");
    std::filesystem::path png_frame_path = std::filesystem::path("logs") / (log_prefix + "_frame.png");
    std::filesystem::remove(stdout_path);
    std::filesystem::remove(stderr_path);
    if (dump_frame) {
        std::filesystem::remove(frame_path);
    }
    if (write_png_frame) {
        std::filesystem::remove(png_frame_path);
    }

    std::vector<std::unique_ptr<ScopedEnvVar>> smoke_env;
    if (dump_frame) {
        smoke_env.push_back(std::make_unique<ScopedEnvVar>("ZEEMU_DUMP_GUEST_GL_FRAME", frame_path.string()));
        if (dump_last_frame) {
            smoke_env.push_back(std::make_unique<ScopedEnvVar>("ZEEMU_DUMP_GUEST_GL_FRAME_MODE", "last"));
        }
    }
    if (trace_profile) {
        smoke_env.push_back(std::make_unique<ScopedEnvVar>("ZEEMU_TRACE_RENDER_PROFILE", "1"));
    }
    if (trace_cpu) {
        smoke_env.push_back(std::make_unique<ScopedEnvVar>("ZEEMU_TRACE_GUEST_PROFILE", "1"));
        smoke_env.push_back(std::make_unique<ScopedEnvVar>("ZEEMU_TRACE_GUEST_PROGRESS", "1"));
        smoke_env.push_back(std::make_unique<ScopedEnvVar>("ZEEMU_TRACE_GUEST_PROGRESS_STEP", "2000000"));
    }
    if (trace_hle) {
        smoke_env.push_back(std::make_unique<ScopedEnvVar>("ZEEMU_TRACE_HLE", "1"));
    }
    if (trace_media) {
        smoke_env.push_back(std::make_unique<ScopedEnvVar>("ZEEMU_TRACE_MEDIA", "1"));
    }
    if (!input_script.empty()) {
        smoke_env.push_back(std::make_unique<ScopedEnvVar>("ZEEMU_INPUT_SCRIPT", input_script));
    }
    if (!std::getenv("ZEEMU_GAMEPAD_PROFILE")) {
        smoke_env.push_back(std::make_unique<ScopedEnvVar>("ZEEMU_GAMEPAD_PROFILE", "off"));
    }

#ifdef _WIN32
    SECURITY_ATTRIBUTES inherit_handles{};
    inherit_handles.nLength = sizeof(inherit_handles);
    inherit_handles.bInheritHandle = TRUE;

    HANDLE out_file = CreateFileW(widen(stdout_path.string()).c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit_handles, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE err_file = CreateFileW(widen(stderr_path.string()).c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit_handles, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out_file == INVALID_HANDLE_VALUE || err_file == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to create smoke log files." << std::endl;
        if (out_file != INVALID_HANDLE_VALUE) CloseHandle(out_file);
        if (err_file != INVALID_HANDLE_VALUE) CloseHandle(err_file);
        return 1;
    }

    std::wstring exe = widen(argv[0]);
    std::wstring cmdline = quote_arg(exe)
        + L" "
        + widen(run_command)
        + L" "
        + quote_arg(widen(target->mod_path))
        + L" "
        + quote_arg(widen(std::string(clsid_str)));

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = out_file;
    si.hStdError = err_file;
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> mutable_cmd(cmdline.begin(), cmdline.end());
    mutable_cmd.push_back(L'\0');

    const auto smoke_start = std::chrono::steady_clock::now();
    BOOL ok = CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
    CloseHandle(out_file);
    CloseHandle(err_file);
    if (!ok) {
        std::cerr << "Failed to start smoke process." << std::endl;
        return 1;
    }

    std::cout << "Smoke " << target->name << " (" << mode << ") running for " << seconds << "s..." << std::endl;
    DWORD wait_ms = static_cast<DWORD>(std::ceil(seconds * 1000.0));
    DWORD wait_result = WaitForSingleObject(pi.hProcess, wait_ms);
    bool smoke_timed_out = wait_result == WAIT_TIMEOUT;
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 2000);
    }
    DWORD child_exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &child_exit_code);
    const auto smoke_end = std::chrono::steady_clock::now();
    const double smoke_elapsed_ms = std::chrono::duration<double, std::milli>(smoke_end - smoke_start).count();
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
#else
    const auto smoke_start = std::chrono::steady_clock::now();
    std::string cmd = "\"";
    cmd += argv[0];
    cmd += "\" ";
    cmd += run_command;
    cmd += " \"";
    cmd += target->mod_path;
    cmd += "\" \"";
    cmd += clsid_str;
    cmd += "\" > \"";
    cmd += stdout_path.string();
    cmd += "\" 2> \"";
    cmd += stderr_path.string();
    cmd += "\" &";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        std::cerr << "Failed to start smoke process." << std::endl;
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    const auto smoke_end = std::chrono::steady_clock::now();
    const double smoke_elapsed_ms = std::chrono::duration<double, std::milli>(smoke_end - smoke_start).count();
    bool smoke_timed_out = true;
    int child_exit_code = 0;
#endif

    if (write_png_frame && std::filesystem::exists(frame_path)) {
        std::string error;
        std::optional<FrameData> frame = load_ppm_frame(frame_path, error);
        if (frame) {
            if (!write_png_rgb(png_frame_path, *frame, error)) {
                std::cerr << "Failed to write smoke frame PNG: " << error << std::endl;
            }
        } else {
            std::cerr << "Failed to read smoke frame for PNG conversion: " << error << std::endl;
        }
    }

    std::cout << "Smoke logs:" << std::endl;
    std::cout << "  status: " << (smoke_timed_out ? "timeout" : "exited")
              << " elapsed_ms=" << std::fixed << std::setprecision(1) << smoke_elapsed_ms
              << " exit_code=" << std::dec << child_exit_code << std::endl;
    std::cout << "  stdout: " << stdout_path.string() << std::endl;
    std::cout << "  stderr: " << stderr_path.string() << std::endl;
    if (dump_frame) {
        std::cout << "  frame:  " << frame_path.string();
        if (std::filesystem::exists(frame_path)) {
            std::cout << " (" << std::filesystem::file_size(frame_path) << " bytes)";
        } else {
            std::cout << " (not captured)";
        }
        std::cout << std::endl;
    }
    if (write_png_frame) {
        std::cout << "  png:    " << png_frame_path.string();
        if (std::filesystem::exists(png_frame_path)) {
            std::cout << " (" << std::filesystem::file_size(png_frame_path) << " bytes)";
        } else {
            std::cout << " (not captured)";
        }
        std::cout << std::endl;
    }
    if (trace_profile || trace_cpu || trace_hle || trace_media || clean_logs || dump_last_frame || !input_script.empty()) {
        std::cout << "Smoke options:";
        if (clean_logs) std::cout << " clean";
        if (dump_last_frame) std::cout << " frame-last";
        if (trace_profile) std::cout << " profile";
        if (trace_cpu) std::cout << " cpu";
        if (trace_hle) std::cout << " hle";
        if (trace_media) std::cout << " media";
        if (!input_script.empty()) std::cout << " input=" << input_script;
        std::cout << std::endl;
    }
    return 0;
}
