#include "brew/BrewSourceUtil.h"

#include "brew/BrewFileMgr.h"
#include "brew/BrewShell.h"
#include "cpu/core/CPU.h"
#include <algorithm>
#include <cstdio>

namespace {

static constexpr uint32_t SUCCESS = 0;
static constexpr uint32_t EFAILED = 1;
static constexpr int32_t IGETLINE_END = -1;
static constexpr int32_t IGETLINE_COMPLETE = 0;

template <typename T>
static void erase_released(std::vector<T>& items, addr_t object) {
    items.erase(std::remove_if(items.begin(), items.end(),
                               [object](const T& item) { return item.object == object; }),
                items.end());
}

} // namespace

BrewSourceUtil::BrewSourceUtil(BrewShell& shell, EndianMemory& memory, BrewFileMgr& file_mgr)
    : shell_(shell), memory_(memory), file_mgr_(file_mgr) {
    setup_vtable();
}

void BrewSourceUtil::setup_vtable() {
    // AEESource.h: ISourceUtil inherits IQueryInterface, then:
    // [3] GetLineFromSource [4] SourceFromSocket [5] SourceFromMemory
    // [6] SourceFromAStream [7] AStreamFromSource [8] PeekFromMemory [9] PeekFromSource
    vtable_ptr_ = shell_.malloc(10 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);

    const char* util_names[] = {
        "AddRef", "Release", "QueryInterface", "GetLineFromSource",
        "SourceFromSocket", "SourceFromMemory", "SourceFromAStream",
        "AStreamFromSource", "PeekFromMemory", "PeekFromSource"
    };
    for (int i = 0; i < 10; ++i) {
        memory_.write_value(vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("ISourceUtil_") + util_names[i], this));
    }

    source_vtable_ptr_ = shell_.malloc(6 * 4);
    const char* source_names[] = { "AddRef", "Release", "QueryInterface", "Readable", "Read", "Cancel" };
    for (int i = 0; i < 6; ++i) {
        memory_.write_value(source_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("ISource_") + source_names[i], this));
    }

    // IGetLine inherits IPeek/ISource-like operations and appends GetLine/UngetLine.
    getline_vtable_ptr_ = shell_.malloc(10 * 4);
    const char* getline_names[] = {
        "AddRef", "Release", "QueryInterface", "Read", "Readable",
        "Peek", "Peekable", "Advance", "GetLine", "UngetLine"
    };
    for (int i = 0; i < 10; ++i) {
        memory_.write_value(getline_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("IGetLine_") + getline_names[i], this));
    }
}

BrewSourceUtil::SourceState* BrewSourceUtil::create_source(std::vector<uint8_t> data) {
    SourceState state;
    state.object = shell_.malloc(4);
    state.vtable = source_vtable_ptr_;
    state.data = std::move(data);
    memory_.write_value(state.object, state.vtable);
    sources_.push_back(std::move(state));
    return &sources_.back();
}

BrewSourceUtil::GetLineState* BrewSourceUtil::create_getline(SourceState* source) {
    GetLineState state;
    state.object = shell_.malloc(4);
    state.vtable = getline_vtable_ptr_;
    state.source = source;
    state.pos = source ? source->pos : 0;
    memory_.write_value(state.object, state.vtable);
    getlines_.push_back(state);
    return &getlines_.back();
}

BrewSourceUtil::SourceState* BrewSourceUtil::find_source(addr_t object) {
    for (auto& source : sources_) {
        if (source.object == object) return &source;
    }
    return nullptr;
}

BrewSourceUtil::GetLineState* BrewSourceUtil::find_getline(addr_t object) {
    for (auto& getline : getlines_) {
        if (getline.object == object) return &getline;
    }
    return nullptr;
}

addr_t BrewSourceUtil::write_line_buffer(const std::string& line, uint32_t prefix_size) {
    if (prefix_size > 8) {
        prefix_size = 0;
    }
    addr_t buf = shell_.malloc(static_cast<uint32_t>(prefix_size + line.size() + 1));
    for (uint32_t i = 0; i < prefix_size; ++i) {
        memory_.write_value(buf + i, static_cast<uint8_t>(' '), EndianMemory::Byte);
    }
    for (size_t i = 0; i < line.size(); ++i) {
        memory_.write_value(buf + prefix_size + static_cast<uint32_t>(i), static_cast<uint8_t>(line[i]), EndianMemory::Byte);
    }
    memory_.write_value(buf + prefix_size + static_cast<uint32_t>(line.size()), static_cast<uint8_t>(0), EndianMemory::Byte);
    return buf;
}

void BrewSourceUtil::handle_hook(const std::string& name, CPU& cpu) {
    const uint32_t r0 = cpu.get_reg(REG_R0);
    const uint32_t r1 = cpu.get_reg(REG_R1);
    const uint32_t r2 = cpu.get_reg(REG_R2);
    const uint32_t r3 = cpu.get_reg(REG_R3);
    const bool is_thunk = (r1 >= 0xFF000000);
    const addr_t arg1 = is_thunk ? cpu.get_reg(REG_R5) : r1;
    const addr_t arg2 = is_thunk ? cpu.get_reg(REG_R6) : r2;
    const addr_t arg3 = is_thunk ? cpu.get_reg(REG_R7) : r3;

    if (name == "ISourceUtil_AddRef" || name == "ISource_AddRef" || name == "IGetLine_AddRef") {
        cpu.set_reg(REG_R0, 1);
        return;
    }
    if (name == "ISourceUtil_Release") {
        cpu.set_reg(REG_R0, 0);
        return;
    }
    if (name == "ISource_Release") {
        erase_released(sources_, r0);
        cpu.set_reg(REG_R0, 0);
        return;
    }
    if (name == "IGetLine_Release") {
        erase_released(getlines_, r0);
        cpu.set_reg(REG_R0, 0);
        return;
    }
    if (name == "ISourceUtil_QueryInterface" || name == "ISource_QueryInterface" || name == "IGetLine_QueryInterface") {
        if (arg2 && arg2 < 0xFF000000) memory_.write_value(arg2, r0);
        cpu.set_reg(REG_R0, SUCCESS);
        return;
    }

    if (name == "ISourceUtil_SourceFromAStream") {
        BrewFile* file = shell_.find_open_file(arg1);
        std::vector<uint8_t> bytes;
        if (!file || !file->read_remaining_from_current(bytes)) {
            printf("  ISourceUtil_SourceFromAStream stream=0x%08x failed\n", arg1);
            cpu.set_reg(REG_R0, EFAILED);
            return;
        }
        SourceState* source = create_source(std::move(bytes));
        if (arg2 && arg2 < 0xFF000000) memory_.write_value(arg2, source->object);
        printf("  ISourceUtil_SourceFromAStream stream=0x%08x -> source=0x%08x bytes=%zu\n",
               arg1, source->object, source->data.size());
        cpu.set_reg(REG_R0, SUCCESS);
        return;
    }

    if (name == "ISourceUtil_SourceFromMemory") {
        addr_t pBuf = arg1;
        uint32_t len = static_cast<uint32_t>(arg2);
        addr_t ppSource = memory_.read_value(cpu.get_reg(REG_SP) + 4);
        std::vector<uint8_t> bytes;
        if (pBuf && pBuf < 0xFF000000 && len < 16 * 1024 * 1024) {
            bytes.resize(len);
            for (uint32_t i = 0; i < len; ++i) {
                bytes[i] = static_cast<uint8_t>(memory_.read_value(pBuf + i, EndianMemory::Byte));
            }
        }
        SourceState* source = create_source(std::move(bytes));
        if (ppSource && ppSource < 0xFF000000) memory_.write_value(ppSource, source->object);
        printf("  ISourceUtil_SourceFromMemory buf=0x%08x len=%u -> source=0x%08x\n", pBuf, len, source->object);
        cpu.set_reg(REG_R0, SUCCESS);
        return;
    }

    if (name == "ISourceUtil_GetLineFromSource") {
        SourceState* source = find_source(arg1);
        if (!source) {
            printf("  ISourceUtil_GetLineFromSource source=0x%08x failed\n", arg1);
            cpu.set_reg(REG_R0, EFAILED);
            return;
        }
        GetLineState* getline = create_getline(source);
        if (arg3 && arg3 < 0xFF000000) memory_.write_value(arg3, getline->object);
        printf("  ISourceUtil_GetLineFromSource source=0x%08x buf=%u -> getline=0x%08x\n",
               arg1, arg2, getline->object);
        cpu.set_reg(REG_R0, SUCCESS);
        return;
    }

    if (name == "IGetLine_GetLine") {
        GetLineState* getline = find_getline(r0);
        addr_t pgl = arg1;
        if (!getline || !getline->source || !pgl || pgl >= 0xFF000000) {
            cpu.set_reg(REG_R0, static_cast<uint32_t>(IGETLINE_END));
            return;
        }
        const auto& data = getline->source->data;
        if (getline->pos >= data.size()) {
            cpu.set_reg(REG_R0, static_cast<uint32_t>(IGETLINE_END));
            return;
        }
        size_t start = getline->pos;
        size_t end = start;
        while (end < data.size() && data[end] != '\n' && data[end] != '\r') ++end;
        std::string line(reinterpret_cast<const char*>(data.data() + start), end - start);
        size_t next = end;
        if (next < data.size() && data[next] == '\r') ++next;
        if (next < data.size() && data[next] == '\n') ++next;
        getline->pos = next;
        getline->source->pos = next;
        // The Z-Wheel config parser reads GetLine::psz + nTypeEOL. Keep a
        // small prefix so the observed BREW caller sees the complete line at
        // that adjusted address.
        getline->last_line = write_line_buffer(line, static_cast<uint32_t>(arg2));
        memory_.write_value(pgl + 0, getline->last_line);
        memory_.write_value(pgl + 4, static_cast<uint32_t>(line.size()));
        memory_.write_value(pgl + 8, static_cast<uint8_t>(0), EndianMemory::Byte);
        memory_.write_value(pgl + 9, static_cast<uint8_t>(0), EndianMemory::Byte);
        printf("  IGetLine_GetLine -> '%.*s'\n", static_cast<int>(std::min<size_t>(line.size(), 80)), line.c_str());
        // AEESource.h defines positive EOL-specific return codes, but the
        // observed Z-Wheel parser advances the returned GetLine::psz by R0
        // before copying it into its config object. Returning a generic
        // complete-line code keeps IGETLINE_LineComplete(ret) true and avoids
        // dropping the first bytes of every config line.
        cpu.set_reg(REG_R0, IGETLINE_COMPLETE);
        return;
    }

    if (name == "ISource_Read" || name == "IGetLine_Read") {
        SourceState* source = (name == "ISource_Read") ? find_source(r0) : nullptr;
        if (!source) {
            if (auto* getline = find_getline(r0)) source = getline->source;
        }
        addr_t dst = arg1;
        auto count = static_cast<uint32_t>(arg2);
        if (!source || !dst || dst >= 0xFF000000) {
            cpu.set_reg(REG_R0, 0);
            return;
        }
        uint32_t n = static_cast<uint32_t>(std::min<size_t>(count, source->data.size() - source->pos));
        for (uint32_t i = 0; i < n; ++i) {
            memory_.write_value(dst + i, source->data[source->pos + i], EndianMemory::Byte);
        }
        source->pos += n;
        cpu.set_reg(REG_R0, n);
        return;
    }

    if (name == "ISource_Readable" || name == "IGetLine_Readable" || name == "IGetLine_Peekable") {
        if (arg1 && arg1 < 0xFF000000) memory_.write_value(arg1, static_cast<uint32_t>(0));
        cpu.set_reg(REG_R0, SUCCESS);
        return;
    }

    if (name == "ISource_Cancel" || name == "IGetLine_UngetLine" || name == "IGetLine_Advance") {
        cpu.set_reg(REG_R0, SUCCESS);
        return;
    }

    if (name == "ISourceUtil_SourceFromSocket" || name == "ISourceUtil_AStreamFromSource" ||
        name == "ISourceUtil_PeekFromMemory" || name == "ISourceUtil_PeekFromSource" ||
        name == "IGetLine_Peek") {
        printf("  %s unsupported in current trace\n", name.c_str());
        cpu.set_reg(REG_R0, EFAILED);
        return;
    }

    printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
           name.c_str(), r0, r1, r2, r3);
    cpu.set_reg(REG_R0, EFAILED);
}
