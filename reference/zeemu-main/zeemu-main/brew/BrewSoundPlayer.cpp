#include "brew/BrewSoundPlayer.h"
#include "brew/BrewAudio.h"
#include "cpu/core/CPU.h"
#include "vfs/VirtualFileSystem.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <string>

namespace {
constexpr uint32_t AEE_SUCCESS = 0;
constexpr uint32_t AEE_EFAILED = 1;
constexpr uint32_t SDT_NONE = 0;
constexpr uint32_t SDT_FILE = 1;
constexpr uint32_t SDT_BUFFER = 2;
}

BrewSoundPlayer::BrewSoundPlayer(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {
    setup_vtable();
}

void BrewSoundPlayer::setup_vtable() {
    vtable_ptr_ = shell_.malloc(19 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);

    auto add_method = [&](int index, const std::string& name) {
        addr_t hook_addr = shell_.add_hook("ISoundPlayer_" + name, this);
        memory_.write_value(vtable_ptr_ + (index * 4), hook_addr);
    };

    // AEESoundPlayer.h (deprecated): IBase + RegisterNotify, Set, Play, ... SetInfo, GetInfo.
    add_method(0, "AddRef");
    add_method(1, "Release");
    add_method(2, "RegisterNotify");
    add_method(3, "Set");
    add_method(4, "Play");
    add_method(5, "Stop");
    add_method(6, "Rewind");
    add_method(7, "FastForward");
    add_method(8, "Pause");
    add_method(9, "Resume");
    add_method(10, "SetTempo");
    add_method(11, "SetTune");
    add_method(12, "SetVolume");
    add_method(13, "GetVolume");
    add_method(14, "GetTotalTime");
    add_method(15, "SetSoundDevice");
    add_method(16, "SetStream");
    add_method(17, "SetInfo");
    add_method(18, "GetInfo");
}

bool BrewSoundPlayer::set_source(uint32_t type, uint32_t data) {
    source_type_ = type;
    source_path_.clear();
    source_bytes_.clear();
    source_size_ = 0;
    source_valid_ = false;
    total_time_ms_ = 0;
    elapsed_time_ms_ = 0;
    playing_ = false;
    paused_ = false;

    if (type == SDT_FILE && data != 0) {
        char path[512] = {};
        shell_.read_string(data, path, sizeof(path));
        source_path_ = path;

        std::string file_data;
        source_valid_ = shell_.get_vfs().read_file(source_path_, file_data, shell_.get_current_directory());
        source_size_ = static_cast<uint32_t>(file_data.size());
        source_bytes_.assign(file_data.begin(), file_data.end());

        // We do not decode audio yet. A stable non-zero duration keeps legacy clients that query
        // metadata from treating a valid file-backed source as empty.
        if (source_valid_) {
            total_time_ms_ = source_size_ > 0 ? 60000 : 1000;
        }
        return source_valid_;
    }

    if (type == SDT_BUFFER && data != 0) {
        // BREW Simulator only documented SDT_FILE support. Keep buffer sources accepted enough for
        // titles that probe the path, but do not claim a known duration.
        source_valid_ = true;
        return true;
    }

    return type == SDT_NONE;
}

bool BrewSoundPlayer::queue_source_to_sdl() {
    if (!source_valid_ || source_bytes_.empty()) {
        return false;
    }

    if (zeemu::audio::looks_like_mp3(source_bytes_)) {
        return zeemu::audio::queue_mp3_buffer_to_sdl(audio_stream_,
                                                     audio_freq_,
                                                     audio_channels_,
                                                     &audio_format_,
                                                     source_bytes_,
                                                     volume_,
                                                     "ISoundPlayer",
                                                     source_path_.c_str(),
                                                     total_time_ms_,
                                                     0.0f);
    }

    if (zeemu::audio::looks_like_wav(source_bytes_)) {
        return zeemu::audio::queue_wav_buffer_to_sdl(audio_stream_,
                                                     audio_freq_,
                                                     audio_channels_,
                                                     audio_format_,
                                                     source_bytes_,
                                                     volume_,
                                                     "ISoundPlayer",
                                                     source_path_.c_str(),
                                                     total_time_ms_,
                                                     0.0f);
    }

    if (zeemu::audio::looks_like_midi(source_bytes_)) {
        return zeemu::audio::queue_midi_buffer_to_sdl(audio_stream_,
                                                      audio_freq_,
                                                      audio_channels_,
                                                      audio_format_,
                                                      source_bytes_,
                                                      volume_,
                                                      "ISoundPlayer",
                                                      source_path_.c_str(),
                                                      total_time_ms_,
                                                      0.0f);
    }

    if (zeemu::audio::looks_like_qcp(source_bytes_) &&
        zeemu::audio::queue_qcp_buffer_to_sdl(audio_stream_,
                                             audio_freq_,
                                             audio_channels_,
                                             audio_format_,
                                             source_bytes_,
                                             volume_,
                                             "ISoundPlayer",
                                             source_path_.c_str(),
                                             total_time_ms_,
                                             0.0f)) {
        return true;
    }

    uint32_t qcp_payload = 0;
    uint32_t qcp_estimated_ms = 0;
    if (zeemu::audio::describe_qcp_buffer(source_bytes_, qcp_payload, qcp_estimated_ms)) {
        total_time_ms_ = qcp_estimated_ms;
        std::printf("  ISoundPlayer QCP: unsupported PureVoice decode payload=%u estimated=%u ms path='%s'\n",
                    qcp_payload,
                    total_time_ms_,
                    source_path_.c_str());
        return false;
    }

    return false;
}

void BrewSoundPlayer::write_cmd_data(uint32_t cmd_data_ptr, uint32_t value, bool is_volume) {
    if (cmd_data_ptr == 0) {
        return;
    }
    if (is_volume) {
        memory_.write_value(cmd_data_ptr, value, EndianMemory::Halfword);
    } else {
        memory_.write_value(cmd_data_ptr, value);
    }
}

void BrewSoundPlayer::write_info(uint32_t info_ptr) const {
    if (info_ptr == 0) {
        return;
    }
    memory_.write_value(info_ptr + 0, source_type_);
    memory_.write_value(info_ptr + 4, 0);
    memory_.write_value(info_ptr + 8, source_size_);
}

void BrewSoundPlayer::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);

    if (name == "ISoundPlayer_AddRef") {
        cpu.set_reg(REG_R0, r0);
    } else if (name == "ISoundPlayer_Release") {
        if (audio_stream_) {
            SDL_ClearAudioStream(audio_stream_);
        }
        playing_ = false;
        paused_ = false;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "ISoundPlayer_RegisterNotify") {
        notify_fn_ = r1;
        notify_user_ = r2;
        printf("  ISoundPlayer_RegisterNotify pfn=0x%08x pUser=0x%08x\n", notify_fn_, notify_user_);
        cpu.set_reg(REG_R0, AEE_SUCCESS);
    } else if (name == "ISoundPlayer_Set") {
        bool ok = set_source(r1, r2);
        printf("  ISoundPlayer_Set type=%u data=0x%08x path='%s' valid=%d size=%u\n",
               r1, r2, source_path_.c_str(), ok ? 1 : 0, source_size_);
        cpu.set_reg(REG_R0, AEE_SUCCESS);
    } else if (name == "ISoundPlayer_Play") {
        playing_ = source_valid_;
        paused_ = false;
        const bool audio_ok = queue_source_to_sdl();
        printf("  ISoundPlayer_Play path='%s' valid=%d size=%u notify=0x%08x audio=%d\n",
               source_path_.c_str(), source_valid_ ? 1 : 0, source_size_, notify_fn_, audio_ok ? 1 : 0);
        cpu.set_reg(REG_R0, source_valid_ ? AEE_SUCCESS : AEE_EFAILED);
    } else if (name == "ISoundPlayer_Stop") {
        if (audio_stream_) {
            SDL_ClearAudioStream(audio_stream_);
        }
        playing_ = false;
        paused_ = false;
        elapsed_time_ms_ = 0;
        printf("  ISoundPlayer_Stop\n");
        cpu.set_reg(REG_R0, AEE_SUCCESS);
    } else if (name == "ISoundPlayer_Pause") {
        paused_ = playing_;
        playing_ = false;
        printf("  ISoundPlayer_Pause elapsed=%u\n", elapsed_time_ms_);
        cpu.set_reg(REG_R0, AEE_SUCCESS);
    } else if (name == "ISoundPlayer_Resume") {
        playing_ = paused_ && source_valid_;
        paused_ = false;
        printf("  ISoundPlayer_Resume playing=%d\n", playing_ ? 1 : 0);
        cpu.set_reg(REG_R0, source_valid_ ? AEE_SUCCESS : AEE_EFAILED);
    } else if (name == "ISoundPlayer_Rewind") {
        elapsed_time_ms_ = 0;
        printf("  ISoundPlayer_Rewind\n");
        cpu.set_reg(REG_R0, AEE_SUCCESS);
    } else if (name == "ISoundPlayer_FastForward") {
        elapsed_time_ms_ = total_time_ms_;
        printf("  ISoundPlayer_FastForward total=%u\n", total_time_ms_);
        cpu.set_reg(REG_R0, AEE_SUCCESS);
    } else if (name == "ISoundPlayer_SetTempo") {
        tempo_ = r1;
        printf("  ISoundPlayer_SetTempo %u\n", tempo_);
        cpu.set_reg(REG_R0, AEE_SUCCESS);
    } else if (name == "ISoundPlayer_SetTune") {
        tune_ = r1;
        printf("  ISoundPlayer_SetTune %u\n", tune_);
        cpu.set_reg(REG_R0, AEE_SUCCESS);
    } else if (name == "ISoundPlayer_SetVolume") {
        volume_ = r1;
        printf("  ISoundPlayer_SetVolume %u\n", volume_);
        cpu.set_reg(REG_R0, AEE_SUCCESS);
    } else if (name == "ISoundPlayer_GetVolume") {
        write_cmd_data(r1, volume_, true);
        printf("  ISoundPlayer_GetVolume -> %u param=0x%08x\n", volume_, r1);
        cpu.set_reg(REG_R0, AEE_SUCCESS);
    } else if (name == "ISoundPlayer_GetTotalTime") {
        write_cmd_data(r1, total_time_ms_);
        printf("  ISoundPlayer_GetTotalTime -> %u param=0x%08x\n", total_time_ms_, r1);
        cpu.set_reg(REG_R0, source_valid_ ? AEE_SUCCESS : AEE_EFAILED);
    } else if (name == "ISoundPlayer_SetSoundDevice") {
        sound_device_ = r1;
        printf("  ISoundPlayer_SetSoundDevice device=%u earMute=%u micMute=%u\n", r1, r2, cpu.get_reg(REG_R3));
        cpu.set_reg(REG_R0, AEE_SUCCESS);
    } else if (name == "ISoundPlayer_SetStream") {
        stream_ptr_ = r1;
        source_type_ = SDT_BUFFER;
        source_valid_ = stream_ptr_ != 0;
        printf("  ISoundPlayer_SetStream stream=0x%08x\n", stream_ptr_);
        cpu.set_reg(REG_R0, AEE_SUCCESS);
    } else if (name == "ISoundPlayer_SetInfo") {
        if (r1 != 0) {
            uint32_t type = memory_.read_value(r1 + 0);
            uint32_t data = memory_.read_value(r1 + 4);
            bool ok = set_source(type, data);
            printf("  ISoundPlayer_SetInfo type=%u data=0x%08x path='%s' valid=%d size=%u\n",
                   type, data, source_path_.c_str(), ok ? 1 : 0, source_size_);
            cpu.set_reg(REG_R0, AEE_SUCCESS);
        } else {
            cpu.set_reg(REG_R0, AEE_EFAILED);
        }
    } else if (name == "ISoundPlayer_GetInfo") {
        write_info(r1);
        printf("  ISoundPlayer_GetInfo pInfo=0x%08x type=%u size=%u\n", r1, source_type_, source_size_);
        cpu.set_reg(REG_R0, AEE_SUCCESS);
    } else {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), r0, r1, r2, cpu.get_reg(REG_R3));
        cpu.set_reg(REG_R0, 0);
    }
}
