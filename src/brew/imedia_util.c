/* imedia_util.c - IMediaUtil HLE para CreateMedia */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "brew.h"
#include "imedia.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../debug/log.h"

extern char g_base_dir[512];

#define strcasecmp _stricmp

void zbrew_handle_media_util(uint32_t id) {
    uint32_t slot = id - ZT_MEDIAUTIL_ADDREF;
    uint32_t obj = g_cpu.r[0];

    switch (slot) {
    case 0: /* AddRef */
    case 1: /* Release */
        g_cpu.r[0] = 1;
        break;

    case 2: /* CreateMedia(po, AEEMediaData* pMediaData, IMedia** ppIMedia) */
        {
            uint32_t pdata = g_cpu.r[1];
            uint32_t ppMedia = g_cpu.r[2];

            if (pdata && ppMedia && pdata < 0xFF000000) {
                uint32_t clsData = zmem_read32(pdata + 0);
                uint32_t pData = zmem_read32(pdata + 4);
                uint32_t dwSize = zmem_read32(pdata + 8);

                LOGI("IMediaUtil_CreateMedia: clsData=%u pData=0x%08X dwSize=%u",
                     clsData, pData, dwSize);

                uint32_t media_obj = 0;

                if (clsData == 1) { /* MMD_FILE_NAME */
                    char filename[256];
                    zmem_read_cstr(pData, filename, sizeof(filename));
                    LOGI("  MMD_FILE_NAME: '%s'", filename);

                    const char *ext = strrchr(filename, '.');
                    if (ext) {
                        if (strcasecmp(ext, ".mid") == 0 || strcasecmp(ext, ".midi") == 0) {
                            media_obj = zbrew_create_media_midi();
                        } else if (strcasecmp(ext, ".mp3") == 0) {
                            media_obj = zbrew_create_media_mp3();
                        } else if (strcasecmp(ext, ".adp") == 0 || strcasecmp(ext, ".ima") == 0) {
                            media_obj = zbrew_create_media_adpcm();
                        } else if (strcasecmp(ext, ".wav") == 0 || strcasecmp(ext, ".pcm") == 0) {
                            media_obj = zbrew_create_media_pcm();
                        } else {
                            media_obj = zbrew_create_media_pcm();
                        }
                    } else {
                        media_obj = zbrew_create_media_pcm();
                    }

                    if (media_obj) {
                        zmedia_t *m = media_from_obj(media_obj);
                        if (m && clsData == 1) {
                            char filepath[512];
                            snprintf(filepath, sizeof(filepath), "%s/%s", g_base_dir, filename);
                            FILE *fp = fopen(filepath, "rb");
                            if (fp) {
                                fseek(fp, 0, SEEK_END);
                                m->bytes = ftell(fp);
                                fseek(fp, 0, SEEK_SET);
                                if (m->bytes > 0) {
                                    m->addr = zheap_alloc(m->bytes);
                                    if (m->addr) {
                                        fread(zmem_host_ptr(m->addr, m->bytes), 1, m->bytes, fp);
                                        m->rate = 22050;
                                        m->state = ZMEDIA_STATE_READY;
                                    }
                                }
                                fclose(fp);
                            }
                        }
                    }
                } else if (clsData == 2) { /* MMD_BUFFER */
                    media_obj = zbrew_create_media_pcm();
                    if (media_obj) {
                        zmedia_t *m = media_from_obj(media_obj);
                        if (m) {
                            m->addr = pData;
                            m->bytes = dwSize;
                            m->rate = 22050;
                            m->state = ZMEDIA_STATE_READY;
                        }
                    }
                } else if (clsData == 3) { /* MMD_ISOURCE */
                    media_obj = zbrew_create_media_pcm();
                }

                if (media_obj && ppMedia < 0xFF000000) {
                    zmem_write32(ppMedia, media_obj);
                    LOGI("IMediaUtil_CreateMedia -> media=0x%08X", media_obj);
                } else {
                    LOGW("IMediaUtil_CreateMedia: falhou ao criar media");
                }

                g_cpu.r[0] = media_obj ? 0 : 1;
            } else {
                g_cpu.r[0] = 14;
            }
        }
        break;

    case 3: /* DetectType */
        g_cpu.r[0] = AEECLSID_MEDIAPCM;
        break;

    default:
        g_cpu.r[0] = ZBREW_EFAILED;
        break;
    }
}

uint32_t zbrew_create_media_util(void) {
    static const uint32_t vtbl[] = {
        ZT_MEDIAUTIL_ADDREF, ZT_MEDIAUTIL_RELEASE,
        ZT_MEDIAUTIL_CREATEMEDIA, ZT_MEDIAUTIL_DETECTTYPE
    };
    uint32_t obj = zbrew_make_interface(vtbl, 4, 0);
    if (obj) createinstance_cache_put(AEECLSID_MEDIAUTIL, obj);
    return obj;
}