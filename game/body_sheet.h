#ifndef ESPECTER_BODY_SHEET_H
#define ESPECTER_BODY_SHEET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BODY_SHEET_W 16
#define BODY_SHEET_H 16
#define BODY_LAYER_COUNT 4

#define BODY_CLIP_IDLE 0
#define BODY_CLIP_WALK 1
#define BODY_CLIP_RUN 2
#define BODY_CLIP_RUN_TURN 3
#define BODY_CLIP_JUMP 4
#define BODY_CLIP_JUMP_TURN 5
#define BODY_CLIP_DASH 6
#define BODY_CLIP_WALL 7
#define BODY_CLIP_COUNT 8

/* [body_sheet_frames] Devolve quantos quadros o clip tem. Fora da faixa, devolve 0. */
int body_sheet_frames(int clip);
/* [body_sheet_rows] Devolve as 16 linhas 1bpp da layer do quadro. Fora da faixa, devolve 0. */
const uint16_t *body_sheet_rows(int clip, int frame, int layer);

#ifdef __cplusplus
}
#endif

#endif
