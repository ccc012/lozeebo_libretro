#ifndef ZEEMU_MEMORY_DECORATOR_MEMORY_H__
#define ZEEMU_MEMORY_DECORATOR_MEMORY_H__

#include "cpu/memory/Memory.h"

class DecoratorMemory : public Memory {
public:
    virtual std::string read (addr_t addr, size_t bytes);
    virtual void write(addr_t addr, const std::string &data);
    virtual void alloc_protect(addr_t addr, std::size_t size, int protect);
    virtual int get_protect(addr_t addr) const;
    Memory* get_engine() const { return engine; }
protected:
    DecoratorMemory(Memory *engine);
    Memory *engine;
};

#endif
