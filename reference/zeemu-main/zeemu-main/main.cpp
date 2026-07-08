#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <filesystem>
#include "runtime/CliCommands.h"
#include "frontend/Launcher.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        return run_gui();
    }

    std::string cmd = argv[1];

    if (cmd == "help" || cmd == "-h" || cmd == "--help") {
        print_usage(std::cout, argv[0]);
        return 0;
    }

    if (cmd == "vfs-map") return cli_vfs_map(argc, argv);
    if (cmd == "vfs-resolve") return cli_vfs_resolve(argc, argv);
    if (cmd == "inspect-pkg") return cli_inspect_pkg(argc, argv);
    if (cmd == "inspect-fw") return cli_inspect_fw(argc, argv);
    if (cmd == "inspect-split-fw") return cli_inspect_split_fw(argc, argv);
    if (cmd == "scan-apps") return cli_scan_apps(argc, argv);
    if (cmd == "run-app") return cli_run_app(argc, argv, false);
    if (cmd == "run-app-fast") return cli_run_app(argc, argv, true);
    if (cmd == "smoke") return cli_smoke(argc, argv);
    if (cmd == "disasm") return cli_disasm(argc, argv, false);
    if (cmd == "disasm-thumb") return cli_disasm(argc, argv, true);
    if (cmd == "disasm-mod") return cli_disasm_mod(argc, argv);
    if (cmd == "cpu-fixtures") return cli_cpu_fixtures(argc, argv);
    if (cmd == "inspect-frame") return cli_inspect_frame(argc, argv);
    if (cmd == "crop-frame") return cli_crop_frame(argc, argv);

    std::cerr << "Unknown command: " << cmd << std::endl;
    return 1;
}
