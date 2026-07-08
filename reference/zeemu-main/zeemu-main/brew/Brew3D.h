#ifndef ZEEMU_BREW_3D_H_
#define ZEEMU_BREW_3D_H_

#include "brew/BrewService.h"
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class BrewShell;

class Brew3D : public BrewService {
public:
    Brew3D(BrewShell& shell, EndianMemory& memory);
    addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;
private:
    void setup_vtable();
    void handle_set_state(CPU& cpu);
    void handle_get_state(CPU& cpu);
    void queue_frame_events();
    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_;
    addr_t vtable_ptr_;

    // AEE3DLight as used by tutori3d lighting.c: light.direction (AEE3DPoint
    // x,y,z) and light.color (r,g,b,a), plus a type the struct must carry so
    // I3D_SetLight(p, &light) knows which light to update (the sample
    // round-trips GetLight -> modify -> SetLight with only the struct).
    // Direction is Q14 per the sample comment "multiply ... to get a Q14 unit
    // vector as SetLight expects"; colors step in units of 1 (0..255 range).
    struct LightState {
        int32_t direction_x = 0;
        int32_t direction_y = 0;
        int32_t direction_z = 0;
        int32_t color_r = 0;
        int32_t color_g = 0;
        int32_t color_b = 0;
        int32_t color_a = 0;
    };

    // AEE3DMaterial per tutori3d lighting.c: color.r/g/b/a, shininess,
    // emissive (material.color.a exists; see lighting.c MATERIAL_ALPHA path).
    struct MaterialState {
        int32_t color_r = 0;
        int32_t color_g = 0;
        int32_t color_b = 0;
        int32_t color_a = 0;
        int32_t shininess = 0;
        int32_t emissive = 0;
    };

    LightState diffused_light_{};
    LightState specular_light_{};
    MaterialState material_{};
    uint32_t render_mode_ = 0;
    uint32_t lighting_mode_ = 0;
    uint32_t focal_length_ = 0;
    uint32_t view_depth_near_ = 0;
    uint32_t view_depth_far_ = 0;
    uint32_t current_texture_ = 0;
    uint32_t dest_bitmap_ = 0;
    addr_t event_callback_ = 0;
    addr_t event_user_data_ = 0;
    // Three guest-visible AEE3DEventNotify blocks reused every frame
    // (STARTED, COMPLETED, UPDATE_DISPLAY are queued together by StartFrame).
    addr_t event_blocks_[3] = {0, 0, 0};
    // I3D_Enable(cap, on) capability flags, keyed by raw cap value until the
    // cap enum (blending / perspective correction) is pinned down.
    std::unordered_map<uint32_t, bool> enable_state_;
};

class Brew3DUtil : public BrewService {
public:
    Brew3DUtil(BrewShell& shell, EndianMemory& memory);
    [[nodiscard]] addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;
private:
    void setup_vtable();
    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_;
    addr_t vtable_ptr_;
};

class Brew3DModel : public BrewService {
public:
    Brew3DModel(BrewShell& shell, EndianMemory& memory);
    addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;
private:
    void setup_vtable();
    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_;
    addr_t vtable_ptr_;
    
    // Model state
    struct TextureEntry {
        uint32_t type = 0;
        uint32_t sampling_mode = 0;
        uint32_t wrap_s = 0;
        uint32_t wrap_t = 0;
        uint32_t border_color_index = 0;
        addr_t image_ptr = 0;
    };
    
    std::string model_path_;
    std::vector<TextureEntry> textures_;
    bool loaded_ = false;
    // Last I3DModel_SetSegmentMVT call: guest matrix pointer + segment index
    // (-1 selects all segments, observed in the tutori3d trace as R2=0xffffffff).
    addr_t segment_mvt_matrix_ = 0;
    int32_t segment_mvt_index_ = -1;
};

class BrewEGL : public BrewService {
public:
    BrewEGL(BrewShell& shell, EndianMemory& memory);
    addr_t get_qegl_object_ptr() const { return qegl_object_ptr_; }
    addr_t get_egl_object_ptr() const { return egl_object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    void setup_vtables();
    void show_visible_marker(const char* label);
    addr_t make_proc_stub(const std::string& name);
    struct SurfaceScaleRect {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t width = 640;
        uint32_t height = 480;
    };
    struct SurfaceState {
        uint32_t width = 640;
        uint32_t height = 480;
        uint32_t swap_behavior = 0x3095; // EGL_BUFFER_DESTROYED
        bool scale_enabled = false;
        bool has_scale_src = false;
        bool has_scale_dst = false;
        SurfaceScaleRect scale_src{};
        SurfaceScaleRect scale_dst{};
    };
    SurfaceState& surface_state(uint32_t surface);
    uint32_t create_window_surface_state();
    uint32_t query_surface_value(uint32_t surface, uint32_t attrib);
    void set_surface_swap_behavior(uint32_t surface, uint32_t value);
    bool read_surface_scale_rect(uint32_t addr, SurfaceScaleRect& rect);
    void write_surface_scale_rect(uint32_t addr, const SurfaceScaleRect& rect);
    bool set_surface_scale_state(uint32_t surface, uint32_t src_addr, uint32_t dst_addr);
    void apply_surface_scale_to_presenter(uint32_t surface);
    void write_surface_scale_caps(uint32_t addr);
    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t qegl_object_ptr_ = 0;
    addr_t qegl_vtable_ptr_ = 0;
    addr_t egl_object_ptr_ = 0;
    addr_t egl_vtable_ptr_ = 0;
    addr_t gles_object_ptr_ = 0;
    addr_t gles_vtable_ptr_ = 0;
    addr_t gles10_ext_object_ptr_ = 0;
    addr_t gles10_ext_vtable_ptr_ = 0;
    addr_t gles11_ext_object_ptr_ = 0;
    addr_t gles11_ext_vtable_ptr_ = 0;
    addr_t gles11_extpak_object_ptr_ = 0;
    addr_t gles11_extpak_vtable_ptr_ = 0;
    addr_t gles_imageon_ext_object_ptr_ = 0;
    addr_t gles_imageon_ext_vtable_ptr_ = 0;
    addr_t surface_manip_object_ptr_ = 0;
    addr_t surface_manip_vtable_ptr_ = 0;
    addr_t extensions_ptr_ = 0;
    addr_t vendor_ptr_ = 0;
    addr_t renderer_ptr_ = 0;
    addr_t version_ptr_ = 0;
    uint32_t next_texture_id_ = 1;
    uint32_t next_surface_id_ = 2;
    uint32_t current_display_ = 0;
    uint32_t current_draw_surface_ = 0;
    uint32_t current_read_surface_ = 0;
    uint32_t current_context_ = 0;
    std::unordered_map<uint32_t, SurfaceState> surfaces_;
};

class BrewGL : public BrewService {
public:
    BrewGL(BrewShell& shell, EndianMemory& memory);
    addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    struct TextureInfo {
        uint32_t target = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t format = 0;
        uint32_t type = 0;
        bool compressed = false;
    };

    void setup_vtable();
    void paint_visible_frame(const char* label);
    uint16_t clear_color_565_from_fixed(uint32_t r, uint32_t g, uint32_t b) const;
    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_ = 0;
    addr_t vtable_ptr_ = 0;
    addr_t extensions_ptr_ = 0;
    addr_t vendor_ptr_ = 0;
    addr_t renderer_ptr_ = 0;
    addr_t version_ptr_ = 0;
    uint32_t next_texture_id_ = 1;
    uint32_t bound_texture_2d_ = 0;
    uint32_t pending_preserved_texture_2d_ = 0;
    uint32_t last_bound_texture_2d_ = 0;
    std::unordered_set<uint32_t> texture_names_;
    std::unordered_map<uint32_t, TextureInfo> textures_;
    uint16_t pending_clear_color_ = 0x0000;
    bool pending_clear_color_valid_ = false;
    uint32_t texture_unbind_log_count_ = 0;
    uint32_t texture_delete_log_count_ = 0;
};

#endif
