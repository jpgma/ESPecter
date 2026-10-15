#ifndef ESPECTER_ARENA_H
#define ESPECTER_ARENA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ARENA_BYTES 65536u
#define ARENA_WORLD_BYTES 4096u
#define ARENA_LEVEL_BYTES 32768u
#define ARENA_SCRATCH_BYTES 16384u

typedef struct arena_slice {
    uint8_t *data;
    size_t size;
} arena_slice;

/* Takes the block for the process lifetime. Never returned. Returns 0 on success. */
int arena_init(void *block, size_t capacity);

int arena_ready(void);
size_t arena_capacity(void);
size_t arena_offset(void);

arena_slice arena_world(void);
arena_slice arena_level(void);
arena_slice arena_scratch(void);

#ifdef __cplusplus
}
#endif

#endif
