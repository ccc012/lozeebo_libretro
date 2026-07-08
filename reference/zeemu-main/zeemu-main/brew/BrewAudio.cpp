#include "brew/BrewAudio.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>

#ifdef ZEEMU_USE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/samplefmt.h>
}
#endif

#include "third_party/tml.h"
#include "third_party/tsf.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#include "minimp3_ex.h"

namespace zeemu::audio {

namespace {

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

#ifdef ZEEMU_USE_FFMPEG

struct MemoryReadContext {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
};

int ffmpeg_memory_read(void* opaque, uint8_t* buf, int buf_size) {
    auto* ctx = static_cast<MemoryReadContext*>(opaque);
    if (!ctx || ctx->pos >= ctx->size) {
        return AVERROR_EOF;
    }
    const size_t remaining = ctx->size - ctx->pos;
    const size_t count = std::min<size_t>(remaining, static_cast<size_t>(buf_size));
    std::memcpy(buf, ctx->data + ctx->pos, count);
    ctx->pos += count;
    return static_cast<int>(count);
}

int64_t ffmpeg_memory_seek(void* opaque, int64_t offset, int whence) {
    auto* ctx = static_cast<MemoryReadContext*>(opaque);
    if (!ctx) {
        return AVERROR(EINVAL);
    }
    if (whence == AVSEEK_SIZE) {
        return static_cast<int64_t>(ctx->size);
    }
    int64_t base = 0;
    if (whence == SEEK_CUR) {
        base = static_cast<int64_t>(ctx->pos);
    } else if (whence == SEEK_END) {
        base = static_cast<int64_t>(ctx->size);
    } else if (whence != SEEK_SET) {
        return AVERROR(EINVAL);
    }
    const int64_t next = base + offset;
    if (next < 0 || static_cast<uint64_t>(next) > ctx->size) {
        return AVERROR(EINVAL);
    }
    ctx->pos = static_cast<size_t>(next);
    return next;
}

int16_t sample_to_s16(const AVFrame* frame, int sample, int channels) {
    const auto fmt = static_cast<AVSampleFormat>(frame->format);
    const bool planar = av_sample_fmt_is_planar(fmt) != 0;
    const AVSampleFormat packed_fmt = av_get_packed_sample_fmt(fmt);
    double mixed = 0.0;
    for (int ch = 0; ch < channels; ++ch) {
        const int plane = planar ? ch : 0;
        const int index = planar ? sample : sample * channels + ch;
        if (packed_fmt == AV_SAMPLE_FMT_FLT) {
            mixed += static_cast<const float*>(static_cast<const void*>(frame->data[plane]))[index];
        } else if (packed_fmt == AV_SAMPLE_FMT_DBL) {
            mixed += static_cast<const double*>(static_cast<const void*>(frame->data[plane]))[index];
        } else if (packed_fmt == AV_SAMPLE_FMT_S16) {
            mixed += static_cast<const int16_t*>(static_cast<const void*>(frame->data[plane]))[index] / 32768.0;
        } else if (packed_fmt == AV_SAMPLE_FMT_S32) {
            mixed += static_cast<const int32_t*>(static_cast<const void*>(frame->data[plane]))[index] / 2147483648.0;
        } else if (packed_fmt == AV_SAMPLE_FMT_U8) {
            mixed += (static_cast<const uint8_t*>(static_cast<const void*>(frame->data[plane]))[index] - 128) / 128.0;
        }
    }
    mixed /= std::max(1, channels);
    mixed = std::max(-1.0, std::min(1.0, mixed));
    return static_cast<int16_t>(mixed * 32767.0);
}

bool append_frame_as_pcm16(const AVFrame* frame, std::vector<uint8_t>& pcm, int& sample_rate) {
    if (!frame || frame->nb_samples <= 0) {
        return false;
    }
    const int channels = std::max(1, frame->ch_layout.nb_channels);
    sample_rate = frame->sample_rate > 0 ? frame->sample_rate : sample_rate;
    const size_t old_size = pcm.size();
    pcm.resize(old_size + static_cast<size_t>(frame->nb_samples) * sizeof(int16_t));
    auto* out = reinterpret_cast<int16_t*>(pcm.data() + old_size);
    for (int i = 0; i < frame->nb_samples; ++i) {
        out[i] = sample_to_s16(frame, i, channels);
    }
    return true;
}

bool decode_qcp_with_ffmpeg(const std::vector<uint8_t>& bytes,
                            std::vector<uint8_t>& pcm,
                            int& sample_rate,
                            const char* label,
                            const char* source_label) {
    constexpr int kAvioBufferSize = 4096;
    MemoryReadContext mem{bytes.data(), bytes.size(), 0};
    auto* avio_buffer = static_cast<uint8_t*>(av_malloc(kAvioBufferSize));
    AVIOContext* avio = avio_alloc_context(avio_buffer, kAvioBufferSize, 0, &mem,
                                           ffmpeg_memory_read, nullptr, ffmpeg_memory_seek);
    AVFormatContext* fmt = avformat_alloc_context();
    if (!avio || !fmt) {
        if (avio) avio_context_free(&avio);
        else av_free(avio_buffer);
        if (fmt) avformat_free_context(fmt);
        return false;
    }
    fmt->pb = avio;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;

    const AVInputFormat* input = av_find_input_format("qcp");
    int ret = avformat_open_input(&fmt, nullptr, input, nullptr);
    if (ret < 0) {
        std::printf("  %s FFmpeg QCP open failed ret=%d%s%s%s\n",
                    label, ret, source_label ? " source='" : "",
                    source_label ? source_label : "", source_label ? "'" : "");
        avio_context_free(&avio);
        avformat_free_context(fmt);
        return false;
    }

    ret = avformat_find_stream_info(fmt, nullptr);
    if (ret < 0) {
        avformat_close_input(&fmt);
        avio_context_free(&avio);
        return false;
    }
    const int stream_index = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_index < 0) {
        avformat_close_input(&fmt);
        avio_context_free(&avio);
        return false;
    }

    AVStream* stream = fmt->streams[stream_index];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext* codec_ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!codec_ctx || avcodec_parameters_to_context(codec_ctx, stream->codecpar) < 0 ||
        avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        if (codec_ctx) avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt);
        avio_context_free(&avio);
        return false;
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool decoded_any = false;
    sample_rate = codec_ctx->sample_rate > 0 ? codec_ctx->sample_rate : 8000;
    auto receive_frames = [&]() {
        while (avcodec_receive_frame(codec_ctx, frame) == 0) {
            decoded_any |= append_frame_as_pcm16(frame, pcm, sample_rate);
            av_frame_unref(frame);
        }
    };

    while (av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == stream_index && avcodec_send_packet(codec_ctx, packet) == 0) {
            receive_frames();
        }
        av_packet_unref(packet);
    }
    avcodec_send_packet(codec_ctx, nullptr);
    receive_frames();

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt);
    avio_context_free(&avio);
    return decoded_any && !pcm.empty();
}

#else

bool decode_qcp_with_ffmpeg(const std::vector<uint8_t>&,
                            std::vector<uint8_t>&,
                            int&,
                            const char*,
                            const char*) {
    return false;
}

#endif

} // namespace

bool ensure_audio_subsystem(const char* label) {
    static bool initialized = false;
    if (initialized) {
        return true;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        printf("  %s SDL_INIT_AUDIO failed: %s\n", label, SDL_GetError());
        return false;
    }
    initialized = true;
    return true;
}

bool looks_like_mp3(const std::vector<uint8_t>& bytes) {
    return (bytes.size() >= 3 && bytes[0] == 'I' && bytes[1] == 'D' && bytes[2] == '3') ||
           (bytes.size() >= 2 && bytes[0] == 0xff && (bytes[1] & 0xe0) == 0xe0);
}

bool looks_like_wav(const std::vector<uint8_t>& bytes) {
    return bytes.size() >= 12 &&
           std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
           std::memcmp(bytes.data() + 8, "WAVE", 4) == 0;
}

bool looks_like_midi(const std::vector<uint8_t>& bytes) {
    return bytes.size() >= 14 && std::memcmp(bytes.data(), "MThd", 4) == 0;
}

bool looks_like_qcp(const std::vector<uint8_t>& bytes) {
    return bytes.size() >= 12 &&
           std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
           std::memcmp(bytes.data() + 8, "QLCM", 4) == 0;
}

bool describe_qcp_buffer(
    const std::vector<uint8_t>& bytes,
    uint32_t& payload_bytes,
    uint32_t& estimated_time_ms) {
    payload_bytes = 0;
    estimated_time_ms = 0;
    if (!looks_like_qcp(bytes)) {
        return false;
    }

    for (size_t off = 12; off + 8 <= bytes.size();) {
        const uint32_t chunk_size = read_le32(bytes, off + 4);
        const size_t payload = off + 8;
        if (payload + chunk_size > bytes.size()) {
            break;
        }
        if (std::memcmp(bytes.data() + off, "data", 4) == 0) {
            payload_bytes = chunk_size;
            break;
        }
        off = payload + chunk_size + (chunk_size & 1u);
    }

    if (payload_bytes == 0 && bytes.size() > 12) {
        payload_bytes = static_cast<uint32_t>(bytes.size() - 12);
    }

    // Zeebo Developer Guide documents QCP as fixed full-rate 13 kbps QCELP at
    // 8 kHz. Without a local PureVoice decoder, keep media duration/state sane
    // but never queue the compressed RIFF/QLCM payload as bogus PCM.
    if (payload_bytes != 0) {
        const uint64_t ms = (static_cast<uint64_t>(payload_bytes) * 8u * 1000u + 12999u) / 13000u;
        estimated_time_ms = static_cast<uint32_t>(std::min<uint64_t>(ms, 0xffffffffull));
    }
    return true;
}

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
    float min_gain) {
    mp3dec_t dec{};
    mp3dec_file_info_t info{};
    const int rc = mp3dec_load_buf(&dec, bytes.data(), bytes.size(), &info, nullptr, nullptr);
    if (rc != 0 || !info.buffer || info.samples == 0 || info.hz <= 0 || info.channels <= 0) {
        printf("  %s SDL MP3 decode failed rc=%d samples=%zu hz=%d channels=%d size=%zu\n",
               label, rc, info.samples, info.hz, info.channels, bytes.size());
        if (info.buffer) {
            std::free(info.buffer);
        }
        return false;
    }

    if (!ensure_audio_subsystem(label)) {
        std::free(info.buffer);
        return false;
    }

    if (stream && (stream_freq != static_cast<uint32_t>(info.hz) ||
                   stream_channels != static_cast<uint32_t>(info.channels) ||
                   (stream_format && *stream_format != SDL_AUDIO_S16LE))) {
        SDL_DestroyAudioStream(stream);
        stream = nullptr;
    }

    if (!stream) {
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16LE;
        spec.channels = info.channels;
        spec.freq = info.hz;
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        stream_freq = static_cast<uint32_t>(info.hz);
        stream_channels = static_cast<uint32_t>(info.channels);
        if (stream_format) {
            *stream_format = SDL_AUDIO_S16LE;
        }
        if (!stream) {
            printf("  %s SDL open stream failed: %s\n", label, SDL_GetError());
            std::free(info.buffer);
            return false;
        }
    }

    SDL_ClearAudioStream(stream);
    const float gain = (volume > 100) ? 1.0f : std::max(min_gain, static_cast<float>(volume) / 100.0f);
    SDL_SetAudioStreamGain(stream, gain);
    const int byte_count = static_cast<int>(info.samples * sizeof(mp3d_sample_t));
    const bool queued = SDL_PutAudioStreamData(stream, info.buffer, byte_count);
    if (queued) {
        SDL_ResumeAudioStreamDevice(stream);
    }

    total_time_ms = static_cast<uint32_t>((info.samples / static_cast<size_t>(info.channels)) * 1000u /
                                          static_cast<uint32_t>(info.hz));
    printf("  %s SDL MP3 queued=%d%s%s%s hz=%d channels=%d samples=%zu bytes=%d duration=%u ms\n",
           label,
           queued ? 1 : 0,
           source_label ? " source='" : "",
           source_label ? source_label : "",
           source_label ? "'" : "",
           info.hz,
           info.channels,
           info.samples,
           byte_count,
           total_time_ms);
    std::free(info.buffer);
    return queued;
}

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
    float min_gain) {
    if (!looks_like_wav(bytes)) {
        return false;
    }

    uint32_t data_offset = 0;
    uint32_t data_size = 0;
    uint32_t freq = 22050;
    uint32_t channels = 1;
    SDL_AudioFormat format = SDL_AUDIO_S16LE;
    uint16_t wav_format = 1;
    uint16_t block_align = 0;
    uint16_t samples_per_block = 0;
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

    if (!have_fmt || !have_data || data_offset >= bytes.size()) {
        printf("  %s SDL WAV unsupported fmt=%d data=%d size=%zu%s%s%s\n",
               label, have_fmt ? 1 : 0, have_data ? 1 : 0, bytes.size(),
               source_label ? " source='" : "", source_label ? source_label : "",
               source_label ? "'" : "");
        return false;
    }

    std::vector<uint8_t> decoded_pcm;
    if (wav_format == 0x0011) {
        decoded_pcm = decode_ima_adpcm_mono(bytes, data_offset, data_size, block_align, samples_per_block);
        if (decoded_pcm.empty()) {
            printf("  %s SDL WAV IMA ADPCM decode failed block=%u samples=%u size=%u%s%s%s\n",
                   label, block_align, samples_per_block, data_size,
                   source_label ? " source='" : "", source_label ? source_label : "",
                   source_label ? "'" : "");
            return false;
        }
        data_offset = 0;
        data_size = static_cast<uint32_t>(decoded_pcm.size());
    } else {
        data_size = std::min<uint32_t>(data_size, static_cast<uint32_t>(bytes.size() - data_offset));
    }

    const uint8_t* audio_data = decoded_pcm.empty() ? bytes.data() + data_offset : decoded_pcm.data();
    const uint32_t bytes_per_sample = (format == SDL_AUDIO_U8) ? 1u : 2u;
    const uint32_t frame_bytes = std::max<uint32_t>(1u, bytes_per_sample * channels);
    total_time_ms = (freq != 0) ? (data_size / frame_bytes) * 1000u / freq : 0u;

    if (!ensure_audio_subsystem(label)) {
        return false;
    }
    if (stream && (stream_freq != freq || stream_channels != channels || stream_format != format)) {
        SDL_DestroyAudioStream(stream);
        stream = nullptr;
    }
    if (!stream) {
        SDL_AudioSpec spec{};
        spec.format = format;
        spec.channels = static_cast<int>(channels);
        spec.freq = static_cast<int>(freq);
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        stream_freq = freq;
        stream_channels = channels;
        stream_format = format;
        if (!stream) {
            printf("  %s SDL WAV open stream failed: %s\n", label, SDL_GetError());
            return false;
        }
    }

    SDL_ClearAudioStream(stream);
    const float gain = (volume > 100) ? 1.0f : std::max(min_gain, static_cast<float>(volume) / 100.0f);
    SDL_SetAudioStreamGain(stream, gain);
    const bool queued = SDL_PutAudioStreamData(stream, audio_data, static_cast<int>(data_size));
    if (queued) {
        SDL_ResumeAudioStreamDevice(stream);
    }
    printf("  %s SDL WAV queued=%d fmt=%s%s freq=%u channels=%u bytes=%u duration=%u ms%s%s%s\n",
           label, queued ? 1 : 0, (format == SDL_AUDIO_U8) ? "u8" : "s16le",
           (wav_format == 0x0011) ? " ima-adpcm" : "", freq, channels, data_size,
           total_time_ms, source_label ? " source='" : "", source_label ? source_label : "",
           source_label ? "'" : "");
    return queued;
}

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
    float min_gain) {
    if (!looks_like_midi(bytes)) {
        return false;
    }

    tml_message* midi = tml_load_memory(bytes.data(), static_cast<int>(bytes.size()));
    if (!midi) {
        printf("  %s SDL MIDI parse failed size=%zu%s%s%s\n",
               label, bytes.size(), source_label ? " source='" : "",
               source_label ? source_label : "", source_label ? "'" : "");
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
        printf("  %s SDL MIDI no soundfont found under soundfont/ or ZEEMU_SOUNDFONT%s%s%s\n",
               label, source_label ? " source='" : "", source_label ? source_label : "",
               source_label ? "'" : "");
        tml_free(midi);
        return false;
    }
    tsf* synth = tsf_load_filename(soundfont_path);
    if (!synth) {
        printf("  %s SDL MIDI failed to load soundfont '%s'%s%s%s\n",
               label, soundfont_path, source_label ? " source='" : "",
               source_label ? source_label : "", source_label ? "'" : "");
        tml_free(midi);
        return false;
    }

    constexpr uint32_t kSampleRate = 44100;
    constexpr uint32_t kMaxMidiMs = 5u * 60u * 1000u;
    total_time_ms = std::min<uint32_t>(length_ms + 500u, kMaxMidiMs);
    if (total_time_ms == 0 || total_notes == 0) {
        tsf_close(synth);
        tml_free(midi);
        return false;
    }

    const size_t sample_count = static_cast<size_t>(total_time_ms) * kSampleRate / 1000u;
    std::vector<int16_t> pcm(sample_count, 0);
    tsf_set_output(synth, TSF_MONO, static_cast<int>(kSampleRate), -8.0f);
    tsf_set_volume(synth, (volume > 100) ? 1.0f : std::max(min_gain, static_cast<float>(volume) / 100.0f));
    tsf_set_max_voices(synth, 128);
    for (int channel = 0; channel < 16; ++channel) {
        tsf_channel_set_presetnumber(synth, channel, 0, channel == 9 ? 1 : 0);
    }

    auto render_until = [&](uint32_t& cursor_ms, uint32_t target_ms) {
        target_ms = std::min<uint32_t>(target_ms, total_time_ms);
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
            const auto velocity = static_cast<uint8_t>(msg->velocity);
            if (velocity == 0) {
                tsf_channel_note_off(synth, channel, static_cast<uint8_t>(msg->key));
            } else {
                tsf_channel_note_on(synth, channel, static_cast<uint8_t>(msg->key), static_cast<float>(velocity) / 127.0f);
            }
        } else if (msg->type == TML_NOTE_OFF) {
            tsf_channel_note_off(synth, channel, static_cast<uint8_t>(msg->key));
        }
    }
    render_until(cursor_ms, total_time_ms);
    tsf_close(synth);
    tml_free(midi);

    if (!ensure_audio_subsystem(label)) {
        return false;
    }
    if (stream && (stream_freq != kSampleRate || stream_channels != 1 || stream_format != SDL_AUDIO_S16LE)) {
        SDL_DestroyAudioStream(stream);
        stream = nullptr;
    }
    if (!stream) {
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16LE;
        spec.channels = 1;
        spec.freq = static_cast<int>(kSampleRate);
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        stream_freq = kSampleRate;
        stream_channels = 1;
        stream_format = SDL_AUDIO_S16LE;
        if (!stream) {
            printf("  %s SDL MIDI open stream failed: %s\n", label, SDL_GetError());
            return false;
        }
    }

    SDL_ClearAudioStream(stream);
    const bool queued = SDL_PutAudioStreamData(stream, pcm.data(), static_cast<int>(pcm.size() * sizeof(int16_t)));
    if (queued) {
        SDL_ResumeAudioStreamDevice(stream);
    }
    printf("  %s SDL MIDI queued=%d notes=%d channels=%d programs=%d bytes=%u duration=%u ms sf='%s'%s%s%s\n",
           label, queued ? 1 : 0, total_notes, used_channels, used_programs,
           static_cast<uint32_t>(pcm.size() * sizeof(int16_t)), total_time_ms,
           soundfont_path, source_label ? " source='" : "",
           source_label ? source_label : "", source_label ? "'" : "");
    return queued;
}

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
    float min_gain) {
    if (!looks_like_qcp(bytes)) {
        return false;
    }

    constexpr uint32_t kChannels = 1;
    constexpr SDL_AudioFormat kFormat = SDL_AUDIO_S16LE;
    int decoded_rate = 8000;
    std::vector<uint8_t> pcm;
    if (!decode_qcp_with_ffmpeg(bytes, pcm, decoded_rate, label, source_label)) {
        return false;
    }
    const uint32_t sample_rate = decoded_rate > 0 ? static_cast<uint32_t>(decoded_rate) : 8000u;
    total_time_ms = static_cast<uint32_t>((pcm.size() / 2u) * 1000u / sample_rate);

    if (!ensure_audio_subsystem(label)) {
        return false;
    }
    if (stream && (stream_freq != sample_rate || stream_channels != kChannels || stream_format != kFormat)) {
        SDL_DestroyAudioStream(stream);
        stream = nullptr;
    }
    if (!stream) {
        SDL_AudioSpec spec{};
        spec.format = kFormat;
        spec.channels = static_cast<int>(kChannels);
        spec.freq = static_cast<int>(sample_rate);
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        stream_freq = sample_rate;
        stream_channels = kChannels;
        stream_format = kFormat;
        if (!stream) {
            std::printf("  %s SDL QCP open stream failed: %s\n", label, SDL_GetError());
            return false;
        }
    }

    SDL_ClearAudioStream(stream);
    const float gain = (volume > 100) ? 1.0f : std::max(min_gain, static_cast<float>(volume) / 100.0f);
    SDL_SetAudioStreamGain(stream, gain);
    const bool queued = SDL_PutAudioStreamData(stream, pcm.data(), static_cast<int>(pcm.size()));
    if (queued) {
        SDL_ResumeAudioStreamDevice(stream);
    }
    std::printf("  %s FFmpeg QCP queued=%d bytes=%u duration=%u ms%s%s%s\n",
                label,
                queued ? 1 : 0,
                static_cast<uint32_t>(pcm.size()),
                total_time_ms,
                source_label ? " source='" : "",
                source_label ? source_label : "",
                source_label ? "'" : "");
    return queued;
}

} // namespace zeemu::audio
