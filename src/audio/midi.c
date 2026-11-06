/* midi.c — Renderer MIDI via TinySoundFont (compatível com Infuse) */
#include <stdio.h>
#include "audio.h"
#ifndef ZMIDI_DEFAULT_SF
#define ZMIDI_DEFAULT_SF "default.sf2"
#endif
#include <string.h>
#include <stdlib.h>
#include "midi.h"
#include "../memory/memory.h"
#include "../debug/log.h"

#define TSF_IMPLEMENTATION
#define TSF_STATIC
#include "extern/tinysoundfont/tsf.h"
#define TML_IMPLEMENTATION
#define TML_STATIC
#include "extern/tinysoundfont/tml.h"

static struct tsf *g_tsf = NULL;
static bool g_active = true;
static uint8_t g_volume = 200;

/* Sequencia SMF em reproducao (TML) */
static tml_message *g_midi_seq = NULL;
static tml_message *g_midi_cur = NULL;
static double g_midi_time_ms = 0.0;
static double g_midi_length_ms = 0.0;
static bool g_midi_playing = false;

bool zmidi_init(uint32_t sf2_addr, uint32_t sf2_size, uint32_t sample_rate) {
    if (g_tsf) {
        tsf_close(g_tsf);
        g_tsf = NULL;
    }
    if (!sf2_addr || sf2_size < 100 || sample_rate == 0) return false;

    /* Copia a soundfont da memoria emulada para um buffer host e carrega. */
    void *buf = malloc(sf2_size);
    if (!buf) return false;
    const uint8_t *src = (const uint8_t *)zmem_host_ptr(sf2_addr, sf2_size);
    if (!src) { free(buf); return false; }
    memcpy(buf, src, sf2_size);

    g_tsf = tsf_load_memory(buf, (int)sf2_size);
    free(buf);

    if (!g_tsf) {
        LOGE("zmidi_init: falha ao carregar soundfont");
        return false;
    }
    tsf_set_output(g_tsf, TSF_STEREO_INTERLEAVED, (int)sample_rate, 0.f);
    tsf_set_volume(g_tsf, 1.0f);
    LOGI("zmidi_init: soundfont carregada (rate=%u presets=%d)", sample_rate, g_tsf->presetNum);
    return true;
}

void zmidi_shutdown(void) {
    if (g_tsf) {
        tsf_close(g_tsf);
        g_tsf = NULL;
    }
}

void zmidi_note_on(uint8_t ch, uint8_t note, uint8_t vel) {
    if (!g_tsf || !g_active) return;
    if (ch >= 15) return;
    if (vel == 0) { zmidi_note_off(ch, note); return; }
    tsf_channel_note_on(g_tsf, (int)ch, (int)note, (float)vel / 127.f);
}

void zmidi_note_off(uint8_t ch, uint8_t note) {
    if (!g_tsf || !g_active) return;
    if (ch >= 15) return;
    tsf_channel_note_off(g_tsf, (int)ch, (int)note);
}

void zmidi_program_change(uint8_t ch, uint8_t program) {
    if (!g_tsf || !g_active) return;
    if (ch >= 15) return;
    tsf_channel_set_presetnumber(g_tsf, (int)ch, (int)program, (ch == 9));
}

void zmidi_control_change(uint8_t ch, uint8_t ctrl, uint8_t val) {
    if (!g_tsf || !g_active) return;
    if (ch >= 15) return;
    tsf_channel_midi_control(g_tsf, (int)ch, (int)ctrl, (int)val);
}

void zmidi_pitch_bend(uint8_t ch, int16_t bend) {
    if (!g_tsf || !g_active) return;
    if (ch >= 15) return;
    tsf_channel_set_pitchwheel(g_tsf, (int)ch, (int)bend);
}

void zmidi_all_notes_off(uint8_t ch) {
    if (!g_tsf || !g_active) return;
    if (ch == 0xFF) {
        tsf_note_off_all(g_tsf);
    } else if (ch < 15) {
        tsf_channel_note_off_all(g_tsf, (int)ch);
    }
}

void zmidi_byte(uint8_t byte) {
    (void)byte;
}

void zmidi_render(int16_t *out, uint32_t frames) {
    if (!g_tsf || !g_active) return;
    if (g_volume == 0) return;

    /* Avanca a sequencia SMF (se houver) */
    if (g_midi_playing && g_midi_seq) {
        double ms_per_frame = (double)frames / (double)ZAUDIO_SAMPLE_RATE * 1000.0;
        g_midi_time_ms += ms_per_frame;
        while (g_midi_cur) {
            if ((double)g_midi_cur->time > g_midi_time_ms) break;
            switch (g_midi_cur->type) {
                case TML_PROGRAM_CHANGE:
                    tsf_channel_set_presetnumber(g_tsf, g_midi_cur->channel,
                        g_midi_cur->program, g_midi_cur->channel == 9 ? 1 : 0);
                    break;
                case TML_CONTROL_CHANGE:
                    tsf_channel_midi_control(g_tsf, g_midi_cur->channel,
                        g_midi_cur->control, g_midi_cur->control_value);
                    break;
                case TML_PITCH_BEND:
                    tsf_channel_set_pitchwheel(g_tsf, g_midi_cur->channel,
                        g_midi_cur->pitch_bend);
                    break;
                case TML_NOTE_ON:
                    if (g_midi_cur->velocity == 0)
                        tsf_channel_note_off(g_tsf, g_midi_cur->channel,
                            (unsigned char)g_midi_cur->key);
                    else
                        tsf_channel_note_on(g_tsf, g_midi_cur->channel,
                            (unsigned char)g_midi_cur->key,
                            (float)g_midi_cur->velocity / 127.0f);
                    break;
                case TML_NOTE_OFF:
                    tsf_channel_note_off(g_tsf, g_midi_cur->channel,
                        (unsigned char)g_midi_cur->key);
                    break;
                case TML_ALL_NOTES_OFF:
                    tsf_channel_note_off_all(g_tsf, g_midi_cur->channel);
                    break;
                case TML_ALL_SOUND_OFF:
                    tsf_channel_sounds_off_all(g_tsf, g_midi_cur->channel);
                    break;
                default:
                    break;
            }
            g_midi_cur = g_midi_cur->next;
            if (!g_midi_cur && g_midi_length_ms > 0.0) {
                /* loop */
                g_midi_time_ms -= g_midi_length_ms;
                if (g_midi_time_ms < 0) g_midi_time_ms = 0;
                g_midi_cur = g_midi_seq;
            }
        }
    }

    float *tmp = (float *)calloc(frames * 2, sizeof(float));
    if (!tmp) return;
    tsf_render_float(g_tsf, tmp, (int)frames, 0);

    float vol = (float)g_volume / 255.f;
    for (uint32_t i = 0; i < frames * 2; i++) {
        float s = tmp[i] * vol;
        int32_t s32 = (int32_t)(s * 32767.f);
        if (s32 > 32767) s32 = 32767;
        if (s32 < -32768) s32 = -32768;
        int32_t acc = (int32_t)out[i] + s32;
        if (acc > 32767) acc = 32767;
        if (acc < -32768) acc = -32768;
        out[i] = (int16_t)acc;
    }
    free(tmp);
}

void zmidi_set_volume(uint8_t vol) {
    g_volume = vol;
}

void zmidi_set_active(bool active) {
    g_active = active;
    if (!active && g_tsf) tsf_note_off_all(g_tsf);
}

bool zmidi_is_active(void) {
    return g_active;
}

uint32_t zmidi_active_notes(void) {
    if (!g_tsf) return 0;
    uint32_t n = 0;
    for (int i = 0; i < g_tsf->voiceNum; i++)
        if (g_tsf->voices[i].playingPreset >= 0) n++;
    return n;
}

uint32_t zmidi_voice_count(void) {
    return g_tsf ? (uint32_t)g_tsf->voiceNum : 0;
}

bool zmidi_load_smf(const void *data, size_t size) {
    if (g_midi_seq) {
        tml_free(g_midi_seq);
        g_midi_seq = NULL;
    }
    g_midi_cur = NULL;
    g_midi_time_ms = 0.0;
    g_midi_length_ms = 0.0;
    g_midi_playing = false;
    if (!data || size < 16) return false;
    g_midi_seq = tml_load_memory(data, (int)size);
    if (!g_midi_seq) return false;
    g_midi_cur = g_midi_seq;
    {
        int used_channels = 0, used_programs = 0, total_notes = 0;
        unsigned int first_note_ms = 0, length_ms = 0;
        tml_get_info(g_midi_seq, &used_channels, &used_programs,
                     &total_notes, &first_note_ms, &length_ms);
        g_midi_length_ms = (double)(length_ms ? length_ms : 1000);
    }
    LOGI("zmidi_load_smf: carregado (%u bytes, dur=%u ms)", (unsigned)size,
         (unsigned)g_midi_length_ms);
    return true;
}

void zmidi_play_smf(void) {
    g_midi_time_ms = 0.0;
    g_midi_cur = g_midi_seq;
    g_midi_playing = (g_midi_seq != NULL);
    g_active = true;
    if (g_tsf) tsf_note_off_all(g_tsf);
}

void zmidi_stop_smf(void) {
    g_midi_playing = false;
    g_midi_cur = g_midi_seq;
    g_midi_time_ms = 0.0;
    if (g_tsf) tsf_note_off_all(g_tsf);
}

bool zmidi_smf_playing(void) {
    return g_midi_playing;
}
/* Inicializa MIDI com a soundfont padrao do disco (data/default.sf2). */
void zmidi_init_default(void) {
    const char *paths[] = {
        "default.sf2",
        "data/default.sf2",
        NULL
    };
    FILE *f = NULL;
    for (int i = 0; paths[i]; i++) {
        f = fopen(paths[i], "rb");
        if (f) break;
    }
    if (!f) {
        LOGW("zmidi_init_default: soundfont nao encontrada (MIDI desativado)");
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return; }
    fread(buf, 1, (size_t)sz, f);
    fclose(f);

    if (g_tsf) { tsf_close(g_tsf); g_tsf = NULL; }
    g_tsf = tsf_load_memory(buf, (int)sz);
    if (g_tsf) {
        tsf_set_output(g_tsf, TSF_STEREO_INTERLEAVED, ZAUDIO_SAMPLE_RATE, 0.f);
        tsf_set_volume(g_tsf, 1.0f);
        LOGI("zmidi_init_default: soundfont carregada (%ld bytes, %d presets)", sz, g_tsf->presetNum);
    } else {
        LOGE("zmidi_init_default: falha ao carregar soundfont");
    }
    free(buf);
}
