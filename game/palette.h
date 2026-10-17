#ifndef ESPECTER_PALETTE_H
#define ESPECTER_PALETTE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PALETTE_COUNT 256
#define PALETTE_BODY_LEAF 95
#define PALETTE_BODY_GLASS 79
#define PALETTE_BODY_EMBER 143
#define PALETTE_BODY_KILN 159
#define PALETTE_TILE_MID 140
#define PALETTE_TILE_DEEP 137
#define PALETTE_TILE_EDGE 17
#define PALETTE_TILE_INK 16

extern const uint16_t palette_rgb565[PALETTE_COUNT];

#ifdef __cplusplus
}
#endif

#endif
