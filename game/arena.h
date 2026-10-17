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

#define ARENA_INTERNAL_BYTES 208896u
#define ARENA_BANDS_BYTES 115200u
#define ARENA_PIXELS_BYTES 57600u
#define ARENA_SNAPSHOT_BYTES 8948u
#define ARENA_STACK_SIM_BYTES 3072u
#define ARENA_STACK_PRESENT_BYTES 4096u
#define ARENA_STACK_BACKLIGHT_BYTES 3072u
#define ARENA_STACK_PAD_BYTES 2048u
#define ARENA_TCB_BYTES 352u
#define ARENA_ENGINE_BYTES 4096u
#define ARENA_DRAWER_BYTES 256u

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

/* [arena_internal_init] Fixa o block interno até o fim do processo. Devolve 0 se couber. */
int arena_internal_init(void *block, size_t capacity);
/* [arena_internal_capacity] Devolve o tamanho total do block interno. */
size_t arena_internal_capacity(void);
/* [arena_internal_offset] Devolve quantos bytes já foram recortados no block interno. */
size_t arena_internal_offset(void);

/* [arena_bands] Devolve a slice dos bands DMA. */
arena_slice arena_bands(void);
/* [arena_pixels] Devolve a slice do buffer 8bpp. */
arena_slice arena_pixels(void);
/* [arena_snapshot] Devolve a slice do draw snapshot. */
arena_slice arena_snapshot(void);
/* [arena_stack_sim] Devolve a stack da task sim. */
arena_slice arena_stack_sim(void);
/* [arena_stack_present] Devolve a stack da task present. */
arena_slice arena_stack_present(void);
/* [arena_stack_backlight] Devolve a stack da task do backlight. */
arena_slice arena_stack_backlight(void);
/* [arena_stack_pad] Devolve a stack da task do BOOT e do PLUS. */
arena_slice arena_stack_pad(void);
/* [arena_tcb_sim] Devolve o StaticTask_t da task sim. */
arena_slice arena_tcb_sim(void);
/* [arena_tcb_present] Devolve o StaticTask_t da task present. */
arena_slice arena_tcb_present(void);
/* [arena_tcb_backlight] Devolve o StaticTask_t da task do backlight. */
arena_slice arena_tcb_backlight(void);
/* [arena_tcb_pad] Devolve o StaticTask_t da task do BOOT e do PLUS. */
arena_slice arena_tcb_pad(void);
/* [arena_engine] Devolve a slice do Engine. */
arena_slice arena_engine(void);
/* [arena_drawer] Devolve a slice do drawer. */
arena_slice arena_drawer(void);

#ifdef __cplusplus
}
#endif

#endif
