#ifndef ZEEMU_BREW_AUDIO_H_
#define ZEEMU_BREW_AUDIO_H_

#include <cstdint>
#include <vector>

struct SDL_AudioStream;

namespace zeemu::audio {

bool ensure_audio_subsystem(const char* label);
bool looks_like_mp3(const std::vector<uint8_t>& bytes);
bool looks_like_wav(const std::vector<uint8_t>& bytes);
bool looks_like_midi(const std::vector<uint8_t>& bytes);
bool looks_like_qcp(const std::vector<uint8_t>& bytes);

bool describe_qcp_buffer(
    const std::vector<uint8_t>& bytes,
    uint32_t& payload_bytes,
    uint32_t& estimated_time_ms);

bool queue_mp3_buffer_to_sdl(
    SDL_AudioStream*& stream,
    uint32_t& stream_freq,
    uint32_t& stream_channels,
    uint32_t* stream_format,
    const std::vector<uint8_t>& bytes,
    uint32_t volume,
    const char* label,
    const char* source_label,
    uint32_t& total_time_ms,
    float min_gain);

bool queue_wav_buffer_to_sdl(
    SDL_AudioStream*& stream,
    uint32_t& stream_freq,
    uint32_t& stream_channels,
    uint32_t& stream_format,
    const std::vector<uint8_t>& bytes,
    uint32_t volume,
    const char* label,
    const char* source_label,
    uint32_t& total_time_ms,
    float min_gain);

bool queue_midi_buffer_to_sdl(
    SDL_AudioStream*& stream,
    uint32_t& stream_freq,
    uint32_t& stream_channels,
    uint32_t& stream_format,
    const std::vector<uint8_t>& bytes,
    uint32_t volume,
    const char* label,
    const char* source_label,
    uint32_t& total_time_ms,
    float min_gain);

bool queue_qcp_buffer_to_sdl(
    SDL_AudioStream*& stream,
    uint32_t& stream_freq,
    uint32_t& stream_channels,
    uint32_t& stream_format,
    const std::vector<uint8_t>& bytes,
    uint32_t volume,
    const char* label,
    const char* source_label,
    uint32_t& total_time_ms,
    float min_gain);

} // namespace zeemu::audio

#endif
