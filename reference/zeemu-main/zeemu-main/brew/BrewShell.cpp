#include "brew/BrewShell.h"
#include "brew/BrewFileMgr.h"
#include "brew/BrewDisplay.h"
#include "brew/BrewFont.h"
#include "brew/BrewBitmap.h"
#include "brew/BrewImage.h"
#include "brew/BrewImageDecoder.h"
#include "brew/Brew3D.h"
#include "brew/BrewDatabase.h"
#include "brew/BrewSoundPlayer.h"
#include "brew/BrewSound.h"
#include "brew/BrewMediaPCM.h"
#include "brew/BrewMediaUtil.h"
#include "brew/BrewMemAStream.h"
#include "brew/BrewMemCache.h"
#include "brew/BrewUnzipStream.h"
#include "brew/BrewHeap.h"
#include "brew/BrewGraphics.h"
#include "brew/BrewApplet.h"
#include "brew/BrewAppletCtl.h"
#include "brew/BrewHID.h"
#include "brew/BrewHash.h"
#include "brew/BrewCipher.h"
#include "brew/BrewRandom.h"
#include "brew/BrewAppHistory.h"
#include "brew/BrewMenuCtl.h"
#include "brew/BrewSourceUtil.h"
#include "brew/BrewNet.h"
#include "brew/BrewZWheelOem.h"
#include "brew/BrewAppUI.h"
#include "brew/BrewMicro3D.h"
#include "brew/BrewFlash.h"
#include "brew/BrewShellClasses.h"
#include "brew/BrewShellResources.h"
#include "cpu/core/CPU.h"
#include "vfs/VirtualFileSystem.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <vector>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <memory>
#include <utility>

namespace {

std::vector<std::unique_ptr<BrewBitmap>> g_resource_bitmaps;
constexpr uint16_t kNativeMagentaTransparent = 0xf81fu;

uint16_t shell_read_le16(const uint8_t* data, size_t size, size_t offset) {
    if (offset + 2 > size) {
        return 0;
    }
    return static_cast<uint16_t>(data[offset]) |
           static_cast<uint16_t>(data[offset + 1] << 8);
}

uint32_t shell_read_le32(const uint8_t* data, size_t size, size_t offset) {
    if (offset + 4 > size) {
        return 0;
    }
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

uint16_t shell_rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

std::string preview_u16_ascii(const std::u16string& text, size_t limit = 80) {
    std::string out;
    out.reserve(std::min(text.size(), limit));
    for (size_t i = 0; i < text.size() && i < limit; ++i) {
        const char16_t ch = text[i];
        if (ch >= 0x20 && ch < 0x7f) {
            out.push_back(static_cast<char>(ch));
        } else if (ch == '\n') {
            out += "\\n";
        } else if (ch == '\r') {
            out += "\\r";
        } else if (ch == '\t') {
            out += "\\t";
        } else {
            out.push_back('?');
        }
    }
    if (text.size() > limit) {
        out += "...";
    }
    return out;
}

std::unique_ptr<BrewBitmap> make_bitmap_from_winbmp(BrewShell& shell, EndianMemory& memory,
                                                     const uint8_t* data, size_t size) {
    if (!data || size < 54 || data[0] != 'B' || data[1] != 'M') {
        return nullptr;
    }

    const uint32_t pixel_offset = shell_read_le32(data, size, 10);
    const uint32_t dib_size = shell_read_le32(data, size, 14);
    const int32_t width_signed = static_cast<int32_t>(shell_read_le32(data, size, 18));
    const int32_t height_signed = static_cast<int32_t>(shell_read_le32(data, size, 22));
    const uint16_t planes = shell_read_le16(data, size, 26);
    const uint16_t bpp = shell_read_le16(data, size, 28);
    const uint32_t compression = shell_read_le32(data, size, 30);
    if (dib_size < 40 || width_signed <= 0 || height_signed == 0 || planes != 1 ||
        compression != 0 || (bpp != 8 && bpp != 24 && bpp != 32)) {
        return nullptr;
    }

    const uint32_t width = static_cast<uint32_t>(width_signed);
    const uint32_t height = static_cast<uint32_t>(height_signed < 0 ? -height_signed : height_signed);
    const bool top_down = height_signed < 0;
    const uint32_t src_stride = ((width * bpp + 31) / 32) * 4;
    const size_t required = static_cast<size_t>(pixel_offset) + static_cast<size_t>(src_stride) * height;
    if (required > size) {
        return nullptr;
    }

    auto pixel_native = [&](uint32_t x, uint32_t y) -> uint16_t {
        const uint32_t src_y = top_down ? y : (height - 1 - y);
        const uint8_t* src_row = data + pixel_offset + static_cast<size_t>(src_y) * src_stride;
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        if (bpp == 8) {
            const uint8_t index = src_row[x];
            const size_t palette = 14 + static_cast<size_t>(dib_size) + static_cast<size_t>(index) * 4;
            if (palette + 3 > size) {
                return 0;
            }
            b = data[palette];
            g = data[palette + 1];
            r = data[palette + 2];
        } else {
            const uint8_t* px = src_row + static_cast<size_t>(x) * (bpp / 8);
            b = px[0];
            g = px[1];
            r = px[2];
        }
        return shell_rgb888_to_rgb565(r, g, b);
    };
    const bool magenta_keyed =
        pixel_native(0, 0) == kNativeMagentaTransparent ||
        pixel_native(width - 1, 0) == kNativeMagentaTransparent ||
        pixel_native(0, height - 1) == kNativeMagentaTransparent ||
        pixel_native(width - 1, height - 1) == kNativeMagentaTransparent;
    const uint32_t transparent = magenta_keyed ? kNativeMagentaTransparent : 0xffffffffu;
    auto bitmap = std::make_unique<BrewBitmap>(shell, memory, static_cast<int>(width),
                                               static_cast<int>(height), 16, 0, 0, -1,
                                               transparent);
    std::vector<uint8_t> row(static_cast<size_t>(bitmap->get_pitch()));
    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t src_y = top_down ? y : (height - 1 - y);
        const uint8_t* src_row = data + pixel_offset + static_cast<size_t>(src_y) * src_stride;
        std::fill(row.begin(), row.end(), 0);
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            if (bpp == 8) {
                const uint8_t index = src_row[x];
                const size_t palette = 14 + static_cast<size_t>(dib_size) + static_cast<size_t>(index) * 4;
                if (palette + 3 > size) {
                    continue;
                }
                b = data[palette];
                g = data[palette + 1];
                r = data[palette + 2];
            } else {
                const uint8_t* px = src_row + static_cast<size_t>(x) * (bpp / 8);
                b = px[0];
                g = px[1];
                r = px[2];
            }
            const uint16_t native = shell_rgb888_to_rgb565(r, g, b);
            row[x * 2] = static_cast<uint8_t>(native & 0xffu);
            row[x * 2 + 1] = static_cast<uint8_t>(native >> 8);
        }
        memory.write_bytes(bitmap->get_buffer_ptr() + static_cast<addr_t>(y * bitmap->get_pitch()),
                           row.data(), row.size());
    }
    return bitmap;
}

bool wants_bitmap_object(uint32_t htype) {
    return htype == 0x01001021u;
}

} // namespace

static uint64_t host_now_ms() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

static std::string lower_ascii(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower((unsigned char) ch));
    }
    return s;
}

static uint32_t media_handler_for_mime(const std::string& mime) {
    const std::string m = lower_ascii(mime);
    if (m == "audio/mid" || m == "audio/midi" || m == "snd/midi") {
        return 0x01005501; // AEECLSID_MEDIAMIDI
    }
    if (m == "audio/mp3" || m == "audio/mpeg" || m == "snd/mp3") {
        return 0x01005502; // AEECLSID_MEDIAMP3
    }
    if (m == "audio/wav" || m == "audio/wave" || m == "audio/x-wav" || m == "snd/wav") {
        return 0x0100550a; // AEECLSID_MEDIAADPCM (BREW WAV/ADPCM handler)
    }
    if (m == "audio/qcp" || m == "audio/vnd.qcelp" || m == "snd/qcp") {
        return 0x01005503; // AEECLSID_MEDIAQCP
    }
    if (m == "audio/qcf" || m == "snd/qcf") {
        return 0x01005506; // AEECLSID_MEDIAMIDIOUTQCP
    }
    if (m == "video/pmd") {
        return 0x01005504; // AEECLSID_MEDIAPMD
    }
    if (m == "video/mp4" || m == "video/3gpp" || m == "video/3gpp2") {
        return 0x01005507; // AEECLSID_MEDIAMPEG4
    }
    if (m == "audio/mmf" || m == "snd/mmf") {
        return 0x01005508; // AEECLSID_MEDIAMMF
    }
    if (m == "audio/spf" || m == "snd/spf") {
        return 0x01005509; // AEECLSID_MEDIAPHR
    }
    if (m == "audio/aac" || m == "snd/aac") {
        return 0x0100550b; // AEECLSID_MEDIAAAC
    }
    if (m == "audio/imy" || m == "snd/imy") {
        return 0x0100550c; // AEECLSID_MEDIAIMELODY
    }
    if (m == "audio/amr" || m == "snd/amr") {
        return 0x0100550e; // AEECLSID_MEDIAAMR
    }
    if (m == "audio/hvs") {
        return 0x0100550f; // AEECLSID_MEDIAHVS
    }
    if (m == "audio/saf") {
        return 0x01005510; // AEECLSID_MEDIASAF
    }
    if (m == "audio/xmf" || m == "audio/mxmf" || m == "audio/xmf0" || m == "audio/xmf1") {
        return 0x01005512; // AEECLSID_MEDIAXMF
    }
    if (m == "audio/dls") {
        return 0x01005513; // AEECLSID_MEDIADLS
    }
    if (m == "image/svg" || m == "image/svg+xml" || m == "video/svg" || m == "video/svgz") {
        return 0x01005514; // AEECLSID_MEDIASVG
    }
    if (m == "audio/wma") {
        return 0x0108d5f5; // AEECLSID_MediaWMARaw (OEM extension)
    }
    return 0;
}

static const char* media_type_name_for_clsid(uint32_t clsId) {
    switch (clsId) {
        case 0x01005501: return "MIDI";
        case 0x01005502: return "MP3";
        case 0x01005503: return "QCP";
        case 0x01005504: return "PMD";
        case 0x01005506: return "QCF";
        case 0x01005507: return "MPEG4";
        case 0x01005508: return "MMF";
        case 0x01005509: return "PHR";
        case 0x0100550a: return "ADPCM";
        case 0x0100550b: return "AAC";
        case 0x0100550c: return "IMelody";
        case 0x0100550e: return "AMR";
        case 0x0100550f: return "HVS";
        case 0x01005510: return "SAF";
        case 0x01005511: return "PCM";
        case 0x01005512: return "XMF";
        case 0x01005513: return "DLS";
        case 0x01005514: return "SVG";
        case 0x0108d5f5: return "WMA";
        default: return nullptr;
    }
}

static uint32_t image_handler_for_mime(const std::string& mime) {
    const std::string m = lower_ascii(mime);
    if (m == "image/bmp" || m == "image/x-bmp") {
        return 0x01004001; // AEECLSID_WinBMP / AEECLSID_CWinBMP, default IImage viewer
    }
    if (m == "image/png") {
        return 0x01004004; // AEECLSID_CPNG, default IImage viewer
    }
    return 0;
}

static bool is_audio_mime(const std::string& mime) {
    const std::string m = lower_ascii(mime);
    return m.rfind("audio/", 0) == 0 || m.rfind("snd/", 0) == 0;
}

static void write_guest_blob(EndianMemory& memory, addr_t dst, const std::vector<uint8_t>& blob) {
    if (blob.empty()) {
        return;
    }
    memory.write(dst, std::string(reinterpret_cast<const char*>(blob.data()), blob.size()));
}

static std::string mime_from_extension(const std::string& name) {
    const std::string lower = lower_ascii(name);
    const auto dot = lower.find_last_of('.');
    if (dot == std::string::npos) {
        return {};
    }
    const std::string ext = lower.substr(dot + 1);
    if (ext == "mid" || ext == "midi") return "audio/mid";
    if (ext == "mp3") return "audio/mp3";
    if (ext == "wav") return "audio/wav";
    if (ext == "qcp") return "audio/qcp";
    if (ext == "qcf") return "audio/qcf";
    if (ext == "pmd") return "video/pmd";
    if (ext == "mp4" || ext == "3gp" || ext == "3g2" || ext == "m4a") return "video/mp4";
    if (ext == "mmf") return "audio/mmf";
    if (ext == "spf") return "audio/spf";
    if (ext == "aac") return "audio/aac";
    if (ext == "imy") return "audio/imy";
    if (ext == "amr") return "audio/amr";
    if (ext == "wma") return "audio/wma";
    if (ext == "hvs") return "audio/hvs";
    if (ext == "saf") return "audio/saf";
    if (ext == "xmf" || ext == "mxmf") return "audio/xmf";
    if (ext == "dls") return "audio/dls";
    if (ext == "tga") return "image/x-tga";
    if (ext == "svg" || ext == "svgz") return "image/svg+xml";
    if (ext == "wmv") return "video/wmv";
    return {};
}

static std::string detect_media_mime_from_buffer(EndianMemory& memory, addr_t buf, uint32_t size) {
    if (buf == 0 || buf >= 0xFF000000 || size < 4) {
        return {};
    }

    auto byte_at = [&](uint32_t off) -> uint8_t {
        return memory.read_value(buf + off, EndianMemory::Byte);
    };
    auto has = [&](uint32_t off, const char* magic, uint32_t count) -> bool {
        if (size < off + count) {
            return false;
        }
        for (uint32_t i = 0; i < count; ++i) {
            if (byte_at(off + i) != static_cast<uint8_t>(magic[i])) {
                return false;
            }
        }
        return true;
    };
    auto u16le = [&](uint32_t off) -> uint16_t {
        if (size < off + 2) {
            return 0;
        }
        return static_cast<uint16_t>(byte_at(off) | (byte_at(off + 1) << 8));
    };
    auto looks_like_tga = [&]() -> bool {
        if (size < 18) {
            return false;
        }
        const uint8_t id_len = byte_at(0);
        const uint8_t color_map_type = byte_at(1);
        const uint8_t image_type = byte_at(2);
        if (color_map_type > 1) {
            return false;
        }
        if (image_type != 1 && image_type != 2 && image_type != 3 &&
            image_type != 9 && image_type != 10 && image_type != 11) {
            return false;
        }
        if (color_map_type == 0 && (image_type == 1 || image_type == 9)) {
            return false;
        }

        const uint16_t color_map_len = u16le(5);
        const uint8_t color_map_depth = byte_at(7);
        if (color_map_type == 1) {
            if (color_map_len == 0 ||
                (color_map_depth != 15 && color_map_depth != 16 && color_map_depth != 24 && color_map_depth != 32)) {
                return false;
            }
        } else if (color_map_len != 0) {
            return false;
        }

        const uint16_t width = u16le(12);
        const uint16_t height = u16le(14);
        const uint8_t pixel_depth = byte_at(16);
        const uint8_t descriptor = byte_at(17);
        if (width == 0 || height == 0 || width > 8192 || height > 8192) {
            return false;
        }
        if (pixel_depth != 8 && pixel_depth != 15 && pixel_depth != 16 && pixel_depth != 24 && pixel_depth != 32) {
            return false;
        }
        if ((descriptor & 0xc0) != 0) {
            return false;
        }

        uint32_t color_map_bytes = 0;
        if (color_map_type == 1) {
            color_map_bytes = static_cast<uint32_t>(color_map_len) * ((color_map_depth + 7u) / 8u);
        }
        const uint32_t header_end = 18u + id_len + color_map_bytes;
        if (header_end > size) {
            return false;
        }
        if (image_type == 1 || image_type == 2 || image_type == 3) {
            const uint32_t pixel_bytes = static_cast<uint32_t>((pixel_depth + 7u) / 8u) * width * height;
            const uint32_t expected = header_end + pixel_bytes;
            return expected <= size && size - expected <= 4096;
        }
        return header_end < size;
    };

    if (has(0, "MThd", 4)) {
        return "audio/mid";
    }
    if (has(0, "RIFF", 4) && has(8, "DLS ", 4)) {
        return "audio/dls";
    }
    if (has(0, "RIFF", 4) && has(8, "WAVE", 4)) {
        return "audio/wav";
    }
    if (has(0, "RIFF", 4) && has(8, "QLCM", 4)) {
        return "audio/qcp";
    }
    if (has(0, "#!AMR\n", 6) || has(0, "#!AMR-WB\n", 9)) {
        return "audio/amr";
    }
    if (size >= 2 && byte_at(0) == 0xff && (byte_at(1) & 0xf6) == 0xf0) {
        return "audio/aac";
    }
    if (has(0, "ID3", 3)) {
        return "audio/mp3";
    }
    if (size >= 2 && byte_at(0) == 0xff && (byte_at(1) & 0xe0) == 0xe0) {
        return "audio/mp3";
    }
    if (has(0, "MMMD", 4)) {
        return "audio/mmf";
    }
    if (has(0, "XMF_", 4)) {
        return "audio/xmf";
    }
    if (has(0, "cmid", 4)) {
        return "video/pmd";
    }
    if (looks_like_tga()) {
        return "image/x-tga";
    }
    if (size >= 16 && has(4, "ftyp", 4)) {
        return "video/mp4";
    }
    if (has(0, "\x30\x26\xb2\x75", 4) &&
        size >= 16 &&
        byte_at(4) == 0x8e && byte_at(5) == 0x66 && byte_at(6) == 0xcf && byte_at(7) == 0x11 &&
        byte_at(8) == 0xa6 && byte_at(9) == 0xd9 && byte_at(10) == 0x00 && byte_at(11) == 0xaa &&
        byte_at(12) == 0x00 && byte_at(13) == 0x62 && byte_at(14) == 0xce && byte_at(15) == 0x6c) {
        return "video/wmv";
    }
    std::string head;
    const uint32_t head_len = std::min<uint32_t>(size, 256);
    head.reserve(head_len);
    for (uint32_t i = 0; i < head_len; ++i) {
        head.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(byte_at(i)))));
    }
    const size_t first = head.find_first_not_of(" \t\r\n");
    const std::string trimmed = first == std::string::npos ? std::string{} : head.substr(first);
    if (trimmed.rfind("<svg", 0) == 0 || (trimmed.rfind("<?xml", 0) == 0 && trimmed.find("<svg") != std::string::npos)) {
        return "image/svg+xml";
    }
    std::string upper;
    upper.reserve(head_len);
    for (uint32_t i = 0; i < head_len; ++i) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(byte_at(i)))));
    }
    if (upper.find("BEGIN:IMELODY") != std::string::npos) {
        return "audio/imy";
    }
    return {};
}

void BrewShell::push_hid_event(uint32_t uid, bool down, uint32_t device_index) {
    if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
        printf("[INPUT_TRACE] BrewShell::push_hid_event dev=%u uid=0x%08x down=%u hid=%p\n",
               device_index,
               uid,
               down ? 1u : 0u,
               static_cast<void*>(hid_));
    }
    if (hid_) {
        hid_->push_button_event(uid, down, device_index);
    }
}

void BrewShell::push_hid_keyboard_event(uint32_t key_id, bool down) {
    if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
        printf("[INPUT_TRACE] BrewShell::push_hid_keyboard_event id=%u down=%u hid=%p\n",
               key_id,
               down ? 1u : 0u,
               static_cast<void*>(hid_));
    }
    if (hid_) {
        hid_->push_keyboard_event(key_id, down);
    }
}

void BrewShell::set_hid_axis(uint32_t uid, uint32_t value, uint32_t device_index) {
    if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
        printf("[INPUT_TRACE] BrewShell::set_hid_axis dev=%u uid=0x%08x value=0x%04x hid=%p\n",
               device_index,
               uid,
               value,
               static_cast<void*>(hid_));
    }
    if (hid_) {
        hid_->set_axis_value(uid, value, device_index);
    }
}

void BrewShell::set_hid_rumble_callback(std::function<void(uint32_t, uint32_t, uint32_t)> callback) {
    if (hid_) {
        hid_->set_rumble_callback(std::move(callback));
    }
}

bool BrewShell::hid_default_key_events_enabled(uint32_t device_index) const {
    return !hid_ || hid_->default_key_events_enabled(device_index);
}

BrewShell::BrewShell(EndianMemory& memory, VirtualFileSystem& vfs, int display_width, int display_height)
    : memory_(memory), vfs_(vfs), file_mgr_(nullptr), display_(nullptr), font_(nullptr), image_(nullptr), png_decoder_(nullptr), database_(nullptr),
      sound_player_(nullptr), sound_(nullptr), media_util_(nullptr), unzip_stream_(nullptr), hash_(nullptr), heap_(nullptr), graphics_(nullptr), applet_(nullptr), applet_ctl_(nullptr),
      egl_(nullptr), gl_(nullptr), hid_(nullptr), random_(nullptr), apphistory_(nullptr), menu_ctl_(nullptr), source_util_(nullptr), net_(nullptr), zwheel_oem_(nullptr), app_ui_(nullptr), micro_3d_(nullptr), flash_(nullptr), brew_3d_(nullptr), brew_3d_util_(nullptr), brew_3d_model_(nullptr),
      shell_ptr_(0), object_ptr_(0), vtable_ptr_(0), dummy_module_ptr_(0), dummy_module_vtable_(0),
      hooks_base_(0xFF000000), next_hook_id_(0), display_width_(display_width), display_height_(display_height),
      time_origin_ms_(host_now_ms()),
      heap_next_(0x50000000)
{
    setup_vtable();

    file_mgr_ = new BrewFileMgr(*this, memory, vfs);
    display_ = new BrewDisplay(*this, memory);
    font_ = new BrewFont(*this, memory);
    png_decoder_ = new BrewImageDecoder(*this, memory, "PNGDecoderBREW");
    database_ = new BrewDatabase(*this, memory);
    sound_player_ = new BrewSoundPlayer(*this, memory);
    sound_ = new BrewSound(*this, memory);
    media_pcm_ = new BrewMediaPCM(*this, memory);
    media_util_ = new BrewMediaUtil(*this, memory);
    mem_cache_ = new BrewMemCache(*this, memory);
    unzip_stream_ = new BrewUnzipStream(*this, memory);
    hash_ = new BrewHash(*this, memory);
    cipher_ = new BrewCipher(*this, memory);
    heap_ = new BrewHeap(*this, memory);
    graphics_ = new BrewGraphics(*this, memory);
    applet_ = new BrewApplet(*this, memory);
    applet_ctl_ = new BrewAppletCtl(*this, memory);
    egl_ = new BrewEGL(*this, memory);
    gl_ = new BrewGL(*this, memory);
    brew_3d_ = new Brew3D(*this, memory);
    brew_3d_util_ = new Brew3DUtil(*this, memory);
    brew_3d_model_ = new Brew3DModel(*this, memory);
    hid_ = new BrewHID(*this, memory);
    random_ = new BrewRandom(*this, memory);
    apphistory_ = new BrewAppHistory(*this, memory);
    menu_ctl_ = new BrewMenuCtl(*this, memory);
    source_util_ = new BrewSourceUtil(*this, memory, *file_mgr_);
    net_ = new BrewNet(*this, memory);
    zwheel_oem_ = new BrewZWheelOem(*this, memory);
    app_ui_ = new BrewAppUI(*this, memory);
    micro_3d_ = new BrewMicro3D(*this, memory);
    flash_ = new BrewFlash(*this, memory);

    // Last-resort COM vtable. Reaching it is an implementation bug, so every
    // non-IBase slot must be noisy instead of behaving like successful HLE.
    addr_t stub_vtable = malloc(128 * 4);
    for (int i = 0; i < 128; ++i) {
        std::string sn = (i == 0) ? "StubCom_AddRef" : (i == 1) ? "StubCom_Release" : (i == 2) ? "StubCom_QueryInterface" : ("StubCom_Fn" + std::to_string(i));
        addr_t h = add_hook(sn);
        memory_.write_value(stub_vtable + static_cast<uint32_t>(i * 4), h);
    }
    stub_com_obj_ = malloc(0x100);
    memory_.write_value(stub_com_obj_ + 0, stub_vtable);
    for (int i = 1; i < 64; ++i)
        memory_.write_value(stub_com_obj_ + static_cast<uint32_t>(i * 4), stub_com_obj_);

    // SignalCBFactory stub
    addr_t signal_factory_vtable = malloc(8 * 4);
    const char* sf_names[] = { "AddRef", "Release", "QueryInterface", "CreateSignal" };
    for (int i = 0; i < 4; ++i) {
        memory_.write_value(signal_factory_vtable + static_cast<uint32_t>(i * 4), add_hook(std::string("SignalCBFactory_") + sf_names[i]));
    }
    for (int i = 4; i < 8; ++i) {
        memory_.write_value(signal_factory_vtable + static_cast<uint32_t>(i * 4), add_hook("SignalCBFactory_Fn" + std::to_string(i)));
    }
    stub_signal_factory_obj_ = malloc(0x10);
    memory_.write_value(stub_signal_factory_obj_, signal_factory_vtable);

    // Minimal ISignal / ISignalCtl implementation. Signal factory callbacks are
    // real guest callbacks; HID and other async SDK APIs depend on Set/Enable.
    signal_vtable_ = malloc(8 * 4);
    const char* s_names[] = { "AddRef", "Release", "QueryInterface", "Set", "Detach", "Enable" };
    for (int i = 0; i < 6; ++i) {
        memory_.write_value(signal_vtable_ + static_cast<uint32_t>(i * 4), add_hook(std::string("Signal_") + s_names[i]));
    }
    for (int i = 6; i < 8; ++i) {
        memory_.write_value(signal_vtable_ + static_cast<uint32_t>(i * 4), add_hook("Signal_Fn" + std::to_string(i)));
    }
    stub_signal_obj_ = malloc(0x18);
    memory_.write_value(stub_signal_obj_ + 0, signal_vtable_);
    memory_.write_value(stub_signal_obj_ + 4, 0);
    memory_.write_value(stub_signal_obj_ + 8, 0);
    memory_.write_value(stub_signal_obj_ + 12, 0);
    memory_.write_value(stub_signal_obj_ + 16, 0);

    // Thread stub
    addr_t thread_vtable = malloc(16 * 4);
    const char* t_names[] = { "AddRef", "Release", "QueryInterface", "Malloc", "Free", "HoldRsc", "ReleaseRsc", "Start", "Exit", "Join", "Suspend", "GetResumeCBK" };
    for (int i = 0; i < 12; ++i) {
        memory_.write_value(thread_vtable + static_cast<uint32_t>(i * 4), add_hook(std::string("Thread_") + t_names[i]));
    }
    for (int i = 12; i < 16; ++i) {
        memory_.write_value(thread_vtable + static_cast<uint32_t>(i * 4), add_hook("Thread_Fn" + std::to_string(i)));
    }
    stub_thread_obj_ = malloc(0x10);
    memory_.write_value(stub_thread_obj_, thread_vtable);
    thread_resume_cb_ = malloc(24);
    // BREW 1.x AEECallback layout observed by timer code:
    // pfnNotify at +16 and pNotifyData at +20. The resume callback is a real
    // callback object returned by ITHREAD_GetResumeCBK; ISHELL_Resume may be
    // called with it before ITHREAD_Suspend yields back to the primordial app.
    memory_.write_value(thread_resume_cb_ + 16, add_hook("Thread_ResumeCallback"));
    memory_.write_value(thread_resume_cb_ + 20, stub_thread_obj_);

    // BREW 4 AEELicense.h: IBase + IsExpired/GetInfo/SetUsesRemaining/
    // GetPurchaseInfo. This is deterministic offline license state, not a shop.
    addr_t license_vtable = malloc(6 * 4);
    const char* license_names[] = {
        "AddRef", "Release", "IsExpired", "GetInfo", "SetUsesRemaining", "GetPurchaseInfo"
    };
    for (int i = 0; i < 6; ++i) {
        memory_.write_value(license_vtable + static_cast<uint32_t>(i * 4), add_hook(std::string("ILicense_") + license_names[i]));
    }
    license_obj_ = malloc(0x10);
    memory_.write_value(license_obj_, license_vtable);

    // BREW IWeb is IxOpts + GetResponse/GetResponseV. Zeemu keeps networking
    // deterministic/offline: options are accepted, web responses fail cleanly.
    addr_t web_vtable = malloc(8 * 4);
    const char* web_names[] = {
        "AddRef", "Release", "QueryInterface", "AddOpt",
        "RemoveOpt", "GetOpt", "GetResponse", "GetResponseV"
    };
    for (int i = 0; i < 8; ++i) {
        memory_.write_value(web_vtable + static_cast<uint32_t>(i * 4),
                            add_hook(std::string("IWeb_") + web_names[i]));
    }
    web_obj_ = malloc(0x10);
    memory_.write_value(web_obj_, web_vtable);

    // BREW AEEText.h: ITextCtl = IBase + IControl + text editing slots.
    textctl_vtable_ = malloc(28 * 4);
    const char* textctl_names[] = {
        "AddRef", "Release", "HandleEvent", "Redraw", "SetActive", "IsActive",
        "SetRect", "GetRect", "SetProperties", "GetProperties", "Reset",
        "SetTitle", "SetText", "GetText", "GetTextPtr", "EnableCommand",
        "SetMaxSize", "SetSoftKeyMenu", "SetInputMode", "GetCursorPos",
        "SetCursorPos", "GetInputMode", "EnumModeInit", "EnumNextMode",
        "SetSelection", "GetSelection", "SetPropertiesEx", "GetPropertiesEx"
    };
    for (int i = 0; i < 28; ++i) {
        memory_.write_value(textctl_vtable_ + static_cast<uint32_t>(i * 4),
                            add_hook(std::string("ITextCtl_") + textctl_names[i]));
    }

    // Image stub fallback for non-raster or unsupported paths.
    // BREW 4 AEEIImage.h: IBase + Draw/DrawFrame/GetInfo/SetParm/Start/Stop
    // followed by the extended SetStream/HandleEvent/Notify slots.
    addr_t image_vtable = malloc(11 * 4);
    const char* img_names[] = {
        "AddRef", "Release", "Draw", "DrawFrame", "GetInfo", "SetParm",
        "Start", "Stop", "SetStream", "HandleEvent", "Notify"
    };
    for (int i = 0; i < 11; ++i) {
        memory_.write_value(image_vtable + static_cast<uint32_t>(i * 4), add_hook(std::string("IImage_") + img_names[i]));
    }
    stub_image_obj_ = malloc(0x10);
    memory_.write_value(stub_image_obj_, image_vtable);

    // Quake (Zeebo Tectoy port) reads appinstance+0x68 as IFileMgr* and calls OpenFile on it.
    memory_.write_value(stub_com_obj_ + 0x68, file_mgr_->get_object_ptr());
    memory_.write_value(applet_->get_object_ptr() + 0x68, file_mgr_->get_object_ptr());
}

uint64_t BrewShell::uptime_ms() const {
    const uint64_t elapsed = host_now_ms() - time_origin_ms_;
    return std::max(current_uptime_ms_, elapsed);
}

void BrewShell::set_uptime_ms(uint64_t value) {
    current_uptime_ms_ = std::max(current_uptime_ms_, value);
}

uint64_t BrewShell::next_timer_expiration_ms() const {
    uint64_t next = UINT64_MAX;
    for (const auto& timer : pending_timers_) {
        next = std::min(next, timer.expire_ms);
    }
    return next;
}

void BrewShell::setup_vtable() {
    next_hook_id_ = 0; // Keep IShell hooks starting at 0.
    vtable_ptr_ = malloc(128 * 4);
    object_ptr_ = malloc(128 * 4); // Large enough for offsets such as 0x68.
    shell_ptr_ = malloc(4);

    // Dummy module used by loader bypass paths (Double Dragon Z).
    dummy_module_vtable_ = malloc(12 * 4);
    dummy_module_ptr_ = malloc(4);
    memory_.write_value(dummy_module_ptr_, dummy_module_vtable_);

    // Dummy module hooks.
    memory_.write_value(dummy_module_vtable_ + 0, add_hook("IModule_AddRef"));
    memory_.write_value(dummy_module_vtable_ + 4, add_hook("IModule_Release"));
    memory_.write_value(dummy_module_vtable_ + 8, add_hook("IModule_CreateInstance"));

    // Object points to its vtable at offset 0 (COM/BREW convention).
    memory_.write_value(object_ptr_, vtable_ptr_);

    // IShell pointer points to the object.
    memory_.write_value(shell_ptr_, object_ptr_);

    std::cout << "Shell Setup: shell_ptr=0x" << std::hex << shell_ptr_
              << " object_ptr=0x" << object_ptr_
              << " vtable_ptr=0x" << vtable_ptr_ << std::endl;

    // Assign known names for clearer logs.
    auto get_name = [](int i) -> std::string {
        switch (i) {
            case 0:  return "IShell_AddRef";
            case 1:  return "IShell_Release";
            case 2:  return "IShell_CreateInstance";
            case 3:  return "IShell_QueryClass";
            case 4:  return "IShell_GetDeviceInfo";
            case 5:  return "IShell_StartApplet";
            case 6:  return "IShell_CloseApplet";
            case 7:  return "IShell_CanStartApplet";
            case 8:  return "IShell_ActiveApplet";
            case 9:  return "IShell_EnumAppletInit";
            case 10: return "IShell_EnumNextApplet";
            case 11: return "IShell_SetTimer";
            case 12: return "IShell_CancelTimer";
            case 13: return "IShell_GetTimerExpiration";
            case 14: return "IShell_CreateDialog";
            case 15: return "IShell_GetActiveDialog";
            case 16: return "IShell_EndDialog";
            case 17: return "IShell_LoadResString";
            case 18: return "IShell_LoadResData";
            case 19: return "IShell_LoadResObject";
            case 20: return "IShell_FreeResData";
            case 21: return "IShell_SendEvent";
            case 22: return "IShell_Beep";
            case 23: return "IShell_GetPrefs";
            case 24: return "IShell_SetPrefs";
            case 25: return "IShell_GetItemStyle";
            case 26: return "IShell_Prompt";
            case 27: return "IShell_MessageBox";
            case 28: return "IShell_MessageBoxText";
            case 29: return "IShell_SetAlarm";
            case 30: return "IShell_CancelAlarm";
            case 31: return "IShell_AlarmsActive";
            case 32: return "IShell_GetHandler";
            case 33: return "IShell_RegisterHandler";
            case 34: return "IShell_RegisterNotify";
            case 35: return "IShell_Notify";
            case 36: return "IShell_Resume";
            case 37: return "IShell_ForceExit";
            case 38: return "IShell_GetPosition";
            case 39: return "IShell_CheckPrivLevel";
            case 40: return "IShell_IsValidResource";
            case 41: return "IShell_LoadResDataEx";
            case 42: return "IShell_RegisterSystemCallback";
            case 43: return "IShell_DetectType";
            case 44: return "IShell_GetDeviceInfoEx";
            case 45: return "IShell_GetClassItemID";
            case 46: return "IShell_Obsolete";
            case 47: return "IShell_GetProperty";
            case 48: return "IShell_SetProperty";
            case 49: return "IShell_RegisterEvent";
            case 50: return "IShell_Reset";
            case 51: return "IShell_AppIsInGroup";
            case 52: return "IShell_GetUpTimeMS";
            case 57: return "AEEHelper_strexpand";
            default: return "IShell_Idx_" + std::to_string(i);
        }
    };

    for (int i = 0; i < 128; ++i) {
        std::string name = get_name(i);

        BrewService* svc = nullptr;

        addr_t hook_addr = add_hook(name, svc);
        memory_.write_value(vtable_ptr_ + (i * 4), hook_addr);

        // Mirror hooks for Zeebo binaries that access the object as a vtable.
        if (i > 0) {
            memory_.write_value(object_ptr_ + (i * 4), hook_addr);
        }
    }

    // Last-resort COM object for legacy paths; unknown services must not be
    // synthesized here.
    // Self-referential data fields removed from main loop to constructor.
}

addr_t BrewShell::malloc(uint32_t size, bool zero) {
    if (size == 0) {
        return 0;
    }
    (void)zero;

    constexpr uint32_t kHeapAlignment = 8;
    addr_t addr = (heap_next_ + (kHeapAlignment - 1)) & ~(kHeapAlignment - 1);
    uint32_t aligned_size = (size + (kHeapAlignment - 1)) & ~(kHeapAlignment - 1);
    // Zeemu uses a monotonic guest heap without FREE reuse. Even when BREW's
    // ALLOC_NO_ZMEM removes the zero-fill guarantee, fresh RAM should not
    // expose stale ROM/code bytes from the emulator backing store.
    memory_.fill(addr, 0, aligned_size);
    heap_next_ = addr + aligned_size;
    heap_alloc_sizes_[addr] = size;
    return addr;
}

uint32_t BrewShell::allocation_size(addr_t ptr) const {
    auto it = heap_alloc_sizes_.find(ptr);
    return it == heap_alloc_sizes_.end() ? 0 : it->second;
}

addr_t BrewShell::realloc_block(addr_t old_ptr, uint32_t size, bool zero_extra) {
    if (size == 0) {
        return 0;
    }

    addr_t new_ptr = malloc(size, zero_extra);
    if (!new_ptr || !old_ptr || old_ptr >= 0xFF000000u) {
        return new_ptr;
    }

    const uint32_t old_size = allocation_size(old_ptr);
    const uint32_t copy_size = old_size ? std::min(old_size, size) : size;
    for (uint32_t i = 0; i < copy_size; ++i) {
        memory_.write_value(new_ptr + i,
                            memory_.read_value(old_ptr + i, EndianMemory::Byte),
                            EndianMemory::Byte);
    }

    if (!zero_extra && size > copy_size) {
        // malloc() returns zeroed memory; BREW ALLOC_NO_ZMEM only promises the
        // extra bytes are not explicitly zeroed. Keeping deterministic zeroes is
        // safer than synthesizing garbage while still preserving realloc data.
    }

    return new_ptr;
}

uint32_t BrewShell::heap_used_bytes() const {
    return heap_next_ >= heap_base_ ? heap_next_ - heap_base_ : 0;
}

addr_t BrewShell::add_hook(const std::string& name, BrewService* service) {
    uint32_t hook_id = next_hook_id_++;
    addr_t hook_addr = hooks_base_ + (hook_id * 4);

    // Custom instruction 0xEFxxxxxx (SVC).
    uint32_t insn = 0xEF000000 | hook_id;
    memory_.write_value(hook_addr, insn);

    // std::cout << "Added hook: " << name << " id=" << hook_id << " at 0x" << std::hex << hook_addr << std::endl;

    hooks_.push_back({name, hook_addr, service});
    return hook_addr;
}

void BrewShell::read_string(addr_t addr, char* buf, size_t max_len) {
    for (size_t i = 0; i < max_len - 1; ++i) {
        uint8_t c = memory_.read_value(addr + i, EndianMemory::Byte);
        buf[i] = static_cast<char>(c);
        if (c == 0) break;
    }
}

static bool looks_like_utf16le(const std::vector<uint8_t>& bytes) {
    size_t pairs = 0;
    size_t wide_pairs = 0;
    for (size_t i = 0; i + 1 < bytes.size() && pairs < 32; i += 2, ++pairs) {
        uint8_t lo = bytes[i];
        uint8_t hi = bytes[i + 1];
        if (lo == 0 && hi == 0) {
            break;
        }
        if (hi == 0 && (lo == '\t' || lo == '\n' || lo == '\r' || (lo >= 0x20 && lo < 0x7f))) {
            ++wide_pairs;
        }
    }
    return pairs >= 4 && wide_pairs * 4 >= pairs * 3;
}

std::string BrewShell::read_guest_text(addr_t addr, size_t max_len) {
    if (addr == 0 || addr >= 0xFF000000 || max_len == 0) {
        return {};
    }

    std::vector<uint8_t> bytes;
    bytes.reserve(std::min<size_t>(max_len, 4096));
    for (size_t i = 0; i < max_len; ++i) {
        uint8_t c = memory_.read_value(addr + static_cast<uint32_t>(i), EndianMemory::Byte);
        bytes.push_back(c);
        if (c == 0) {
            if (i > 0 && bytes[i - 1] == 0) {
                break;
            }
            if ((i & 1) == 0) {
                break;
            }
        }
    }

    if (looks_like_utf16le(bytes)) {
        std::string out;
        out.reserve(bytes.size() / 2);
        for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
            uint8_t lo = bytes[i];
            uint8_t hi = bytes[i + 1];
            if (lo == 0 && hi == 0) {
                break;
            }
            out.push_back(static_cast<char>(lo));
        }
        return out;
    }

    return std::string(reinterpret_cast<const char*>(bytes.data()),
                       strnlen(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

std::string BrewShell::format_guest(addr_t fmt_ptr, CPU& cpu, int start_reg, bool is_va_list, addr_t va_ptr) {
    char fmt_buf[1024] = {};
    std::string fmt_text = read_guest_text(fmt_ptr, sizeof(fmt_buf));
    std::snprintf(fmt_buf, sizeof(fmt_buf), "%s", fmt_text.c_str());

    std::string result;
    int reg = start_reg;
    int stack_off = 0;
    addr_t sp = cpu.get_reg(REG_SP);
    addr_t va_arg_ptr = va_ptr;

    auto next_arg = [&]() -> uint32_t {
        if (is_va_list) {
            if (va_arg_ptr == va_ptr) {
                addr_t candidate = memory_.read_value(va_ptr);
                if (candidate && candidate < 0xFF000000) {
                    va_arg_ptr = candidate;
                }
            }
            uint32_t val = memory_.read_value(va_arg_ptr);
            va_arg_ptr += 4;
            return val;
        } else {
            if (reg < 4) {
                return cpu.get_reg(static_cast<CPUReg>(REG_R0 + reg++));
            } else {
                uint32_t val = memory_.read_value(sp + (stack_off++) * 4);
                return val;
            }
        }
    };

    for (const char* p = fmt_buf; *p; ++p) {
        if (*p == '%') {
            const char* start = p;
            p++;
            // Skip flags, width, precision
            while (*p && strchr("-+ #0123456789.", *p)) p++;
            // Length modifiers (h, l, L, z, j, t)
            while (*p && strchr("hlLzj", *p)) p++;

            if (*p == '%') {
                result += '%';
            } else if (*p == 's') {
                addr_t s_ptr = next_arg();
                if (s_ptr) {
                    char s_buf[1024] = {};
            std::string s_text = read_guest_text(s_ptr, sizeof(s_buf));
            std::snprintf(s_buf, sizeof(s_buf), "%s", s_text.c_str());
                    char out[1100] = {};
                    std::string spec(start, p - start + 1);
                    snprintf(out, sizeof(out), spec.c_str(), s_buf);
                    result += out;
                } else {
                    result += "(null)";
                }
            } else if (*p == 'f' || *p == 'e' || *p == 'g' || *p == 'E' || *p == 'G') {
                // double is 8 bytes, always 8-byte aligned on stack in AAPCS?
                // BREW might be different, but let's assume 2 slots.
                if (!is_va_list && reg >= 4 && (stack_off & 1)) stack_off++; // Align
                uint32_t v1 = next_arg();
                uint32_t v2 = next_arg();
                double d;
                uint64_t v = (static_cast<uint64_t>(v2) << 32) | v1;
                memcpy(&d, &v, 8);
                char out[128];
                std::string spec(start, p - start + 1);
                snprintf(out, sizeof(out), spec.c_str(), d);
                result += out;
            } else if (*p == 'd' || *p == 'i' || *p == 'u' || *p == 'x' || *p == 'X' || *p == 'p' || *p == 'c' || *p == 'o') {
                uint32_t val = next_arg();
                char out[128];
                std::string spec(start, p - start + 1);
                snprintf(out, sizeof(out), spec.c_str(), val);
                result += out;
            } else {
                result.append(start, p - start + 1);
            }
        } else {
            result += *p;
        }
    }
    return result;
}

void BrewShell::handle_hook(uint32_t hook_id, CPU& cpu) {
    if (hook_id >= hooks_.size()) {
        fprintf(stderr,
                "Unknown hook ID: %u (0x%06x) PC=0x%08x LR=0x%08x R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x R4=0x%08x R5=0x%08x R6=0x%08x R7=0x%08x\n",
                hook_id,
                hook_id,
                cpu.get_reg(REG_PC),
                cpu.get_reg(REG_LR),
                cpu.get_reg(REG_R0),
                cpu.get_reg(REG_R1),
                cpu.get_reg(REG_R2),
                cpu.get_reg(REG_R3),
                cpu.get_reg(REG_R4),
                cpu.get_reg(REG_R5),
                cpu.get_reg(REG_R6),
                cpu.get_reg(REG_R7));
        return;
    }

    auto& hook = hooks_[hook_id];

    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    uint32_t r3 = cpu.get_reg(REG_R3);
    uint32_t r4 = cpu.get_reg(REG_R4);
    uint32_t r5 = cpu.get_reg(REG_R5);
    uint32_t r6 = cpu.get_reg(REG_R6);
    uint32_t lr = cpu.get_reg(REG_LR);
    uint32_t sp = cpu.get_reg(REG_SP);

    // Helper thunks pass the shell slot offset in R0 and a hook/stub address in
    // R1. Normal vtable calls can leave an unrelated hook address in R1, so do
    // not classify large object pointers as thunk slot numbers.
    bool is_thunk = (r1 >= 0xFF000000 && r0 < 512);
    int call_idx = is_thunk ? (r0 / 4) : -1;
    bool is_helper_hook = hook.name.rfind("AEEHelper_", 0) == 0;

    static uint32_t s_gl_unbind_hook_logs = 0;
    static uint32_t s_gl_delete_hook_logs = 0;
    static uint32_t s_bitmap_rgb_to_native_logs = 0;
    static uint32_t s_bitmap_draw_pixel_logs = 0;
    static uint32_t s_image_rop_setparm_logs = 0;
    static uint32_t s_image_draw_logs = 0;
    bool suppress_hook_log = false;
    if (hook.name == "IGL_glBindTexture" && r0 == 0x0DE1 && r1 == 0) {
        suppress_hook_log = s_gl_unbind_hook_logs >= 8;
        ++s_gl_unbind_hook_logs;
    } else if (hook.name == "IGL_glDeleteTextures" && r0 == 1) {
        suppress_hook_log = s_gl_delete_hook_logs >= 8;
        ++s_gl_delete_hook_logs;
    } else if (hook.name == "IBitmap_RGBToNative") {
        suppress_hook_log = s_bitmap_rgb_to_native_logs >= 16;
        ++s_bitmap_rgb_to_native_logs;
    } else if (hook.name == "IBitmap_DrawPixel") {
        suppress_hook_log = s_bitmap_draw_pixel_logs >= 16;
        ++s_bitmap_draw_pixel_logs;
    } else if (hook.name == "IImage_SetParm" && r1 == 0x03) {
        suppress_hook_log = s_image_rop_setparm_logs >= 16;
        ++s_image_rop_setparm_logs;
    } else if (hook.name == "IImage_Draw") {
        suppress_hook_log = s_image_draw_logs >= 32;
        ++s_image_draw_logs;
    }
    if (suppress_dbgprintf_ &&
        (is_helper_hook ||
         hook.name.rfind("IDisplay_", 0) == 0 ||
         hook.name == "IBitmap_FillRect")) {
        suppress_hook_log = true;
    }
    if (std::getenv("ZEEMU_TRACE_HLE") == nullptr) {
        suppress_hook_log = true;
    }
    if (!suppress_hook_log) {
        printf("HLE Hook Call: %s (Idx=%d, ID=%u, R0=0x%x, R1=0x%x, R2=0x%x, R3=0x%x, R4=0x%x, R5=0x%x, R6=0x%x, LR=0x%x)\n",
               hook.name.c_str(), call_idx, hook_id, r0, r1, r2, r3, r4, r5, r6, lr);
        fflush(stdout);
    }
    const bool skip_final_flush =
        suppress_hook_log &&
        (hook.name == "IBitmap_RGBToNative" ||
         hook.name == "IBitmap_DrawPixel" ||
         hook.name == "IImage_Draw" ||
         (hook.name == "IImage_SetParm" && r1 == 0x03));

    // Heurística: hook named GetConfig + r5 is a known cfgId
    bool is_validation_call = is_thunk && (hook.name == "IShell_GetConfig") && (r5 == 20 || r5 == 0x14 || r5 == 268 || r5 == 0x10C);

    // Dummy module hook dispatcher.
    if (hook.name == "IModule_AddRef") {
        cpu.set_reg(REG_R0, r0);
        return;
    } else if (hook.name == "IModule_Release") {
        cpu.set_reg(REG_R0, 0);
        return;
    } else if (hook.name == "IModule_CreateInstance") {
        uint32_t clsId = is_thunk ? r5 : r2;
        uint32_t ppObj = is_thunk ? r6 : r3;
        printf("  IModule_CreateInstance: CLSID=0x%x ppObj=0x%x\n", clsId, ppObj);
        create_instance_internal(clsId, ppObj, cpu);
        return;
    } else if (hook.name == "Applet_HandleEvent") {
        uint32_t evt = r1;
        uint32_t wp = r2;
        uint32_t dwp = r3;
        printf("  Applet_HandleEvent: Event=0x%x WP=0x%x DWP=0x%x\n", evt, wp, dwp);
        cpu.set_reg(REG_R0, 0); // Event handled
        return;
    } else if (hook.name == "IShell_StartApplet") {
        uint32_t clsId = is_thunk ? r5 : r1;
        uint32_t flags = is_thunk ? r6 : r2;
        uint32_t pszArgs = is_thunk ? (cpu.get_reg(REG_R7)) : r3;
        printf("  StartApplet: cls=0x%08x flags=0x%x args=0x%08x\n", clsId, flags, pszArgs);
        if (clsId != 0) {
            current_applet_cls_ = clsId;
            current_applet_obj_ = applet_->get_object_ptr();
            applet_->set_current_class(clsId);
        }
        cpu.set_reg(REG_R0, 0);
        return;
    } else if (hook.name == "IShell_CloseApplet") {
        cpu.set_reg(REG_R0, 0);
        return;
    } else if (hook.name == "IShell_CanStartApplet") {
        uint32_t clsId = is_thunk ? r5 : r1;
        // The launched app is always startable regardless of the catalog list,
        // so any rom can run without being hardcoded in is_known_applet_clsid.
        bool startable = is_known_applet_clsid(clsId) ||
                         (clsId != 0 && clsId == current_applet_cls_);
        cpu.set_reg(REG_R0, startable ? 1u : 0u);
        return;
    } else if (hook.name == "IShell_ActiveApplet") {
        cpu.set_reg(REG_R0, current_applet_cls_);
        return;
    } else if (hook.name == "IShell_EnumAppletInit") {
        cpu.set_reg(REG_R0, 0);
        return;
    } else if (hook.name == "IShell_EnumNextApplet") {
        cpu.set_reg(REG_R0, 0);
        return;
    } else if (hook.name.rfind("IShell_Idx_", 0) == 0) {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x R5=0x%08x R6=0x%08x\n",
               hook.name.c_str(), r0, r1, r2, r3, r5, r6);
        cpu.set_reg(REG_R0, 0);
        return;
    }

    if (hook.name == "IDisplay_MeasureTextEx" &&
        lr >= 0x10010000 && lr < 0x11000000 &&
        r0 <= 4 && r1 <= 4 && r2 < 0x1000 && r3 != 0 && r3 < 0xFF000000) {
        // Double Dragon's Qualcomm plain-GL veneer can call the IDisplay slot
        // that normally maps to MeasureTextEx while carrying the no-this
        // glVertexPointer ABI: size/type/stride/pointer in R0-R3. Keep this
        // restricted to the high GL veneer area: the low helper area also
        // reaches slot 3 with GL-looking registers while walking memory.
        static int routed_vertex_pointer_logs = 0;
        if (routed_vertex_pointer_logs < 8) {
            printf("  [GL route] display slot call -> IGL_glVertexPointer size=%u type=0x%x stride=%u ptr=0x%08x\n",
                   r0, r1, r2, r3);
        } else if (routed_vertex_pointer_logs == 8) {
            printf("  [GL route] suppressing repeated IGL_glVertexPointer route logs\n");
        }
        ++routed_vertex_pointer_logs;
        gl_->handle_hook("IGL_glVertexPointer", cpu);
        return;
    }

    if (hook.service) {
        hook.service->handle_hook(hook.name, cpu);
    } else {
        if (hook.name == "IShell_AddRef" || (!is_helper_hook && call_idx == 0)) {
            cpu.set_reg(REG_R0, is_thunk ? r4 : r0);
        }
        else if (hook.name == "IShell_Release" || (!is_helper_hook && call_idx == 1)) {
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "IShell_CreateInstance" || (!is_helper_hook && call_idx == 2)) {
            uint32_t clsId = is_thunk ? r5 : r1;
            uint32_t ppObj = is_thunk ? r6 : r2;
            printf("  IShell_CreateInstance ClsId=0x%08x ppObj=0x%08x\n", clsId, ppObj);
            create_instance_internal(clsId, ppObj, cpu);
        }
        else if (hook.name == "IShell_GetHandler" || (!is_helper_hook && call_idx == 32)) {
            const uint32_t handler_type = is_thunk ? r5 : r1;
            const addr_t psz_in = is_thunk ? r6 : r2;
            const std::string mime = read_guest_text(psz_in, 128);
            uint32_t cls = 0;
            if (handler_type == 0x01005500) { // AEECLSID_MEDIA
                cls = media_handler_for_mime(mime);
            }
            if (cls == 0) {
                cls = image_handler_for_mime(mime);
            }
            printf("  IShell_GetHandler type=0x%08x mime='%s' -> 0x%08x\n",
                   handler_type, mime.c_str(), cls);
            cpu.set_reg(REG_R0, cls);
        }
        else if (hook.name == "IShell_DetectType" || (!is_helper_hook && call_idx == 43)) {
            const addr_t cp_buf = is_thunk ? r5 : r1;
            const addr_t pdw_size = is_thunk ? r6 : r2;
            const addr_t cpsz_name = is_thunk ? cpu.get_reg(REG_R7) : r3;
            const addr_t pcpsz_mime = memory_.read_value(sp);
            constexpr uint32_t ENEEDMORE = 35;
            constexpr uint32_t ENOTYPE = 18;
            constexpr uint32_t DETECT_BYTES = 16;

            if (cp_buf == 0) {
                if (pdw_size && pdw_size < 0xFF000000) {
                    memory_.write_value(pdw_size, DETECT_BYTES);
                }
                printf("  IShell_DetectType probe name=0x%08x -> ENEEDMORE size=%u\n",
                       cpsz_name, DETECT_BYTES);
                cpu.set_reg(REG_R0, ENEEDMORE);
                return;
            }

            uint32_t input_size = 0;
            if (pdw_size && pdw_size < 0xFF000000) {
                input_size = memory_.read_value(pdw_size);
            }
            std::string mime = detect_media_mime_from_buffer(memory_, cp_buf, input_size);
            if (mime.empty() && cpsz_name && cpsz_name < 0xFF000000) {
                mime = mime_from_extension(read_guest_text(cpsz_name, 512));
            }

            if (!mime.empty()) {
                if (pcpsz_mime && pcpsz_mime < 0xFF000000) {
                    addr_t guest_mime = malloc(static_cast<uint32_t>(mime.size() + 1));
                    for (size_t i = 0; i < mime.size(); ++i) {
                        memory_.write_value(guest_mime + static_cast<uint32_t>(i),
                                            static_cast<uint8_t>(mime[i]),
                                            EndianMemory::Byte);
                    }
                    memory_.write_value(guest_mime + static_cast<uint32_t>(mime.size()),
                                        static_cast<uint8_t>(0),
                                        EndianMemory::Byte);
                    memory_.write_value(pcpsz_mime, guest_mime);
                }
                if (pdw_size && pdw_size < 0xFF000000) {
                    memory_.write_value(pdw_size, 0u);
                }
                printf("  IShell_DetectType buf=0x%08x size=%u -> '%s'\n",
                       cp_buf, input_size, mime.c_str());
                cpu.set_reg(REG_R0, 0);
            } else {
                printf("  IShell_DetectType buf=0x%08x size=%u -> ENOTYPE\n",
                       cp_buf, input_size);
                cpu.set_reg(REG_R0, ENOTYPE);
            }
        }
        else if (hook.name == "IShell_QueryClass" || (!is_helper_hook && call_idx == 3)) {
            uint32_t clsId = is_thunk ? r5 : r1;
            uint32_t pInfo = is_thunk ? r6 : r2;

            auto is_known_service = [](uint32_t cls) -> bool {
                switch (cls) {
                    case 0x01001001: // AEECLSID_DISPLAY
                    case 0x010127d4: // AEECLSID_DISPLAY1
                    case 0x0106e415: // AEECLSID_CMemCache1 / MemCache1
                    case 0x01001002: // AEECLSID_HEAP
                    case 0x01001003: // AEECLSID_FILEMGR
                    case 0x01001008: // AEECLSID_DBMGR
                    case 0x01001017: // AEECLSID_THREAD
                    case 0x01001015: // AEECLSID_MD5
                    case 0x01001027: // AEECLSID_MD2
                    case 0x01001028: // AEECLSID_SHA1
                    case 0x01001039: // AEECLSID_MD5CTX
                    case 0x0100103a: // AEECLSID_MD2CTX
                    case 0x0100103b: // AEECLSID_SHA1CTX
                    case 0x0100103c: // AEECLSID_RANDOM
                    case 0x01001049: // AEECLSID_HMAC_MD5
                    case 0x0100104a: // AEECLSID_HMAC_MD2
                    case 0x0100104b: // AEECLSID_HMAC_SHA1
                    case 0x0100104c: // AEECLSID_HMAC_MD5CTX
                    case 0x0100104d: // AEECLSID_HMAC_MD2CTX
                    case 0x0100104e: // AEECLSID_HMAC_SHA1CTX
                    case 0x01002000: // AEECLSID_SOUNDPLAYER
                    case 0x01005500: // AEECLSID_MEDIA
                    case 0x01005501: // AEECLSID_MEDIAMIDI
                    case 0x01005502: // AEECLSID_MEDIAMP3
                    case 0x01005503: // AEECLSID_MEDIAQCP
                    case 0x01005504: // AEECLSID_MEDIAPMD
                    case 0x01005506: // AEECLSID_MEDIAMIDIOUTQCP
                    case 0x01005507: // AEECLSID_MEDIAMPEG4
                    case 0x01005508: // AEECLSID_MEDIAMMF
                    case 0x01005509: // AEECLSID_MEDIAPHR
                    case 0x0100550a: // AEECLSID_MEDIAADPCM
                    case 0x0100550b: // AEECLSID_MEDIAAAC
                    case 0x0100550c: // AEECLSID_MEDIAIMELODY
                    case 0x0100550d: // AEECLSID_MEDIAUTIL
                    case 0x0100550e: // AEECLSID_MEDIAAMR
                    case 0x0100550f: // AEECLSID_MEDIAHVS
                    case 0x01005510: // AEECLSID_MEDIASAF
                    case 0x01005511: // AEECLSID_MEDIAPCM
                    case 0x01005512: // AEECLSID_MEDIAXMF
                    case 0x01005513: // AEECLSID_MEDIADLS
                    case 0x01005514: // AEECLSID_MEDIASVG
                    case 0x0108d5f5: // AEECLSID_MediaWMARaw
                    case 0x01002001: // AEECLSID_GRAPHICS
                    case 0x0102fb92: // AEECLSID_GRAPHICS_BREW
                    case 0x01013a83: // AEECLSID_3D (classic BREW I3D)
                    case 0x0101132f: // AEECLSID_3DUTIL
                    case 0x010113f6: // AEECLSID_3DMODEL
                    case 0x01001058: // AEECLSID_APPLETCTL
                    case 0x0106c411: // AEECLSID_HID
                    case 0x01041207: // AEECLSID_SignalCBFactory
                    case 0x01014bc3: // AEECLSID_GL
                    case 0x01014bc4: // AEECLSID_EGL
                    case 0x0103d8ec: // AEECLSID_QEGL
                    case 0x0102c4e8: // Z-Wheel internal/unknown service reached in tectoy.mod
                        return true;
                    default:
                        return is_generic_core_stub_clsid(cls);
                }
            };

            auto is_known_applet = [](uint32_t cls) -> bool {
                switch (cls) {
                    case 0x01070798: // Z-Wheel
                    case 0x01077cf4: // Z-Wheel secondary class
                    case 0x01072195: // Zeebo App
                    case 0x0102f789: // Double Dragon
                    case 0x01087a3c: // Quake
                    case 0x01087c1c: // Quake II
                    case 0x01087b72: // Pac-Mania
                    case 0x0103d666: // PrBoom homebrew
                    case 0x01009ff2: // TutorI3D / intro homebrew
                    case 0x0108ff07: // Zeebo Extreme Baja
                    case 0x00c9b04b: // Chess / Xadrez
                    case 0xbf2e2021: // Zenonia
                        return true;
                    default:
                        return false;
                }
            };

            bool applet = is_known_applet(clsId);
            bool supported = applet || is_known_service(clsId);
            bool result = (pInfo != 0) ? applet : supported;
            printf("  QueryClass: cls=0x%08x pInfo=0x%08x -> %s\n", clsId, pInfo, result ? "TRUE" : "FALSE");

            if (result && applet && pInfo != 0 && pInfo < 0xFF000000) {
                // AEEAppInfo: cls, pszMIF, wIDBase, wAppType, wHostLo, wHostHi, wIDPrivSet, wFlags
                memory_.write_value(pInfo + 0x00, clsId);
                memory_.write_value(pInfo + 0x04, 0u); // pszMIF: optional resource path; leave NULL for now.
                memory_.write_value(pInfo + 0x08, (uint16_t)1, EndianMemory::Halfword);
                memory_.write_value(pInfo + 0x0a, (uint16_t)0, EndianMemory::Halfword);
                memory_.write_value(pInfo + 0x0c, static_cast<uint16_t>(clsId & 0xffff), EndianMemory::Halfword);
                memory_.write_value(pInfo + 0x0e, static_cast<uint16_t>(clsId >> 16), EndianMemory::Halfword);
                memory_.write_value(pInfo + 0x10, (uint16_t)0, EndianMemory::Halfword);
                memory_.write_value(pInfo + 0x12, (uint16_t)0, EndianMemory::Halfword);
            }
            cpu.set_reg(REG_R0, result ? 1u : 0u);
        }
        else if (hook.name == "IShell_GetDeviceInfo" || (!is_helper_hook && call_idx == 4)) {
            // AEEDeviceInfo layout (ARM aligned, EmptyEnum=int):
            // +0  cxScreen(u16)  +2  cyScreen(u16)
            // +4  cxAltScreen    +6  cyAltScreen   +8  cxScrollBar  +10 wEncoding
            // +12 wMenuTextScroll  +14 nColorDepth(u16)
            // +16 unused2(int=4)  +20 wMenuImageDelay(u32)  +24 dwRAM(u32)
            // +28 flags(u32)  +32 dwPromptProps(u32)
            // +36 wKeyCloseApp(u16)  +38 wKeyCloseAllApps(u16)  +40 dwLang(u32)
            // +44 wStructSize(u16)  +48 dwNetLinger  +52 dwSleepDefer
            // +56 wMaxPath(u16)  +60 dwPlatformID(u32)
            uint32_t pInfo = is_thunk ? r5 : r1;
            printf("  GetDeviceInfo pInfo=0x%08x\n", pInfo);
            if (pInfo != 0 && pInfo < 0xFF000000) {
                const auto requested_size = static_cast<uint16_t>(memory_.read_value(pInfo + 44, EndianMemory::Halfword));
                memory_.write_value(pInfo + 0,  static_cast<uint16_t>(display_width_), EndianMemory::Halfword);  // cxScreen
                memory_.write_value(pInfo + 2,  static_cast<uint16_t>(display_height_), EndianMemory::Halfword);  // cyScreen
                memory_.write_value(pInfo + 4,  (uint16_t)0,   EndianMemory::Halfword);  // cxAltScreen
                memory_.write_value(pInfo + 6,  (uint16_t)0,   EndianMemory::Halfword);  // cyAltScreen
                memory_.write_value(pInfo + 8,  (uint16_t)8,   EndianMemory::Halfword);  // cxScrollBar
                memory_.write_value(pInfo + 10, (uint16_t)0x00FD, EndianMemory::Halfword); // wEncoding = AEE_ENC_S_JIS
                memory_.write_value(pInfo + 12, (uint16_t)200, EndianMemory::Halfword); // wMenuTextScroll
                memory_.write_value(pInfo + 14, (uint16_t)24,  EndianMemory::Halfword);  // nColorDepth=24bpp hint
                memory_.write_value(pInfo + 16, (uint16_t)0,   EndianMemory::Halfword);  // unused2
                memory_.write_value(pInfo + 20, (uint32_t)1000, EndianMemory::Word); // wMenuImageDelay
                memory_.write_value(pInfo + 24, (uint32_t)(64u * 1024u * 1024u));        // dwRAM=64MB
                memory_.write_value(pInfo + 28, (uint32_t)0x10C);                          // bVibrator/bExtSpeaker/bPen
                memory_.write_value(pInfo + 32, (uint32_t)0);                              // dwPromptProps
                memory_.write_value(pInfo + 36, (uint16_t)0xE030, EndianMemory::Halfword); // wKeyCloseApp = AVK_END
                memory_.write_value(pInfo + 38, (uint16_t)0x0000, EndianMemory::Halfword); // wKeyCloseAllApps
                // BREW 4.0.2 SP19/sdk/inc/AEELngCode.h: LNG_PORTUGUESE_BRAZIL
                // is "ptbr" (0x72627470). Zeebo retail titles generally ran on
                // Brazilian Portuguese consoles; returning zero makes language
                // probes look unknown and can push games into manual selectors.
                memory_.write_value(pInfo + 40, (uint32_t)0x72627470);                     // dwLang
                memory_.write_value(pInfo + 44, requested_size ? requested_size : static_cast<uint16_t>(48), EndianMemory::Halfword); // wStructSize
                if (requested_size >= 48) {
                    memory_.write_value(pInfo + 48, (uint32_t)0);                          // dwNetLinger
                    memory_.write_value(pInfo + 52, (uint32_t)0);                          // dwSleepDefer
                    memory_.write_value(pInfo + 56, (uint16_t)260, EndianMemory::Halfword); // wMaxPath
                    memory_.write_value(pInfo + 60, (uint32_t)0x01000000);                  // dwPlatformID
                }
            }
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "IShell_GetDeviceInfoEx" || (!is_helper_hook && call_idx == 44)) {
            uint32_t nItem = is_thunk ? r5 : r1;
            uint32_t pBuff = is_thunk ? r6 : r2;
            uint32_t pnSize = is_thunk ? (cpu.get_reg(REG_R7)) : r3;
            printf("  GetDeviceInfoEx item=%d pBuff=0x%x pnSize=0x%x\n", nItem, pBuff, pnSize);
            // Default: report 0 size for unknown items
            if (pnSize && pnSize < 0xFF000000) {
                memory_.write_value(pnSize, 0);
            }
            cpu.set_reg(REG_R0, 0); // SUCCESS
        }
        else if (hook.name == "IShell_GetClassItemID" || (!is_helper_hook && call_idx == 45)) {
            uint32_t cls = is_thunk ? r5 : r1;
            // BREW returns the MIF item id for a class. Zeemu does not parse the
            // current applet MIF yet, but the running applet is a known class and
            // must not look absent to in-module startup code.
            uint32_t item_id = (cls != 0 && cls == current_applet_cls_) ? 1u : 0u;
            printf("  IShell_GetClassItemID cls=0x%08x current=0x%08x -> %u\n",
                   cls, current_applet_cls_, item_id);
            cpu.set_reg(REG_R0, item_id);
        }
        else if (hook.name == "IShell_SetTimer" || (!is_helper_hook && call_idx == 11)) {
            uint32_t ms   = is_thunk ? r5 : r1;
            uint32_t pfn  = is_thunk ? r6 : r2;
            uint32_t pUser= is_thunk ? (cpu.get_reg(REG_R7)) : r3;
            uint32_t callback_addr = 0;

            if (pfn == pUser && pfn != 0 && pfn < 0xFF000000) {
                // Heuristic: Quake and other Zeebo apps may pass an AEECallback* as BOTH pfn and pUser.
                // In BREW v1.1 AEECallback, pfnNotify is at offset 16, pNotifyData at offset 20.
                uint32_t real_pfn = memory_.read_value(pfn + 16);
                uint32_t real_pUser = memory_.read_value(pfn + 20);
                if (real_pfn >= 0x10000000 && real_pfn < 0xFF000000) {
                    if (std::getenv("ZEEMU_TRACE_TIMERS")) {
                        printf("  SetTimer: Detected AEECallback at 0x%08x -> real_pfn=0x%08x real_pUser=0x%08x\n", pfn, real_pfn, real_pUser);
                    }
                    callback_addr = pfn;
                    pfn = real_pfn;
                    pUser = real_pUser;
                } else if (auto it = timer_callback_bindings_.find(pfn); it != timer_callback_bindings_.end()) {
                    if (std::getenv("ZEEMU_TRACE_TIMERS")) {
                        printf("  SetTimer: Reusing cleared AEECallback at 0x%08x -> real_pfn=0x%08x real_pUser=0x%08x\n",
                               pfn, it->second.pfn, it->second.pUser);
                    }
                    callback_addr = pfn;
                    pfn = it->second.pfn;
                    pUser = it->second.pUser;
                }
            }

            uint32_t pfn_val = 0;
            if (pfn && pfn < 0xFF000000) pfn_val = memory_.read_value(pfn);
            const uint32_t effective_ms = (ms & 0x80000000u) ? 0u : ms;
            if (std::getenv("ZEEMU_TRACE_TIMERS")) {
                if (effective_ms != ms) {
                    printf("  SetTimer: ms=%u signed=%d -> effective=0 pfn=0x%08x (val=0x%08x) pUser=0x%08x\n",
                           ms, static_cast<int32_t>(ms), pfn, pfn_val, pUser);
                } else {
                    printf("  SetTimer: ms=%u pfn=0x%08x (val=0x%08x) pUser=0x%08x\n", ms, pfn, pfn_val, pUser);
                }
            }
            add_timer(pfn, pUser, effective_ms, callback_addr);
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "IShell_CancelTimer" || (!is_helper_hook && call_idx == 12)) {
            uint32_t pfn  = is_thunk ? r5 : r1;
            uint32_t pUser= is_thunk ? r6 : r2;
            if (pfn == pUser && pfn != 0 && pfn < 0xFF000000) {
                if (auto it = timer_callback_bindings_.find(pfn); it != timer_callback_bindings_.end()) {
                    pfn = it->second.pfn;
                    pUser = it->second.pUser;
                }
            }
            printf("  CancelTimer: pfn=0x%08x pUser=0x%08x\n", pfn, pUser);
            cancel_timer(pfn, pUser);
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "IShell_GetTimerExpiration" || (!is_helper_hook && call_idx == 13)) {
            uint32_t pfn  = is_thunk ? r5 : r1;
            uint32_t pUser= is_thunk ? r6 : r2;
            if (pfn == pUser && pfn != 0 && pfn < 0xFF000000) {
                if (auto it = timer_callback_bindings_.find(pfn); it != timer_callback_bindings_.end()) {
                    pfn = it->second.pfn;
                    pUser = it->second.pUser;
                }
            }
            const uint64_t now = uptime_ms();
            uint32_t remaining = 0;
            for (const auto& timer : pending_timers_) {
                if (timer.pfn == pfn && timer.pUser == pUser && timer.expire_ms > now) {
                    const uint64_t delta = timer.expire_ms - now;
                    remaining = delta > 0xffffffffull ? 0xffffffffu : static_cast<uint32_t>(delta);
                    break;
                }
            }
            if (std::getenv("ZEEMU_TRACE_TIMERS")) {
                printf("  GetTimerExpiration: pfn=0x%08x pUser=0x%08x -> %u ms\n", pfn, pUser, remaining);
            }
            cpu.set_reg(REG_R0, remaining);
        }
        else if (hook.name == "IShell_SetAlarm") {
            // ISHELL_SetAlarm(IShell*, AEECLSID cls, uint16 nID, uint32 dwMSecs):
            // deliver EVT_ALARM (wParam = nID) to the applet after dwMSecs.
            // Z-Wheel relies on these alarms to advance its launcher state
            // machine off the welcome splash; without them it stays on the
            // static welcome screen.
            const uint32_t cls_app = r1;
            const uint32_t alarm_id = r2 & 0xffffu;
            const uint32_t delay_ms = r3;
            if (std::getenv("ZEEMU_TRACE_TIMERS")) {
                printf("  SetAlarm: cls=0x%08x id=%u delay=%u ms\n", cls_app, alarm_id, delay_ms);
            }
            set_alarm(cls_app, alarm_id, delay_ms);
            cpu.set_reg(REG_R0, 0); // SUCCESS
        }
        else if (hook.name == "IShell_CancelAlarm") {
            // ISHELL_CancelAlarm(IShell*, AEECLSID cls, uint16 nID).
            const uint32_t cls_app = r1;
            const uint32_t alarm_id = r2 & 0xffffu;
            if (std::getenv("ZEEMU_TRACE_TIMERS")) {
                printf("  CancelAlarm: cls=0x%08x id=%u\n", cls_app, alarm_id);
            }
            cancel_alarm(cls_app, alarm_id);
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "IShell_GetUpTimeMS" || hook.name == "AEEHelper_GetUpTimeMS" || hook.name == "AEEHelper_GetTimeMS") {
            // BREW/QXEngine game loops compute elapsed simulation time from
            // GETUPTIMEMS/ISHELL_GetUpTimeMS, so return the same uptime domain
            // used by ISHELL_SetTimer expiration.
            auto now = static_cast<uint32_t>(uptime_ms());
            if (std::getenv("ZEEMU_TRACE_TIME")) {
                printf("  GetUpTimeMS -> %u ms\n", now);
            }
            cpu.set_reg(REG_R0, now);
        }
        else if (hook.name == "IShell_SendEvent" || (!is_helper_hook && call_idx == 21)) {
            constexpr uint32_t kEvtFlagUnique = 0x0001;
            constexpr uint32_t kEvtFlagAsync = 0x0002;
            uint32_t flags = is_thunk ? r4 : r1;
            uint32_t clsId = is_thunk ? r5 : r2;
            uint32_t evt = is_thunk ? r6 : r3;
            uint32_t wp = is_thunk ? 0u : memory_.read_value(sp);
            uint32_t dwp = is_thunk ? 0u : memory_.read_value(sp + 4);
            printf("  IShell_SendEvent: flags=0x%04x cls=0x%08x evt=0x%08x wp=0x%04x dwp=0x%08x\n",
                   flags, clsId, evt, wp, dwp);
            if ((flags & kEvtFlagAsync) && current_applet_obj_ != 0) {
                const bool unique = (flags & kEvtFlagUnique) != 0;
                bool already_pending = false;
                if (unique) {
                    for (const auto& e : pending_app_events_) {
                        if (e.event == evt) {
                            already_pending = true;
                            break;
                        }
                    }
                }
                if (!already_pending) {
                    char label[64];
                    std::snprintf(label, sizeof(label), "posted evt=0x%04x", evt);
                    queue_app_event(evt, wp, dwp, label);
                }
            }
            cpu.set_reg(REG_R0, 1);
        }
        else if (hook.name == "IShell_Resume" || (!is_helper_hook && call_idx == 36)) {
            uint32_t pcb = is_thunk ? r5 : r1;
            static int resume_logs = 0;
            if (resume_logs < 8 || std::getenv("ZEEMU_TRACE_HLE") != nullptr) {
                printf("  IShell_Resume: cb=0x%08x\n", pcb);
                ++resume_logs;
            }
            if (pcb == thread_resume_cb_) {
                queue_thread_resume(memory_.read_value(thread_resume_cb_ + 20));
            }
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "IShell_GetConfig" || is_validation_call) {
            uint32_t cfgId = is_thunk ? r5 : r1;
            uint32_t pData = is_thunk ? r6 : r2;
            printf("  GetConfig idx=%d cfgId=0x%x pData=0x%x\n", call_idx, cfgId, pData);
            if (cfgId == 20 || cfgId == 0x14 || cfgId == 268 || cfgId == 0x10C) {
                if (pData != 0 && pData < 0xFF000000) {
                    if (pData == object_ptr_) {
                        memory_.write_value(pData + 4, 1);
                        printf("    [IShell_GetConfig] simulated TRUE at object_ptr+4\n");
                    } else {
                        memory_.write_value(pData, 1);
                        printf("    [IShell_GetConfig] simulated TRUE at pData\n");
                    }
                }
            }
            cpu.set_reg(REG_R0, 0);
        }
        else if (handle_aee_helper_hook(hook, cpu, call_idx)) {
            return;
        }
        else if (hook.name == "SignalCBFactory_CreateSignal") {
            uint32_t callback = r1;
            uint32_t pUser = r2;
            uint32_t ppSignal = r3;
            uint32_t ppSignalCtl = memory_.read_value(sp);
            addr_t signal = malloc(0x18);
            memory_.write_value(signal + 0, signal_vtable_);
            memory_.write_value(signal + 4, callback);
            memory_.write_value(signal + 8, pUser);
            memory_.write_value(signal + 12, 0); // detached
            memory_.write_value(signal + 16, 0); // ready while disabled
            if (ppSignal && ppSignal < 0xFF000000) memory_.write_value(ppSignal, signal);
            if (ppSignalCtl && ppSignalCtl < 0xFF000000) memory_.write_value(ppSignalCtl, signal);
            printf("  SignalCBFactory_CreateSignal cb=0x%08x pUser=0x%08x -> 0x%08x\n",
                   callback, pUser, signal);
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "Signal_Set") {
            set_signal(r0, "signal");
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "Signal_Detach") {
            if (r0 && r0 < 0xFF000000) {
                memory_.write_value(r0 + 12, 1);
                memory_.write_value(r0 + 16, 0);
            }
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "Signal_Enable") {
            if (r0 && r0 < 0xFF000000 && memory_.read_value(r0 + 16) != 0) {
                memory_.write_value(r0 + 16, 0);
                set_signal(r0, "signal-enable");
            }
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "Thread_Start") {
            const uint32_t thread_obj = r0;
            const uint32_t stack_size = r1;
            const uint32_t entry = r2;
            const uint32_t pUser = r3;
            printf("  Thread_Start thread=0x%08x stack=0x%08x entry=0x%08x pUser=0x%08x\n",
                   thread_obj, stack_size, entry, pUser);
            if (entry != 0 && entry < 0xFF000000) {
                queue_thread(thread_obj, entry, pUser, stack_size, "thread");
            }
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "Thread_Malloc") {
            uint32_t size = r1;
            cpu.set_reg(REG_R0, malloc(size));
        }
        else if (hook.name == "Thread_Free") {
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "Thread_HoldRsc" || hook.name == "Thread_ReleaseRsc") {
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "Thread_Suspend") {
            thread_yield_requested_ = true;
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "Thread_Exit") {
            cpu.set_reg(REG_R0, 0);
            cpu.set_reg(REG_PC, 0xDEADBEE0u);
        }
        else if (hook.name == "Thread_Join") {
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "Thread_GetResumeCBK") {
            if (r0 != 0 && r0 < 0xFF000000) {
                memory_.write_value(thread_resume_cb_ + 20, r0);
            }
            cpu.set_reg(REG_R0, thread_resume_cb_);
        }
        else if (hook.name == "Thread_ResumeCallback") {
            if (r0 != 0 && r0 < 0xFF000000) {
                queue_thread_resume(r0);
            }
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "Thread_QueryInterface") {
            uint32_t pp = r2;
            if (pp && pp < 0xFF000000) memory_.write_value(pp, stub_thread_obj_);
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "StubCom_AddRef" || hook.name == "SignalCBFactory_AddRef" || hook.name == "Signal_AddRef" || hook.name == "Thread_AddRef") {
            cpu.set_reg(REG_R0, 1);
        }
        else if (hook.name == "StubCom_Release" || hook.name == "SignalCBFactory_Release" || hook.name == "Signal_Release" || hook.name == "Thread_Release") {
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "IShell_LoadResObject" || (!is_helper_hook && call_idx == 19)) {
            char path[512] = {};
            read_string(is_thunk ? r5 : r1, path, sizeof(path));
            uint32_t res_id = is_thunk ? r6 : r2;
            uint32_t htype = is_thunk ? cpu.get_reg(REG_R7) : r3;
            printf("  [IShell_LoadResObject] path='%s' id=0x%x htype=0x%x\n", path, res_id, htype);

            // Return a real image object for raster resources.
            std::string sp(path);
            const std::string lower_path = lower_ascii(sp);
            if (lower_path.find(".png") != std::string::npos ||
                lower_path.find(".bmp") != std::string::npos ||
                lower_path.find(".gif") != std::string::npos ||
                lower_path.find(".jpg") != std::string::npos ||
                lower_path.find(".jpeg") != std::string::npos) {
                auto image = std::make_shared<BrewImage>(*this, memory_, sp, current_directory_);
                addr_t image_obj = image->get_object_ptr();
                images_.push_back(image);
                printf("  [IShell_LoadResObject] image file '%s' -> 0x%08x\n", sp.c_str(), image_obj);
                cpu.set_reg(REG_R0, image_obj);
            } else if (lower_path.find(".bar") != std::string::npos) {
                std::string bar_data;
                if (vfs_.read_file(sp, bar_data, current_directory_)) {
                    BarResource bar;
                    if (parse_bar_resource_file(bar_data, bar)) {
                        const BarResource::Entry* entry = find_bar_image_entry(bar, static_cast<uint16_t>(res_id));
                        if (entry) {
                            std::string label = sp + "#" + std::to_string(entry->id);
                            if (wants_bitmap_object(htype) && lower_ascii(entry->mime) == "image/bmp") {
                                auto bitmap = make_bitmap_from_winbmp(*this, memory_, entry->data.data(), entry->data.size());
                                if (bitmap) {
                                    const addr_t bitmap_obj = bitmap->get_object_ptr();
                                    printf("  [IShell_LoadResObject] BAR bitmap id=%u mime='%s' size=%zu -> 0x%08x\n",
                                           entry->id, entry->mime.c_str(), entry->data.size(), bitmap_obj);
                                    g_resource_bitmaps.push_back(std::move(bitmap));
                                    cpu.set_reg(REG_R0, bitmap_obj);
                                } else {
                                    printf("  [IShell_LoadResObject] BAR bitmap id=%u decode failed\n", entry->id);
                                    cpu.set_reg(REG_R0, 0);
                                }
                            } else {
                                auto image = std::make_shared<BrewImage>(*this, memory_, label, entry->data.data(), entry->data.size());
                                addr_t image_obj = image->get_object_ptr();
                                images_.push_back(image);
                                printf("  [IShell_LoadResObject] BAR image id=%u mime='%s' size=%zu\n", entry->id, entry->mime.c_str(), entry->data.size());
                                cpu.set_reg(REG_R0, image_obj);
                            }
                        } else {
                            printf("  [IShell_LoadResObject] BAR resource id=0x%x not found or unsupported\n", res_id);
                            cpu.set_reg(REG_R0, 0);
                        }
                    } else {
                        printf("  [IShell_LoadResObject] BAR parse failed for '%s'\n", sp.c_str());
                        cpu.set_reg(REG_R0, 0);
                    }
                } else {
                    printf("  [IShell_LoadResObject] BAR file missing: '%s'\n", sp.c_str());
                    cpu.set_reg(REG_R0, 0);
                }
            } else {
                cpu.set_reg(REG_R0, stub_com_obj_);
            }
        }
        else if (hook.name == "IShell_LoadResString" || (!is_helper_hook && call_idx == 17)) {
            char path[512] = {};
            read_string(is_thunk ? r5 : r1, path, sizeof(path));
            auto res_id = static_cast<uint16_t>(is_thunk ? r6 : r2);
            addr_t pBuff = is_thunk ? cpu.get_reg(REG_R7) : r3;
            uint32_t nSize = memory_.read_value(sp);
            std::u16string text;
            printf("  [IShell_LoadResString] path='%s' id=0x%x pBuff=0x%x nSize=%u\n", path, res_id, pBuff, nSize);

            if (!load_string_payload(*this, path, res_id, text) || !pBuff || nSize < 2) {
                if (std::getenv("ZEEMU_TRACE_STRINGS")) {
                    printf("  [STRINGS_TRACE] LoadResString path='%s' id=0x%x -> ret=0\n", path, res_id);
                }
                cpu.set_reg(REG_R0, 0);
            } else {
                const uint32_t capacity = nSize / 2;
                const uint32_t to_copy = capacity > 0 ? std::min<uint32_t>(static_cast<uint32_t>(text.size()), capacity - 1) : 0;
                for (uint32_t i = 0; i < to_copy; ++i) {
                    memory_.write_value(pBuff + i * 2, static_cast<uint16_t>(text[i]), EndianMemory::Halfword);
                }
                memory_.write_value(pBuff + to_copy * 2, (uint16_t)0, EndianMemory::Halfword);
                if (std::getenv("ZEEMU_TRACE_STRINGS")) {
                    const std::string preview = preview_u16_ascii(text);
                    printf("  [STRINGS_TRACE] LoadResString path='%s' id=0x%x -> ret=%u text='%s'\n",
                           path, res_id, to_copy, preview.c_str());
                }
                cpu.set_reg(REG_R0, to_copy);
            }
        }
        else if (hook.name == "IShell_LoadResData" || (!is_helper_hook && call_idx == 18)) {
            char path[512] = {};
            read_string(is_thunk ? r5 : r1, path, sizeof(path));
            auto res_id = static_cast<uint16_t>(is_thunk ? r6 : r2);
            uint32_t nType = is_thunk ? cpu.get_reg(REG_R7) : r3;
            std::vector<uint8_t> payload;
            std::vector<uint8_t> raw_resource;
            std::string mime;
            bool from_bar = false;
            printf("  [IShell_LoadResData] path='%s' id=0x%x type=0x%x\n", path, res_id, nType);

            const bool trace_media = std::getenv("ZEEMU_TRACE_MEDIA") != nullptr;
            const uint64_t load_start = trace_media ? host_now_ms() : 0;
            const bool loaded = load_resource_payload(*this, path, res_id, payload, mime, &from_bar, &raw_resource);
            const uint64_t load_done = trace_media ? host_now_ms() : 0;
            std::vector<uint8_t> blob;
            if (loaded) {
                blob = (nType == RESTYPE_IMAGE)
                    ? ((from_bar && !raw_resource.empty()) ? raw_resource : make_res_blob(payload, mime))
                    : payload;
            }
            if (!loaded || blob.empty()) {
                cpu.set_reg(REG_R0, 0);
            } else {
                addr_t dst = malloc(static_cast<uint32_t>(blob.size()));
                const uint64_t copy_start = trace_media ? host_now_ms() : 0;
                write_guest_blob(memory_, dst, blob);
                const uint64_t copy_done = trace_media ? host_now_ms() : 0;
                printf("  [IShell_LoadResData] %s size=%zu mime='%s' -> 0x%08x\n",
                       from_bar ? "BAR" : "file", blob.size(), mime.c_str(), dst);
                if (trace_media && is_audio_mime(mime)) {
                    printf("  [MEDIA_TRACE] LoadResData id=0x%x size=%zu mime='%s' load_ms=%llu copy_ms=%llu\n",
                           res_id,
                           blob.size(),
                           mime.c_str(),
                           static_cast<unsigned long long>(load_done - load_start),
                           static_cast<unsigned long long>(copy_done - copy_start));
                }
                cpu.set_reg(REG_R0, dst);
            }
        }
        else if (hook.name == "IShell_Prompt" || (!is_helper_hook && call_idx == 26)) {
            // SDK AEEShell.h: ISHELL_Prompt returns TRUE when the prompt is created.
            // Zeebo titles use this for transient modal/status prompts during loading;
            // failing it can incorrectly abort asset load paths.
            addr_t pInfo = is_thunk ? r5 : r1;
            printf("  IShell_Prompt pInfo=0x%08x -> TRUE\n", pInfo);
            cpu.set_reg(REG_R0, 1);
        }
        else if (hook.name == "IShell_LoadResDataEx" || (!is_helper_hook && call_idx == 41)) {
            char path[512] = {};
            read_string(is_thunk ? r5 : r1, path, sizeof(path));
            auto res_id = static_cast<uint16_t>(is_thunk ? r6 : r2);
            uint32_t nType = is_thunk ? cpu.get_reg(REG_R7) : r3;
            addr_t pBuf = memory_.read_value(sp);
            addr_t pnBufSize = memory_.read_value(sp + 4);
            std::vector<uint8_t> payload;
            std::vector<uint8_t> raw_resource;
            std::string mime;
            bool from_bar = false;
            printf("  [IShell_LoadResDataEx] path='%s' id=0x%x type=0x%x pBuf=0x%x pnBufSize=0x%x\n",
                   path, res_id, nType, pBuf, pnBufSize);

            const bool trace_media = std::getenv("ZEEMU_TRACE_MEDIA") != nullptr;
            const uint64_t load_start = trace_media ? host_now_ms() : 0;
            const bool loaded = pnBufSize && pnBufSize < 0xFF000000 &&
                load_resource_payload(*this, path, res_id, payload, mime, &from_bar, &raw_resource);
            const uint64_t load_done = trace_media ? host_now_ms() : 0;
            if (!pnBufSize || pnBufSize >= 0xFF000000) {
                cpu.set_reg(REG_R0, 0);
            } else if (loaded && !payload.empty()) {
                std::vector<uint8_t> blob = (nType == RESTYPE_IMAGE)
                    ? ((from_bar && !raw_resource.empty()) ? raw_resource : make_res_blob(payload, mime))
                    : payload;
                const auto needed = static_cast<uint32_t>(blob.size());
                uint64_t copy_ms = 0;

                if (pBuf == 0xFFFFFFFFu) {
                    memory_.write_value(pnBufSize, needed);
                    cpu.set_reg(REG_R0, 0xFFFFFFFFu);
                } else if (!pBuf) {
                    addr_t dst = malloc(needed);
                    const uint64_t copy_start = trace_media ? host_now_ms() : 0;
                    write_guest_blob(memory_, dst, blob);
                    const uint64_t copy_done = trace_media ? host_now_ms() : 0;
                    copy_ms = copy_done - copy_start;
                    memory_.write_value(pnBufSize, needed);
                    cpu.set_reg(REG_R0, dst);
                } else {
                    uint32_t provided = memory_.read_value(pnBufSize);
                    if (provided < needed) {
                        memory_.write_value(pnBufSize, needed);
                        cpu.set_reg(REG_R0, 0);
                    } else {
                        const uint64_t copy_start = trace_media ? host_now_ms() : 0;
                        write_guest_blob(memory_, pBuf, blob);
                        const uint64_t copy_done = trace_media ? host_now_ms() : 0;
                        copy_ms = copy_done - copy_start;
                        memory_.write_value(pnBufSize, needed);
                        cpu.set_reg(REG_R0, pBuf);
                    }
                }

                printf("  [IShell_LoadResDataEx] %s size=%zu mime='%s'\n",
                       from_bar ? "BAR" : "file", blob.size(), mime.c_str());
                if (trace_media && is_audio_mime(mime)) {
                    printf("  [MEDIA_TRACE] LoadResDataEx id=0x%x size=%zu mime='%s' load_ms=%llu copy_ms=%llu pBuf=0x%08x ret=0x%08x\n",
                           res_id,
                           blob.size(),
                           mime.c_str(),
                           static_cast<unsigned long long>(load_done - load_start),
                           static_cast<unsigned long long>(copy_ms),
                           pBuf,
                           cpu.get_reg(REG_R0));
                }
            } else {
                memory_.write_value(pnBufSize, 0u);
                printf("  [IShell_LoadResDataEx] missing resource -> size=0\n");
                cpu.set_reg(REG_R0, 0);
            }
        }
        else if (hook.name == "IShell_FreeResData" || (!is_helper_hook && call_idx == 20)) {
            addr_t pData = is_thunk ? r5 : r1;
            printf("  [IShell_FreeResData] pData=0x%08x\n", pData);
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "IShell_IsValidResource" || (!is_helper_hook && call_idx == 40)) {
            char path[512] = {};
            read_string(is_thunk ? r5 : r1, path, sizeof(path));
            auto res_id = static_cast<uint16_t>(is_thunk ? r6 : r2);
            uint32_t nType = is_thunk ? cpu.get_reg(REG_R7) : r3;
            std::vector<uint8_t> payload;
            std::string mime;
            bool from_bar = false;
            bool ok = load_resource_payload(*this, path, res_id, payload, mime, &from_bar) && !payload.empty();
            printf("  [IShell_IsValidResource] path='%s' id=0x%x type=0x%x -> %s\n",
                   path, res_id, nType, ok ? "TRUE" : "FALSE");
            cpu.set_reg(REG_R0, ok ? 1u : 0u);
        }
        else if (hook.name.find("_QueryInterface") != std::string::npos) {
            uint32_t ppObj = is_thunk ? r6 : r2;
            if (ppObj && ppObj < 0xFF000000) {
                memory_.write_value(ppObj, r0);
            }
            cpu.set_reg(REG_R0, 0); // SUCCESS
        }
        else if (hook.name.substr(0, 8) == "StubCom_") {
            if (hook.name != "StubCom_AddRef" && hook.name != "StubCom_Release" &&
                hook.name != "StubCom_QueryInterface") {
                printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x R5=0x%08x R6=0x%08x\n",
                       hook.name.c_str(), r0, r1, r2, r3, r5, r6);
            }
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name.rfind("ILicense_", 0) == 0) {
            constexpr uint32_t kLtNone = 0;
            constexpr uint32_t kPtNone = 0;
            constexpr uint32_t kBvUnlimited = 0xffffffffu;

            if (hook.name == "ILicense_AddRef") {
                cpu.set_reg(REG_R0, 1);
            } else if (hook.name == "ILicense_Release") {
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "ILicense_IsExpired") {
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "ILicense_GetInfo") {
                const uint32_t pExpire = is_thunk ? r5 : r1;
                if (pExpire && pExpire < 0xFF000000) {
                    memory_.write_value(pExpire, kBvUnlimited);
                }
                printf("  [ILicense_GetInfo] LT_NONE expire=BV_UNLIMITED\n");
                cpu.set_reg(REG_R0, kLtNone);
            } else if (hook.name == "ILicense_SetUsesRemaining") {
                const uint32_t count = is_thunk ? r5 : r1;
                printf("  [ILicense_SetUsesRemaining] count=%u ignored for LT_NONE\n", count);
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "ILicense_GetPurchaseInfo") {
                const uint32_t pLt = is_thunk ? r5 : r1;
                const uint32_t pExpire = is_thunk ? r6 : r2;
                const uint32_t pSeq = is_thunk ? cpu.get_reg(REG_R7) : r3;
                if (pLt && pLt < 0xFF000000) {
                    memory_.write_value(pLt, static_cast<uint8_t>(kLtNone), EndianMemory::Byte);
                }
                if (pExpire && pExpire < 0xFF000000) {
                    memory_.write_value(pExpire, kBvUnlimited);
                }
                if (pSeq && pSeq < 0xFF000000) {
                    memory_.write_value(pSeq, 0u);
                }
                printf("  [ILicense_GetPurchaseInfo] PT_NONE LT_NONE expire=BV_UNLIMITED seq=0\n");
                cpu.set_reg(REG_R0, kPtNone);
            } else {
                printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x R5=0x%08x R6=0x%08x\n",
                       hook.name.c_str(), r0, r1, r2, r3, r5, r6);
                cpu.set_reg(REG_R0, 0);
            }
        }
        else if (hook.name.rfind("IWeb_", 0) == 0) {
            constexpr uint32_t kSuccess = 0;
            constexpr uint32_t kEBadParm = 14;
            constexpr uint32_t kENoSuch = 39;
            auto plausible_ptr = [](uint32_t ptr) {
                return (ptr >= 0x1e000000u && ptr < 0x20000000u) ||
                       (ptr >= 0x50000000u && ptr < 0x70000000u);
            };
            auto stack_arg = [&](uint32_t index) -> uint32_t {
                if (sp == 0 || sp >= 0xFF000000u) {
                    return 0;
                }
                return memory_.read_value(sp + index * 4);
            };

            if (hook.name == "IWeb_AddRef") {
                cpu.set_reg(REG_R0, 1);
            } else if (hook.name == "IWeb_Release") {
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "IWeb_QueryInterface") {
                const uint32_t ppObj = is_thunk ? r6 : r2;
                if (plausible_ptr(ppObj)) {
                    memory_.write_value(ppObj, web_obj_);
                }
                cpu.set_reg(REG_R0, kSuccess);
            } else if (hook.name == "IWeb_AddOpt") {
                const uint32_t opts = is_thunk ? r5 : r1;
                if (!plausible_ptr(opts)) {
                    printf("  [IWeb_AddOpt] invalid opts=0x%08x\n", opts);
                    cpu.set_reg(REG_R0, kEBadParm);
                } else {
                    printf("  [IWeb_AddOpt] opts=0x%08x", opts);
                    for (uint32_t i = 0; i < 16; ++i) {
                        const uint32_t item = opts + i * 8;
                        const uint32_t id = memory_.read_value(item);
                        const uint32_t val = memory_.read_value(item + 4);
                        printf(" [%u:id=0x%08x,val=0x%08x]", i, id, val);
                        if (id == 0) {
                            break;
                        }
                    }
                    printf("\n");
                    cpu.set_reg(REG_R0, kSuccess);
                }
            } else if (hook.name == "IWeb_RemoveOpt") {
                const uint32_t opt_id = is_thunk ? r5 : r1;
                const uint32_t index = is_thunk ? r6 : r2;
                printf("  [IWeb_RemoveOpt] id=0x%08x index=%u\n", opt_id, index);
                cpu.set_reg(REG_R0, kSuccess);
            } else if (hook.name == "IWeb_GetOpt") {
                const uint32_t opt_id = is_thunk ? r5 : r1;
                const uint32_t index = is_thunk ? r6 : r2;
                const uint32_t out = is_thunk ? cpu.get_reg(REG_R7) : r3;
                if (plausible_ptr(out)) {
                    memory_.write_value(out, 0u);
                    memory_.write_value(out + 4, 0u);
                }
                printf("  [IWeb_GetOpt] id=0x%08x index=%u -> ENOSUCH\n", opt_id, index);
                cpu.set_reg(REG_R0, kENoSuch);
            } else if (hook.name == "IWeb_GetResponse" || hook.name == "IWeb_GetResponseV") {
                const uint32_t ppResp = is_thunk ? r5 : r1;
                const uint32_t pcb = is_thunk ? r6 : r2;
                const uint32_t url_ptr = is_thunk ? cpu.get_reg(REG_R7) : r3;
                const uint32_t opt_list = (hook.name == "IWeb_GetResponseV") ? stack_arg(0) : 0;
                char url[256] = {};
                if (url_ptr && url_ptr < 0xFF000000u) {
                    read_string(url_ptr, url, sizeof(url));
                }
                if (plausible_ptr(ppResp)) {
                    memory_.write_value(ppResp, 0u);
                }
                if (plausible_ptr(pcb)) {
                    const uint32_t pfn = memory_.read_value(pcb + 16);
                    const uint32_t pUser = memory_.read_value(pcb + 20);
                    if (pfn >= 0x10000000u && pfn < 0xFF000000u) {
                        add_timer(pfn, pUser, 0, pcb);
                    }
                }
                printf("  [%s] offline url='%s' ppResp=0x%08x cb=0x%08x opts=0x%08x -> no response\n",
                       hook.name.c_str(), url, ppResp, pcb, opt_list);
                cpu.set_reg(REG_R0, kSuccess);
            } else {
                cpu.set_reg(REG_R0, kENoSuch);
            }
        }
        else if (hook.name.rfind("ITextCtl_", 0) == 0) {
            auto plausible_ptr = [](uint32_t ptr) {
                return (ptr >= 0x1e000000u && ptr < 0x20000000u) ||
                       (ptr >= 0x50000000u && ptr < 0x70000000u);
            };
            auto& state = textctl_states_[r0];
            if (state.text_buffer == 0) {
                state.text_buffer = malloc(512);
                memory_.write_value(state.text_buffer, static_cast<uint16_t>(0), EndianMemory::Halfword);
            }
            auto clear_text = [&]() {
                state.text_chars = 0;
                state.cursor = 0;
                state.selection = 0;
                if (state.text_buffer) {
                    memory_.write_value(state.text_buffer, static_cast<uint16_t>(0), EndianMemory::Halfword);
                }
            };

            if (hook.name == "ITextCtl_AddRef") {
                cpu.set_reg(REG_R0, 1);
            } else if (hook.name == "ITextCtl_Release") {
                textctl_states_.erase(r0);
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "ITextCtl_HandleEvent") {
                printf("  [ITextCtl_HandleEvent] evt=0x%08x wp=0x%08x dw=0x%08x\n", r1, r2, r3);
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "ITextCtl_Redraw") {
                cpu.set_reg(REG_R0, 1);
            } else if (hook.name == "ITextCtl_SetActive") {
                state.active = (r1 != 0);
                printf("  [ITextCtl_SetActive] active=%u\n", state.active ? 1u : 0u);
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "ITextCtl_IsActive") {
                cpu.set_reg(REG_R0, state.active ? 1u : 0u);
            } else if (hook.name == "ITextCtl_SetRect") {
                if (plausible_ptr(r1)) {
                    state.rect_x = static_cast<int16_t>(memory_.read_value(r1 + 0, EndianMemory::Halfword));
                    state.rect_y = static_cast<int16_t>(memory_.read_value(r1 + 2, EndianMemory::Halfword));
                    state.rect_dx = static_cast<int16_t>(memory_.read_value(r1 + 4, EndianMemory::Halfword));
                    state.rect_dy = static_cast<int16_t>(memory_.read_value(r1 + 6, EndianMemory::Halfword));
                }
                printf("  [ITextCtl_SetRect] rect=(%d,%d,%d,%d)\n",
                       state.rect_x, state.rect_y, state.rect_dx, state.rect_dy);
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "ITextCtl_GetRect") {
                if (plausible_ptr(r1)) {
                    memory_.write_value(r1 + 0, static_cast<uint16_t>(state.rect_x), EndianMemory::Halfword);
                    memory_.write_value(r1 + 2, static_cast<uint16_t>(state.rect_y), EndianMemory::Halfword);
                    memory_.write_value(r1 + 4, static_cast<uint16_t>(state.rect_dx), EndianMemory::Halfword);
                    memory_.write_value(r1 + 6, static_cast<uint16_t>(state.rect_dy), EndianMemory::Halfword);
                }
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "ITextCtl_SetProperties") {
                state.properties = r1;
                printf("  [ITextCtl_SetProperties] props=0x%08x\n", state.properties);
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "ITextCtl_GetProperties") {
                cpu.set_reg(REG_R0, state.properties);
            } else if (hook.name == "ITextCtl_Reset") {
                clear_text();
                state.active = false;
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "ITextCtl_SetTitle") {
                cpu.set_reg(REG_R0, 1);
            } else if (hook.name == "ITextCtl_SetText") {
                const uint32_t src = r1;
                uint32_t chars = r2;
                if (chars == 0xffffffffu && plausible_ptr(src)) {
                    chars = 0;
                    while (chars < state.max_chars && memory_.read_value(src + chars * 2, EndianMemory::Halfword) != 0) {
                        ++chars;
                    }
                }
                if (chars > state.max_chars) {
                    chars = state.max_chars;
                }
                if (chars > 255) {
                    chars = 255;
                }
                if (plausible_ptr(src) && state.text_buffer) {
                    for (uint32_t i = 0; i < chars; ++i) {
                        const uint16_t ch = static_cast<uint16_t>(memory_.read_value(src + i * 2, EndianMemory::Halfword));
                        memory_.write_value(state.text_buffer + i * 2, ch, EndianMemory::Halfword);
                    }
                    memory_.write_value(state.text_buffer + chars * 2, static_cast<uint16_t>(0), EndianMemory::Halfword);
                    state.text_chars = chars;
                    state.cursor = chars;
                    state.selection = chars | (chars << 16);
                }
                printf("  [ITextCtl_SetText] chars=%u\n", state.text_chars);
                cpu.set_reg(REG_R0, 1);
            } else if (hook.name == "ITextCtl_GetText") {
                const uint32_t dst = r1;
                uint32_t max_chars = r2;
                uint32_t copied = 0;
                if (plausible_ptr(dst) && max_chars != 0) {
                    const uint32_t limit = (state.text_chars < (max_chars - 1)) ? state.text_chars : (max_chars - 1);
                    for (; copied < limit; ++copied) {
                        const uint16_t ch = static_cast<uint16_t>(memory_.read_value(state.text_buffer + copied * 2, EndianMemory::Halfword));
                        memory_.write_value(dst + copied * 2, ch, EndianMemory::Halfword);
                    }
                    memory_.write_value(dst + copied * 2, static_cast<uint16_t>(0), EndianMemory::Halfword);
                }
                printf("  [ITextCtl_GetText] copied=%u max=%u\n", copied, max_chars);
                cpu.set_reg(REG_R0, 1);
            } else if (hook.name == "ITextCtl_GetTextPtr") {
                cpu.set_reg(REG_R0, state.text_buffer);
            } else if (hook.name == "ITextCtl_EnableCommand") {
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "ITextCtl_SetMaxSize") {
                state.max_chars = r1 & 0xffffu;
                if (state.max_chars == 0 || state.max_chars > 255) {
                    state.max_chars = 255;
                }
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "ITextCtl_SetSoftKeyMenu") {
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "ITextCtl_SetInputMode") {
                state.input_mode = r1;
                printf("  [ITextCtl_SetInputMode] mode=0x%08x\n", state.input_mode);
                cpu.set_reg(REG_R0, state.input_mode);
            } else if (hook.name == "ITextCtl_GetCursorPos") {
                cpu.set_reg(REG_R0, state.cursor);
            } else if (hook.name == "ITextCtl_SetCursorPos") {
                state.cursor = (r1 <= state.text_chars) ? r1 : state.text_chars;
                state.selection = state.cursor | (state.cursor << 16);
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "ITextCtl_GetInputMode") {
                if (plausible_ptr(r1)) {
                    memory_.write_value(r1, state.input_mode);
                }
                cpu.set_reg(REG_R0, state.input_mode);
            } else if (hook.name == "ITextCtl_EnumModeInit") {
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "ITextCtl_EnumNextMode") {
                if (plausible_ptr(r1)) {
                    memory_.write_value(r1, state.input_mode);
                }
                cpu.set_reg(REG_R0, state.input_mode);
            } else if (hook.name == "ITextCtl_SetSelection") {
                state.selection = r1;
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "ITextCtl_GetSelection") {
                cpu.set_reg(REG_R0, state.selection);
            } else if (hook.name == "ITextCtl_SetPropertiesEx") {
                state.properties_ex = r1;
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "ITextCtl_GetPropertiesEx") {
                cpu.set_reg(REG_R0, state.properties_ex);
            } else {
                printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
                       hook.name.c_str(), r0, r1, r2, r3);
                cpu.set_reg(REG_R0, 0);
            }
        }
        else if (hook.name.substr(0, 9) == "DummySvc_") {
            printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x R5=0x%08x R6=0x%08x\n",
                   hook.name.c_str(), r0, r1, r2, r3, r5, r6);
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name.rfind("IImage_", 0) == 0) {
            if (hook.name == "IImage_AddRef") {
                cpu.set_reg(REG_R0, 1);
            } else if (hook.name == "IImage_Release") {
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "IImage_GetInfo") {
                const uint32_t pInfo = is_thunk ? r5 : r1;
                if (pInfo && pInfo < 0xFF000000) {
                    memory_.write_value(pInfo + 0, static_cast<uint16_t>(1), EndianMemory::Halfword);
                    memory_.write_value(pInfo + 2, static_cast<uint16_t>(1), EndianMemory::Halfword);
                    memory_.write_value(pInfo + 4, static_cast<uint16_t>(2), EndianMemory::Halfword);
                    memory_.write_value(pInfo + 6, static_cast<uint8_t>(16), EndianMemory::Byte);
                }
                cpu.set_reg(REG_R0, 0);
            } else if (hook.name == "IImage_HandleEvent") {
                cpu.set_reg(REG_R0, 0);
            } else {
                printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x R5=0x%08x R6=0x%08x\n",
                       hook.name.c_str(), r0, r1, r2, r3, r5, r6);
                cpu.set_reg(REG_R0, 0);
            }
        }
        else if (hook.name.rfind("SignalCBFactory_Fn", 0) == 0 ||
                 hook.name.rfind("Signal_Fn", 0) == 0 ||
                 hook.name.rfind("Thread_Fn", 0) == 0) {
            printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x R5=0x%08x R6=0x%08x\n",
                   hook.name.c_str(), r0, r1, r2, r3, r5, r6);
            cpu.set_reg(REG_R0, 0);
        }
        else if (hook.name == "IShell_CheckPrivLevel") {
            cpu.set_reg(REG_R0, 0); // 0 = EPRIVLEVEL_NONE = allowed
        }
        else if (hook.name.find("IDisplay_Update") != std::string::npos) {
            printf("  IDisplay_Update called!\n");
            cpu.set_reg(REG_R0, 0);
        }
        else {
            if (is_thunk && (call_idx < 64)) {
                printf("  [IShell_Idx_%d] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x R5=0x%08x R6=0x%08x\n",
                       call_idx, r0, r1, r2, r3, r5, r6);
                cpu.set_reg(REG_R0, 0);
            } else {
                printf("  [%s] not implemented yet Idx=%d R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x R5=0x%08x R6=0x%08x\n",
                       hook.name.c_str(), call_idx, r0, r1, r2, r3, r5, r6);
                cpu.set_reg(REG_R0, 0);
            }
        }
        if (!skip_final_flush || std::getenv("ZEEMU_FLUSH_EACH_HOOK") != nullptr) {
            fflush(stdout);
        }
    }
}

void BrewShell::add_timer(uint32_t pfn, uint32_t pUser, uint32_t delay_ms, uint32_t callback_addr) {
    if (pfn == 0) return;
    cancel_timer(pfn, pUser); // remove duplicate
    uint64_t expire = uptime_ms() + delay_ms;
    if (callback_addr != 0 && callback_addr < 0xFF000000) {
        timer_callback_bindings_[callback_addr] = TimerCallbackBinding{pfn, pUser};
    }
    pending_timers_.push_back({pfn, pUser, expire, callback_addr});
}

void BrewShell::cancel_timer(uint32_t pfn, uint32_t pUser) {
    pending_timers_.erase(
        std::remove_if(pending_timers_.begin(), pending_timers_.end(),
            [pfn, pUser](const Timer& t) {
                const bool pfn_matches = (pfn == 0) || (t.pfn == pfn);
                const bool user_matches = (pUser == 0) || (t.pUser == pUser);
                return pfn_matches && user_matches;
            }),
        pending_timers_.end());
}

std::vector<BrewShell::Timer> BrewShell::pop_expired_timers(uint64_t now_ms) {
    std::vector<Timer> expired;
    auto it = pending_timers_.begin();
    while (it != pending_timers_.end()) {
        if (it->expire_ms <= now_ms) {
            if (it->callback_addr != 0 && it->callback_addr < 0xFF000000) {
                // BREW AEECallback timers are one-shot. The shell cancels the
                // callback before invoking it; recurring users must reinitialize
                // the AEECallback before scheduling it again. Quake depends on
                // this to avoid re-entering its initialization timer.
                memory_.write_value(it->callback_addr + 16, 0u);
                memory_.write_value(it->callback_addr + 20, 0u);
            }
            expired.push_back(*it);
            it = pending_timers_.erase(it);
        } else {
            ++it;
        }
    }
    // Fire any due ISHELL_SetAlarm alarms in the same poll. Alarms deliver
    // EVT_ALARM (wParam = nID) to the applet rather than calling a guest
    // callback, so they go through the normal pending app-event queue that the
    // runner drains. One-shot, matching BREW alarm semantics.
    auto ait = pending_alarms_.begin();
    while (ait != pending_alarms_.end()) {
        if (ait->expire_ms <= now_ms) {
            const uint32_t alarm_id = ait->alarm_id;
            queue_app_event(0x400u /* EVT_ALARM */, alarm_id, 0u,
                            "EVT_ALARM id=" + std::to_string(alarm_id));
            ait = pending_alarms_.erase(ait);
        } else {
            ++ait;
        }
    }
    return expired;
}

void BrewShell::set_alarm(uint32_t cls_app, uint32_t alarm_id, uint32_t delay_ms) {
    cancel_alarm(cls_app, alarm_id); // a re-set replaces the pending alarm
    const uint64_t expire = uptime_ms() + delay_ms;
    pending_alarms_.push_back({cls_app, alarm_id, expire});
}

void BrewShell::cancel_alarm(uint32_t cls_app, uint32_t alarm_id) {
    pending_alarms_.erase(
        std::remove_if(pending_alarms_.begin(), pending_alarms_.end(),
            [cls_app, alarm_id](const PendingAlarm& a) {
                return a.cls_app == cls_app && a.alarm_id == alarm_id;
            }),
        pending_alarms_.end());
}

void BrewShell::queue_app_event(uint32_t event, uint32_t wParam, uint32_t dwParam, std::string label) {
    pending_app_events_.push_back(PendingAppEvent{event, wParam, dwParam, std::move(label)});
}

std::vector<BrewShell::PendingAppEvent> BrewShell::pop_pending_app_events() {
    std::vector<PendingAppEvent> events;
    events.swap(pending_app_events_);
    return events;
}

void BrewShell::queue_thread(uint32_t object, uint32_t entry, uint32_t pUser, uint32_t stackSize, std::string label) {
    pending_threads_.push_back(PendingThread{object, entry, pUser, stackSize, std::move(label)});
}

std::vector<BrewShell::PendingThread> BrewShell::pop_pending_threads() {
    std::vector<PendingThread> threads;
    threads.swap(pending_threads_);
    return threads;
}

void BrewShell::queue_thread_resume(uint32_t object) {
    if (object != 0 && object < 0xFF000000) {
        pending_thread_resumes_.push_back(object);
    }
}

std::vector<uint32_t> BrewShell::pop_pending_thread_resumes() {
    std::vector<uint32_t> resumes;
    resumes.swap(pending_thread_resumes_);
    return resumes;
}

void BrewShell::queue_signal_callback(uint32_t callback, uint32_t pUser, std::string label) {
    queue_signal_callback(callback, pUser, 0u, 0u, std::move(label));
}

void BrewShell::queue_signal_callback(uint32_t callback, uint32_t pUser, uint32_t arg1, std::string label) {
    queue_signal_callback(callback, pUser, arg1, 0u, std::move(label));
}

void BrewShell::queue_signal_callback(uint32_t callback, uint32_t pUser, uint32_t arg1, uint32_t arg2, std::string label) {
    if (callback != 0 && callback < 0xFF000000) {
        pending_signal_callbacks_.push_back(PendingSignalCallback{callback, pUser, arg1, arg2, std::move(label)});
    }
}

std::vector<BrewShell::PendingSignalCallback> BrewShell::pop_pending_signal_callbacks() {
    std::vector<PendingSignalCallback> callbacks;
    callbacks.swap(pending_signal_callbacks_);
    return callbacks;
}

void BrewShell::set_signal(addr_t signal_obj, std::string label) {
    if (signal_obj == 0 || signal_obj >= 0xFF000000) {
        if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr && label.find("hid") != std::string::npos) {
            printf("[INPUT_TRACE] set_signal ignored label=%s signal=0x%08x\n", label.c_str(), signal_obj);
        }
        return;
    }
    if (memory_.read_value(signal_obj + 12) != 0) {
        if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr && label.find("hid") != std::string::npos) {
            printf("[INPUT_TRACE] set_signal ignored disabled label=%s signal=0x%08x\n", label.c_str(), signal_obj);
        }
        return;
    }
    const uint32_t callback = memory_.read_value(signal_obj + 4);
    const uint32_t pUser = memory_.read_value(signal_obj + 8);
    if (callback == 0 || callback >= 0xFF000000) {
        memory_.write_value(signal_obj + 16, 1);
        if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr && label.find("hid") != std::string::npos) {
            printf("[INPUT_TRACE] set_signal pending label=%s signal=0x%08x callback=0x%08x\n",
                   label.c_str(),
                   signal_obj,
                   callback);
        }
        return;
    }
    memory_.write_value(signal_obj + 16, 0);
    if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr && label.find("hid") != std::string::npos) {
        printf("[INPUT_TRACE] set_signal callback label=%s signal=0x%08x callback=0x%08x pUser=0x%08x\n",
               label.c_str(),
               signal_obj,
               callback,
               pUser);
    }
    queue_signal_callback(callback, pUser, std::move(label));
}

bool BrewShell::consume_thread_yield_request() {
    bool requested = thread_yield_requested_;
    thread_yield_requested_ = false;
    return requested;
}

void BrewShell::request_thread_slice_yield() {
    thread_slice_yield_requested_ = true;
}

bool BrewShell::consume_thread_slice_yield_request() {
    bool requested = thread_slice_yield_requested_;
    thread_slice_yield_requested_ = false;
    return requested;
}

BrewMediaPCM* BrewShell::create_media_object(uint32_t clsId) {
    const char* type = media_type_name_for_clsid(clsId);
    if (type == nullptr) {
        return nullptr;
    }
    auto* media = new BrewMediaPCM(*this, memory_, type, clsId);
    media_instances_.push_back(media);
    printf("  Created IMedia(%s) object for CLSID 0x%08x\n", type, clsId);
    return media;
}

addr_t BrewShell::create_media_from_data(addr_t media_data_ptr) {
    constexpr uint32_t MMD_FILE_NAME = 0;
    constexpr uint32_t MMD_BUFFER = 1;

    if (media_data_ptr == 0 || media_data_ptr >= 0xFF000000) {
        return 0;
    }

    const uint32_t cls_data = memory_.read_value(media_data_ptr);
    const addr_t data_ptr = memory_.read_value(media_data_ptr + 4);
    const uint32_t data_size = memory_.read_value(media_data_ptr + 8);

    std::string mime;
    if (cls_data == MMD_BUFFER) {
        mime = detect_media_mime_from_buffer(memory_, data_ptr, data_size);
    } else if (cls_data == MMD_FILE_NAME && data_ptr != 0 && data_ptr < 0xFF000000) {
        mime = mime_from_extension(read_guest_text(data_ptr, 512));
    }

    const uint32_t cls = media_handler_for_mime(mime);
    if (cls == 0) {
        printf("  IMediaUtil media detect clsData=0x%08x data=0x%08x size=%u mime='%s' -> unsupported\n",
               cls_data, data_ptr, data_size, mime.c_str());
        return 0;
    }

    BrewMediaPCM* media = create_media_object(cls);
    if (media == nullptr) {
        return 0;
    }
    media->set_media_data_from_guest(media_data_ptr);
    printf("  IMediaUtil media detect clsData=0x%08x data=0x%08x size=%u mime='%s' -> 0x%08x\n",
           cls_data, data_ptr, data_size, mime.c_str(), media->get_object_ptr());
    return media->get_object_ptr();
}

void BrewShell::create_instance_internal(uint32_t clsId, uint32_t ppObj, CPU& cpu) {
    auto write_guest32 = [&](uint32_t addr, uint32_t val) {
        memory_.write_value(addr, val);
    };
    if (clsId == 0x01001003) { // AEECLSID_FILEMGR
        auto* file_mgr = new BrewFileMgr(*this, memory_, vfs_);
        file_mgr_instances_.push_back(file_mgr);
        write_guest32(ppObj, file_mgr->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01001002) { // AEECLSID_HEAP
        write_guest32(ppObj, heap_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01001001 || clsId == 0x010127d4) { // AEECLSID_DISPLAY / DISPLAY1
        write_guest32(ppObj, display_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x0106e415) { // AEECLSID_CMemCache1 / MemCache1
        write_guest32(ppObj, mem_cache_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01002001 || clsId == 0x0102fb92) { // AEECLSID_GRAPHICS / GRAPHICS_BREW
        write_guest32(ppObj, graphics_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01013a83) { // AEECLSID_3D / I3D
        write_guest32(ppObj, brew_3d_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x0101132f) { // AEECLSID_3DUTIL / I3DUtil
        write_guest32(ppObj, brew_3d_util_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x010113f6) { // AEECLSID_3DMODEL / I3DModel
        write_guest32(ppObj, brew_3d_model_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (is_font_clsid(clsId)) {
        write_guest32(ppObj, font_->create_instance(clsId));
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01001045 || clsId == 0x01001021 || clsId == 0x0100102c) { // AEECLSID_DIB / BITMAP / legacy DIB_20
        write_guest32(ppObj, display_->get_device_bitmap()->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01001029) { // AEECLSID_TRANSFORM / AEEIID_ITransform
        write_guest32(ppObj, display_->get_device_bitmap()->get_transform_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01004001 || clsId == 0x01004002) { // AEECLSID_WinBMP / NativeBMP, default IImage viewer
        auto image = std::make_shared<BrewImage>(*this, memory_, clsId == 0x01004001 ? "WinBMP" : "NativeBMP", nullptr, 0);
        const addr_t image_obj = image->get_object_ptr();
        images_.push_back(image);
        printf("  IShell_CreateInstance BMP viewer cls=0x%08x -> 0x%08x\n", clsId, image_obj);
        write_guest32(ppObj, image_obj);
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01004004 || clsId == 0x01030765) { // AEECLSID_CPNG / AEECLSID_CPNGBREW, default IImage viewer
        auto image = std::make_shared<BrewImage>(*this, memory_, clsId == 0x01004004 ? "CPNG" : "CPNGBREW", nullptr, 0);
        const addr_t image_obj = image->get_object_ptr();
        images_.push_back(image);
        printf("  IShell_CreateInstance CPNG viewer cls=0x%08x -> 0x%08x\n", clsId, image_obj);
        write_guest32(ppObj, image_obj);
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01030766) { // AEECLSID_PNGDecoderBREW / IImageDecoder + IForceFeed
        write_guest32(ppObj, png_decoder_->get_decoder_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01001008 || clsId == 0x0102c4e8) { // AEECLSID_DBMGR / Z-Wheel SQLite manager
        write_guest32(ppObj, database_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01002000) { // AEECLSID_SOUNDPLAYER
        write_guest32(ppObj, sound_player_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01005511) { // AEECLSID_MEDIAPCM
        auto* media = create_media_object(clsId);
        if (media == nullptr) {
            cpu.set_reg(REG_R0, 20);
            return;
        }
        write_guest32(ppObj, media->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01005501 || // AEECLSID_MediaMIDI
               clsId == 0x01005502 || // AEECLSID_MediaMP3
               clsId == 0x01005503 || // AEECLSID_MediaQCP
               clsId == 0x01005504 || // AEECLSID_MediaPMD
               clsId == 0x01005506 || // AEECLSID_MediaMIDIOutQCP
               clsId == 0x01005507 || // AEECLSID_MediaMPEG4
               clsId == 0x01005508 || // AEECLSID_MediaMMF
               clsId == 0x01005509 || // AEECLSID_MediaPHR
               clsId == 0x0100550a || // AEECLSID_MediaADPCM
               clsId == 0x0100550b || // AEECLSID_MediaAAC
               clsId == 0x0100550c || // AEECLSID_MediaIMelody
               clsId == 0x0100550e || // AEECLSID_MediaAMR
               clsId == 0x0100550f || // AEECLSID_MediaHVS
               clsId == 0x01005510 || // AEECLSID_MediaSAF
               clsId == 0x01005512 || // AEECLSID_MediaXMF
               clsId == 0x01005513 || // AEECLSID_MediaDLS
               clsId == 0x01005514 || // AEECLSID_MediaSVG
               clsId == 0x0108d5f5) { // AEECLSID_MediaWMARaw
        // Same IMedia ABI as MediaPCM. Create a fresh per-instance object so the
        // guest can use several independently (each with its own notify/state).
        // Unsupported decoders intentionally fail at Play() after preserving the
        // IMedia control/notify contract; this exposes real caller behavior
        // instead of hiding MIME routing behind unsupported CreateInstance.
        auto* media = create_media_object(clsId);
        if (media == nullptr) {
            cpu.set_reg(REG_R0, 20);
            return;
        }
        write_guest32(ppObj, media->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x0100550d) { // AEECLSID_MEDIAUTIL / IMediaUtil
        write_guest32(ppObj, media_util_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x0100100c) { // AEECLSID_MEMASTREAM / IMemAStream
        auto* stream = new BrewMemAStream(*this, memory_);
        mem_astream_instances_.push_back(stream);
        write_guest32(ppObj, stream->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x0100100f) { // AEECLSID_LICENSE / ILicense
        write_guest32(ppObj, license_obj_);
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01001014) { // AEECLSID_UNZIPSTREAM / IUnzipAStream
        write_guest32(ppObj, unzip_stream_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x0102cce1) { // AEECLSID_CipherFactory / ICipherFactory
        write_guest32(ppObj, cipher_->get_factory_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01005000) { // AEECLSID_Web / IWeb
        write_guest32(ppObj, web_obj_);
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x0103d8ec) { // AEECLSID_QEGL
        write_guest32(ppObj, egl_->get_qegl_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01014bc4) { // AEECLSID_EGL
        write_guest32(ppObj, egl_->get_egl_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01014bc3) { // AEECLSID_GL
        write_guest32(ppObj, gl_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x0106c411) { // AEECLSID_HID
        write_guest32(ppObj, hid_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (is_hash_clsid(clsId)) {
        write_guest32(ppObj, hash_->create_instance(clsId));
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x0100103c) { // AEECLSID_RANDOM
        write_guest32(ppObj, random_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01001058) { // AEECLSID_APPLETCTL
        write_guest32(ppObj, applet_ctl_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01003109 || clsId == 0x01003209) { // AEECLSID_TEXTCTL_10 / AEECLSID_TEXTCTL
        addr_t obj = malloc(0x10);
        memory_.write_value(obj, textctl_vtable_);
        TextCtlState state;
        state.text_buffer = malloc(512);
        memory_.write_value(state.text_buffer, static_cast<uint16_t>(0), EndianMemory::Halfword);
        textctl_states_[obj] = state;
        write_guest32(ppObj, obj);
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01003100 || // AEECLSID_MENUCTL
               clsId == 0x01003101 || // AEECLSID_SOFTKEYCTL
               clsId == 0x01003102 || // AEECLSID_LISTCTL
               clsId == 0x01003103) { // AEECLSID_ICONVIEWCTL
        // BREW 4 AEEClassIDs.h defines these as one AEECLSID_CONTROL family.
        // Zeemu's current BrewMenuCtl implements the shared IMenuCtl surface
        // used by traced title/menu flows; keep all family members out of the
        // generic dummy service until a target proves a split object is needed.
        write_guest32(ppObj, menu_ctl_->create_instance(clsId));
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01001056 || clsId == 0x0100100b) { // AEECLSID_SOUND / AEECLSID_SOUND_10
        write_guest32(ppObj, sound_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01001011) { // AEECLSID_SOURCEUTIL (ISourceUtil)
        write_guest32(ppObj, source_util_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x0100102e) { // AEECLSID_NET (INetMgr)
        write_guest32(ppObj, net_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x0100104f) { // AEECLSID_APPHISTORY (IAppHistory)
        printf("  IAppHistory CLSID 0x%08x -> BrewAppHistory\n", clsId);
        apphistory_->set_current_applet_cls(current_applet_cls_);
        write_guest32(ppObj, apphistory_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01028e35) { // Z-Wheel OEM config service (not in SDK)
        write_guest32(ppObj, zwheel_oem_->get_config_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01028e51) { // Z-Wheel OEM root form/input service (not in SDK)
        write_guest32(ppObj, zwheel_oem_->get_root_form_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01006c05 || clsId == 0x01006c02) { // Z-Wheel MCP/catalog helpers (not in SDK)
        write_guest32(ppObj, zwheel_oem_->get_mcp_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01011810) { // Z-Wheel telemetry/offline reporting service (not in SDK)
        write_guest32(ppObj, zwheel_oem_->get_telemetry_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (app_ui_->handles(clsId)) {
        printf("  AppUI CLSID 0x%08x -> %s\n", clsId, app_ui_->class_name(clsId));
        write_guest32(ppObj, app_ui_->create_instance(clsId));
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x010292c3) { // Private HI Corporation 3D service from imicro3d.mod.
        printf("  Private3D CLSID 0x%08x -> bundled 3D HLE\n", clsId);
        write_guest32(ppObj, micro_3d_->get_object_ptr());
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01079787) { // AEECLSID_FlashAMCPlayer
        write_guest32(ppObj, flash_->create_instance(clsId));
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x0101f0aa) {
        // Duke3D probes this first, then falls back to the bundled HI Corporation Micro3D class
        // 0x010292c3 when creation fails. Do not synthesize a dummy here, or the real service path is
        // skipped.
        printf("  Private3D probe CLSID 0x%08x unsupported -> fallback\n", clsId);
        write_guest32(ppObj, 0);
        cpu.set_reg(REG_R0, 1);
    } else if (is_generic_core_stub_clsid(clsId)) {
        printf("  [IShell_CreateInstance] not implemented yet CLSID=0x%08x ppObj=0x%08x\n", clsId, ppObj);
        write_guest32(ppObj, 0);
        cpu.set_reg(REG_R0, 1);
    } else if (clsId == 0x01041207) { // AEECLSID_SignalCBFactory
        write_guest32(ppObj, stub_signal_factory_obj_);
        cpu.set_reg(REG_R0, 0);
    } else if (clsId == 0x01001017) { // AEECLSID_THREAD
        write_guest32(ppObj, stub_thread_obj_);
        cpu.set_reg(REG_R0, 0);
    } else if (clsId != 0 && clsId == current_applet_cls_) {
        // The running app creating an instance of its own launched class: return
        // the main applet object. Keyed on the launched CLSID (set up front by
        // the app runner) instead of a hardcoded per-game list.
        applet_->set_current_class(clsId);
        write_guest32(ppObj, applet_->get_object_ptr());
        current_applet_obj_ = applet_->get_object_ptr();
        printf("  Created applet object for launched CLSID 0x%08x\n", clsId);
        cpu.set_reg(REG_R0, 0);
    } else {
        printf("  [IShell_CreateInstance] not implemented yet CLSID=0x%08x ppObj=0x%08x\n", clsId, ppObj);
        write_guest32(ppObj, 0);
        cpu.set_reg(REG_R0, 1);
    }
}

addr_t BrewShell::get_file_mgr_object_ptr() const {
    return file_mgr_ ? file_mgr_->get_object_ptr() : 0;
}

BrewFile* BrewShell::find_open_file(addr_t object_ptr) const {
    if (file_mgr_) {
        if (auto* file = file_mgr_->find_open_file(object_ptr)) {
            return file;
        }
    }
    for (auto* mgr : file_mgr_instances_) {
        if (!mgr) {
            continue;
        }
        if (auto* file = mgr->find_open_file(object_ptr)) {
            return file;
        }
    }
    return nullptr;
}

BrewMemAStream* BrewShell::find_mem_astream(addr_t object_ptr) const {
    for (auto* stream : mem_astream_instances_) {
        if (stream && stream->get_object_ptr() == object_ptr) {
            return stream;
        }
    }
    return nullptr;
}

bool BrewShell::draw_image_object(addr_t image_object, int x, int y) {
    for (const auto& image : images_) {
        if (image && image->get_object_ptr() == image_object) {
            image->draw_at(x, y);
            return true;
        }
    }
    return false;
}
