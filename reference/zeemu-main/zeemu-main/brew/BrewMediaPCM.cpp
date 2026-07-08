#include "brew/BrewMediaPCM.h"
#include "brew/BrewAudio.h"
#include "cpu/core/CPU.h"
#include "vfs/VirtualFileSystem.h"
#define TML_IMPLEMENTATION
#include "third_party/tml.h"
#define TSF_IMPLEMENTATION
#include "third_party/tsf.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

namespace {
constexpr uint32_t AEECLSID_MEDIAPCM = 0x01005511;
constexpr uint32_t AEEIID_IMEDIA = 0x01005500;

constexpr uint32_t MM_PARM_MEDIA_DATA = 1;
constexpr uint32_t MM_PARM_VOLUME = 4;
constexpr uint32_t MM_PARM_TICK_TIME = 9;
constexpr uint32_t MM_PARM_CLSID = 13;
constexpr uint32_t MM_PARM_CAPS = 14;
constexpr uint32_t MM_PARM_CHANNEL_SHARE = 16;
constexpr uint32_t MM_CAPS_AUDIO = 0x00000001;

constexpr uint32_t MM_STATE_IDLE = 1;
constexpr uint32_t MM_STATE_READY = 2;
constexpr uint32_t MM_STATE_PLAY = 3;
constexpr uint32_t MM_STATE_PLAY_PAUSE = 5;

constexpr uint32_t MMD_FILE_NAME = 0;
constexpr uint32_t MMD_BUFFER = 1;

// AEEIMedia.h command/status codes (MM_CMD_BASE = MM_STATUS_BASE = 1).
constexpr int MM_CMD_PLAY = 4;       // MM_CMD_BASE + 3
constexpr int MM_STATUS_START = 1;   // MM_STATUS_BASE
constexpr int MM_STATUS_DONE = 2;    // MM_STATUS_BASE + 1

uint16_t read_le16(const std::vector<uint8_t>& data, size_t off) {
    if (off + 2 > data.size()) return 0;
    return static_cast<uint16_t>(data[off] | (data[off + 1] << 8));
}

uint32_t read_le32(const std::vector<uint8_t>& data, size_t off) {
    if (off + 4 > data.size()) return 0;
    return static_cast<uint32_t>(data[off] |
                                 (data[off + 1] << 8) |
                                 (data[off + 2] << 16) |
                                 (data[off + 3] << 24));
}

bool trace_media() {
    return std::getenv("ZEEMU_TRACE_MEDIA") != nullptr;
}

bool is_smf_midi(const std::vector<uint8_t>& bytes) {
    return bytes.size() >= 14 && std::memcmp(bytes.data(), "MThd", 4) == 0;
}

std::vector<uint8_t> decode_ima_adpcm_mono(const std::vector<uint8_t>& bytes,
                                           uint32_t data_offset,
                                           uint32_t data_size,
                                           uint16_t block_align,
                                           uint16_t samples_per_block) {
    static constexpr int index_table[16] = {
        -1, -1, -1, -1, 2, 4, 6, 8,
        -1, -1, -1, -1, 2, 4, 6, 8,
    };
    static constexpr int step_table[89] = {
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
        19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
        50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
        130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
        337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
        2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
        5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
        15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
    };

    std::vector<uint8_t> pcm;
    if (block_align < 4 || data_offset >= bytes.size()) {
        return pcm;
    }

    const uint32_t data_end = std::min<uint32_t>(static_cast<uint32_t>(bytes.size()), data_offset + data_size);
    auto append_s16 = [&pcm](int sample) {
        sample = std::max(-32768, std::min(32767, sample));
        const auto s = static_cast<int16_t>(sample);
        pcm.push_back(static_cast<uint8_t>(s & 0xff));
        pcm.push_back(static_cast<uint8_t>((static_cast<uint16_t>(s) >> 8) & 0xff));
    };
    auto decode_nibble = [&](uint8_t nibble, int& predictor, int& step_index) {
        const int step = step_table[step_index];
        int diff = step >> 3;
        if (nibble & 1) diff += step >> 2;
        if (nibble & 2) diff += step >> 1;
        if (nibble & 4) diff += step;
        predictor += (nibble & 8) ? -diff : diff;
        step_index = std::max(0, std::min(88, step_index + index_table[nibble & 0x0f]));
        append_s16(predictor);
    };

    for (uint32_t block = data_offset; block + 4 <= data_end; block += block_align) {
        const uint32_t block_end = std::min<uint32_t>(data_end, block + block_align);
        int predictor = static_cast<int16_t>(read_le16(bytes, block));
        int step_index = std::max(0, std::min(88, static_cast<int>(bytes[block + 2])));
        uint16_t samples_in_block = 1;
        append_s16(predictor);
        for (uint32_t off = block + 4; off < block_end; ++off) {
            const uint8_t packed = bytes[off];
            decode_nibble(packed & 0x0f, predictor, step_index);
            ++samples_in_block;
            if (samples_per_block && samples_in_block >= samples_per_block) break;
            decode_nibble((packed >> 4) & 0x0f, predictor, step_index);
            ++samples_in_block;
            if (samples_per_block && samples_in_block >= samples_per_block) break;
        }
    }
    return pcm;
}

const char* find_soundfont_path() {
    static std::string selected;
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        const char* env = std::getenv("ZEEMU_SOUNDFONT");
        const std::filesystem::path candidates[] = {
            env ? std::filesystem::path(env) : std::filesystem::path(),
            std::filesystem::path("soundfont") / "GMGSx.sf2",
            std::filesystem::path("soundfont") / "GeneralUser GS v1.471.sf2",
        };
        for (const auto& candidate : candidates) {
            if (!candidate.empty() && std::filesystem::exists(candidate)) {
                selected = candidate.string();
                break;
            }
        }
    }
    return selected.empty() ? nullptr : selected.c_str();
}
}

BrewMediaPCM::BrewMediaPCM(BrewShell& shell, EndianMemory& memory, std::string type_name, uint32_t media_clsid)
    : shell_(shell), memory_(memory), type_name_(std::move(type_name)), media_clsid_(media_clsid) {
    setup_vtable();
}

BrewMediaPCM::~BrewMediaPCM() {
    if (audio_stream_) {
        SDL_DestroyAudioStream(audio_stream_);
        audio_stream_ = nullptr;
    }
}

void BrewMediaPCM::setup_vtable() {
    vtable_ptr_ = shell_.malloc(16 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);

    auto add_method = [&](int index, const std::string& name) {
        memory_.write_value(vtable_ptr_ + static_cast<uint32_t>(index * 4),
                            shell_.add_hook("IMediaPCM_" + name, this));
    };

    // SDK AEEIMedia.h: IQI + RegisterNotify, Set/GetMediaParm, playback controls.
    add_method(0, "AddRef");
    add_method(1, "Release");
    add_method(2, "QueryInterface");
    add_method(3, "RegisterNotify");
    add_method(4, "SetMediaParm");
    add_method(5, "GetMediaParm");
    add_method(6, "Play");
    add_method(7, "Record");
    add_method(8, "Stop");
    add_method(9, "Seek");
    add_method(10, "Pause");
    add_method(11, "Resume");
    add_method(12, "GetTotalTime");
    add_method(13, "GetState");
    for (int index = 14; index < 16; ++index) {
        add_method(index, "Fn" + std::to_string(index));
    }
}

void BrewMediaPCM::read_media_data(uint32_t media_data_ptr) {
    media_cls_data_ = memory_.read_value(media_data_ptr);
    media_data_ptr_ = memory_.read_value(media_data_ptr + 4);
    media_size_ = memory_.read_value(media_data_ptr + 8);
    if (media_cls_data_ == MMD_FILE_NAME) {
        load_file_media_data();
    }
    state_ = MM_STATE_READY;
}

void BrewMediaPCM::set_media_data_from_guest(uint32_t media_data_ptr) {
    read_media_data(media_data_ptr);
}

void BrewMediaPCM::load_file_media_data() {
    if (media_data_ptr_ == 0 || media_data_ptr_ >= 0xFF000000) {
        return;
    }
    const std::string path = shell_.read_guest_text(media_data_ptr_, 512);
    std::string bytes;
    if (!shell_.get_vfs().read_file(path, bytes, shell_.get_current_directory()) || bytes.empty()) {
        if (trace_media()) {
            printf("  IMedia%s_SetMediaData file '%s' unavailable\n", type_name_.c_str(), path.c_str());
        }
        media_size_ = 0;
        return;
    }
    addr_t guest_buf = shell_.malloc(static_cast<uint32_t>(bytes.size()), false);
    if (guest_buf == 0) {
        media_size_ = 0;
        return;
    }
    memory_.write(guest_buf, bytes);
    media_cls_data_ = MMD_BUFFER;
    media_data_ptr_ = guest_buf;
    media_size_ = static_cast<uint32_t>(bytes.size());
    if (trace_media()) {
        printf("  IMedia%s_SetMediaData file '%s' -> buffer=0x%08x size=%u\n",
               type_name_.c_str(), path.c_str(), media_data_ptr_, media_size_);
    }
}

void BrewMediaPCM::write_media_data(uint32_t media_data_ptr) const {
    memory_.write_value(media_data_ptr, media_cls_data_);
    memory_.write_value(media_data_ptr + 4, media_data_ptr_);
    memory_.write_value(media_data_ptr + 8, media_size_);
}

void BrewMediaPCM::fire_notify(int nCmd, int nStatus) {
    if (notify_fn_ == 0) {
        return;
    }
    // Build AEEMediaCmdNotify (AEEIMedia.h):
    //   +0x00 AEECLSID clsMedia
    //   +0x04 IMedia*  pIMedia
    //   +0x08 int      nCmd
    //   +0x0C int      nSubCmd
    //   +0x10 int      nStatus
    //   +0x14 void*    pCmdData
    //   +0x18 uint32   dwSize
    if (cmd_notify_buf_ == 0) {
        cmd_notify_buf_ = shell_.malloc(0x1C);
    }
    if (cmd_notify_buf_ == 0) {
        return;
    }
    memory_.write_value(cmd_notify_buf_ + 0x00, media_clsid_);
    memory_.write_value(cmd_notify_buf_ + 0x04, object_ptr_);
    memory_.write_value(cmd_notify_buf_ + 0x08, static_cast<uint32_t>(nCmd));
    memory_.write_value(cmd_notify_buf_ + 0x0C, 0u);
    memory_.write_value(cmd_notify_buf_ + 0x10, static_cast<uint32_t>(nStatus));
    memory_.write_value(cmd_notify_buf_ + 0x14, 0u);
    memory_.write_value(cmd_notify_buf_ + 0x18, 0u);
    // PFNMEDIANOTIFY(void* pUser, AEEMediaCmdNotify* pCmdNotify): R0=pUser, R1=struct.
    shell_.queue_signal_callback(notify_fn_, notify_user_, cmd_notify_buf_, "IMediaPCM_notify");
}

bool BrewMediaPCM::queue_pcm_to_sdl() {
    if (!media_data_ptr_ || !media_size_) {
        return false;
    }

    std::vector<uint8_t> bytes(media_size_);
    for (uint32_t i = 0; i < media_size_; ++i) {
        bytes[i] = static_cast<uint8_t>(memory_.read_value(media_data_ptr_ + i, EndianMemory::Byte));
    }

    if (zeemu::audio::looks_like_mp3(bytes)) {
        const std::string label = "IMedia" + type_name_;
        return zeemu::audio::queue_mp3_buffer_to_sdl(audio_stream_,
                                                     audio_freq_,
                                                     audio_channels_,
                                                     &audio_format_,
                                                     bytes,
                                                     volume_,
                                                     label.c_str(),
                                                     nullptr,
                                                     total_time_ms_,
                                                     0.05f);
    }

    uint32_t data_offset = 0;
    uint32_t data_size = media_size_;
    uint32_t freq = 22050;
    uint32_t channels = 1;
    SDL_AudioFormat format = SDL_AUDIO_S16LE;
    uint16_t wav_format = 1;
    uint16_t block_align = 0;
    uint16_t samples_per_block = 0;
    std::vector<uint8_t> decoded_pcm;

    uint32_t qcp_payload = 0;
    uint32_t qcp_estimated_ms = 0;
    if (zeemu::audio::describe_qcp_buffer(bytes, qcp_payload, qcp_estimated_ms)) {
        if (zeemu::audio::queue_qcp_buffer_to_sdl(audio_stream_,
                                                 audio_freq_,
                                                 audio_channels_,
                                                 audio_format_,
                                                 bytes,
                                                 100,
                                                 "IMediaQCP",
                                                 nullptr,
                                                 total_time_ms_,
                                                 0.0f)) {
            return true;
        }
        total_time_ms_ = qcp_estimated_ms;
        std::printf("  IMedia%s SDL: unsupported QCP/PureVoice decode payload=%u estimated=%u ms\n",
                    type_name_.c_str(),
                    qcp_payload,
                    total_time_ms_);
        return false;
    }

    if (bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
        std::memcmp(bytes.data() + 8, "WAVE", 4) == 0) {
        bool have_fmt = false;
        bool have_data = false;
        for (size_t off = 12; off + 8 <= bytes.size();) {
            const uint32_t chunk_size = read_le32(bytes, off + 4);
            const size_t payload = off + 8;
            if (payload + chunk_size > bytes.size()) {
                break;
            }
            if (std::memcmp(bytes.data() + off, "fmt ", 4) == 0 && chunk_size >= 16) {
                const uint16_t audio_format = read_le16(bytes, payload);
                const uint16_t wav_channels = read_le16(bytes, payload + 2);
                const uint32_t wav_freq = read_le32(bytes, payload + 4);
                const uint16_t wav_block_align = read_le16(bytes, payload + 12);
                const uint16_t bits = read_le16(bytes, payload + 14);
                if (audio_format == 1 && wav_channels >= 1 && wav_channels <= 2 && wav_freq >= 4000) {
                    wav_format = audio_format;
                    channels = wav_channels;
                    freq = wav_freq;
                    block_align = wav_block_align;
                    format = (bits == 8) ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
                    have_fmt = (bits == 8 || bits == 16);
                } else if (audio_format == 0x0011 && wav_channels == 1 && wav_freq >= 4000 &&
                           bits == 4 && wav_block_align >= 4 && chunk_size >= 20) {
                    wav_format = audio_format;
                    channels = wav_channels;
                    freq = wav_freq;
                    block_align = wav_block_align;
                    samples_per_block = read_le16(bytes, payload + 18);
                    format = SDL_AUDIO_S16LE;
                    have_fmt = true;
                }
            } else if (std::memcmp(bytes.data() + off, "data", 4) == 0) {
                data_offset = static_cast<uint32_t>(payload);
                data_size = chunk_size;
                have_data = true;
            }
            off = payload + chunk_size + (chunk_size & 1u);
        }
        if (!have_fmt || !have_data) {
            printf("  IMediaPCM SDL: unsupported WAV buffer fmt=%d data=%d size=%u\n",
                   have_fmt ? 1 : 0, have_data ? 1 : 0, media_size_);
            return false;
        }
        if (wav_format == 0x0011) {
            decoded_pcm = decode_ima_adpcm_mono(bytes, data_offset, data_size, block_align, samples_per_block);
            if (decoded_pcm.empty()) {
                printf("  IMediaPCM SDL: IMA ADPCM decode failed block=%u samples=%u size=%u\n",
                       block_align, samples_per_block, data_size);
                return false;
            }
            data_offset = 0;
            data_size = static_cast<uint32_t>(decoded_pcm.size());
        }
    }

    if (data_offset >= bytes.size()) {
        return false;
    }
    const uint8_t* audio_data = decoded_pcm.empty() ? bytes.data() + data_offset : decoded_pcm.data();
    if (decoded_pcm.empty()) {
        data_size = std::min<uint32_t>(data_size, static_cast<uint32_t>(bytes.size() - data_offset));
    }
    const uint32_t bytes_per_sample = (format == SDL_AUDIO_U8) ? 1u : 2u;
    const uint32_t frame_bytes = std::max<uint32_t>(1u, bytes_per_sample * channels);
    const uint32_t aligned_data_size = data_size - (data_size % frame_bytes);
    if (aligned_data_size != data_size) {
        printf("  IMediaPCM SDL: trimming unaligned payload bytes=%u frame=%u -> %u\n",
               data_size, frame_bytes, aligned_data_size);
        data_size = aligned_data_size;
    }
    if (data_size == 0) {
        return false;
    }
    total_time_ms_ = (freq != 0) ? (data_size / frame_bytes) * 1000u / freq : 0u;

    if (!zeemu::audio::ensure_audio_subsystem("IMediaPCM SDL:")) {
        return false;
    }

    if (audio_stream_ && (audio_freq_ != freq || audio_channels_ != channels || audio_format_ != format)) {
        SDL_DestroyAudioStream(audio_stream_);
        audio_stream_ = nullptr;
    }

    if (!audio_stream_) {
        SDL_AudioSpec spec{};
        spec.format = format;
        spec.channels = static_cast<int>(channels);
        spec.freq = static_cast<int>(freq);
        audio_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        audio_freq_ = freq;
        audio_channels_ = channels;
        audio_format_ = format;
        if (!audio_stream_) {
            printf("  IMediaPCM SDL: open stream failed: %s\n", SDL_GetError());
            return false;
        }
    }

    const float gain = (volume_ > 100) ? 1.0f : std::max(0.05f, static_cast<float>(volume_) / 100.0f);
    SDL_SetAudioStreamGain(audio_stream_, gain);
    const bool queued = SDL_PutAudioStreamData(audio_stream_, audio_data, static_cast<int>(data_size));
    if (queued) {
        SDL_ResumeAudioStreamDevice(audio_stream_);
    } else {
        printf("  IMediaPCM SDL: queue failed: %s\n", SDL_GetError());
    }
    printf("  IMediaPCM SDL: queued=%d fmt=%s%s freq=%u channels=%u bytes=%u duration=%u ms\n",
           queued ? 1 : 0, (format == SDL_AUDIO_U8) ? "u8" : "s16le",
           (wav_format == 0x0011) ? " ima-adpcm" : "", freq, channels, data_size, total_time_ms_);
    return queued;
}

bool BrewMediaPCM::queue_mp3_to_sdl() {
    if (!media_data_ptr_ || !media_size_) {
        return false;
    }

    std::vector<uint8_t> bytes(media_size_);
    for (uint32_t i = 0; i < media_size_; ++i) {
        bytes[i] = static_cast<uint8_t>(memory_.read_value(media_data_ptr_ + i, EndianMemory::Byte));
    }

    if (!zeemu::audio::looks_like_mp3(bytes)) {
        printf("  IMediaMP3 SDL: unsupported MP3 buffer size=%u\n", media_size_);
        return false;
    }

    return zeemu::audio::queue_mp3_buffer_to_sdl(audio_stream_,
                                                 audio_freq_,
                                                 audio_channels_,
                                                 &audio_format_,
                                                 bytes,
                                                 volume_,
                                                 "IMediaMP3",
                                                 nullptr,
                                                 total_time_ms_,
                                                 0.05f);
}

bool BrewMediaPCM::queue_midi_to_sdl() {
    if (!media_data_ptr_ || !media_size_) {
        return false;
    }

    std::vector<uint8_t> bytes(media_size_);
    for (uint32_t i = 0; i < media_size_; ++i) {
        bytes[i] = static_cast<uint8_t>(memory_.read_value(media_data_ptr_ + i, EndianMemory::Byte));
    }
    if (!is_smf_midi(bytes)) {
        return false;
    }

    tml_message* midi = tml_load_memory(bytes.data(), static_cast<int>(bytes.size()));
    if (!midi) {
        printf("  IMediaMIDI SDL: SMF parse failed size=%u\n", media_size_);
        return false;
    }

    int used_channels = 0;
    int used_programs = 0;
    int total_notes = 0;
    unsigned int first_note_ms = 0;
    unsigned int length_ms = 0;
    tml_get_info(midi, &used_channels, &used_programs, &total_notes, &first_note_ms, &length_ms);

    const char* soundfont_path = find_soundfont_path();
    if (!soundfont_path) {
        printf("  IMediaMIDI SDL: no soundfont found under soundfont/ or ZEEMU_SOUNDFONT\n");
        tml_free(midi);
        return false;
    }
    tsf* synth = tsf_load_filename(soundfont_path);
    if (!synth) {
        printf("  IMediaMIDI SDL: failed to load soundfont '%s'\n", soundfont_path);
        tml_free(midi);
        return false;
    }

    constexpr uint32_t kSampleRate = 44100;
    constexpr uint32_t kMaxMidiMs = 5u * 60u * 1000u;
    total_time_ms_ = std::min<uint32_t>(length_ms + 500u, kMaxMidiMs);
    if (total_time_ms_ == 0 || total_notes == 0) {
        tsf_close(synth);
        tml_free(midi);
        return false;
    }

    const size_t sample_count = static_cast<size_t>(total_time_ms_) * kSampleRate / 1000u;
    std::vector<int16_t> pcm(sample_count, 0);
    tsf_set_output(synth, TSF_MONO, static_cast<int>(kSampleRate), -8.0f);
    tsf_set_volume(synth, (volume_ > 100) ? 1.0f : std::max(0.05f, static_cast<float>(volume_) / 100.0f));
    tsf_set_max_voices(synth, 128);
    for (int channel = 0; channel < 16; ++channel) {
        tsf_channel_set_presetnumber(synth, channel, 0, channel == 9 ? 1 : 0);
    }

    auto render_until = [&](uint32_t& cursor_ms, uint32_t target_ms) {
        target_ms = std::min<uint32_t>(target_ms, total_time_ms_);
        size_t start = static_cast<size_t>(cursor_ms) * kSampleRate / 1000u;
        size_t end = static_cast<size_t>(target_ms) * kSampleRate / 1000u;
        end = std::min(end, pcm.size());
        if (end > start) {
            tsf_render_short(synth, pcm.data() + start, static_cast<int>(end - start), 0);
        }
        cursor_ms = target_ms;
    };

    uint32_t cursor_ms = 0;
    for (tml_message* msg = midi; msg; msg = msg->next) {
        render_until(cursor_ms, msg->time);
        const uint8_t channel = static_cast<uint8_t>(msg->channel & 0x0f);
        if (msg->type == TML_PROGRAM_CHANGE) {
            tsf_channel_set_presetnumber(synth, channel, static_cast<uint8_t>(msg->program), channel == 9 ? 1 : 0);
        } else if (msg->type == TML_CONTROL_CHANGE) {
            tsf_channel_midi_control(synth, channel, static_cast<uint8_t>(msg->control), static_cast<uint8_t>(msg->control_value));
        } else if (msg->type == TML_PITCH_BEND) {
            tsf_channel_set_pitchwheel(synth, channel, msg->pitch_bend);
        } else if (msg->type == TML_NOTE_ON) {
            const uint8_t velocity = static_cast<uint8_t>(msg->velocity);
            if (velocity == 0) {
                tsf_channel_note_off(synth, channel, static_cast<uint8_t>(msg->key));
            } else {
                tsf_channel_note_on(synth, channel, static_cast<uint8_t>(msg->key), static_cast<float>(velocity) / 127.0f);
            }
        } else if (msg->type == TML_NOTE_OFF) {
            tsf_channel_note_off(synth, channel, static_cast<uint8_t>(msg->key));
        }
    }
    render_until(cursor_ms, total_time_ms_);
    tsf_close(synth);
    tml_free(midi);

    if (!zeemu::audio::ensure_audio_subsystem("IMediaMIDI SDL:")) {
        return false;
    }
    if (audio_stream_ && (audio_freq_ != kSampleRate || audio_channels_ != 1 || audio_format_ != SDL_AUDIO_S16LE)) {
        SDL_DestroyAudioStream(audio_stream_);
        audio_stream_ = nullptr;
    }
    if (!audio_stream_) {
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16LE;
        spec.channels = 1;
        spec.freq = static_cast<int>(kSampleRate);
        audio_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        audio_freq_ = kSampleRate;
        audio_channels_ = 1;
        audio_format_ = SDL_AUDIO_S16LE;
        if (!audio_stream_) {
            printf("  IMediaMIDI SDL: open stream failed: %s\n", SDL_GetError());
            return false;
        }
    }
    const float gain = (volume_ > 100) ? 1.0f : std::max(0.05f, static_cast<float>(volume_) / 100.0f);
    SDL_SetAudioStreamGain(audio_stream_, gain);
    const bool queued = SDL_PutAudioStreamData(audio_stream_, pcm.data(), static_cast<int>(pcm.size() * sizeof(int16_t)));
    if (queued) {
        SDL_ResumeAudioStreamDevice(audio_stream_);
    }
    printf("  IMediaMIDI SDL: queued=%d notes=%d channels=%d programs=%d bytes=%u duration=%u ms sf='%s'\n",
           queued ? 1 : 0, total_notes, used_channels, used_programs,
           static_cast<uint32_t>(pcm.size() * sizeof(int16_t)), total_time_ms_, soundfont_path);
    return queued;
}

void BrewMediaPCM::handle_hook(const std::string& name, CPU& cpu) {
    const uint32_t r0 = cpu.get_reg(REG_R0);
    const uint32_t r1 = cpu.get_reg(REG_R1);
    const uint32_t r2 = cpu.get_reg(REG_R2);
    const uint32_t r3 = cpu.get_reg(REG_R3);

    if (name == "IMediaPCM_AddRef") {
        cpu.set_reg(REG_R0, r0);
    } else if (name == "IMediaPCM_Release") {
        state_ = MM_STATE_IDLE;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMediaPCM_QueryInterface") {
        const uint32_t iid = r1;
        const uint32_t pp = r2;
        const bool supported = (iid == AEEIID_IMEDIA || iid == media_clsid_);
        if (pp) {
            memory_.write_value(pp, supported ? object_ptr_ : 0);
        }
        cpu.set_reg(REG_R0, supported ? 0u : 4u);
    } else if (name == "IMediaPCM_RegisterNotify") {
        notify_fn_ = r1;
        notify_user_ = r2;
        printf("  IMediaPCM_RegisterNotify pfn=0x%08x pUser=0x%08x\n", notify_fn_, notify_user_);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMediaPCM_SetMediaParm") {
        const uint32_t parm = r1;
        if (parm == MM_PARM_MEDIA_DATA && r2) {
            read_media_data(r2);
            if (trace_media() || type_name_ == "MIDI") {
                std::vector<uint8_t> sig;
                const uint32_t sig_len = std::min<uint32_t>(media_size_, 4);
                sig.resize(sig_len);
                for (uint32_t i = 0; i < sig_len; ++i) {
                    sig[i] = static_cast<uint8_t>(memory_.read_value(media_data_ptr_ + i, EndianMemory::Byte));
                }
                printf("  IMedia%s_SetMediaData cls=0x%08x data=0x%08x size=%u sig='%c%c%c%c'\n",
                       type_name_.c_str(), media_cls_data_, media_data_ptr_, media_size_,
                       sig_len > 0 ? sig[0] : '.', sig_len > 1 ? sig[1] : '.',
                       sig_len > 2 ? sig[2] : '.', sig_len > 3 ? sig[3] : '.');
            } else {
                printf("  IMediaPCM_SetMediaData cls=0x%08x data=0x%08x size=%u\n",
                       media_cls_data_, media_data_ptr_, media_size_);
            }
        } else if (parm == MM_PARM_VOLUME) {
            volume_ = r2;
            printf("  IMediaPCM_SetVolume %u\n", volume_);
        } else {
            printf("  IMediaPCM_SetMediaParm parm=%u p1=0x%08x p2=0x%08x\n", parm, r2, r3);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMediaPCM_GetMediaParm") {
        const uint32_t parm = r1;
        if (parm == MM_PARM_MEDIA_DATA && r2) {
            write_media_data(r2);
        } else if (parm == MM_PARM_VOLUME && r2) {
            memory_.write_value(r2, volume_, EndianMemory::Halfword);
        } else if (parm == MM_PARM_CLSID && r2) {
            memory_.write_value(r2, media_clsid_);
        } else if (parm == MM_PARM_CAPS && r2) {
            memory_.write_value(r2, MM_CAPS_AUDIO);
            if (r3) memory_.write_value(r3, 0);
        } else if (parm == MM_PARM_CHANNEL_SHARE && r2) {
            memory_.write_value(r2, 1, EndianMemory::Byte);
        } else if (parm == MM_PARM_TICK_TIME && r2) {
            memory_.write_value(r2, 0);
        }
        printf("  IMediaPCM_GetMediaParm parm=%u p1=0x%08x p2=0x%08x\n", parm, r2, r3);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMediaPCM_Play") {
        state_ = MM_STATE_PLAY;
        const bool audio_ok =
            (type_name_ == "MIDI") ? queue_midi_to_sdl() :
            (type_name_ == "MP3") ? queue_mp3_to_sdl() :
            queue_pcm_to_sdl();
        printf("  IMedia%s_Play data=0x%08x size=%u notify=0x%08x audio=%d\n",
               type_name_.c_str(), media_data_ptr_, media_size_, notify_fn_, audio_ok ? 1 : 0);
        cpu.set_reg(REG_R0, 0);
        // AEEIMedia playback is asynchronous. Short effects can complete in
        // the same smoke slice, but long tracks must not receive DONE
        // immediately or callers will restart them in a tight loop.
        fire_notify(MM_CMD_PLAY, (audio_ok && total_time_ms_ > 1000u) ? MM_STATUS_START : MM_STATUS_DONE);
        if (!audio_ok || total_time_ms_ <= 1000u) {
            state_ = media_data_ptr_ ? MM_STATE_READY : MM_STATE_IDLE;
        }
    } else if (name == "IMediaPCM_Record") {
        state_ = MM_STATE_PLAY;
        printf("  IMediaPCM_Record data=0x%08x size=%u\n", media_data_ptr_, media_size_);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMediaPCM_Stop") {
        if (audio_stream_) {
            SDL_ClearAudioStream(audio_stream_);
        }
        state_ = media_data_ptr_ ? MM_STATE_READY : MM_STATE_IDLE;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMediaPCM_Seek") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMediaPCM_Pause") {
        state_ = MM_STATE_PLAY_PAUSE;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMediaPCM_Resume") {
        state_ = MM_STATE_PLAY;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IMediaPCM_GetTotalTime") {
        cpu.set_reg(REG_R0, total_time_ms_);
    } else if (name == "IMediaPCM_GetState") {
        if (r1) memory_.write_value(r1, 0, EndianMemory::Byte);
        cpu.set_reg(REG_R0, state_);
    } else if (name.rfind("IMediaPCM_Fn", 0) == 0) {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), r0, r1, r2, r3);
        cpu.set_reg(REG_R0, 0);
    }
}
