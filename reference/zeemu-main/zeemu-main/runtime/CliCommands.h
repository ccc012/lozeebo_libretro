#ifndef ZEEMU_RUNTIME_CLI_COMMANDS_H_
#define ZEEMU_RUNTIME_CLI_COMMANDS_H_

#include <cstddef>
#include <cstdint>
#include <iosfwd>

void print_usage(std::ostream& out, const char* argv0);
bool parse_u32(const char* s, uint32_t& val);
bool parse_size(const char* s, std::size_t& val);

int cli_vfs_map(int argc, char* argv[]);
int cli_vfs_resolve(int argc, char* argv[]);
int cli_inspect_pkg(int argc, char* argv[]);
int cli_inspect_fw(int argc, char* argv[]);
int cli_inspect_split_fw(int argc, char* argv[]);
int cli_scan_apps(int argc, char* argv[]);
int cli_run_app(int argc, char* argv[], bool fast);
int cli_disasm(int argc, char* argv[], bool thumb);
int cli_disasm_mod(int argc, char* argv[]);
int cli_cpu_fixtures(int argc, char* argv[]);
int cli_inspect_frame(int argc, char* argv[]);
int cli_crop_frame(int argc, char* argv[]);
int cli_smoke(int argc, char* argv[]);

#endif
