#ifndef ZEEMU_BREW_MEDIA_PCM_H_
#define ZEEMU_BREW_MEDIA_PCM_H_

#include "brew/BrewShell.h"
#include "cpu/memory/EndianMemory.h"
#include <cstdint>
#include <string>
#include <vector>

struct SDL_AudioStream;

class BrewMediaPCM : public BrewService {
public:
    // type_name identifies the concrete IMedia class (PCM/MP3/MIDI/ADPCM/...).
    // All share the IMedia ABI. PCM/WAV and traced SMF MIDI paths queue audio
    // through SDL; unsupported payloads still honor the async notify contract.
    explicit BrewMediaPCM(BrewShell& shell, EndianMemory& memory,
                          std::string type_name = "PCM",
                          uint32_t media_clsid = 0x01005511);
    ~BrewMediaPCM() override;

    addr_t get_object_ptr() const { return object_ptr_; }
    void set_media_data_from_guest(uint32_t media_data_ptr);
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    void setup_vtable();
    void read_media_data(uint32_t media_data_ptr);
    void load_file_media_data();
    void write_media_data(uint32_t media_data_ptr) const;
    bool queue_pcm_to_sdl();
    bool queue_mp3_to_sdl();
    bool queue_midi_to_sdl();
    // Deliver an asynchronous AEEMediaCmdNotify to the registered notify callback.
    void fire_notify(int nCmd, int nStatus);

    BrewShell& shell_;
    EndianMemory& memory_;
    std::string type_name_;
    uint32_t media_clsid_ = 0x01005511;
    addr_t object_ptr_ = 0;
    addr_t vtable_ptr_ = 0;
    addr_t cmd_notify_buf_ = 0; // guest scratch for AEEMediaCmdNotify

    uint32_t notify_fn_ = 0;
    uint32_t notify_user_ = 0;
    uint32_t media_cls_data_ = 0;
    uint32_t media_data_ptr_ = 0;
    uint32_t media_size_ = 0;
    uint32_t volume_ = 100;
    uint32_t state_ = 1; // MM_STATE_IDLE
    uint32_t total_time_ms_ = 0;
    SDL_AudioStream* audio_stream_ = nullptr;
    uint32_t audio_freq_ = 0;
    uint32_t audio_channels_ = 0;
    uint32_t audio_format_ = 0;
};

#endif
