#ifndef ESPECTER_ENGINE_HOST_H
#define ESPECTER_ENGINE_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

/* [engine_host_run] Sobe a Scene depois de [world_init]. No PC entra no laço do Engine. No device monta o snapshot, publica a view e prende o sim no core 0 e o present no core 1. */
void engine_host_run(void);

#ifdef __cplusplus
}
#endif

#endif
