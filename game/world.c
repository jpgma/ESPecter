#include "world.h"

#include "arena.h"

#define WORLD_CAP 8
#define WORLD_SUB 8
#define WORLD_RUN_ACCEL 64
#define WORLD_RUN_MAX 768
#define WORLD_GRAVITY_RISE 128
#define WORLD_GRAVITY_FALL 256
#define WORLD_JUMP_VY (-2048)
#define WORLD_FALL_MAX 2048
#define WORLD_COLS 11

static int *row_x;
static int *row_y;
static int *row_w;
static int *row_h;
static int *row_vx;
static int *row_vy;
static int *row_hit_left;
static int *row_hit_right;
static int *row_hit_floor;
static int *row_hit_ceil;
static int *row_jump_was;
static int row_count;

/* [body_span] Devolve o maior subpixel em que o Rect cabe na View. */
static int body_span(void)
{
    return (WORLD_VIEW - WORLD_RECT) << WORLD_SUB;
}

/* [body_run] Acelera ou atrita vx segundo move_left e move_right. */
static void body_run(const actions *input)
{
    int vx;

    vx = row_vx[0];
    if (input->move_left && !input->move_right) {
        vx -= WORLD_RUN_ACCEL;
    } else if (input->move_right && !input->move_left) {
        vx += WORLD_RUN_ACCEL;
    } else if (vx > 0) {
        vx -= WORLD_RUN_ACCEL;
        if (vx < 0) {
            vx = 0;
        }
    } else if (vx < 0) {
        vx += WORLD_RUN_ACCEL;
        if (vx > 0) {
            vx = 0;
        }
    }
    if (vx > WORLD_RUN_MAX) {
        vx = WORLD_RUN_MAX;
    } else if (vx < -WORLD_RUN_MAX) {
        vx = -WORLD_RUN_MAX;
    }
    row_vx[0] = vx;
}

/* [body_gravity] Soma a gravidade em vy e limita a queda. */
static void body_gravity(const actions *input)
{
    int vy;

    vy = row_vy[0];
    if (vy < 0 && input->jump) {
        vy += WORLD_GRAVITY_RISE;
    } else {
        vy += WORLD_GRAVITY_FALL;
    }
    if (vy > WORLD_FALL_MAX) {
        vy = WORLD_FALL_MAX;
    }
    row_vy[0] = vy;
}

/* [body_solve_x] Soma vx e encosta o corpo nas bordas verticais. */
static void body_solve_x(void)
{
    int x;
    int span;

    x = row_x[0] + row_vx[0];
    span = body_span();
    row_hit_left[0] = 0;
    row_hit_right[0] = 0;
    if (x < 0) {
        x = 0;
        row_vx[0] = 0;
        row_hit_left[0] = 1;
    } else if (x > span) {
        x = span;
        row_vx[0] = 0;
        row_hit_right[0] = 1;
    }
    row_x[0] = x;
}

/* [body_solve_y] Soma vy e encosta o corpo nas bordas horizontais. */
static void body_solve_y(void)
{
    int y;
    int span;

    y = row_y[0] + row_vy[0];
    span = body_span();
    row_hit_floor[0] = 0;
    row_hit_ceil[0] = 0;
    if (y < 0) {
        y = 0;
        row_vy[0] = 0;
        row_hit_ceil[0] = 1;
    } else if (y > span) {
        y = span;
        row_vy[0] = 0;
        row_hit_floor[0] = 1;
    }
    row_y[0] = y;
}

/* [body_jump] Impulsiona vy se a borda de jump achou o chão de [body_solve_y]. */
static void body_jump(const actions *input)
{
    if (input->jump && !row_jump_was[0] && row_hit_floor[0]) {
        row_vy[0] = WORLD_JUMP_VY;
        row_hit_floor[0] = 0;
    }
    row_jump_was[0] = input->jump ? 1 : 0;
}

/* [world_init] Monta o corpo no centro da slice de [arena_world]. Devolve 0 se [arena_ready] servir. */
int world_init(void)
{
    arena_slice slice;
    int *rows;
    size_t need;
    int origin;

    if (!arena_ready()) {
        return 1;
    }
    slice = arena_world();
    need = (size_t)WORLD_CAP * (size_t)WORLD_COLS * sizeof(int);
    if (slice.data == 0 || slice.size < need) {
        return 1;
    }
    rows = (int *)slice.data;
    row_x = rows;
    row_y = rows + WORLD_CAP;
    row_w = rows + WORLD_CAP * 2;
    row_h = rows + WORLD_CAP * 3;
    row_vx = rows + WORLD_CAP * 4;
    row_vy = rows + WORLD_CAP * 5;
    row_hit_left = rows + WORLD_CAP * 6;
    row_hit_right = rows + WORLD_CAP * 7;
    row_hit_floor = rows + WORLD_CAP * 8;
    row_hit_ceil = rows + WORLD_CAP * 9;
    row_jump_was = rows + WORLD_CAP * 10;
    row_count = 1;
    origin = ((WORLD_VIEW - WORLD_RECT) / 2) << WORLD_SUB;
    row_x[0] = origin;
    row_y[0] = origin;
    row_w[0] = WORLD_RECT;
    row_h[0] = WORLD_RECT;
    row_vx[0] = 0;
    row_vy[0] = 0;
    row_hit_left[0] = 0;
    row_hit_right[0] = 0;
    row_hit_floor[0] = 0;
    row_hit_ceil[0] = 0;
    row_jump_was[0] = 0;
    return 0;
}

/* [world_tick] Integra um tick fixo com as actions de [actions_capture]. */
void world_tick(const actions *input)
{
    if (row_count < 1 || input == 0) {
        return;
    }
    body_run(input);
    body_gravity(input);
    body_solve_x();
    body_solve_y();
    body_jump(input);
}

/* [world_rect] Escreve a posição em pixels e o tamanho do Rect. */
void world_rect(int *x, int *y, int *w, int *h)
{
    if (row_count < 1) {
        *x = 0;
        *y = 0;
        *w = 0;
        *h = 0;
        return;
    }
    *x = row_x[0] >> WORLD_SUB;
    *y = row_y[0] >> WORLD_SUB;
    *w = row_w[0];
    *h = row_h[0];
}
