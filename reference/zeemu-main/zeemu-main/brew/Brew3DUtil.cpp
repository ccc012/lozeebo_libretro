#include "brew/Brew3D.h"
#include "brew/Brew3DCommon.h"
#include "brew/BrewShell.h"
#include "cpu/core/CPU.h"
#include <cmath>
#include <cstdio>
#include <string>
Brew3DUtil::Brew3DUtil(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory)
{
    setup_vtable();
}

void Brew3DUtil::setup_vtable() {
    vtable_ptr_ = shell_.malloc(32 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);
    // Slot evidence (tutori3d trace + sample call sites):
    // - Fn3: I3DUtil_SetIdentityMatrix(p, &mtx) -- model.c:197.
    // - Fn4: I3DUtil_MatrixMultiply(p, &dst, &src) -- result lands in the
    //   first matrix (every sample call site passes two matrices and reuses
    //   the first). Trace anomaly: src arrives in R3, R2 holds scratch.
    // - Fn5: I3DUtil_GetUnitVector(p, &src, &dst) -- lighting.c:402 (Q12 out).
    // - Fn6: I3DUtil_SetTranslationMatrix(p, &vec, &mtx) -- tutori3d.c:1405.
    // - Fn10/Fn11: I3DUtil_sin / I3DUtil_cos -- model.c:734-741 sphere
    //   generation; trace arg/order correlation gives sin=Fn10, cos=Fn11
    //   (3 sin + 2 cos per vertex; first iteration cos(0), sin(0x155),
    //   cos(0x155), sin(0), sin(0x155)). Angle is Q12 with 4096 = full turn,
    //   return is Q12 (y = (cos * MODEL_SCALE/1000) >> 2 reaching MODEL_SCALE).
    static const char* kNames[32] = {
        "I3DUtil_AddRef", "I3DUtil_Release", "I3DUtil_QueryInterface",
        "I3DUtil_SetIdentityMatrix", "I3DUtil_MatrixMultiply",
        "I3DUtil_GetUnitVector", "I3DUtil_SetTranslationMatrix",
        nullptr, nullptr, nullptr,
        "I3DUtil_sin", "I3DUtil_cos",
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    };
    for (int i = 0; i < 32; ++i) {
        std::string name = kNames[i] ? std::string(kNames[i])
                                     : "IBrew3DUtil_Fn" + std::to_string(i);
        memory_.write_value(vtable_ptr_ + static_cast<uint32_t>(i * 4), shell_.add_hook(name, this));
    }
}

void Brew3DUtil::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    uint32_t r3 = cpu.get_reg(REG_R3);

    if (name == "I3DUtil_AddRef") {
        cpu.set_reg(REG_R0, r0);
    } else if (name == "I3DUtil_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "I3DUtil_QueryInterface") {
        uint32_t cls = r1;
        uint32_t pp = r2;
        if (cls == 0x0101132f) {
            if (i3d_guest_ptr(pp)) memory_.write_value(pp, object_ptr_);
            cpu.set_reg(REG_R0, 0); // SUCCESS
        } else {
            if (i3d_guest_ptr(pp)) memory_.write_value(pp, 0u);
            cpu.set_reg(REG_R0, 3); // ECLASSNOTSUPPORT
        }
    } else if (name == "I3DUtil_SetIdentityMatrix") {
        // I3DUtil_SetIdentityMatrix(p, &mtx). AEE3DTransformMatrix is modeled
        // as 3 rows of (m_0, m_1, m_2, t) int32 values with translation at
        // offsets 12/28/44. The element Q-format is not pinned by the sample
        // (only m00/m11/m22 are touched, scale-invariantly); Q16.16 is used
        // consistently across SetIdentity/MatrixMultiply so HLE-internal math
        // stays coherent. Revisit when I3DUtil_GetRotateMatrix is reached.
        if (i3d_guest_ptr(r1)) {
            static const uint32_t kIdentity[12] = {
                0x00010000, 0, 0, 0,
                0, 0x00010000, 0, 0,
                0, 0, 0x00010000, 0,
            };
            for (int i = 0; i < 12; ++i) {
                memory_.write_value(r1 + static_cast<uint32_t>(i * 4), kIdentity[i]);
            }
            if (i3d_trace_enabled()) printf("  I3DUtil_SetIdentityMatrix mtx=0x%08x\n", r1);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "I3DUtil_MatrixMultiply") {
        // I3DUtil_MatrixMultiply(p, &dst, &src): every tutori3d call site
        // passes two matrices and keeps using the first as the result
        // (model.c:205, tutori3d.c:1547/1690/3040, transform.c:436), so the
        // product lands in-place in the first matrix. Trace anomaly: the src
        // matrix pointer arrives in R3 while R2 holds a stale hook address
        // (run log 2026-06-10); read src from R3, fall back to R2 if R3 is
        // not a guest pointer.
        uint32_t dst = r1;
        uint32_t src = i3d_guest_ptr(r3) ? r3 : r2;
        if (i3d_guest_ptr(dst) && i3d_guest_ptr(src)) {
            int32_t a[12];
            int32_t b[12];
            for (int i = 0; i < 12; ++i) {
                a[i] = static_cast<int32_t>(memory_.read_value(dst + static_cast<uint32_t>(i * 4)));
                b[i] = static_cast<int32_t>(memory_.read_value(src + static_cast<uint32_t>(i * 4)));
            }
            auto q16mul = [](int32_t x, int32_t y) -> int32_t {
                return static_cast<int32_t>((static_cast<int64_t>(x) * static_cast<int64_t>(y)) >> 16);
            };
            // rows of a: {a[0..3]}, {a[4..7]}, {a[8..11]}; column t composes
            // as dst_t = A*b_t + a_t (affine 3x4 compose).
            int32_t out[12];
            for (int row = 0; row < 3; ++row) {
                const int32_t* ar = &a[row * 4];
                out[row * 4 + 0] = q16mul(ar[0], b[0]) + q16mul(ar[1], b[4]) + q16mul(ar[2], b[8]);
                out[row * 4 + 1] = q16mul(ar[0], b[1]) + q16mul(ar[1], b[5]) + q16mul(ar[2], b[9]);
                out[row * 4 + 2] = q16mul(ar[0], b[2]) + q16mul(ar[1], b[6]) + q16mul(ar[2], b[10]);
                out[row * 4 + 3] = q16mul(ar[0], b[3]) + q16mul(ar[1], b[7]) + q16mul(ar[2], b[11]) + ar[3];
            }
            for (int i = 0; i < 12; ++i) {
                memory_.write_value(dst + static_cast<uint32_t>(i * 4), static_cast<uint32_t>(out[i]));
            }
            if (i3d_trace_enabled()) printf("  I3DUtil_MatrixMultiply dst=0x%08x src=0x%08x\n", dst, src);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "I3DUtil_GetUnitVector") {
        // I3DUtil_GetUnitVector(p, &src, &dst): "GetUnitVector returns a Q12
        // unit vector" (lighting.c:400). Normalization is scale-invariant, so
        // the src Q-format does not matter.
        if (i3d_guest_ptr(r1) && i3d_guest_ptr(r2)) {
            int32_t x = static_cast<int32_t>(memory_.read_value(r1 + 0));
            int32_t y = static_cast<int32_t>(memory_.read_value(r1 + 4));
            int32_t z = static_cast<int32_t>(memory_.read_value(r1 + 8));
            double dx = static_cast<double>(x);
            double dy = static_cast<double>(y);
            double dz = static_cast<double>(z);
            double len = sqrt(dx * dx + dy * dy + dz * dz);
            int32_t ux = 0, uy = 0, uz = 0;
            if (len > 0.0) {
                ux = static_cast<int32_t>((dx / len) * 4096.0);
                uy = static_cast<int32_t>((dy / len) * 4096.0);
                uz = static_cast<int32_t>((dz / len) * 4096.0);
            }
            memory_.write_value(r2 + 0, static_cast<uint32_t>(ux));
            memory_.write_value(r2 + 4, static_cast<uint32_t>(uy));
            memory_.write_value(r2 + 8, static_cast<uint32_t>(uz));
            if (i3d_trace_enabled()) {
                printf("  I3DUtil_GetUnitVector src=(%d,%d,%d) dst=(%d,%d,%d)\n", x, y, z, ux, uy, uz);
            }
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "I3DUtil_SetTranslationMatrix") {
        // I3DUtil_SetTranslationMatrix(p, &vec, &mtx): writes the translation
        // column (offsets 12/28/44), preserving the rotation part. Sample call
        // sites run SetIdentityMatrix first (tutori3d.c:1397+1405), so either
        // merge or full-overwrite semantics agree there.
        if (i3d_guest_ptr(r1) && i3d_guest_ptr(r2)) {
            uint32_t tx = memory_.read_value(r1 + 0);
            uint32_t ty = memory_.read_value(r1 + 4);
            uint32_t tz = memory_.read_value(r1 + 8);
            memory_.write_value(r2 + 12, tx);
            memory_.write_value(r2 + 28, ty);
            memory_.write_value(r2 + 44, tz);
            if (i3d_trace_enabled()) {
                printf("  I3DUtil_SetTranslationMatrix t=(%d,%d,%d) mtx=0x%08x\n",
                       static_cast<int32_t>(tx), static_cast<int32_t>(ty), static_cast<int32_t>(tz), r2);
            }
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "I3DUtil_sin" || name == "I3DUtil_cos") {
        // Q12 trig: angle in R1 with 4096 = full turn, returns Q12 (proven by
        // the sphere generator in model.c:734-741, where
        // y = (cos(phi) * MODEL_SCALE/1000) >> 2 reaches MODEL_SCALE).
        int32_t angle = static_cast<int32_t>(r1);
        double radians = (static_cast<double>(angle) / 4096.0) * 2.0 * 3.14159265358979323846;
        double v = (name == "I3DUtil_sin") ? std::sin(radians) : std::cos(radians);
        int32_t q12 = static_cast<int32_t>(std::lround(v * 4096.0));
        if (i3d_trace_enabled()) {
            printf("  %s angle=%d -> %d\n", name.c_str(), angle, q12);
        }
        cpu.set_reg(REG_R0, static_cast<uint32_t>(q12));
    } else {
        if (i3d_log_unknown(name)) {
            printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
                   name.c_str(), r0, r1, r2, r3);
        }
        cpu.set_reg(REG_R0, 0);
    }
}

