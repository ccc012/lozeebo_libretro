/* isound.c - ISound: efeitos sonoros (stubs compativeis com AEEISound.h) */
#include "brew.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../audio/audio.h"
#include "../debug/log.h"

void zbrew_handle_sound(uint32_t id) {
    uint32_t slot = id - ZT_SND_ADDREF;

    switch (slot) {
    case 0: /* AddRef */
    case 1: /* Release */
        g_cpu.r[0] = 1;
        break;

    case 12: /* SetVolume(R1=vol) */
        /* volume global ignorado (mixer usa volume por voz) */
        g_cpu.r[0] = 0; /* SUCCESS */
        break;

    case 13: /* GetVolume(R1=pVol) */
        if (g_cpu.r[1] && g_cpu.r[1] < 0x40000000u)
            zmem_write16(g_cpu.r[1], 255);
        g_cpu.r[0] = 0;
        break;

    case 2:  /* RegisterNotify */
    case 3:  /* Set */
    case 4:  /* Get */
    case 5:  /* SetDevice */
    case 6:  /* PlayTone */
    case 7:  /* PlayToneList */
    case 8:  /* PlayFreqTone */
    case 9:  /* StopTone */
    case 10: /* Vibrate */
    case 11: /* StopVibrate */
    case 14: /* GetResourceCtl */
    default:
        g_cpu.r[0] = 0; /* SUCCESS */
        break;
    }
}