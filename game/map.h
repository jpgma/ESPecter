#ifndef ESPECTER_MAP_H
#define ESPECTER_MAP_H

#include <stdint.h>

#define MAP_TILE 16
#define MAP_CELL_W 15
#define MAP_CELL_H 16
#define MAP_CELLS 8
#define MAP_W (MAP_CELLS * MAP_CELL_W)
#define MAP_H MAP_CELL_H

#define MAP_EMPTY 0
#define MAP_SET 1
#define MAP_SPIKE 2
#define MAP_CHECKPOINT 3
#define MAP_EXIT 4

#define MAP_FLOOR_ROW 14
#define MAP_FLOOR_Y (MAP_FLOOR_ROW * MAP_TILE)
#define MAP_CP_TX 26
#define MAP_CP_TY 13

/* [map_at] Devolve o kind do tile. Fora da faixa, devolve vazio. */
int map_at(int tx, int ty);
/* [map_find] Escreve o primeiro tile desse kind. Devolve 0 se não achar. */
int map_find(int kind, int *tx, int *ty);

#endif
