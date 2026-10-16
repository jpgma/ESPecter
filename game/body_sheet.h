#ifndef ESPECTER_BODY_SHEET_H
#define ESPECTER_BODY_SHEET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BODY_SHEET_W 16
#define BODY_SHEET_H 16

#define BODY_CLIP_IDLE 0
#define BODY_CLIP_WALK 1
#define BODY_CLIP_JOG 2
#define BODY_CLIP_RUN 3
#define BODY_CLIP_RUN_TURN 4
#define BODY_CLIP_JUMP 5
#define BODY_CLIP_JUMP_TURN 6
#define BODY_CLIP_DASH 7
#define BODY_CLIP_WALL 8
#define BODY_CLIP_COUNT 9

/* [body_sheet_frames] Devolve quantos quadros o clip tem. Fora da faixa, devolve 0. */
int body_sheet_frames(int clip);
/* [body_sheet_frame] Devolve os 256 índices do quadro. Fora da faixa, devolve 0. */
const uint8_t *body_sheet_frame(int clip, int frame);

#ifdef __cplusplus
}
#endif

#endif
