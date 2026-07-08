#ifndef ZEEMU_BREW_SHELL_H_
#define ZEEMU_BREW_SHELL_H_

#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"
#include "brew/BrewService.h"
#include <map>
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <functional>

class VirtualFileSystem;
class VirtualMemory;
namespace zeemu::gfx { class FramePresenter; }
class BrewFile;
class BrewFileMgr;
class BrewDisplay;
class BrewFont;
class BrewImage;
class BrewImageDecoder;
class BrewDatabase;
class BrewSoundPlayer;
class BrewSound;
class BrewMediaPCM;
class BrewMediaUtil;
class BrewMemAStream;
class BrewMemCache;
class BrewUnzipStream;
class BrewHeap;
class BrewGraphics;
class Brew3D;
class Brew3DUtil;
class Brew3DModel;
class BrewEGL;
class BrewGL;
class BrewHID;
class BrewHash;
class BrewCipher;
class BrewRandom;
class BrewApplet;
class BrewAppletCtl;
class BrewAppHistory;
class BrewMenuCtl;
class BrewSourceUtil;
class BrewNet;
class BrewZWheelOem;
class BrewAppUI;
class BrewMicro3D;
class BrewFlash;

class BrewShell {
public:
    explicit BrewShell(EndianMemory& memory, VirtualFileSystem& vfs, int display_width = 640, int display_height = 480);

    addr_t get_shell_ptr() const { return shell_ptr_; }
    addr_t get_object_ptr() const { return object_ptr_; }
    addr_t get_vtable_ptr() const { return vtable_ptr_; }
    addr_t get_dummy_module_ptr() const { return dummy_module_ptr_; }

    addr_t malloc(uint32_t size, bool zero = true);
    addr_t realloc_block(addr_t old_ptr, uint32_t size, bool zero_extra = true);
    uint32_t allocation_size(addr_t ptr) const;
    uint32_t heap_used_bytes() const;
    addr_t add_hook(const std::string& name, BrewService* service = nullptr);

    // Dispatcher for host hooks
    void handle_hook(uint32_t hook_id, class CPU& cpu);
    void read_string(addr_t addr, char* buf, size_t max_len);
    std::string read_guest_text(addr_t addr, size_t max_len = 4096);

    std::string format_guest(addr_t fmt_ptr, class CPU& cpu, int start_reg = 1, bool is_va_list = false, addr_t va_ptr = 0);

    VirtualFileSystem& get_vfs() { return vfs_; }
    void set_presenter(zeemu::gfx::FramePresenter* presenter) { presenter_ = presenter; }
    zeemu::gfx::FramePresenter* get_presenter() const { return presenter_; }
    int get_display_width() const { return display_width_; }
    int get_display_height() const { return display_height_; }
    BrewDisplay* get_display() { return display_; }
    BrewFont* get_font() { return font_; }
    BrewFileMgr* get_file_mgr() { return file_mgr_; }
    BrewFile* find_open_file(addr_t object_ptr) const;
    BrewMemAStream* find_mem_astream(addr_t object_ptr) const;
    addr_t create_media_from_data(addr_t media_data_ptr);
    addr_t get_file_mgr_object_ptr() const;
    bool draw_image_object(addr_t image_object, int x, int y);
    uint32_t get_current_applet_cls() const { return current_applet_cls_; }
    void set_current_applet_cls(uint32_t cls) { current_applet_cls_ = cls; }
    addr_t get_applet_object_ptr() const { return current_applet_obj_; }
    void set_applet_object_ptr(addr_t obj) { current_applet_obj_ = obj; }
    void set_pending_applet_output_ptr(addr_t ptr) { pending_applet_output_ptr_ = ptr; }

    // Timer management
    struct Timer {
        uint32_t pfn;
        uint32_t pUser;
        uint64_t expire_ms;
        uint32_t callback_addr;
    };
    struct TimerCallbackBinding {
        uint32_t pfn;
        uint32_t pUser;
    };
    struct PendingAppEvent {
        uint32_t event;
        uint32_t wParam;
        uint32_t dwParam;
        std::string label;
    };
    struct PendingThread {
        uint32_t object;
        uint32_t entry;
        uint32_t pUser;
        uint32_t stackSize;
        std::string label;
    };
    struct PendingSignalCallback {
        uint32_t callback;
        uint32_t pUser;
        uint32_t arg1 = 0; // passed as R1 to the guest callback (e.g. struct ptr)
        uint32_t arg2 = 0; // passed as R2 to the guest callback when needed
        std::string label;
    };
    void add_timer(uint32_t pfn, uint32_t pUser, uint32_t delay_ms, uint32_t callback_addr = 0);
    void cancel_timer(uint32_t pfn, uint32_t pUser);
    std::vector<Timer> pop_expired_timers(uint64_t now_ms);
    // ISHELL_SetAlarm/CancelAlarm: app-level one-shot alarms that deliver
    // EVT_ALARM (wParam = nID) to the applet after delay_ms. Unlike timers these
    // do not call a guest callback; they post an app event. Expired alarms are
    // fired from pop_expired_timers (which queues the app event).
    void set_alarm(uint32_t cls_app, uint32_t alarm_id, uint32_t delay_ms);
    void cancel_alarm(uint32_t cls_app, uint32_t alarm_id);
    void queue_app_event(uint32_t event, uint32_t wParam, uint32_t dwParam, std::string label);
    std::vector<PendingAppEvent> pop_pending_app_events();
    void queue_thread(uint32_t object, uint32_t entry, uint32_t pUser, uint32_t stackSize, std::string label);
    std::vector<PendingThread> pop_pending_threads();
    void queue_thread_resume(uint32_t object);
    std::vector<uint32_t> pop_pending_thread_resumes();
    void queue_signal_callback(uint32_t callback, uint32_t pUser, std::string label);
    void queue_signal_callback(uint32_t callback, uint32_t pUser, uint32_t arg1, std::string label);
    void queue_signal_callback(uint32_t callback, uint32_t pUser, uint32_t arg1, uint32_t arg2, std::string label);
    std::vector<PendingSignalCallback> pop_pending_signal_callbacks();
    void set_signal(addr_t signal_obj, std::string label);
    bool consume_thread_yield_request();
    void request_thread_slice_yield();
    bool consume_thread_slice_yield_request();

    void set_current_directory(const std::string& cd) { current_directory_ = cd; }
    const std::string& get_current_directory() const { return current_directory_; }
    uint64_t uptime_ms() const;
    void set_uptime_ms(uint64_t value);
    uint64_t next_timer_expiration_ms() const;

    void push_hid_event(uint32_t uid, bool down, uint32_t device_index = 0);
    void push_hid_keyboard_event(uint32_t key_id, bool down);
    void set_hid_axis(uint32_t uid, uint32_t value, uint32_t device_index = 0);
    void set_hid_rumble_callback(std::function<void(uint32_t, uint32_t, uint32_t)> callback);
    bool hid_default_key_events_enabled(uint32_t device_index = 0) const;

    void set_virtual_memory(VirtualMemory* vm) { virtual_memory_ = vm; }
    VirtualMemory* get_virtual_memory() const { return virtual_memory_; }

    void set_suppress_dbgprintf(bool v) { suppress_dbgprintf_ = v; }
    bool suppress_dbgprintf() const { return suppress_dbgprintf_; }

private:
    struct Hook;

    void setup_vtable();
    void create_instance_internal(uint32_t clsId, uint32_t ppObj, class CPU& cpu);
    BrewMediaPCM* create_media_object(uint32_t clsId);
    bool handle_aee_helper_hook(const Hook& hook, class CPU& cpu, int call_idx);

    EndianMemory& memory_;
    VirtualMemory* virtual_memory_ = nullptr;
    VirtualFileSystem& vfs_;
    zeemu::gfx::FramePresenter* presenter_ = nullptr;
    std::string current_directory_ = "fs:/";
    BrewFileMgr* file_mgr_;
    BrewDisplay* display_;
    BrewFont* font_;
    BrewImage* image_;
    BrewImageDecoder* png_decoder_;
    BrewDatabase* database_;
    BrewSoundPlayer* sound_player_;
    BrewSound* sound_;
    // ISHELL_CreateInstance(AEECLSID_FILEMGR) returns independent IFileMgr
    // objects. Keep the primary manager for legacy applet slots, and own extra
    // instances created by guests that expect separate enumeration/error state.
    std::vector<BrewFileMgr*> file_mgr_instances_;
    BrewMediaPCM* media_pcm_;
    // Per-instance IMedia objects created on demand for the various audio media
    // classes (MIDI/ADPCM/MP3/PCM). The guest creates several and uses them
    // independently, so each needs its own notify/state. Owned here.
    std::vector<BrewMediaPCM*> media_instances_;
    BrewMediaUtil* media_util_;
    BrewMemCache* mem_cache_;
    std::vector<BrewMemAStream*> mem_astream_instances_;
    BrewUnzipStream* unzip_stream_;
    BrewHash* hash_;
    BrewCipher* cipher_;
    BrewHeap* heap_;
    BrewGraphics* graphics_;
    BrewApplet* applet_;
    BrewAppletCtl* applet_ctl_;
    BrewEGL* egl_;
    BrewGL* gl_;
    BrewHID* hid_;
    BrewRandom* random_;
    BrewAppHistory* apphistory_;
    BrewMenuCtl* menu_ctl_;
    BrewSourceUtil* source_util_;
    BrewNet* net_;
    BrewZWheelOem* zwheel_oem_;
    BrewAppUI* app_ui_;
    BrewMicro3D* micro_3d_;
    BrewFlash* flash_;
    Brew3D* brew_3d_;
    Brew3DUtil* brew_3d_util_;
    Brew3DModel* brew_3d_model_;
    addr_t shell_ptr_;
    addr_t object_ptr_;
    addr_t vtable_ptr_;
    addr_t dummy_module_ptr_;
    addr_t dummy_module_vtable_;
    addr_t hooks_base_ = 0xEE000000;
    uint32_t next_hook_id_ = 0;
    uint32_t current_applet_cls_ = 0;
    addr_t current_applet_obj_ = 0;
    addr_t pending_applet_output_ptr_ = 0;
    int display_width_ = 640;
    int display_height_ = 480;
    uint64_t time_origin_ms_ = 0;
    uint64_t current_uptime_ms_ = 0;
    bool suppress_dbgprintf_ = false;
    bool thread_yield_requested_ = false;
    bool thread_slice_yield_requested_ = false;

    struct Hook {
        std::string name;
        addr_t address;
        BrewService* service;
    };
    std::vector<Hook> hooks_;

    // Simple heap management for guest
    addr_t heap_base_ = 0x50000000;
    addr_t heap_next_ = 0x50000000;
    std::map<addr_t, uint32_t> heap_alloc_sizes_;

    // Last-resort COM objects. Unknown services must fail/log before reaching
    // these; non-IBase slots are noisy if an old path still calls them.
    addr_t stub_com_obj_ = 0;
    addr_t stub_signal_factory_obj_ = 0;
    addr_t stub_signal_obj_ = 0;
    addr_t signal_vtable_ = 0;
    addr_t stub_thread_obj_ = 0;
    addr_t thread_resume_cb_ = 0;
    addr_t license_obj_ = 0;
    addr_t web_obj_ = 0;
    addr_t textctl_vtable_ = 0;
    addr_t stub_image_obj_ = 0;
    addr_t stub_sound_obj_ = 0;
    addr_t stub_apphistory_obj_ = 0;

    struct TextCtlState {
        bool active = false;
        uint32_t properties = 0;
        uint32_t properties_ex = 0;
        uint32_t input_mode = 0;
        uint32_t cursor = 0;
        uint32_t selection = 0;
        uint32_t max_chars = 255;
        addr_t text_buffer = 0;
        uint32_t text_chars = 0;
        int16_t rect_x = 0;
        int16_t rect_y = 0;
        int16_t rect_dx = 0;
        int16_t rect_dy = 0;
    };
    std::map<addr_t, TextCtlState> textctl_states_;

    struct PendingAlarm {
        uint32_t cls_app;
        uint32_t alarm_id;
        uint64_t expire_ms;
    };
    std::vector<Timer> pending_timers_;
    std::vector<PendingAlarm> pending_alarms_;
    std::map<uint32_t, TimerCallbackBinding> timer_callback_bindings_;
    std::vector<PendingAppEvent> pending_app_events_;
    std::vector<PendingThread> pending_threads_;
    std::vector<uint32_t> pending_thread_resumes_;
    std::vector<PendingSignalCallback> pending_signal_callbacks_;
    std::vector<std::shared_ptr<BrewImage>> images_;
};

#endif
