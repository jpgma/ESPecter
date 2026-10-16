#ifndef ESPECTER_WORLD_H
#define ESPECTER_WORLD_H

#include "actions.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORLD_VIEW 240
#define WORLD_TICK_MS 12
#define WORLD_BODY_W 8
#define WORLD_BODY_H 12
#define WORLD_SOLID_CAP 256

/* [world_init] Monta o corpo no spawn, o apron de [map_at] na slice de [arena_level], o checkpoint no spawn, o shuttle em x 464 e a câmera com a mola parada. Devolve 0 se [arena_ready] servir. */
int world_init(void);
/* [world_reset] Devolve o checkpoint ao spawn, recarrega o apron, encosta o shuttle em x 464 e põe o corpo lá. */
void world_reset(void);
/* [world_tick] Integra um tick fixo. O shuttle anda sempre. O rider soma esse dx antes das solves. move_down solta o latch e atravessa o one-way. O pit e o spike abrem 252 ms de morte e depois [body_place_at] no checkpoint. A saída congela o corpo até [world_reset]. A mola e [body_pose] só correm vivo. */
void world_tick(const actions *input);
/* [world_rect] Escreve a posição no mundo, em pixels, e o tamanho do Rect. */
void world_rect(int *x, int *y, int *w, int *h);
/* [world_body_pose] Escreve o clip, o quadro e a face. Face -1 pede o flip. */
void world_body_pose(int *clip, int *frame, int *face);
/* [world_camera] Escreve a origem da View em pixels do mundo. */
void world_camera(int *x, int *y);
/* [world_solid_count] Devolve quantos tiles set ou one-way cruzam a View. */
int world_solid_count(void);
/* [world_solid_rect] Escreve o tile set ou a faixa one-way que cruza a View. Fora da contagem, zera a saída. */
void world_solid_rect(int index, int *x, int *y, int *w, int *h);
/* [world_spike_rect] Escreve o spike em pixels do mundo. */
void world_spike_rect(int *x, int *y, int *w, int *h);
/* [world_checkpoint_rect] Escreve o volume do checkpoint em pixels do mundo. */
void world_checkpoint_rect(int *x, int *y, int *w, int *h);
/* [world_exit_rect] Escreve a saída em pixels do mundo. */
void world_exit_rect(int *x, int *y, int *w, int *h);
/* [world_shuttle_rect] Escreve o shuttle em pixels do mundo. */
void world_shuttle_rect(int *x, int *y, int *w, int *h);

#ifdef __cplusplus
}
#endif

#endif
