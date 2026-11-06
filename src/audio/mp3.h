/* mp3.h — Decoder MP3 via dr_mp3 */
#ifndef ZEEBO_MP3_H
#define ZEEBO_MP3_H

#include <stdint.h>
#include <stdbool.h>

void zmp3_init(void);
void zmp3_shutdown(void);
int  zmp3_play(uint32_t addr, uint32_t bytes, bool loop);
void zmp3_stop(int stream);
void zmp3_stop_all(void);
void zmp3_restart(int stream);
void zmp3_fill_voice(int stream, int16_t *out, uint32_t frames);
void zmp3_render_all(int16_t *out, uint32_t frames);
bool zmp3_is_active(int stream);

#endif /* ZEEBO_MP3_H */
