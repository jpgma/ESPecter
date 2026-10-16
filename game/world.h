#ifndef ESPECTER_WORLD_H
#define ESPECTER_WORLD_H

#include "actions.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORLD_VIEW 240
#define WORLD_BODY_W 8
#define WORLD_BODY_H 12

/* [world_init] Monta o corpo no centro da célula 0, o chão, os muros e as plataformas na slice de [arena_level], e a câmera centrada com a mola parada. Devolve 0 se [arena_ready] servir. */
int world_init(void);
/* [world_reset] Re-rola os dois muros e as seis plataformas, devolve o corpo ao centro da célula 0 e encosta a câmera. */
void world_reset(void);
/* [world_tick] Integra um tick fixo; move_down solta o latch antes da corrida, a mola da câmera segue o meio horizontal e a linha do chão, e [body_pose] escolhe o clip. */
void world_tick(const actions *input);
/* [world_rect] Escreve a posição no mundo, em pixels, e o tamanho do Rect. */
void world_rect(int *x, int *y, int *w, int *h);
/* [world_body_pose] Escreve o clip, o quadro e a face. Face -1 pede o flip. */
void world_body_pose(int *clip, int *frame, int *face);
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
