#include "brew/BrewDatabase.h"
#include "cpu/core/CPU.h"
#include "vfs/VirtualFileSystem.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <vector>

BrewDatabase::BrewDatabase(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory)
{
    setup_vtable();
}

BrewDatabase::~BrewDatabase() {
    if (db_) sqlite3_close(db_);
}

void BrewDatabase::setup_vtable() {
    object_ptr_ = shell_.malloc(6 * 4);
    vtable_ptr_ = shell_.malloc(6 * 4);
    memory_.write_value(object_ptr_, vtable_ptr_);

    // Z-Wheel's 0x0102C4E8 SQLite manager is IQI-shaped: its OpenDatabase-like
    // slot follows QueryInterface and returns status with the database out-param.
    const char* mgr_names[] = {
        "IDBMgr_AddRef", "IDBMgr_Release", "IDBMgr_QueryInterface",
        "IDBMgr_OpenDatabase", "IDBMgr_Remove", "IDBMgr_SetCacheSize",
    };
    for (int i = 0; i < 6; ++i) {
        memory_.write_value(vtable_ptr_ + (uint32_t)(i * 4), shell_.add_hook(mgr_names[i], this));
    }

    database_object_ptr_ = shell_.malloc(8 * 4);
    database_vtable_ptr_ = shell_.malloc(8 * 4);
    memory_.write_value(database_object_ptr_, database_vtable_ptr_);

    // Keep the paired database object on the same IQI-shaped ABI used by the
    // launcher SQLite manager. The legacy AEECLSID_DBMGR layout is different.
    const char* db_names[] = {
        "IDatabase_AddRef", "IDatabase_Release", "IDatabase_QueryInterface",
        "IDatabase_GetRecordCount", "IDatabase_Reset", "IDatabase_GetNextRecord",
        "IDatabase_GetRecordByID", "IDatabase_CreateRecord",
    };
    for (int i = 0; i < 8; ++i) {
        memory_.write_value(database_vtable_ptr_ + (uint32_t)(i * 4), shell_.add_hook(db_names[i], this));
    }
}

bool BrewDatabase::open_database(const std::string& name, bool create) {
    std::vector<std::string> candidates;
    if (name.find("fs:/") == 0) {
        candidates.push_back(name);
    } else {
        candidates.push_back("fs:/~0x01070798/" + name);
        candidates.push_back("fs:/mod/274755/" + name);
        candidates.push_back("fs:/sys/" + name);
    }

    for (const std::string& vpath : candidates) {
        auto res = shell_.get_vfs().resolve(vpath);
        if (!res.resolved || (!create && !res.exists)) {
            continue;
        }

        printf("    Resolved DB %s -> %s\n", vpath.c_str(), res.host_path.string().c_str());
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        int rc = sqlite3_open(res.host_path.string().c_str(), &db_);
        if (rc != SQLITE_OK) {
            printf("    [ERROR] sqlite3_open failed: %s\n", db_ ? sqlite3_errmsg(db_) : "<no db>");
            return false;
        }

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT name FROM sqlite_master WHERE type='table'", -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                printf("      Table found: %s\n", sqlite3_column_text(stmt, 0));
            }
            sqlite3_finalize(stmt);
        }
        return true;
    }

    printf("    [WARN] Could not resolve DB path: %s\n", name.c_str());
    return false;
}

int BrewDatabase::execute_sql(const std::string& sql, addr_t callback, addr_t user_data, CPU& cpu) {
    if (!db_) {
        printf("  SQL exec without open database\n");
        return SQLITE_MISUSE;
    }

    std::string upper = sql;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return (char)std::toupper(c); });

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        printf("  SQL prepare failed: %s\n", sqlite3_errmsg(db_));
        return rc;
    }

    bool saw_row = false;
    std::string first_text;
    int first_int = 0;
    int second_int = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        saw_row = true;
        int column_count = sqlite3_column_count(stmt);
        if (column_count > 0 && first_text.empty()) {
            const unsigned char* text = sqlite3_column_text(stmt, 0);
            if (text) first_text = reinterpret_cast<const char*>(text);
        }
        if (column_count > 0) first_int = sqlite3_column_int(stmt, 0);
        if (column_count > 1) second_int = sqlite3_column_int(stmt, 1);

        if (callback) {
            addr_t argv = shell_.malloc((uint32_t)(column_count * 4));
            addr_t names = shell_.malloc((uint32_t)(column_count * 4));
            for (int i = 0; i < column_count; ++i) {
                const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                if (!value) value = "";
                const char* name = sqlite3_column_name(stmt, i);
                if (!name) name = "";

                auto write_cstring = [&](const char* s) -> addr_t {
                    size_t len = std::strlen(s);
                    addr_t ptr = shell_.malloc((uint32_t)len + 1);
                    for (size_t j = 0; j <= len; ++j) {
                        memory_.write_value(ptr + (addr_t)j, (uint8_t)s[j], EndianMemory::Byte);
                    }
                    return ptr;
                };

                memory_.write_value(argv + (uint32_t)(i * 4), write_cstring(value));
                memory_.write_value(names + (uint32_t)(i * 4), write_cstring(name));
            }

            uint32_t saved[16];
            for (int i = 0; i < 16; ++i) saved[i] = cpu.get_reg((CPUReg)i);
            uint32_t saved_cpsr = cpu.get_reg(REG_CPSR);
            constexpr uint32_t magic_ret = 0xDEADBEE0u;

            cpu.set_reg(REG_R0, user_data);
            cpu.set_reg(REG_R1, (uint32_t)column_count);
            cpu.set_reg(REG_R2, argv);
            cpu.set_reg(REG_R3, names);
            cpu.set_reg(REG_LR, magic_ret);
            cpu.set_reg(REG_PC, callback & ~1u);
            cpu.set_reg(REG_CPSR, (saved_cpsr & ~0x20u) | ((callback & 1u) ? 0x20u : 0u));

            for (int guard = 0; guard < 100000 && !cpu.is_stopped() && cpu.get_reg(REG_PC) != magic_ret; ++guard) {
                cpu.step_once();
            }

            for (int i = 0; i < 16; ++i) cpu.set_reg((CPUReg)i, saved[i]);
            cpu.set_reg(REG_CPSR, saved_cpsr);
        }
    }

    int final_rc = (rc == SQLITE_DONE) ? SQLITE_OK : rc;
    sqlite3_finalize(stmt);

    printf("  SQL exec: \"%s\" rc=%d rows=%u cb=0x%08x user=0x%08x first=\"%s\"\n",
           sql.c_str(), final_rc, saw_row ? 1u : 0u, callback, user_data, first_text.c_str());

    // Z-Wheel's Tectoy DB wrappers pass a sqlite-style callback and a small
    // result flag for PRAGMA integrity_check. Until guest callbacks are
    // trampolined from HLE, reproduce the callback's observable result.
    if (upper.find("PRAGMA") == 0 && upper.find("INTEGRITY_CHECK") != std::string::npos && user_data) {
        memory_.write_value(user_data, first_text == "ok" ? 1u : 0u);
    }
    if (upper.find("SELECT VERSION, SUBVERSION FROM DBINFO") == 0 && user_data) {
        memory_.write_value(user_data, (uint32_t)first_int);
        memory_.write_value(user_data + 4, (uint32_t)second_int);
        memory_.write_value(user_data + 8, saw_row ? 1u : 0u);
    }

    return final_rc;
}

void BrewDatabase::handle_hook(const std::string& name, class CPU& cpu) {
    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    uint32_t r3 = cpu.get_reg(REG_R3);
    uint32_t r12 = cpu.get_reg(REG_R12);

    printf("BrewDatabase hook: %s (R0=0x%08x, R1=0x%08x, R2=0x%08x, R3=0x%08x, R12=0x%08x)\n", 
           name.c_str(), r0, r1, r2, r3, r12);
    
    // Suportar Thunk do Zeebo no DatabaseMgr também?
    uint32_t arg1 = r1;
    uint32_t arg2 = r2;
    uint32_t arg3 = r3;
    if (r1 >= 0xFF000000) {
        arg1 = r2;
        arg2 = r3;
        arg3 = cpu.get_reg(REG_R4);
    }

    if (name == "IDBMgr_AddRef" || name == "IDatabase_AddRef") {
        cpu.set_reg(REG_R0, r0);
    } else if (name == "IDBMgr_Release" || name == "IDatabase_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IDBMgr_QueryInterface" || name == "IDatabase_QueryInterface") {
        uint32_t pp = r2;
        if (pp) memory_.write_value(pp, r0);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IDBMgr_OpenDatabase") {
        uint32_t pszName = arg1;
        bool sqlmgr_outptr_abi = arg2 >= 0x10000000 && arg2 < 0xFF000000 && arg3 == 0;
        bool create = sqlmgr_outptr_abi || arg2 != 0;
        
        char name_buf[256];
        shell_.read_string(pszName, name_buf, sizeof(name_buf));
        printf("  IDBMgr_OpenDatabase: %s create=%u\n", name_buf, create ? 1u : 0u);
        bool ok = open_database(name_buf, create);
        if (sqlmgr_outptr_abi) {
            memory_.write_value(arg2, ok ? database_object_ptr_ : 0u);
            cpu.set_reg(REG_R0, ok ? 0u : 1u);
        } else {
            cpu.set_reg(REG_R0, ok ? database_object_ptr_ : 0u);
        }
    } else if (name == "IDBMgr_Remove") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IDBMgr_SetCacheSize") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IDatabase_GetRecordCount") {
        char sql_buf[512] = {};
        if (arg1 >= 0x10000000 && arg1 < 0xFF000000) {
            shell_.read_string(arg1, sql_buf, sizeof(sql_buf));
        }
        if (sql_buf[0]) {
            int rc = execute_sql(sql_buf, arg2, arg3, cpu);
            cpu.set_reg(REG_R0, (uint32_t)rc);
            return;
        }

        int count = 0;
        if (db_) {
            sqlite3_stmt* stmt;
            const char* sql = "SELECT COUNT(*) FROM sqlite_master WHERE type='table'";
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    count = sqlite3_column_int(stmt, 0);
                }
                sqlite3_finalize(stmt);
            }
        }
        printf("  IDatabase_GetRecordCount: returning %d\n", count);
        cpu.set_reg(REG_R0, count);
    } else if (name == "IDatabase_Reset") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IDatabase_GetNextRecord") {
        printf("  [IDatabase_GetNextRecord] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               r0, r1, r2, r3);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IDatabase_GetRecordByID") {
        printf("  [IDatabase_GetRecordByID] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               r0, r1, r2, r3);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IDatabase_CreateRecord") {
        printf("  [IDatabase_CreateRecord] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               r0, r1, r2, r3);
        cpu.set_reg(REG_R0, 0);
    } else {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), r0, r1, r2, r3);
        cpu.set_reg(REG_R0, 0);
    }
}
