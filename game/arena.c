#include "arena.h"

#include <stdint.h>

static uint8_t *arena_base;
static size_t arena_cap;
static size_t arena_at;
static arena_slice slice_world;
static arena_slice slice_level;
static arena_slice slice_scratch;

static uint8_t *internal_base;
static size_t internal_cap;
static size_t internal_at;
static arena_slice slice_bands;
static arena_slice slice_pixels;
static arena_slice slice_snapshot;
static arena_slice slice_stack_sim;
static arena_slice slice_stack_present;
static arena_slice slice_stack_backlight;
static arena_slice slice_stack_pad;
static arena_slice slice_tcb_sim;
static arena_slice slice_tcb_present;
static arena_slice slice_tcb_backlight;
static arena_slice slice_tcb_pad;
static arena_slice slice_engine;
static arena_slice slice_drawer;

/* [align_up] Sobe o valor até o próximo múltiplo do alinhamento. */
static size_t align_up(size_t value, size_t align)
{
    return (value + (align - 1u)) & ~(align - 1u);
}

/* [carve] Recorta uma slice alinhada de um block. Devolve 0 se couber. */
static int carve(uint8_t *base, size_t cap, size_t *at, size_t align, arena_slice *out, size_t size)
{
    size_t next;

    next = align_up(*at, align);
    if (size > cap || next > cap - size) {
        return 1;
    }
    out->data = base + next;
    out->size = size;
    *at = next + size;
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
    if (carve(arena_base, arena_cap, &arena_at, 8u, &slice_world, ARENA_WORLD_BYTES) != 0) {
        return 1;
    }
    if (carve(arena_base, arena_cap, &arena_at, 8u, &slice_level, ARENA_LEVEL_BYTES) != 0) {
        return 1;
    }
    if (carve(arena_base, arena_cap, &arena_at, 8u, &slice_scratch, ARENA_SCRATCH_BYTES) != 0) {
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

/* [arena_internal_init] Fixa o block interno até o fim do processo. Devolve 0 se couber. */
int arena_internal_init(void *block, size_t capacity)
{
    if (block == 0 || capacity < ARENA_INTERNAL_BYTES) {
        return 1;
    }
    internal_base = (uint8_t *)block;
    internal_cap = capacity;
    internal_at = 0;
    if (carve(internal_base, internal_cap, &internal_at, 16u, &slice_bands, ARENA_BANDS_BYTES) != 0) {
        return 1;
    }
    if (carve(internal_base, internal_cap, &internal_at, 16u, &slice_pixels, ARENA_PIXELS_BYTES) != 0) {
        return 1;
    }
    if (carve(internal_base, internal_cap, &internal_at, 16u, &slice_snapshot, ARENA_SNAPSHOT_BYTES) != 0) {
        return 1;
    }
    if (carve(internal_base, internal_cap, &internal_at, 16u, &slice_stack_sim, ARENA_STACK_SIM_BYTES) != 0) {
        return 1;
    }
    if (carve(internal_base, internal_cap, &internal_at, 16u, &slice_stack_present, ARENA_STACK_PRESENT_BYTES) != 0) {
        return 1;
    }
    if (carve(internal_base, internal_cap, &internal_at, 16u, &slice_stack_backlight, ARENA_STACK_BACKLIGHT_BYTES) != 0) {
        return 1;
    }
    if (carve(internal_base, internal_cap, &internal_at, 16u, &slice_stack_pad, ARENA_STACK_PAD_BYTES) != 0) {
        return 1;
    }
    if (carve(internal_base, internal_cap, &internal_at, 16u, &slice_tcb_sim, ARENA_TCB_BYTES) != 0) {
        return 1;
    }
    if (carve(internal_base, internal_cap, &internal_at, 16u, &slice_tcb_present, ARENA_TCB_BYTES) != 0) {
        return 1;
    }
    if (carve(internal_base, internal_cap, &internal_at, 16u, &slice_tcb_backlight, ARENA_TCB_BYTES) != 0) {
        return 1;
    }
    if (carve(internal_base, internal_cap, &internal_at, 16u, &slice_tcb_pad, ARENA_TCB_BYTES) != 0) {
        return 1;
    }
    if (carve(internal_base, internal_cap, &internal_at, 16u, &slice_engine, ARENA_ENGINE_BYTES) != 0) {
        return 1;
    }
    if (carve(internal_base, internal_cap, &internal_at, 16u, &slice_drawer, ARENA_DRAWER_BYTES) != 0) {
        return 1;
    }
    return 0;
}

/* [arena_internal_capacity] Devolve o tamanho total do block interno. */
size_t arena_internal_capacity(void)
{
    return internal_cap;
}

/* [arena_internal_offset] Devolve quantos bytes já foram recortados no block interno. */
size_t arena_internal_offset(void)
{
    return internal_at;
}

/* [arena_bands] Devolve a slice dos bands DMA. */
arena_slice arena_bands(void)
{
    return slice_bands;
}

/* [arena_pixels] Devolve a slice do buffer 8bpp. */
arena_slice arena_pixels(void)
{
    return slice_pixels;
}

/* [arena_snapshot] Devolve a slice do draw snapshot. */
arena_slice arena_snapshot(void)
{
    return slice_snapshot;
}

/* [arena_stack_sim] Devolve a stack da task sim. */
arena_slice arena_stack_sim(void)
{
    return slice_stack_sim;
}

/* [arena_stack_present] Devolve a stack da task present. */
arena_slice arena_stack_present(void)
{
    return slice_stack_present;
}

/* [arena_stack_backlight] Devolve a stack da task do backlight. */
arena_slice arena_stack_backlight(void)
{
    return slice_stack_backlight;
}

/* [arena_stack_pad] Devolve a stack da task do BOOT e do PLUS. */
arena_slice arena_stack_pad(void)
{
    return slice_stack_pad;
}

/* [arena_tcb_sim] Devolve o StaticTask_t da task sim. */
arena_slice arena_tcb_sim(void)
{
    return slice_tcb_sim;
}

/* [arena_tcb_present] Devolve o StaticTask_t da task present. */
arena_slice arena_tcb_present(void)
{
    return slice_tcb_present;
}

/* [arena_tcb_backlight] Devolve o StaticTask_t da task do backlight. */
arena_slice arena_tcb_backlight(void)
{
    return slice_tcb_backlight;
}

/* [arena_tcb_pad] Devolve o StaticTask_t da task do BOOT e do PLUS. */
arena_slice arena_tcb_pad(void)
{
    return slice_tcb_pad;
}

/* [arena_engine] Devolve a slice do Engine. */
arena_slice arena_engine(void)
{
    return slice_engine;
}

/* [arena_drawer] Devolve a slice do drawer. */
arena_slice arena_drawer(void)
{
    return slice_drawer;
}
