#ifndef ESPECTER_WORLD_H
#define ESPECTER_WORLD_H

#include "actions.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORLD_VIEW 240
#define WORLD_RECT 16

/* Returns 0 when the world slice is in place. */
int world_init(void);
void world_tick(const actions *input);
void world_rect(int *x, int *y, int *w, int *h);

#ifdef __cplusplus
}
#endif

#endif
