/* isound.c - ISound: efeitos sonoros PCM via mixer */
#include "brew.h"
#include "../cpu/cpu.h"
#include "../audio/audio.h"
#include "../debug/log.h"

void zbrew_handle_sound(uint32_t id) {
    switch (id) {
    case ZT_SND_ADDREF:
    case ZT_SND_RELEASE:
        g_cpu.r[0] = 1;
        break;

    case ZT_SND_PLAY: {
        /* R1=addr R2=bytes R3=rate [SP]=formato [SP+4]=loop */
        uint32_t addr  = g_cpu.r[1];
        uint32_t bytes = g_cpu.r[2];
        uint32_t rate  = g_cpu.r[3];
        uint32_t fmt   = zbrew_stack_arg(0);
        uint32_t loop  = zbrew_stack_arg(1);
        int voice = zaudio_play_pcm(addr, bytes, rate,
                                    fmt == 1 ? ZPCM_S16 : ZPCM_U8,
                                    loop != 0, 255);
        LOGD("ISound_Play(0x%08X, %u bytes, %u Hz) -> voz %d",
             addr, bytes, rate, voice);
        g_cpu.r[0] = (uint32_t)voice;
        break;
    }

    case ZT_SND_STOP:
        zaudio_stop((int)g_cpu.r[1]);
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;

    case ZT_SND_SETVOLUME:
        zaudio_set_volume((int)g_cpu.r[1], (uint8_t)g_cpu.r[2]);
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;
    }
}
