#include "world.h"

#include "arena.h"

#define WORLD_CAP 8
#define WORLD_STEP 2

static int *row_x;
static int *row_y;
static int *row_w;
static int *row_h;
static int row_count;

/* [clamp_pos] Limita a posição para o Rect caber na View. */
static int clamp_pos(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > WORLD_VIEW - WORLD_RECT) {
        return WORLD_VIEW - WORLD_RECT;
    }
    return value;
}

/* [world_init] Monta o Rect na slice de [arena_world]. Devolve 0 se [arena_ready] servir. */
int world_init(void)
{
    arena_slice slice;
    int *rows;
    size_t need;

    if (!arena_ready()) {
        return 1;
    }
    slice = arena_world();
    need = (size_t)WORLD_CAP * 4u * sizeof(int);
    if (slice.data == 0 || slice.size < need) {
        return 1;
    }
    rows = (int *)slice.data;
    row_x = rows;
    row_y = rows + WORLD_CAP;
    row_w = rows + WORLD_CAP * 2;
    row_h = rows + WORLD_CAP * 3;
    row_count = 1;
    row_x[0] = (WORLD_VIEW - WORLD_RECT) / 2;
    row_y[0] = (WORLD_VIEW - WORLD_RECT) / 2;
    row_w[0] = WORLD_RECT;
    row_h[0] = WORLD_RECT;
    return 0;
}

/* [world_tick] Desloca o Rect no plano com as actions de [actions_capture]. */
void world_tick(const actions *input)
{
    int x;
    int y;
    int up;

    if (row_count < 1 || input == 0) {
        return;
    }
    x = row_x[0];
    y = row_y[0];
    if (input->move_left && !input->move_right) {
        x -= WORLD_STEP;
    } else if (input->move_right && !input->move_left) {
        x += WORLD_STEP;
    }
    up = input->move_up || input->jump;
    if (up && !input->move_down) {
        y -= WORLD_STEP;
    } else if (input->move_down && !up) {
        y += WORLD_STEP;
    }
    row_x[0] = clamp_pos(x);
    row_y[0] = clamp_pos(y);
}

/* [world_rect] Escreve posição e tamanho do Rect. */
void world_rect(int *x, int *y, int *w, int *h)
{
    if (row_count < 1) {
        *x = 0;
        *y = 0;
        *w = 0;
        *h = 0;
        return;
    }
    *x = row_x[0];
    *y = row_y[0];
    *w = row_w[0];
    *h = row_h[0];
}
