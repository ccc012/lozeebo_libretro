/* input.c - Estado do controle (2 players + analogico) */
#include "input.h"
#include "../memory/memory.h"

static uint32_t g_keys[2];
static uint32_t g_prev_keys[2];
static int16_t  g_ax[2], g_ay[2];
static int16_t  g_rx[2], g_ry[2];

void zinput_update(bool up, bool down, bool left, bool right,
                   bool a, bool b, bool y, bool start, bool select) {
    g_prev_keys[0] = g_keys[0];
    g_keys[0] = 0;
    if (up)     g_keys[0] |= ZKEY_UP;
    if (down)   g_keys[0] |= ZKEY_DOWN;
    if (left)   g_keys[0] |= ZKEY_LEFT;
    if (right)  g_keys[0] |= ZKEY_RIGHT;
    if (a)      g_keys[0] |= ZKEY_A;
    if (b)      g_keys[0] |= ZKEY_B;
    if (y)      g_keys[0] |= ZKEY_C;
    if (start)  g_keys[0] |= ZKEY_MENU;
    if (select) g_keys[0] |= ZKEY_BACK;
}

void zinput_update_analog(int player, int16_t ax, int16_t ay) {
    if (player >= 0 && player < 2) {
        g_ax[player] = ax;
        g_ay[player] = ay;
    }
}

void zinput_update_analog_right(int player, int16_t ax, int16_t ay) {
    if (player >= 0 && player < 2) {
        g_rx[player] = ax;
        g_ry[player] = ay;
    }
}

void zinput_update_p2(uint32_t keys) {
    g_prev_keys[1] = g_keys[1];
    g_keys[1] = keys;
}

uint32_t zinput_key_mask(void) {
    return g_keys[0];
}

uint32_t zinput_player_keys(int p) {
    return (p >= 0 && p < 2) ? g_keys[p] : 0;
}

uint32_t zinput_key_down(void) {
    return g_keys[0];
}

uint32_t zinput_pressed(void) {
    return g_keys[0] & ~g_prev_keys[0];
}

uint32_t zinput_released(void) {
    return g_prev_keys[0] & ~g_keys[0];
}

#define ZSTICK_CENTER 0x8000u
#define ZSTICK_MIN    0x0000u
#define ZSTICK_MAX    0xFFFFu

static uint32_t analog_to_axis(int16_t v) {
    if (v < -16384) return ZSTICK_MIN;
    if (v >  16384) return ZSTICK_MAX;
    return (uint32_t)(((int32_t)v + 32768) * 0xFFFF / 65535);
}

uint32_t zinput_left_stick_x(void) {
    if (g_keys[0] & ZKEY_LEFT)  return ZSTICK_MIN;
    if (g_keys[0] & ZKEY_RIGHT) return ZSTICK_MAX;
    if (g_ax[0] < -16384 || g_ax[0] > 16384) return analog_to_axis(g_ax[0]);
    return ZSTICK_CENTER;
}

uint32_t zinput_left_stick_y(void) {
    if (g_keys[0] & ZKEY_UP)   return ZSTICK_MIN;
    if (g_keys[0] & ZKEY_DOWN) return ZSTICK_MAX;
    if (g_ay[0] < -16384 || g_ay[0] > 16384) return analog_to_axis(g_ay[0]);
    return ZSTICK_CENTER;
}

uint32_t zinput_right_stick_x(void) {
    if (g_rx[0] < -16384 || g_rx[0] > 16384) return analog_to_axis(g_rx[0]);
    return ZSTICK_CENTER;
}

uint32_t zinput_right_stick_y(void) {
    if (g_ry[0] < -16384 || g_ry[0] > 16384) return analog_to_axis(g_ry[0]);
    return ZSTICK_CENTER;
}

static void hid_zero_pos(uint32_t p) {
    uint32_t i;
    for (i = 0; i < ZINPUT_HID_POS_WORDS; i++)
        zmem_write32(p + i * 4, 0);
}

void zinput_hid_fill_position(uint32_t p_pos,
                              uint32_t lx, uint32_t ly,
                              uint32_t rx, uint32_t ry) {
    if (!p_pos) return;
    hid_zero_pos(p_pos);
    zmem_write32(p_pos + 4, lx);
    zmem_write32(p_pos + 8, ly);
    zmem_write32(p_pos + 16, rx);
    zmem_write32(p_pos + 20, ry);
}

void zinput_hid_fill_min_position(uint32_t p_pos) {
    if (!p_pos) return;
    hid_zero_pos(p_pos);
}

void zinput_hid_fill_max_position(uint32_t p_pos) {
    if (!p_pos) return;
    hid_zero_pos(p_pos);
    zmem_write32(p_pos + 4, 0xFFFFu);
    zmem_write32(p_pos + 8, 0xFFFFu);
    zmem_write32(p_pos + 16, 0xFFFFu);
    zmem_write32(p_pos + 20, 0xFFFFu);
}

void zinput_hid_fill_axes_info(uint32_t p_axes) {
    uint32_t i;
    if (!p_axes) return;
    /* So 7 words (ate Rz). Alice stack gap=0x1C; 100B smash. */
    for (i = 1; i < ZINPUT_HID_POS_WORDS; i++)
        zmem_write32(p_axes + i * 4, 0xFFFFFFFFu);
    zmem_write32(p_axes + 0, 0);
    zmem_write32(p_axes + 4, 0x0106C4D0u);  /* LeftThumb X */
    zmem_write32(p_axes + 8, 0x0106C4D1u);  /* LeftThumb Y */
    zmem_write32(p_axes + 16, 0x0106C4CEu); /* RightThumb X */
    zmem_write32(p_axes + 20, 0x0106C4CFu); /* RightThumb Y */
}
