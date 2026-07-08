#include "brew/BrewShell.h"
#include "brew/BrewApplet.h"
#include "brew/BrewBitmap.h"
#include "cpu/core/CPU.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cfloat>
#include <ctime>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr uint16_t kNativeMagentaTransparent = 0xf81fu;

std::vector<std::unique_ptr<BrewBitmap>> g_native_image_bitmaps;

std::string lower_ascii_helper(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint32_t read_u32(EndianMemory& memory, addr_t addr) {
    return memory.read_value(addr, EndianMemory::Byte) |
           (memory.read_value(addr + 1, EndianMemory::Byte) << 8) |
           (memory.read_value(addr + 2, EndianMemory::Byte) << 16) |
           (memory.read_value(addr + 3, EndianMemory::Byte) << 24);
}

uint16_t read_u16(EndianMemory& memory, addr_t addr) {
    return static_cast<uint16_t>(memory.read_value(addr, EndianMemory::Byte) |
                                 (memory.read_value(addr + 1, EndianMemory::Byte) << 8));
}

uint32_t wstrlen_guest(EndianMemory& memory, addr_t addr, uint32_t max_chars = 4096) {
    if (!addr || addr >= 0xFF000000u) {
        return 0;
    }
    uint32_t len = 0;
    while (len < max_chars && memory.read_value(addr + len * 2, EndianMemory::Halfword) != 0) {
        ++len;
    }
    return len;
}

std::string read_guest_wide_ascii(EndianMemory& memory, addr_t addr, uint32_t max_chars = 4096) {
    std::string out;
    if (!addr || addr >= 0xFF000000u) {
        return out;
    }
    for (uint32_t i = 0; i < max_chars; ++i) {
        const uint16_t ch = static_cast<uint16_t>(memory.read_value(addr + i * 2, EndianMemory::Halfword));
        if (ch == 0) {
            break;
        }
        out.push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
    }
    return out;
}

void write_guest_wide_ascii(EndianMemory& memory, addr_t dst, uint32_t size_bytes, const std::string& text) {
    if (!dst || dst >= 0xFF000000u || size_bytes < 2) {
        return;
    }
    const uint32_t max_chars = size_bytes / 2;
    const uint32_t copy_chars = std::min<uint32_t>(static_cast<uint32_t>(text.size()), max_chars - 1);
    for (uint32_t i = 0; i < copy_chars; ++i) {
        memory.write_value(dst + i * 2, static_cast<uint16_t>(static_cast<unsigned char>(text[i])),
                           EndianMemory::Halfword);
    }
    memory.write_value(dst + copy_chars * 2, 0u, EndianMemory::Halfword);
}

int wstrcmp_guest(EndianMemory& memory, addr_t a, addr_t b, uint32_t max_chars, bool insensitive) {
    for (uint32_t i = 0; i < max_chars; ++i) {
        uint16_t av = static_cast<uint16_t>(memory.read_value(a + i * 2, EndianMemory::Halfword));
        uint16_t bv = static_cast<uint16_t>(memory.read_value(b + i * 2, EndianMemory::Halfword));
        if (insensitive) {
            av = static_cast<uint16_t>(std::tolower(static_cast<unsigned char>(av & 0xffu)));
            bv = static_cast<uint16_t>(std::tolower(static_cast<unsigned char>(bv & 0xffu)));
        }
        if (av != bv || av == 0 || bv == 0) {
            return static_cast<int>(av) - static_cast<int>(bv);
        }
    }
    return 0;
}

void write_double_result(CPU& cpu, double value) {
    uint64_t raw;
    std::memcpy(&raw, &value, sizeof(raw));
    cpu.set_reg(REG_R0, static_cast<uint32_t>(raw));
    cpu.set_reg(REG_R1, static_cast<uint32_t>(raw >> 32));
}

void trace_mem_bytes(const char* label, EndianMemory& memory, addr_t addr, uint32_t size) {
    if (!std::getenv("ZEEMU_TRACE_MEMOPS") || !addr || addr >= 0xFF000000u) {
        return;
    }

    const uint32_t count = std::min<uint32_t>(std::max<uint32_t>(size, 16), 32);
    printf("  [ZEEMU_TRACE_MEMOPS] %s addr=0x%08x size=%u bytes=", label, addr, size);
    for (uint32_t i = 0; i < count; ++i) {
        printf("%02x", static_cast<unsigned>(memory.read_value(addr + i, EndianMemory::Byte)));
        if (i + 1 < count) {
            printf(" ");
        }
    }
    if (size > count) {
        printf(" ...");
    }
    printf("\n");
}

constexpr uint32_t kAllocNoZmem = 0x80000000u;
constexpr int64_t kBrewEpochUnixSeconds = 315964800; // 1980-01-06 00:00:00 UTC.

uint32_t brew_time_seconds() {
    const std::time_t now = std::time(nullptr);
    if (now <= static_cast<std::time_t>(kBrewEpochUnixSeconds)) {
        return 0;
    }
    const auto seconds = static_cast<uint64_t>(now - static_cast<std::time_t>(kBrewEpochUnixSeconds));
    return seconds > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(seconds);
}

bool write_julian_date(EndianMemory& memory, addr_t dest, uint32_t brew_seconds) {
    if (dest == 0 || dest >= 0xFF000000u) {
        return false;
    }
    if (brew_seconds == 0) {
        brew_seconds = brew_time_seconds();
    }
    const std::time_t unix_seconds = static_cast<std::time_t>(
        static_cast<int64_t>(brew_seconds) + kBrewEpochUnixSeconds);
    std::tm tm_utc{};
#if defined(_WIN32)
    if (gmtime_s(&tm_utc, &unix_seconds) != 0) {
        return false;
    }
#else
    if (gmtime_r(&unix_seconds, &tm_utc) == nullptr) {
        return false;
    }
#endif
    memory.write_value(dest + 0, static_cast<uint16_t>(tm_utc.tm_year + 1900), EndianMemory::Halfword);
    memory.write_value(dest + 2, static_cast<uint16_t>(tm_utc.tm_mon + 1), EndianMemory::Halfword);
    memory.write_value(dest + 4, static_cast<uint16_t>(tm_utc.tm_mday), EndianMemory::Halfword);
    memory.write_value(dest + 6, static_cast<uint16_t>(tm_utc.tm_hour), EndianMemory::Halfword);
    memory.write_value(dest + 8, static_cast<uint16_t>(tm_utc.tm_min), EndianMemory::Halfword);
    memory.write_value(dest + 10, static_cast<uint16_t>(tm_utc.tm_sec), EndianMemory::Halfword);
    memory.write_value(dest + 12, static_cast<uint16_t>(tm_utc.tm_wday), EndianMemory::Halfword);
    return true;
}

} // namespace

bool BrewShell::handle_aee_helper_hook(const Hook& hook, CPU& cpu, int call_idx) {
    const bool is_helper_hook = hook.name.rfind("AEEHelper_", 0) == 0;
    if (!is_helper_hook && call_idx != 45 && call_idx != 46 && call_idx != 47 &&
        call_idx != 48 && call_idx != 57) {
        return false;
    }

    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    uint32_t r3 = cpu.get_reg(REG_R3);
    uint32_t sp = cpu.get_reg(REG_SP);

    if (hook.name == "AEEHelper_memmove") {
        uint32_t dst = r0;
        uint32_t src = r1;
        uint32_t size = r2;
        std::string tmp = memory_.read(src, size);
        memory_.write(dst, tmp);
        trace_mem_bytes("memmove src", memory_, src, size);
        trace_mem_bytes("memmove dst", memory_, dst, size);
        cpu.set_reg(REG_R0, dst);
    } else if (hook.name == "AEEHelper_memset") {
        uint32_t dst = r0;
        auto value = static_cast<uint8_t>(r1);
        uint32_t size = r2;
        memory_.fill(dst, value, size);
        cpu.set_reg(REG_R0, dst);
    } else if (hook.name == "AEEHelper_memcmp") {
        uint32_t a = r0;
        uint32_t b = r1;
        uint32_t size = r2;
        int result = 0;
        for (uint32_t i = 0; i < size; ++i) {
            int av = static_cast<int>(memory_.read_value(a + i, EndianMemory::Byte));
            int bv = static_cast<int>(memory_.read_value(b + i, EndianMemory::Byte));
            if (av != bv) {
                result = av - bv;
                break;
            }
        }
        cpu.set_reg(REG_R0, static_cast<uint32_t>(result));
    } else if (hook.name == "AEEHelper_strlen") {
        std::string s = read_guest_text(r0, 4096);
        if (std::getenv("ZEEMU_TRACE_STRINGS")) {
            const uint32_t trace_r5 = cpu.get_reg(REG_R5);
            const uint32_t display = (trace_r5 != 0 && trace_r5 < 0xFF000000u) ? memory_.read_value(trace_r5) : 0u;
            const uint32_t font = (trace_r5 != 0 && trace_r5 + 0x20 < 0xFF000000u) ? memory_.read_value(trace_r5 + 0x20) : 0u;
            const uint32_t vtable = (display != 0 && display < 0xFF000000u) ? memory_.read_value(display) : 0u;
            const uint32_t draw_text = (vtable != 0 && vtable + 0x10 < 0xFF000000u) ? memory_.read_value(vtable + 0x10) : 0u;
            printf("  [AEEHelper_strlen] ptr=0x%08x len=%u text=\"%s\" R5=0x%08x [R5]=0x%08x [R5+0x20]=0x%08x display_vt=0x%08x draw_text=0x%08x\n",
                   r0, static_cast<unsigned>(s.size()), s.c_str(), trace_r5, display, font, vtable, draw_text);
        }
        cpu.set_reg(REG_R0, static_cast<uint32_t>(s.size()));
    } else if (hook.name == "AEEHelper_wstrlen" || hook.name == "AEEHelper_wstrsize") {
        const uint32_t len = wstrlen_guest(memory_, r0);
        cpu.set_reg(REG_R0, hook.name == "AEEHelper_wstrsize" ? (len + 1) * 2 : len);
    } else if (hook.name == "AEEHelper_strcmp" || hook.name == "AEEHelper_stricmp") {
        std::string sa = read_guest_text(r0, 4096);
        std::string sb = read_guest_text(r1, 4096);
        if (hook.name == "AEEHelper_stricmp") {
            sa = lower_ascii_helper(sa);
            sb = lower_ascii_helper(sb);
        }
        if (std::getenv("ZEEMU_TRACE_STRINGS")) {
            printf("  [%s] \"%s\" vs \"%s\" -> %d\n", hook.name.c_str(), sa.c_str(), sb.c_str(), static_cast<int>(sa.compare(sb)));
        }
        cpu.set_reg(REG_R0, static_cast<uint32_t>(sa.compare(sb)));
    } else if (hook.name == "AEEHelper_wstrcmp" || hook.name == "AEEHelper_wstricmp") {
        cpu.set_reg(REG_R0, static_cast<uint32_t>(wstrcmp_guest(memory_, r0, r1, 4096,
                                                                hook.name == "AEEHelper_wstricmp")));
    } else if (hook.name == "AEEHelper_strncmp" || hook.name == "AEEHelper_strnicmp") {
        std::string sa = read_guest_text(r0, 4096);
        std::string sb = read_guest_text(r1, 4096);
        uint32_t n = r2;
        sa.resize(std::min<size_t>(sa.size(), n));
        sb.resize(std::min<size_t>(sb.size(), n));
        if (hook.name == "AEEHelper_strnicmp") {
            sa = lower_ascii_helper(sa);
            sb = lower_ascii_helper(sb);
        }
        cpu.set_reg(REG_R0, static_cast<uint32_t>(sa.compare(sb)));
    } else if (hook.name == "AEEHelper_wstrncmp" || hook.name == "AEEHelper_wstrnicmp") {
        cpu.set_reg(REG_R0, static_cast<uint32_t>(wstrcmp_guest(memory_, r0, r1, r2,
                                                                hook.name == "AEEHelper_wstrnicmp")));
    } else if (hook.name == "AEEHelper_strcpy" || hook.name == "AEEHelper_strncpy") {
        uint32_t dst = r0;
        uint32_t src = r1;
        uint32_t limit = (hook.name == "AEEHelper_strncpy") ? r2 : 4095u;
        uint32_t i = 0;
        std::string src_str = read_guest_text(src, 512);
        if (std::getenv("ZEEMU_TRACE_STRINGS")) {
            printf("  [%s] src=\"%s\" dst=0x%08x limit=%u\n", hook.name.c_str(), src_str.c_str(), dst, limit);
        }
        uint32_t copy_len = std::min<uint32_t>(limit, static_cast<uint32_t>(src_str.size()));
        for (; i < copy_len; ++i) {
            memory_.write_value(dst + i, static_cast<uint8_t>(src_str[i]), EndianMemory::Byte);
        }
        if (hook.name == "AEEHelper_strcpy") {
            memory_.write_value(dst + i, static_cast<uint8_t>(0), EndianMemory::Byte);
        } else if (copy_len < limit) {
            memory_.write_value(dst + i, static_cast<uint8_t>(0), EndianMemory::Byte);
            for (++i; i < limit; ++i) {
                memory_.write_value(dst + i, 0, EndianMemory::Byte);
            }
        }
        cpu.set_reg(REG_R0, dst);
    } else if (hook.name == "AEEHelper_wstrcpy") {
        uint32_t i = 0;
        do {
            const auto ch = static_cast<uint16_t>(memory_.read_value(r1 + i * 2, EndianMemory::Halfword));
            memory_.write_value(r0 + i * 2, ch, EndianMemory::Halfword);
            ++i;
            if (ch == 0) {
                break;
            }
        } while (i < 4096);
        cpu.set_reg(REG_R0, r0);
    } else if (hook.name == "AEEHelper_wstrcat") {
        const uint32_t dst_len = wstrlen_guest(memory_, r0);
        uint32_t i = 0;
        do {
            const auto ch = static_cast<uint16_t>(memory_.read_value(r1 + i * 2, EndianMemory::Halfword));
            memory_.write_value(r0 + (dst_len + i) * 2, ch, EndianMemory::Halfword);
            ++i;
            if (ch == 0) {
                break;
            }
        } while (i < 4096);
        cpu.set_reg(REG_R0, r0);
    } else if (hook.name == "AEEHelper_strchr" || hook.name == "AEEHelper_strrchr") {
        auto needle = static_cast<uint8_t>(r1);
        uint32_t found = 0;
        for (uint32_t i = 0; i < 4096; ++i) {
            auto c = static_cast<uint8_t>(memory_.read_value(r0 + i, EndianMemory::Byte));
            if (c == needle) {
                found = r0 + i;
                if (hook.name == "AEEHelper_strchr") {
                    break;
                }
            }
            if (c == 0) {
                break;
            }
        }
        cpu.set_reg(REG_R0, found);
    } else if (hook.name == "AEEHelper_wstrchr" || hook.name == "AEEHelper_wstrrchr") {
        const auto needle = static_cast<uint16_t>(r1);
        uint32_t found = 0;
        for (uint32_t i = 0; i < 4096; ++i) {
            const auto ch = static_cast<uint16_t>(memory_.read_value(r0 + i * 2, EndianMemory::Halfword));
            if (ch == needle) {
                found = r0 + i * 2;
                if (hook.name == "AEEHelper_wstrchr") {
                    break;
                }
            }
            if (ch == 0) {
                break;
            }
        }
        cpu.set_reg(REG_R0, found);
    } else if (hook.name == "AEEHelper_strstr") {
        std::string haystack = read_guest_text(r0, 4096);
        std::string needle = read_guest_text(r1, 512);
        const char* pos = std::strstr(haystack.c_str(), needle.c_str());
        uint32_t result = pos ? r0 + static_cast<uint32_t>(pos - haystack.c_str()) : 0;
        printf("  [AEEHelper_strstr] '%s' in extensions -> 0x%08x\n", needle.c_str(), result);
        cpu.set_reg(REG_R0, result);
    } else if (hook.name == "AEEHelper_stristr") {
        std::string haystack = lower_ascii_helper(read_guest_text(r0, 4096));
        std::string needle = lower_ascii_helper(read_guest_text(r1, 512));
        const char* pos = std::strstr(haystack.c_str(), needle.c_str());
        cpu.set_reg(REG_R0, pos ? r0 + static_cast<uint32_t>(pos - haystack.c_str()) : 0);
    } else if (hook.name == "AEEHelper_memstr") {
        std::string haystack = memory_.read(r0, std::min<uint32_t>(r2, 4096));
        std::string needle = read_guest_text(r1, 512);
        const size_t pos = haystack.find(needle);
        cpu.set_reg(REG_R0, pos == std::string::npos ? 0u : r0 + static_cast<uint32_t>(pos));
    } else if (hook.name == "AEEHelper_strbegins" || hook.name == "AEEHelper_aee_stribegins") {
        std::string prefix = read_guest_text(r0, 512);
        std::string s = read_guest_text(r1, 4096);
        if (hook.name == "AEEHelper_aee_stribegins") {
            prefix = lower_ascii_helper(prefix);
            s = lower_ascii_helper(s);
        }
        cpu.set_reg(REG_R0, s.rfind(prefix, 0) == 0 ? 1u : 0u);
    } else if (hook.name == "AEEHelper_strends") {
        std::string suffix = read_guest_text(r0, 512);
        std::string s = read_guest_text(r1, 4096);
        cpu.set_reg(REG_R0, s.size() >= suffix.size() &&
                            s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0 ? 1u : 0u);
    } else if (hook.name == "AEEHelper_strtoul") {
        std::string s = read_guest_text(r0, 128);
        char* end = nullptr;
        auto value = static_cast<uint32_t>(std::strtoul(s.c_str(), &end, static_cast<int>(r2)));
        if (r1 && end) {
            memory_.write_value(r1, r0 + static_cast<uint32_t>(end - s.c_str()));
        }
        cpu.set_reg(REG_R0, value);
    } else if (hook.name == "AEEHelper_atoi") {
        std::string s = read_guest_text(r0, 128);
        cpu.set_reg(REG_R0, static_cast<uint32_t>(std::atoi(s.c_str())));
    } else if (hook.name == "AEEHelper_wstrtofloat") {
        std::string s;
        const uint32_t len = wstrlen_guest(memory_, r0, 128);
        for (uint32_t i = 0; i < len; ++i) {
            s.push_back(static_cast<char>(memory_.read_value(r0 + i * 2, EndianMemory::Halfword) & 0xffu));
        }
        write_double_result(cpu, std::strtod(s.c_str(), nullptr));
    } else if (hook.name == "AEEHelper_floattowstr") {
        uint64_t raw = (static_cast<uint64_t>(r1) << 32) | r0;
        double value;
        std::memcpy(&value, &raw, sizeof(value));
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g", value);
        const uint32_t dst = r2;
        const uint32_t max_chars = r3;
        uint32_t i = 0;
        for (; i + 1 < max_chars && buf[i]; ++i) {
            memory_.write_value(dst + i * 2, static_cast<uint16_t>(buf[i]), EndianMemory::Halfword);
        }
        if (max_chars > 0) {
            memory_.write_value(dst + i * 2, 0u, EndianMemory::Halfword);
        }
        cpu.set_reg(REG_R0, 1);
    } else if (hook.name == "AEEHelper_GetAEEVersion") {
        cpu.set_reg(REG_R0, 0x00040218u);
    } else if (hook.name == "AEEHelper_SetupNativeImage") {
constexpr uint32_t kAEECLSID_WINBMP = 0x01004001u;
        const uint32_t cls = r0;
        const addr_t pBuffer = r1;
        const addr_t pii = r2;
        const addr_t pbRealloc = r3;

        addr_t result = 0;
        bool realloced = false;
        if (cls == kAEECLSID_WINBMP && pBuffer &&
            memory_.read_value(pBuffer, EndianMemory::Byte) == 'B' &&
            memory_.read_value(pBuffer + 1, EndianMemory::Byte) == 'M') {
            const uint32_t pixel_offset = read_u32(memory_, pBuffer + 10);
            const uint32_t dib_size = read_u32(memory_, pBuffer + 14);
            const int32_t width_signed = static_cast<int32_t>(read_u32(memory_, pBuffer + 18));
            const int32_t height_signed = static_cast<int32_t>(read_u32(memory_, pBuffer + 22));
            const uint16_t planes = read_u16(memory_, pBuffer + 26);
            const uint16_t bpp = read_u16(memory_, pBuffer + 28);
            const uint32_t compression = read_u32(memory_, pBuffer + 30);

            if (dib_size >= 40 && width_signed > 0 && height_signed != 0 && planes == 1 &&
                compression == 0 && (bpp == 8 || bpp == 24 || bpp == 32)) {
                const uint32_t width = static_cast<uint32_t>(width_signed);
                const uint32_t height = static_cast<uint32_t>(height_signed < 0 ? -height_signed : height_signed);
                const bool top_down = height_signed < 0;
                const uint32_t src_stride = ((width * bpp + 31) / 32) * 4;
                auto bmp_pixel_native = [&](uint32_t x, uint32_t y) -> uint16_t {
                    const uint32_t src_y = top_down ? y : (height - 1 - y);
                    addr_t src_row = pBuffer + pixel_offset + src_y * src_stride;
                    uint8_t r = 0;
                    uint8_t g = 0;
                    uint8_t b = 0;
                    if (bpp == 8) {
                        const uint8_t index = static_cast<uint8_t>(memory_.read_value(src_row + x, EndianMemory::Byte));
                        addr_t palette = pBuffer + 14 + dib_size + static_cast<addr_t>(index) * 4;
                        b = static_cast<uint8_t>(memory_.read_value(palette, EndianMemory::Byte));
                        g = static_cast<uint8_t>(memory_.read_value(palette + 1, EndianMemory::Byte));
                        r = static_cast<uint8_t>(memory_.read_value(palette + 2, EndianMemory::Byte));
                    } else {
                        addr_t px = src_row + x * (bpp / 8);
                        b = static_cast<uint8_t>(memory_.read_value(px, EndianMemory::Byte));
                        g = static_cast<uint8_t>(memory_.read_value(px + 1, EndianMemory::Byte));
                        r = static_cast<uint8_t>(memory_.read_value(px + 2, EndianMemory::Byte));
                    }
                    return rgb888_to_rgb565(r, g, b);
                };
                const bool magenta_keyed =
                    bmp_pixel_native(0, 0) == kNativeMagentaTransparent ||
                    bmp_pixel_native(width - 1, 0) == kNativeMagentaTransparent ||
                    bmp_pixel_native(0, height - 1) == kNativeMagentaTransparent ||
                    bmp_pixel_native(width - 1, height - 1) == kNativeMagentaTransparent;
                const uint32_t transparent = magenta_keyed ? kNativeMagentaTransparent : 0xffffffffu;
                auto bitmap = std::make_unique<BrewBitmap>(*this, memory_, static_cast<int>(width),
                                                            static_cast<int>(height), 16, 0, 0, -1,
                                                            transparent);
                addr_t dst = bitmap->get_buffer_ptr();
                const uint32_t dst_stride = static_cast<uint32_t>(bitmap->get_pitch());

                if (dst) {
                    for (uint32_t y = 0; y < height; ++y) {
                        const uint32_t src_y = top_down ? y : (height - 1 - y);
                        addr_t src_row = pBuffer + pixel_offset + src_y * src_stride;
                        addr_t dst_row = dst + y * dst_stride;
                        for (uint32_t x = 0; x < width; ++x) {
                            uint8_t r = 0;
                            uint8_t g = 0;
                            uint8_t b = 0;
                            if (bpp == 8) {
                                const uint8_t index = static_cast<uint8_t>(memory_.read_value(src_row + x, EndianMemory::Byte));
                                addr_t palette = pBuffer + 14 + dib_size + static_cast<addr_t>(index) * 4;
                                b = static_cast<uint8_t>(memory_.read_value(palette, EndianMemory::Byte));
                                g = static_cast<uint8_t>(memory_.read_value(palette + 1, EndianMemory::Byte));
                                r = static_cast<uint8_t>(memory_.read_value(palette + 2, EndianMemory::Byte));
                            } else {
                                addr_t px = src_row + x * (bpp / 8);
                                b = static_cast<uint8_t>(memory_.read_value(px, EndianMemory::Byte));
                                g = static_cast<uint8_t>(memory_.read_value(px + 1, EndianMemory::Byte));
                                r = static_cast<uint8_t>(memory_.read_value(px + 2, EndianMemory::Byte));
                            }
                            memory_.write_value(dst_row + x * 2, rgb888_to_rgb565(r, g, b), EndianMemory::Halfword);
                        }
                    }

                    if (pii) {
                        memory_.write_value(pii + 0, static_cast<uint16_t>(std::min<uint32_t>(width, 0xFFFFu)), EndianMemory::Halfword);
                        memory_.write_value(pii + 2, static_cast<uint16_t>(std::min<uint32_t>(height, 0xFFFFu)), EndianMemory::Halfword);
                        memory_.write_value(pii + 4, static_cast<uint16_t>(0), EndianMemory::Halfword);
                        memory_.write_value(pii + 6, static_cast<uint8_t>(0), EndianMemory::Byte);
                        memory_.write_value(pii + 8, static_cast<uint16_t>(std::min<uint32_t>(width, 0xFFFFu)), EndianMemory::Halfword);
                    }
                    result = bitmap->get_object_ptr();
                    realloced = true;
                    printf("  [AEEHelper_SetupNativeImage] WINBMP %ux%u %ubpp -> IBitmap 0x%08x pixels=0x%08x\n",
                           width, height, bpp, result, dst);
                    g_native_image_bitmaps.push_back(std::move(bitmap));
                }
            }
        }

        if (pbRealloc) {
            memory_.write_value(pbRealloc, static_cast<uint8_t>(realloced ? 1 : 0), EndianMemory::Byte);
        }
        cpu.set_reg(REG_R0, result);
    } else if (hook.name == "AEEHelper_malloc" || (!is_helper_hook && call_idx == 45)) {
        uint32_t size = r0 & ~kAllocNoZmem;
        if (size > 64u * 1024u * 1024u) {
            printf("  [AEEHelper_malloc] size=%u raw=0x%08x REJECTED (> 64MB) -> 0\n", size, r0);
            cpu.set_reg(REG_R0, 0);
        } else {
            addr_t addr = malloc(size, (r0 & kAllocNoZmem) == 0);
            if (!suppress_dbgprintf_) {
                printf("  [AEEHelper_malloc] size=%u%s -> 0x%08x\n",
                       size, (r0 & kAllocNoZmem) ? " ALLOC_NO_ZMEM" : "", addr);
            }
            trace_mem_bytes("malloc", memory_, addr, size);
            cpu.set_reg(REG_R0, addr);
        }
    } else if (hook.name == "AEEHelper_free" || hook.name == "AEEHelper_sysfree" ||
               (!is_helper_hook && (call_idx == 46 || call_idx == 47))) {
        if (std::getenv("ZEEMU_TRACE_FREE_BYTES") && r0 && r0 < 0xFF000000) {
            printf("  [%s] ptr=0x%08x lr=0x%08x bytes=", hook.name.c_str(), r0, cpu.get_reg(REG_LR));
            for (uint32_t i = 0; i < 32; ++i) {
                printf("%02x", static_cast<unsigned>(memory_.read_value(r0 + i, EndianMemory::Byte)));
                if (i + 1 < 32) {
                    printf(" ");
                }
            }
            printf("\n");
        }
        // BREW declares FREE/SYSFREE as void. Do not clobber R0: some guest
        // code keeps a decoder/result value live across FREE veneers.
    } else if (hook.name == "AEEHelper_realloc") {
        uint32_t size = r1 & ~kAllocNoZmem;
        addr_t addr = realloc_block(r0, size, (r1 & kAllocNoZmem) == 0);
        if (!suppress_dbgprintf_) {
            printf("  [AEEHelper_realloc] ptr=0x%08x old_size=%u size=%u%s -> 0x%08x\n",
                   r0, allocation_size(r0), size, (r1 & kAllocNoZmem) ? " ALLOC_NO_ZMEM" : "", addr);
        }
        cpu.set_reg(REG_R0, addr);
    } else if (hook.name == "AEEHelper_wstrdup") {
        addr_t src = r0;
        uint32_t len = 0;
        while (len < 256) {
            auto c = static_cast<uint16_t>(memory_.read_value(src + len * 2, EndianMemory::Halfword));
            ++len;
            if (c == 0) {
                break;
            }
        }
        addr_t dst = malloc(len * 2);
        for (uint32_t i = 0; i < len; ++i) {
            auto c = static_cast<uint16_t>(memory_.read_value(src + i * 2, EndianMemory::Halfword));
            memory_.write_value(dst + i * 2, c, EndianMemory::Halfword);
        }
        cpu.set_reg(REG_R0, dst);
    } else if (hook.name == "AEEHelper_GetAppInstance" || (!is_helper_hook && call_idx == 48)) {
        addr_t applet_obj = current_applet_obj_ ? current_applet_obj_ : applet_->get_object_ptr();
        if (pending_applet_output_ptr_ != 0 && pending_applet_output_ptr_ < 0xFF000000u) {
            const addr_t pending_applet = memory_.read_value(pending_applet_output_ptr_);
            if (pending_applet != 0 && pending_applet < 0xFF000000u) {
                applet_obj = pending_applet;
            }
        }
        if (std::getenv("ZEEMU_TRACE_APPINSTANCE")) {
            printf("  [AEEHelper_GetAppInstance] Returning applet obj 0x%08x (r0=0x%x r1=0x%x r2=0x%x)\n",
                   applet_obj, r0, r1, r2);
        }
        cpu.set_reg(REG_R0, applet_obj);
    } else if (hook.name == "AEEHelper_dbgprintf") {
        if (!suppress_dbgprintf_) {
            std::string formatted = format_guest(r0, cpu, 1, false);
            printf("  [AEEHelper_dbgprintf] %s\n", formatted.c_str());
        }
        cpu.set_reg(REG_R0, 0);
    } else if (hook.name == "AEEHelper_sprintf") {
        std::string formatted = format_guest(r1, cpu, 2, false);
        if (std::getenv("ZEEMU_TRACE_STRINGS")) {
            printf("  [AEEHelper_sprintf] dst=0x%08x fmt=0x%08x('%s') r2=0x%08x('%s') r3=0x%08x('%s') -> '%s'\n",
                   r0,
                   r1,
                   read_guest_text(r1, 256).c_str(),
                   r2,
                   read_guest_text(r2, 256).c_str(),
                   r3,
                   read_guest_text(r3, 256).c_str(),
                   formatted.c_str());
        }
        memory_.write(r0, formatted + '\0');
        cpu.set_reg(REG_R0, static_cast<uint32_t>(formatted.length()));
    } else if (hook.name == "AEEHelper_snprintf") {
        std::string formatted = format_guest(r2, cpu, 3, false);
        if (formatted.length() >= r1) {
            formatted = formatted.substr(0, r1 - 1);
        }
        if (std::getenv("ZEEMU_TRACE_STRINGS")) {
            printf("  [AEEHelper_snprintf] fmt='%s' size=%u -> '%s'\n",
                   read_guest_text(r2, 256).c_str(),
                   r1,
                   formatted.c_str());
        }
        memory_.write(r0, formatted + '\0');
        cpu.set_reg(REG_R0, static_cast<uint32_t>(formatted.length()));
    } else if (hook.name == "AEEHelper_vsprintf") {
        std::string formatted = format_guest(r1, cpu, 0, true, r2);
        if (std::getenv("ZEEMU_TRACE_STRINGS")) {
            printf("  [AEEHelper_vsprintf] fmt='%s' va=0x%08x -> '%s'\n",
                   read_guest_text(r1, 256).c_str(),
                   r2,
                   formatted.c_str());
        }
        memory_.write(r0, formatted + '\0');
        cpu.set_reg(REG_R0, static_cast<uint32_t>(formatted.length()));
    } else if (hook.name == "AEEHelper_vsnprintf") {
        std::string formatted = format_guest(r2, cpu, 0, true, r3);
        if (formatted.length() >= r1) {
            formatted = formatted.substr(0, r1 - 1);
        }
        if (std::getenv("ZEEMU_TRACE_STRINGS")) {
            printf("  [AEEHelper_vsnprintf] fmt='%s' size=%u va=0x%08x -> '%s'\n",
                   read_guest_text(r2, 256).c_str(),
                   r1,
                   r3,
                   formatted.c_str());
        }
        memory_.write(r0, formatted + '\0');
        cpu.set_reg(REG_R0, static_cast<uint32_t>(formatted.length()));
    } else if (hook.name == "AEEHelper_strcat") {
        std::string dest = read_guest_text(r0, 4096);
        std::string src = read_guest_text(r1, 4096);
        dest += src;
        memory_.write(r0, dest + '\0');
        cpu.set_reg(REG_R0, r0);
    } else if (hook.name == "AEEHelper_strdup") {
        std::string s = read_guest_text(r0, 4096);
        auto len = static_cast<uint32_t>(s.size());
        addr_t dst = malloc(len + 1);
        memory_.write(dst, s + '\0');
        cpu.set_reg(REG_R0, dst);
    } else if (hook.name == "AEEHelper_strlcpy") {
        std::string src = read_guest_text(r1, 4096);
        if (r2 > 0) {
            const uint32_t copy_len = std::min<uint32_t>(static_cast<uint32_t>(src.size()), r2 - 1);
            memory_.write(r0, src.substr(0, copy_len));
            memory_.write_value(r0 + copy_len, 0u, EndianMemory::Byte);
        }
        cpu.set_reg(REG_R0, static_cast<uint32_t>(src.size()));
    } else if (hook.name == "AEEHelper_strlcat") {
        std::string dst = read_guest_text(r0, r2);
        std::string src = read_guest_text(r1, 4096);
        if (r2 > dst.size() + 1) {
            const uint32_t copy_len = std::min<uint32_t>(static_cast<uint32_t>(src.size()),
                                                        r2 - static_cast<uint32_t>(dst.size()) - 1);
            memory_.write(r0 + static_cast<uint32_t>(dst.size()), src.substr(0, copy_len));
            memory_.write_value(r0 + static_cast<uint32_t>(dst.size()) + copy_len, 0u, EndianMemory::Byte);
        }
        cpu.set_reg(REG_R0, static_cast<uint32_t>(dst.size() + src.size()));
    } else if (hook.name == "AEEHelper_wstrlcpy") {
        const uint32_t src_len = wstrlen_guest(memory_, r1);
        if (r2 > 0 && r0 && r0 < 0xFF000000u) {
            const uint32_t copy_len = std::min<uint32_t>(src_len, r2 - 1);
            for (uint32_t i = 0; i < copy_len; ++i) {
                const auto ch = static_cast<uint16_t>(
                    memory_.read_value(r1 + i * 2, EndianMemory::Halfword));
                memory_.write_value(r0 + i * 2, ch, EndianMemory::Halfword);
            }
            memory_.write_value(r0 + copy_len * 2, 0u, EndianMemory::Halfword);
        }
        cpu.set_reg(REG_R0, src_len);
    } else if (hook.name == "AEEHelper_wstrlcat") {
        const uint32_t dst_len = wstrlen_guest(memory_, r0, r2);
        const uint32_t src_len = wstrlen_guest(memory_, r1);
        if (r2 > dst_len + 1 && r0 && r0 < 0xFF000000u) {
            const uint32_t copy_len = std::min<uint32_t>(src_len, r2 - dst_len - 1);
            for (uint32_t i = 0; i < copy_len; ++i) {
                const auto ch = static_cast<uint16_t>(
                    memory_.read_value(r1 + i * 2, EndianMemory::Halfword));
                memory_.write_value(r0 + (dst_len + i) * 2, ch, EndianMemory::Halfword);
            }
            memory_.write_value(r0 + (dst_len + copy_len) * 2, 0u, EndianMemory::Halfword);
        }
        cpu.set_reg(REG_R0, std::min<uint32_t>(dst_len, r2) + src_len);
    } else if (hook.name == "AEEHelper_wsprintf") {
        const addr_t dst = r0;
        const uint32_t size_bytes = r1;
        const std::string fmt = read_guest_wide_ascii(memory_, r2, 1024);
        std::string result;
        bool unsupported = false;
        int reg = 3;
        int stack_off = 0;
        const addr_t call_sp = sp;
        auto next_arg = [&]() -> uint32_t {
            if (reg < 4) {
                return cpu.get_reg(static_cast<CPUReg>(REG_R0 + reg++));
            }
            const uint32_t val = memory_.read_value(call_sp + static_cast<uint32_t>(stack_off * 4));
            ++stack_off;
            return val;
        };

        for (const char* p = fmt.c_str(); *p && !unsupported; ++p) {
            if (*p != '%') {
                result += *p;
                continue;
            }
            const char* start = p++;
            while (*p && std::strchr("-+ #0123456789.", *p)) {
                ++p;
            }
            while (*p && std::strchr("hlLzj", *p)) {
                ++p;
            }

            if (*p == '%') {
                result += '%';
            } else if (*p == 's') {
                const addr_t s_ptr = next_arg();
                const std::string s_text = read_guest_wide_ascii(memory_, s_ptr, 1024);
                char out[1100] = {};
                std::string spec(start, p - start + 1);
                std::snprintf(out, sizeof(out), spec.c_str(), s_text.c_str());
                result += out;
            } else if (*p == 'c') {
                const uint32_t ch = next_arg();
                char out[64] = {};
                std::string spec(start, p - start + 1);
                std::snprintf(out, sizeof(out), spec.c_str(), static_cast<char>(ch & 0xffu));
                result += out;
            } else if (*p == 'd' || *p == 'i' || *p == 'u' || *p == 'x' ||
                       *p == 'X' || *p == 'p' || *p == 'o') {
                const uint32_t val = next_arg();
                char out[128] = {};
                std::string spec(start, p - start + 1);
                std::snprintf(out, sizeof(out), spec.c_str(), val);
                result += out;
            } else if (*p == 'C' || *p == 'S' || *p == 'f' || *p == 'e' ||
                       *p == 'g' || *p == 'E' || *p == 'G') {
                unsupported = true;
            } else {
                result.append(start, p - start + 1);
            }
        }
        write_guest_wide_ascii(memory_, dst, size_bytes, unsupported ? std::string() : result);
        if (std::getenv("ZEEMU_TRACE_STRINGS")) {
            printf("  [AEEHelper_wsprintf] fmt=\"%s\" size=%u -> \"%s\"%s\n",
                   fmt.c_str(), size_bytes, result.c_str(), unsupported ? " unsupported" : "");
        }
        cpu.set_reg(REG_R0, 0);
    } else if (hook.name == "AEEHelper_strlower" || hook.name == "AEEHelper_strupper") {
        for (uint32_t i = 0; i < 4096; ++i) {
            auto ch = static_cast<uint8_t>(memory_.read_value(r0 + i, EndianMemory::Byte));
            if (ch == 0) {
                break;
            }
            ch = static_cast<uint8_t>(hook.name == "AEEHelper_strlower"
                                          ? std::tolower(static_cast<unsigned char>(ch))
                                          : std::toupper(static_cast<unsigned char>(ch)));
            memory_.write_value(r0 + i, ch, EndianMemory::Byte);
        }
        cpu.set_reg(REG_R0, r0);
    } else if (hook.name == "AEEHelper_strtowstr") {
        addr_t src = r0;
        addr_t dst = r1;
        uint32_t size_bytes = r2;
        if (!src || !dst || src >= 0xFF000000u || dst >= 0xFF000000u || size_bytes < 2) {
            cpu.set_reg(REG_R0, 0);
        } else {
            const uint32_t max_chars = size_bytes / 2;
            uint32_t written = 0;
            while (written + 1 < max_chars) {
                auto ch = static_cast<uint8_t>(memory_.read_value(src + written, EndianMemory::Byte));
                memory_.write_value(dst + written * 2, static_cast<uint16_t>(ch), EndianMemory::Halfword);
                ++written;
                if (ch == 0) {
                    break;
                }
            }
            if (written == 0 || memory_.read_value(dst + (written - 1) * 2, EndianMemory::Halfword) != 0) {
                memory_.write_value(dst + written * 2, static_cast<uint16_t>(0), EndianMemory::Halfword);
            }
            cpu.set_reg(REG_R0, dst);
        }
    } else if (hook.name == "AEEHelper_wstrtostr") {
        addr_t src = r0;
        addr_t dst = r1;
        uint32_t size_bytes = r2;
        if (!src || !dst || src >= 0xFF000000u || dst >= 0xFF000000u || size_bytes == 0) {
            cpu.set_reg(REG_R0, 0);
        } else {
            uint32_t written = 0;
            while (written + 1 < size_bytes) {
                uint16_t ch = static_cast<uint16_t>(memory_.read_value(src + written * 2, EndianMemory::Halfword));
                memory_.write_value(dst + written, static_cast<uint8_t>(ch & 0xff), EndianMemory::Byte);
                ++written;
                if (ch == 0) {
                    break;
                }
            }
            if (written == 0 || memory_.read_value(dst + written - 1, EndianMemory::Byte) != 0) {
                memory_.write_value(dst + written, static_cast<uint8_t>(0), EndianMemory::Byte);
            }
            cpu.set_reg(REG_R0, dst);
        }
    } else if (hook.name == "AEEHelper_utf8towstr") {
        addr_t src = r0;
        int32_t src_len = static_cast<int32_t>(r1);
        addr_t dst = r2;
        uint32_t dst_size_bytes = r3;
        if (!src || !dst || src >= 0xFF000000u || dst >= 0xFF000000u || dst_size_bytes < 2) {
            cpu.set_reg(REG_R0, 0);
        } else {
            const uint32_t max_chars = dst_size_bytes / 2;
            uint32_t in_pos = 0;
            uint32_t out_chars = 0;
            bool ok = true;
            while (out_chars + 1 < max_chars) {
                if (src_len >= 0 && in_pos >= static_cast<uint32_t>(src_len)) {
                    break;
                }
                uint32_t ch = memory_.read_value(src + in_pos, EndianMemory::Byte);
                if (ch == 0) {
                    break;
                }
                ++in_pos;
                if (ch >= 0xc2 && ch <= 0xdf) {
                    if (src_len >= 0 && in_pos >= static_cast<uint32_t>(src_len)) {
                        ok = false;
                        ch = '?';
                    } else {
                        uint32_t b1 = memory_.read_value(src + in_pos, EndianMemory::Byte);
                        ++in_pos;
                        if ((b1 & 0xc0u) == 0x80u) {
                            ch = ((ch & 0x1fu) << 6) | (b1 & 0x3fu);
                        } else {
                            ok = false;
                            ch = '?';
                        }
                    }
                } else if (ch >= 0xe0 && ch <= 0xef) {
                    if (src_len >= 0 && in_pos + 1 >= static_cast<uint32_t>(src_len)) {
                        ok = false;
                        ch = '?';
                        in_pos = static_cast<uint32_t>(std::max<int32_t>(src_len, 0));
                    } else {
                        uint32_t b1 = memory_.read_value(src + in_pos, EndianMemory::Byte);
                        uint32_t b2 = memory_.read_value(src + in_pos + 1, EndianMemory::Byte);
                        in_pos += 2;
                        if ((b1 & 0xc0u) == 0x80u && (b2 & 0xc0u) == 0x80u) {
                            ch = ((ch & 0x0fu) << 12) | ((b1 & 0x3fu) << 6) | (b2 & 0x3fu);
                        } else {
                            ok = false;
                            ch = '?';
                        }
                    }
                } else if (ch >= 0x80) {
                    ok = false;
                    ch = '?';
                }
                if (ch > 0xffffu) {
                    ok = false;
                    ch = '?';
                }
                memory_.write_value(dst + out_chars * 2, ch, EndianMemory::Halfword);
                ++out_chars;
            }
            memory_.write_value(dst + out_chars * 2, 0u, EndianMemory::Halfword);
            cpu.set_reg(REG_R0, ok ? 1u : 0u);
        }
    } else if (hook.name == "AEEHelper_wstrtoutf8") {
        addr_t src = r0;
        int32_t src_len = static_cast<int32_t>(r1);
        addr_t dst = r2;
        uint32_t dst_size = r3;
        if (!src || !dst || src >= 0xFF000000u || dst >= 0xFF000000u || dst_size == 0) {
            cpu.set_reg(REG_R0, 0);
        } else {
            uint32_t out = 0;
            uint32_t in = 0;
            bool ok = true;
            while (out + 1 < dst_size) {
                if (src_len >= 0 && in >= static_cast<uint32_t>(src_len)) {
                    break;
                }
                uint16_t ch = static_cast<uint16_t>(memory_.read_value(src + in * 2, EndianMemory::Halfword));
                ++in;
                if (ch == 0) {
                    break;
                }
                if (ch < 0x80) {
                    memory_.write_value(dst + out++, ch, EndianMemory::Byte);
                } else if (out + 2 < dst_size) {
                    memory_.write_value(dst + out++, 0xC0u | (ch >> 6), EndianMemory::Byte);
                    memory_.write_value(dst + out++, 0x80u | (ch & 0x3Fu), EndianMemory::Byte);
                } else {
                    ok = false;
                    break;
                }
            }
            memory_.write_value(dst + out, 0u, EndianMemory::Byte);
            cpu.set_reg(REG_R0, ok ? 1u : 0u);
        }
    } else if (hook.name == "AEEHelper_wstrlower" || hook.name == "AEEHelper_wstrupper") {
        for (uint32_t i = 0; i < 4096; ++i) {
            auto ch = static_cast<uint16_t>(memory_.read_value(r0 + i * 2, EndianMemory::Halfword));
            if (ch == 0) {
                break;
            }
            if (ch < 0x80) {
                ch = static_cast<uint16_t>(hook.name == "AEEHelper_wstrlower"
                                               ? std::tolower(static_cast<unsigned char>(ch))
                                               : std::toupper(static_cast<unsigned char>(ch)));
                memory_.write_value(r0 + i * 2, ch, EndianMemory::Halfword);
            }
        }
    } else if (hook.name == "AEEHelper_chartype") {
        const auto ch = static_cast<uint16_t>(r0);
        uint32_t type = 0;
        if (ch < 0x80) {
            if (std::isalpha(static_cast<unsigned char>(ch))) type |= 0x01;
            if (std::isdigit(static_cast<unsigned char>(ch))) type |= 0x02;
            if (std::isspace(static_cast<unsigned char>(ch))) type |= 0x04;
        }
        cpu.set_reg(REG_R0, type);
    } else if (hook.name == "AEEHelper_memchr") {
        uint8_t buf[4096] = {};
        size_t len = static_cast<size_t>(std::min(static_cast<uint32_t>(sizeof(buf)), r2));
        for (size_t i = 0; i < len; ++i) {
            auto c = static_cast<uint8_t>(memory_.read_value(r0 + static_cast<uint32_t>(i), EndianMemory::Byte));
            if (c == static_cast<uint8_t>(r1)) {
                cpu.set_reg(REG_R0, r0 + static_cast<uint32_t>(i));
                return true;
            }
        }
        cpu.set_reg(REG_R0, 0);
    } else if (hook.name == "AEEHelper_strexpand" || (!is_helper_hook && call_idx == 57)) {
        uint32_t src = r0;
        int32_t count = static_cast<int32_t>(r1);
        uint32_t dst = r2;
        int32_t size_bytes = static_cast<int32_t>(r3);
        if (src && dst && src < 0xFF000000 && dst < 0xFF000000 && size_bytes >= 2) {
            uint32_t max_chars = static_cast<uint32_t>(size_bytes / 2);
            uint32_t written = 0;
            const bool explicit_count = count > 1;
            while (written + 1 < max_chars) {
                if (explicit_count && static_cast<int32_t>(written) >= count) {
                    break;
                }
                auto ch = static_cast<uint8_t>(memory_.read_value(src + written, EndianMemory::Byte));
                memory_.write_value(dst + written * 2, static_cast<uint16_t>(ch), EndianMemory::Halfword);
                ++written;
                if (ch == 0) {
                    break;
                }
            }
            if (written == 0 || memory_.read_value(dst + (written - 1) * 2, EndianMemory::Halfword) != 0) {
                memory_.write_value(dst + written * 2, static_cast<uint16_t>(0), EndianMemory::Halfword);
            }
            printf("  [AEEHelper_strexpand] src=0x%08x count=%d dst=0x%08x size=%d chars=%u\n",
                   src, count, dst, size_bytes, written);
        }
        cpu.set_reg(REG_R0, dst);
    } else if (hook.name == "AEEHelper_f_op") {
        double v1, v2;
        uint64_t u1 = (static_cast<uint64_t>(r1) << 32) | r0;
        uint64_t u2 = (static_cast<uint64_t>(r3) << 32) | r2;
        std::memcpy(&v1, &u1, 8);
        std::memcpy(&v2, &u2, 8);
        uint32_t nType = memory_.read_value(sp);
        double res = 0;
        switch (nType) {
            case 0: res = v1 + v2; break;
            case 1: res = v1 - v2; break;
            case 2: res = v1 * v2; break;
            case 3: res = v1 / v2; break;
            case 9: res = std::pow(v1, v2); break;
        }
        uint64_t ur;
        std::memcpy(&ur, &res, 8);
        cpu.set_reg(REG_R0, static_cast<uint32_t>(ur));
        cpu.set_reg(REG_R1, static_cast<uint32_t>(ur >> 32));
    } else if (hook.name == "AEEHelper_f_cmp") {
        double v1, v2;
        uint64_t u1 = (static_cast<uint64_t>(r1) << 32) | r0;
        uint64_t u2 = (static_cast<uint64_t>(r3) << 32) | r2;
        std::memcpy(&v1, &u1, 8);
        std::memcpy(&v2, &u2, 8);
        uint32_t nType = memory_.read_value(sp);
        bool res = false;
        switch (nType) {
            case 4: res = v1 < v2; break;
            case 5: res = v1 <= v2; break;
            case 6: res = v1 == v2; break;
            case 7: res = v1 > v2; break;
            case 8: res = v1 >= v2; break;
        }
        cpu.set_reg(REG_R0, res ? 1 : 0);
    } else if (hook.name == "AEEHelper_f_calc") {
        double v1;
        uint64_t u1 = (static_cast<uint64_t>(r1) << 32) | r0;
        std::memcpy(&v1, &u1, 8);
        uint32_t nType = r2;
        double res = 0;
        switch (nType) {
            case 10: res = std::floor(v1); break;
            case 11: res = std::ceil(v1); break;
            case 12: res = std::sqrt(v1); break;
            case 16: res = std::sin(v1); break;
            case 17: res = std::cos(v1); break;
            case 18: res = std::abs(v1); break;
            case 19: res = std::tan(v1); break;
        }
        uint64_t ur;
        std::memcpy(&ur, &res, 8);
        cpu.set_reg(REG_R0, static_cast<uint32_t>(ur));
        cpu.set_reg(REG_R1, static_cast<uint32_t>(ur >> 32));
    } else if (hook.name == "AEEHelper_f_assignstr" || hook.name == "AEEHelper_strtod") {
        std::string s = read_guest_text(r0, 128);
        char* end = nullptr;
        double result = std::strtod(s.c_str(), &end);
        if (hook.name == "AEEHelper_strtod" && r1 && end) {
            memory_.write_value(r1, r0 + static_cast<uint32_t>(end - s.c_str()));
        }
        write_double_result(cpu, result);
    } else if (hook.name == "AEEHelper_f_assignint") {
        write_double_result(cpu, static_cast<double>(static_cast<int32_t>(r0)));
    } else if (hook.name == "AEEHelper_f_toint") {
        double value;
        uint64_t raw = (static_cast<uint64_t>(r1) << 32) | r0;
        std::memcpy(&value, &raw, sizeof(value));
        cpu.set_reg(REG_R0, static_cast<uint32_t>(static_cast<int32_t>(value)));
    } else if (hook.name == "AEEHelper_trunc" || hook.name == "AEEHelper_utrunc") {
        double value;
        uint64_t raw = (static_cast<uint64_t>(r1) << 32) | r0;
        std::memcpy(&value, &raw, sizeof(value));
        if (hook.name == "AEEHelper_utrunc") {
            cpu.set_reg(REG_R0, static_cast<uint32_t>(value));
        } else {
            cpu.set_reg(REG_R0, static_cast<uint32_t>(static_cast<int32_t>(value)));
        }
    } else if (hook.name == "AEEHelper_f_get") {
        double result = 0.0;
        switch (r0) {
            case 13: result = HUGE_VAL; break;
            case 14: result = DBL_MAX; break;
            case 15: result = DBL_MIN; break;
        }
        write_double_result(cpu, result);
    } else if (hook.name == "AEEHelper_qsort") {
        const addr_t base = r0;
        const uint32_t count = r1;
        const uint32_t element_size = r2;
        const addr_t comparator = r3;
        if (std::getenv("ZEEMU_TRACE_HLE")) {
            printf("  [AEEHelper_qsort] base=0x%08x count=%u size=%u cmp=0x%08x\n",
                   base, count, element_size, comparator);
        }

        if (base != 0 && base < 0xFF000000u && count > 1 && element_size != 0 &&
            comparator != 0 && comparator < 0xFF000000u &&
            static_cast<uint64_t>(count - 1) * element_size < 0xFF000000ull - base) {
            bool comparator_failed = false;
            auto call_guest_comparator = [&](addr_t a, addr_t b) -> int32_t {
                uint32_t saved_regs[16];
                for (int i = 0; i < 16; ++i) {
                    saved_regs[i] = cpu.get_reg(static_cast<CPUReg>(i));
                }
                const uint32_t saved_cpsr = cpu.get_reg(REG_CPSR);
                constexpr uint32_t magic_ret = 0xDEADBEE0u;

                cpu.set_reg(REG_R0, a);
                cpu.set_reg(REG_R1, b);
                cpu.set_reg(REG_R2, 0);
                cpu.set_reg(REG_R3, 0);
                cpu.set_reg(REG_LR, magic_ret);
                cpu.set_reg(REG_PC, comparator & ~1u);
                cpu.set_reg(REG_CPSR, (saved_cpsr & ~0x20u) | ((comparator & 1u) ? 0x20u : 0u));

                int guard = 0;
                for (; guard < 100000 && !cpu.is_stopped() && cpu.get_reg(REG_PC) != magic_ret; ++guard) {
                    cpu.step_once();
                }
                const auto result = static_cast<int32_t>(cpu.get_reg(REG_R0));
                if (guard >= 100000) {
                    comparator_failed = true;
                    printf("  [AEEHelper_qsort] comparator timeout cmp=0x%08x a=0x%08x b=0x%08x PC=0x%08x LR=0x%08x\n",
                           comparator, a, b, cpu.get_reg(REG_PC), cpu.get_reg(REG_LR));
                }

                for (int i = 0; i < 16; ++i) {
                    cpu.set_reg(static_cast<CPUReg>(i), saved_regs[i]);
                }
                cpu.set_reg(REG_CPSR, saved_cpsr);
                return result;
            };

            // BREW qsort is C qsort: comparator receives guest addresses of
            // the elements. Sort host copies for throughput, but compare via
            // guest scratch buffers so source-backed comparators can deref
            // element contents exactly like qsort(int(*)(const void*,...)).
            std::vector<std::string> elements;
            elements.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                elements.push_back(memory_.read(base + i * element_size, element_size));
            }

            const addr_t scratch = malloc(element_size * 2, false);
            const addr_t scratch_b = scratch + element_size;
            std::sort(elements.begin(), elements.end(), [&](const std::string& a, const std::string& b) {
                if (comparator_failed || scratch == 0) {
                    return false;
                }
                memory_.write(scratch, a);
                memory_.write(scratch_b, b);
                const int32_t cmp = call_guest_comparator(scratch, scratch_b);
                comparator_failed = comparator_failed || cpu.is_stopped();
                return cmp < 0;
            });

            if (!comparator_failed && scratch != 0) {
                for (uint32_t i = 0; i < count; ++i) {
                    memory_.write(base + i * element_size, elements[i]);
                }
            }
        }
        cpu.set_reg(REG_R0, 0);
    } else if (hook.name == "AEEHelper_GetTimeMS" || hook.name == "AEEHelper_GetUpTimeMS") {
        cpu.set_reg(REG_R0, static_cast<uint32_t>(uptime_ms()));
    } else if (hook.name == "AEEHelper_GetSeconds") {
        cpu.set_reg(REG_R0, brew_time_seconds());
    } else if (hook.name == "AEEHelper_aee_GetUTCSeconds") {
        cpu.set_reg(REG_R0, brew_time_seconds());
    } else if (hook.name == "AEEHelper_aee_GetJulianDate") {
        write_julian_date(memory_, r1, r0);
    } else if (hook.name == "AEEHelper_aee_LocalTimeOffset") {
        if (r0 && r0 < 0xFF000000u) {
            memory_.write_value(r0, 0u, EndianMemory::Byte);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (hook.name == "AEEHelper_aee_GetRand") {
        auto state = static_cast<uint32_t>(uptime_ms());
        for (uint32_t i = 0; i < r1; ++i) {
            state = state * 1664525u + 1013904223u;
            memory_.write_value(r0 + i, static_cast<uint8_t>(state >> 24), EndianMemory::Byte);
        }
    } else if (hook.name == "AEEHelper_GetFSFree") {
        constexpr uint32_t kTotal = 64u * 1024u * 1024u;
        if (r0 && r0 < 0xFF000000u) {
            memory_.write_value(r0, kTotal);
        }
        cpu.set_reg(REG_R0, kTotal / 2);
    } else if (hook.name == "AEEHelper_GetRAMFree") {
        constexpr uint32_t kTotal = 32u * 1024u * 1024u;
        constexpr uint32_t kLargest = 16u * 1024u * 1024u;
        if (r0 && r0 < 0xFF000000u) {
            memory_.write_value(r0, kTotal);
        }
        if (r1 && r1 < 0xFF000000u) {
            memory_.write_value(r1, kLargest);
        }
        cpu.set_reg(REG_R0, kLargest);
    } else if (hook.name == "AEEHelper_OEMStrLen" || hook.name == "AEEHelper_OEMStrSize") {
        std::string s = read_guest_text(r0, 4096);
        cpu.set_reg(REG_R0, static_cast<uint32_t>(s.size() + (hook.name == "AEEHelper_OEMStrSize" ? 1 : 0)));
    } else if (hook.name == "AEEHelper_swapl") {
        cpu.set_reg(REG_R0, ((r0 & 0x000000FFu) << 24) | ((r0 & 0x0000FF00u) << 8) |
                            ((r0 & 0x00FF0000u) >> 8) | ((r0 & 0xFF000000u) >> 24));
    } else if (hook.name == "AEEHelper_swaps") {
        cpu.set_reg(REG_R0, ((r0 & 0x00FFu) << 8) | ((r0 & 0xFF00u) >> 8));
    } else if (hook.name == "AEEHelper_sleep") {
        const uint32_t delay_ms = std::min<uint32_t>(r0, 1000u);
        if (delay_ms != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
        request_thread_slice_yield();
        cpu.set_reg(REG_R0, 0);
        printf("  [AEEHelper_sleep] slept %u ms%s\n",
               delay_ms,
               delay_ms != r0 ? " (capped)" : "");
    } else if (hook.name == "AEEHelper_aee_IsBadPtr") {
        cpu.set_reg(REG_R0, (!r1 || r1 >= 0xFF000000u || r1 + r2 >= 0xFF000000u) ? 1u : 0u);
    } else {
        if (is_helper_hook) {
            printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x SP=0x%08x LR=0x%08x\n",
                   hook.name.c_str(), r0, r1, r2, r3, sp, cpu.get_reg(REG_LR));
            cpu.set_reg(REG_R0, 0);
        } else {
            return false;
        }
    }

    return true;
}
