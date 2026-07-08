#include "brew/BrewImageDecoder.h"

#include "brew/BrewBitmap.h"
#include "third_party/stb_image.h"
#include "cpu/core/CPU.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace {
constexpr uint32_t kAEEIID_IImageDecoder = 0x01026e20u;
constexpr uint32_t kAEEIID_IForceFeed = 0x0101eb0bu;
constexpr uint32_t kAEECLSID_PNGDecoderBREW = 0x01030766u;
constexpr uint32_t kAEE_RO_COPY = 0x00CC0020u;
constexpr uint32_t kSUCCESS = 0u;
constexpr uint32_t kEFAILED = 1u;
constexpr uint32_t kECLASSNOTSUPPORT = 4u;
}

BrewImageDecoder::BrewImageDecoder(BrewShell& shell, EndianMemory& memory, std::string label)
    : shell_(shell), memory_(memory), label_(std::move(label)) {
    setup_vtables();
}

void BrewImageDecoder::setup_vtables() {
    decoder_vtable_ptr_ = shell_.malloc(32 * 4);
    decoder_object_ptr_ = shell_.malloc(4);
    memory_.write_value(decoder_object_ptr_, decoder_vtable_ptr_);

    const char* decoder_names[] = {
        "AddRef", "Release", "QueryInterface", "GetBitmap", "GetRop"
    };
    for (int i = 0; i < 5; ++i) {
        memory_.write_value(decoder_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("IImageDecoder_") + decoder_names[i], this));
    }
    for (int i = 5; i < 32; ++i) {
        memory_.write_value(decoder_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook("IImageDecoder_Ext_" + std::to_string(i), this));
    }

    forcefeed_vtable_ptr_ = shell_.malloc(5 * 4);
    forcefeed_object_ptr_ = shell_.malloc(4);
    memory_.write_value(forcefeed_object_ptr_, forcefeed_vtable_ptr_);

    const char* forcefeed_names[] = {
        "AddRef", "Release", "QueryInterface", "Write", "Reset"
    };
    for (int i = 0; i < 5; ++i) {
        memory_.write_value(forcefeed_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("IForceFeed_") + forcefeed_names[i], this));
    }
}

bool BrewImageDecoder::decode_if_needed() {
    if (!dirty_ && bitmap_) {
        return true;
    }
    if (encoded_.empty()) {
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(encoded_.data()),
                                            static_cast<int>(encoded_.size()),
                                            &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        printf("  [IImageDecoder] decode failed label='%s' bytes=%zu\n", label_.c_str(), encoded_.size());
        return false;
    }

    auto decoded = std::make_unique<BrewBitmap>(shell_, memory_, width, height, 16);
    const addr_t dst = decoded->get_buffer_ptr();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int i = (y * width + x) * 4;
            const uint8_t r = pixels[i + 0];
            const uint8_t g = pixels[i + 1];
            const uint8_t b = pixels[i + 2];
            const uint8_t a = pixels[i + 3];
            const uint16_t rgb565 = (a == 0)
                ? 0
                : static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            memory_.write_value(dst + static_cast<addr_t>((y * width + x) * 2), rgb565, EndianMemory::Halfword);
        }
    }
    stbi_image_free(pixels);

    printf("  [IImageDecoder] decoded '%s' %dx%d channels=%d bytes=%zu -> bitmap=0x%08x\n",
           label_.c_str(), width, height, channels, encoded_.size(), decoded->get_object_ptr());
    bitmap_ = std::move(decoded);
    dirty_ = false;
    return true;
}

void BrewImageDecoder::write_query_result(uint32_t iid, uint32_t pp, CPU& cpu) {
    uint32_t out = 0;
    if (iid == kAEEIID_IImageDecoder || iid == kAEECLSID_PNGDecoderBREW) {
        out = decoder_object_ptr_;
    } else if (iid == kAEEIID_IForceFeed) {
        out = forcefeed_object_ptr_;
    }
    if (pp && pp < 0xFF000000) {
        memory_.write_value(pp, out);
    }
    printf("  ImageDecoder_QueryInterface iid=0x%08x -> 0x%08x\n", iid, out);
    cpu.set_reg(REG_R0, out ? kSUCCESS : kECLASSNOTSUPPORT);
}

void BrewImageDecoder::handle_hook(const std::string& name, CPU& cpu) {
    const uint32_t r1 = cpu.get_reg(REG_R1);
    const uint32_t r2 = cpu.get_reg(REG_R2);
    if (name == "IImageDecoder_AddRef" || name == "IForceFeed_AddRef") {
        cpu.set_reg(REG_R0, ++refs_);
    } else if (name == "IImageDecoder_Release" || name == "IForceFeed_Release") {
        if (refs_ > 0) {
            --refs_;
        }
        cpu.set_reg(REG_R0, refs_);
    } else if (name == "IImageDecoder_QueryInterface" || name == "IForceFeed_QueryInterface") {
        write_query_result(r1, r2, cpu);
    } else if (name == "IForceFeed_Reset") {
        encoded_.clear();
        bitmap_.reset();
        dirty_ = false;
        printf("  IForceFeed_Reset '%s'\n", label_.c_str());
        cpu.set_reg(REG_R0, kSUCCESS);
    } else if (name == "IForceFeed_Write") {
        const uint32_t pBuf = r1;
        const int32_t cb = static_cast<int32_t>(r2);
        if (pBuf && pBuf < 0xFF000000 && cb > 0) {
            const size_t old_size = encoded_.size();
            encoded_.resize(old_size + static_cast<size_t>(cb));
            for (int32_t i = 0; i < cb; ++i) {
                encoded_[old_size + static_cast<size_t>(i)] =
                    static_cast<uint8_t>(memory_.read_value(pBuf + static_cast<uint32_t>(i), EndianMemory::Byte));
            }
            dirty_ = true;
            printf("  IForceFeed_Write '%s' chunk=%d total=%zu\n", label_.c_str(), cb, encoded_.size());
        } else if (pBuf == 0) {
            printf("  IForceFeed_Write '%s' end total=%zu\n", label_.c_str(), encoded_.size());
            decode_if_needed();
        }
        cpu.set_reg(REG_R0, kSUCCESS);
    } else if (name == "IImageDecoder_GetBitmap") {
        const uint32_t ppBitmap = r1;
        const bool ok = decode_if_needed();
        const uint32_t obj = (ok && bitmap_) ? bitmap_->get_object_ptr() : 0;
        if (ppBitmap && ppBitmap < 0xFF000000) {
            memory_.write_value(ppBitmap, obj);
        }
        printf("  IImageDecoder_GetBitmap '%s' -> 0x%08x\n", label_.c_str(), obj);
        cpu.set_reg(REG_R0, obj ? kSUCCESS : kEFAILED);
    } else if (name == "IImageDecoder_GetRop") {
        cpu.set_reg(REG_R0, kAEE_RO_COPY);
    } else if (name.rfind("IImageDecoder_Ext_", 0) == 0) {
        printf("  %s '%s' R1=0x%08x R2=0x%08x\n", name.c_str(), label_.c_str(), r1, r2);
        cpu.set_reg(REG_R0, kSUCCESS);
    } else {
        cpu.set_reg(REG_R0, kSUCCESS);
    }
}
