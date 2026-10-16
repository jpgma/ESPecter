#ifndef ESPECTER_ENGINE_HOST_H
#define ESPECTER_ENGINE_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

/* [st7789_alloc_bands] Reserva os bands DMA e os índices 8bpp antes do NimBLE. Aborta se o heap interno não servir. */
void st7789_alloc_bands(void);

/* [engine_host_run] Sobe a Scene depois de [world_init]. No PC entra no laço do Engine. No device publica a view e prende o sim no core 0 e o present no core 1. */
void engine_host_run(void);

#ifdef __cplusplus
}
#endif

#endif
