#include "runtime/AEEHelperTable.h"

#include "brew/BrewShell.h"
#include "cpu/memory/EndianMemory.h"

#include <string>

uint32_t build_aee_helper_table(BrewShell& shell, EndianMemory& memory) {
    uint32_t helper_table = shell.malloc(0x1000);
    for (uint32_t off = 0; off < 0x1000; off += 4) {
        memory.write_value(helper_table + off, 0u);
    }

    struct HelperSlot {
        uint32_t offset;
        const char* name;
    };
    static constexpr HelperSlot kSlots[] = {
        {0x000, "memmove"}, {0x004, "memset"}, {0x008, "strcpy"}, {0x00C, "strcat"},
        {0x010, "strcmp"}, {0x014, "strlen"}, {0x018, "strchr"}, {0x01C, "strrchr"},
        {0x020, "sprintf"}, {0x024, "wstrcpy"}, {0x028, "wstrcat"}, {0x02C, "wstrcmp"},
        {0x030, "wstrlen"}, {0x034, "wstrchr"}, {0x038, "wstrrchr"}, {0x03C, "wsprintf"},
        {0x040, "strtowstr"}, {0x044, "wstrtostr"}, {0x048, "wstrtofloat"},
        {0x04C, "floattowstr"}, {0x050, "utf8towstr"}, {0x054, "wstrtoutf8"},
        {0x058, "wstrlower"}, {0x05C, "wstrupper"}, {0x060, "chartype"},
        {0x064, "SetupNativeImage"}, {0x068, "malloc"}, {0x06C, "free"},
        {0x070, "wstrdup"}, {0x074, "realloc"}, {0x078, "wwritelongex"},
        {0x07C, "wstrsize"}, {0x080, "wstrncopyn"}, {0x084, "OEMStrLen"},
        {0x088, "OEMStrSize"}, {0x08C, "GetAEEVersion"}, {0x090, "atoi"},
        {0x094, "f_op"}, {0x098, "f_cmp"}, {0x09C, "dbgprintf"},
        {0x0A0, "wstrcompress"}, {0x0A4, "aee_LocalTimeOffset"}, {0x0A8, "aee_GetRand"},
        {0x0AC, "GetTimeMS"}, {0x0B0, "GetUpTimeMS"}, {0x0B4, "GetSeconds"},
        {0x0B8, "aee_GetJulianDate"}, {0x0BC, "sysfree"}, {0x0C0, "GetAppInstance"},
        {0x0C4, "strtoul"}, {0x0C8, "strncpy"}, {0x0CC, "strncmp"},
        {0x0D0, "stricmp"}, {0x0D4, "strnicmp"}, {0x0D8, "strstr"}, {0x0DC, "memcmp"},
        {0x0E0, "memchr"}, {0x0E4, "strexpand"}, {0x0E8, "stristr"}, {0x0EC, "memstr"},
        {0x0F0, "wstrncmp"}, {0x0F4, "strdup"}, {0x0F8, "strbegins"},
        {0x0FC, "strends"}, {0x100, "strchrend"}, {0x104, "strchrsend"},
        {0x108, "memrchr"}, {0x10C, "memchrend"}, {0x110, "memrchrbegin"},
        {0x114, "strlower"}, {0x118, "strupper"}, {0x11C, "wstricmp"},
        {0x120, "wstrnicmp"}, {0x124, "inet_aton"}, {0x128, "inet_ntoa"},
        {0x12C, "swapl"}, {0x130, "swaps"}, {0x134, "GetFSFree"},
        {0x138, "GetRAMFree"}, {0x13C, "vsprintf"}, {0x140, "vsnprintf"},
        {0x144, "snprintf"}, {0x148, "aee_JulianToSeconds"}, {0x14C, "strlcpy"},
        {0x150, "strlcat"}, {0x154, "wstrlcpy"}, {0x158, "wstrlcat"},
        {0x15C, "setstaticptr"}, {0x160, "f_assignstr"}, {0x164, "f_assignint"},
        {0x168, "wwritelong"}, {0x16C, "dbgheapmark"}, {0x170, "lockmem"},
        {0x174, "unlockmem"}, {0x178, "dumpheap"}, {0x17C, "strtod"}, {0x180, "f_calc"},
        {0x184, "sleep"}, {0x188, "getlasterror"}, {0x18C, "wgs84_to_degrees"},
        {0x190, "dbgevent"}, {0x194, "aee_IsBadPtr"}, {0x198, "aee_basename"},
        {0x19C, "aee_makepath"}, {0x1A0, "aee_splitpath"}, {0x1A4, "aee_stribegins"},
        {0x1A8, "aee_GetUTCSeconds"}, {0x1AC, "f_toint"}, {0x1B0, "f_get"},
        {0x1B4, "qsort"}, {0x1B8, "trunc"}, {0x1BC, "utrunc"}, {0x1C0, "err_realloc"},
        {0x1C4, "err_strdup"}, {0x1C8, "inet_pton"}, {0x1CC, "inet_ntop"},
        {0x1D0, "GetALSContext"},
    };
    for (const auto& slot : kSlots) {
        memory.write_value(helper_table + slot.offset, shell.add_hook(std::string("AEEHelper_") + slot.name));
    }

    return helper_table;
}
