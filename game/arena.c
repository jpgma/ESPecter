#include "arena.h"

#include <stdint.h>

static uint8_t *arena_base;
static size_t arena_cap;
static size_t arena_at;
static arena_slice slice_world;
static arena_slice slice_level;
static arena_slice slice_scratch;

/* [align_up] Sobe o valor até o próximo múltiplo do alinhamento. */
static size_t align_up(size_t value, size_t align)
{
    return (value + (align - 1u)) & ~(align - 1u);
}

/* [carve] Recorta uma slice alinhada. Devolve 0 se couber. */
static int carve(arena_slice *out, size_t size)
{
    size_t at;

    at = align_up(arena_at, 8u);
    if (size > arena_cap || at > arena_cap - size) {
        return 1;
    }
    out->data = arena_base + at;
    out->size = size;
    arena_at = at + size;
    return 0;
}

/* [arena_init] Fixa o block até o fim do processo. Devolve 0 se couber. */
int arena_init(void *block, size_t capacity)
{
    if (block == 0 || capacity < ARENA_BYTES) {
        return 1;
    }
    arena_base = (uint8_t *)block;
    arena_cap = capacity;
    arena_at = 0;
    if (carve(&slice_world, ARENA_WORLD_BYTES) != 0) {
        return 1;
    }
    if (carve(&slice_level, ARENA_LEVEL_BYTES) != 0) {
        return 1;
    }
    if (carve(&slice_scratch, ARENA_SCRATCH_BYTES) != 0) {
        return 1;
    }
    return 0;
}

/* [arena_ready] Devolve 1 se a arena já foi inicializada. */
int arena_ready(void)
{
    return arena_base != 0;
}

/* [arena_capacity] Devolve o tamanho total do block. */
size_t arena_capacity(void)
{
    return arena_cap;
}

/* [arena_offset] Devolve quantos bytes já foram recortados. */
size_t arena_offset(void)
{
    return arena_at;
}

/* [arena_world] Devolve a slice fixa do world. */
arena_slice arena_world(void)
{
    return slice_world;
}

/* [arena_level] Devolve a slice fixa do level. */
arena_slice arena_level(void)
{
    return slice_level;
}

/* [arena_scratch] Devolve a slice fixa de scratch. */
arena_slice arena_scratch(void)
{
    return slice_scratch;
}
