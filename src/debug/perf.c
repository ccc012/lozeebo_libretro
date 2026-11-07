#include "perf.h"
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

typedef struct { uint64_t ticks, calls, pixels; } counter_t;
static counter_t g_counters[ZPERF_COUNT];
static int g_enabled = -1;
static LARGE_INTEGER g_freq;

static void report(void)
{
    static const char *names[ZPERF_COUNT] = {
        "raster_triangle", "sample_texture", "blend", "zfb_set_pixel",
        "CPU guest", "autopresent"
    };
    int i;
    fprintf(stderr, "ZEEMU PERF (QPC)\n");
    for (i = 0; i < ZPERF_COUNT; ++i)
        fprintf(stderr, "  %-16s %10.3f ms calls=%llu pixels=%llu\n", names[i],
                g_freq.QuadPart ? 1000.0 * (double)g_counters[i].ticks / (double)g_freq.QuadPart : 0.0,
                (unsigned long long)g_counters[i].calls,
                (unsigned long long)g_counters[i].pixels);
}

static void ensure_freq(void)
{
    static int atexit_set;
    if (!g_freq.QuadPart)
        QueryPerformanceFrequency(&g_freq);
    if (!atexit_set) {
        atexit(report);
        atexit_set = 1;
    }
}

int zperf_enabled(void)
{
    if (g_enabled < 0) {
        g_enabled = getenv("ZEEMU_PROFILE_CORE") != NULL;
        if (g_enabled) ensure_freq();
    }
    return g_enabled;
}

void zperf_set_option(int enabled)
{
    if (getenv("ZEEMU_PROFILE_CORE") != NULL)
        enabled = 1;
    if (enabled)
        ensure_freq();
    g_enabled = enabled ? 1 : 0;
}

uint64_t zperf_begin(void)
{
    LARGE_INTEGER now;
    if (!zperf_enabled()) return 0;
    QueryPerformanceCounter(&now);
    return (uint64_t)now.QuadPart;
}

void zperf_end(zperf_id_t id, uint64_t start, uint64_t pixels)
{
    LARGE_INTEGER now;
    if (!start) return;
    QueryPerformanceCounter(&now);
    g_counters[id].ticks += (uint64_t)now.QuadPart - start;
    g_counters[id].calls++;
    g_counters[id].pixels += pixels;
}
