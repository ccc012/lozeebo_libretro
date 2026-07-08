#ifndef ZEEMU_RUNTIME_APP_RUNNER_H_
#define ZEEMU_RUNTIME_APP_RUNNER_H_

#include <cstdint>
#include <string>

int run_emulator(const std::string& mod_path, uint32_t target_clsid);

#endif