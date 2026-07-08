#ifndef ZEEMU_BREW_SERVICE_H_
#define ZEEMU_BREW_SERVICE_H_

#include <string>

class CPU;

class BrewService {
public:
    virtual ~BrewService() = default;
    virtual void handle_hook(const std::string& name, class CPU& cpu) = 0;
};

#endif

