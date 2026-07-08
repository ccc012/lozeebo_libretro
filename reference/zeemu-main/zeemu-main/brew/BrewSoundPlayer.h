#ifndef ZEEMU_BREW_SOUND_PLAYER_H_
#define ZEEMU_BREW_SOUND_PLAYER_H_

#include "brew/BrewShell.h"
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"
#include <string>
#include <vector>

struct SDL_AudioStream;

class BrewSoundPlayer : public BrewService {
public:
    BrewSoundPlayer(BrewShell& shell, EndianMemory& memory);

    addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    void setup_vtable();

    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_ = 0;
    addr_t vtable_ptr_ = 0;
    uint32_t notify_fn_ = 0;
    uint32_t notify_user_ = 0;
    uint32_t source_type_ = 0;
    uint32_t source_size_ = 0;
    uint32_t volume_ = 100;
    uint32_t total_time_ms_ = 0;
    uint32_t elapsed_time_ms_ = 0;
    uint32_t tempo_ = 0;
    uint32_t tune_ = 0;
    uint32_t sound_device_ = 0;
    uint32_t stream_ptr_ = 0;
    SDL_AudioStream* audio_stream_ = nullptr;
    uint32_t audio_freq_ = 0;
    uint32_t audio_channels_ = 0;
    uint32_t audio_format_ = 0;
    bool source_valid_ = false;
    bool playing_ = false;
    bool paused_ = false;
    std::string source_path_;
    std::vector<uint8_t> source_bytes_;

    bool set_source(uint32_t type, uint32_t data);
    bool queue_source_to_sdl();
    void write_cmd_data(uint32_t cmd_data_ptr, uint32_t value, bool is_volume = false);
    void write_info(uint32_t info_ptr) const;
};

#endif
