#ifndef ZEEMU_BREW_RANDOM_H_
#define ZEEMU_BREW_RANDOM_H_

#include "brew/BrewShell.h"
#include <random>

class BrewRandom : public BrewService {
public:
    BrewRandom(BrewShell& shell, EndianMemory& memory);

    [[nodiscard]] addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, CPU& cpu) override;

private:
    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_ = 0;
    addr_t vtable_ptr_ = 0;
    std::mt19937 rng_;
};

#endif
