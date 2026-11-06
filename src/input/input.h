/* input.h - Mapeamento RetroPad -> teclas do Zeebo */
#ifndef ZEEBO_INPUT_H
#define ZEEBO_INPUT_H

#include <stdint.h>
#include <stdbool.h>

/* Bitmask retornado pelo trap ZT_GET_KEYS */
#define ZKEY_UP     (1u << 0)
#define ZKEY_DOWN   (1u << 1)
#define ZKEY_LEFT   (1u << 2)
#define ZKEY_RIGHT  (1u << 3)
#define ZKEY_A      (1u << 4)   /* RetroPad A */
#define ZKEY_B      (1u << 5)   /* RetroPad B */
#define ZKEY_C      (1u << 6)   /* RetroPad Y */
#define ZKEY_MENU   (1u << 7)   /* RetroPad Start */
#define ZKEY_BACK   (1u << 8)   /* RetroPad Select */

/* Chamado pelo retro_run com o estado de cada botao RetroPad */
void zinput_update(bool up, bool down, bool left, bool right,
                   bool a, bool b, bool y, bool start, bool select);

/* Atualiza eixos analogicos reais do RetroPad (player 0 ou 1) */
void zinput_update_analog(int player, int16_t ax, int16_t ay);
/* Atualiza teclas do player 2 (bitmask direto) */
void zinput_update_p2(uint32_t keys);

/* Estado atual (lido pelo trap HLE) */
uint32_t zinput_key_mask(void);
uint32_t zinput_player_keys(int p);
uint32_t zinput_key_down(void);

/* Eixos analogicos (0..0xffff, centro 0x8000). Prioridade: D-pad > analogico.
 * Jogos como Family Pack navegam o menu via IHIDDevice position em vez de
 * botoes; espelhar o D-pad no stick esquerdo faz o jogo enxergar o
 * cursor analogico na tela de titulo. */
uint32_t zinput_left_stick_x(void);
uint32_t zinput_left_stick_y(void);

/* Teclas que mudaram desde o frame anterior (para eventos EVT_KEY) */
uint32_t zinput_pressed(void);   /* transicao 0->1 */
uint32_t zinput_released(void);  /* transicao 1->0 */

/* AEEHIDPositionInfo campos usados: RelativeAxes+X+Y+Z+Rx+Ry+Rz = 28B.
 * SDK struct tem 100B (stride heap 0x64), mas Alice passa temps de
 * stack com gap 0x1C — zeroar 100B smash LR/heap → TTDMM. So tocamos
 * ate Rz; X/Y/Rx/Ry bastam pra FP/AH/Alice. */
#define ZINPUT_HID_POS_WORDS 7u

void zinput_hid_fill_position(uint32_t p_pos,
                              uint32_t lx, uint32_t ly,
                              uint32_t rx, uint32_t ry);
void zinput_hid_fill_min_position(uint32_t p_pos);
void zinput_hid_fill_max_position(uint32_t p_pos);
void zinput_hid_fill_axes_info(uint32_t p_axes);

#endif /* ZEEBO_INPUT_H */
