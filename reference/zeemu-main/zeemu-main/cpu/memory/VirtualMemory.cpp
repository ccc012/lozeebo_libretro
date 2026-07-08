#include <map>
#include <cassert>
#include <algorithm>
#include <new>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#ifndef MAP_ANONYMOUS
// needed for Mac OS X (and other BSDs probably too)
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

#include <iostream>
#include <vector>
#include "cpu/memory/VirtualMemory.h"

struct PageSizeInit {
    PageSizeInit(std::size_t &page_size){
#ifdef _WIN32
        SYSTEM_INFO sInf;
        GetSystemInfo(&sInf);
        page_size = sInf.dwPageSize;
#else
        page_size = sysconf(_SC_PAGESIZE);
#endif
    }
};
static std::size_t s_page_size;
static PageSizeInit pageSizeInit(s_page_size);

#ifdef _WIN32
static DWORD protect_flags(int protect) {
    DWORD dProtect = PAGE_NOACCESS;

    if (Memory::Read & protect) {
        dProtect = PAGE_READONLY;
        if (Memory::Write & protect) {
            dProtect = PAGE_READWRITE;
        }
    }
    
    if (Memory::Execute & protect) {
        if (dProtect == PAGE_READONLY) dProtect = PAGE_EXECUTE_READ;
        else if (dProtect == PAGE_READWRITE) dProtect = PAGE_EXECUTE_READWRITE;
        else dProtect = PAGE_EXECUTE;
    }

    return dProtect;
}
#else
static int protect_flags(int protect) {
        int prot = PROT_NONE;
        if (Memory::Read & protect)
            prot |= PROT_READ;
        if (Memory::Write & protect)
            prot |= PROT_WRITE;
        // we are writing this for ARMY - EXECUTE has no sense, since its
        // equivalent to READ to allow the interpreter to fetch instruction
        // words. EXECUTE bit (and the whole MMU) should be rather implemented
        // in the CPU itself (just like CPURegisters, there would be a class,
        // e.g. "MMU", to handle this)
        //if (Memory::Execute & protect)
        //    prot |= PROT_EXECUTE;

        return prot;
}
#endif

// Jimmy Page rulezz :-)
class /*Jimmy*/ Page {
public:
    Page(int protect = Memory::Read | Memory::Write) {
#ifdef _WIN32
        DWORD dProtect = protect_flags(protect);
        if ((addr_ = VirtualAlloc(NULL, s_page_size, MEM_RESERVE | MEM_COMMIT, dProtect)) == NULL) {
            throw std::bad_alloc();
        }
#else
        int prot = protect_flags(protect);
        if ((addr_ = mmap(NULL, s_page_size, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED)
            throw std::bad_alloc();
#endif
    }

    Page(const Page& other) {
        // std::auto_ptr-like copy
        addr_ = other.addr_;
        other.addr_ = 0;
    }

    void *addr() const { return addr_; }
    void protect(int protect) {
#ifdef _WIN32
        DWORD dProtect = protect_flags(protect);
        DWORD oldProtect;
        if (!VirtualProtect(addr_, s_page_size, dProtect, &oldProtect)) {
            std::cerr << "Could not change page protection (" << std::hex << oldProtect << "->" << std::hex << dProtect << std::endl;
        }
#else
        int prot = protect_flags(protect);
        if (mprotect(addr_, s_page_size, prot) != 0) {
            std::cerr << "Could not change page protection to " << std::hex << prot << std::endl;
        }
#endif
    }

    ~Page() {
        if (addr_) {
#ifdef _WIN32

            if (!VirtualFree(addr_, 0, MEM_RELEASE)) {
                DWORD err = GetLastError();
                std::cerr << "Error releasing page " << std::hex << addr_ << ": GetLastError returned " << err << std::endl;
            }
#else
        if (munmap(addr_, s_page_size) != 0)
            std::cerr << "Error unmapping page " << std::hex << addr_ << std::endl;
#endif
        }
    }

private:
    mutable void *addr_;
};

static addr_t get_page_addr(addr_t addr)
{
    return addr - (addr % s_page_size);
}

struct VirtualMemory::VMPrivate
{
    struct vmmap_entry {
        vmmap_entry(void* host_, int protect_): host(host_), protect(protect_) {}
        void* host;
        int protect;
    };
    
    typedef std::map<addr_t, vmmap_entry> vmmap_type;
    vmmap_type vmmap;
    std::vector<std::pair<void*, std::size_t>> regions;

    void *alloc_protect(addr_t addr, std::size_t size, int protect) {
        addr_t page_addr = addr - (addr % s_page_size);
        std::size_t map_size = (size + s_page_size - 1) & ~(s_page_size - 1);
        
        int prot = protect;
        if (protect == -1) prot = Memory::Read | Memory::Write;
        
        DWORD dProtect = protect_flags(prot);
        void* host = VirtualAlloc(NULL, map_size, MEM_RESERVE | MEM_COMMIT, dProtect);
        if (!host) throw std::bad_alloc();
        
        regions.push_back(std::make_pair(host, map_size));
        
        for (std::size_t off = 0; off < map_size; off += s_page_size) {
            addr_t a = page_addr + off;
            void* h = (char*)host + off;
            vmmap.insert(std::make_pair(a, vmmap_entry(h, prot)));
        }
        
        return host;
    }

    template<typename T>
    void gather_scatter(addr_t addr, std::size_t size, T scatter_action)
    {
        std::size_t left = size;
        addr_t curr_addr = addr;

        while (left > 0) {
            addr_t page_addr = curr_addr - (curr_addr % s_page_size);
            std::size_t page_off = curr_addr - page_addr;
            std::size_t req_sz = std::min(left, (std::size_t)(s_page_size - page_off));

            auto it = vmmap.find(page_addr);
            if (it == vmmap.end()) {
                alloc_protect(page_addr, s_page_size, -1);
                it = vmmap.find(page_addr);
            }
            
            char* host_addr = (char*)it->second.host + page_off;
            scatter_action(host_addr, req_sz);
            
            curr_addr += req_sz;
            left -= req_sz;
        }
    }
};

VirtualMemory::VirtualMemory()
: pimpl_(new VMPrivate),
  page_table_(new void*[kPageTableSize]())
{}

void VirtualMemory::alloc_protect(addr_t addr, std::size_t size, int protect)
{
    addr_t page_addr = addr - (addr % s_page_size);
    std::size_t map_size = (size + s_page_size - 1) & ~(s_page_size - 1);
    pimpl_->alloc_protect(addr, size, protect);

    // Update flat page table for O(1) lookup
    for (std::size_t off = 0; off < map_size; off += s_page_size) {
        addr_t a = page_addr + off;
        unsigned idx = a >> 12;
        if (idx < kPageTableSize) {
            auto it = pimpl_->vmmap.find(a);
            if (it != pimpl_->vmmap.end())
                page_table_[idx] = it->second.host;
        }
    }
}

int VirtualMemory::get_protect(addr_t addr) const
{
    int prot = -1;

    VMPrivate::vmmap_type& vmmap = pimpl_->vmmap;

    addr_t page_addr = get_page_addr(addr);
    VMPrivate::vmmap_type::const_iterator it;

    if ((it = vmmap.find(page_addr)) != vmmap.end())
        prot = it->second.protect;

    return prot;
}

void* VirtualMemory::get_host_address(addr_t addr)
{
    // Fast path: O(1) flat page table lookup
    unsigned idx = addr >> 12;
    if (idx < kPageTableSize && page_table_[idx]) {
        return (char*)page_table_[idx] + (addr & 0xFFF);
    }

    // Slow path: std::map lookup (for auto-allocated pages)
    VMPrivate::vmmap_type& vmmap = pimpl_->vmmap;
    addr_t page_addr = addr - (addr % s_page_size);
    auto it = vmmap.find(page_addr);
    if (it != vmmap.end()) {
        // Cache in flat table for next time
        if (idx < kPageTableSize)
            page_table_[idx] = it->second.host;
        void* host = (char*)it->second.host + (addr - page_addr);
        return host;
    }
    return nullptr;
}

void VirtualMemory::alias_pages(addr_t alias_addr, addr_t target_addr, std::size_t size)
{
    std::size_t page_sz = s_page_size;
    for (std::size_t off = 0; off < size; off += page_sz) {
        unsigned alias_idx = (alias_addr + off) >> 12;
        unsigned target_idx = (target_addr + off) >> 12;
        if (target_idx < kPageTableSize && page_table_[target_idx]) {
            if (alias_idx < kPageTableSize) {
                page_table_[alias_idx] = page_table_[target_idx];
            }
            // Also update the std::map so get_host_address slow path works
            addr_t alias_page = alias_addr + off;
            addr_t target_page = target_addr + off;
            auto it = pimpl_->vmmap.find(target_page);
            if (it != pimpl_->vmmap.end()) {
                pimpl_->vmmap.insert_or_assign(alias_page, it->second);
            }
        }
    }
}

/*
** "Scatters" single request on a contiguous area of virtual memory [addr, addr+size)
** into possibly multiple requests on different pages of host memory,
** _in the order_ original virtual memory area maps to these host pages.
** Calls "ftor" for each request on host memory.
** Requirements on T:
**  * T::operator()(const char *host_addr, std::size_t req_sz)
**     - [host_addr, host_addr+req_sz) is the region in host memory
**       that recieves
*/

template<typename RandomIt>
struct write_scatter_action
{
    write_scatter_action(RandomIt begin): begin_(begin){}
    void operator()(char *host_addr, std::size_t req_sz) {
        const RandomIt end = begin_ + req_sz;
        std::copy(begin_, end, host_addr);
        begin_ = end;
    }
private:
    RandomIt begin_;
};

template<typename RandomIt>
static write_scatter_action<RandomIt> make_write_scatter_action(RandomIt it)
{
    return write_scatter_action<RandomIt>(it);
}

void VirtualMemory::write(addr_t addr, const std::string &data)
{
    pimpl_->gather_scatter(addr, data.size(), make_write_scatter_action(data.begin()));
}

template<typename OutputIt>
struct read_scatter_action
{
    read_scatter_action(OutputIt begin): begin_(begin){}
    void operator()(const char *host_addr, std::size_t req_sz) {
        begin_ = std::copy(host_addr, host_addr + req_sz, begin_);
    }
private:
    OutputIt begin_;
};

template<typename OutputIt>
static read_scatter_action<OutputIt> make_read_scatter_action(OutputIt it)
{
    return read_scatter_action<OutputIt>(it);
}

std::string VirtualMemory::read(addr_t addr, std::size_t bytes)
{
    std::string ret(bytes, 0);
    pimpl_->gather_scatter(addr, bytes, make_read_scatter_action(ret.begin()));
    return ret;
}

VirtualMemory::~VirtualMemory()
{
    for (auto& r : pimpl_->regions) {
        VirtualFree(r.first, 0, MEM_RELEASE);
    }
    delete pimpl_;
    delete[] page_table_;
}

std::size_t VirtualMemory::page_size()
{
    return s_page_size;
}
