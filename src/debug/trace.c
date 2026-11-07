/* trace.c - Ring buffer dos ultimos PCs executados (debug de travas) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"

#define ZTRACE_SIZE 64

static uint32_t g_trace[ZTRACE_SIZE];
static unsigned g_trace_pos = 0;

/* Histograma de paginas (PC>>12): onde o guest passa o tempo. */
#define ZHIST_BUCKETS 4096
static uint32_t g_hist[ZHIST_BUCKETS];
static uint32_t g_hist_done = 0;

void ztrace_pc(uint32_t pc);
void ztrace_dump(void);
void ztrace_set_profile_pc(int enabled);
static void zhist_dump(void);

/* Release gate: -1 lazy; 0/1 from option (env ganha). */
static int g_trace_enabled = -1;
/* Histograma: -1 lazy; so PROFILE_PC / option profile. */
static int g_profile_pc = -1;

void ztrace_set_profile_pc(int enabled)
{
    /* Env ZEEMU_PROFILE_PC / ZEEMU_TRACE_PC ganha se setada. */
    int env_prof = getenv("ZEEMU_PROFILE_PC") != NULL;
    int env_trace = getenv("ZEEMU_TRACE_PC") != NULL;
    if (env_prof || env_trace) {
        g_trace_enabled = 1;
        g_profile_pc = env_prof ? 1 : 0;
        return;
    }
    g_trace_enabled = enabled ? 1 : 0;
    g_profile_pc = enabled ? 1 : 0;
}

void ztrace_pc(uint32_t pc) {
#if defined(NDEBUG)
    /* Release: ring + hist so sob ZEEMU_TRACE_PC / ZEEMU_PROFILE_PC / option. */
    if (g_trace_enabled < 0)
        g_trace_enabled = (getenv("ZEEMU_TRACE_PC") != NULL) ||
                          (getenv("ZEEMU_PROFILE_PC") != NULL);
    if (!g_trace_enabled) return;
#endif
    {
        g_trace[g_trace_pos++ & (ZTRACE_SIZE - 1)] = pc;
        if (g_profile_pc < 0)
            g_profile_pc = getenv("ZEEMU_PROFILE_PC") != NULL;
        if (!g_profile_pc) return;
        if ((pc & ~0xFFFu) < 0x01000000u)
            g_hist[(pc >> 12) & (ZHIST_BUCKETS - 1)]++;
        if (!g_hist_done) {
            static uint32_t acc = 0;
            if (++acc < (1u << 22))
                return;
            g_hist_done = 1;
        } else {
            static uint32_t win = 0;
            if (++win < (1u << 22))
                return;
            win = 0;
        }
        zhist_dump();
        ztrace_dump();
        memset(g_hist, 0, sizeof(g_hist));
    }
}

static void zhist_dump(void) {
    int best[12] = { 0 }, i, j;
    uint32_t bcount[12] = { 0 };
    for (i = 0; i < ZHIST_BUCKETS; i++) {
        uint32_t v = g_hist[i];
        if (!v) continue;
        for (j = 0; j < 12; j++) {
            if (v > bcount[j]) {
                int k;
                for (k = 11; k > j; k--) {
                    best[k] = best[k - 1];
                    bcount[k] = bcount[k - 1];
                }
                best[j] = i;
                bcount[j] = v;
                break;
            }
        }
    }
    LOGI("hist: paginas PCs (top 12):");
    for (i = 0; i < 12; i++)
        if (bcount[i])
            LOGI("hist: 0x%07X*: %u", best[i] << 12, bcount[i]);
}

void ztrace_dump(void) {
    unsigned i;
    LOGI("trace: ultimos %d PCs (mais recente por ultimo):", ZTRACE_SIZE);
    for (i = 0; i < ZTRACE_SIZE; i += 8) {
        LOGI("  0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X",
             g_trace[(g_trace_pos + i)     & (ZTRACE_SIZE - 1)],
             g_trace[(g_trace_pos + i + 1) & (ZTRACE_SIZE - 1)],
             g_trace[(g_trace_pos + i + 2) & (ZTRACE_SIZE - 1)],
             g_trace[(g_trace_pos + i + 3) & (ZTRACE_SIZE - 1)],
             g_trace[(g_trace_pos + i + 4) & (ZTRACE_SIZE - 1)],
             g_trace[(g_trace_pos + i + 5) & (ZTRACE_SIZE - 1)],
             g_trace[(g_trace_pos + i + 6) & (ZTRACE_SIZE - 1)],
             g_trace[(g_trace_pos + i + 7) & (ZTRACE_SIZE - 1)]);
    }
}
