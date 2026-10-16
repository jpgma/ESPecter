#include "world.h"

#include "arena.h"
#include "body_sheet.h"

#define WORLD_CAP 8
#define WORLD_SUB 8
#define WORLD_RUN_ACCEL 64
#define WORLD_RUN_MAX 700
#define WORLD_GRAVITY_RISE 128
#define WORLD_GRAVITY_FALL 256
#define WORLD_JUMP_VY (-2048)
#define WORLD_FALL_MAX 2048
#define WORLD_SLIDE_MAX 256
#define WORLD_WALL_JUMP 1500
#define WORLD_COLS 20
#define BODY_ACT_NONE 0
#define BODY_ACT_FLOOR 1
#define BODY_ACT_WALL 2
#define BODY_HOLD_SLOW 4
#define BODY_HOLD_FAST 2
#define WORLD_CAM_STIFF 2
#define WORLD_CAM_DAMP_NUM 3
#define WORLD_CAM_DAMP_DEN 8
#define WORLD_CAM_FLOOR 32
#define WORLD_SPAN (WORLD_VIEW << WORLD_SUB)
#define SOLID_CAP 256
#define SOLID_COLS 6
#define SOLID_USED 9
#define PLAY_H 16
#define LANE_L 96
#define LANE_R 144
#define RIGHT_END 232
#define LOW_TOP_LO 176
#define LOW_TOP_HI 200
#define MID_GAP_LO 32
#define MID_GAP_HI 56
#define WALL_W 16
#define WALL_TOP_LO 8
#define WALL_TOP_HI 24
#define WALL_BOTTOM 200
#define WALL_X_L_LO 64
#define WALL_X_L_HI (LANE_L - WALL_W)
#define WALL_X_R_LO LANE_R
#define WALL_X_R_HI 160
#define STACK_N 3
#define STACK_TOP 8
#define GROUND_CELL_X (-8)
#define GROUND_Y 224
#define GROUND_W (17 * WORLD_VIEW)
#define GROUND_H 32

static const int low_widths[] = {40, 56, 72};

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
static int *row_face;
static int *row_clip;
static int *row_frame;
static int *row_hold;
static int *row_act;
static int *row_vx_was;
static int *row_cell_x;
static int *row_cell_y;
static int row_count;
static int *cam_x;
static int *cam_y;
static int *cam_vx;
static int *cam_vy;
static int *cam_ground;
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

/* [cam_feet] Devolve o y dos pés em subpixels do mundo. */
static int cam_feet(void)
{
    return compose_sub(row_cell_y[0], row_y[0]) + (row_h[0] << WORLD_SUB);
}

/* [cam_note_ground] Grava a linha dos pés quando [body_solve_y] marca o chão. */
static void cam_note_ground(void)
{
    if (row_hit_floor[0]) {
        *cam_ground = cam_feet();
    }
}

/* [body_solve_y] Soma vy, projeta nas faces horizontais, separa a célula, solta o latch no chão ou na saída do muro, e o chão chama [cam_note_ground]. */
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
    cam_note_ground();
}

/* [body_jump] Impulsiona no chão, ou na diagonal se o latch de [body_solve_x] vale, e grava o ato do tick. */
static void body_jump(const actions *input)
{
    int edge;

    edge = input->jump && !row_jump_was[0];
    row_act[0] = BODY_ACT_NONE;
    if (edge && row_hit_floor[0]) {
        row_vy[0] = WORLD_JUMP_VY;
        row_hit_floor[0] = 0;
        row_act[0] = BODY_ACT_FLOOR;
    } else if (edge && body_latched()) {
        if (row_wall[0] < 0) {
            row_vx[0] = WORLD_WALL_JUMP;
        } else {
            row_vx[0] = -WORLD_WALL_JUMP;
        }
        row_vy[0] = -WORLD_WALL_JUMP;
        row_wall[0] = 0;
        row_act[0] = BODY_ACT_WALL;
    }
    row_jump_was[0] = input->jump ? 1 : 0;
}

/* [body_speed] Devolve o módulo de vx. */
static int body_speed(void)
{
    if (row_vx[0] < 0) {
        return -row_vx[0];
    }
    return row_vx[0];
}

/* [body_turned] Devolve 1 se vx trocou de sinal com os dois valores diferentes de zero. */
static int body_turned(void)
{
    int prev;
    int vx;

    prev = row_vx_was[0];
    vx = row_vx[0];
    if (prev == 0 || vx == 0) {
        return 0;
    }
    if ((prev > 0 && vx < 0) || (prev < 0 && vx > 0)) {
        return 1;
    }
    return 0;
}

/* [body_face_now] A face olha para longe do muro no latch; senão segue o sinal de vx. */
static void body_face_now(void)
{
    if (body_latched()) {
        row_face[0] = -row_wall[0];
        return;
    }
    if (row_vx[0] > 0) {
        row_face[0] = 1;
    } else if (row_vx[0] < 0) {
        row_face[0] = -1;
    }
}

/* [body_hold_ticks] Devolve os ticks do quadro. Idle não avança. */
static int body_hold_ticks(int clip)
{
    if (clip == BODY_CLIP_WALK || clip == BODY_CLIP_WALL) {
        return BODY_HOLD_SLOW;
    }
    if (clip == BODY_CLIP_IDLE) {
        return 0;
    }
    return BODY_HOLD_FAST;
}

/* [body_oneshot] Devolve 1 se o clip toca uma vez e solta. */
static int body_oneshot(int clip)
{
    return clip == BODY_CLIP_RUN_TURN || clip == BODY_CLIP_JUMP_TURN || clip == BODY_CLIP_DASH;
}

/* [body_air_clip] Devolve 1 se o pouso cancela o clip. */
static int body_air_clip(int clip)
{
    return clip == BODY_CLIP_JUMP || clip == BODY_CLIP_JUMP_TURN || clip == BODY_CLIP_DASH;
}

/* [body_restart] Começa o clip no quadro 0 com o hold zerado. */
static void body_restart(int clip)
{
    row_clip[0] = clip;
    row_frame[0] = 0;
    row_hold[0] = 0;
}

/* [body_enter] Troca o clip. Walk e run conservam o quadro. */
static void body_enter(int clip)
{
    int keep;

    if (row_clip[0] == clip) {
        return;
    }
    keep = (row_clip[0] == BODY_CLIP_WALK && clip == BODY_CLIP_RUN)
        || (row_clip[0] == BODY_CLIP_RUN && clip == BODY_CLIP_WALK);
    if (!keep) {
        body_restart(clip);
        return;
    }
    row_clip[0] = clip;
    row_hold[0] = 0;
    if (row_frame[0] < 0 || row_frame[0] >= body_sheet_frames(clip)) {
        row_frame[0] = 0;
    }
}

/* [body_advance_due] Avança o quadro quando o hold enche. Devolve 1 se o one-shot terminou. */
static int body_advance_due(void)
{
    int clip;
    int frames;
    int need;

    clip = row_clip[0];
    frames = body_sheet_frames(clip);
    need = body_hold_ticks(clip);
    if (frames <= 0 || need <= 0 || row_hold[0] < need) {
        return 0;
    }
    row_hold[0] = 0;
    if (row_frame[0] + 1 < frames) {
        row_frame[0] += 1;
        return 0;
    }
    if (clip == BODY_CLIP_JUMP) {
        return 0;
    }
    if (clip == BODY_CLIP_WALK || clip == BODY_CLIP_RUN || clip == BODY_CLIP_WALL) {
        row_frame[0] = 0;
        return 0;
    }
    return 1;
}

/* [body_commit] Soma um tick no hold do clip que ficou para o desenho. */
static void body_commit(void)
{
    int need;

    need = body_hold_ticks(row_clip[0]);
    if (need <= 0) {
        return;
    }
    row_hold[0] += 1;
}

/* [body_select_ground] Escolhe idle, walk, run ou a virada no chão. */
static void body_select_ground(void)
{
    if (body_turned()) {
        body_restart(BODY_CLIP_RUN_TURN);
        return;
    }
    if (body_speed() >= WORLD_RUN_MAX) {
        if (row_clip[0] == BODY_CLIP_RUN) {
            body_advance_due();
            return;
        }
        body_enter(BODY_CLIP_RUN);
        return;
    }
    if (row_vx[0] != 0) {
        if (row_clip[0] == BODY_CLIP_WALK) {
            body_advance_due();
            return;
        }
        body_enter(BODY_CLIP_WALK);
        return;
    }
    if (row_clip[0] == BODY_CLIP_IDLE) {
        return;
    }
    body_restart(BODY_CLIP_IDLE);
}

/* [body_select_air] Continua o jump, segura o último quadro depois do dash, ou começa a virada. */
static void body_select_air(int dash_done)
{
    int last;

    if (body_turned()) {
        body_restart(BODY_CLIP_JUMP_TURN);
        return;
    }
    if (dash_done) {
        last = body_sheet_frames(BODY_CLIP_JUMP) - 1;
        if (last < 0) {
            last = 0;
        }
        row_clip[0] = BODY_CLIP_JUMP;
        row_frame[0] = last;
        row_hold[0] = 0;
        return;
    }
    if (row_clip[0] == BODY_CLIP_JUMP) {
        body_advance_due();
        return;
    }
    body_restart(BODY_CLIP_JUMP);
}

/* [body_pose] Escolhe o clip no fim do tick. O latch ganha, o pulo da parede começa o dash, e o one-shot aberto avança. */
static void body_pose(void)
{
    int grounded;
    int done;

    body_face_now();
    grounded = row_hit_floor[0] != 0;
    if (body_latched()) {
        if (row_clip[0] == BODY_CLIP_WALL) {
            body_advance_due();
        } else {
            body_restart(BODY_CLIP_WALL);
        }
    } else if (row_act[0] == BODY_ACT_WALL) {
        body_restart(BODY_CLIP_DASH);
    } else if (grounded && body_air_clip(row_clip[0])) {
        body_select_ground();
    } else if (body_oneshot(row_clip[0])) {
        done = body_advance_due();
        if (done) {
            if (grounded) {
                body_select_ground();
            } else {
                body_select_air(row_clip[0] == BODY_CLIP_DASH);
            }
        }
    } else if (grounded) {
        body_select_ground();
    } else {
        body_select_air(0);
    }
    body_commit();
    row_vx_was[0] = row_vx[0];
}

/* [cam_focus_x] Devolve a origem x da View que põe o centro do corpo no meio horizontal. */
static int cam_focus_x(void)
{
    int center;

    center = compose_sub(row_cell_x[0], row_x[0]) + ((row_w[0] / 2) << WORLD_SUB);
    return center - ((WORLD_VIEW / 2) << WORLD_SUB);
}

/* [cam_focus_y] Devolve a origem y da View que deixa a linha de [cam_ground] a WORLD_CAM_FLOOR px da base. */
static int cam_focus_y(void)
{
    return *cam_ground - ((WORLD_VIEW - WORLD_CAM_FLOOR) << WORLD_SUB);
}

/* [cam_axis] Integra um eixo da mola até o alvo. */
static void cam_axis(int *pos, int *vel, int target)
{
    *vel += (target - *pos) / WORLD_CAM_STIFF;
    *vel = *vel * WORLD_CAM_DAMP_NUM / WORLD_CAM_DAMP_DEN;
    *pos += *vel;
}

/* [cam_spring] Segue o meio horizontal e a linha do chão. O pulo não levanta a View; a queda abaixo da linha sim. */
static void cam_spring(void)
{
    int feet;

    feet = cam_feet();
    if (feet > *cam_ground) {
        *cam_ground = feet;
    }
    cam_axis(cam_x, cam_vx, cam_focus_x());
    cam_axis(cam_y, cam_vy, cam_focus_y());
}

/* [body_place] Põe o corpo no centro da célula 0, zerado, com a pose idle virada à direita, e encosta a câmera na linha dos pés com a mola parada. */
static void body_place(void)
{
    row_cell_x[0] = 0;
    row_cell_y[0] = 0;
    row_x[0] = ((WORLD_VIEW - WORLD_BODY_W) / 2) << WORLD_SUB;
    row_y[0] = ((WORLD_VIEW - WORLD_BODY_H) / 2) << WORLD_SUB;
    row_w[0] = WORLD_BODY_W;
    row_h[0] = WORLD_BODY_H;
    row_vx[0] = 0;
    row_vy[0] = 0;
    row_hit_left[0] = 0;
    row_hit_right[0] = 0;
    row_hit_floor[0] = 0;
    row_hit_ceil[0] = 0;
    row_jump_was[0] = 0;
    row_wall[0] = 0;
    row_face[0] = 1;
    row_clip[0] = BODY_CLIP_IDLE;
    row_frame[0] = 0;
    row_hold[0] = 0;
    row_act[0] = BODY_ACT_NONE;
    row_vx_was[0] = 0;
    *cam_ground = cam_feet();
    *cam_x = cam_focus_x();
    *cam_y = cam_focus_y();
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

/* [roll_span] Devolve uma largura que cabe no bolso. Uma em cada quatro usa o bolso inteiro. */
static int roll_span(int pocket)
{
    int w;

    if (pocket < 32) {
        return pocket;
    }
    if (roll_next() % 4 == 0) {
        return pocket;
    }
    w = roll_width(low_widths, 3);
    if (w > pocket) {
        w = pocket;
    }
    return w;
}

/* [solid_wall] Grava um muro de 16 px que desce até 24 px acima do chão. */
static void solid_wall(int index, int x)
{
    int top;

    top = roll_pick(WALL_TOP_LO, WALL_TOP_HI);
    solid_put(index, 0, 0, x, top, WALL_W, WALL_BOTTOM - top);
}

/* [solid_stack] Empilha três plataformas de 16 px no bolso, cada uma acima da anterior e fora do corredor. */
static void solid_stack(int index, int min_x, int max_edge)
{
    int pocket;
    int w;
    int x;
    int y;
    int gap;
    int i;

    pocket = max_edge - min_x;
    w = roll_span(pocket);
    x = roll_pick(min_x, max_edge - w);
    y = roll_pick(LOW_TOP_LO, LOW_TOP_HI);
    solid_put(index, 0, 0, x, y, w, PLAY_H);
    for (i = 1; i < STACK_N; i++) {
        gap = roll_pick(MID_GAP_LO, MID_GAP_HI);
        w = roll_span(pocket);
        x = solid_over(x, solid_w[index + i - 1], w, min_x, max_edge);
        y -= gap;
        if (y < STACK_TOP) {
            y = STACK_TOP;
        }
        solid_put(index + i, 0, 0, x, y, w, PLAY_H);
    }
}

/* [solid_roll] Sorteia os dois muros altos ao lado do corredor e três plataformas em cada bolso. */
static void solid_roll(void)
{
    int left_x;
    int right_x;

    left_x = roll_pick(WALL_X_L_LO, WALL_X_L_HI);
    right_x = roll_pick(WALL_X_R_LO, WALL_X_R_HI);
    solid_wall(1, left_x);
    solid_wall(2, right_x);
    solid_stack(3, 0, left_x);
    solid_stack(6, right_x + WALL_W, RIGHT_END);
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

/* [world_init] Monta o corpo no centro da célula 0, o chão, os muros e as plataformas na slice de [arena_level], e a câmera centrada com a mola parada. Devolve 0 se [arena_ready] servir. */
int world_init(void)
{
    arena_slice slice;
    int *rows;
    size_t need;

    if (!arena_ready()) {
        return 1;
    }
    slice = arena_world();
    need = (size_t)WORLD_CAP * (size_t)WORLD_COLS * sizeof(int) + 6u * sizeof(int);
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
    row_face = rows + WORLD_CAP * 14;
    row_clip = rows + WORLD_CAP * 15;
    row_frame = rows + WORLD_CAP * 16;
    row_hold = rows + WORLD_CAP * 17;
    row_act = rows + WORLD_CAP * 18;
    row_vx_was = rows + WORLD_CAP * 19;
    row_cell_x = rows + WORLD_CAP * 12;
    row_cell_y = rows + WORLD_CAP * 13;
    cam_x = rows + WORLD_CAP * WORLD_COLS;
    cam_y = cam_x + 1;
    cam_vx = cam_y + 1;
    cam_vy = cam_vx + 1;
    cam_ground = cam_vy + 1;
    roll_state = cam_ground + 1;
    *roll_state = 1;
    row_count = 1;
    if (solid_load() != 0) {
        return 1;
    }
    body_place();
    return 0;
}

/* [world_reset] Re-rola os dois muros e as seis plataformas, devolve o corpo ao centro da célula 0 e encosta a câmera. */
void world_reset(void)
{
    if (row_count < 1) {
        return;
    }
    solid_roll();
    body_place();
}

/* [world_tick] Integra um tick fixo; move_down solta o latch antes da corrida, a mola da câmera segue o meio horizontal e a linha do chão, e [body_pose] escolhe o clip. */
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
    body_pose();
}

/* [world_body_pose] Escreve o clip, o quadro e a face. Face -1 pede o flip. */
void world_body_pose(int *clip, int *frame, int *face)
{
    if (row_count < 1) {
        *clip = BODY_CLIP_IDLE;
        *frame = 0;
        *face = 1;
        return;
    }
    *clip = row_clip[0];
    *frame = row_frame[0];
    *face = row_face[0];
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
