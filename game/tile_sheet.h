#ifndef ESPECTER_TILE_SHEET_H
#define ESPECTER_TILE_SHEET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TILE_SHEET_W 16
#define TILE_SHEET_H 16
#define TILE_SHEET_COUNT 25

/* [tile_sheet_at] Devolve os 256 índices do kind. Sem quadro, devolve 0. */
const uint8_t *tile_sheet_at(int kind);

#ifdef __cplusplus
}
#endif

#endif
