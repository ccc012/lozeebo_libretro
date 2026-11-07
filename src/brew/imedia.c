/* imedia.c - IMedia HLE para PCM, ADPCM, MIDI, MP3
 *
 * Vtable na ordem real do AEEIMedia.h (confirmada no zeemu
 * BrewMediaPCM.cpp) - ver comentario do enum ZT_MEDIA_* em brew.h.
 * SetMediaData NAO e um slot: o SDK entrega os dados via
 * SetMediaParm(MM_PARM_MEDIA_DATA). */
#include <string.h>
#include <stdlib.h>
#include "brew.h"
#include "imedia.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../audio/audio.h"
#include "../audio/audio_internal.h"
#include "../audio/adpcm.h"
#include "../audio/midi.h"
#include "../audio/mp3.h"
#include "../debug/log.h"

/* Alice 28, Foot Camp 32+ no boot; 8 falhava apos o 8o CreateInstance. */
#define ZMEDIA_MAX 64

/* AEEMedia.h: parms e estados */
#define MM_PARM_MEDIA_DATA    1
#define MM_PARM_VOLUME        4
#define MM_PARM_TICK_TIME     9
#define MM_PARM_CLSID         13
#define MM_PARM_CAPS          14
#define MM_PARM_CHANNEL_SHARE 16
#define MM_CAPS_AUDIO         1u

/* AEEMedia.h: notificacao assincrona */
#define MM_CMD_SETMEDIAPARM   2
#define MM_CMD_PLAY           4
#define MM_STATUS_START       1
#define MM_STATUS_DONE        2

/* MMD (AEEMediaData.clsData) */
#define MMD_FILE_NAME 0u
#define MMD_BUFFER    1u

#define AEEIID_IMEDIA 0x01005500u

/* Limite superior de endereco emulado valido (RAM + heap + stack + VRAM). */
#define ZMEDIA_ADDR_MAX (ZMEM_VRAM_BASE + ZMEM_VRAM_SIZE)

static zmedia_t g_media[ZMEDIA_MAX];

static zmedia_t *media_alloc(void) {
    int i;
    for (i = 0; i < ZMEDIA_MAX; i++) {
        if (!g_media[i].active) {
            memset(&g_media[i], 0, sizeof(zmedia_t));
            g_media[i].active = true;
            g_media[i].volume = 255;
            g_media[i].state = ZMEDIA_STATE_IDLE;
            g_media[i].notify_enabled = true;
            g_media[i].voice = -1;
            g_media[i].mp3_stream = -1;
            g_media[i].fmt = ZPCM_S16;
            g_media[i].channels = 1;
            g_media[i].block_align = 0;
            g_media[i].samples_per_block = 1024;
            return &g_media[i];
        }
    }
    return NULL;
}

zmedia_t *media_from_obj(uint32_t obj) {
    int i;
    if (obj == 0) return NULL;
    for (i = 0; i < ZMEDIA_MAX; i++)
        if (g_media[i].active && g_media[i].obj == obj)
            return &g_media[i];
    return NULL;
}

static uint32_t make_media_interface(uint32_t clsid);
static void media_notify(zmedia_t *m, uint32_t cmd, uint32_t subcmd,
                         uint32_t status);

static zmedia_t *media_alloc_for(uint32_t clsid) {
    zmedia_t *m = media_alloc();
    if (!m) return NULL;
    m->clsid = clsid;
    m->obj = make_media_interface(clsid);
    if (!m->obj) {
        m->active = false;
        return NULL;
    }
    return m;
}

static uint32_t make_media_interface(uint32_t clsid) {
    static const uint32_t vtbl[] = {
        ZT_MEDIA_ADDREF, ZT_MEDIA_RELEASE, ZT_MEDIA_QUERYINTERFACE,
        ZT_MEDIA_REGISTERNOTIFY, ZT_MEDIA_SETMEDIADATA, ZT_MEDIA_GETMEDIADATA,
        ZT_MEDIA_PLAY, ZT_MEDIA_RECORD, ZT_MEDIA_STOP, ZT_MEDIA_SEEK,
        ZT_MEDIA_PAUSE, ZT_MEDIA_RESUME, ZT_MEDIA_GETTOTALTIME,
        ZT_MEDIA_GETSTATE,
        /* Fn14/Fn15: slots reservados, respondem com sucesso neutro */
        ZT_MEDIA_RECORD, ZT_MEDIA_RECORD
    };
    uint32_t obj = zbrew_make_interface(vtbl, 16, 2);
    if (obj) {
        zmem_write32(obj + 4, clsid);
        createinstance_cache_put(clsid, obj);
    }
    return obj;
}

static uint32_t media_time_ms(const zmedia_t *m) {
    uint64_t samples;
    uint32_t denom;
    if (!m || !m->rate || !m->bytes) return 0;
    if (m->clsid == AEECLSID_MEDIAMIDI || m->clsid == AEECLSID_MEDIAMP3)
        return m->time_ms;
    denom = (m->fmt == ZPCM_U8 ? 1u : 2u) * (m->channels ? m->channels : 1u);
    samples = m->bytes / denom;
    return (uint32_t)((samples * 1000u) / m->rate);
}

/* Le o AEEMediaData {clsData, pData, dwSize} e prepara a midia.
 * Para WAV, interpreta cabecalho RIFF/fmt/data (rate/canais/bits). */
static void media_set_data(zmedia_t *m, uint32_t pdata) {
    uint32_t clsdata, data, size;
    if (!m || !pdata || pdata >= ZMEDIA_ADDR_MAX) return;
    clsdata = zmem_read32(pdata + 0);
    data = zmem_read32(pdata + 4);
    size = zmem_read32(pdata + 8);
    m->rate = 22050;
    m->fmt = ZPCM_S16;
    m->channels = 1;
    m->time_ms = 0;
    m->is_mp3 = false;

    if (clsdata == MMD_FILE_NAME) {
        /* AEEMediaData.clsData == MMD_FILE_NAME: pData e o caminho. Carrega
         * do host/VFS (data.vfs) para um buffer no heap emulado. */
        char path[192];
        uint8_t *fb = NULL;
        uint32_t fsz = 0;
        zmem_read_cstr(data, path, sizeof(path));
        if (!zbrew_read_asset(path, &fb, &fsz) || !fsz) {
            LOGW("IMedia_SetMediaData: arquivo '%s' indisponivel", path);
            m->addr = 0;
            m->bytes = 0;
            m->state = ZMEDIA_STATE_IDLE;
            return;
        }
        {
            uint32_t guest = zheap_alloc(fsz);
            if (!guest || !zmem_write_block(guest, fb, fsz)) {
                free(fb);
                LOGW("IMedia_SetMediaData: sem heap para '%s' (%u bytes)",
                     path, fsz);
                m->addr = 0;
                m->bytes = 0;
                m->state = ZMEDIA_STATE_IDLE;
                return;
            }
            m->addr = guest;
            m->bytes = fsz;
            LOGI("IMedia_SetMediaData: '%s' carregado (%u bytes) -> 0x%08X",
                 path, fsz, guest);
        }
        free(fb);
    } else {
        m->addr = data;
        m->bytes = size;
        /* Ridge: dwSize=0x10005AE8 (ponteiro heap) apos LoadResDataEx errado.
         * Rejeita tamanho que parece ponteiro ou excede a regiao do pData. */
        if (m->addr && m->addr < ZMEDIA_ADDR_MAX) {
            uint32_t avail = 0;
            if (m->addr < ZMEM_RAM_SIZE)
                avail = ZMEM_RAM_SIZE - m->addr;
            else if (m->addr >= ZMEM_HEAP_BASE &&
                     m->addr < ZMEM_HEAP_BASE + ZMEM_HEAP_SIZE)
                avail = (ZMEM_HEAP_BASE + ZMEM_HEAP_SIZE) - m->addr;
            else if (m->addr >= ZMEM_GUARD_BASE &&
                     m->addr < ZMEM_GUARD_BASE + ZMEM_GUARD_SIZE)
                avail = (ZMEM_GUARD_BASE + ZMEM_GUARD_SIZE) - m->addr;
            if (size == 0) {
                /* Guest clear (DD PCM apos MIDI): idle, sem WARN. */
                static uint32_t clear_logs;
                if (clear_logs++ < 4)
                    LOGI("IMedia_SetMediaData: clear (dwSize=0) prev=0x%08X",
                         m->addr);
                m->bytes = 0;
                m->addr = 0;
            } else if (size >= ZMEM_HEAP_BASE || (avail && size > avail)) {
                LOGW("IMedia_SetMediaData: dwSize=%u invalido addr=0x%08X "
                     "(descarta)", size, m->addr);
                m->bytes = 0;
                m->addr = 0;
            }
        }
    }

    if (m->addr && m->bytes >= 44 && m->addr < ZMEDIA_ADDR_MAX &&
        zmem_read32(m->addr) == 0x46464952u /* RIFF */ &&
        zmem_read32(m->addr + 8) == 0x45564157u /* WAVE */) {
        uint32_t tag = m->addr + 12;
        uint32_t end = m->addr + m->bytes;
        while (tag + 8 <= end) {
            uint32_t id = zmem_read32(tag);
            uint32_t csz = zmem_read32(tag + 4);
            if (id == 0x20746D66u) { /* "fmt " */
                uint16_t ch = zmem_read16(tag + 10);
                uint32_t sr = zmem_read32(tag + 12);
                uint16_t bits = zmem_read16(tag + 22);
                uint16_t afmt = zmem_read16(tag + 8);
                if (ch >= 1 && ch <= 2) m->channels = (uint8_t)ch;
                if (sr >= 4000 && sr <= 192000) m->rate = sr;
                if (afmt == 1 && bits == 8) m->fmt = ZPCM_U8;
                else if (afmt == 0x11 && bits == 4) {
                    /* WAV IMA ADPCM (fmt 0x0011): decode via IMA */
                    m->fmt = ZPCM_ADPCM;
                    m->block_align = zmem_read16(tag + 12);
                    m->samples_per_block = zmem_read16(tag + 18);
                } else m->fmt = ZPCM_S16;
            } else if (id == 0x61746164u) { /* "data" */
                m->addr = tag + 8;
                m->bytes = csz;
                if (m->bytes > (uint32_t)(end - (tag + 8)))
                    m->bytes = (uint32_t)(end - (tag + 8));
                break;
            }
            tag += 8 + ((csz + 1u) & ~1u);
        }
    }

    /* Mídia ADPCM: decode via IMA (portado do Zeemu BrewMediaPCM) */
    if (m->clsid == AEECLSID_MEDIAADPCM) {
        m->fmt = ZPCM_ADPCM;
        if (m->adpcm_state)
            zimad_set_stream(m->adpcm_state, m->addr, m->bytes,
                            m->block_align, m->samples_per_block);
    }

    /* MP3 por conteudo (tag ID3 ou sync MPEG), como o zeemu faz. */
    if (m->addr && m->bytes >= 4) {
        uint8_t b0 = zmem_read8(m->addr), b1 = zmem_read8(m->addr + 1);
        if ((b0 == 'I' && b1 == 'D') ||
            (b0 == 0xFF && (b1 & 0xE0) == 0xE0))
            m->is_mp3 = true;
    }

    m->state = m->bytes ? ZMEDIA_STATE_READY : ZMEDIA_STATE_IDLE;
    LOGI("IMedia_SetMediaData done: clsid=0x%08X addr=0x%08X bytes=%u rate=%u ch=%u fmt=%s vol=%u",
         m->clsid, m->addr, m->bytes, m->rate, m->channels,
         m->fmt == ZPCM_U8 ? "u8" : (m->fmt == ZPCM_ADPCM ? "ima-adpcm" : "s16"), m->volume);
    /* Pac-Mania registra o callback depois de SetMediaData. A notificacao
     * inicial e enviada em RegisterNotify, quando o callback ja existe. */
    if (getenv("ZEEMU_TRACE_MEDIA") && m->addr && m->addr < ZMEDIA_ADDR_MAX) {
        LOGI("  dados: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
             "%02X %02X %02X %02X %02X %02X",
             zmem_read8(m->addr + 0), zmem_read8(m->addr + 1),
             zmem_read8(m->addr + 2), zmem_read8(m->addr + 3),
             zmem_read8(m->addr + 4), zmem_read8(m->addr + 5),
             zmem_read8(m->addr + 6), zmem_read8(m->addr + 7),
             zmem_read8(m->addr + 8), zmem_read8(m->addr + 9),
             zmem_read8(m->addr + 10), zmem_read8(m->addr + 11),
             zmem_read8(m->addr + 12), zmem_read8(m->addr + 13),
             zmem_read8(m->addr + 14), zmem_read8(m->addr + 15));
    }
}

/* Enfileira PFNMEDIANOTIFY(pUser, AEEMediaCmdNotify*) conforme AEEIMedia.h:
 *   +0x00 clsMedia, +0x04 IMedia*, +0x08 nCmd, +0x0C nSubCmd,
 *   +0x10 nStatus, +0x14 pCmdData, +0x18 dwSize */
static void media_notify(zmedia_t *m, uint32_t cmd, uint32_t subcmd,
                         uint32_t status) {
    uint32_t buf;
    if (!m || !m->notify_cb || !m->notify_enabled) return;
    if (!m->notify_buf) {
        m->notify_buf = zheap_alloc(0x1C);
        if (!m->notify_buf) return;
    }
    buf = m->notify_buf;
    zmem_write32(buf + 0x00, m->clsid);
    zmem_write32(buf + 0x04, m->obj);
    zmem_write32(buf + 0x08, cmd);
    zmem_write32(buf + 0x0C, subcmd);
    zmem_write32(buf + 0x10, status);
    zmem_write32(buf + 0x14, 0);
    zmem_write32(buf + 0x18, 0);
    LOGI("IMedia_notify: cmd=%u subcmd=%u status=%u cb=0x%08X user=0x%08X",
         cmd, subcmd, status, m->notify_cb, m->notify_user);
    zbrew_queue_signal_callback(m->notify_cb, m->notify_user, buf, 0);
}

void zbrew_handle_media(uint32_t id) {
    uint32_t slot = id - ZT_MEDIA_ADDREF;
    uint32_t obj = g_cpu.r[0];
    zmedia_t *m = media_from_obj(obj);

    if (!m && slot != 0 && slot != 1) {
        g_cpu.r[0] = ZBREW_EFAILED;
        return;
    }

    switch (slot) {
    case 0: /* AddRef */
        g_cpu.r[0] = 1;
        break;

    case 1: /* Release */
        if (m) {
            if (m->voice >= 0) {
                zaudio_stop(m->voice);
                m->voice = -1;
            }
            if (m->mp3_stream >= 0) {
                zmp3_stop(m->mp3_stream);
                m->mp3_stream = -1;
            }
            if (m->adpcm_state) { free(m->adpcm_state); m->adpcm_state = NULL; }
            if (m->midi_state) { free(m->midi_state); m->midi_state = NULL; }
            if (m->mp3_state) { free(m->mp3_state); m->mp3_state = NULL; }
            m->active = false;
        }
        g_cpu.r[0] = 0;
        break;

    case 2: { /* QueryInterface(iid, ppObj) */
        uint32_t iid = g_cpu.r[1];
        uint32_t out = g_cpu.r[2];
        uint32_t ok = m && (iid == AEEIID_IMEDIA || iid == m->clsid);
        if (ok && out && out < ZMEDIA_ADDR_MAX)
            zmem_write32(out, obj);
        g_cpu.r[0] = ok ? 0 : 4; /* SUCCESS / ECLASSNOTSUPPORT */
        break;
    }

    case 3: /* RegisterNotify(pfn, pUser) */
        if (m) {
            m->notify_cb = g_cpu.r[1];
            m->notify_user = g_cpu.r[2];
            m->notify_enabled = true;
            LOGI("IMedia_RegisterNotify: cb=0x%08X user=0x%08X",
                 m->notify_cb, m->notify_user);
            /* Pac-Mania/AH/Ovos: RegisterNotify apos SetMediaData e nao
             * chamam Play. Ovos PCM no heap — auto-Play cobre. */
            if (m->state == ZMEDIA_STATE_READY && m->addr && m->bytes) {
                g_cpu.r[0] = obj;
                zbrew_handle_media(ZT_MEDIA_PLAY);
                return;
            }
        }
        g_cpu.r[0] = 0;
        break;

    case 4: { /* SetMediaParm(parm, value) */
        uint32_t parm = g_cpu.r[1];
        uint32_t val = g_cpu.r[2];
        if (m) {
            if (parm == MM_PARM_MEDIA_DATA)
                media_set_data(m, val);
            else if (parm == MM_PARM_VOLUME) {
                if (val != 0)
                    m->volume = (uint8_t)(val > 255 ? 255 : val);
                if (m->voice >= 0) zaudio_set_volume(m->voice, m->volume);
                if (m->clsid == AEECLSID_MEDIAMIDI)
                    zmidi_set_volume(m->volume);
            } else if (parm == MM_PARM_CLSID && val) {
                m->clsid = val;
            } else {
                LOGD("IMedia_SetMediaParm: parm=%u val=0x%08X (ignorado)",
                     parm, val);
            }
        }
        g_cpu.r[0] = 0;
        break;
    }

    case 5: { /* GetMediaParm(parm, pValue) */
        uint32_t parm = g_cpu.r[1];
        uint32_t out = g_cpu.r[2];
        if (m && out && out < ZMEDIA_ADDR_MAX) {
            if (parm == MM_PARM_MEDIA_DATA) {
                zmem_write32(out + 0, MMD_BUFFER);
                zmem_write32(out + 4, m->addr);
                zmem_write32(out + 8, m->bytes);
            } else if (parm == MM_PARM_VOLUME) {
                zmem_write16(out, m->volume);
            } else if (parm == MM_PARM_TICK_TIME) {
                zmem_write32(out, 0);
            } else if (parm == MM_PARM_CLSID) {
                zmem_write32(out, m->clsid);
            } else if (parm == MM_PARM_CAPS) {
                zmem_write32(out, MM_CAPS_AUDIO);
                if (g_cpu.r[3] && g_cpu.r[3] < ZMEDIA_ADDR_MAX)
                    zmem_write32(g_cpu.r[3], 0);
            } else if (parm == MM_PARM_CHANNEL_SHARE) {
                zmem_write8(out, 1);
            } else {
                zmem_write32(out, 0);
            }
        }
        g_cpu.r[0] = 0;
        break;
    }

        g_cpu.r[0] = 0;
        break;

    case 6: /* Play */
        if (!m) break;
        LOGI("IMedia_Play: clsid=0x%08X voice=%d addr=0x%08X bytes=%u rate=%u ch=%u", m->clsid, m->voice, m->addr, m->bytes, m->rate, m->channels);
        if (m->voice >= 0) {
            zaudio_stop(m->voice);
            m->voice = -1;
        }
        if (m->state == ZMEDIA_STATE_READY ||
            m->state == ZMEDIA_STATE_PLAY ||
            m->state == ZMEDIA_STATE_PLAY_PAUSE) {
            if (m->clsid == AEECLSID_MEDIAMIDI) {
                zmidi_load_smf(zmem_host_ptr(m->addr, m->bytes), m->bytes);
                zmidi_play_smf();
                zmidi_set_volume(m->volume);
                m->state = ZMEDIA_STATE_PLAY;
                LOGI("IMedia_Play: MIDI ativado (%u bytes)", m->bytes);
            } else if (m->is_mp3 || m->clsid == AEECLSID_MEDIAMP3) {
                /* MP3 via dr_mp3. Re-Play: seek ao inicio — nao cair em PCM
                 * (DONE falso com time_ms=0 fazia spam de bitstream como s16). */
                if (m->mp3_stream < 0) {
                    m->mp3_stream = zmp3_play(m->addr, m->bytes, m->loop);
                    if (m->mp3_stream >= 0) {
                        if (!m->time_ms)
                            m->time_ms = 60000u; /* evita DONE imediato */
                        m->state = ZMEDIA_STATE_PLAY;
                        LOGI("IMedia_Play: MP3 stream=%d", m->mp3_stream);
                    }
                } else {
                    zmp3_restart(m->mp3_stream);
                    m->state = ZMEDIA_STATE_PLAY;
                    LOGI("IMedia_Play: MP3 stream=%d (restart)", m->mp3_stream);
                }
            } else {
                m->voice = zaudio_play_pcm_ch(m->addr, m->bytes, m->rate,
                                              (enum zpcm_format)m->fmt,
                                              m->channels,
                                              m->loop, m->volume);
                if (m->voice >= 0) {
                    if (m->fmt == ZPCM_ADPCM && m->adpcm_state) {
                        /* ADPCM: vincula o decoder de blocos ao mixer */
                        zvoice_t *v = zaudio_voice_internal(m->voice);
                        v->adpcm = m->adpcm_state;
                        zimad_reset(m->adpcm_state);
                    }
                    m->state = ZMEDIA_STATE_PLAY;
                }
                LOGI("IMedia_Play: clsid=0x%08X voice=%d addr=0x%08X bytes=%u "
                     "rate=%u ch=%u", m->clsid, m->voice, m->addr, m->bytes,
                     m->rate, m->channels);
            }
        }
        if (m->notify_cb) {
            /* START se voz/stream ativa. PCM curto Ovos: notify sync
             * no Play faz guest Stop antes do zaudio_render → audio=0.
             * Adia START; DONE via zbrew_media_tick quando voz acaba
             * (permite re-Play / loops de SFX). */
            uint32_t status = MM_STATUS_DONE;
            if (m->state == ZMEDIA_STATE_PLAY ||
                m->voice >= 0 || m->mp3_stream >= 0 ||
                m->clsid == AEECLSID_MEDIAMIDI ||
                m->is_mp3 || m->clsid == AEECLSID_MEDIAMP3)
                status = MM_STATUS_START;
            if (status == MM_STATUS_START && m->voice >= 0 &&
                !m->is_mp3 && m->clsid != AEECLSID_MEDIAMP3 &&
                m->clsid != AEECLSID_MEDIAMIDI) {
                m->pending_start_notify = 1;
                m->awaiting_done = 1;
            } else {
                media_notify(m, MM_CMD_PLAY, 0, status);
                if (status == MM_STATUS_DONE)
                    m->state = m->bytes ? ZMEDIA_STATE_READY
                                        : ZMEDIA_STATE_IDLE;
            }
        }
        g_cpu.r[0] = 0;
        break;

    case 7: /* Record (nao suportado) */
        g_cpu.r[0] = 0;
        break;

    case 8: /* Stop */
        if (m) {
            if (m->voice >= 0) {
                zaudio_stop(m->voice);
                m->voice = -1;
            }
            if (m->mp3_stream >= 0) {
                zmp3_stop(m->mp3_stream);
                m->mp3_stream = -1;
            }
            if (m->clsid == AEECLSID_MEDIAMIDI) {
                zmidi_stop_smf();
            }
            m->state = m->bytes ? ZMEDIA_STATE_READY : ZMEDIA_STATE_IDLE;
        }
        g_cpu.r[0] = 0;
        break;

    case 9: /* Seek(ms) */
        if (m && m->voice >= 0 && m->rate) {
            uint32_t ms = g_cpu.r[1];
            uint32_t sample = (uint32_t)(((uint64_t)ms * m->rate) / 1000u);
            zaudio_seek(m->voice, sample);
        }
        g_cpu.r[0] = 0;
        break;

    case 10: /* Pause */
        if (m) {
            if (m->voice >= 0) zaudio_pause(m->voice);
            if (m->clsid == AEECLSID_MEDIAMIDI) zmidi_set_active(false);
            if (m->state == ZMEDIA_STATE_PLAY)
                m->state = ZMEDIA_STATE_PLAY_PAUSE;
        }
        g_cpu.r[0] = 0;
        break;

    case 11: /* Resume */
        if (m) {
            if (m->voice >= 0) zaudio_resume(m->voice);
            if (m->clsid == AEECLSID_MEDIAMIDI) zmidi_set_active(true);
            if (m->state == ZMEDIA_STATE_PLAY_PAUSE)
                m->state = ZMEDIA_STATE_PLAY;
        }
        g_cpu.r[0] = 0;
        break;

    case 12: /* GetTotalTime -> ms */
        g_cpu.r[0] = media_time_ms(m);
        break;

    case 13: /* GetState(po, pnState) */
        if (m && g_cpu.r[1] && g_cpu.r[1] < ZMEDIA_ADDR_MAX)
            zmem_write8(g_cpu.r[1], 0);
        g_cpu.r[0] = m ? m->state : ZMEDIA_STATE_IDLE;
        break;

    default: /* slots reservados */
        g_cpu.r[0] = 0;
        break;
    }
}

uint32_t zbrew_create_media_pcm(void) {
    zmedia_t *m = media_alloc_for(AEECLSID_MEDIAPCM);
    if (!m) return 0;
    m->rate = 22050;
    return m->obj;
}

uint32_t zbrew_create_media_midi(void) {
    zmedia_t *m = media_alloc_for(AEECLSID_MEDIAMIDI);
    if (!m) return 0;
    m->midi_state = calloc(1, sizeof(struct zmidi_state));
    return m->obj;
}

uint32_t zbrew_create_media_mp3(void) {
    zmedia_t *m = media_alloc_for(AEECLSID_MEDIAMP3);
    if (!m) return 0;
    m->mp3_state = calloc(1, 128);
    return m->obj;
}

uint32_t zbrew_create_media_adpcm(void) {
    zmedia_t *m = media_alloc_for(AEECLSID_MEDIAADPCM);
    if (!m) return 0;
    m->adpcm_state = calloc(1, sizeof(struct zimad_state));
    return m->obj;
}

void zbrew_media_tick(void) {
    int i;
    for (i = 0; i < ZMEDIA_MAX; i++) {
        zmedia_t *m = &g_media[i];
        if (!m->active) continue;
        if (m->pending_start_notify) {
            m->pending_start_notify = 0;
            media_notify(m, MM_CMD_PLAY, 0, MM_STATUS_START);
        }
        if (m->awaiting_done && m->voice >= 0 &&
            !zaudio_voice_active(m->voice)) {
            m->awaiting_done = 0;
            m->voice = -1;
            m->state = m->bytes ? ZMEDIA_STATE_READY : ZMEDIA_STATE_IDLE;
            media_notify(m, MM_CMD_PLAY, 0, MM_STATUS_DONE);
        }
    }
}
