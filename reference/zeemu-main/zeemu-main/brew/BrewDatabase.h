#ifndef ZEEMU_BREW_DATABASE_H_
#define ZEEMU_BREW_DATABASE_H_

#include "brew/BrewShell.h"
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"
#include "third_party/sqlite/sqlite3.h"
#include <string>

class BrewDatabase : public BrewService {
public:
    BrewDatabase(BrewShell& shell, EndianMemory& memory);
    ~BrewDatabase();

    addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    void setup_vtable();
    bool open_database(const std::string& name, bool create);
    int execute_sql(const std::string& sql, addr_t callback, addr_t user_data, class CPU& cpu);

    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_;
    addr_t vtable_ptr_;
    addr_t database_object_ptr_ = 0;
    addr_t database_vtable_ptr_ = 0;
    sqlite3* db_ = nullptr;
};

#endif
