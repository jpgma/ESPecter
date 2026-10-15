#ifndef ESPECTER_WORLD_H
#define ESPECTER_WORLD_H

#include "actions.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORLD_VIEW 240
#define WORLD_RECT 16

/* [world_init] Monta o Rect na slice de [arena_world]. Devolve 0 se [arena_ready] servir. */
int world_init(void);
/* [world_tick] Move o Rect com as actions de [actions_capture]. */
void world_tick(const actions *input);
/* [world_rect] Escreve posição e tamanho do Rect. */
void world_rect(int *x, int *y, int *w, int *h);

#ifdef __cplusplus
}
#endif

#endif
