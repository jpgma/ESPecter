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
#define WORLD_SLIDE_MAX 256
#define WORLD_WALL_JUMP 2000
#define WORLD_COLS 12
#define SOLID_CAP 32
#define SOLID_COLS 4

/* Degraus, teto e pilar. A coluna x 112–128 fica vazia. */
static const int solid_px[] = {
    16, 184, 64, 16,
    16, 96, 64, 16,
    136, 176, 32, 16,
    176, 64, 24, 128
};

#define SOLID_N ((int)(sizeof solid_px / sizeof solid_px[0] / 4))

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
static int *row_wall;
static int row_count;
static int *solid_x;
static int *solid_y;
static int *solid_w;
static int *solid_h;
static int solid_count;

/* [range_overlaps] Devolve 1 se [a0, a1) cruza [b0, b1). Aresta encostada não conta. */
static int range_overlaps(int a0, int a1, int b0, int b1)
{
    return a0 < b1 && b0 < a1;
}

/* [solid_box] Escreve o AABB index em subpixels. */
static void solid_box(int index, int *left, int *top, int *right, int *bottom)
{
    *left = solid_x[index] << WORLD_SUB;
    *top = solid_y[index] << WORLD_SUB;
    *right = (solid_x[index] + solid_w[index]) << WORLD_SUB;
    *bottom = (solid_y[index] + solid_h[index]) << WORLD_SUB;
}

/* [axis_out] Devolve a posição que sai pela face mais curta. side recebe 1 na mínima e -1 na máxima. */
static int axis_out(int pos, int size, int min_edge, int max_edge, int *side)
{
    int into_min;
    int into_max;

    into_min = (pos + size) - min_edge;
    into_max = max_edge - pos;
    if (into_min <= into_max) {
        *side = 1;
        return min_edge - size;
    }
    *side = -1;
    return max_edge;
}

/* [body_embed] Tira best do sólido mais raso neste eixo e repete. hit fica se já estiver fora. */
static void body_embed(int horizontal, int fixed, int fixed_size, int move_size, int *best, int *hit)
{
    int pass;
    int i;
    int found;
    int pen_best;
    int out_best;
    int side_best;
    int left;
    int top;
    int right;
    int bottom;
    int min_edge;
    int max_edge;
    int out;
    int side;
    int pen;

    for (pass = 0; pass < solid_count; pass++) {
        found = 0;
        pen_best = 0;
        out_best = *best;
        side_best = 0;
        for (i = 0; i < solid_count; i++) {
            if (solid_w[i] <= 0 || solid_h[i] <= 0) {
                continue;
            }
            solid_box(i, &left, &top, &right, &bottom);
            if (horizontal) {
                if (!range_overlaps(fixed, fixed + fixed_size, top, bottom)) {
                    continue;
                }
                if (!range_overlaps(*best, *best + move_size, left, right)) {
                    continue;
                }
                min_edge = left;
                max_edge = right;
            } else {
                if (!range_overlaps(fixed, fixed + fixed_size, left, right)) {
                    continue;
                }
                if (!range_overlaps(*best, *best + move_size, top, bottom)) {
                    continue;
                }
                min_edge = top;
                max_edge = bottom;
            }
            out = axis_out(*best, move_size, min_edge, max_edge, &side);
            pen = side > 0 ? (*best + move_size) - min_edge : max_edge - *best;
            if (!found || pen < pen_best) {
                found = 1;
                pen_best = pen;
                out_best = out;
                side_best = side;
            }
        }
        if (!found) {
            return;
        }
        *best = out_best;
        *hit = side_best;
    }
}

/* [body_span] Devolve o maior subpixel em que o Rect cabe na View. */
static int body_span(void)
{
    return (WORLD_VIEW - WORLD_RECT) << WORLD_SUB;
}

/* [body_latched] Devolve 1 se o latch de parede segura o corpo. */
static int body_latched(void)
{
    return row_wall[0] != 0;
}

/* [body_run] Acelera ou atrita vx. O latch e o sangramento acima do teto de corrida ignoram o eixo. */
static void body_run(const actions *input)
{
    int vx;

    if (body_latched()) {
        return;
    }
    vx = row_vx[0];
    if (vx > WORLD_RUN_MAX) {
        vx -= WORLD_RUN_ACCEL;
        if (vx < WORLD_RUN_MAX) {
            vx = WORLD_RUN_MAX;
        }
    } else if (vx < -WORLD_RUN_MAX) {
        vx += WORLD_RUN_ACCEL;
        if (vx > -WORLD_RUN_MAX) {
            vx = -WORLD_RUN_MAX;
        }
    } else {
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

/* [body_slide] Limita a queda enquanto [body_latched] vale. */
static void body_slide(void)
{
    if (body_latched() && row_vy[0] > WORLD_SLIDE_MAX) {
        row_vy[0] = WORLD_SLIDE_MAX;
    }
}

/* [body_stop_x] Devolve o x na primeira face vertical cruzada. hit recebe -1, 1 ou 0. */
static int body_stop_x(int x0, int dest, int y, int bw, int bh, int *hit)
{
    int span;
    int best;
    int i;
    int left;
    int top;
    int right;
    int bottom;
    int face;

    span = body_span();
    *hit = 0;
    best = dest;
    if (best < 0) {
        best = 0;
        *hit = -1;
    } else if (best > span) {
        best = span;
        *hit = 1;
    }
    for (i = 0; i < solid_count; i++) {
        if (solid_w[i] <= 0 || solid_h[i] <= 0) {
            continue;
        }
        solid_box(i, &left, &top, &right, &bottom);
        if (!range_overlaps(y, y + bh, top, bottom)) {
            continue;
        }
        if (dest > x0) {
            face = left - bw;
            if (x0 + bw <= left && dest + bw > left && face < best) {
                best = face;
                *hit = 1;
            }
        } else if (dest < x0) {
            face = right;
            if (x0 >= right && dest < right && face > best) {
                best = face;
                *hit = -1;
            }
        }
    }
    body_embed(1, y, bh, bw, &best, hit);
    if (best < 0) {
        best = 0;
        *hit = -1;
    } else if (best > span) {
        best = span;
        *hit = 1;
    }
    return best;
}

/* [body_stop_y] Devolve o y na primeira face horizontal cruzada. hit recebe -1 teto, 1 chão ou 0. */
static int body_stop_y(int y0, int dest, int x, int bw, int bh, int *hit)
{
    int span;
    int best;
    int i;
    int left;
    int top;
    int right;
    int bottom;
    int face;

    span = body_span();
    *hit = 0;
    best = dest;
    if (best < 0) {
        best = 0;
        *hit = -1;
    } else if (best > span) {
        best = span;
        *hit = 1;
    }
    for (i = 0; i < solid_count; i++) {
        if (solid_w[i] <= 0 || solid_h[i] <= 0) {
            continue;
        }
        solid_box(i, &left, &top, &right, &bottom);
        if (!range_overlaps(x, x + bw, left, right)) {
            continue;
        }
        if (dest > y0) {
            face = top - bh;
            if (y0 + bh <= top && dest + bh > top && face < best) {
                best = face;
                *hit = 1;
            }
        } else if (dest < y0) {
            face = bottom;
            if (y0 >= bottom && dest < bottom && face > best) {
                best = face;
                *hit = -1;
            }
        }
    }
    body_embed(0, x, bw, bh, &best, hit);
    if (best < 0) {
        best = 0;
        *hit = -1;
    } else if (best > span) {
        best = span;
        *hit = 1;
    }
    return best;
}

/* [body_on_latch] Devolve 1 se um subpixel contra o muro ainda cruza uma face ou a borda. */
static int body_on_latch(void)
{
    int side;
    int x;
    int y;
    int bw;
    int bh;
    int probe;
    int span;
    int i;
    int left;
    int top;
    int right;
    int bottom;

    side = row_wall[0];
    if (side == 0) {
        return 0;
    }
    x = row_x[0];
    y = row_y[0];
    bw = row_w[0] << WORLD_SUB;
    bh = row_h[0] << WORLD_SUB;
    span = body_span();
    if (side < 0) {
        if (x <= 0) {
            return 1;
        }
        probe = x - 1;
    } else {
        if (x >= span) {
            return 1;
        }
        probe = x + 1;
    }
    for (i = 0; i < solid_count; i++) {
        if (solid_w[i] <= 0 || solid_h[i] <= 0) {
            continue;
        }
        solid_box(i, &left, &top, &right, &bottom);
        if (range_overlaps(probe, probe + bw, left, right) &&
            range_overlaps(y, y + bh, top, bottom)) {
            return 1;
        }
    }
    return 0;
}

/* [body_solve_x] Soma vx, projeta nas faces verticais e grava o latch. */
static void body_solve_x(const actions *input)
{
    int x0;
    int bw;
    int bh;
    int hit;
    int x;

    x0 = row_x[0];
    bw = row_w[0] << WORLD_SUB;
    bh = row_h[0] << WORLD_SUB;
    row_hit_left[0] = 0;
    row_hit_right[0] = 0;
    x = body_stop_x(x0, x0 + row_vx[0], row_y[0], bw, bh, &hit);
    if (hit != 0) {
        row_vx[0] = 0;
        if (hit < 0) {
            row_hit_left[0] = 1;
            if (!input->move_down) {
                row_wall[0] = -1;
            }
        } else {
            row_hit_right[0] = 1;
            if (!input->move_down) {
                row_wall[0] = 1;
            }
        }
    }
    row_x[0] = x;
}

/* [body_solve_y] Soma vy, projeta nas faces horizontais, e o chão ou a saída do muro solta o latch. */
static void body_solve_y(void)
{
    int y0;
    int bw;
    int bh;
    int hit;
    int y;

    y0 = row_y[0];
    bw = row_w[0] << WORLD_SUB;
    bh = row_h[0] << WORLD_SUB;
    row_hit_floor[0] = 0;
    row_hit_ceil[0] = 0;
    y = body_stop_y(y0, y0 + row_vy[0], row_x[0], bw, bh, &hit);
    row_y[0] = y;
    if (hit > 0) {
        row_vy[0] = 0;
        row_hit_floor[0] = 1;
        row_wall[0] = 0;
    } else if (hit < 0) {
        row_vy[0] = 0;
        row_hit_ceil[0] = 1;
    }
    if (row_wall[0] != 0 && !body_on_latch()) {
        row_wall[0] = 0;
    }
}

/* [body_jump] Impulsiona no chão, ou na diagonal se o latch de [body_solve_x] vale. */
static void body_jump(const actions *input)
{
    int edge;

    edge = input->jump && !row_jump_was[0];
    if (edge && row_hit_floor[0]) {
        row_vy[0] = WORLD_JUMP_VY;
        row_hit_floor[0] = 0;
    } else if (edge && body_latched()) {
        if (row_wall[0] < 0) {
            row_vx[0] = WORLD_WALL_JUMP;
        } else {
            row_vx[0] = -WORLD_WALL_JUMP;
        }
        row_vy[0] = -WORLD_WALL_JUMP;
        row_wall[0] = 0;
    }
    row_jump_was[0] = input->jump ? 1 : 0;
}

/* [solid_load] Copia os AABB de prova para a slice de [arena_level]. Devolve 0 se couberem. */
static int solid_load(void)
{
    arena_slice slice;
    int *rows;
    size_t need;
    int i;

    if (SOLID_N > SOLID_CAP) {
        return 1;
    }
    slice = arena_level();
    need = (size_t)SOLID_CAP * (size_t)SOLID_COLS * sizeof(int);
    if (slice.data == 0 || slice.size < need) {
        return 1;
    }
    rows = (int *)slice.data;
    solid_x = rows;
    solid_y = rows + SOLID_CAP;
    solid_w = rows + SOLID_CAP * 2;
    solid_h = rows + SOLID_CAP * 3;
    for (i = 0; i < SOLID_N; i++) {
        solid_x[i] = solid_px[i * 4];
        solid_y[i] = solid_px[i * 4 + 1];
        solid_w[i] = solid_px[i * 4 + 2];
        solid_h[i] = solid_px[i * 4 + 3];
    }
    solid_count = SOLID_N;
    return 0;
}

/* [world_init] Monta o corpo no centro da slice de [arena_world] e copia os sólidos para a slice de [arena_level]. Devolve 0 se [arena_ready] servir. */
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
    row_wall = rows + WORLD_CAP * 11;
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
    row_wall[0] = 0;
    return solid_load();
}

/* [world_tick] Integra um tick fixo; move_down solta o latch antes da corrida. */
void world_tick(const actions *input)
{
    if (row_count < 1 || input == 0) {
        return;
    }
    if (input->move_down) {
        row_wall[0] = 0;
    }
    body_run(input);
    body_gravity(input);
    body_slide();
    body_solve_x(input);
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

/* [world_solid_count] Devolve quantos AABB estáticos [world_init] copiou. */
int world_solid_count(void)
{
    return solid_count;
}

/* [world_solid_rect] Escreve o AABB index em pixels. Fora da contagem, zera a saída. */
void world_solid_rect(int index, int *x, int *y, int *w, int *h)
{
    if (index < 0 || index >= solid_count) {
        *x = 0;
        *y = 0;
        *w = 0;
        *h = 0;
        return;
    }
    *x = solid_x[index];
    *y = solid_y[index];
    *w = solid_w[index];
    *h = solid_h[index];
}
