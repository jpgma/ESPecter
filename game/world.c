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
#define WORLD_COLS 14
#define WORLD_CAM_STIFF 12
#define WORLD_CAM_DAMP_NUM 6
#define WORLD_CAM_DAMP_DEN 8
#define WORLD_SPAN (WORLD_VIEW << WORLD_SUB)
#define SOLID_CAP 256
#define SOLID_COLS 6
#define SOLID_USED 5
#define PLAY_H 16
#define LANE_L 96
#define LANE_R 144
#define RIGHT_END 232
#define LOW_TOP_LO 176
#define LOW_TOP_HI 200
#define MID_GAP_LO 32
#define MID_GAP_HI 56
#define GROUND_CELL_X (-8)
#define GROUND_Y 224
#define GROUND_W (17 * WORLD_VIEW)
#define GROUND_H 32

static const int low_widths[] = {40, 56, 72};
static const int mid_widths[] = {32, 48};

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
static int *row_cell_x;
static int *row_cell_y;
static int row_count;
static int *cam_x;
static int *cam_y;
static int *cam_vx;
static int *cam_vy;
static int *roll_state;
static int *solid_cx;
static int *solid_cy;
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

/* [compose_sub] Junta a célula e o local no subpixel do mundo. */
static int compose_sub(int cell, int local)
{
    return cell * WORLD_SPAN + local;
}

/* [split_sub] Separa o subpixel composto na célula e no local de [0, 240<<8). */
static void split_sub(int composed, int *cell, int *local)
{
    int next;

    next = composed / WORLD_SPAN;
    if (composed < 0 && composed % WORLD_SPAN != 0) {
        next -= 1;
    }
    *cell = next;
    *local = composed - next * WORLD_SPAN;
}

/* [solid_box] Escreve o AABB index em subpixels do mundo. */
static void solid_box(int index, int *left, int *top, int *right, int *bottom)
{
    int x;
    int y;

    x = solid_cx[index] * WORLD_VIEW + solid_x[index];
    y = solid_cy[index] * WORLD_VIEW + solid_y[index];
    *left = x << WORLD_SUB;
    *top = y << WORLD_SUB;
    *right = (x + solid_w[index]) << WORLD_SUB;
    *bottom = (y + solid_h[index]) << WORLD_SUB;
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

/* [body_stop_x] Devolve o x composto na primeira face vertical cruzada. hit recebe -1, 1 ou 0. */
static int body_stop_x(int x0, int dest, int y, int bw, int bh, int *hit)
{
    int best;
    int i;
    int left;
    int top;
    int right;
    int bottom;
    int face;

    *hit = 0;
    best = dest;
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
    return best;
}

/* [body_stop_y] Devolve o y composto na primeira face horizontal cruzada. hit recebe -1 teto, 1 chão ou 0. */
static int body_stop_y(int y0, int dest, int x, int bw, int bh, int *hit)
{
    int best;
    int i;
    int left;
    int top;
    int right;
    int bottom;
    int face;

    *hit = 0;
    best = dest;
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
    return best;
}

/* [body_on_latch] Devolve 1 se um subpixel contra o muro ainda cruza uma face. */
static int body_on_latch(void)
{
    int side;
    int x;
    int y;
    int bw;
    int bh;
    int probe;
    int i;
    int left;
    int top;
    int right;
    int bottom;

    side = row_wall[0];
    if (side == 0) {
        return 0;
    }
    x = compose_sub(row_cell_x[0], row_x[0]);
    y = compose_sub(row_cell_y[0], row_y[0]);
    bw = row_w[0] << WORLD_SUB;
    bh = row_h[0] << WORLD_SUB;
    if (side < 0) {
        probe = x - 1;
    } else {
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

/* [body_solve_x] Soma vx, projeta nas faces verticais, grava o latch e separa a célula. */
static void body_solve_x(const actions *input)
{
    int x0;
    int y0;
    int bw;
    int bh;
    int hit;
    int x;

    x0 = compose_sub(row_cell_x[0], row_x[0]);
    y0 = compose_sub(row_cell_y[0], row_y[0]);
    bw = row_w[0] << WORLD_SUB;
    bh = row_h[0] << WORLD_SUB;
    row_hit_left[0] = 0;
    row_hit_right[0] = 0;
    x = body_stop_x(x0, x0 + row_vx[0], y0, bw, bh, &hit);
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
    split_sub(x, &row_cell_x[0], &row_x[0]);
}

/* [body_solve_y] Soma vy, projeta nas faces horizontais, separa a célula, e o chão ou a saída do muro solta o latch. */
static void body_solve_y(void)
{
    int x0;
    int y0;
    int bw;
    int bh;
    int hit;
    int y;

    x0 = compose_sub(row_cell_x[0], row_x[0]);
    y0 = compose_sub(row_cell_y[0], row_y[0]);
    bw = row_w[0] << WORLD_SUB;
    bh = row_h[0] << WORLD_SUB;
    row_hit_floor[0] = 0;
    row_hit_ceil[0] = 0;
    y = body_stop_y(y0, y0 + row_vy[0], x0, bw, bh, &hit);
    split_sub(y, &row_cell_y[0], &row_y[0]);
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

/* [cam_target] Devolve a origem da View que põe o centro do corpo no centro da View. */
static int cam_target(int cell, int local)
{
    int center;

    center = compose_sub(cell, local) + ((WORLD_RECT / 2) << WORLD_SUB);
    return center - ((WORLD_VIEW / 2) << WORLD_SUB);
}

/* [cam_axis] Integra um eixo da mola até o alvo. */
static void cam_axis(int *pos, int *vel, int target)
{
    *vel += (target - *pos) / WORLD_CAM_STIFF;
    *vel = *vel * WORLD_CAM_DAMP_NUM / WORLD_CAM_DAMP_DEN;
    *pos += *vel;
}

/* [cam_spring] Puxa a origem da View para deixar o corpo no centro, com folga e retorno. */
static void cam_spring(void)
{
    cam_axis(cam_x, cam_vx, cam_target(row_cell_x[0], row_x[0]));
    cam_axis(cam_y, cam_vy, cam_target(row_cell_y[0], row_y[0]));
}

/* [body_place] Põe o corpo no centro da célula 0, zerado, e encosta a câmera com a mola parada. */
static void body_place(void)
{
    int origin;

    origin = ((WORLD_VIEW - WORLD_RECT) / 2) << WORLD_SUB;
    row_cell_x[0] = 0;
    row_cell_y[0] = 0;
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
    *cam_x = 0;
    *cam_y = 0;
    *cam_vx = 0;
    *cam_vy = 0;
}

/* [roll_next] Avança o LCG da slice e devolve 15 bits. */
static int roll_next(void)
{
    unsigned int state;

    state = (unsigned int)*roll_state;
    state = state * 1103515245u + 12345u;
    *roll_state = (int)state;
    return (int)((state >> 16) & 0x7fffu);
}

/* [roll_pick] Devolve um inteiro em [lo, hi]. */
static int roll_pick(int lo, int hi)
{
    if (hi <= lo) {
        return lo;
    }
    return lo + roll_next() % (hi - lo + 1);
}

/* [roll_width] Devolve uma largura da tabela. */
static int roll_width(const int *table, int count)
{
    return table[roll_next() % count];
}

/* [solid_put] Grava o AABB index na célula e no local em pixels. */
static void solid_put(int index, int cell_x, int cell_y, int x, int y, int w, int h)
{
    solid_cx[index] = cell_x;
    solid_cy[index] = cell_y;
    solid_x[index] = x;
    solid_y[index] = y;
    solid_w[index] = w;
    solid_h[index] = h;
}

/* [solid_over] Devolve o x do médio que cobre parte do baixo e fica fora do corredor. */
static int solid_over(int low_x, int low_w, int width, int min_x, int max_edge)
{
    int lo;
    int hi;

    lo = low_x - width + 1;
    if (lo < min_x) {
        lo = min_x;
    }
    hi = low_x + low_w - 1;
    if (hi > max_edge - width) {
        hi = max_edge - width;
    }
    return roll_pick(lo, hi);
}

/* [solid_roll] Sorteia as quatro plataformas na célula 0, fora do corredor x 96–144. */
static void solid_roll(void)
{
    int lw;
    int rw;
    int lx;
    int rx;
    int ly;
    int ry;
    int mw;
    int gap;

    lw = roll_width(low_widths, 3);
    lx = roll_pick(0, LANE_L - lw);
    ly = roll_pick(LOW_TOP_LO, LOW_TOP_HI);
    solid_put(1, 0, 0, lx, ly, lw, PLAY_H);

    rw = roll_width(low_widths, 3);
    rx = roll_pick(LANE_R, RIGHT_END - rw);
    ry = roll_pick(LOW_TOP_LO, LOW_TOP_HI);
    solid_put(2, 0, 0, rx, ry, rw, PLAY_H);

    mw = roll_width(mid_widths, 2);
    gap = roll_pick(MID_GAP_LO, MID_GAP_HI);
    solid_put(3, 0, 0, solid_over(lx, lw, mw, 0, LANE_L), ly - gap, mw, PLAY_H);

    mw = roll_width(mid_widths, 2);
    gap = roll_pick(MID_GAP_LO, MID_GAP_HI);
    solid_put(4, 0, 0, solid_over(rx, rw, mw, LANE_R, RIGHT_END), ry - gap, mw, PLAY_H);
}

/* [solid_load] Grava o chão na slice de [arena_level] e chama [solid_roll]. Devolve 0 se couberem. */
static int solid_load(void)
{
    arena_slice slice;
    int *rows;
    size_t need;

    if (SOLID_USED > SOLID_CAP) {
        return 1;
    }
    slice = arena_level();
    need = (size_t)SOLID_CAP * (size_t)SOLID_COLS * sizeof(int);
    if (slice.data == 0 || slice.size < need) {
        return 1;
    }
    rows = (int *)slice.data;
    solid_cx = rows;
    solid_cy = rows + SOLID_CAP;
    solid_x = rows + SOLID_CAP * 2;
    solid_y = rows + SOLID_CAP * 3;
    solid_w = rows + SOLID_CAP * 4;
    solid_h = rows + SOLID_CAP * 5;
    solid_put(0, GROUND_CELL_X, 0, 0, GROUND_Y, GROUND_W, GROUND_H);
    solid_count = SOLID_USED;
    solid_roll();
    return 0;
}

/* [world_init] Monta o corpo no centro da célula 0, o chão e as plataformas na slice de [arena_level], e a câmera centrada com a mola parada. Devolve 0 se [arena_ready] servir. */
int world_init(void)
{
    arena_slice slice;
    int *rows;
    size_t need;

    if (!arena_ready()) {
        return 1;
    }
    slice = arena_world();
    need = (size_t)WORLD_CAP * (size_t)WORLD_COLS * sizeof(int) + 5u * sizeof(int);
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
    row_cell_x = rows + WORLD_CAP * 12;
    row_cell_y = rows + WORLD_CAP * 13;
    cam_x = rows + WORLD_CAP * WORLD_COLS;
    cam_y = cam_x + 1;
    cam_vx = cam_y + 1;
    cam_vy = cam_vx + 1;
    roll_state = cam_vy + 1;
    *roll_state = 1;
    row_count = 1;
    if (solid_load() != 0) {
        return 1;
    }
    body_place();
    return 0;
}

/* [world_reset] Re-rola as quatro plataformas, devolve o corpo ao centro da célula 0 e encosta a câmera. */
void world_reset(void)
{
    if (row_count < 1) {
        return;
    }
    solid_roll();
    body_place();
}

/* [world_tick] Integra um tick fixo; move_down solta o latch antes da corrida e a mola da câmera segue o centro do corpo. */
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
    cam_spring();
}

/* [world_rect] Escreve a posição no mundo, em pixels, e o tamanho do Rect. */
void world_rect(int *x, int *y, int *w, int *h)
{
    if (row_count < 1) {
        *x = 0;
        *y = 0;
        *w = 0;
        *h = 0;
        return;
    }
    *x = row_cell_x[0] * WORLD_VIEW + (row_x[0] >> WORLD_SUB);
    *y = row_cell_y[0] * WORLD_VIEW + (row_y[0] >> WORLD_SUB);
    *w = row_w[0];
    *h = row_h[0];
}

/* [world_camera] Escreve a origem da View em pixels do mundo. */
void world_camera(int *x, int *y)
{
    if (cam_x == 0) {
        *x = 0;
        *y = 0;
        return;
    }
    *x = *cam_x >> WORLD_SUB;
    *y = *cam_y >> WORLD_SUB;
}

/* [world_solid_count] Devolve quantos AABB estáticos a slice guarda. */
int world_solid_count(void)
{
    return solid_count;
}

/* [world_solid_rect] Escreve o AABB index em pixels do mundo. Fora da contagem, zera a saída. */
void world_solid_rect(int index, int *x, int *y, int *w, int *h)
{
    if (index < 0 || index >= solid_count) {
        *x = 0;
        *y = 0;
        *w = 0;
        *h = 0;
        return;
    }
    *x = solid_cx[index] * WORLD_VIEW + solid_x[index];
    *y = solid_cy[index] * WORLD_VIEW + solid_y[index];
    *w = solid_w[index];
    *h = solid_h[index];
}
