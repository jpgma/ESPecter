#ifndef ESPECTER_ENGINE_HOST_H
#define ESPECTER_ENGINE_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

/* [engine_host_run] Sobe a Scene depois de [world_init] e entra no laço do Engine. No device cada Engine::run pinta um frame e volta, então o host repete. */
void engine_host_run(void);

#ifdef __cplusplus
}
#endif

#endif
