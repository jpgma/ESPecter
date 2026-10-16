#ifndef ESPECTER_WORLD_H
#define ESPECTER_WORLD_H

#include "actions.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORLD_VIEW 240
#define WORLD_RECT 16

/* [world_init] Monta o corpo no centro da célula 0, o chão e as plataformas na slice de [arena_level], e a câmera centrada com a mola parada. Devolve 0 se [arena_ready] servir. */
int world_init(void);
/* [world_reset] Re-rola as quatro plataformas, devolve o corpo ao centro da célula 0 e encosta a câmera. */
void world_reset(void);
/* [world_tick] Integra um tick fixo; move_down solta o latch antes da corrida e a mola da câmera segue o centro do corpo. */
void world_tick(const actions *input);
/* [world_rect] Escreve a posição no mundo, em pixels, e o tamanho do Rect. */
void world_rect(int *x, int *y, int *w, int *h);
/* [world_camera] Escreve a origem da View em pixels do mundo. */
void world_camera(int *x, int *y);
/* [world_solid_count] Devolve quantos AABB estáticos a slice guarda. */
int world_solid_count(void);
/* [world_solid_rect] Escreve o AABB index em pixels do mundo. Fora da contagem, zera a saída. */
void world_solid_rect(int index, int *x, int *y, int *w, int *h);

#ifdef __cplusplus
}
#endif

#endif
