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

/* [arena_init] Fixa o block até o fim do processo. Devolve 0 se couber. */
int arena_init(void *block, size_t capacity);

/* [arena_ready] Devolve 1 se a arena já foi inicializada. */
int arena_ready(void);
/* [arena_capacity] Devolve o tamanho total do block. */
size_t arena_capacity(void);
/* [arena_offset] Devolve quantos bytes já foram recortados. */
size_t arena_offset(void);

/* [arena_world] Devolve a slice fixa do world. */
arena_slice arena_world(void);
/* [arena_level] Devolve a slice fixa do level. */
arena_slice arena_level(void);
/* [arena_scratch] Devolve a slice fixa de scratch. */
arena_slice arena_scratch(void);

#ifdef __cplusplus
}
#endif

#endif
