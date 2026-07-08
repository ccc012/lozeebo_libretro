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

/* Estado atual (lido pelo trap HLE) */
uint32_t zinput_key_mask(void);
uint32_t zinput_key_down(void);

/* Teclas que mudaram desde o frame anterior (para eventos EVT_KEY) */
uint32_t zinput_pressed(void);   /* transicao 0->1 */
uint32_t zinput_released(void);  /* transicao 1->0 */

#endif /* ZEEBO_INPUT_H */
