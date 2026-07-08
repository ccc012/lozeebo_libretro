#ifndef ZEEMU_VIRTUALMEMORY_H_
#define ZEEMU_VIRTUALMEMORY_H_

#include "cpu/memory/Memory.h"

/**
 * Memory backed by pages allocated directly from OS
 * (i.e. using Virtual{Alloc,Free} on WinNT, m{map,unmap} on UNIX
 */
class ZEEMU_EXPORT VirtualMemory : public Memory
{
public:
    VirtualMemory();
    virtual ~VirtualMemory();

    virtual std::string read(addr_t addr, std::size_t bytes);
    virtual void write(addr_t addr, const std::string& data);

    virtual void alloc_protect(addr_t addr, std::size_t size, int protect);
    virtual int get_protect(addr_t addr) const;
    void* get_host_address(addr_t addr);
    void* get_host_address_fast(addr_t addr) const {
        const unsigned idx = addr >> 12;
        if (idx < kPageTableSize && page_table_[idx]) {
            return static_cast<char*>(page_table_[idx]) + (addr & 0xFFFu);
        }
        return nullptr;
    }
    void alias_pages(addr_t alias_addr, addr_t target_addr, std::size_t size);
    static std::size_t page_size();

private:
    struct VMPrivate;
    VMPrivate *const pimpl_;

    // Flat page table for O(1) host address lookup (heap-allocated)
    static constexpr unsigned kPageTableBits = 20;
    static constexpr unsigned kPageTableSize = 1u << kPageTableBits;
    void** page_table_;
};

#endif
