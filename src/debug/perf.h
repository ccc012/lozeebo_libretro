#ifndef ZEEBO_PERF_H
#define ZEEBO_PERF_H

#include <stdint.h>

typedef enum {
    ZPERF_RASTER_TRIANGLE,
    ZPERF_SAMPLE_TEXTURE,
    ZPERF_BLEND,
    ZPERF_ZFB_SET_PIXEL,
    ZPERF_CPU_GUEST,
    ZPERF_AUTOPRESENT,
    ZPERF_COUNT
} zperf_id_t;

uint64_t zperf_begin(void);
void zperf_end(zperf_id_t id, uint64_t start, uint64_t pixels);
int zperf_enabled(void);
/* Core option; ZEEMU_PROFILE_CORE env ganha se setada. */
void zperf_set_option(int enabled);

#endif
