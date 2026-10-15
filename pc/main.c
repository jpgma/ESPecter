#include <stdio.h>
#include <stdlib.h>

#include "arena.h"
#include "engine_host.h"

/* [main] Aloca a arena com [arena_init] e entra em [engine_host_run]. */
int main(void)
{
    void *block;

    block = malloc(ARENA_BYTES);
    if (block == NULL || arena_init(block, ARENA_BYTES) != 0) {
        fprintf(stderr, "arena init failed\n");
        return 1;
    }
    engine_host_run();
    return 0;
}
