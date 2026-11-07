/* Standalone: see CMake target audio_adpcm_rate. No game assets required. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/audio/audio_internal.h"
#include "../src/audio/adpcm.h"
#include "../src/debug/log.h"

static unsigned char memory[16] = {0xE8, 0x03, 0, 0, 0x77, 0x77, 0x77, 0x77};
uint8_t zmem_read8(uint32_t a) { if (a >= sizeof(memory)) abort(); return memory[a]; }
uint16_t zmem_read16(uint32_t a) { return zmem_read8(a) | (zmem_read8(a + 1) << 8); }
void zlog(enum zlog_level level, const char *fmt, ...) { (void)level; (void)fmt; }
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void check_rate(uint32_t rate, unsigned frames, bool loop) {
    struct zimad_state st = {0}, reference = {0};
    int16_t expected[9], out[80];
    zimad_set_stream(&reference, 0, 8, 8, 9);
    for (unsigned i = 0; i < 9; ++i) CHECK(zimad_next(&reference, &expected[i]));
    zimad_set_stream(&st, 0, 8, 8, 9);
    zaudio_reset();
    int v = zaudio_play_pcm(0, 8, rate, ZPCM_ADPCM, loop, 255);
    CHECK(v >= 0);
    zaudio_voice_internal(v)->adpcm = &st;
    /* Split calls verify retained sample and fractional phase across batches. */
    zaudio_render(out, 3);
    zaudio_render(out + 6, frames - 3);
    for (unsigned f = 0; f < frames; ++f) {
        unsigned idx = (unsigned)(((uint64_t)f * rate) / ZAUDIO_SAMPLE_RATE);
        int16_t sample = loop ? expected[idx % 9] : (idx < 9 ? expected[idx] : 0);
        CHECK(out[f * 2] == sample);
        CHECK(out[f * 2 + 1] == sample);
    }
}
int main(void) {
    struct zimad_state clipping = {0};
    int16_t sample;
    memory[0] = 0xF8; memory[1] = 0x7F; memory[2] = 88;
    zimad_set_stream(&clipping, 0, 8, 8, 9);
    CHECK(zimad_next(&clipping, &sample) && sample == 32760);
    CHECK(zimad_next(&clipping, &sample) && sample == 32767);
    memory[0] = 0xE8; memory[1] = 0x03; memory[2] = 0;
    check_rate(22050, 20, false);
    check_rate(44100, 12, false);
    check_rate(88200, 7, false);
    check_rate(22050, 36, true);
    puts("PASS ADPCM 22050/44100/88200 Hz, EOF, loop, batch continuity");
    return 0;
}
