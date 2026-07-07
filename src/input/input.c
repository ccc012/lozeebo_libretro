/* input.c - Estado do controle */
#include "input.h"

static uint32_t g_keys = 0;
static uint32_t g_prev_keys = 0;

void zinput_update(bool up, bool down, bool left, bool right,
                   bool a, bool b, bool y, bool start, bool select) {
    g_prev_keys = g_keys;
    g_keys = 0;
    if (up)     g_keys |= ZKEY_UP;
    if (down)   g_keys |= ZKEY_DOWN;
    if (left)   g_keys |= ZKEY_LEFT;
    if (right)  g_keys |= ZKEY_RIGHT;
    if (a)      g_keys |= ZKEY_A;
    if (b)      g_keys |= ZKEY_B;
    if (y)      g_keys |= ZKEY_C;
    if (start)  g_keys |= ZKEY_MENU;
    if (select) g_keys |= ZKEY_BACK;
}

uint32_t zinput_key_mask(void) {
    return g_keys;
}

uint32_t zinput_pressed(void) {
    return g_keys & ~g_prev_keys;
}

uint32_t zinput_released(void) {
    return g_prev_keys & ~g_keys;
}
