#pragma once

#include <stdint.h>

enum {
    FRAME_MASK = 0,
    FRAME_GROUND = 1,
    FRAME_SOLIDS = 2,
    FRAME_BODY = 3,
    FRAME_DEBUG = 4,
    FRAME_EXPAND = 5,
    FRAME_SPI = 6,
    FRAME_ENGINE = 7,
    FRAME_WAIT = 8,
    FRAME_PART_COUNT = 9
};

/* [frame_profile_on] Devolve 1 no segundo em que o log do frame está armado. */
int frame_profile_on(void);
/* [frame_profile_begin] Zera os pedaços deste frame no core 1. */
void frame_profile_begin(void);
/* [frame_profile_add] Soma us no pedaço do core 1. Com [frame_profile_on] desligado, não lê o relógio. */
void frame_profile_add(int part, int64_t us);
/* [frame_profile_frame] Fecha o laço do core 1: o resto vira engine, a espera entra em wait. */
void frame_profile_frame(int64_t loop_us, int64_t wait_us, int empty, int rows);
/* [frame_profile_core0] Soma um laço do core 0: espera, [sim_advance] e o resto. */
void frame_profile_core0(int64_t wait_us, int64_t sim_us, int64_t other_us);
/* [frame_profile_flush] Se o SOF andou, imprime uma vez por segundo média, máximo e percentual de cada core. */
void frame_profile_flush(void);
