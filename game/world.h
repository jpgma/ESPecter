#ifndef ESPECTER_WORLD_H
#define ESPECTER_WORLD_H

#include "actions.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORLD_VIEW 240
#define WORLD_RECT 16

/* [world_init] Monta o corpo no centro da slice de [arena_world] e copia os sólidos para a slice de [arena_level]. Devolve 0 se [arena_ready] servir. */
int world_init(void);
/* [world_tick] Integra um tick fixo; move_down solta o latch antes da corrida. */
void world_tick(const actions *input);
/* [world_rect] Escreve a posição em pixels e o tamanho do Rect. */
void world_rect(int *x, int *y, int *w, int *h);
/* [world_solid_count] Devolve quantos AABB estáticos [world_init] copiou. */
int world_solid_count(void);
/* [world_solid_rect] Escreve o AABB index em pixels. Fora da contagem, zera a saída. */
void world_solid_rect(int index, int *x, int *y, int *w, int *h);

#ifdef __cplusplus
}
#endif

#endif
