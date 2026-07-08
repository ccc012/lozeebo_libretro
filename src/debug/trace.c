/* trace.c - Ring buffer dos ultimos PCs executados (debug de travas) */
#include <stdint.h>
#include "log.h"

#define ZTRACE_SIZE 64

static uint32_t g_trace[ZTRACE_SIZE];
static unsigned g_trace_pos = 0;

void ztrace_pc(uint32_t pc);
void ztrace_dump(void);

void ztrace_pc(uint32_t pc) {
    g_trace[g_trace_pos++ & (ZTRACE_SIZE - 1)] = pc;
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
