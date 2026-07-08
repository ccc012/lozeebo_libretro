#ifndef ZEEMU_BREW_NET_H_
#define ZEEMU_BREW_NET_H_

#include "brew/BrewService.h"
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"
#include <string>

class BrewShell;

class BrewNet : public BrewService {
public:
    BrewNet(BrewShell& shell, EndianMemory& memory);

    addr_t get_object_ptr() const { return net_obj_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    void setup_netmgr();
    void setup_socket();
    std::string read_guest_string(addr_t addr, size_t max_len = 512) const;

    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t net_obj_ = 0;
    addr_t net_vtable_ = 0;
    addr_t socket_obj_ = 0;
    addr_t socket_vtable_ = 0;
    int last_error_ = 0;
};

#endif
